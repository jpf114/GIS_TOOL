#include "raster_manage_plugin.h"

#include <gis/core/error.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>

#include <algorithm>
#include <cmath>
#include <cpl_conv.h>
#include <filesystem>
#include <gdal_alg.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <ogr_api.h>
#include <ogr_geometry.h>
#include <ogrsf_frmts.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace gis::plugins {

namespace {

gis::core::ProcessingMetadata buildMetadata(const std::string& input, GDALDataset* srcDs, const std::string& algorithm) {
    gis::core::ProcessingMetadata meta;
    meta.sourceFile = input;
    meta.sourceCrs = gis::core::getSRSWKT(srcDs);
    meta.processingAlgorithm = algorithm;
    return meta;
}

} // namespace

std::vector<gis::framework::ParamSpec> RasterManagePlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "动作", "Raster management action",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"overviews", "nodata", "cog", "zonal_stats", "proximity"}
        },
        gis::framework::ParamSpec{"input", "输入栅格", "Input raster path", gis::framework::ParamType::FilePath, true, std::string{}},
        gis::framework::ParamSpec{"output", "输出结果", "Output path used by cog or derived products", gis::framework::ParamType::FilePath, false, std::string{}},
        // band 的默认值必须与下面的描述、与 doSetNoData 的兜底（getParam(..., "band", 0)）一致：
        // 0 = 全波段。展示层以前把它覆写成 0 而这里写 1，GUI 与 CLI 的"默认"因此不是同一个值。
        gis::framework::ParamSpec{"band", "波段", "Band index, 0 means all bands when supported", gis::framework::ParamType::Int, false, int{0}, int{0}, int{999}},
        gis::framework::ParamSpec{"levels", "金字塔层级", "Overview levels separated by spaces, for example 2 4 8 16", gis::framework::ParamType::String, false, std::string{"2 4 8 16"}},
        gis::framework::ParamSpec{"resample", "重采样", "Resampling method for overview generation", gis::framework::ParamType::Enum, false, std::string{"nearest"}, int{0}, int{0}, {"nearest", "gaussian", "cubic", "average", "mode"}},
        gis::framework::ParamSpec{"nodata_value", "NoData值", "NoData value to assign", gis::framework::ParamType::Double, false, double{0.0}, double{-1e15}, double{1e15}},
        gis::framework::ParamSpec{"vector", "分区矢量", "Vector path used by zonal statistics", gis::framework::ParamType::FilePath, false, std::string{}},
        gis::framework::ParamSpec{"feature_id_field", "要素ID字段", "Feature identifier field for zonal statistics output", gis::framework::ParamType::String, false, std::string{}},
        gis::framework::ParamSpec{"max_distance", "最大距离", "Maximum search distance, 0 means unlimited", gis::framework::ParamType::Double, false, double{0.0}, double{0.0}, double{1e15}},
    };
}

gis::framework::Result RasterManagePlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string action = gis::framework::getParam<std::string>(params, "action", "");

    if (action == "overviews")   return doBuildOverviews(params, progress);
    if (action == "cog")         return doBuildCog(params, progress);
    if (action == "nodata")      return doSetNoData(params, progress);
    if (action == "zonal_stats") return doZonalStats(params, progress);
    if (action == "proximity")   return doProximity(params, progress);

    return gis::framework::Result::fail("Unknown action: " + action);
}

gis::framework::Result RasterManagePlugin::doBuildOverviews(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string levelsStr = gis::framework::getParam<std::string>(params, "levels", "2 4 8 16");
    const std::string resample = gis::framework::getParam<std::string>(params, "resample", "nearest");

    if (input.empty()) return gis::framework::Result::fail("input is required");

    progress.throwIfCancelled();

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

    progress.throwIfCancelled();

    {
        gis::core::ProcessingMetadata meta;
        meta.sourceFile = input;
        meta.processingAlgorithm = "raster_manage.overviews";
        ds.reset();
        gis::core::writeProcessingMetadata(input, meta);
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

    progress.throwIfCancelled();

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
    progress.throwIfCancelled();
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

    {
        auto srcDs = gis::core::openRaster(input, true);
        auto meta = buildMetadata(input, srcDs.get(), "raster_manage.cog");
        gis::core::writeProcessingMetadata(output, meta);
    }

    progress.throwIfCancelled();
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

    progress.throwIfCancelled();

    progress.onProgress(0.1);
    auto ds = gis::core::openRaster(input, false);

    if (band <= 0) {
        const int bandCount = ds->GetRasterCount();
        for (int b = 1; b <= bandCount; ++b) {
            ds->GetRasterBand(b)->SetNoDataValue(nodataVal);
        }
        progress.throwIfCancelled();

        {
            auto meta = buildMetadata(input, ds.get(), "raster_manage.nodata");
            gis::core::writeProcessingMetadata(input, meta);
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
    progress.throwIfCancelled();

    {
        auto meta = buildMetadata(input, ds.get(), "raster_manage.nodata");
        gis::core::writeProcessingMetadata(input, meta);
    }

    progress.onProgress(1.0);
    return gis::framework::Result::ok(
        "NoData set to " + std::to_string(nodataVal) + " for band " + std::to_string(band), input);
}

gis::framework::Result RasterManagePlugin::doZonalStats(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string vectorPath = gis::framework::getParam<std::string>(params, "vector", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string idField = gis::framework::getParam<std::string>(params, "feature_id_field", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (vectorPath.empty()) return gis::framework::Result::fail("vector is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.throwIfCancelled();
    progress.onProgress(0.05);
    progress.onMessage("Loading raster and vector...");

    auto rasterDS = gis::core::openRaster(input, true);
    const int width = rasterDS->GetRasterXSize();
    const int height = rasterDS->GetRasterYSize();
    const int bands = rasterDS->GetRasterCount();

    double geoTransform[6] = {};
    if (rasterDS->GetGeoTransform(geoTransform) != CE_None) {
        return gis::framework::Result::fail("Failed to get geotransform");
    }

    auto vectorDS = std::unique_ptr<GDALDataset, gis::core::GdalDatasetDeleter>(
        static_cast<GDALDataset*>(GDALOpenEx(vectorPath.c_str(),
            GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr)));
    if (!vectorDS) {
        return gis::framework::Result::fail("Failed to open vector: " + vectorPath);
    }

    auto* layer = vectorDS->GetLayer(0);
    if (!layer) {
        return gis::framework::Result::fail("No layer found in vector file");
    }

    const std::string outExt = std::filesystem::path(output).extension().string();
    const bool outputCsv = (outExt == ".csv");

    progress.onProgress(0.1);
    progress.onMessage("Computing zonal statistics...");

    struct ZoneAccum {
        double sum = 0.0;
        double sumSq = 0.0;
        double minVal = 1e30;
        double maxVal = -1e30;
        uint64_t count = 0;
    };
    std::unordered_map<std::string, std::vector<ZoneAccum>> zoneStats;

    progress.onMessage("Pre-reading raster bands into memory...");
    std::vector<cv::Mat> allBands;
    allBands.reserve(static_cast<size_t>(bands));
    for (int b = 1; b <= bands && !progress.isCancelled(); ++b) {
        allBands.push_back(gis::core::gdalBandToMat(rasterDS.get(), b));
        progress.onProgress(0.1 + 0.05 * static_cast<double>(b) / bands);
    }

    const double burnValue = 1.0;
    const int bandNum = 1;
    GDALDriver* memDriver = GetGDALDriverManager()->GetDriverByName("MEM");

    int featCount = 0;
    const GIntBig totalFeatures = std::max<GIntBig>(1, layer->GetFeatureCount());
    layer->ResetReading();
    OGRFeature* feat = nullptr;

    while ((feat = layer->GetNextFeature()) != nullptr) {
        std::unique_ptr<OGRFeature, void(*)(OGRFeature*)> featGuard(feat, OGRFeature::DestroyFeature);
        auto* geom = feat->GetGeometryRef();
        if (!geom) continue;

        std::string fid = idField.empty()
            ? std::to_string(static_cast<long long>(feat->GetFID()))
            : feat->GetFieldAsString(idField.c_str());

        OGREnvelope envelope;
        geom->getEnvelope(&envelope);

        const int colStart = std::max(0, static_cast<int>((envelope.MinX - geoTransform[0]) / geoTransform[1]));
        const int colEnd = std::min(width - 1, static_cast<int>((envelope.MaxX - geoTransform[0]) / geoTransform[1]));
        const int rowStart = std::max(0, static_cast<int>((envelope.MaxY - geoTransform[3]) / geoTransform[5]));
        const int rowEnd = std::min(height - 1, static_cast<int>((envelope.MinY - geoTransform[3]) / geoTransform[5]));

        if (colStart > colEnd || rowStart > rowEnd) continue;

        const int subW = colEnd - colStart + 1;
        const int subH = rowEnd - rowStart + 1;

        double subGT[6] = {};
        std::copy(geoTransform, geoTransform + 6, subGT);
        subGT[0] = geoTransform[0] + colStart * geoTransform[1];
        subGT[3] = geoTransform[3] + rowStart * geoTransform[5];

        GDALDataset* maskDS = memDriver->Create("", subW, subH, 1, GDT_Byte, nullptr);
        if (maskDS) {
            maskDS->SetGeoTransform(subGT);
            maskDS->SetProjection(rasterDS->GetProjectionRef());

            OGRGeometryH hGeom = OGRGeometry::ToHandle(geom);
            GDALRasterizeGeometries(
                GDALDataset::ToHandle(maskDS), 1, &bandNum,
                1, &hGeom, nullptr, nullptr, &burnValue,
                nullptr, nullptr, nullptr);

            cv::Mat mask(subH, subW, CV_8UC1);
            CPLErr readErr = maskDS->GetRasterBand(1)->RasterIO(
                GF_Read, 0, 0, subW, subH,
                mask.data, subW, subH, GDT_Byte, 0, 0);
            GDALClose(maskDS);

            if (readErr == CE_None) {
                auto& bandAccums = zoneStats[fid];
                if (bandAccums.empty()) {
                    bandAccums.resize(static_cast<size_t>(bands));
                }

                for (int b = 0; b < bands; ++b) {
                    auto& acc = bandAccums[static_cast<size_t>(b)];
                    cv::Mat subBand = allBands[static_cast<size_t>(b)](
                        cv::Rect(colStart, rowStart, subW, subH));
                    int hasNoDataInt = 0;
                    const double nodata = rasterDS->GetRasterBand(b + 1)->GetNoDataValue(&hasNoDataInt);
                    const bool hasNd = (hasNoDataInt != 0);

                    for (int r = 0; r < subH; ++r) {
                        const uchar* maskRow = mask.ptr<uchar>(r);
                        const float* bandRow = subBand.ptr<float>(r);
                        for (int c = 0; c < subW; ++c) {
                            if (maskRow[c] == 0) continue;
                            const double val = static_cast<double>(bandRow[c]);
                            if (hasNd && std::abs(val - nodata) < 1e-9) continue;
                            acc.sum += val;
                            acc.sumSq += val * val;
                            if (val < acc.minVal) acc.minVal = val;
                            if (val > acc.maxVal) acc.maxVal = val;
                            ++acc.count;
                        }
                    }
                }
            }
        }

        ++featCount;
        if ((featCount % 50) == 0) {
            progress.throwIfCancelled();
            progress.onMessage("Processed " + std::to_string(featCount) + " features...");
            progress.onProgress(0.15 + 0.75 * static_cast<double>(featCount) / totalFeatures);
        }
    }

    progress.onProgress(0.92);
    progress.onMessage("Writing results to " + output + "...");

    if (outputCsv) {
        std::ofstream csv(output);
        csv << "feature_id,band";
        csv << ",min,max,mean,stddev,sum,count\n";
        for (const auto& [fid, accums] : zoneStats) {
            for (size_t bi = 0; bi < accums.size(); ++bi) {
                const auto& acc = accums[bi];
                csv << fid << "," << (bi + 1);
                if (acc.count > 0) {
                    const double mean = acc.sum / acc.count;
                    const double variance = (acc.sumSq / acc.count) - (mean * mean);
                    const double stddev = variance > 0 ? std::sqrt(variance) : 0.0;
                    csv << "," << acc.minVal << "," << acc.maxVal << "," << mean << "," << stddev;
                    csv << "," << acc.sum << "," << acc.count;
                } else {
                    csv << ",,,,,";
                }
                csv << "\n";
            }
        }
        csv.close();
    } else {
        std::ofstream json(output);
        json << "{\n";
        for (auto it = zoneStats.begin(); it != zoneStats.end(); ++it) {
            if (it != zoneStats.begin()) json << ",\n";
            json << "  \"" << it->first << "\": [";
            for (size_t bi = 0; bi < it->second.size(); ++bi) {
                if (bi > 0) json << ",";
                const auto& acc = it->second[bi];
                json << "\n    {";
                json << "\"band\":" << (bi + 1);
                if (acc.count > 0) {
                    const double mean = acc.sum / acc.count;
                    const double variance = (acc.sumSq / acc.count) - (mean * mean);
                    const double stddev = variance > 0 ? std::sqrt(variance) : 0.0;
                    json << ",\"min\":" << acc.minVal;
                    json << ",\"max\":" << acc.maxVal;
                    json << ",\"mean\":" << mean;
                    json << ",\"stddev\":" << stddev;
                    json << ",\"sum\":" << acc.sum;
                    json << ",\"count\":" << acc.count;
                }
                json << "}";
            }
            json << "\n  ]";
        }
        json << "\n}\n";
        json.close();
    }

    progress.onProgress(1.0);
    auto result = gis::framework::Result::ok(
        "Zonal statistics completed: " + std::to_string(zoneStats.size()) + " zones", output);
    result.metadata["zone_count"] = std::to_string(zoneStats.size());
    result.metadata["band_count"] = std::to_string(bands);
    return result;
}

gis::framework::Result RasterManagePlugin::doProximity(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const double maxDistance = gis::framework::getParam<double>(params, "max_distance", 0.0);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.throwIfCancelled();
    progress.onProgress(0.1);
    progress.onMessage("Computing Euclidean distance...");

    auto srcDS = gis::core::openRaster(input, true);
    const int bandCount = srcDS->GetRasterCount();

    std::vector<cv::Mat> outputs;
    outputs.reserve(static_cast<size_t>(bandCount));
    struct BandNoData {
        bool declared = false;
        double value = 0.0;
    };
    std::vector<BandNoData> declarations;
    declarations.reserve(static_cast<size_t>(bandCount));

    double gt[6] = {};
    srcDS->GetGeoTransform(gt);

    for (int b = 1; b <= bandCount; ++b) {
        progress.throwIfCancelled();
        progress.onMessage("Proximity band " + std::to_string(b) + "/" + std::to_string(bandCount));

        cv::Mat bandMat = gis::core::gdalBandToMat(srcDS.get(), b);

        int hasNoDataInt = 0;
        const double nodata = srcDS->GetRasterBand(b)->GetNoDataValue(&hasNoDataInt);
        declarations.push_back({hasNoDataInt != 0, nodata});

        cv::Mat validMask;   // 255 = 有效像元（不是 NoData）
        cv::Mat targetMask;  // 255 = 目标像元（值非 0 且非 NoData）
        cv::compare(bandMat, 0.0f, targetMask, cv::CMP_NE);
        if (hasNoDataInt) {
            cv::compare(bandMat, static_cast<float>(nodata), validMask, cv::CMP_NE);
            cv::bitwise_and(targetMask, validMask, targetMask);
        }

        // distanceTransform 度量的是"非零像元到最近零像元"的距离，所以必须喂取反后的掩膜，
        // 目标像元在其中为 0，输出的才是"到最近目标像元的距离"（目标自身为 0）。
        cv::Mat distanceInput;
        cv::bitwise_not(targetMask, distanceInput);
        cv::Mat dist;
        cv::distanceTransform(distanceInput, dist, cv::DIST_L2, cv::DIST_MASK_PRECISE);
        dist *= static_cast<float>(std::abs(gt[1]));

        if (maxDistance > 0.0) {
            cv::Mat clamped;
            cv::threshold(dist, clamped, static_cast<float>(maxDistance), 0, cv::THRESH_TRUNC);
            dist = clamped;
        }

        // NoData 赋值排在 THRESH_TRUNC **之后**：截断会把所有大于 maxDistance 的值改写成
        // maxDistance，先写 NoData 会让正的 NoData 值（9999、3.4e38 这类产品里常见）被悄悄毁掉。
        if (hasNoDataInt) {
            dist.setTo(static_cast<float>(nodata), validMask == 0);
        }

        outputs.push_back(dist);

        progress.onProgress(0.1 + 0.85 * static_cast<double>(b) / bandCount);
    }

    // 各波段的 NoData 声明只要不完全一致就必然有波段丢声明：Create(..., nullptr) 出的多波段 GeoTIFF
    // 是单 IFD，matsToGdalTiff 的逐波段 SetNoDataValue 覆写的是同一个 TIFFTAG_GDAL_NODATA。丢掉声明的
    // 波段里那些 NoData 像元的物理值仍是它自己原来的值，于是下游按文件声明把它当成合法距离读。
    // 只告警不失败：同传感器/多源产品常有波段间 NoData 差异（与 Task 13 的 R-T13-1 同型），
    // 硬失败会打断本来可用的正常用法。
    //
    // R2-F1：参照必须取"最后一个声明了 NoData 的波段"，不是第一个。存活者由我们自己的写出顺序决定：
    // opencv_wrapper.cpp:96-104 只对"源里声明了 NoData"的波段调 SetNoDataValue，单 IFD 只有一个共享
    // 标签且后写覆盖前写 ⇒ 最后一个有声明的波段活下来。以它为参照，计数才逐字等于"下游会被误导的
    // 波段数"；以第一个为参照时 (A,A,B) 少报一个、(A,B,B) 多报一个（双向实测见 task-7-report.md）。
    // 没有任何波段声明 ⇒ 没有存活者、也不存在丢声明，计数为 0 且不告警。
    //
    // R2-F5：声明值要按"同为 NaN 即一致"比较。NaN != NaN 恒真，直接用 != 会把各波段都声明 nan 的
    // 输入（外部产品里会出现）假报成分歧。
    int survivorIndex = -1;   // 输出里实际留下来的那个共享声明来自哪个波段
    for (size_t i = 0; i < declarations.size(); ++i) {
        if (declarations[i].declared) survivorIndex = static_cast<int>(i);
    }
    auto sameDeclaration = [](const BandNoData& a, const BandNoData& b) {
        if (a.declared != b.declared) return false;
        if (!a.declared) return true;                       // 都不声明：一致
        if (std::isnan(a.value) || std::isnan(b.value)) {
            return std::isnan(a.value) && std::isnan(b.value);
        }
        return a.value == b.value;
    };
    int divergentBands = 0;
    if (survivorIndex >= 0) {
        const auto& survivor = declarations[static_cast<size_t>(survivorIndex)];
        for (size_t i = 0; i < declarations.size(); ++i) {
            if (static_cast<int>(i) == survivorIndex) continue;
            if (!sameDeclaration(declarations[i], survivor)) ++divergentBands;
        }
    }
    if (divergentBands > 0) {
        progress.onMessage("警告: 输入各波段的 NoData 声明不一致 (nodata_divergent_band_count=" +
                           std::to_string(divergentBands) + "/" + std::to_string(bandCount) +
                           ")：输出栅格只剩一个共享 NoData 声明，其中 " + std::to_string(divergentBands) +
                           " 个波段的声明不会保留，那些波段的 NoData 像元会被下游当成合法距离");
    }

    // 循环外一次写出：若逐波段调用 matToGdalTiff(..., b)，它每次都重新创建整个文件，
    // 前面的波段会被覆盖掉，最终输出只剩 1 个波段。
    gis::core::matsToGdalTiff(outputs, srcDS.get(), output);

    {
        auto meta = buildMetadata(input, srcDS.get(), "raster_manage.euclidean_distance");
        gis::core::writeProcessingMetadata(output, meta);
    }

    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("Proximity map created", output);
    result.metadata["algorithm"] = "euclidean_distance_cv";
    // 键名与告警文本共用同一个键，CLI/GUI 的元数据摘要会把 key: value 原样显示给用户。
    // 这里的 divergent = "与输出里存活的那个共享声明不一致"，所以数值逐字就是"声明不会保留的波段数"。
    if (divergentBands > 0) {
        result.metadata["nodata_divergent_band_count"] = std::to_string(divergentBands);
    }
    if (maxDistance > 0.0) {
        result.metadata["max_distance"] = std::to_string(maxDistance);
    }
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::RasterManagePlugin)

