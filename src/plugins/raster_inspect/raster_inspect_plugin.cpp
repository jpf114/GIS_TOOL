#include "raster_inspect_plugin.h"

#include <gis/core/error.h>
#include <gis/core/gdal_wrapper.h>

#include <cpl_conv.h>
#include <gdal_priv.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

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

std::vector<gis::framework::ParamSpec> RasterInspectPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"histogram", "info"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入影像文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出文件", "输出结果路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "band", "波段序号", "要统计的波段序号，从 1 开始",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "bins", "分箱数", "直方图分箱数量",
            gis::framework::ParamType::Int, false, int{256}
        },
    };
}

gis::framework::Result RasterInspectPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string action = gis::framework::getParam<std::string>(params, "action", "");

    if (action == "histogram") return doHistogram(params, progress);
    if (action == "info")      return doRasterInfo(params, progress);

    return gis::framework::Result::fail("Unknown action: " + action);
}

gis::framework::Result RasterInspectPlugin::doHistogram(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const int band = gis::framework::getParam<int>(params, "band", 1);
    const int bins = gis::framework::getParam<int>(params, "bins", 256);

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

    double minVal = 0;
    double maxVal = 0;
    rasterBand->ComputeStatistics(false, &minVal, &maxVal, nullptr, nullptr, nullptr, nullptr);

    if (minVal >= maxVal) {
        return gis::framework::Result::fail("Band has no value range (min >= max)");
    }

    progress.onProgress(0.4);

    GUIntBig* histogram = new GUIntBig[bins]();
    const CPLErr err = rasterBand->GetHistogram(
        minVal, maxVal, bins, histogram,
        TRUE, FALSE, nullptr, nullptr);

    if (err != CE_None) {
        delete[] histogram;
        return gis::framework::Result::fail("Failed to compute histogram");
    }

    progress.onProgress(0.8);

    const double binWidth = (maxVal - minVal) / bins;

    std::ostringstream oss;
    oss << "Histogram for " << input << " (Band " << band << ")\n";
    oss << "Range: [" << minVal << ", " << maxVal << "]\n";
    oss << "Bins: " << bins << " (width=" << std::fixed << std::setprecision(6) << binWidth << ")\n\n";

    oss << "{\"histogram\":[\n";
    GUIntBig totalCount = 0;
    for (int i = 0; i < bins; ++i) {
        const double binCenter = minVal + (i + 0.5) * binWidth;
        totalCount += histogram[i];
        oss << "  {\"bin\":" << i
            << ",\"center\":" << std::fixed << std::setprecision(4) << binCenter
            << ",\"count\":" << histogram[i] << "}";
        if (i + 1 < bins) {
            oss << ",";
        }
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

gis::framework::Result RasterInspectPlugin::doRasterInfo(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    if (input.empty()) return gis::framework::Result::fail("input is required");

    progress.onProgress(0.1);

    auto ds = gis::core::openRaster(input, true);
    const int width = ds->GetRasterXSize();
    const int height = ds->GetRasterYSize();
    const int bands = ds->GetRasterCount();

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

    const std::string wkt = gis::core::getSRSWKT(ds.get());
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

    const int ovrCount = ds->GetRasterBand(1)->GetOverviewCount();
    oss << "Overviews: " << ovrCount << "\n";

    oss << "\n--- Band Details ---\n";
    for (int b = 1; b <= bands; ++b) {
        auto* rasterBand = ds->GetRasterBand(b);
        const GDALDataType dataType = rasterBand->GetRasterDataType();

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
        const double noDataVal = rasterBand->GetNoDataValue(&noDataOk);
        const double scale = rasterBand->GetScale();
        const double offset = rasterBand->GetOffset();

        oss << "\nBand " << b << ":\n";
        oss << "  Type:   " << typeName << "\n";
        if (noDataOk) oss << "  NoData: " << noDataVal << "\n";
        if (scale != 1.0 || offset != 0.0) {
            oss << "  Scale:  " << scale << "\n";
            oss << "  Offset: " << offset << "\n";
        }

        double bMin = 0;
        double bMax = 0;
        double bMean = 0;
        double bStd = 0;
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

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::RasterInspectPlugin)
