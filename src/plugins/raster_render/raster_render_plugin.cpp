#include "raster_render_plugin.h"

#include <gis/core/error.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>

#include <array>
#include <filesystem>
#include <gdal_priv.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace gis::plugins {

namespace {

void ensureParentDirectoryForFile(const std::string& path) {
    const std::filesystem::path fsPath(path);
    if (!fsPath.has_parent_path()) {
        return;
    }
    std::filesystem::create_directories(fsPath.parent_path());
}

int mapColormapName(const std::string& name) {
    if (name == "autumn") return cv::COLORMAP_AUTUMN;
    if (name == "bone") return cv::COLORMAP_BONE;
    if (name == "jet") return cv::COLORMAP_JET;
    if (name == "winter") return cv::COLORMAP_WINTER;
    if (name == "rainbow") return cv::COLORMAP_RAINBOW;
    if (name == "ocean") return cv::COLORMAP_OCEAN;
    if (name == "summer") return cv::COLORMAP_SUMMER;
    if (name == "spring") return cv::COLORMAP_SPRING;
    if (name == "cool") return cv::COLORMAP_COOL;
    if (name == "hsv") return cv::COLORMAP_HSV;
    if (name == "hot") return cv::COLORMAP_HOT;
    if (name == "viridis") return cv::COLORMAP_VIRIDIS;
    return cv::COLORMAP_JET;
}

std::array<uint8_t, 256> buildHistogramMatchLut(const cv::Mat& source, const cv::Mat& reference) {
    std::array<int, 256> sourceHist{};
    std::array<int, 256> referenceHist{};

    for (int y = 0; y < source.rows; ++y) {
        const auto* sourceRow = source.ptr<uint8_t>(y);
        const auto* referenceRow = reference.ptr<uint8_t>(y);
        for (int x = 0; x < source.cols; ++x) {
            ++sourceHist[sourceRow[x]];
            ++referenceHist[referenceRow[x]];
        }
    }

    std::array<double, 256> sourceCdf{};
    std::array<double, 256> referenceCdf{};
    const double sourceTotal = static_cast<double>(source.rows * source.cols);
    const double referenceTotal = static_cast<double>(reference.rows * reference.cols);
    double sourceAccum = 0.0;
    double referenceAccum = 0.0;
    for (int i = 0; i < 256; ++i) {
        sourceAccum += static_cast<double>(sourceHist[i]);
        referenceAccum += static_cast<double>(referenceHist[i]);
        sourceCdf[i] = sourceAccum / sourceTotal;
        referenceCdf[i] = referenceAccum / referenceTotal;
    }

    std::array<uint8_t, 256> lut{};
    int referenceIndex = 0;
    for (int i = 0; i < 256; ++i) {
        while (referenceIndex < 255 && referenceCdf[referenceIndex] < sourceCdf[i]) {
            ++referenceIndex;
        }
        lut[i] = static_cast<uint8_t>(referenceIndex);
    }
    return lut;
}

void copyRasterGeoMetadata(GDALDataset* srcDS, GDALDataset* dstDS) {
    if (!srcDS || !dstDS) {
        return;
    }

    double adfGT[6];
    if (srcDS->GetGeoTransform(adfGT) == CE_None) {
        dstDS->SetGeoTransform(adfGT);
    }
    const std::string srcWkt = gis::core::getSRSWKT(srcDS);
    if (!srcWkt.empty()) {
        dstDS->SetProjection(srcWkt.c_str());
    }
}

} // namespace

std::vector<gis::framework::ParamSpec> RasterRenderPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"colormap", "histogram_match"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入影像文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "reference", "参考影像", "直方图匹配使用的参考影像路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出文件", "输出结果路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "band", "波段序号", "处理的波段序号，从 1 开始",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "cmap", "颜色映射", "伪彩色映射方案",
            gis::framework::ParamType::Enum, false, std::string{"jet"},
            int{0}, int{0},
            {"jet", "viridis", "hot", "cool", "spring", "summer", "autumn", "winter", "bone", "hsv", "rainbow", "ocean"}
        },
    };
}

gis::framework::Result RasterRenderPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string action = gis::framework::getParam<std::string>(params, "action", "");
    if (action == "colormap") {
        return doColormap(params, progress);
    }
    if (action == "histogram_match") {
        return doHistogramMatch(params, progress);
    }
    return gis::framework::Result::fail("Unknown action: " + action);
}

gis::framework::Result RasterRenderPlugin::doColormap(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const int band = gis::framework::getParam<int>(params, "band", 1);
    const std::string cmap = gis::framework::getParam<std::string>(params, "cmap", "jet");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);

    auto srcDS = gis::core::openRaster(input, true);
    cv::Mat mat = gis::core::gdalBandToMat(srcDS.get(), band);
    progress.onProgress(0.3);

    cv::Mat u8;
    double minVal = 0.0;
    double maxVal = 0.0;
    cv::minMaxLoc(mat, &minVal, &maxVal);
    if (maxVal - minVal < 1e-10) {
        return gis::framework::Result::fail("Band has no value range for color mapping");
    }
    cv::normalize(mat, u8, 0, 255, cv::NORM_MINMAX, CV_8U);
    progress.onProgress(0.5);

    cv::Mat colored;
    cv::applyColorMap(u8, colored, mapColormapName(cmap));
    progress.onProgress(0.7);

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    ensureParentDirectoryForFile(output);
    GDALDataset* dstDS = drv->Create(output.c_str(), colored.cols, colored.rows, 3, GDT_Byte, nullptr);
    if (!dstDS) {
        return gis::framework::Result::fail("Cannot create output: " + output);
    }

    copyRasterGeoMetadata(srcDS.get(), dstDS);

    std::vector<cv::Mat> bgrChannels;
    cv::split(colored, bgrChannels);
    for (int i = 0; i < 3; ++i) {
        GDALRasterBand* outBand = dstDS->GetRasterBand(i + 1);
        outBand->RasterIO(
            GF_Write, 0, 0, colored.cols, colored.rows,
            bgrChannels[static_cast<size_t>(i)].data, colored.cols, colored.rows, GDT_Byte, 0, 0);
        outBand->SetColorInterpretation(static_cast<GDALColorInterp>(GCI_RedBand + i));
    }

    GDALClose(dstDS);
    progress.onProgress(1.0);
    return gis::framework::Result::ok("Colormap applied: " + cmap, output);
}

gis::framework::Result RasterRenderPlugin::doHistogramMatch(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string reference = gis::framework::getParam<std::string>(params, "reference", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const int band = gis::framework::getParam<int>(params, "band", 1);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (reference.empty()) return gis::framework::Result::fail("reference is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);

    auto srcDS = gis::core::openRaster(input, true);
    auto refDS = gis::core::openRaster(reference, true);
    cv::Mat sourceMat = gis::core::gdalBandToMat(srcDS.get(), band);
    cv::Mat referenceMat = gis::core::gdalBandToMat(refDS.get(), band);
    progress.onProgress(0.35);

    cv::Mat sourceU8;
    cv::Mat referenceResized;
    if (referenceMat.size() != sourceMat.size()) {
        cv::resize(referenceMat, referenceResized, sourceMat.size(), 0.0, 0.0, cv::INTER_LINEAR);
    } else {
        referenceResized = referenceMat;
    }

    cv::Mat referenceU8;
    cv::normalize(sourceMat, sourceU8, 0, 255, cv::NORM_MINMAX, CV_8U);
    cv::normalize(referenceResized, referenceU8, 0, 255, cv::NORM_MINMAX, CV_8U);
    progress.onProgress(0.55);

    const auto lutValues = buildHistogramMatchLut(sourceU8, referenceU8);
    cv::Mat lut(1, 256, CV_8U);
    for (int i = 0; i < 256; ++i) {
        lut.at<uint8_t>(0, i) = lutValues[static_cast<size_t>(i)];
    }

    cv::Mat matchedU8;
    cv::LUT(sourceU8, lut, matchedU8);
    cv::Mat matchedFloat;
    matchedU8.convertTo(matchedFloat, CV_32F);
    progress.onProgress(0.75);

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    ensureParentDirectoryForFile(output);
    GDALDataset* dstDS = drv->Create(output.c_str(), matchedFloat.cols, matchedFloat.rows, 1, GDT_Float32, nullptr);
    if (!dstDS) {
        return gis::framework::Result::fail("Cannot create output: " + output);
    }

    copyRasterGeoMetadata(srcDS.get(), dstDS);

    GDALRasterBand* outBand = dstDS->GetRasterBand(1);
    outBand->RasterIO(
        GF_Write, 0, 0, matchedFloat.cols, matchedFloat.rows,
        matchedFloat.data, matchedFloat.cols, matchedFloat.rows, GDT_Float32, 0, 0);

    GDALClose(dstDS);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("Histogram match completed", output);
    result.metadata["action"] = "histogram_match";
    result.metadata["band"] = std::to_string(band);
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::RasterRenderPlugin)
