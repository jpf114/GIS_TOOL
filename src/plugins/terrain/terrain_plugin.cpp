#include "terrain_plugin.h"

#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>

#include <cpl_conv.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace gis::plugins {

namespace {

struct DemOptionsDeleter {
    void operator()(GDALDEMProcessingOptions* options) const {
        if (options) {
            GDALDEMProcessingOptionsFree(options);
        }
    }
};

gis::framework::Result runDemProcess(
    const char* processName,
    const std::string& successLabel,
    const std::string& input,
    const std::string& output,
    int band,
    double zFactor,
    double azimuth,
    double altitude,
    gis::core::ProgressReporter& progress) {

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");
    if (zFactor <= 0.0) return gis::framework::Result::fail("z_factor must be greater than 0");

    progress.onProgress(0.1);
    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        return gis::framework::Result::fail("Cannot open DEM raster: " + input);
    }
    if (!srcDs->GetRasterBand(band)) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    std::vector<std::string> argStorage = {
        "-of", "GTiff",
        "-b", std::to_string(band),
        "-z", std::to_string(zFactor)
    };
    if (std::string(processName) == "hillshade") {
        argStorage.push_back("-az");
        argStorage.push_back(std::to_string(azimuth));
        argStorage.push_back("-alt");
        argStorage.push_back(std::to_string(altitude));
    }

    std::vector<char*> argv;
    argv.reserve(argStorage.size() + 1);
    for (auto& item : argStorage) {
        argv.push_back(item.data());
    }
    argv.push_back(nullptr);

    std::unique_ptr<GDALDEMProcessingOptions, DemOptionsDeleter> options(
        GDALDEMProcessingOptionsNew(argv.data(), nullptr));
    if (!options) {
        return gis::framework::Result::fail("Failed to create terrain processing options");
    }

    progress.onMessage("Running terrain action: " + successLabel);
    progress.onProgress(0.4);

    int usageError = FALSE;
    GDALDatasetH outHandle = GDALDEMProcessing(
        output.c_str(),
        GDALDataset::ToHandle(srcDs.get()),
        processName,
        nullptr,
        options.get(),
        &usageError);

    if (!outHandle || usageError) {
        if (outHandle) {
            GDALClose(outHandle);
        }
        return gis::framework::Result::fail(
            "Terrain processing failed: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALClose(outHandle);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(successLabel + " completed", output);
    result.metadata["action"] = processName;
    result.metadata["band"] = std::to_string(band);
    result.metadata["z_factor"] = std::to_string(zFactor);
    if (std::string(processName) == "hillshade") {
        result.metadata["azimuth"] = std::to_string(azimuth);
        result.metadata["altitude"] = std::to_string(altitude);
    }
    return result;
}

gis::framework::Result runLocalTerrainProcess(
    const std::string& action,
    const std::string& successLabel,
    const std::string& input,
    const std::string& output,
    int band,
    double zFactor,
    gis::core::ProgressReporter& progress) {

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");
    if (zFactor <= 0.0) return gis::framework::Result::fail("z_factor must be greater than 0");

    progress.onProgress(0.1);
    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        return gis::framework::Result::fail("Cannot open DEM raster: " + input);
    }

    auto* srcBand = srcDs->GetRasterBand(band);
    if (!srcBand) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    progress.onMessage("Reading terrain raster band");
    cv::Mat elevation = gis::core::gdalBandToMat(srcDs.get(), band);
    if (zFactor != 1.0) {
        elevation *= static_cast<float>(zFactor);
    }
    progress.onProgress(0.35);

    cv::Mat result;
    if (action == "tpi") {
        cv::Mat neighborhoodMean;
        cv::boxFilter(
            elevation,
            neighborhoodMean,
            CV_32F,
            cv::Size(3, 3),
            cv::Point(-1, -1),
            true,
            cv::BORDER_REPLICATE);
        result = elevation - ((neighborhoodMean * 9.0f - elevation) / 8.0f);
    } else if (action == "roughness") {
        cv::Mat neighborhoodMax;
        cv::Mat neighborhoodMin;
        cv::dilate(elevation, neighborhoodMax, cv::Mat(), cv::Point(-1, -1), 1, cv::BORDER_REPLICATE);
        cv::erode(elevation, neighborhoodMin, cv::Mat(), cv::Point(-1, -1), 1, cv::BORDER_REPLICATE);
        result = neighborhoodMax - neighborhoodMin;
    } else {
        return gis::framework::Result::fail("Unknown local terrain action: " + action);
    }
    progress.onProgress(0.7);

    int hasNoData = 0;
    const double noDataValue = srcBand->GetNoDataValue(&hasNoData);
    if (hasNoData) {
        cv::Mat noDataMask;
        cv::compare(elevation, static_cast<float>(noDataValue * zFactor), noDataMask, cv::CMP_EQ);
        result.setTo(static_cast<float>(noDataValue), noDataMask);
    }

    progress.onMessage("Writing terrain output: " + output);
    gis::core::matToGdalTiff(result, srcDs.get(), output, band);
    progress.onProgress(1.0);

    auto terrainResult = gis::framework::Result::ok(successLabel + " completed", output);
    terrainResult.metadata["action"] = action;
    terrainResult.metadata["band"] = std::to_string(band);
    terrainResult.metadata["z_factor"] = std::to_string(zFactor);
    terrainResult.metadata["window_size"] = "3";
    return terrainResult;
}

} // namespace

std::vector<gis::framework::ParamSpec> TerrainPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的地形分析子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"slope", "aspect", "hillshade", "tpi", "roughness"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入 DEM 栅格路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出文件", "输出 GeoTIFF 路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "band", "波段序号", "参与计算的高程波段序号，从 1 开始",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "z_factor", "高程缩放", "高程与平面单位换算系数",
            gis::framework::ParamType::Double, false, double{1.0}
        },
        gis::framework::ParamSpec{
            "azimuth", "方位角", "山体阴影光源方位角，单位为度",
            gis::framework::ParamType::Double, false, double{315.0}
        },
        gis::framework::ParamSpec{
            "altitude", "高度角", "山体阴影光源高度角，单位为度",
            gis::framework::ParamType::Double, false, double{45.0}
        },
    };
}

gis::framework::Result TerrainPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string action = gis::framework::getParam<std::string>(params, "action", "");
    if (action == "slope") return doSlope(params, progress);
    if (action == "aspect") return doAspect(params, progress);
    if (action == "hillshade") return doHillshade(params, progress);
    if (action == "tpi") return doTpi(params, progress);
    if (action == "roughness") return doRoughness(params, progress);
    return gis::framework::Result::fail("Unknown action: " + action);
}

gis::framework::Result TerrainPlugin::doSlope(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runDemProcess(
        "slope",
        "Slope",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        315.0,
        45.0,
        progress);
}

gis::framework::Result TerrainPlugin::doAspect(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runDemProcess(
        "aspect",
        "Aspect",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        315.0,
        45.0,
        progress);
}

gis::framework::Result TerrainPlugin::doHillshade(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runDemProcess(
        "hillshade",
        "Hillshade",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        gis::framework::getParam<double>(params, "azimuth", 315.0),
        gis::framework::getParam<double>(params, "altitude", 45.0),
        progress);
}

gis::framework::Result TerrainPlugin::doTpi(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runLocalTerrainProcess(
        "tpi",
        "TPI",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        progress);
}

gis::framework::Result TerrainPlugin::doRoughness(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runLocalTerrainProcess(
        "roughness",
        "Roughness",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        progress);
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::TerrainPlugin)
