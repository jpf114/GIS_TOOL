#include "spindex_plugin.h"

#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>
#include <gis/core/error.h>

#include <gdal_priv.h>
#include <opencv2/opencv.hpp>

namespace gis::plugins {

std::vector<gis::framework::ParamSpec> SpindexPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"ndvi"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入影像文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出文件", "输出文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "red_band", "红波段", "NDVI计算的红波段序号",
            gis::framework::ParamType::Int, false, int{3}
        },
        gis::framework::ParamSpec{
            "nir_band", "近红外波段", "NDVI计算的近红外波段序号",
            gis::framework::ParamType::Int, false, int{4}
        },
    };
}

gis::framework::Result SpindexPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string action = gis::framework::getParam<std::string>(params, "action", "");
    if (action == "ndvi") {
        return doComputeNdvi(params, progress);
    }

    return gis::framework::Result::fail("Unknown action: " + action);
}

gis::framework::Result SpindexPlugin::doComputeNdvi(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const int redBand = gis::framework::getParam<int>(params, "red_band", 3);
    const int nirBand = gis::framework::getParam<int>(params, "nir_band", 4);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.05);

    auto ds = gis::core::openRaster(input, true);
    const int bands = ds.get()->GetRasterCount();
    if (redBand < 1 || redBand > bands) {
        return gis::framework::Result::fail(
            "red_band " + std::to_string(redBand) +
            " out of range (1-" + std::to_string(bands) + ")");
    }
    if (nirBand < 1 || nirBand > bands) {
        return gis::framework::Result::fail(
            "nir_band " + std::to_string(nirBand) +
            " out of range (1-" + std::to_string(bands) + ")");
    }

    progress.onMessage(
        "Reading Red band " + std::to_string(redBand) +
        " and NIR band " + std::to_string(nirBand));

    cv::Mat red = gis::core::gdalBandToMat(ds.get(), redBand);
    progress.onProgress(0.25);
    cv::Mat nir = gis::core::gdalBandToMat(ds.get(), nirBand);
    progress.onProgress(0.5);

    progress.onMessage("Computing NDVI = (NIR - Red) / (NIR + Red)...");

    cv::Mat diff;
    cv::Mat sum;
    cv::Mat ndvi;
    cv::subtract(nir, red, diff);
    cv::add(nir, red, sum);
    sum += 1e-10f;
    cv::divide(diff, sum, ndvi);
    progress.onProgress(0.8);

    gis::core::matToGdalTiff(ndvi, input, output, 1);
    progress.onProgress(1.0);

    double ndviMin = 0.0;
    double ndviMax = 0.0;
    cv::minMaxLoc(ndvi, &ndviMin, &ndviMax);

    auto result = gis::framework::Result::ok(
        "NDVI computed: range [" + std::to_string(ndviMin) + ", " + std::to_string(ndviMax) + "]",
        output);
    result.metadata["ndvi_min"] = std::to_string(ndviMin);
    result.metadata["ndvi_max"] = std::to_string(ndviMax);
    result.metadata["red_band"] = std::to_string(redBand);
    result.metadata["nir_band"] = std::to_string(nirBand);
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::SpindexPlugin)
