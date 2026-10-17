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
#include <filesystem>
#include <cmath>
#include <fstream>
#include <chrono>
#include <iomanip>

namespace gis::plugins {

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
            "skip_blank", "跳过空瓦片", "Skip tiles that contain only nodata",
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
        // 提交 236327d 本身 + 本机日志 build/t17_logs/n37_debug_full.log 的尾行
        // "100% tests passed, 0 tests failed out of 607"（该全量跑在删除提交之后）。
        // WHY 不再写"实测判据见 task-17-report.md"：那份报告当时并不存在（实施代理交付 8 个提交后
        // 被账号配额截断，报告从未写出），指向不存在文件的证据指针等于让这条注释无法核验。
        // 注：build/ 是本机忽略目录（.gitignore:2），日志路径的不可交付性留给批次收尾（Task 21）
        // 统一清理，本轮只把"指错"改成"指对"，不顺手扩大改动面。
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
// 同进程并发工作流的隔离与临时目录归划仍留给 Plan 3 的 C8 统一处理。
// WHY 定义全部写进类体：批次 A 实测过"匿名命名空间内的成员函数定义 ⇒ 内部链接
// ⇒ LNK2019"的坑，内联定义从根上避开。
struct WarpedRasterCloser {
    std::string path;
    void operator()(GDALDataset* ds) const {
        if (ds) {
            GDALClose(GDALDataset::ToHandle(ds));
        }
        if (!path.empty()) {
            std::filesystem::remove(path);
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

static bool isTileBlank(const cv::Mat& rgba) {
    if (rgba.channels() < 4) return false;
    int nonzero = 0;
    for (int y = 0; y < rgba.rows; ++y) {
        const uchar* row = rgba.ptr<uchar>(y);
        for (int x = 0; x < rgba.cols; ++x) {
            if (row[x * 4 + 3] > 0) { ++nonzero; if (nonzero > 4) return false; }
        }
    }
    return nonzero <= 4;
}

static bool encodeTile(const cv::Mat& rgba, const std::string& fmt, int quality,
                        const std::string& path) {
    std::vector<int> params;
    if (fmt == "jpeg") {
        params = {cv::IMWRITE_JPEG_QUALITY, quality};
        cv::Mat bgr;
        cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
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
    const std::string resampling = gis::framework::getParam<std::string>(params, "resampling", "average");
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
    // 增长（N64/F3，报告 build/tmp/t17_probe/）。
    // warped 由 unique_ptr+WarpedRasterCloser 持有：fail 提前返回与取消展开都会
    // 关闭并删除临时文件（R-T17-4），文件名带 pid 避免跨进程互踩。
    const std::string warpedPath = (std::filesystem::temp_directory_path() /
        ("gis_tile_warped_" + std::to_string(CPLGetPID()) + ".tif")).string();
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

    int tilesGenerated = 0;
    const std::string ext = tileFileExt(fmt);

    // hasNd/srcNodata 只在 warped 数据集上读一次：层级数据集被 R-T17-7 删除后
    // 它们不再随层级变化。原 :886 的 srcBands 是死变量（只有 band 1 被读），删除；
    // 多波段处理是 Task 18 的地盘（R-T17-5 红线）。
    int hasNd = 0;
    double srcNodata = warpedDS->GetRasterBand(1)->GetNoDataValue(&hasNd);
    if (!hasNd) srcNodata = nodataVal;

    for (int z = minZoom; z <= maxZoom; ++z) {
        progress.throwIfCancelled();

        gis::core::TileGrid grid;
        const LevelBounds b = levelTileBounds(z, grid);
        const int xMin = b.xMin, yMin = b.yMin, xMax = b.xMax, yMax = b.yMax;

        const int tilesInLevel = (xMax - xMin + 1) * (yMax - yMin + 1);
        progress.onMessage("Zoom " + std::to_string(z) + ": " + std::to_string(tilesInLevel) + " tiles");

        // R-T17-7：删除"每层级物化全球网格"的 -outsize translate（原 :860-884）与
        // 死变量 levelRes/globalSize（:854-855）。F3/N64 实测（build/tmp/t17_probe/）：
        // 该物化 z=6 就要 5.72s、z=7 21.26s、×4 外推 z=10 ≈ 21min，真实层级不可用。
        // 层级 translate 是 ovResampling 唯一旧用途（:869），处置见上面 R-T17-11①。

        for (int tx = xMin; tx <= xMax; ++tx) {
            for (int ty = yMin; ty <= yMax; ++ty) {
                int tyWrite = ty;
                if (scheme == "tms") {
                    tyWrite = b.tiles - 1 - ty;   // == (1<<z)-1-ty，但取自同一网格推导
                }

                const std::string tileDir = output + "/" + std::to_string(z) + "/" + std::to_string(tx);
                const std::string tilePath = tileDir + "/" + std::to_string(tyWrite) + ext;

                if (resume && std::filesystem::exists(tilePath)) {
                    ++tilesGenerated;
                    continue;
                }

                // R-T17-7 第 3 点：瓦片索引 → EPSG:3857 世界矩形（纯函数 tileWorldRect）
                // → 用 warped 数据集的 wgt 反算像元窗口（col=(x-wgt[0])/wgt[1]、
                // row=(y-wgt[3])/wgt[5]）→ 裁剪到数据集范围 → **按覆盖比例**把读到的块
                // 放进 tileSize×tileSize 缓冲区的正确子像素位置，未覆盖边保持透明。
                // 原 :912-925"度矩形除以米制 wgt"的混算与 :919 srcWin[3] 的符号错误（F1，
                // 恒为负 ⇒ 每瓦片 continue）在这里一并废除；north-up 守卫已保证
                // wgt[2]/wgt[4] 为零、wgt[1]>0、wgt[5]<0，反算不含近似。
                const gis::core::TileRect rect = gis::core::tileWorldRect(grid, tileSize, tx, ty);
                if (!rect.valid) {
                    ++tilesGenerated;
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
                    ++tilesGenerated;
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
                if (dx1 <= dx0 || dy1 <= dy0) {
                    ++tilesGenerated;
                    continue;
                }

                cv::Mat tileData(tileSize, tileSize, CV_32F, cv::Scalar(static_cast<float>(srcNodata)));
                cv::Mat coverMask(tileSize, tileSize, CV_8UC1, cv::Scalar(0));

                cv::Mat block(r1 - r0, c1 - c0, CV_32F);
                CPLErr err = warpedDS->GetRasterBand(1)->RasterIO(
                    GF_Read, c0, r0, c1 - c0, r1 - r0,
                    block.data, block.cols, block.rows, GDT_Float32, 0, 0);

                if (err != CE_None) {
                    ++tilesGenerated;
                    continue;
                }

                // R-T17-11 处置①：插值项来自 overview_resampling 映射出的 tileInterpolation，
                // 原 :943 的硬编码 cv::INTER_LINEAR 作废（判别用例：nearest 与 average 同输入
                // 必须产出不同字节；删掉映射硬回 INTER_LINEAR ⇒ 两次输出一致 ⇒ 用例必红）。
                cv::resize(block, block, cv::Size(dx1 - dx0, dy1 - dy0), 0, 0, tileInterpolation);
                block.copyTo(tileData(cv::Rect(dx0, dy0, dx1 - dx0, dy1 - dy0)));
                coverMask(cv::Rect(dx0, dy0, dx1 - dx0, dy1 - dy0)).setTo(255);

                cv::Mat rgba(tileSize, tileSize, CV_8UC4, cv::Scalar(0, 0, 0, 0));
                double bandMin = 1e30, bandMax = -1e30;
                for (int py = 0; py < tileSize; ++py) {
                    for (int px = 0; px < tileSize; ++px) {
                        const float v = tileData.at<float>(py, px);
                        if (coverMask.at<uchar>(py, px) == 0) continue;   // 未覆盖垫边不参与拉伸
                        if (hasNd && std::abs(v - srcNodata) < 1e-6) continue;
                        if (v < bandMin) bandMin = v;
                        if (v > bandMax) bandMax = v;
                    }
                }

                if (bandMin > bandMax) {
                    ++tilesGenerated;
                    continue;
                }

                const double range = (bandMax - bandMin > 1e-9) ? (bandMax - bandMin) : 1.0;
                for (int py = 0; py < tileSize; ++py) {
                    for (int px = 0; px < tileSize; ++px) {
                        const float v = tileData.at<float>(py, px);
                        if (coverMask.at<uchar>(py, px) == 0 ||
                            (hasNd && std::abs(v - srcNodata) < 1e-6)) {
                            rgba.at<cv::Vec4b>(py, px) = cv::Vec4b(0, 0, 0, 0);
                        } else {
                            const uchar c = static_cast<uchar>(255.0 * (v - bandMin) / range);
                            // R-T17-10/N65：addAlpha 的三元两个分支相同 ⇒ add_alpha 目前是空操作，
                            // 这是 Task 18（多波段与 alpha）的地盘，本任务只登记不修（R-T17-5 红线）。
                            rgba.at<cv::Vec4b>(py, px) = cv::Vec4b(c, c, c, addAlpha ? 255 : 255);
                        }
                    }
                }

                if (skipBlank && isTileBlank(rgba)) {
                    ++tilesGenerated;
                    continue;
                }

                std::filesystem::create_directories(tileDir);
                if (!encodeTile(rgba, fmt, jpegQuality, tilePath)) {
                    progress.onMessage("Warning: failed to write " + tilePath);
                }

                ++tilesGenerated;
                if ((tilesGenerated % 50) == 0) {
                    progress.throwIfCancelled();
                    const double pct = 0.12 + 0.85 * static_cast<double>(tilesGenerated) / std::max(1, totalTiles);
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
    result.metadata["tiles_generated"] = std::to_string(tilesGenerated);
    result.metadata["zoom_range"] = std::to_string(minZoom) + "-" + std::to_string(maxZoom);
    result.metadata["scheme"] = scheme;
    result.metadata["profile"] = profile;
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::CuttingPlugin)

