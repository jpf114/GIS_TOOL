#include "cutting_plugin.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace gis::plugins {

std::vector<gis::framework::ParamSpec> CuttingPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"clip", "mosaic", "split", "merge_bands"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入影像文件路径(多文件用逗号分隔)",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出文件", "输出影像文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "extent", "裁切范围", "矩形范围(xmin,ymin,xmax,ymax)",
            gis::framework::ParamType::Extent, false, std::array<double,4>{0,0,0,0}
        },
        gis::framework::ParamSpec{
            "cutline", "裁切矢量", "用于裁切的矢量文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "tile_size", "分块大小", "分块切割时每块的像素大小",
            gis::framework::ParamType::Int, false, int{1024}
        },
        gis::framework::ParamSpec{
            "overlap", "重叠像素数", "分块切割时块间的重叠像素数",
            gis::framework::ParamType::Int, false, int{0}
        },
        gis::framework::ParamSpec{
            "bands", "波段列表", "合并波段时各波段对应的文件路径(逗号分隔)",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "dst_srs", "目标坐标系", "镶嵌时统一的目标坐标系",
            gis::framework::ParamType::CRS, false, std::string{}
        },
        gis::framework::ParamSpec{
            "resample", "重采样方法", "重采样算法",
            gis::framework::ParamType::Enum, false, std::string{"nearest"},
            int{0}, int{0},
            {"nearest", "bilinear", "cubic", "cubicspline", "lanczos", "average"}
        },
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

    return gis::framework::Result::fail("Unknown action: " + action);
}

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

            tileIdx++;
            double pct = 0.1 + 0.9 * static_cast<double>(tileIdx) / totalTiles;
            progress.onProgress(pct);
        }
    }

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
    if (!bandsStr.empty()) {
        bandFiles = splitString(bandsStr, ',');
    } else if (!input.empty()) {
        bandFiles = splitString(input, ',');
    }

    if (bandFiles.empty()) {
        return gis::framework::Result::fail("At least one input file required (use --input or --bands)");
    }

    progress.onMessage("Building VRT from " + std::to_string(bandFiles.size()) + " files...");
    progress.onProgress(0.1);

    std::vector<const char*> vrtInputPaths;
    for (auto& f : bandFiles) vrtInputPaths.push_back(f.c_str());
    vrtInputPaths.push_back(nullptr);

    GDALBuildVRTOptions* vrtOpts = GDALBuildVRTOptionsNew(nullptr, nullptr);
    if (!vrtOpts) {
        return gis::framework::Result::fail("Failed to create VRT options");
    }

    GDALDatasetH vrtDS = GDALBuildVRT("/vsimem/band_merge.vrt",
        static_cast<int>(bandFiles.size()),
        const_cast<char**>(vrtInputPaths.data()),
        nullptr, vrtOpts, nullptr);
    GDALBuildVRTOptionsFree(vrtOpts);

    if (!vrtDS) {
        return gis::framework::Result::fail("Failed to build VRT: " + std::string(CPLGetLastErrorMsg()));
    }

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
    progress.onProgress(1.0);
    progress.onMessage("Band merge completed.");
    return gis::framework::Result::ok("Band merge completed successfully", output);
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::CuttingPlugin)
