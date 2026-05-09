#include "cutting_plugin.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <gdal_utils.h>
#include <opencv2/opencv.hpp>
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
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"clip", "mosaic", "split", "merge_bands", "tile"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入影像文件路径(多文件用逗号分隔，merge_bands 时可不填)",
            gis::framework::ParamType::FilePath, false, std::string{}
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
            gis::framework::ParamType::Int, false, int{1024},
            int{1}, int{65536}
        },
        gis::framework::ParamSpec{
            "overlap", "重叠像素数", "分块切割时块间的重叠像素数",
            gis::framework::ParamType::Int, false, int{0},
            int{0}, int{65536}
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
        gis::framework::ParamSpec{
            "tile_scheme", "瓦片编号方案", "XYZ(y=0在北)或TMS(y=0在南)",
            gis::framework::ParamType::Enum, false, std::string{"xyz"},
            int{0}, int{0},
            {"xyz", "tms"}
        },
        gis::framework::ParamSpec{
            "profile", "切片剖面", "mercator=EPSG:3857, geodetic=EPSG:4326, raster=不重投影",
            gis::framework::ParamType::Enum, false, std::string{"mercator"},
            int{0}, int{0},
            {"mercator", "geodetic", "raster"}
        },
        gis::framework::ParamSpec{
            "min_zoom", "最小缩放层级", "瓦片金字塔的最小缩放层级",
            gis::framework::ParamType::Int, false, int{0},
            int{0}, int{30}
        },
        gis::framework::ParamSpec{
            "max_zoom", "最大缩放层级", "瓦片金字塔的最大缩放层级，-1表示自动计算",
            gis::framework::ParamType::Int, false, int{-1},
            int{-1}, int{30}
        },
        gis::framework::ParamSpec{
            "tile_pixel_size", "瓦片像素尺寸", "每个瓦片的像素宽高",
            gis::framework::ParamType::Int, false, int{256},
            int{128}, int{1024}
        },
        gis::framework::ParamSpec{
            "overview_resampling", "概览重采样", "金字塔概览图的重采样方法",
            gis::framework::ParamType::Enum, false, std::string{"nearest"},
            int{0}, int{0},
            {"nearest", "bilinear", "cubic", "cubicspline", "lanczos", "average", "mode"}
        },
        gis::framework::ParamSpec{
            "output_format", "瓦片输出格式", "瓦片图像格式",
            gis::framework::ParamType::Enum, false, std::string{"png"},
            int{0}, int{0},
            {"png", "jpeg", "webp", "gtiff"}
        },
        gis::framework::ParamSpec{
            "jpeg_quality", "JPEG压缩质量", "JPEG/WebP压缩质量(1-100)",
            gis::framework::ParamType::Int, false, int{75},
            int{1}, int{100}
        },
        gis::framework::ParamSpec{
            "add_alpha", "添加透明通道", "为瓦片添加Alpha透明通道，NoData区域变透明",
            gis::framework::ParamType::Bool, false, bool{true}
        },
        gis::framework::ParamSpec{
            "nodata_value", "NoData值", "指定输入NoData值，覆盖文件内已有值",
            gis::framework::ParamType::Double, false, double{-9999.0},
            double{-1e15}, double{1e15}
        },
        gis::framework::ParamSpec{
            "skip_blank", "跳过空白瓦片", "跳过全透明或全NoData的空白瓦片",
            gis::framework::ParamType::Bool, false, bool{true}
        },
        gis::framework::ParamSpec{
            "resume", "续切模式", "仅生成缺失的瓦片文件",
            gis::framework::ParamType::Bool, false, bool{false}
        },
        gis::framework::ParamSpec{
            "tile_extent", "切片范围", "自定义切片范围(xmin,ymin,xmax,ymax WGS84)，留空使用全图",
            gis::framework::ParamType::Extent, false, std::array<double,4>{0,0,0,0}
        },
        gis::framework::ParamSpec{
            "webviewer", "Web预览页面", "生成Web地图预览页面",
            gis::framework::ParamType::Enum, false, std::string{"none"},
            int{0}, int{0},
            {"none", "leaflet", "openlayers", "all"}
        },
        gis::framework::ParamSpec{
            "title", "地图标题", "Web预览页面的地图标题",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "copyright", "版权信息", "Web预览页面的版权信息",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "num_threads", "并行线程数", "瓦片生成的并行线程数",
            gis::framework::ParamType::Int, false, int{1},
            int{1}, int{64}
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
    if (action == "tile")        return doTile(params, progress);

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

static void lonLatToTileXYZ(double lon, double lat, int z, int& x, int& y) {
    x = static_cast<int>(std::floor((lon + 180.0) / 360.0 * (1 << z)));
    const double latRad = lat * M_PI / 180.0;
    y = static_cast<int>(std::floor((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * (1 << z)));
    x = std::max(0, std::min(x, (1 << z) - 1));
    y = std::max(0, std::min(y, (1 << z) - 1));
}

static void tileXYToLonLat(int x, int y, int z, double& lon, double& lat) {
    const double n = M_PI * (1.0 - 2.0 * y / static_cast<double>(1 << z));
    lon = x / static_cast<double>(1 << z) * 360.0 - 180.0;
    lat = 180.0 / M_PI * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
}

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

static void writeLeafletHtml(const std::string& outputDir, int minZ, int maxZ,
                              double centerLon, double centerLat,
                              const std::string& title, const std::string& copyright) {
    std::ofstream html(outputDir + "/leaflet.html");
    html << "<!DOCTYPE html>\n<html><head><meta charset='utf-8'><title>"
         << (title.empty() ? "Tile Preview" : title)
         << "</title>\n"
         << "<link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'/>\n"
         << "<script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'></script>\n"
         << "<style>body{margin:0}#map{width:100%;height:100vh}</style></head><body>\n"
         << "<div id='map'></div><script>\n"
         << "var map=L.map('map').setView([" << centerLat << "," << centerLon << "]," << std::max(minZ, std::min(maxZ, minZ+2)) << ");\n"
         << "L.tileLayer('./{z}/{x}/{y}.png',{minZoom:" << minZ << ",maxZoom:" << maxZ
         << ",attribution:'" << (copyright.empty() ? "" : copyright) << "'}).addTo(map);\n"
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

    progress.throwIfCancelled();
    progress.onProgress(0.02);
    progress.onMessage("Opening input raster...");

    auto srcDS = gis::core::openRaster(input, true);
    double geoTransform[6] = {};
    srcDS->GetGeoTransform(geoTransform);
    const double pixelSize = std::abs(geoTransform[1]);

    if (maxZoom < 0) {
        maxZoom = computeMaxZoom(pixelSize, tileSize);
        progress.onMessage("Auto max_zoom = " + std::to_string(maxZoom));
    }
    maxZoom = std::min(maxZoom, 22);

    std::string targetSRS;
    if (profile == "mercator") targetSRS = "EPSG:3857";
    else if (profile == "geodetic") targetSRS = "EPSG:4326";
    else targetSRS = "";

    progress.onProgress(0.05);
    progress.onMessage("Reprojecting to " + (targetSRS.empty() ? "original SRS" : targetSRS) + "...");

    GDALDataset* warpedDS = nullptr;
    std::string warpedPath;

    if (!targetSRS.empty()) {
        warpedPath = (std::filesystem::temp_directory_path() / "gis_tile_warped.tif").string();
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
            return gis::framework::Result::fail("Reprojection failed: " + std::string(CPLGetLastErrorMsg()));
        }
        warpedDS = static_cast<GDALDataset*>(hWarped);
    } else {
        warpedDS = srcDS.get();
    }

    progress.throwIfCancelled();
    progress.onProgress(0.1);

    double wgt[6] = {};
    warpedDS->GetGeoTransform(wgt);
    const int wWidth = warpedDS->GetRasterXSize();
    const int wHeight = warpedDS->GetRasterYSize();

    double minLon, minLat, maxLon, maxLat;
    if (extentArr[0] != 0 || extentArr[1] != 0 || extentArr[2] != 0 || extentArr[3] != 0) {
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

    progress.onMessage("Bounds: " + std::to_string(minLon) + "," + std::to_string(minLat) +
                       " to " + std::to_string(maxLon) + "," + std::to_string(maxLat));

    std::filesystem::create_directories(output);

    int totalTiles = 0;
    for (int z = minZoom; z <= maxZoom; ++z) {
        int xMin, yMin, xMax, yMax;
        lonLatToTileXYZ(minLon, maxLat, z, xMin, yMin);
        lonLatToTileXYZ(maxLon, minLat, z, xMax, yMax);
        totalTiles += (xMax - xMin + 1) * (yMax - yMin + 1);
    }

    progress.onMessage("Total tiles to generate: " + std::to_string(totalTiles));
    progress.onProgress(0.12);

    int tilesGenerated = 0;
    const std::string ext = tileFileExt(fmt);

    for (int z = minZoom; z <= maxZoom; ++z) {
        progress.throwIfCancelled();

        int xMin, yMin, xMax, yMax;
        lonLatToTileXYZ(minLon, maxLat, z, xMin, yMin);
        lonLatToTileXYZ(maxLon, minLat, z, xMax, yMax);

        const int tilesInLevel = (xMax - xMin + 1) * (yMax - yMin + 1);
        progress.onMessage("Zoom " + std::to_string(z) + ": " + std::to_string(tilesInLevel) + " tiles");

        const double levelRes = mercatorResolution(z, tileSize);
        const int globalSize = tileSize * (1 << z);

        GDALDataset* levelDS = warpedDS;
        std::string levelPath;

        if (z < maxZoom) {
            levelPath = (std::filesystem::temp_directory_path() /
                ("gis_tile_level_" + std::to_string(z) + ".tif")).string();

            std::vector<std::string> translateArgStorage;
            translateArgStorage.push_back("-outsize");
            translateArgStorage.push_back(std::to_string(globalSize));
            translateArgStorage.push_back(std::to_string(globalSize));
            translateArgStorage.push_back("-r");
            translateArgStorage.push_back(ovResampling);
            translateArgStorage.push_back("-co");
            translateArgStorage.push_back("COMPRESS=LZW");

            std::vector<char*> translateArgv;
            for (auto& a : translateArgStorage) translateArgv.push_back(a.data());
            translateArgv.push_back(nullptr);

            GDALTranslateOptions* tOpts = GDALTranslateOptionsNew(translateArgv.data(), nullptr);
            auto hLevel = GDALTranslate(levelPath.c_str(),
                GDALDataset::ToHandle(warpedDS), tOpts, nullptr);
            GDALTranslateOptionsFree(tOpts);
            if (hLevel) {
                levelDS = static_cast<GDALDataset*>(hLevel);
            }
        }

        const int srcBands = levelDS->GetRasterCount();
        int hasNd = 0;
        double srcNodata = levelDS->GetRasterBand(1)->GetNoDataValue(&hasNd);
        if (!hasNd) srcNodata = nodataVal;

        for (int tx = xMin; tx <= xMax; ++tx) {
            for (int ty = yMin; ty <= yMax; ++ty) {
                int tyWrite = ty;
                if (scheme == "tms") {
                    tyWrite = (1 << z) - 1 - ty;
                }

                const std::string tileDir = output + "/" + std::to_string(z) + "/" + std::to_string(tx);
                const std::string tilePath = tileDir + "/" + std::to_string(tyWrite) + ext;

                if (resume && std::filesystem::exists(tilePath)) {
                    ++tilesGenerated;
                    continue;
                }

                double tileMinLon, tileMinLat, tileMaxLon, tileMaxLat;
                tileXYToLonLat(tx, ty, z, tileMinLon, tileMaxLat);
                tileXYToLonLat(tx + 1, ty + 1, z, tileMaxLon, tileMinLat);

                double tileGeoTransform[6] = {};
                std::copy(wgt, wgt + 6, tileGeoTransform);
                const double dstXMin = tileGeoTransform[0] + (tileMinLon - wgt[0]) / wgt[1] * wgt[1];
                const double dstYMin = tileGeoTransform[3] + (tileMinLat - wgt[3]) / wgt[5] * wgt[5];

                std::vector<double> srcWin = {
                    (tileMinLon - wgt[0]) / wgt[1],
                    (tileMaxLat - wgt[3]) / wgt[5],
                    (tileMaxLon - tileMinLon) / wgt[1],
                    (tileMinLat - tileMaxLat) / (-wgt[5])
                };

                srcWin[0] = std::max(0.0, srcWin[0]);
                srcWin[1] = std::max(0.0, srcWin[1]);
                srcWin[2] = std::min(static_cast<double>(levelDS->GetRasterXSize()) - srcWin[0], srcWin[2]);
                srcWin[3] = std::min(static_cast<double>(levelDS->GetRasterYSize()) - srcWin[1], srcWin[3]);

                if (srcWin[2] <= 0 || srcWin[3] <= 0) {
                    ++tilesGenerated;
                    continue;
                }

                cv::Mat tileData(static_cast<int>(srcWin[3]), static_cast<int>(srcWin[2]), CV_32F);
                CPLErr err = levelDS->GetRasterBand(1)->RasterIO(
                    GF_Read, static_cast<int>(srcWin[0]), static_cast<int>(srcWin[1]),
                    static_cast<int>(srcWin[2]), static_cast<int>(srcWin[3]),
                    tileData.data, tileData.cols, tileData.rows, GDT_Float32, 0, 0);

                if (err != CE_None) {
                    ++tilesGenerated;
                    continue;
                }

                cv::resize(tileData, tileData, cv::Size(tileSize, tileSize), 0, 0, cv::INTER_LINEAR);

                cv::Mat rgba(tileSize, tileSize, CV_8UC4, cv::Scalar(0, 0, 0, 0));
                double bandMin = 1e30, bandMax = -1e30;
                for (int py = 0; py < tileSize; ++py) {
                    for (int px = 0; px < tileSize; ++px) {
                        const float v = tileData.at<float>(py, px);
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
                        if (hasNd && std::abs(v - srcNodata) < 1e-6) {
                            rgba.at<cv::Vec4b>(py, px) = cv::Vec4b(0, 0, 0, 0);
                        } else {
                            const uchar c = static_cast<uchar>(255.0 * (v - bandMin) / range);
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

        if (levelDS != warpedDS) {
            GDALClose(levelDS);
            std::filesystem::remove(levelPath);
        }
    }

    if (warpedDS != srcDS.get()) {
        GDALClose(warpedDS);
        std::filesystem::remove(warpedPath);
    }

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
