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
            {"overviews", "nodata", "histogram", "info", "colormap"}
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

    if (action == "overviews") return doBuildOverviews(params, progress);
    if (action == "nodata")    return doSetNoData(params, progress);
    if (action == "histogram") return doHistogram(params, progress);
    if (action == "info")      return doRasterInfo(params, progress);
    if (action == "colormap")  return doColormap(params, progress);

    return gis::framework::Result::fail("Unknown action: " + action);
}

gis::framework::Result UtilityPlugin::doBuildOverviews(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string levelsStr = gis::framework::getParam<std::string>(params, "levels", "2 4 8 16");
    std::string resample = gis::framework::getParam<std::string>(params, "resample", "nearest");

    if (input.empty()) return gis::framework::Result::fail("input is required");

    progress.onProgress(0.1);
    auto ds = gis::core::openRaster(input, false);

    std::vector<int> ovrLevels;
    std::istringstream iss(levelsStr);
    int lvl;
    while (iss >> lvl) {
        if (lvl > 1) ovrLevels.push_back(lvl);
    }
    if (ovrLevels.empty()) {
        return gis::framework::Result::fail("No valid overview levels specified (must be > 1)");
    }

    std::string resampleUpper = resample;
    std::transform(resampleUpper.begin(), resampleUpper.end(), resampleUpper.begin(), ::toupper);

    progress.onMessage("Building overviews at levels: " + levelsStr + " using " + resample);

    CPLErr err = ds->BuildOverviews(resampleUpper.c_str(),
        static_cast<int>(ovrLevels.size()), ovrLevels.data(),
        0, nullptr, nullptr, nullptr);

    if (err != CE_None) {
        return gis::framework::Result::fail(
            "Failed to build overviews: " + std::string(CPLGetLastErrorMsg()));
    }

    progress.onProgress(1.0);
    return gis::framework::Result::ok("Overviews built successfully: " + levelsStr, input);
}

gis::framework::Result UtilityPlugin::doSetNoData(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    int band = gis::framework::getParam<int>(params, "band", 0);
    double nodataVal = gis::framework::getParam<double>(params, "nodata_value", 0.0);

    if (input.empty()) return gis::framework::Result::fail("input is required");

    progress.onProgress(0.1);
    auto ds = gis::core::openRaster(input, false);

    if (band <= 0) {
        int bandCount = ds->GetRasterCount();
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

gis::framework::Result UtilityPlugin::doHistogram(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);
    int bins = gis::framework::getParam<int>(params, "bins", 256);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (bins <= 0) {
        return gis::framework::Result::fail("bins must be > 0");
    }

    progress.onProgress(0.1);

    auto ds = gis::core::openRaster(input, true);
    auto* rasterBand = ds->GetRasterBand(band);
    if (!rasterBand) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    double minVal = 0, maxVal = 0;
    rasterBand->ComputeStatistics(false, &minVal, &maxVal, nullptr, nullptr, nullptr, nullptr);

    if (minVal >= maxVal) {
        return gis::framework::Result::fail("Band has no value range (min >= max)");
    }

    progress.onProgress(0.4);

    GUIntBig* histogram = new GUIntBig[bins]();
    CPLErr err = rasterBand->GetHistogram(
        minVal, maxVal, bins, histogram,
        TRUE, FALSE, nullptr, nullptr);

    if (err != CE_None) {
        delete[] histogram;
        return gis::framework::Result::fail("Failed to compute histogram");
    }

    progress.onProgress(0.8);

    double binWidth = (maxVal - minVal) / bins;

    std::ostringstream oss;
    oss << "Histogram for " << input << " (Band " << band << ")\n";
    oss << "Range: [" << minVal << ", " << maxVal << "]\n";
    oss << "Bins: " << bins << " (width=" << std::fixed << std::setprecision(6) << binWidth << ")\n\n";

    oss << "{\"histogram\":[\n";
    GUIntBig totalCount = 0;
    for (int i = 0; i < bins; ++i) {
        double binCenter = minVal + (i + 0.5) * binWidth;
        totalCount += histogram[i];
        oss << "  {\"bin\":" << i
            << ",\"center\":" << std::fixed << std::setprecision(4) << binCenter
            << ",\"count\":" << histogram[i] << "}";
        if (i + 1 < bins) oss << ",";
        oss << "\n";
    }
    oss << "],\"total\":" << totalCount << "}\n";

    delete[] histogram;

    if (!output.empty()) {
        ensureParentDirectoryForFile(output);
        std::ofstream ofs(output);
        if (ofs.is_open()) {
            ofs << oss.str();
        }
    }

    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(oss.str(), output);
    result.metadata["bins"] = std::to_string(bins);
    result.metadata["min"] = std::to_string(minVal);
    result.metadata["max"] = std::to_string(maxVal);
    result.metadata["total_pixels"] = std::to_string(totalCount);
    return result;
}

gis::framework::Result UtilityPlugin::doRasterInfo(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    if (input.empty()) return gis::framework::Result::fail("input is required");

    progress.onProgress(0.1);

    auto ds = gis::core::openRaster(input, true);
    int width  = ds->GetRasterXSize();
    int height = ds->GetRasterYSize();
    int bands  = ds->GetRasterCount();

    std::ostringstream oss;
    oss << "=== Raster Info: " << input << " ===\n\n";
    oss << "Driver: " << ds->GetDriver()->GetDescription() << "\n";
    oss << "Size:   " << width << " x " << height << " x " << bands << " bands\n";

    double adfGT[6];
    if (ds->GetGeoTransform(adfGT) == CE_None) {
        oss << "\nGeoTransform:\n";
        oss << "  Origin:      (" << adfGT[0] << ", " << adfGT[3] << ")\n";
        oss << "  Pixel Size:  (" << adfGT[1] << ", " << adfGT[5] << ")\n";
        oss << "  Rotation:    (" << adfGT[2] << ", " << adfGT[4] << ")\n";

        double minX = adfGT[0];
        double maxX = adfGT[0] + width * adfGT[1] + height * adfGT[2];
        double minY = adfGT[3] + width * adfGT[4] + height * adfGT[5];
        double maxY = adfGT[3];
        if (minX > maxX) std::swap(minX, maxX);
        if (minY > maxY) std::swap(minY, maxY);
        oss << "  Extent:      (" << minX << ", " << minY << ") - ("
            << maxX << ", " << maxY << ")\n";
    } else {
        oss << "\nGeoTransform: (none)\n";
    }

    std::string wkt = gis::core::getSRSWKT(ds.get());
    if (!wkt.empty()) {
        OGRSpatialReference srs;
        const char* wktPtr = wkt.c_str();
        if (srs.importFromWkt(&wktPtr) == OGRERR_NONE) {
            const char* authName = srs.GetAuthorityName(nullptr);
            const char* authCode = srs.GetAuthorityCode(nullptr);
            if (authName && authCode) {
                oss << "CRS:    " << authName << ":" << authCode << "\n";
            }
            char* prettyWkt = nullptr;
            srs.exportToPrettyWkt(&prettyWkt, false);
            if (prettyWkt) {
                oss << "CRS WKT:\n" << prettyWkt << "\n";
                CPLFree(prettyWkt);
            }
        }
    } else {
        oss << "CRS:    (none)\n";
    }

    int ovrCount = ds->GetRasterBand(1)->GetOverviewCount();
    oss << "Overviews: " << ovrCount << "\n";

    oss << "\n--- Band Details ---\n";
    for (int b = 1; b <= bands; ++b) {
        auto* rasterBand = ds->GetRasterBand(b);
        GDALDataType dataType = rasterBand->GetRasterDataType();

        std::string typeName;
        switch (dataType) {
            case GDT_Byte:    typeName = "Byte"; break;
            case GDT_UInt16:  typeName = "UInt16"; break;
            case GDT_Int16:   typeName = "Int16"; break;
            case GDT_UInt32:  typeName = "UInt32"; break;
            case GDT_Int32:   typeName = "Int32"; break;
            case GDT_Float32: typeName = "Float32"; break;
            case GDT_Float64: typeName = "Float64"; break;
            default:          typeName = "Unknown"; break;
        }

        int noDataOk = 0;
        double noDataVal = rasterBand->GetNoDataValue(&noDataOk);
        double scale = rasterBand->GetScale();
        double offset = rasterBand->GetOffset();

        oss << "\nBand " << b << ":\n";
        oss << "  Type:   " << typeName << "\n";
        if (noDataOk) oss << "  NoData: " << noDataVal << "\n";
        if (scale != 1.0 || offset != 0.0) {
            oss << "  Scale:  " << scale << "\n";
            oss << "  Offset: " << offset << "\n";
        }

        double bMin, bMax, bMean, bStd;
        if (rasterBand->ComputeStatistics(false, &bMin, &bMax, &bMean, &bStd, nullptr, nullptr) == CE_None) {
            oss << "  Min:    " << bMin << "\n";
            oss << "  Max:    " << bMax << "\n";
            oss << "  Mean:   " << bMean << "\n";
            oss << "  StdDev: " << bStd << "\n";
        }

        const char* colorInterp = GDALGetColorInterpretationName(rasterBand->GetColorInterpretation());
        oss << "  Color:  " << colorInterp << "\n";
    }

    progress.onProgress(1.0);
    return gis::framework::Result::ok(oss.str());
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
