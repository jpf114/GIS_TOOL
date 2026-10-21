#include "cutting_plugin.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/error.h>
#include <gis/core/runtime_env.h>
#include <gis/core/text_escape.h>
#include <gis/core/tile_grid.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <gdal_utils.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <cmath>
#include <fstream>
#include <chrono>
#include <iomanip>

namespace gis::plugins {

// skip_blank 的判空容差：不透明像元 **<= 此值** 才算空瓦片。
// WHY 阈值不是 0（这一条有意偏离计划 Step 3 的措辞，偏离理由=盘上事实）：
//   "0 个有效像元"这种瓦片已经被 renderTileRgba 末尾的"没有任何有效像元 ⇒ 不交付"守卫
//   **无条件**挡掉（R-T18-6 明令保留该语义；#298 的 z=11 只写 9 张、#310 的
//   tiles_generated=13/tiles_skipped=7 都把它钉成了与 skip_blank 无关的事实，改期望=GOLD 回归）。
//   于是把这里的阈值也写成 0，两个谓词就完全同判据 ⇒ isTileBlank 那条调用永不可真 ⇒
//   skip_blank 退化成"面板上能勾、CLI 能设、结果不变"的死键 —— 正是本任务要消灭的 N65 形态。
//   留一个 >0 的小容差，skip_blank 管的就正是"只有寥寥几个不透明像元"的近空瓦片
//   （判别用例 #315 的档 B：1×1 源栅格 ⇒ 4 个不透明像元 ⇒ true 跳过 / false 写出）。
// WHY 仍是 4 而不是按比例：改前实现就是 4（原 :660 `if (nonzero > 4) return false`），
//   本任务不动这条口径；要改成"占瓦片像元的百分比"属于口径变更，交控制器随 Task 21 定。
constexpr int kBlankOpaquePixelTolerance = 4;

std::vector<gis::framework::ParamSpec> CuttingPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "动作", "Cutting plugin action",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"clip", "mosaic", "split", "merge_bands", "tile"}
        },
        gis::framework::ParamSpec{
            "input", "输入栅格", "Primary raster input",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出路径", "Output raster or tile directory",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "extent", "范围", "Clip extent as xmin,ymin,xmax,ymax",
            gis::framework::ParamType::Extent, false, std::array<double,4>{0,0,0,0}
        },
        gis::framework::ParamSpec{
            "cutline", "裁切线", "Optional cutline vector path",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "tile_size", "分块大小", "Tile size used by split action",
            gis::framework::ParamType::Int, false, int{1024},
            int{1}, int{65536}
        },
        gis::framework::ParamSpec{
            "overlap", "重叠像素", "Overlap size used by split action",
            gis::framework::ParamType::Int, false, int{0},
            int{0}, int{65536}
        },
        gis::framework::ParamSpec{
            "bands", "波段列表", "Comma-separated band indexes",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "dst_srs", "目标坐标系", "Destination CRS for reprojection",
            gis::framework::ParamType::CRS, false, std::string{}
        },
        gis::framework::ParamSpec{
            "resample", "重采样", "Resampling method",
            gis::framework::ParamType::Enum, false, std::string{"nearest"},
            int{0}, int{0},
            {"nearest", "bilinear", "cubic", "cubicspline", "lanczos", "average"}
        },
        gis::framework::ParamSpec{
            "tile_scheme", "瓦片方案", "xyz or tms tile scheme",
            gis::framework::ParamType::Enum, false, std::string{"xyz"},
            int{0}, int{0},
            {"xyz", "tms"}
        },
        gis::framework::ParamSpec{
            "profile", "瓦片投影",
            "Only mercator is implemented; geodetic/raster fail explicitly (N48, batch B)",
            gis::framework::ParamType::Enum, false, std::string{"mercator"},
            int{0}, int{0},
            {"mercator", "geodetic", "raster"}
        },
        gis::framework::ParamSpec{
            "min_zoom", "最小缩放", "Minimum zoom level for tile output",
            gis::framework::ParamType::Int, false, int{0},
            int{0}, int{30}
        },
        gis::framework::ParamSpec{
            "max_zoom", "最大缩放", "Maximum zoom level for tile output",
            gis::framework::ParamType::Int, false, int{-1},
            int{-1}, int{30}
        },
        gis::framework::ParamSpec{
            "tile_pixel_size", "瓦片像素", "Tile pixel size",
            gis::framework::ParamType::Int, false, int{256},
            int{128}, int{1024}
        },
        gis::framework::ParamSpec{
            "overview_resampling", "金字塔重采样",
            "Resampling used to place source pixels into each tile image "
            "(nearest/bilinear/cubic/average); cubicspline/lanczos/mode fail explicitly",
            gis::framework::ParamType::Enum, false, std::string{"nearest"},
            int{0}, int{0},
            {"nearest", "bilinear", "cubic", "cubicspline", "lanczos", "average", "mode"}
        },
        gis::framework::ParamSpec{
            "output_format", "输出格式", "Tile image format",
            gis::framework::ParamType::Enum, false, std::string{"png"},
            int{0}, int{0},
            {"png", "jpeg", "webp", "gtiff"}
        },
        gis::framework::ParamSpec{
            "jpeg_quality", "JPEG质量", "JPEG/WebP quality 1-100",
            gis::framework::ParamType::Int, false, int{75},
            int{1}, int{100}
        },
        gis::framework::ParamSpec{
            "add_alpha", "添加Alpha", "Add alpha channel for transparent nodata",
            gis::framework::ParamType::Bool, false, bool{true}
        },
        gis::framework::ParamSpec{
            "nodata_value", "NoData值", "Output nodata value",
            gis::framework::ParamType::Double, false, double{-9999.0},
            double{-1e15}, double{1e15}
        },
        gis::framework::ParamSpec{
            "skip_blank", "跳过空瓦片", std::string("Skip tiles with at most ") + std::to_string(kBlankOpaquePixelTolerance) + " opaque pixels",
            gis::framework::ParamType::Bool, false, bool{true}
        },
        gis::framework::ParamSpec{
            "resume", "断点续跑", "Resume tiling when outputs already exist",
            gis::framework::ParamType::Bool, false, bool{false}
        },
        gis::framework::ParamSpec{
            "tile_extent", "瓦片范围", "Optional tile extent in WGS84",
            gis::framework::ParamType::Extent, false, std::array<double,4>{0,0,0,0}
        },
        gis::framework::ParamSpec{
            "webviewer", "Web预览", "Generate leaflet/openlayers preview",
            gis::framework::ParamType::Enum, false, std::string{"none"},
            int{0}, int{0},
            {"none", "leaflet", "openlayers", "all"}
        },
        gis::framework::ParamSpec{
            "title", "标题", "Title used by generated web preview",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "copyright", "版权信息", "Copyright text used by generated web preview",
            gis::framework::ParamType::String, false, std::string{}
        },
        // N37（R-T17-12）：原此处的 num_threads 声明后全文件零读取点，已在提交 236327d 删除。
        // 与已删的 georef x_res/y_res 同族"填了会被静默忽略"的形态。三判据（grep 全树只剩注释、
        // 不属任何 visibleKeys、删除后 #334 与 action_gold_cases 登记不变红）的实测出处：
        // 提交 236327d 本身 + 删除提交之后那次 Debug 全量的尾行 "100% tests passed, 0 tests failed
        // out of 607"（当时基线 607；批次 A 收尾时两棵树基线为 Debug 632 / Release 630）。
        // WHY 这里不留日志路径、也不指任何任务报告文件：build/ 与 .superpowers/sdd/ 都在忽略规则里
        // （.gitignore:2 的 build/、.superpowers/sdd/.gitignore:1 的 *），而那份报告当时根本没写出来
        // （实施代理交付 8 个提交后被账号配额截断）——读者永远拿不到的证据指针等于没有证据，
        // 所以判据与实测数字一律写进注释本身。
    };
}

gis::framework::Result CuttingPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string action = gis::framework::getParam<std::string>(params, "action", "");

    if (action == "clip")        return doClip(params, progress);
    if (action == "mosaic")      return doMosaic(params, progress);
    if (action == "split")       return doSplit(params, progress);
    if (action == "merge_bands") return doMergeBands(params, progress);
    if (action == "tile")        return doTile(params, progress);

    return gis::framework::Result::fail("Unknown action: " + action);
}

namespace {

gis::core::ProcessingMetadata buildMetadata(const std::string& input, GDALDataset* srcDs, const std::string& algorithm) {
    gis::core::ProcessingMetadata meta;
    meta.sourceFile = input;
    meta.sourceCrs = gis::core::getSRSWKT(srcDs);
    meta.processingAlgorithm = algorithm;
    return meta;
}

} // namespace

static std::vector<std::string> splitString(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, delim)) {
        auto start = token.find_first_not_of(" \t");
        auto end = token.find_last_not_of(" \t");
        if (start != std::string::npos) {
            tokens.push_back(token.substr(start, end - start + 1));
        }
    }
    return tokens;
}

gis::framework::Result CuttingPlugin::doClip(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string cutline = gis::framework::getParam<std::string>(params, "cutline", "");
    auto extentArr = gis::framework::getParam<std::array<double,4>>(params, "extent", {0,0,0,0});

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    bool hasExtent = extentArr[0] != 0 || extentArr[1] != 0 ||
                     extentArr[2] != 0 || extentArr[3] != 0;

    if (!hasExtent && cutline.empty()) {
        return gis::framework::Result::fail("Either extent or cutline must be specified for clip");
    }

    progress.onMessage("Clipping: " + input);
    progress.throwIfCancelled();
    progress.onProgress(0.1);

    if (!cutline.empty()) {
        std::vector<std::string> argStorage;
        argStorage.push_back("-cutline");
        argStorage.push_back(cutline);
        argStorage.push_back("-crop_to_cutline");

        std::vector<const char*> warpArgs;
        for (auto& a : argStorage) warpArgs.push_back(a.c_str());
        warpArgs.push_back(nullptr);

        GDALWarpAppOptions* warpOpts = GDALWarpAppOptionsNew(
            const_cast<char**>(warpArgs.data()), nullptr);
        if (!warpOpts) {
            return gis::framework::Result::fail("Failed to create warp options");
        }

        auto srcDS = gis::core::openRaster(input, true);
        GDALDatasetH srcHandle = static_cast<GDALDatasetH>(srcDS.get());
        int errCode = 0;
        GDALDatasetH dstHandle = GDALWarp(output.c_str(), nullptr,
            1, &srcHandle, warpOpts, &errCode);
        GDALWarpAppOptionsFree(warpOpts);

        if (!dstHandle || errCode) {
            return gis::framework::Result::fail("Clip by cutline failed: " + std::string(CPLGetLastErrorMsg()));
        }
        GDALClose(dstHandle);
    } else {
        std::vector<std::string> argStorage;
        argStorage.push_back("-projwin");
        argStorage.push_back(std::to_string(extentArr[0]));
        argStorage.push_back(std::to_string(extentArr[3]));
        argStorage.push_back(std::to_string(extentArr[2]));
        argStorage.push_back(std::to_string(extentArr[1]));

        std::vector<const char*> translateArgs;
        for (auto& a : argStorage) translateArgs.push_back(a.c_str());
        translateArgs.push_back(nullptr);

        GDALTranslateOptions* translateOpts = GDALTranslateOptionsNew(
            const_cast<char**>(translateArgs.data()), nullptr);
        if (!translateOpts) {
            return gis::framework::Result::fail("Failed to create translate options");
        }

        auto srcDS = gis::core::openRaster(input, true);
        int errCode = 0;
        GDALDatasetH dstHandle = GDALTranslate(output.c_str(),
            static_cast<GDALDatasetH>(srcDS.get()), translateOpts, &errCode);
        GDALTranslateOptionsFree(translateOpts);

        if (!dstHandle || errCode) {
            return gis::framework::Result::fail("Clip by extent failed: " + std::string(CPLGetLastErrorMsg()));
        }
        GDALClose(dstHandle);
    }

    {
        auto srcDs = gis::core::openRaster(input, true);
        auto meta = buildMetadata(input, srcDs.get(), "cutting.clip");
        gis::core::writeProcessingMetadata(output, meta);
    }

    progress.throwIfCancelled();

    progress.onProgress(1.0);
    progress.onMessage("Clip completed.");
    return gis::framework::Result::ok("Clip completed successfully", output);
}

gis::framework::Result CuttingPlugin::doMosaic(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string dstSrs = gis::framework::getParam<std::string>(params, "dst_srs", "");
    std::string resample = gis::framework::getParam<std::string>(params, "resample", "nearest");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    auto inputFiles = splitString(input, ',');
    if (inputFiles.size() < 2) {
        return gis::framework::Result::fail("At least 2 input files required for mosaic (comma-separated)");
    }

    progress.onMessage("Mosaicing " + std::to_string(inputFiles.size()) + " files...");
    progress.throwIfCancelled();
    progress.onProgress(0.1);

    std::vector<GDALDatasetH> srcHandles;
    std::vector<gis::core::GdalDatasetPtr> srcDatasets;

    for (auto& f : inputFiles) {
        auto ds = gis::core::openRaster(f, true);
        srcHandles.push_back(static_cast<GDALDatasetH>(ds.get()));
        srcDatasets.push_back(std::move(ds));
    }

    std::vector<std::string> argStorage;
    if (!dstSrs.empty()) {
        argStorage.push_back("-t_srs");
        argStorage.push_back(dstSrs);
    }
    argStorage.push_back("-r");
    argStorage.push_back(resample);

    std::vector<const char*> warpArgs;
    for (auto& a : argStorage) warpArgs.push_back(a.c_str());
    warpArgs.push_back(nullptr);

    GDALWarpAppOptions* warpOpts = GDALWarpAppOptionsNew(
        const_cast<char**>(warpArgs.data()), nullptr);
    if (!warpOpts) {
        return gis::framework::Result::fail("Failed to create warp options");
    }

    int errCode = 0;
    GDALDatasetH dstHandle = GDALWarp(output.c_str(), nullptr,
        static_cast<int>(srcHandles.size()), srcHandles.data(), warpOpts, &errCode);
    GDALWarpAppOptionsFree(warpOpts);

    if (!dstHandle || errCode) {
        return gis::framework::Result::fail("Mosaic failed: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALClose(dstHandle);

    {
        auto meta = buildMetadata(inputFiles[0], srcDatasets[0].get(), "cutting.mosaic");
        gis::core::writeProcessingMetadata(output, meta);
    }

    progress.throwIfCancelled();
    progress.onProgress(1.0);
    progress.onMessage("Mosaic completed.");
    return gis::framework::Result::ok("Mosaic completed successfully", output);
}

gis::framework::Result CuttingPlugin::doSplit(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int tileSize = gis::framework::getParam<int>(params, "tile_size", 1024);
    int overlap  = gis::framework::getParam<int>(params, "overlap", 0);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required (output directory prefix)");

    auto srcDS = gis::core::openRaster(input, true);
    int width  = srcDS->GetRasterXSize();
    int height = srcDS->GetRasterYSize();
    int bands  = srcDS->GetRasterCount();

    progress.onMessage("Splitting " + std::to_string(width) + "x" + std::to_string(height) +
                       " into " + std::to_string(tileSize) + "px tiles...");
    progress.throwIfCancelled();
    progress.onProgress(0.1);

    std::string outDir = output;
    namespace fs = std::filesystem;
    if (!fs::exists(outDir)) {
        fs::create_directories(outDir);
    }

    int tileIdx = 0;
    int totalTilesX = (width + tileSize - 1) / tileSize;
    int totalTilesY = (height + tileSize - 1) / tileSize;
    int totalTiles = totalTilesX * totalTilesY;

    for (int yOff = 0; yOff < height; yOff += tileSize) {
        for (int xOff = 0; xOff < width; xOff += tileSize) {
            int xSize = std::min(tileSize + overlap, width - xOff);
            int ySize = std::min(tileSize + overlap, height - yOff);

            std::ostringstream oss;
            oss << outDir << "/tile_" << (xOff / tileSize) << "_"
                << (yOff / tileSize) << ".tif";
            std::string tilePath = oss.str();

            std::vector<std::string> argStorage;
            argStorage.push_back("-srcwin");
            argStorage.push_back(std::to_string(xOff));
            argStorage.push_back(std::to_string(yOff));
            argStorage.push_back(std::to_string(xSize));
            argStorage.push_back(std::to_string(ySize));

            std::vector<const char*> translateArgs;
            for (auto& a : argStorage) translateArgs.push_back(a.c_str());
            translateArgs.push_back(nullptr);

            GDALTranslateOptions* translateOpts = GDALTranslateOptionsNew(
                const_cast<char**>(translateArgs.data()), nullptr);
            if (!translateOpts) continue;

            int errCode = 0;
            GDALDatasetH dstHandle = GDALTranslate(tilePath.c_str(),
                static_cast<GDALDatasetH>(srcDS.get()), translateOpts, &errCode);
            GDALTranslateOptionsFree(translateOpts);

            if (dstHandle) GDALClose(dstHandle);

            gis::core::writeProcessingMetadata(tilePath, buildMetadata(input, srcDS.get(), "cutting.split"));

            tileIdx++;
            double pct = 0.1 + 0.9 * static_cast<double>(tileIdx) / totalTiles;
            progress.throwIfCancelled();
            progress.onProgress(pct);
        }
    }

    progress.throwIfCancelled();

    progress.onProgress(1.0);
    progress.onMessage("Split completed: " + std::to_string(tileIdx) + " tiles created.");
    return gis::framework::Result::ok(
        "Split completed: " + std::to_string(tileIdx) + " tiles", outDir);
}

gis::framework::Result CuttingPlugin::doMergeBands(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string bandsStr = gis::framework::getParam<std::string>(params, "bands", "");

    if (output.empty()) return gis::framework::Result::fail("output is required");

    std::vector<std::string> bandFiles;
    if (!input.empty()) {
        auto inputFiles = splitString(input, ',');
        bandFiles.insert(bandFiles.end(), inputFiles.begin(), inputFiles.end());
    }
    if (!bandsStr.empty()) {
        auto extraBandFiles = splitString(bandsStr, ',');
        bandFiles.insert(bandFiles.end(), extraBandFiles.begin(), extraBandFiles.end());
    }

    if (bandFiles.empty()) {
        return gis::framework::Result::fail("At least one input file required (use --input or --bands)");
    }

    progress.onMessage("Building VRT from " + std::to_string(bandFiles.size()) + " files...");
    progress.throwIfCancelled();
    progress.onProgress(0.1);

    std::vector<GDALDatasetH> srcDSVec;
    std::vector<std::string> srcPaths = bandFiles;
    for (auto& f : srcPaths) {
        GDALDatasetH ds = GDALOpen(f.c_str(), GA_ReadOnly);
        if (!ds) {
            for (auto& d : srcDSVec) GDALClose(d);
            return gis::framework::Result::fail("Cannot open file: " + f);
        }
        srcDSVec.push_back(ds);
    }

    std::vector<const char*> srcNamePtrs;
    for (auto& f : srcPaths) srcNamePtrs.push_back(f.c_str());
    srcNamePtrs.push_back(nullptr);

    const char* vrtArgv[] = {"-separate", nullptr};
    GDALBuildVRTOptions* vrtOpts = GDALBuildVRTOptionsNew(
        const_cast<char**>(vrtArgv), nullptr);
    if (!vrtOpts) {
        for (auto& d : srcDSVec) GDALClose(d);
        return gis::framework::Result::fail("Failed to create VRT options");
    }

    int usageError = 0;
    GDALDatasetH vrtDS = GDALBuildVRT("/vsimem/band_merge.vrt",
        static_cast<int>(srcDSVec.size()),
        srcDSVec.data(),
        srcNamePtrs.data(),
        vrtOpts, &usageError);
    GDALBuildVRTOptionsFree(vrtOpts);

    for (auto& d : srcDSVec) GDALClose(d);

    if (!vrtDS) {
        return gis::framework::Result::fail("Failed to build VRT: " + std::string(CPLGetLastErrorMsg()));
    }

    progress.throwIfCancelled();

    progress.onProgress(0.4);
    progress.onMessage("Converting VRT to GeoTIFF...");

    GDALTranslateOptions* translateOpts = GDALTranslateOptionsNew(nullptr, nullptr);
    if (!translateOpts) {
        GDALClose(vrtDS);
        return gis::framework::Result::fail("Failed to create translate options");
    }

    int errCode = 0;
    GDALDatasetH dstHandle = GDALTranslate(output.c_str(), vrtDS, translateOpts, &errCode);
    GDALTranslateOptionsFree(translateOpts);
    GDALClose(vrtDS);

    if (!dstHandle || errCode) {
        return gis::framework::Result::fail("Band merge failed: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALClose(dstHandle);

    {
        auto firstDs = gis::core::openRaster(bandFiles[0], true);
        auto meta = buildMetadata(bandFiles[0], firstDs.get(), "cutting.merge_bands");
        gis::core::writeProcessingMetadata(output, meta);
    }

    progress.throwIfCancelled();
    progress.onProgress(1.0);
    progress.onMessage("Band merge completed.");
    return gis::framework::Result::ok("Band merge completed successfully", output);
}

static double mercatorResolution(int z, int tileSize) {
    return 2.0 * M_PI * 6378137.0 / (tileSize * static_cast<double>(1 << z));
}

static int computeMaxZoom(double pixelSize, int tileSize) {
    for (int z = 30; z >= 0; --z) {
        if (mercatorResolution(z, tileSize) >= pixelSize * 0.5) return z;
    }
    return 0;
}

// R-T17-7 第 4 点：doTile 的瓦片索引范围改为"直接从 EPSG:3857 的度量范围算"，不再
// 经度分往返；这两个函数**保留**（签名与其他潜在调用路径共用 static 工具面），
// 加 [[maybe_unused]] 免得编译器按未引用局部函数报警——本批不改构建面。
[[maybe_unused]] static void lonLatToTileXYZ(double lon, double lat, int z, int& x, int& y) {
    x = static_cast<int>(std::floor((lon + 180.0) / 360.0 * (1 << z)));
    const double latRad = lat * M_PI / 180.0;
    y = static_cast<int>(std::floor((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * (1 << z)));
    x = std::max(0, std::min(x, (1 << z) - 1));
    y = std::max(0, std::min(y, (1 << z) - 1));
}

[[maybe_unused]] static void tileXYToLonLat(int x, int y, int z, double& lon, double& lat) {
    const double n = M_PI * (1.0 - 2.0 * y / static_cast<double>(1 << z));
    lon = x / static_cast<double>(1 << z) * 360.0 - 180.0;
    lat = 180.0 / M_PI * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
}

// R-T17-7 第 4 点的输入侧换算：tile_extent 按 spec 是 WGS84 经纬度矩形
// （"Optional tile extent in WGS84"），索引既然只从度量范围推导，用户给的经纬度
// 必须先**前向投影**进 EPSG:3857（与 mercatorTileGrid 共用同一条 Web Mercator 定义），
// 纬度截到 ±85.05112878（口径照旧 :820-821）。
static void lonLatToMercatorXY(double lon, double lat, double& x, double& y) {
    constexpr double kHalf = 20037508.342789244;
    const double clampedLat = std::clamp(lat, -85.05112878, 85.05112878);
    const double rad = clampedLat * M_PI / 180.0;
    x = std::clamp(lon, -180.0, 180.0) * kHalf / 180.0;
    y = std::clamp(6378137.0 * std::log(std::tan(M_PI / 4.0 + rad / 2.0)), -kHalf, kHalf);
}

// R-T17-11 处置①：overview_resampling 驱动逐瓦片"源窗口 → 瓦片缓冲"的 cv::resize 插值。
// 只映射 cv::resize 存在严格对应项的四个取值；cubicspline/lanczos/mode 显式拒绝
// （TileRejectsOverviewResamplingWithoutResizeEquivalent 用例钉"失败必须点名取值"）。
// WHY 拒绝而不是猜一个近似项：cv::resize 没有这三种的对应实现，替用户挑一个就是在
// 面板上放一个"改了等于没改"的假选项，正是本批次要消灭的形态。
// WHY 映射表放插件本地而不是公共 API：这是 tile 动作的产物实现细节，
// 与 tile_grid.h 描述的世界网格几何不是一类东西，不为它开导出面。
static bool mapOverviewResampling(const std::string& value, int& interpolation) {
    if (value == "nearest") { interpolation = cv::INTER_NEAREST; return true; }
    if (value == "bilinear") { interpolation = cv::INTER_LINEAR; return true; }
    if (value == "cubic") { interpolation = cv::INTER_CUBIC; return true; }
    if (value == "average") { interpolation = cv::INTER_AREA; return true; }
    return false;
}

// R-T17-4：warped 临时栅格必须在**所有**退出路径上关闭并删除——正常收尾、
// Result::fail 提前返回、以及 throwIfCancelled 抛异常展开。修正前只有函数末尾
// :999-1002 的正常路径清理，取消路径把 gis_tile_warped.tif 直接漏在系统临时目录
// （Task 17 的 pre-fix RED 用例 TileCleansWarpedTempOnCancel 实测到了残留文件）。
// RAII 把三条路径收敛到一个清理点；文件名带 pid（CPLGetPID）隔离跨进程，
//   进程内的"每次调用唯一"由 Task B6 / m-4 补上（见 doTile 里拼 warped 临时路径那一段的
//   静态原子序号），临时目录归划仍留给 Plan 3 的 C8 统一处理。
// WHY 定义全部写进类体：批次 A 实测过"匿名命名空间内的成员函数定义 ⇒ 内部链接
// ⇒ LNK2019"的坑，内联定义从根上避开。
struct WarpedRasterCloser {
    std::string path;
    void operator()(GDALDataset* ds) const {
        if (ds) {
            GDALClose(GDALDataset::ToHandle(ds));
        }
        if (!path.empty()) {
            // F8（修复轮 2）：清理器里的 remove **必须**用非抛出重载 `remove(p, ec)`。
            // WHY 1（无条件成立）：本 operator() 只经 std::unique_ptr 的析构被调用，
            //   而 `~unique_ptr()` 在标准里是 noexcept；noexcept 函数内抛出 ⇒ 直接
            //   std::terminate，异常类型根本来不及被上层 catch。
            // WHY 2（更常见的那条路）：取消路径上 progress.throwIfCancelled() 正在抛出，
            //   栈展开中销毁 warped ⇒ deleter 处于"异常已 in flight"的状态，此时再抛
            //   一个异常就是 std::terminate。两种场景的终态相同，都不可恢复。
            // 抛出口是真实的：remove(const path&) 用 `filesystem::filesystem_error`
            //   报告权限/占用（杀毒、资源管理器句柄、临时目录只读、ACL 拒绝）等 OS 错误，
            //   而这些恰是"取消/失败收尾"时最容易同时发生的。
            // ec 为何可以忽略：deleter 没有错误通道（返回值无人接收，抛又不能抛），
            //   且 warped 是纯中间产物——没删掉的代价只是一次临时文件占用，不影响
            //   任何交付结果；远小于 terminate 的代价。文件名带 pid 保证下一轮运行不会
            //   与本次泄漏互踩；跨进程/重启后的临时目录回收仍属 Plan 3 的 C8。
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
};

// 某层级的源包络索引范围（闭区间）与该层级瓦片数，由度量范围 floor 推导。
struct LevelBounds {
    int xMin = 0, xMax = 0, yMin = 0, yMax = 0;
    int tiles = 0;
};

static std::string tileFileExt(const std::string& fmt) {
    if (fmt == "jpeg") return ".jpg";
    if (fmt == "webp") return ".webp";
    if (fmt == "gtiff") return ".tif";
    return ".png";
}

// 一个待输出波段的拉伸参数（整幅统计出来的 min/max）。
// WHY 不用计划 Step 3 片段写的 std::vector<cv::Range>（R-T18-2）：cv::Range 是**窗口索引
//   区间**（start/end 都是 int），拿它装 double 的 (min,max) 既有 double→int 收窄
//   （MSVC C4244，本批次红线是零 warning）语义也是错的；计划自己的注释还前后矛盾
//   （上一行写"以 cv::Scalar2d 形式保存"，下一行代码却是 cv::Range）。
//   这里用小 struct 而不是 std::pair：字段名 minV/maxV 在 stretchByte 里自解释，
//   且加 clamp/兜底时不会退化成 first/second 的无名字段。
struct BandStretch {
    double minV = 0.0;
    double maxV = 0.0;
};

static bool isTileBlank(const cv::Mat& rgba) {
    if (rgba.channels() < 4) return false;
    int nonzero = 0;
    for (int y = 0; y < rgba.rows; ++y) {
        const uchar* row = rgba.ptr<uchar>(y);
        for (int x = 0; x < rgba.cols; ++x) {
            if (row[x * 4 + 3] > 0) {
                ++nonzero;
                if (nonzero > kBlankOpaquePixelTolerance) return false;
            }
        }
    }
    return nonzero <= kBlankOpaquePixelTolerance;
}

// 拉伸：把一个波段的源值映到 0..255。
// WHY 兜底 range=1.0 照搬改前 :1157（**常数波段映射成 0=黑**），本任务不改这条口径，
//   否则 #315 档 B 那种"整幅单值"的瓦片亮度会莫名其妙翻面。
// WHY clamp：改前 `static_cast<uchar>(double)` 对越界浮点是**未定义行为**
//   （MSVC 实测截成 0 或 0x80000000 的低字节），clamp 之后越界才是可预期的黑。
//   诚实登记（复审 M9 + 报告 S6）：本批实测"去掉 clamp ⇒ 26/26 全绿"⇒ 它在现有夹具下
//   **不承重**（min/max 与参与换算的 v 来自同一批像元时 scaled 恒在 [0,255]），
//   留着它是防"区间来自一批、v 来自另一批"的那种实现错误，不拿它当判别目标。
//   更正记录（复审 M-16，fix round 2 F-1 同段代码）：此处原文写"add_alpha=true 时
//   NoData 原值（-9999 这类）会参与这条换算 ⇒ scaled 可能远小于 0"，机理**写错了**——
//   真实数据上参与统计的是**写入值 0**（源 refas.tif 的 NoData=-32768 并没进 min，
//   否则输出会被挤到 247..255，实测最大 166），而 F-1 之后这类像元已被 alpha 掩膜
//   滤出整幅统计（见 computeSceneBandStretch 的 F-1 段）。
//   仍然成立的那一半：多波段源里"某波段局部 NoData、其余波段有效"的像元，其色波段值
//   会带着 -dstalpha 剥掉 NODATA 元数据后的**写入值**进这条换算（该像元 alpha>0 ⇒
//   F-1 的掩膜滤不掉它）⇒ #315 洞带绿道为 0 钉的就是这件事。
static uchar stretchByte(double v, const BandStretch& s) {
    const double range = (s.maxV - s.minV > 1e-9) ? (s.maxV - s.minV) : 1.0;
    const double scaled = 255.0 * (v - s.minV) / range;
    return static_cast<uchar>(std::clamp(scaled, 0.0, 255.0));
}

// R-T18-6：拉伸参数按**整幅 warpedDS**统计一次，所有层级、所有瓦片共用。
// WHY 不是 Produces 原措辞的"每层级一次"：R-T17-7 删掉"每层级物化全球网格"之后，
//   层级只剩索引网格、**不再是可统计的数据对象**；要按层级统计就得按该层分辨率把整幅
//   包络重读一遍，代价与语义都不划算。整幅一次是原设计的**超集**（连层级之间都一致），
//   实现的正是承诺的那件事：同一地物跨瓦片/跨层级亮度一致。
//   判别用例 #312 TileStretchIsWholeSceneWideNotPerTile；变异取证（R3a 那一组：把整幅 stretch 换成
//   "helper 内先算 tileStretches 再取代 in.stretches"的提变量形态）实测 **killed**，红点正是 #312 的
//   东列三条断言（同轮另一条 R3b 另杀 #298/#310/#315，不是这条变异的功劳）。这条偏离已登记进 Task 21 的口径同步清单。
// WHY 用 ComputeStatistics 的**精算**（形参是 GDAL 的 bApproxOK，传 false ⇒ 不许近似；
//   复审 M-6 指出这里的旧注释把名称与数值都写错过一次）而不是把整幅读进 cv::Mat 再
//   cv::minMaxIdx：前者按块流式扫描，warped 临时栅格多大都不额外占内存；后者按幅吃内存。
//   精算的理由：近似统计走概视图/采样会漏极值，而"亮度一致"正是本任务要承诺的性质。
//   NoData 像元不参与统计：GDAL 会跳过该波段声明的 NODATA 值 ⇒ 拉伸区间只由有效像元决定
//   （这也是"整幅统计"能替掉"逐瓦片统计"的前提）。
//
// F-1（fix round 2 / 复审 I-1）：**add_alpha=true 时必须按 alpha 掩膜过滤统计**。
//   盘上事实（复审实测，真实数据 data/dem/refas.tif，同一张瓦 8/207/96.tif，gdalinfo -stats）：
//     add_alpha=true  Band1 Min=0.000 Max=166.000 Mean=7.489 StdDev=30.818
//     add_alpha=false Band1 Min=0.000 Max=151.000 Mean=6.340 StdDev=26.234
//     源 refas.tif Band1 Int16 Min=391 Max=2729 NoData=-32768
//   机理：`-dstalpha` 把"NoData 区"改由末尾 alpha 波段表达，因此**不再给色波段写 NODATA
//     元数据**（S9 的盘上事实）⇒ 色波段上被遮掉的像元以**写入值 0**（不是源 NoData 原值）
//     进整幅统计 ⇒ 下界被 0 顶掉：同一个 v=1776 映射成 255*1776/2729=165.9 而不是
//     255*(1776-391)/2338=151.2，与实测 166/151 **逐字节吻合**。
//   后果：值域整体偏正的栅格（DEM/温度/深度）低段动态范围被挤掉；值域整体偏负时 0 变成
//     **上界** ⇒ 反差直接反掉。而 add_alpha 的默认值是 **true** ⇒ 这正是**默认路径**，
//     Produces 承诺的"同一地物亮度一致"在它上面是假的。
//   处置取工单裁定的 (a)：**在同一次全幅 pass 内**逐块同读 alpha 波段，只对 `alpha > 0` 的
//     像元累积 (min,max)；判据与 renderTileRgba 的不透明判据 `ar[px] > 0.0f` **同一条**，
//     不留两套口径。代价＝多扫一遍 alpha 波段（原来 n 个色波段各扫一遍，现在 n+1 遍），
//     **不加第二遍循环**（工单明令）。
//   全幅 alpha 皆为 0 ⇒ 一条有效像元都没见到 ⇒ 留 (0,0)，即"该波段无有效数据"的既有口径；
//     绝不产出 min>max 的脏区间（常数波段同样留 (0,0)，见下面的 maxAcc > minAcc 判据）。
//   判别用例 #319 TileColorBandsDoNotDependOnAddAlpha（"同一含 NoData 的单波段源，
//     add_alpha 两档的颜色波段逐像元相同"）；把这条掩膜过滤退回"不过滤"的变异（字面 `<= 0.0f`⇒`<= -1.0f`
//     与同判据提变量两种形态）实测**都只红 #319 一条**：不透明像元 min 由 0 变 63（=255·50/200，
//     下界被 0 顶掉的算术预言值）、两档不等像元数由 0 变 5565（53 列 × 105 行）。
// 统计失败或退化（空数据集 / 全 NoData）时退回 (0,0)：配合 range=1.0 兜底与瓦片级
//   "无有效像元 ⇒ 不交付"守卫，这种数据集一张瓦片都不会写出去，不会产生脏值。
static std::vector<BandStretch> computeSceneBandStretch(GDALDataset* warpedDS, int bandCount,
                                                        int alphaBandIndex) {
    std::vector<BandStretch> stretches;
    stretches.reserve(std::max(0, bandCount));
    for (int b = 1; b <= bandCount; ++b) stretches.emplace_back();
    if (!warpedDS || bandCount < 1) return stretches;

    GDALRasterBand* alphaBand =
        alphaBandIndex > 0 ? warpedDS->GetRasterBand(alphaBandIndex) : nullptr;
    if (!alphaBand) {
        // 没有 alpha 掩膜可依据 ⇒ 走 GDAL 自己的精算（它会跳过该波段**声明的** NODATA），
        // 语义与 F-1 之前逐字相同 ⇒ add_alpha=false 那一档的产物一个字节都不动
        // （GOLD #296/#298 就站在这条上）。alphaBandIndex>0 却取不到波段属防御臂。
        for (int b = 1; b <= bandCount; ++b) {
            GDALRasterBand* band = warpedDS->GetRasterBand(b);
            if (!band) continue;
            double minV = 0.0, maxV = 0.0, meanV = 0.0, stdDevV = 0.0;
            if (band->ComputeStatistics(false, &minV, &maxV, &meanV, &stdDevV, nullptr, nullptr) != CE_None)
                continue;
            if (!(maxV > minV)) continue;   // NaN / 常数波段的退化臂：保留 (0,0) 兜底
            stretches[static_cast<size_t>(b) - 1].minV = minV;
            stretches[static_cast<size_t>(b) - 1].maxV = maxV;
        }
        return stretches;
    }

    // 按块扫全幅**一遍**：每块先读 alpha，再逐色波段读同一块，只有 alpha>0 的像元进累积。
    // WHY 分块：与上面"按块流式扫描、不按幅吃内存"同一条口径（两个 float 缓冲 ≈ 512 KB）。
    const int nX = warpedDS->GetRasterXSize();
    const int nY = warpedDS->GetRasterYSize();
    if (nX < 1 || nY < 1) return stretches;   // 空数据集 ⇒ 全部留 (0,0)
    constexpr int kStatChunkPixels = 1 << 16;                        // 65536 像元/块
    const int chunkCols = std::min(nX, 4096);
    const int chunkRows = std::max(1, kStatChunkPixels / std::max(1, chunkCols));
    const size_t chunkCapacity = static_cast<size_t>(chunkCols) * static_cast<size_t>(chunkRows);
    std::vector<float> alphaBuf(chunkCapacity, 0.0f);
    std::vector<float> valueBuf(chunkCapacity, 0.0f);
    std::vector<double> minAcc(static_cast<size_t>(bandCount), 0.0);
    std::vector<double> maxAcc(static_cast<size_t>(bandCount), 0.0);
    std::vector<char> seen(static_cast<size_t>(bandCount), 0);

    for (int y0 = 0; y0 < nY; y0 += chunkRows) {
        const int ny = std::min(chunkRows, nY - y0);
        for (int x0 = 0; x0 < nX; x0 += chunkCols) {
            const int nx = std::min(chunkCols, nX - x0);
            const size_t pixels = static_cast<size_t>(nx) * static_cast<size_t>(ny);
            if (alphaBand->RasterIO(GF_Read, x0, y0, nx, ny, alphaBuf.data(), nx, ny,
                                    GDT_Float32, 0, 0) != CE_None)
                return stretches;   // 读失败 ⇒ 退回 (0,0) 口径（与统计失败同一条），不猜区间
            for (int b = 1; b <= bandCount; ++b) {
                GDALRasterBand* band = warpedDS->GetRasterBand(b);
                if (!band) return stretches;
                if (band->RasterIO(GF_Read, x0, y0, nx, ny, valueBuf.data(), nx, ny,
                                   GDT_Float32, 0, 0) != CE_None)
                    return stretches;
                for (size_t i = 0; i < pixels; ++i) {
                    if (alphaBuf[i] <= 0.0f) continue;   // 被 -dstalpha 遮掉的像元**不是数据**
                    const double v = valueBuf[i];
                    const size_t k = static_cast<size_t>(b) - 1;
                    if (!seen[k]) { seen[k] = 1; minAcc[k] = v; maxAcc[k] = v; }
                    else {
                        if (v < minAcc[k]) minAcc[k] = v;
                        if (v > maxAcc[k]) maxAcc[k] = v;
                    }
                }
            }
        }
    }
    for (int b = 1; b <= bandCount; ++b) {
        const size_t k = static_cast<size_t>(b) - 1;
        if (!seen[k]) continue;                    // 全幅 alpha 皆 0 ⇒ 该波段无有效数据 ⇒ 留 (0,0)
        if (!(maxAcc[k] > minAcc[k])) continue;    // 常数波段：与上面精算分支同一条退化臂
        stretches[k].minV = minAcc[k];
        stretches[k].maxV = maxAcc[k];
    }
    return stretches;
}

// Task 18 S0（复审建议）：doTile 的"每瓦片渲染段"（改前 cutting_plugin.cpp:1118-1171）
// 原样抽进这一个函数；S1..S4 在同一处换成多波段 + 整幅拉伸 + 真 alpha。
// WHY 先抽再加：Task 17 之后 doTile 已有 453 行，网格反算、波段读取、拉伸、alpha、
//   编码、计数六件事挤在三重循环里的一处；本任务要在**同一处**加多波段与真 alpha，
//   就地堆叠会让"渲染语义"和"网格语义"再也分不开。抽出后 doTile 只留网格与计数。
// WHY 传引用出参而不是返回值：rgba 是 tileSize×tileSize×4 的缓冲（256 档 256KB），
//   按值返回在 MSVC 下多一次深拷贝，而这个函数每张瓦片调一次。
// 返回 false = 这张瓦片**没有可交付内容**：源读失败，或者没有任何有效像元可上色
//   （后者是原 :1151 `bandMin > bandMax` 那条守卫的语义，R-T18-6 要求保留）。
//   两个不可交付分支在调用方合并成同一条跳过臂：改前它们是两条 continue，但动作完全
//   相同（++tilesSkipped; ++tilesProcessed; continue），合并后 F6 计数口径不变。
struct TileRenderInput {
    GDALDataset* warpedDS = nullptr;
    int srcX = 0, srcY = 0, srcW = 0, srcH = 0;   // warped 数据集上的整数读窗
    int dstX = 0, dstY = 0, dstW = 0, dstH = 0;   // 瓦片内的目标子矩形（子像素放置）
    int tileSize = 256;
    int interpolation = cv::INTER_LINEAR;         // overview_resampling 映射出的插值（R-T18-8）
    bool addAlpha = true;
    int colorBands = 1;                           // 参与上色的波段数 1..4（不含 -dstalpha 那条）
    int alphaBandIndex = 0;                       // warped 里 -dstalpha 波段的 1-based 号，0=没有
    // 三个数组按 1-based 波段号访问，所以**按值**装进结构体（不用指针：省掉空指针那一类
    // 防御臂，也不用 const&：那会让结构体不能逐瓦片临时构造）。长度 ≤ 4，相对下面
    // tileSize² 的逐像元循环，每瓦片三次小堆拷贝的成本可以忽略。
    std::vector<double> bandNodata;               // 1-based：bandNodata[b-1]（未声明时取 nodata_value）
    std::vector<char> bandHasNodata;              // 1-based：该波段是否声明了 NODATA
    std::vector<BandStretch> stretches;           // 1-based：整幅统计出的 (min,max)（R-T18-6）
};

static bool renderTileRgba(const TileRenderInput& in, cv::Mat& rgba) {
    // 波段数按**每个数组各自钳位**，钳到的是该数组的**原始长度**（fix round 2 F-2 / 复审 I-2）。
    // 这段钳位兑现的唯一保证是"三个数组长度不一致或为空时**不越界读**"；
    // **可达性由调用方保证**（doTile 恒让三者长度 == colorBands ≥ 1，正常路径下这几行是空操作），
    // 长度不一致时这里只保证不崩、**不保证结果正确** ⇒ 不拿它当判别目标。
    // WHY 改前那句"越界即缩到 1 / 不可能越界"是**谎话**（复审 §I-2）：三重 std::min 之后紧跟的
    //   `bands = std::max(1, bands)` 会把 bands **抬回零长数组之上**（size()==0 时 bands 仍是 1），
    //   于是下面按 `in.stretches[b-1]` 取区间的那行直接越界读。复审实测：
    //   统计循环写成 `b <= bandCount - 1` ⇒ stretches 变空 ⇒ 9 条单波段用例 Exit code 0xc0000409。
    // WHY 不是控制器给的 `std::min(bands, std::max<size_t>(1, size()))` 形态（同一条 F-2 的盘上对照）：`max<size_t>(1, 0)==1` ⇒ 空数组仍被钳到 1 ⇒ `[0]` 照旧越界 ⇒ 照旧崩；
    //   钳到原始长度 + 显式 `bands < 1 ⇒ return false` 才真的防得住（崩溃变成"该瓦片不交付"）。
    int bands = std::min(std::max(1, in.colorBands), 4);
    bands = std::min(bands, static_cast<int>(in.stretches.size()));
    bands = std::min(bands, static_cast<int>(in.bandNodata.size()));
    bands = std::min(bands, static_cast<int>(in.bandHasNodata.size()));
    if (bands < 1) return false;   // 没有任何长度≥1 的数组 ⇒ 没有可上色波段：不交付，也不越界读
    const cv::Rect target(in.dstX, in.dstY, in.dstW, in.dstH);

    // 逐波段：读 → 缩放（R-T18-8：每波段用**同一个** tileInterpolation）→ 放置到子矩形，
    // 同时按该波段自己的 NODATA 建有效掩膜。改前只有 band 1 走这条路（R-T17-5 的红线：
    // 多波段是 Task 18 的地盘），垫边值 srcNodata 的口径在这里按波段展开。
    std::vector<cv::Mat> values;
    std::vector<cv::Mat> bandValid;
    values.reserve(bands);
    bandValid.reserve(bands);
    for (int b = 1; b <= bands; ++b) {
        const double nd = in.bandNodata[static_cast<size_t>(b) - 1];
        values.emplace_back(in.tileSize, in.tileSize, CV_32F, cv::Scalar(static_cast<float>(nd)));
        bandValid.emplace_back(in.tileSize, in.tileSize, CV_8UC1, cv::Scalar(0));

        GDALRasterBand* band = in.warpedDS ? in.warpedDS->GetRasterBand(b) : nullptr;
        if (!band) return false;
        cv::Mat block(in.srcH, in.srcW, CV_32F);
        if (band->RasterIO(GF_Read, in.srcX, in.srcY, in.srcW, in.srcH,
                           block.data, in.srcW, in.srcH, GDT_Float32, 0, 0) != CE_None)
            return false;
        cv::resize(block, block, cv::Size(in.dstW, in.dstH), 0, 0, in.interpolation);
        block.copyTo(values[static_cast<size_t>(b) - 1](target));

        cv::Mat validRect = bandValid[static_cast<size_t>(b) - 1](target);
        validRect.setTo(255);   // 覆盖子矩形内默认有效（改前的 coverMask 那一臂）
        if (in.bandHasNodata[static_cast<size_t>(b) - 1]) {
            // 判据照搬改前 :1145：|v - nodata| < 1e-6（不是 ==，浮点声明值常有末位差）
            const float ndf = static_cast<float>(nd);
            for (int py = 0; py < validRect.rows; ++py) {
                const float* vrow = values[static_cast<size_t>(b) - 1].ptr<float>(target.y + py);
                uchar* mrow = validRect.ptr<uchar>(py);
                for (int px = 0; px < validRect.cols; ++px)
                    if (std::abs(vrow[target.x + px] - ndf) < 1e-6) mrow[px] = 0;
            }
        }
    }

    // alpha / 有效像元（S3 + S9，N65 在这里落地）：两条来源按 add_alpha 分派。
    //  add_alpha=true  ⇒ 读 GDALWarp 的 **-dstalpha 波段**。S9 的盘上事实：这条选项把
    //    "NoData 区"改由末尾 alpha 波段表达，并因此不再给色波段写 NODATA 元数据
    //    ⇒ 改前只信 GetNoDataValue 的代码在 add_alpha=true 时 NoData 判据整体失效，
    //    整幅都是 NoData 的瓦片被当成"有数据"，写成一张不透明黑瓦（RED 取证 #315 档 A
    //    variant=1/3）。dstalpha 的口径是"源像元里**只要有一个**波段有效就不透明"，
    //    所以某一波段的局部空洞不会被放大成整孔。
    //  add_alpha=false ⇒ 没有 dstalpha 波段 ⇒ 逐波段 NODATA 判据取 **AND**（任一波段
    //    NoData 即遮掉）。单波段输入下 AND 与改前判据逐像元相同 ⇒ #296 的覆盖缘列 104/105
    //    硬边与 #298 的东端 NoData 带透明都一字不动地保住（GOLD）。
    //  ⇒ add_alpha 的可见差别只出现在"多波段且只有部分波段 NoData"的源上：这也正是
    //    N65 能藏到今天的机理（单波段下两个分支恒等，而改前所有用例都用单波段源）。
    cv::Mat alphaValid(in.tileSize, in.tileSize, CV_8UC1, cv::Scalar(0));
    if (in.addAlpha && in.alphaBandIndex > 0) {
        GDALRasterBand* aBand = in.warpedDS->GetRasterBand(in.alphaBandIndex);
        if (!aBand) return false;
        cv::Mat aBlock(in.srcH, in.srcW, CV_32F);
        if (aBand->RasterIO(GF_Read, in.srcX, in.srcY, in.srcW, in.srcH,
                             aBlock.data, in.srcW, in.srcH, GDT_Float32, 0, 0) != CE_None)
            return false;
        cv::resize(aBlock, aBlock, cv::Size(in.dstW, in.dstH), 0, 0, in.interpolation);
        for (int py = 0; py < in.dstH; ++py) {
            const float* ar = aBlock.ptr<float>(py);
            uchar* mr = alphaValid.ptr<uchar>(target.y + py);
            // 取 >0 而不是 >=255：插值后的中间值仍代表"这里源上有数据"，
            // 只有真无数据才是 0 ⇒ 不把抗锯齿边误判成洞。
            for (int px = 0; px < in.dstW; ++px) mr[target.x + px] = (ar[px] > 0.0f) ? 255 : 0;
        }
    } else {
        alphaValid = bandValid[0];
        for (int b = 1; b < bands; ++b)
            cv::bitwise_and(alphaValid, bandValid[static_cast<size_t>(b) - 1], alphaValid);
    }

    // 通道放置口径（R-T18-4(ii) 要求写清"哪两个通道该相等"不是猜的）：
    //   矩阵按 OpenCV 约定是 CV_8UC4 = 内存序 (B,G,R,A)；cv::imwrite 写 PNG/WebP 时转成
    //   文件序 (R,G,B,A)，而 GDAL 的驱动也按 (R,G,B,A) 暴露波段 ⇒ **openRaster 的波段号 i
    //   就是图像的第 i 个通道**（1=R、2=G、3=B、4=A），用例侧不再做第二次翻转。
    //   映射式：bandForChannel(c) = min(c+1, bands)，c=0,1,2 依次是 R,G,B ⇒
    //     1 波段 → R=G=B=band1（灰度，#313 锁这条口径；容器仍是 4 波段，出处是 GOLD #298
    //       的 ASSERT_EQ(ds->GetRasterCount(), 4)）
    //     2 波段 → R=b1, G=B=b2（缺口用最后一个波段补齐，与"1 波段→灰度"是同一条规则；
    //       没有波段被丢弃。2 波段没有公认语义，本任务不为此发明新口径）
    //     3 波段 → R=b1, G=b2, B=b3
    //     4 波段 → R=b1, G=b2, B=b3，band4 进 alpha（Produces 承诺的"4→RGBA"）
    //   内存下标 = 2 - c（R 在 2、G 在 1、B 在 0），A 恒在 3。
    rgba = cv::Mat(in.tileSize, in.tileSize, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    int validCount = 0;
    for (int py = 0; py < in.tileSize; ++py) {
        uchar* out = rgba.ptr<uchar>(py);
        for (int px = 0; px < in.tileSize; ++px) {
            if (alphaValid.at<uchar>(py, px) == 0) {
                out[px * 4 + 0] = 0; out[px * 4 + 1] = 0; out[px * 4 + 2] = 0; out[px * 4 + 3] = 0;
                continue;
            }
            ++validCount;
            for (int c = 0; c < 3; ++c) {
                const int b = std::min(c, bands - 1);
                const cv::Mat& v = values[static_cast<size_t>(b)];
                const bool bv = bandValid[static_cast<size_t>(b)].at<uchar>(py, px) != 0;
                out[px * 4 + (2 - c)] = bv ? stretchByte(v.at<float>(py, px),
                                                         in.stretches[static_cast<size_t>(b)]) : 0;
            }
            if (bands == 4) {
                const cv::Mat& v = values[3];
                out[px * 4 + 3] = bandValid[3].at<uchar>(py, px)
                    ? stretchByte(v.at<float>(py, px), in.stretches[3]) : 0;
            } else {
                out[px * 4 + 3] = 255;   // 真 alpha：不透明只由上面的有效判据决定
            }
        }
    }
    // 原 :1151 "取不到任何有效值 ⇒ 跳过"的瓦片级判据（R-T18-6 明令保留）。
    return validCount > 0;
}

// 编码落盘（Task 18 S10：把通道顺序在**四种输出格式**上统一成"GDAL 读回的波段号 == 图像通道"）。
// WHY 这次动它：改前瓦片只有灰度（R=G=B），通道顺序错了**看不出来**；S1 让三波段彩色成为可能之后，
//   jpeg 分支的 cv::COLOR_RGBA2BGR 就从"潜伏"变成"真的错色"——那个枚举把内存序当成 (R,G,B,A) 读，
//   而 OpenCV 的 CV_8UC4 内存序是 (B,G,R,A) ⇒ 它产出的三通道是 (R,G,B)，再被 imwrite 按 BGR 解释
//   ⇒ JPEG 的红蓝互换（判别取证：#316 在改前红在 band1/band3）。
// WHY gtiff 分支**不**动：编码器一律把矩阵按 (B,G,R[,A]) 解释（PNG/WebP/JPEG/TIFF 同一条口径），
//   所以直接写 BGRA 落进文件的就是 R,G,B,A ⇒ GeoTIFF 的 band1 仍是红，与 PNG 同口径。
//   这里曾经加过一次 cv::COLOR_BGRA2RGBA "对齐顺序"，#316 实测反而把 band1 变成了蓝
//   （均值 189.4 落在 band1、126.3 落在 band3）⇒ 那次改动是错的，已退回并留本注释。
// 盘上事实：改前 `grep -n "output_format" tests/` 在 tile 侧零命中，四个格式一条用例都没有
//   ⇒ 这两支从来没被钉过（不是本任务的回归，但本任务让彩色成为可见产物，所以按射程内缺陷修）。
static bool encodeTile(const cv::Mat& rgba, const std::string& fmt, int quality,
                        const std::string& path) {
    std::vector<int> params;
    if (fmt == "jpeg") {
        params = {cv::IMWRITE_JPEG_QUALITY, quality};
        cv::Mat bgr;
        cv::cvtColor(rgba, bgr, cv::COLOR_BGRA2BGR);   // 输入矩阵是 BGRA，不是 RGBA
        return cv::imwrite(path, bgr, params);
    } else if (fmt == "webp") {
        params = {cv::IMWRITE_WEBP_QUALITY, quality};
        return cv::imwrite(path, rgba, params);
    } else if (fmt == "gtiff") {
        return cv::imwrite(path, rgba);
    }
    return cv::imwrite(path, rgba);
}

static void writeMetadataJson(const std::string& outputDir,
                               const std::string& scheme, int minZ, int maxZ,
                               int tileSize, const std::string& profile,
                               const std::string& fmt,
                               double minLon, double minLat, double maxLon, double maxLat,
                               const std::string& sourceFile) {
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::ostringstream timeStr;
    timeStr << std::put_time(std::gmtime(&timeT), "%Y-%m-%dT%H:%M:%SZ");

    double centerLon = (minLon + maxLon) / 2.0;
    double centerLat = (minLat + maxLat) / 2.0;
    int defaultZoom = std::max(minZ, std::min(maxZ, minZ + 2));

    std::ofstream json(outputDir + "/metadata.json");
    json << "{\n";
    json << "  \"tilejson\": \"3.0.0\",\n";
    json << "  \"name\": \"" << std::filesystem::path(sourceFile).stem().string() << "\",\n";
    json << "  \"description\": \"Generated by GIS Tool\",\n";
    json << "  \"version\": \"1.0.0\",\n";
    json << "  \"scheme\": \"" << scheme << "\",\n";
    json << "  \"tiles\": [\"{z}/{x}/{y}" << tileFileExt(fmt) << "\"],\n";
    json << "  \"minzoom\": " << minZ << ",\n";
    json << "  \"maxzoom\": " << maxZ << ",\n";
    json << "  \"bounds\": [" << minLon << ", " << minLat << ", " << maxLon << ", " << maxLat << "],\n";
    json << "  \"center\": [" << centerLon << ", " << centerLat << ", " << defaultZoom << "],\n";
    json << "  \"tile_size\": " << tileSize << ",\n";
    json << "  \"profile\": \"" << profile << "\",\n";
    json << "  \"format\": \"" << fmt << "\",\n";
    json << "  \"generated\": \"" << timeStr.str() << "\"\n";
    json << "}\n";
    json.close();
}

static bool copyLeafletAssetsTo(const std::filesystem::path& outputDir) {
    std::error_code ec;
    const auto bundledLeaflet = gis::core::findBundledSharePath("leaflet");
    if (bundledLeaflet.empty()) {
        return false;
    }

    const auto destDir = outputDir / "leaflet";
    std::filesystem::create_directories(destDir, ec);
    if (ec) {
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(bundledLeaflet, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        std::filesystem::copy_file(
            entry.path(),
            destDir / entry.path().filename(),
            std::filesystem::copy_options::overwrite_existing,
            ec);
        if (ec) {
            return false;
        }
    }
    return true;
}

static void writeLeafletHtml(const std::string& outputDir, int minZ, int maxZ,
                              double centerLon, double centerLat,
                              const std::string& title, const std::string& copyright) {
    const std::string safeTitle = title.empty()
        ? "Tile Preview"
        : gis::core::escapeForHtml(title);
    const std::string safeCopyright = gis::core::escapeForJsSingleQuoted(copyright);

    if (!copyLeafletAssetsTo(outputDir)) {
        throw std::runtime_error(
            "Leaflet preview assets not found (expected share/leaflet next to the application).");
    }

    std::ofstream html(outputDir + "/leaflet.html");
    html << "<!DOCTYPE html>\n<html><head><meta charset='utf-8'><title>"
         << safeTitle
         << "</title>\n"
         << "<link rel='stylesheet' href='./leaflet/leaflet.css'/>\n"
         << "<script src='./leaflet/leaflet.js'></script>\n"
         << "<style>body{margin:0}#map{width:100%;height:100vh}</style></head><body>\n"
         << "<div id='map'></div><script>\n"
         << "var map=L.map('map').setView([" << centerLat << "," << centerLon << "]," << std::max(minZ, std::min(maxZ, minZ+2)) << ");\n"
         << "L.tileLayer('./{z}/{x}/{y}.png',{minZoom:" << minZ << ",maxZoom:" << maxZ
         << ",attribution:'" << safeCopyright << "'}).addTo(map);\n"
         << "</script></body></html>\n";
    html.close();
}

gis::framework::Result CuttingPlugin::doTile(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string scheme = gis::framework::getParam<std::string>(params, "tile_scheme", "xyz");
    const std::string profile = gis::framework::getParam<std::string>(params, "profile", "mercator");
    const int minZoom = gis::framework::getParam<int>(params, "min_zoom", 0);
    int maxZoom = gis::framework::getParam<int>(params, "max_zoom", -1);
    const int tileSize = gis::framework::getParam<int>(params, "tile_pixel_size", 256);
    // B16（N36）：这里原先读的是**未声明**的 "resampling"，而 paramSpecs 声明的是 "resample"
    // ⇒ 用户传 --resample= 对 tile 完全无效（永远走内联默认 "average"），且没有任何报错。
    // 改读已声明的 "resample"；该键驱动下面 GDALWarp 的 -r（重投影插值）。
    // ⚠ 默认值**故意**不是 paramSpecs 里 resample 的 default("nearest")，就是这里的 "average"：
    //   改成 nearest 会一次性换掉**所有既有 tile 产物**的插值，那是"修死键"之外的第二件事。
    //   由此产生的已知不一致：GUI 面板显示的 Default: 来自 spec（nearest），而实际缺省按 average
    //   算 ⇒ 归 B19 决策（给 tile 单独默认，或接受"面板默认=插件级默认"的近似）。别当笔误改掉。
    const std::string resampling = gis::framework::getParam<std::string>(params, "resample", "average");
    const std::string ovResampling = gis::framework::getParam<std::string>(params, "overview_resampling", "nearest");
    const std::string fmt = gis::framework::getParam<std::string>(params, "output_format", "png");
    const int jpegQuality = gis::framework::getParam<int>(params, "jpeg_quality", 75);
    const bool addAlpha = gis::framework::getParam<bool>(params, "add_alpha", true);
    const double nodataVal = gis::framework::getParam<double>(params, "nodata_value", -9999.0);
    const bool skipBlank = gis::framework::getParam<bool>(params, "skip_blank", true);
    const bool resume = gis::framework::getParam<bool>(params, "resume", false);
    const auto extentArr = gis::framework::getParam<std::array<double,4>>(params, "tile_extent", std::array<double,4>{0,0,0,0});
    const std::string webviewer = gis::framework::getParam<std::string>(params, "webviewer", "none");
    const std::string title = gis::framework::getParam<std::string>(params, "title", "");
    const std::string copyright = gis::framework::getParam<std::string>(params, "copyright", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    // R-T17-1：非 mercator profile 显式失败并在 message 里点名该取值，不再用错误的
    // 网格产出瓦片。enumValues 不收窄（收窄取值集 = 未经授权改 API，与 R-T5-3 同口径）；
    // 取值集与实现面的不一致登记挂账 N48（批次 B 要么补齐两种方案的网格计算，
    // 要么把取值集收到 {"mercator"} 并改 spec），本批不动。
    if (profile != "mercator") {
        return gis::framework::Result::fail(
            "tile profile \"" + profile + "\" is not supported yet; only profile=mercator is implemented");
    }

    // R-T17-11 处置①：层级 translate 被 R-T17-7 删除后，overview_resampling 若只删不接
    // 就成了"面板上有、插件不读"的死键；改为让它驱动逐瓦片缩放插值。
    // 校验放在任何磁盘/临时产物之前：非法取值要当场失败，不许先干活再报错。
    int tileInterpolation = cv::INTER_LINEAR;
    if (!mapOverviewResampling(ovResampling, tileInterpolation)) {
        return gis::framework::Result::fail(
            "overview_resampling \"" + ovResampling + "\" has no cv::resize equivalent; "
            "supported: nearest, bilinear, cubic, average");
    }

    progress.throwIfCancelled();
    progress.onProgress(0.02);
    progress.onMessage("Opening input raster...");

    auto srcDS = gis::core::openRaster(input, true);
    double geoTransform[6] = {};
    srcDS->GetGeoTransform(geoTransform);
    const double pixelSize = std::abs(geoTransform[1]);

    // 瓦片几何只在"可投影 + 正北像元网格"的源上有定义：
    // (i) 无 CRS：GDALWarp 对无 CRS 输入带 -t_srs **不是失败而是照常产出**（pre-fix
    //     用例 TileCleansWarpedTempOnWarpFailure 实测 Success），错误位置的瓦片全靠
    //     这条显式拒绝来挡；
    // (ii) 旋转/斜切或异常号：瓦片读窗由 wgt 反算轴对齐矩形（R-T17-7 第 3 点），
    //     静默忽略旋转项等于把瓦片铺在"旋转内容的包络"上，是空间错位不是近似。
    if (std::string(srcDS->GetProjectionRef()).empty()) {
        return gis::framework::Result::fail(
            "tile input has no spatial reference; cannot project to EPSG:3857");
    }
    if (geoTransform[2] != 0.0 || geoTransform[4] != 0.0 ||
        !(geoTransform[1] > 0.0) || !(geoTransform[5] < 0.0)) {
        return gis::framework::Result::fail(
            "tile requires a north-up pixel grid (x pixel size > 0, y pixel size < 0, "
            "rotation/skew terms must be zero)");
    }

    if (maxZoom < 0) {
        maxZoom = computeMaxZoom(pixelSize, tileSize);
        progress.onMessage("Auto max_zoom = " + std::to_string(maxZoom));
    }
    maxZoom = std::min(maxZoom, 22);

    // profile 已被钉死为 mercator ⇒ 目标 SRS 恒为 EPSG:3857。原 :738-741 的
    // geodetic(EPSG:4326)/raster(原 SRS) 分支随显式失败一起删除；
    // 原 else 分支"用原 SRS 不 warp"在 mercator-only 下不可达，一并去掉。
    const std::string targetSRS = "EPSG:3857";

    progress.onProgress(0.05);
    progress.onMessage("Reprojecting to " + targetSRS + "...");

    // R-T17-7：整条链路上只有一个 warped 数据集（源包络 warp 进 EPSG:3857），
    // 每瓦片在它上面按世界矩形反算像元窗口。z<maxZoom 那套"每层级物化全球网格"
    // （原 :860-884 的 -outsize tileSize*2^z translate）已被 Tier-0 实测否决：
    // z=6 单层级 5.72s、z=7 21.26s，层级×4 外推 z=10 ≈ 21min 且临时文件按 10MB/s
    // 增长（N64/F3 的 Tier-0 探针实测；探针产物落在 build/ 下不入库，故这里只留数字本身）。
    // warped 由 unique_ptr+WarpedRasterCloser 持有：fail 提前返回与取消展开都会
    // 关闭并删除临时文件（R-T17-4）。
    // Task B6 / m-4：名字必须**每次调用唯一**，原来只带 pid ⇒ 同进程内两次 tile（并发或先后）
    //   都算出同一个路径（后一个把前一个正在读的 warped 覆盖掉，前一个的 closer 又会把
    //   后一个的文件删掉），崩溃后 pid 被复用时还会落到上一个进程的残件上。
    //   ⇒ pid 只保证跨进程分开，进程内再加一个单调序号：静态原子计数器 fetch_add，
    //     不引新依赖。函数内静态量的初始化与递增都是 C++11 起线程安全的，
    //     所以并发两次 tile 也拿不到同一个名字；序号从 1 起，第一次调用在日志里可辨认。
    static std::atomic<uint64_t> warpedSeq{0};
    const std::string warpedPath = (std::filesystem::temp_directory_path() /
        ("gis_tile_warped_" + std::to_string(CPLGetPID()) + "_" +
         std::to_string(warpedSeq.fetch_add(1) + 1) + ".tif")).string();
    std::unique_ptr<GDALDataset, WarpedRasterCloser> warped;
    {
        std::vector<std::string> warpArgStorage;
        warpArgStorage.push_back("-t_srs");
        warpArgStorage.push_back(targetSRS);
        warpArgStorage.push_back("-r");
        warpArgStorage.push_back(resampling);
        if (addAlpha) {
            warpArgStorage.push_back("-dstalpha");
        }
        warpArgStorage.push_back("-co");
        warpArgStorage.push_back("COMPRESS=LZW");
        warpArgStorage.push_back("-co");
        warpArgStorage.push_back("BIGTIFF=IF_SAFER");

        std::vector<char*> warpArgv;
        for (auto& a : warpArgStorage) warpArgv.push_back(a.data());
        warpArgv.push_back(nullptr);

        GDALWarpAppOptions* warpOpts = GDALWarpAppOptionsNew(warpArgv.data(), nullptr);
        int warpErr = 0;
        GDALDatasetH srcHandle = GDALDataset::ToHandle(srcDS.get());
        GDALDatasetH hWarped = GDALWarp(warpedPath.c_str(), nullptr,
            1, &srcHandle, warpOpts, &warpErr);
        GDALWarpAppOptionsFree(warpOpts);

        if (!hWarped || warpErr) {
            if (hWarped) {
                GDALClose(hWarped);
            }
            std::filesystem::remove(warpedPath);   // warp 半途失败可能留下残缺输出，defensive 清理
            return gis::framework::Result::fail("Reprojection failed: " + std::string(CPLGetLastErrorMsg()));
        }
        warped = std::unique_ptr<GDALDataset, WarpedRasterCloser>(
            static_cast<GDALDataset*>(hWarped), WarpedRasterCloser{warpedPath});
    }
    GDALDataset* warpedDS = warped.get();

    progress.throwIfCancelled();
    progress.onProgress(0.1);

    double wgt[6] = {};
    warpedDS->GetGeoTransform(wgt);
    const int wWidth = warpedDS->GetRasterXSize();
    const int wHeight = warpedDS->GetRasterYSize();

    const bool hasUserExtent = extentArr[0] != 0 || extentArr[1] != 0 ||
                               extentArr[2] != 0 || extentArr[3] != 0;
    double minLon, minLat, maxLon, maxLat;
    if (hasUserExtent) {
        minLon = extentArr[0]; minLat = extentArr[1];
        maxLon = extentArr[2]; maxLat = extentArr[3];
    } else {
        const double ulx = wgt[0];
        const double uly = wgt[3];
        const double lrx = wgt[0] + wWidth * wgt[1] + wHeight * wgt[2];
        const double lry = wgt[3] + wWidth * wgt[4] + wHeight * wgt[5];

        OGRSpatialReference srcSRS;
        srcSRS.importFromWkt(warpedDS->GetProjectionRef());
        OGRSpatialReference wgs84;
        wgs84.SetWellKnownGeogCS("WGS84");
        // F2/R-T17-8：GDAL 3 的地理坐标系默认轴序是 (lat, lon)，OGRCreateCoordinate
        // Transformation 会照默认轴序把**纬度放回 x 槽**、经度放回 y 槽；下游 minLon/maxLon
        // 与 metadata.json 的 "bounds"、webviewer 中心点、用户可见进度行全都按 (lon, lat)
        // 消费 ⇒ src 与 target 都要显式钉成 OAMS_TRADITIONAL_GIS_ORDER。
        // 判别取证：删掉这两行，TileMetadataBoundsKeepTraditionalLonLatOrder 必红。
        srcSRS.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        auto ct = std::unique_ptr<OGRCoordinateTransformation>(
            OGRCreateCoordinateTransformation(&srcSRS, &wgs84));

        if (ct) {
            double x1 = ulx, y1 = uly, x2 = lrx, y2 = lry;
            ct->Transform(1, &x1, &y1);
            ct->Transform(1, &x2, &y2);
            minLon = std::min(x1, x2); maxLon = std::max(x1, x2);
            minLat = std::min(y1, y2); maxLat = std::max(y1, y2);
        } else {
            minLon = ulx; maxLon = lrx;
            minLat = lry; maxLat = uly;
        }
    }

    minLat = std::max(minLat, -85.05112878);
    maxLat = std::min(maxLat, 85.05112878);
    minLon = std::max(minLon, -180.0);
    maxLon = std::min(maxLon, 180.0);

    // R-T17-7 第 4 点：瓦片索引范围**直接从 EPSG:3857 的度量范围算**
    // （floor((x - worldMinX)/res) 这条），不再"度量→经纬度→索引"往返。
    // 用户给的 tile_extent 是 WGS84 矩形，走 lonLatToMercatorXY 前向投影进度量面。
    // 经纬度 bounds 仍要算：metadata.json 的 "bounds"/"center" 与 webviewer 消费的是
    // 经纬度（这也是 F2 轴序修复只有产物级断言才钉得住的原因）。
    double extMinX, extMaxX, extMinY, extMaxY;
    if (hasUserExtent) {
        lonLatToMercatorXY(minLon, minLat, extMinX, extMinY);
        lonLatToMercatorXY(maxLon, maxLat, extMaxX, extMaxY);
    } else {
        extMinX = wgt[0];
        extMaxX = wgt[0] + wWidth * wgt[1];
        extMaxY = wgt[3];
        extMinY = wgt[3] + wHeight * wgt[5];
    }
    {
        constexpr double kHalf = 20037508.342789244;
        extMinX = std::clamp(extMinX, -kHalf, kHalf);
        extMaxX = std::clamp(extMaxX, -kHalf, kHalf);
        extMinY = std::clamp(extMinY, -kHalf, kHalf);
        extMaxY = std::clamp(extMaxY, -kHalf, kHalf);
    }

    progress.onMessage("Bounds: " + std::to_string(minLon) + "," + std::to_string(minLat) +
                       " to " + std::to_string(maxLon) + "," + std::to_string(maxLat));

    std::filesystem::create_directories(output);

    // 层级包络 → 索引范围（度量推导；tileWindow/tileWorldRect 同源用 globalWidth
    // 推导瓦片数，合法层级恒等于 2^z）。totalTiles 预估与主循环用同一个 lambda，
    // 两处不可能漂移。
    // WHY 除以 tileWidthM（单瓦片米宽 = 世界宽/瓦片数）而不是 resX（米/全球像元）：
    // floor((x-worldMinX)/resX) 给的是"全球像元下标"，/tileWidthM 才是"瓦片下标"，
    // 前者被 clamp 到 [0,tiles-1] 会把整个包络压成角落那一张（首跑实测复现过）。
    auto levelTileBounds = [&](int z, gis::core::TileGrid& grid) {
        grid = gis::core::mercatorTileGrid(z, tileSize);
        const int tiles = std::max(1, grid.globalWidth / std::max(1, tileSize));
        const double tileWidthM = (grid.worldMaxX - grid.worldMinX) / tiles;
        const double tileHeightM = (grid.worldMaxY - grid.worldMinY) / tiles;
        const auto clampIdx = [tiles](double v) {
            return std::clamp(static_cast<int>(std::floor(v)), 0, tiles - 1);
        };
        return LevelBounds{
            clampIdx((extMinX - grid.worldMinX) / tileWidthM),
            clampIdx((extMaxX - grid.worldMinX) / tileWidthM),
            clampIdx((grid.worldMaxY - extMaxY) / tileHeightM),
            clampIdx((grid.worldMaxY - extMinY) / tileHeightM),
            tiles
        };
    };

    int totalTiles = 0;
    for (int z = minZoom; z <= maxZoom; ++z) {
        gis::core::TileGrid grid;
        const LevelBounds b = levelTileBounds(z, grid);
        totalTiles += (b.xMax - b.xMin + 1) * (b.yMax - b.yMin + 1);
    }

    progress.onMessage("Total tiles to generate: " + std::to_string(totalTiles));
    progress.onProgress(0.12);

    // N62 同形残留收口（修复轮 F6）：改前 `++tilesGenerated` 挂在**每一个** continue 分支上
    // （7 条：resume 命中、`!rect.valid`、与源包络零交集 `c1<=c0||r1<=r0`、子矩形零面积
    //   `dx1<=dx0||dy1<=dy0`、RasterIO 读失败、`bandMin>bandMax` 即全 NoData、skipBlank 判空），
    // 逐条可在 90d7ee6^ 的同文件里数出来，而 metadata["tiles_generated"]
    // 与成功消息都吃这个数 ⇒ "报 N tiles 但磁盘上没 N 个文件"这条本任务要消灭的形态又活了。
    // 三个量各管一件事：
    //   tilesGenerated 交付的瓦片文件数（写出成功，或 resume 命中已存在的同一张 ⇒ 产物确实在 output 里）
    //   tilesSkipped   没有产物的瓦片数（上面那几个 continue）
    //   tilesProcessed 已处理数 = 前两者之和，**只**喂进度百分比（口径照旧，不拿它报产物数）
    // 元数据两键都写（沿用 snake_case 同族命名）；成功消息仍报 tilesGenerated ⇒ 消息里的数字
    // 必须能在 output 下数出来（用例 TileCountersMatchWrittenTileFiles 钉这条）。
    int tilesGenerated = 0;
    int tilesSkipped = 0;
    int tilesProcessed = 0;
    const std::string ext = tileFileExt(fmt);

    // 波段面（Task 18 S1/S2）：改到这里只有 band 1 被读（R-T17-5 把多波段划给本任务，
    // 并把 srcBands 留成死变量）。层级数据集被 R-T17-7 删除后，波段元数据不随层级变化
    // ⇒ 色波段数、每波段 NODATA、整幅拉伸参数都**只算一次**，放在层级循环之外。
    //
    // 色波段数的推导只看 warped 数据集自己（不看 srcDS）：-dstalpha 只在 addAlpha 时加，
    // 且恒追加在**末尾** ⇒ 色波段是 1..(warpBands-1)、alpha 波段是 warpBands；不加时
    // 色波段就是 1..warpBands。上限 4 是 R-T18-10 的既定口径（波段数上限不在本任务扩展），
    // 这里只保证"不越界读、不把 dstalpha 当成第 4 个颜色波段"。
    const int warpBands = std::max(1, warpedDS->GetRasterCount());
    const int colorBands = std::max(1, std::min(addAlpha ? warpBands - 1 : warpBands, 4));
    // alphaBandIndex>0 只在"确有一条 -dstalpha 追加进来的波段"时为真；bandPresent 那一臂
    // 是防御（GDAL 若不追加就不读它），正常路径下 addAlpha 为真时它恒成立 ⇒ 不可单独判别。
    const int alphaBandIndex = (addAlpha && warpBands >= colorBands + 1) ? warpBands : 0;

    std::vector<double> bandNodata;
    std::vector<char> bandHasNodata;
    bandNodata.reserve(colorBands);
    bandHasNodata.reserve(colorBands);
    for (int b = 1; b <= colorBands; ++b) {
        // nodata_value 的读点保留在这里：该波段**未声明** NODATA 时用参数值垫边（口径照改前
        // :1256-1257 的 `if (!hasNd) srcNodata = nodataVal`）。有效判据仍然只在"声明过"时
        // 生效（改前 :1145 的 `hasNd &&` 口径，#296 的注释把这条钉成了既端口径）。
        int hasNd = 0;
        GDALRasterBand* band = warpedDS->GetRasterBand(b);
        const double declared = band ? band->GetNoDataValue(&hasNd) : 0.0;
        bandHasNodata.push_back(hasNd != 0 ? 1 : 0);
        bandNodata.push_back(hasNd ? declared : nodataVal);
    }
    // F-1：把 alphaBandIndex 一起交给统计段——add_alpha=true 时它 >0（-dstalpha 追加在末尾），
    //   统计必须按它过滤，否则被遮掉的像元以写入值进区间 ⇒ 亮度随 add_alpha 变（复审 I-1）。
    //   add_alpha=false 时它是 0 ⇒ 走 GDAL 精算（跳过声明的 NODATA），产物与 F-1 之前逐字节同。
    const std::vector<BandStretch> sceneStretches =
        computeSceneBandStretch(warpedDS, colorBands, alphaBandIndex);
    progress.onMessage("Bands: " + std::to_string(colorBands) + " color + " +
                       std::to_string(alphaBandIndex > 0 ? 1 : 0) + " alpha");

    for (int z = minZoom; z <= maxZoom; ++z) {
        progress.throwIfCancelled();

        gis::core::TileGrid grid;
        const LevelBounds b = levelTileBounds(z, grid);
        const int xMin = b.xMin, yMin = b.yMin, xMax = b.xMax, yMax = b.yMax;

        const int tilesInLevel = (xMax - xMin + 1) * (yMax - yMin + 1);
        progress.onMessage("Zoom " + std::to_string(z) + ": " + std::to_string(tilesInLevel) + " tiles");

        // R-T17-7：删除"每层级物化全球网格"的 -outsize translate（10829d9^:860-884）与
        // 死变量 levelRes/globalSize（10829d9^:854-855）。F3/N64 的 Tier-0 探针实测（数字抄在这里）：
        // 该物化 z=6 就要 5.72s、z=7 21.26s、×4 外推 z=10 ≈ 21min，真实层级不可用。
        // 层级 translate 是 ovResampling 唯一旧用途（10829d9^:869），处置见上面 R-T17-11①。

        for (int tx = xMin; tx <= xMax; ++tx) {
            for (int ty = yMin; ty <= yMax; ++ty) {
                int tyWrite = ty;
                if (scheme == "tms") {
                    tyWrite = b.tiles - 1 - ty;   // == (1<<z)-1-ty，但取自同一网格推导
                }

                const std::string tileDir = output + "/" + std::to_string(z) + "/" + std::to_string(tx);
                const std::string tilePath = tileDir + "/" + std::to_string(tyWrite) + ext;

                if (resume && std::filesystem::exists(tilePath)) {
                    ++tilesGenerated;   // 已存在=已交付：产物真的在 output 里，仍归 generated
                    ++tilesProcessed;
                    continue;
                }

                // R-T17-7 第 3 点：瓦片索引 → EPSG:3857 世界矩形（纯函数 tileWorldRect）
                // → 用 warped 数据集的 wgt 反算像元窗口（col=(x-wgt[0])/wgt[1]、
                // row=(y-wgt[3])/wgt[5]）→ 裁剪到数据集范围 → **按覆盖比例**把读到的块
                // 放进 tileSize×tileSize 缓冲区的正确子像素位置，未覆盖边保持透明。
                // 原 :912-925"度矩形除以米制 wgt"的混算与 :919 srcWin[3] 的符号错误（F1，
                // 恒为负 ⇒ 每瓦片 continue）在这里一并废除。反算不含近似的前提是 wgt[2]/wgt[4] 为零、
                // wgt[1]>0、wgt[5]<0 —— 它来自 GDALWarp 产物的轴对齐 north-up（N70），不是那道查**源**
                // geoTransform 的 north-up 守卫：wgt 取自 warpedDS->GetGeoTransform，守卫管不到它。
                const gis::core::TileRect rect = gis::core::tileWorldRect(grid, tileSize, tx, ty);
                if (!rect.valid) {
                    ++tilesSkipped;
                    ++tilesProcessed;
                    continue;
                }

                const double sx0 = (rect.minX - wgt[0]) / wgt[1];
                const double sx1 = (rect.maxX - wgt[0]) / wgt[1];
                const double syTop = (rect.maxY - wgt[3]) / wgt[5];   // wgt[5]<0 ⇒ 北缘行号小
                const double syBot = (rect.minY - wgt[3]) / wgt[5];
                const int c0 = std::max(0, static_cast<int>(std::floor(std::min(sx0, sx1))));
                const int c1 = std::min(wWidth, static_cast<int>(std::ceil(std::max(sx0, sx1))));
                const int r0 = std::max(0, static_cast<int>(std::floor(std::min(syTop, syBot))));
                const int r1 = std::min(wHeight, static_cast<int>(std::ceil(std::max(syTop, syBot))));
                if (c1 <= c0 || r1 <= r0) {
                    // 与源包络零交集：不得产出文件（端到端用例的字面集合钉不允许空瓦片）。
                    ++tilesSkipped;
                    ++tilesProcessed;
                    continue;
                }

                // 整数读窗的真实世界边 → 瓦片内目标子矩形（覆盖位置，逐像素按比例）。
                const double ax0 = wgt[0] + c0 * wgt[1];
                const double ax1 = wgt[0] + c1 * wgt[1];
                const double ayTop = wgt[3] + r0 * wgt[5];
                const double ayBot = wgt[3] + r1 * wgt[5];
                const double tileResX = (rect.maxX - rect.minX) / tileSize;
                const double tileResY = (rect.maxY - rect.minY) / tileSize;
                const int dx0 = std::clamp(static_cast<int>(std::floor((ax0 - rect.minX) / tileResX)), 0, tileSize);
                const int dx1 = std::clamp(static_cast<int>(std::ceil((ax1 - rect.minX) / tileResX)), 0, tileSize);
                const int dy0 = std::clamp(static_cast<int>(std::floor((rect.maxY - ayTop) / tileResY)), 0, tileSize);
                const int dy1 = std::clamp(static_cast<int>(std::ceil((rect.maxY - ayBot) / tileResY)), 0, tileSize);
                // 守卫②＝**防御性不可达分支**（挂账 N69）：守卫①通过则这里恒不触发。
                // 推导（x 向；y 向把 wgt[5]<0 的方向翻转后同构）。记 sx_min=min(sx0,sx1)、
                // sx_max=max(sx0,sx1)（就是上面 c0/c1 取的那两端）。三条前提：wgt[1]>0 来自 GDALWarp 产物
                // 的轴对齐 north-up（N70；那道 north-up 守卫查的是源 geoTransform，管不到这里的 wgt）
                // ⇒ sx_min<sx_max；rect.valid ⇒ rect.maxX>rect.minX，再配 tileSize>0 ⇒ tileResX>0（这条
                // 不由 doTile 保证：tile_pixel_size 的 [128,1024] 范围校验在 CLI/GUI 派发层做，插件自己不重验）；
                // ① ⇒ c1>c0 ⇒ ax1=ax0+(c1-c0)·wgt[1]>ax0。记 u=(ax0-rect.minX)/tileResX、
                // v=(ax1-rect.minX)/tileResX ⇒ 由 tileResX>0 与 ax1>ax0 得 v>u ⇒ 恒有 ceil(v)≥floor(u)+1
                // （u 为负也成立，与钳位前的取值无关）。
                // 取整是**向外**的：floor(sx_min)≥0 时 c0=floor(sx_min) ⇒ ax0≤rect.minX（非像素对齐时严格
                // 小于），ceil(sx_max)≤wWidth 时 c1=ceil(sx_max) ⇒ ax1≥rect.maxX ⇒ 内部瓦片满足 [ax0,ax1] ⊇
                // [rect.minX,rect.maxX]（**旧写法把这个包含方向写反了**，写成 ⊆ 是借不起来的）；越出源包络的
                // 边缘瓦片两个包含关系都不成立 ⇒ 所以 dx1>dx0 这一步要靠钳位收口：clamp 到 [0,tileSize] 是
                // 唯一能吃掉那个 +1 的地方，而它只在两端同向时才收得住 —— dx0 被顶到 tileSize 需要
                // ax0≥rect.maxX、dx1 被压到 0 需要 ax1≤rect.minX，**这两处都必须借守卫①才拿不到**：
                // floor(sx_min)<0 时 c0=0 ⇒ ax0=wgt[0]>rect.minX，但 ①⇒c1≥1⇒sx_max>0⇒rect.maxX>wgt[0]=ax0；
                // ceil(sx_max)>wWidth 时 c1=wWidth，①⇒c0≤wWidth-1⇒sx_min<wWidth⇒rect.minX<wgt[0]+wWidth·wgt[1]=ax1。
                // 以上"①⇒¬②"的论域是**参数校验过的输入**：tileSize=0 时 tileResX 退化成除零（IEEE 下为 inf）
                // ⇒ dx0 与 dx1 双双落到 0，这条分支会真触发；tileSize<0 则 std::clamp(lo>hi) 本身是 UB。
                // ⇒ 正常几何下这条 continue 永不进入，所以它**没有**也**不该有**判别用例（N69 的纪律：
                //   要求"每个守卫各自可杀"之前先证可达性）。保留它是因为 warp 产物的退化窗口不是可证明的
                //   不变量——删掉它等于赌 GDALWarp 永不给出退化结果，代价是整幅瓦片走进零面积渲染。
                if (dx1 <= dx0 || dy1 <= dy0) {
                    ++tilesSkipped;
                    ++tilesProcessed;
                    continue;
                }

                // 渲染段（读带 → 插值放置 → 拉伸 → alpha）已抽到 renderTileRgba（Task 18 S0）。
                TileRenderInput renderIn;
                renderIn.warpedDS = warpedDS;
                renderIn.srcX = c0;
                renderIn.srcY = r0;
                renderIn.srcW = c1 - c0;
                renderIn.srcH = r1 - r0;
                renderIn.dstX = dx0;
                renderIn.dstY = dy0;
                renderIn.dstW = dx1 - dx0;
                renderIn.dstH = dy1 - dy0;
                renderIn.tileSize = tileSize;
                renderIn.interpolation = tileInterpolation;
                renderIn.addAlpha = addAlpha;
                renderIn.colorBands = colorBands;
                renderIn.alphaBandIndex = alphaBandIndex;
                renderIn.bandNodata = bandNodata;
                renderIn.bandHasNodata = bandHasNodata;
                renderIn.stretches = sceneStretches;

                cv::Mat rgba;
                if (!renderTileRgba(renderIn, rgba)) {
                    ++tilesSkipped;
                    ++tilesProcessed;
                    continue;
                }

                if (skipBlank && isTileBlank(rgba)) {
                    ++tilesSkipped;
                    ++tilesProcessed;
                    continue;
                }

                std::filesystem::create_directories(tileDir);
                if (encodeTile(rgba, fmt, jpegQuality, tilePath)) {
                    ++tilesGenerated;   // 只有真的写出文件才算交付
                } else {
                    // 写失败 = 没有产物：归 skipped 而不是 generated，否则 metadata 又在报不存在的文件。
                    progress.onMessage("Warning: failed to write " + tilePath);
                    ++tilesSkipped;
                }
                ++tilesProcessed;
                if ((tilesProcessed % 50) == 0) {
                    progress.throwIfCancelled();
                    // 百分比继续用"已处理数"（含跳过），口径与改前一致；F6 没顺手改它。
                    const double pct = 0.12 + 0.85 * static_cast<double>(tilesProcessed) / std::max(1, totalTiles);
                    progress.onProgress(pct);
                }
            }
        }
    }

    // 临时 warped 栅格由 WarpedRasterCloser 的析退统一关闭并删除：正常收尾、
    // Result::fail 提前返回、throwIfCancelled 异常展开三条路径同一点清理（R-T17-4）。
    // 原 :993-996 的层级文件清理随层级 translate 一起删除；原 :999-1002 的
    // "仅正常路径清理"就是取消路径泄漏 gis_tile_warped.tif 的成因。

    progress.onProgress(0.97);
    progress.onMessage("Writing metadata.json...");
    writeMetadataJson(output, scheme, minZoom, maxZoom, tileSize, profile, fmt,
                      minLon, minLat, maxLon, maxLat, input);

    if (webviewer == "leaflet" || webviewer == "all") {
        double cLon = (minLon + maxLon) / 2.0;
        double cLat = (minLat + maxLat) / 2.0;
        writeLeafletHtml(output, minZoom, maxZoom, cLon, cLat, title, copyright);
    }

    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Tile generation completed: " + std::to_string(tilesGenerated) + " tiles", output);
    // 消息文案格式保持原样（gui_data_support.cpp:2264 的翻译表按前缀 "Tile generation completed"
    // 匹配），但里面的数字现在是**交付文件数**，不再是含跳过的处理数（N62 同形残留收口）。
    result.metadata["tiles_generated"] = std::to_string(tilesGenerated);
    result.metadata["tiles_skipped"] = std::to_string(tilesSkipped);
    result.metadata["zoom_range"] = std::to_string(minZoom) + "-" + std::to_string(maxZoom);
    result.metadata["scheme"] = scheme;
    result.metadata["profile"] = profile;
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::CuttingPlugin)

