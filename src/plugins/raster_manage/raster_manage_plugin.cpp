#include "raster_manage_plugin.h"

#include <gis/core/error.h>
#include <gis/core/gdal_wrapper.h>

#include <algorithm>
#include <cpl_conv.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <sstream>
#include <string>
#include <vector>

namespace gis::plugins {

std::vector<gis::framework::ParamSpec> RasterManagePlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"overviews", "nodata", "cog"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入影像文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "band", "波段序号", "波段序号，填写 0 表示全部波段，从 1 开始表示单波段",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "levels", "金字塔层级", "金字塔缩放层级，例如 2 4 8 16",
            gis::framework::ParamType::String, false, std::string{"2 4 8 16"}
        },
        gis::framework::ParamSpec{
            "resample", "重采样方式", "金字塔重采样算法",
            gis::framework::ParamType::Enum, false, std::string{"nearest"},
            int{0}, int{0},
            {"nearest", "gaussian", "cubic", "average", "mode"}
        },
        gis::framework::ParamSpec{
            "nodata_value", "NoData 值", "要写入的 NoData 数值",
            gis::framework::ParamType::Double, false, double{0.0}
        },
    };
}

gis::framework::Result RasterManagePlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string action = gis::framework::getParam<std::string>(params, "action", "");

    if (action == "overviews") return doBuildOverviews(params, progress);
    if (action == "cog")       return doBuildCog(params, progress);
    if (action == "nodata")    return doSetNoData(params, progress);

    return gis::framework::Result::fail("Unknown action: " + action);
}

gis::framework::Result RasterManagePlugin::doBuildOverviews(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string levelsStr = gis::framework::getParam<std::string>(params, "levels", "2 4 8 16");
    const std::string resample = gis::framework::getParam<std::string>(params, "resample", "nearest");

    if (input.empty()) return gis::framework::Result::fail("input is required");

    progress.onProgress(0.1);
    auto ds = gis::core::openRaster(input, false);

    std::vector<int> ovrLevels;
    std::istringstream iss(levelsStr);
    int level = 0;
    while (iss >> level) {
        if (level > 1) {
            ovrLevels.push_back(level);
        }
    }
    if (ovrLevels.empty()) {
        return gis::framework::Result::fail("No valid overview levels specified (must be > 1)");
    }

    std::string resampleUpper = resample;
    std::transform(resampleUpper.begin(), resampleUpper.end(), resampleUpper.begin(), ::toupper);

    progress.onMessage("Building overviews at levels: " + levelsStr + " using " + resample);

    const CPLErr err = ds->BuildOverviews(
        resampleUpper.c_str(),
        static_cast<int>(ovrLevels.size()),
        ovrLevels.data(),
        0, nullptr, nullptr, nullptr);

    if (err != CE_None) {
        return gis::framework::Result::fail(
            "Failed to build overviews: " + std::string(CPLGetLastErrorMsg()));
    }

    progress.onProgress(1.0);
    return gis::framework::Result::ok("Overviews built successfully: " + levelsStr, input);
}

gis::framework::Result RasterManagePlugin::doBuildCog(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);
    auto ds = gis::core::openRaster(input, true);

    std::vector<std::string> argStorage = {
        "-of", "COG",
        "-co", "COMPRESS=LZW",
        "-co", "BIGTIFF=IF_SAFER"
    };
    std::vector<char*> argv;
    argv.reserve(argStorage.size() + 1);
    for (auto& item : argStorage) {
        argv.push_back(item.data());
    }
    argv.push_back(nullptr);

    GDALTranslateOptions* translateOpts = GDALTranslateOptionsNew(argv.data(), nullptr);
    if (!translateOpts) {
        return gis::framework::Result::fail("Failed to create COG translate options");
    }

    progress.onMessage("Building Cloud Optimized GeoTIFF: " + output);
    progress.onProgress(0.4);

    int usageError = FALSE;
    GDALDatasetH dstHandle = GDALTranslate(
        output.c_str(),
        GDALDataset::ToHandle(ds.get()),
        translateOpts,
        &usageError);
    GDALTranslateOptionsFree(translateOpts);

    if (!dstHandle || usageError) {
        if (dstHandle) {
            GDALClose(dstHandle);
        }
        return gis::framework::Result::fail(
            "Failed to build COG: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALClose(dstHandle);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("COG built successfully", output);
    result.metadata["format"] = "COG";
    result.metadata["compression"] = "LZW";
    return result;
}

gis::framework::Result RasterManagePlugin::doSetNoData(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const int band = gis::framework::getParam<int>(params, "band", 0);
    const double nodataVal = gis::framework::getParam<double>(params, "nodata_value", 0.0);

    if (input.empty()) return gis::framework::Result::fail("input is required");

    progress.onProgress(0.1);
    auto ds = gis::core::openRaster(input, false);

    if (band <= 0) {
        const int bandCount = ds->GetRasterCount();
        for (int b = 1; b <= bandCount; ++b) {
            ds->GetRasterBand(b)->SetNoDataValue(nodataVal);
        }
        progress.onProgress(1.0);
        return gis::framework::Result::ok(
            "NoData set to " + std::to_string(nodataVal) + " for all " +
            std::to_string(bandCount) + " bands", input);
    }

    auto* rasterBand = ds->GetRasterBand(band);
    if (!rasterBand) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    rasterBand->SetNoDataValue(nodataVal);
    progress.onProgress(1.0);
    return gis::framework::Result::ok(
        "NoData set to " + std::to_string(nodataVal) + " for band " + std::to_string(band), input);
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::RasterManagePlugin)
