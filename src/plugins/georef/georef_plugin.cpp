#include "georef_plugin.h"

#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>

#include <gdal_priv.h>
#include <opencv2/core.hpp>

namespace gis::plugins {

std::vector<gis::framework::ParamSpec> GeorefPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "几何校正与辐射处理功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0}, {"dos_correction", "radiometric_calibration"}
        },
        gis::framework::ParamSpec{
            "input", "输入栅格", "待处理栅格影像路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出栅格", "输出校正后栅格路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "band", "波段序号", "待处理波段序号，从 1 开始",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "dark_object_value", "暗像元值", "小于 0 时自动使用当前波段最小值",
            gis::framework::ParamType::Double, false, double{-1.0}
        },
        gis::framework::ParamSpec{
            "gain", "增益", "辐射定标增益系数",
            gis::framework::ParamType::Double, false, double{1.0}
        },
        gis::framework::ParamSpec{
            "offset", "偏移", "辐射定标偏移量",
            gis::framework::ParamType::Double, false, double{0.0}
        },
    };
}

gis::framework::Result GeorefPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string action = gis::framework::getParam<std::string>(params, "action", "");
    if (action == "dos_correction") {
        return doDosCorrection(params, progress);
    }
    if (action == "radiometric_calibration") {
        return doRadiometricCalibration(params, progress);
    }
    return gis::framework::Result::fail("Unknown action: " + action);
}

gis::framework::Result GeorefPlugin::doDosCorrection(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const int band = gis::framework::getParam<int>(params, "band", 1);
    double darkObject = gis::framework::getParam<double>(params, "dark_object_value", -1.0);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");

    progress.onMessage("Reading raster band for DOS correction");
    progress.onProgress(0.2);
    cv::Mat bandMat = gis::core::readBandAsMat(input, band);

    double minValue = 0.0;
    double maxValue = 0.0;
    cv::minMaxLoc(bandMat, &minValue, &maxValue);
    if (darkObject < 0.0) {
        darkObject = minValue;
    }

    cv::Mat corrected = bandMat - darkObject;
    cv::max(corrected, 0.0, corrected);

    const double scaleDenominator = maxValue - darkObject;
    if (scaleDenominator > 1e-12) {
        corrected.convertTo(corrected, CV_32F, 1.0 / scaleDenominator);
    } else {
        corrected = cv::Mat::zeros(corrected.size(), CV_32F);
    }

    progress.onMessage("Writing DOS correction result");
    progress.onProgress(0.75);
    gis::core::matToGdalTiff(corrected, input, output, band);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("DOS correction completed", output);
    result.metadata["action"] = "dos_correction";
    result.metadata["band"] = std::to_string(band);
    result.metadata["dark_object_value"] = std::to_string(darkObject);
    return result;
}

gis::framework::Result GeorefPlugin::doRadiometricCalibration(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const int band = gis::framework::getParam<int>(params, "band", 1);
    const double gain = gis::framework::getParam<double>(params, "gain", 1.0);
    const double offset = gis::framework::getParam<double>(params, "offset", 0.0);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");

    progress.onMessage("Reading raster band for radiometric calibration");
    progress.onProgress(0.2);
    cv::Mat bandMat = gis::core::readBandAsMat(input, band);

    cv::Mat calibrated = bandMat * gain + offset;
    calibrated.convertTo(calibrated, CV_32F);

    progress.onMessage("Writing radiometric calibration result");
    progress.onProgress(0.75);
    gis::core::matToGdalTiff(calibrated, input, output, band);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("Radiometric calibration completed", output);
    result.metadata["action"] = "radiometric_calibration";
    result.metadata["band"] = std::to_string(band);
    result.metadata["gain"] = std::to_string(gain);
    result.metadata["offset"] = std::to_string(offset);
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::GeorefPlugin)
