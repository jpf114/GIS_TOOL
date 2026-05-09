#include "raster_manage_plugin.h"

#include <gis/core/error.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>

#include <algorithm>
#include <cpl_conv.h>
#include <filesystem>
#include <gdal_alg.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <ogr_api.h>
#include <ogr_geometry.h>
#include <ogrsf_frmts.h>
#include <opencv2/opencv.hpp>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace gis::plugins {

std::vector<gis::framework::ParamSpec> RasterManagePlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"overviews", "nodata", "cog", "zonal_stats", "proximity"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入影像文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出文件", "COG 生成结果路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "band", "波段序号", "波段序号，填写 0 表示全部波段，从 1 开始表示单波段",
            gis::framework::ParamType::Int, false, int{1},
            int{0}, int{999}
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
            gis::framework::ParamType::Double, false, double{0.0},
            double{-1e15}, double{1e15}
        },
        gis::framework::ParamSpec{
            "vector", "矢量分区文件", "用于分区统计的面矢量文件",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "feature_id_field", "要素 ID 字段", "用于标识每个面要素的唯一字段名",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "max_distance", "最大距离", "欧氏距离计算的最大搜索距离（0=无限制）",
            gis::framework::ParamType::Double, false, double{0.0},
            double{0.0}, double{1e15}
        },
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

    for (int b = 1; b <= bandCount; ++b) {
        progress.throwIfCancelled();
        progress.onMessage("Proximity band " + std::to_string(b) + "/" + std::to_string(bandCount));

        cv::Mat bandMat = gis::core::gdalBandToMat(srcDS.get(), b);

        cv::Mat input8u;
        int hasNoDataInt = 0;
        double nodata = srcDS->GetRasterBand(b)->GetNoDataValue(&hasNoDataInt);

        cv::Mat mask = cv::Mat(bandMat.size(), CV_8UC1, cv::Scalar(255));
        if (hasNoDataInt) {
            for (int y = 0; y < bandMat.rows; ++y) {
                for (int x = 0; x < bandMat.cols; ++x) {
                    if (std::abs(bandMat.at<float>(y, x) - static_cast<float>(nodata)) < 1e-6f) {
                        mask.at<uchar>(y, x) = 0;
                    }
                }
            }
        }

        bandMat.convertTo(input8u, CV_8UC1, 255.0);
        cv::threshold(input8u, input8u, 0, 255, cv::THRESH_BINARY);

        cv::Mat dist;
        cv::distanceTransform(input8u, dist, cv::DIST_L2, cv::DIST_MASK_PRECISE);
        double gt[6] = {};
        srcDS->GetGeoTransform(gt);
        dist *= static_cast<float>(std::abs(gt[1]));

        if (maxDistance > 0.0) {
            cv::Mat clamped;
            cv::threshold(dist, clamped, static_cast<float>(maxDistance), 0, cv::THRESH_TRUNC);
            dist = clamped;
        }

        cv::Mat outputMat;
        dist.convertTo(outputMat, CV_32F);

        gis::core::matToGdalTiff(outputMat, srcDS.get(), output, b);

        progress.onProgress(0.1 + 0.85 * static_cast<double>(b) / bandCount);
    }

    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("Proximity map created", output);
    result.metadata["algorithm"] = "euclidean_distance_cv";
    if (maxDistance > 0.0) {
        result.metadata["max_distance"] = std::to_string(maxDistance);
    }
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::RasterManagePlugin)
