#include "utility_plugin.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <cpl_conv.h>
#include <opencv2/opencv.hpp>
#include <sstream>
#include <fstream>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <filesystem>

namespace gis::plugins {

namespace {

void ensureParentDirectoryForFile(const std::string& path) {
    const std::filesystem::path fsPath(path);
    if (!fsPath.has_parent_path()) {
        return;
    }

    std::filesystem::create_directories(fsPath.parent_path());
}

} // namespace

std::vector<gis::framework::ParamSpec> UtilityPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"colormap"}
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
            "band", "波段序号", "处理的波段序号(0表示所有波段,从1开始)",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "levels", "金字塔层级", "金字塔缩放层级(如 2 4 8 16)",
            gis::framework::ParamType::String, false, std::string{"2 4 8 16"}
        },
        gis::framework::ParamSpec{
            "resample", "重采样方法", "金字塔重采样算法",
            gis::framework::ParamType::Enum, false, std::string{"nearest"},
            int{0}, int{0},
            {"nearest", "gaussian", "cubic", "average", "mode"}
        },
        gis::framework::ParamSpec{
            "nodata_value", "NoData值", "要设置的NoData值",
            gis::framework::ParamType::Double, false, double{0.0}
        },
        gis::framework::ParamSpec{
            "bins", "直方图bin数", "直方图分箱数量",
            gis::framework::ParamType::Int, false, int{256}
        },
        gis::framework::ParamSpec{
            "cmap", "色彩映射", "色彩映射方案",
            gis::framework::ParamType::Enum, false, std::string{"jet"},
            int{0}, int{0},
            {"jet", "viridis", "hot", "cool", "spring", "summer", "autumn", "winter", "bone", "hsv", "rainbow", "ocean"}
        },
    };
}

gis::framework::Result UtilityPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string action = gis::framework::getParam<std::string>(params, "action", "");

    if (action == "colormap")  return doColormap(params, progress);

    return gis::framework::Result::fail("Unknown action: " + action);
}

static int mapColormapName(const std::string& name) {
    if (name == "autumn")  return cv::COLORMAP_AUTUMN;
    if (name == "bone")    return cv::COLORMAP_BONE;
    if (name == "jet")     return cv::COLORMAP_JET;
    if (name == "winter")  return cv::COLORMAP_WINTER;
    if (name == "rainbow") return cv::COLORMAP_RAINBOW;
    if (name == "ocean")   return cv::COLORMAP_OCEAN;
    if (name == "summer")  return cv::COLORMAP_SUMMER;
    if (name == "spring")  return cv::COLORMAP_SPRING;
    if (name == "cool")    return cv::COLORMAP_COOL;
    if (name == "hsv")     return cv::COLORMAP_HSV;
    if (name == "hot")     return cv::COLORMAP_HOT;
    if (name == "viridis") return cv::COLORMAP_VIRIDIS;
    return cv::COLORMAP_JET;
}

gis::framework::Result UtilityPlugin::doColormap(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);
    std::string cmap = gis::framework::getParam<std::string>(params, "cmap", "jet");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);

    cv::Mat mat = gis::core::gdalBandToMat(gis::core::openRaster(input, true).get(), band);
    progress.onProgress(0.3);

    cv::Mat u8;
    double minVal, maxVal;
    cv::minMaxLoc(mat, &minVal, &maxVal);
    if (maxVal - minVal < 1e-10) {
        return gis::framework::Result::fail("Band has no value range for color mapping");
    }
    cv::normalize(mat, u8, 0, 255, cv::NORM_MINMAX, CV_8U);
    progress.onProgress(0.5);

    int cmapId = mapColormapName(cmap);
    cv::Mat colored;
    cv::applyColorMap(u8, colored, cmapId);
    progress.onProgress(0.7);

    auto srcDS = gis::core::openRaster(input, true);
    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    ensureParentDirectoryForFile(output);
    GDALDataset* dstDS = drv->Create(output.c_str(),
        colored.cols, colored.rows, 3, GDT_Byte, nullptr);
    if (!dstDS) {
        return gis::framework::Result::fail("Cannot create output: " + output);
    }

    double adfGT[6];
    if (srcDS->GetGeoTransform(adfGT) == CE_None) {
        dstDS->SetGeoTransform(adfGT);
    }
    std::string srcWkt = gis::core::getSRSWKT(srcDS.get());
    if (!srcWkt.empty()) {
        dstDS->SetProjection(srcWkt.c_str());
    }

    std::vector<cv::Mat> bgrChannels;
    cv::split(colored, bgrChannels);
    for (int i = 0; i < 3; ++i) {
        GDALRasterBand* outBand = dstDS->GetRasterBand(i + 1);
        outBand->RasterIO(GF_Write, 0, 0, colored.cols, colored.rows,
            bgrChannels[i].data, colored.cols, colored.rows, GDT_Byte, 0, 0);
        outBand->SetColorInterpretation(static_cast<GDALColorInterp>(GCI_RedBand + i));
    }

    GDALClose(dstDS);
    progress.onProgress(1.0);

    return gis::framework::Result::ok("Colormap applied: " + cmap, output);
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::UtilityPlugin)
