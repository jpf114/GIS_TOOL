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
            gis::framework::ParamType::Int, false, int{1},
            int{1}, int{999}
        },
        gis::framework::ParamSpec{
            "bins", "分箱数", "直方图分箱数量",
            gis::framework::ParamType::Int, false, int{256},
            int{1}, int{65536}
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

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto ds = gis::core::openRaster(input, true);
    auto* rasterBand = ds->GetRasterBand(band);
    if (!rasterBand) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    double minVal = 0;
    double maxVal = 0;
    const CPLErr statsErr = rasterBand->ComputeStatistics(
        false, &minVal, &maxVal, nullptr, nullptr, nullptr, nullptr);

    // N133：判据从"min >= max"换成"统计到底算不算得出来"。
    // min == max 是完全合法的栅格 —— 常数栅格（整幅同一个值）、单像元栅格、以及 rasterize
    // 刚烧出来的产物都会这样；旧实现把它和"全 NoData / 空波段 ⇒ 统计算不出来"合并成同一句
    // "Band has no value range (min >= max)"，于是两件事同时坏：合法产物被判成坏数据，
    // 而真该报错的那张图给出的诊断还是假的（改前实测：16 个 1.0 的 tif 与全 0=NoData 的 tif
    // 返回的是逐字相同的一句）。
    // 现在：只有真的算不出统计才失败，且案文点明"没有有效像元"；min == max 正常出值（见下）。
    if (statsErr != CE_None) {
        return gis::framework::Result::fail(
            "Band has no valid pixels to sample (every cell is NoData, or the band is empty)");
    }

    progress.throwIfCancelled();

    progress.onProgress(0.4);

    // 值域宽度为 0 时没有"可分的区间"，用户要多少桶都只能给 1 个。
    // 计数仍然交给 GDAL：它按掩膜跳过 NoData，所以桶里是**有效像元数**而不是宽×高
    //（12 个 1.0 + 4 个 NoData 的夹具必须给 12，这是本分支唯一能被证伪的形态）。
    // 为了让 GDAL 走普通的"落在区间内"分支，把唯一的桶对称地张成
    // [min-0.5, min+0.5] ⇒ 桶心 (min-0.5 + 0.5) 恰好等于那个常数，不引入第二个值。
    const bool constantRange = (minVal == maxVal);
    const int effectiveBins = constantRange ? 1 : bins;
    std::vector<GUIntBig> bucketCounts(static_cast<size_t>(effectiveBins), 0);
    if (rasterBand->GetHistogram(constantRange ? minVal - 0.5 : minVal,
                                 constantRange ? maxVal + 0.5 : maxVal,
                                 effectiveBins, bucketCounts.data(),
                                 TRUE, FALSE, nullptr, nullptr) != CE_None) {
        return gis::framework::Result::fail("Failed to compute histogram");
    }

    progress.throwIfCancelled();

    progress.onProgress(0.8);

    const double binWidth = constantRange ? 0.0 : (maxVal - minVal) / bins;

    std::ostringstream oss;
    oss << "Histogram for " << input << " (Band " << band << ")\n";
    oss << "Range: [" << minVal << ", " << maxVal << "]\n";
    oss << "Bins: " << effectiveBins << " (width=" << std::fixed << std::setprecision(6) << binWidth
        << ")\n";
    if (constantRange) {
        oss << "Note: min == max, the band is constant => the single bin holds every valid pixel\n";
    }
    oss << "\n";

    oss << "{\"histogram\":[\n";
    GUIntBig totalCount = 0;
    for (int i = 0; i < effectiveBins; ++i) {
        const double binCenter = constantRange ? minVal : minVal + (i + 0.5) * binWidth;
        totalCount += bucketCounts[static_cast<size_t>(i)];
        oss << "  {\"bin\":" << i
            << ",\"center\":" << std::fixed << std::setprecision(4) << binCenter
            << ",\"count\":" << bucketCounts[static_cast<size_t>(i)] << "}";
        if (i + 1 < effectiveBins) {
            oss << ",";
        }
        oss << "\n";
    }
    oss << "],\"total\":" << totalCount << "}\n";

    if (!output.empty()) {
        ensureParentDirectoryForFile(output);
        std::ofstream ofs(output);
        if (ofs.is_open()) {
            ofs << oss.str();
        }
    }

    progress.throwIfCancelled();

    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(oss.str(), output);
    result.metadata["bins"] = std::to_string(effectiveBins);
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

    progress.throwIfCancelled();

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
        } else {
            // N133 的另一半：算不出统计（整波段都是 NoData / 空波段）时旧实现什么都不印，
            // 用户只看到"这个波段没有统计块"，与"常数波段 min == max 照样算得出"两种情况
            // 在屏幕上分不开 —— 直方图那条路径改前还把两者写成同一句案文。
            // 这里把原因写成一行，口径与 histogram 的失败案文一致；
            // 但**不**让整个 info 失败：info 是只读的结构报告，一个波段死掉不该藏起
            // 驱动、尺寸、CRS、其余波段的统计（真实腿里有 33 条判据在读它的文本）。
            oss << "  Statistics: (no valid pixels: band is entirely NoData or empty)\n";
        }

        const char* colorInterp = GDALGetColorInterpretationName(rasterBand->GetColorInterpretation());
        oss << "  Color:  " << colorInterp << "\n";
    }

    progress.throwIfCancelled();

    progress.onProgress(1.0);
    return gis::framework::Result::ok(oss.str());
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::RasterInspectPlugin)
