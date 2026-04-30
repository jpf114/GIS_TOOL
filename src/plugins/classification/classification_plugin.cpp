#include "classification_plugin.h"

#include <gis/core/gdal_wrapper.h>

#include <cpl_conv.h>
#include <gdal_alg.h>
#include <gdal_priv.h>
#include <gdalwarper.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace gis::plugins {

namespace {

using GdalDatasetPtr = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;

struct OgrGeometryDeleter {
    void operator()(OGRGeometry* geometry) const {
        if (geometry != nullptr) {
            OGRGeometryFactory::destroyGeometry(geometry);
        }
    }
};

using OgrGeometryPtr = std::unique_ptr<OGRGeometry, OgrGeometryDeleter>;

struct RasterTaskInput {
    std::string path;
    int band = 1;
    double nodata = 0.0;
};

struct FeatureStatsTask {
    std::string vectorPath;
    std::string featureIdField;
    std::string featureNameField;
    std::string classMapPath;
    std::vector<RasterTaskInput> rasters;
    int targetEpsg = 0;
    std::string outputPath;
    std::string outputFormat;
    std::string vectorOutputPath;
    std::string rasterOutputPath;
};

struct RasterSpatialRefInfo {
    bool isGeographic = false;
    bool isProjected = false;
    int epsg = 0;
    std::string srsText;
};

struct RasterGridInfo {
    double pixelWidth = 0.0;
    double pixelHeight = 0.0;
    std::array<double, 6> geotransform{};
};

struct RasterInspectInfo {
    RasterTaskInput input;
    RasterSpatialRefInfo srs;
    RasterGridInfo grid;
};

struct TargetSrsDecision {
    int epsg = 0;
    std::string epsgText;
};

struct FeatureWindow {
    int xoff = 0;
    int yoff = 0;
    int width = 0;
    int height = 0;
    std::array<double, 6> geotransform{};
};

struct FeatureStatsRecord {
    std::string featureId;
    std::string featureName;
    int classValue = 0;
    std::string className;
    long long pixelCount = 0;
    double area = 0.0;
    double ratio = 0.0;
    std::string actualSrs;
    std::string areaUnit = "m2";
};

struct FeatureStatsResultData {
    std::string actualSrs;
    std::string areaUnit = "m2";
    std::string statMode = "pixel_count";
    std::vector<FeatureStatsRecord> records;
};

struct VectorFeatureData {
    std::string featureId;
    std::string featureName;
    OgrGeometryPtr geometry;
};

struct VectorTaskData {
    int epsg = 0;
    std::vector<VectorFeatureData> features;
};

struct PolygonRecordInfo {
    std::string featureId;
    std::string featureName;
    std::string actualSrs;
    double pixelArea = 0.0;
    const std::map<int, std::string>* classMap = nullptr;
};

class FeatureVectorOutputWriter {
public:
    FeatureVectorOutputWriter(const std::string& path, int targetEpsg, const std::string& layerName);
    void writeResolvedRaster(
        const std::vector<int>& resolvedValues,
        int width,
        int height,
        const std::array<double, 6>& geotransform,
        const PolygonRecordInfo& info);

private:
    GdalDatasetPtr dataset_{nullptr, GDALClose};
    OGRLayer* layer_ = nullptr;
};

class FeatureRasterOutputWriter {
public:
    FeatureRasterOutputWriter(
        const std::string& path,
        int targetEpsg,
        int width,
        int height,
        const std::array<double, 6>& geotransform);

    void writeWindow(const std::vector<int>& values, int xoff, int yoff, int width, int height);

private:
    GdalDatasetPtr dataset_{nullptr, GDALClose};
};

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::vector<std::string> splitCsv(const std::string& value) {
    std::vector<std::string> items;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            items.push_back(item);
        }
    }
    return items;
}

bool parseIntList(const std::string& text, std::vector<int>& values, std::string& error) {
    values.clear();
    for (const auto& item : splitCsv(text)) {
        try {
            std::size_t index = 0;
            const int value = std::stoi(item, &index);
            if (index != item.size()) {
                error = "bands 中存在无效整数: " + item;
                return false;
            }
            values.push_back(value);
        } catch (...) {
            error = "bands 中存在无效整数: " + item;
            return false;
        }
    }
    return true;
}

bool parseDoubleList(const std::string& text, std::vector<double>& values, std::string& error) {
    values.clear();
    for (const auto& item : splitCsv(text)) {
        try {
            std::size_t index = 0;
            const double value = std::stod(item, &index);
            if (index != item.size()) {
                error = "nodatas 中存在无效数字: " + item;
                return false;
            }
            values.push_back(value);
        } catch (...) {
            error = "nodatas 中存在无效数字: " + item;
            return false;
        }
    }
    return true;
}

std::string toLower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string detectOutputFormat(const std::string& outputPath) {
    const std::string lower = toLower(outputPath);
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".json") {
        return "json";
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".csv") {
        return "csv";
    }
    return {};
}

int tryGetAuthorityEpsg(const OGRSpatialReference* srs) {
    if (srs == nullptr) {
        return 0;
    }

    const char* authorityName = srs->GetAuthorityName(nullptr);
    const char* authorityCode = srs->GetAuthorityCode(nullptr);
    if (authorityName == nullptr || authorityCode == nullptr) {
        return 0;
    }
    if (std::string(authorityName) != "EPSG") {
        return 0;
    }
    return std::atoi(authorityCode);
}

std::string buildTargetProjectionWkt(int epsg) {
    OGRSpatialReference srs;
    if (srs.importFromEPSG(epsg) != OGRERR_NONE) {
        throw std::runtime_error("无法构建目标 EPSG 坐标系");
    }

    char* wkt = nullptr;
    if (srs.exportToWkt(&wkt) != OGRERR_NONE || wkt == nullptr) {
        throw std::runtime_error("无法导出目标 EPSG WKT");
    }

    std::string result = wkt;
    CPLFree(wkt);
    return result;
}

void ensureParentDirectory(const std::string& path) {
    const std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path()) {
        std::filesystem::create_directories(fsPath.parent_path());
    }
}

GdalDatasetPtr makeMemDataset(
    GDALDriver* driver,
    int width,
    int height,
    GDALDataType dataType,
    const std::array<double, 6>& geotransform,
    const std::string& projectionWkt) {
    GDALDataset* dataset = driver->Create("", width, height, 1, dataType, nullptr);
    if (dataset == nullptr) {
        throw std::runtime_error("无法创建内存数据集");
    }

    dataset->SetGeoTransform(const_cast<double*>(geotransform.data()));
    dataset->SetProjection(projectionWkt.c_str());
    return GdalDatasetPtr(dataset, GDALClose);
}

RasterSpatialRefInfo inspectRasterSrs(GDALDataset* dataset) {
    RasterSpatialRefInfo info;

    const char* projection = dataset->GetProjectionRef();
    if (projection == nullptr || projection[0] == '\0') {
        return info;
    }

    OGRSpatialReference srs;
    if (srs.importFromWkt(projection) != OGRERR_NONE) {
        return info;
    }

    srs.AutoIdentifyEPSG();
    info.isGeographic = srs.IsGeographic();
    info.isProjected = srs.IsProjected();
    info.epsg = tryGetAuthorityEpsg(&srs);
    if (info.epsg > 0) {
        info.srsText = "EPSG:" + std::to_string(info.epsg);
    }
    return info;
}

RasterInspectInfo inspectRaster(const RasterTaskInput& input) {
    auto dataset = gis::core::openRaster(input.path, true);
    if (!dataset) {
        throw std::runtime_error("无法打开栅格: " + input.path);
    }

    GDALRasterBand* band = dataset->GetRasterBand(input.band);
    if (band == nullptr) {
        throw std::runtime_error("栅格波段不存在: " + input.path);
    }

    const GDALDataType dataType = band->GetRasterDataType();
    const bool supported =
        dataType == GDT_Byte ||
        dataType == GDT_Int16 ||
        dataType == GDT_UInt16 ||
        dataType == GDT_Int32 ||
        dataType == GDT_UInt32;
    if (!supported) {
        throw std::runtime_error("当前只支持整数型分类栅格: " + input.path);
    }

    double gt[6] = {};
    if (dataset->GetGeoTransform(gt) != CE_None) {
        throw std::runtime_error("栅格缺少有效地理变换: " + input.path);
    }

    RasterInspectInfo info;
    info.input = input;
    info.srs = inspectRasterSrs(dataset.get());
    info.grid.pixelWidth = gt[1];
    info.grid.pixelHeight = std::abs(gt[5]);
    for (int i = 0; i < 6; ++i) {
        info.grid.geotransform[i] = gt[i];
    }
    return info;
}

VectorTaskData openVectorData(
    const std::string& path,
    const std::string& idField,
    const std::string& nameField) {
    GDALDataset* rawDataset = static_cast<GDALDataset*>(
        GDALOpenEx(path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    if (rawDataset == nullptr) {
        throw std::runtime_error("无法打开矢量: " + path);
    }
    GdalDatasetPtr dataset(rawDataset, GDALClose);

    OGRLayer* layer = dataset->GetLayer(0);
    if (layer == nullptr) {
        throw std::runtime_error("矢量图层不存在: " + path);
    }

    VectorTaskData result;
    result.epsg = tryGetAuthorityEpsg(layer->GetSpatialRef());

    layer->ResetReading();
    OGRFeature* feature = nullptr;
    while ((feature = layer->GetNextFeature()) != nullptr) {
        VectorFeatureData item;
        item.featureId = idField.empty() ? std::to_string(feature->GetFID()) : feature->GetFieldAsString(idField.c_str());
        item.featureName = nameField.empty() ? "" : feature->GetFieldAsString(nameField.c_str());
        item.geometry.reset(feature->StealGeometry());
        OGRFeature::DestroyFeature(feature);

        if (!item.geometry) {
            throw std::runtime_error("矢量中存在无效几何");
        }

        result.features.push_back(std::move(item));
    }

    if (result.features.empty()) {
        throw std::runtime_error("矢量中没有可统计要素");
    }

    return result;
}

TargetSrsDecision resolveTargetSrs(int configTargetEpsg, const std::vector<RasterInspectInfo>& rasters) {
    bool hasProjected = false;
    bool hasGeographic = false;
    int projectedEpsg = 0;

    for (const auto& raster : rasters) {
        if (raster.srs.isProjected) {
            hasProjected = true;
            if (projectedEpsg == 0) {
                projectedEpsg = raster.srs.epsg;
            } else if (raster.srs.epsg != 0 && projectedEpsg != raster.srs.epsg) {
                throw std::runtime_error("输入栅格存在不一致的投影坐标系");
            }
        }
        if (raster.srs.isGeographic) {
            hasGeographic = true;
        }
    }

    if (configTargetEpsg > 0) {
        if (hasProjected && projectedEpsg != 0 && projectedEpsg != configTargetEpsg) {
            throw std::runtime_error("target_epsg 与输入投影坐标系不一致");
        }
        return {configTargetEpsg, "EPSG:" + std::to_string(configTargetEpsg)};
    }

    if (!hasProjected && hasGeographic) {
        throw std::runtime_error("当输入栅格全部为地理坐标系时，必须显式提供 target_epsg");
    }

    if (projectedEpsg <= 0) {
        throw std::runtime_error("无法自动决策目标坐标系");
    }

    return {projectedEpsg, "EPSG:" + std::to_string(projectedEpsg)};
}

RasterGridInfo suggestTargetGrid(const RasterInspectInfo& raster, int targetEpsg) {
    if (raster.srs.isProjected && raster.srs.epsg == targetEpsg) {
        return raster.grid;
    }

    GDALDataset* rawSource = static_cast<GDALDataset*>(GDALOpen(raster.input.path.c_str(), GA_ReadOnly));
    if (rawSource == nullptr) {
        throw std::runtime_error("无法重新打开栅格: " + raster.input.path);
    }
    GdalDatasetPtr source(rawSource, GDALClose);

    const std::string targetWkt = buildTargetProjectionWkt(targetEpsg);
    GDALDataset* rawWarped = static_cast<GDALDataset*>(GDALAutoCreateWarpedVRT(
        source.get(),
        source->GetProjectionRef(),
        targetWkt.c_str(),
        GRA_NearestNeighbour,
        0.0,
        nullptr));
    if (rawWarped == nullptr) {
        throw std::runtime_error("无法推导目标网格: " + raster.input.path);
    }
    GdalDatasetPtr warped(rawWarped, GDALClose);

    double gt[6] = {};
    if (warped->GetGeoTransform(gt) != CE_None) {
        throw std::runtime_error("重投影后的网格缺少地理变换");
    }

    RasterGridInfo grid;
    grid.pixelWidth = gt[1];
    grid.pixelHeight = std::abs(gt[5]);
    for (int i = 0; i < 6; ++i) {
        grid.geotransform[i] = gt[i];
    }
    return grid;
}

RasterGridInfo buildTargetGrid(const std::vector<RasterGridInfo>& grids) {
    if (grids.empty()) {
        throw std::runtime_error("缺少可用网格");
    }

    std::size_t bestIndex = 0;
    double bestArea = std::abs(grids[0].pixelWidth * grids[0].pixelHeight);
    for (std::size_t i = 1; i < grids.size(); ++i) {
        const double area = std::abs(grids[i].pixelWidth * grids[i].pixelHeight);
        if (area < bestArea) {
            bestArea = area;
            bestIndex = i;
        }
    }
    return grids[bestIndex];
}

OgrGeometryPtr cloneGeometryToTarget(const OGRGeometry* geometry, int sourceEpsg, int targetEpsg) {
    if (geometry == nullptr) {
        throw std::runtime_error("矢量几何为空");
    }

    OgrGeometryPtr cloned(geometry->clone());
    if (!cloned) {
        throw std::runtime_error("矢量几何复制失败");
    }

    if (sourceEpsg == 0 || sourceEpsg == targetEpsg) {
        return cloned;
    }

    OGRSpatialReference sourceSrs;
    OGRSpatialReference targetSrs;
    if (sourceSrs.importFromEPSG(sourceEpsg) != OGRERR_NONE || targetSrs.importFromEPSG(targetEpsg) != OGRERR_NONE) {
        throw std::runtime_error("坐标系构建失败");
    }

    OGRCoordinateTransformation* transform = OGRCreateCoordinateTransformation(&sourceSrs, &targetSrs);
    if (transform == nullptr) {
        throw std::runtime_error("坐标系转换器创建失败");
    }

    const OGRErr transformResult = cloned->transform(transform);
    OCTDestroyCoordinateTransformation(reinterpret_cast<OGRCoordinateTransformationH>(transform));
    if (transformResult != OGRERR_NONE) {
        throw std::runtime_error("要素重投影失败");
    }

    return cloned;
}

FeatureWindow buildFeatureWindow(const OGRGeometry& geometry, const RasterGridInfo& grid) {
    OGREnvelope envelope;
    geometry.getEnvelope(&envelope);

    const double originX = grid.geotransform[0];
    const double originY = grid.geotransform[3];
    const double pixelWidth = grid.pixelWidth;
    const double pixelHeight = grid.pixelHeight;

    const int xoff = static_cast<int>(std::floor((envelope.MinX - originX) / pixelWidth));
    const int xend = static_cast<int>(std::ceil((envelope.MaxX - originX) / pixelWidth));
    const int yoff = static_cast<int>(std::floor((originY - envelope.MaxY) / pixelHeight));
    const int yend = static_cast<int>(std::ceil((originY - envelope.MinY) / pixelHeight));

    FeatureWindow window;
    window.xoff = xoff;
    window.yoff = yoff;
    window.width = std::max(0, xend - xoff);
    window.height = std::max(0, yend - yoff);
    window.geotransform = grid.geotransform;
    window.geotransform[0] = originX + static_cast<double>(xoff) * pixelWidth;
    window.geotransform[3] = originY - static_cast<double>(yoff) * pixelHeight;
    return window;
}

FeatureWindow mergeWindows(const FeatureWindow& lhs, const FeatureWindow& rhs, const RasterGridInfo& grid) {
    FeatureWindow merged;
    merged.xoff = std::min(lhs.xoff, rhs.xoff);
    merged.yoff = std::min(lhs.yoff, rhs.yoff);
    const int xend = std::max(lhs.xoff + lhs.width, rhs.xoff + rhs.width);
    const int yend = std::max(lhs.yoff + lhs.height, rhs.yoff + rhs.height);
    merged.width = std::max(0, xend - merged.xoff);
    merged.height = std::max(0, yend - merged.yoff);
    merged.geotransform = grid.geotransform;
    merged.geotransform[0] = grid.geotransform[0] + static_cast<double>(merged.xoff) * grid.pixelWidth;
    merged.geotransform[3] = grid.geotransform[3] - static_cast<double>(merged.yoff) * grid.pixelHeight;
    return merged;
}

std::vector<unsigned char> buildFeatureMask(
    const OGRGeometry& geometry,
    const FeatureWindow& window,
    const std::string& targetProjectionWkt) {
    GDALDriver* memDriver = GetGDALDriverManager()->GetDriverByName("MEM");
    if (memDriver == nullptr) {
        throw std::runtime_error("无法获取内存驱动");
    }

    auto maskDataset = makeMemDataset(memDriver, window.width, window.height, GDT_Byte, window.geotransform, targetProjectionWkt);
    GDALRasterBand* band = maskDataset->GetRasterBand(1);
    band->Fill(0.0);

    OGRGeometry* geometryPtr = const_cast<OGRGeometry*>(&geometry);
    int bandIndex = 1;
    double burnValue = 1.0;
    if (GDALRasterizeGeometries(
            maskDataset.get(),
            1,
            &bandIndex,
            1,
            reinterpret_cast<OGRGeometryH*>(&geometryPtr),
            nullptr,
            nullptr,
            &burnValue,
            nullptr,
            nullptr,
            nullptr) != CE_None) {
        throw std::runtime_error("要素掩膜栅格化失败");
    }

    std::vector<unsigned char> mask(static_cast<std::size_t>(window.width) * static_cast<std::size_t>(window.height), 0);
    if (band->RasterIO(
            GF_Read,
            0,
            0,
            window.width,
            window.height,
            mask.data(),
            window.width,
            window.height,
            GDT_Byte,
            0,
            0,
            nullptr) != CE_None) {
        throw std::runtime_error("要素掩膜读取失败");
    }

    return mask;
}

std::vector<int> readRasterAsTargetWindow(
    const RasterTaskInput& rasterInput,
    const FeatureWindow& window,
    const std::string& targetProjectionWkt) {
    GDALDataset* rawSource = static_cast<GDALDataset*>(GDALOpen(rasterInput.path.c_str(), GA_ReadOnly));
    if (rawSource == nullptr) {
        throw std::runtime_error("无法打开栅格窗口源: " + rasterInput.path);
    }
    GdalDatasetPtr source(rawSource, GDALClose);

    GDALRasterBand* sourceBand = source->GetRasterBand(rasterInput.band);
    if (sourceBand == nullptr) {
        throw std::runtime_error("栅格波段不存在: " + rasterInput.path);
    }

    GDALDriver* memDriver = GetGDALDriverManager()->GetDriverByName("MEM");
    if (memDriver == nullptr) {
        throw std::runtime_error("无法获取内存驱动");
    }

    auto targetDataset = makeMemDataset(memDriver, window.width, window.height, GDT_Int32, window.geotransform, targetProjectionWkt);
    GDALRasterBand* targetBand = targetDataset->GetRasterBand(1);
    targetBand->SetNoDataValue(rasterInput.nodata);
    targetBand->Fill(rasterInput.nodata);

    if (GDALReprojectImage(
            source.get(),
            source->GetProjectionRef(),
            targetDataset.get(),
            targetProjectionWkt.c_str(),
            GRA_NearestNeighbour,
            0.0,
            0.0,
            nullptr,
            nullptr,
            nullptr) != CE_None) {
        throw std::runtime_error("栅格窗口重采样失败: " + rasterInput.path);
    }

    std::vector<int> data(static_cast<std::size_t>(window.width) * static_cast<std::size_t>(window.height), 0);
    if (targetBand->RasterIO(
            GF_Read,
            0,
            0,
            window.width,
            window.height,
            data.data(),
            window.width,
            window.height,
            GDT_Int32,
            0,
            0,
            nullptr) != CE_None) {
        throw std::runtime_error("栅格窗口读取失败: " + rasterInput.path);
    }

    return data;
}

std::vector<int> resolvePixelsByPriority(
    const std::vector<std::vector<int>>& rasters,
    const std::vector<int>& nodataValues,
    const std::vector<unsigned char>& featureMask,
    const std::map<int, std::string>& classMap) {
    std::vector<int> resolved(featureMask.size(), 0);
    std::vector<unsigned char> covered(featureMask.size(), 0);

    for (std::size_t rasterIndex = 0; rasterIndex < rasters.size(); ++rasterIndex) {
        const auto& raster = rasters[rasterIndex];
        const int nodata = nodataValues[rasterIndex];

        for (std::size_t i = 0; i < raster.size(); ++i) {
            if (!featureMask[i] || covered[i]) {
                continue;
            }

            const int value = raster[i];
            if (value == nodata) {
                continue;
            }

            if (classMap.find(value) == classMap.end()) {
                continue;
            }

            covered[i] = 1;
            resolved[i] = value;
        }
    }

    return resolved;
}

std::vector<FeatureStatsRecord> countPixelsByPriority(
    const std::vector<std::vector<int>>& rasters,
    const std::vector<int>& nodataValues,
    const std::vector<unsigned char>& featureMask,
    const std::map<int, std::string>& classMap,
    double pixelArea) {
    const auto resolved = resolvePixelsByPriority(rasters, nodataValues, featureMask, classMap);

    std::map<int, long long> counts;
    for (const int value : resolved) {
        if (value != 0) {
            counts[value] += 1;
        }
    }

    long long totalPixels = 0;
    for (const auto& item : counts) {
        totalPixels += item.second;
    }

    std::vector<FeatureStatsRecord> records;
    if (counts.empty()) {
        records.push_back({"", "", 0, "", 0, 0.0, 0.0, "", "m2"});
        return records;
    }

    for (const auto& item : counts) {
        FeatureStatsRecord record;
        record.classValue = item.first;
        record.className = classMap.at(item.first);
        record.pixelCount = item.second;
        record.area = static_cast<double>(item.second) * pixelArea;
        record.ratio = totalPixels == 0 ? 0.0 : static_cast<double>(item.second) / static_cast<double>(totalPixels);
        records.push_back(record);
    }

    return records;
}

FeatureStatsRecord buildEmptyRecord(const VectorFeatureData& feature, const std::string& actualSrs) {
    return {feature.featureId, feature.featureName, 0, "", 0, 0.0, 0.0, actualSrs, "m2"};
}

void appendSummaryRecords(FeatureStatsResultData& result) {
    std::map<int, FeatureStatsRecord> summaryByClass;
    double totalArea = 0.0;

    for (const auto& record : result.records) {
        if (record.featureId == "__summary__") {
            continue;
        }

        auto& summary = summaryByClass[record.classValue];
        summary.featureId = "__summary__";
        summary.featureName = "汇总";
        summary.classValue = record.classValue;
        summary.className = record.className;
        summary.pixelCount += record.pixelCount;
        summary.area += record.area;
        summary.actualSrs = record.actualSrs;
        summary.areaUnit = record.areaUnit;
        totalArea += record.area;
    }

    for (auto& [classValue, summary] : summaryByClass) {
        (void)classValue;
        summary.ratio = totalArea == 0.0 ? 0.0 : summary.area / totalArea;
        result.records.push_back(summary);
    }
}

std::string escapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

void writeCsvResult(const std::string& path, const FeatureStatsResultData& result) {
    ensureParentDirectory(path);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("无法写出 CSV 结果: " + path);
    }

    out << "\xEF\xBB\xBF";
    out << "feature_id,feature_name,class_value,class_name,pixel_count,area,ratio,actual_srs,area_unit\n";
    for (const auto& record : result.records) {
        out << record.featureId << ','
            << record.featureName << ','
            << record.classValue << ','
            << record.className << ','
            << record.pixelCount << ','
            << record.area << ','
            << record.ratio << ','
            << record.actualSrs << ','
            << record.areaUnit << '\n';
    }
}

void writeJsonResult(const std::string& path, const FeatureStatsResultData& result) {
    ensureParentDirectory(path);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("无法写出 JSON 结果: " + path);
    }

    out << "\xEF\xBB\xBF";
    out << "{\n";
    out << "  \"meta\": {\n";
    out << "    \"actual_srs\": \"" << escapeJson(result.actualSrs) << "\",\n";
    out << "    \"area_unit\": \"" << escapeJson(result.areaUnit) << "\",\n";
    out << "    \"stat_mode\": \"" << escapeJson(result.statMode) << "\"\n";
    out << "  },\n";
    out << "  \"records\": [\n";

    for (std::size_t i = 0; i < result.records.size(); ++i) {
        const auto& record = result.records[i];
        out << "    {\n";
        out << "      \"feature_id\": \"" << escapeJson(record.featureId) << "\",\n";
        out << "      \"feature_name\": \"" << escapeJson(record.featureName) << "\",\n";
        out << "      \"class_value\": " << record.classValue << ",\n";
        out << "      \"class_name\": \"" << escapeJson(record.className) << "\",\n";
        out << "      \"pixel_count\": " << record.pixelCount << ",\n";
        out << "      \"area\": " << record.area << ",\n";
        out << "      \"ratio\": " << record.ratio << ",\n";
        out << "      \"actual_srs\": \"" << escapeJson(record.actualSrs) << "\",\n";
        out << "      \"area_unit\": \"" << escapeJson(record.areaUnit) << "\"\n";
        out << "    }";
        if (i + 1 < result.records.size()) {
            out << ',';
        }
        out << '\n';
    }

    out << "  ]\n";
    out << "}\n";
}

void writeResult(const FeatureStatsTask& task, const FeatureStatsResultData& result) {
    if (task.outputFormat == "json") {
        writeJsonResult(task.outputPath, result);
        return;
    }
    if (task.outputFormat == "csv") {
        writeCsvResult(task.outputPath, result);
        return;
    }
    throw std::runtime_error("不支持的输出格式");
}

std::map<int, std::string> loadClassMap(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("无法打开分类映射文件: " + path);
    }

    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::regex itemPattern("\"([0-9]+)\"\\s*:\\s*\"([^\"]*)\"");

    std::map<int, std::string> classMap;
    auto begin = std::sregex_iterator(text.begin(), text.end(), itemPattern);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        classMap[std::stoi((*it)[1].str())] = (*it)[2].str();
    }

    if (classMap.empty()) {
        throw std::runtime_error("分类映射为空或格式不正确");
    }

    return classMap;
}

int getFieldIndexOrThrow(OGRLayer* layer, const char* fieldName) {
    const int index = layer->GetLayerDefn()->GetFieldIndex(fieldName);
    if (index < 0) {
        throw std::runtime_error("输出字段缺失");
    }
    return index;
}

FeatureVectorOutputWriter::FeatureVectorOutputWriter(
    const std::string& path,
    int targetEpsg,
    const std::string& layerName) {
    ensureParentDirectory(path);
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }

    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (driver == nullptr) {
        throw std::runtime_error("无法创建矢量分类输出驱动");
    }

    GDALDataset* rawDataset = driver->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (rawDataset == nullptr) {
        throw std::runtime_error("无法创建矢量分类输出");
    }
    dataset_.reset(rawDataset);

    OGRSpatialReference srs;
    if (srs.importFromEPSG(targetEpsg) != OGRERR_NONE) {
        throw std::runtime_error("无法创建矢量分类输出坐标系");
    }

    layer_ = dataset_->CreateLayer(layerName.c_str(), &srs, wkbPolygon, nullptr);
    if (layer_ == nullptr) {
        throw std::runtime_error("无法创建矢量分类输出图层");
    }

    OGRFieldDefn fieldFeatureId("feature_id", OFTString);
    fieldFeatureId.SetWidth(64);
    OGRFieldDefn fieldFeatureName("feature_name", OFTString);
    fieldFeatureName.SetWidth(128);
    OGRFieldDefn fieldClassValue("class_value", OFTInteger);
    OGRFieldDefn fieldClassName("class_name", OFTString);
    fieldClassName.SetWidth(128);
    OGRFieldDefn fieldPixelCount("pixel_count", OFTInteger64);
    OGRFieldDefn fieldArea("area", OFTReal);
    fieldArea.SetPrecision(3);
    OGRFieldDefn fieldActualSrs("actual_srs", OFTString);
    fieldActualSrs.SetWidth(32);

    if (layer_->CreateField(&fieldFeatureId) != OGRERR_NONE ||
        layer_->CreateField(&fieldFeatureName) != OGRERR_NONE ||
        layer_->CreateField(&fieldClassValue) != OGRERR_NONE ||
        layer_->CreateField(&fieldClassName) != OGRERR_NONE ||
        layer_->CreateField(&fieldPixelCount) != OGRERR_NONE ||
        layer_->CreateField(&fieldArea) != OGRERR_NONE ||
        layer_->CreateField(&fieldActualSrs) != OGRERR_NONE) {
        throw std::runtime_error("无法创建矢量分类输出字段");
    }
}

void FeatureVectorOutputWriter::writeResolvedRaster(
    const std::vector<int>& resolvedValues,
    int width,
    int height,
    const std::array<double, 6>& geotransform,
    const PolygonRecordInfo& info) {
    if (layer_ == nullptr || info.classMap == nullptr) {
        throw std::runtime_error("矢量分类输出未初始化");
    }

    GDALDriver* memDriver = GetGDALDriverManager()->GetDriverByName("MEM");
    if (memDriver == nullptr) {
        throw std::runtime_error("无法获取内存驱动");
    }

    const std::string projectionWkt = buildTargetProjectionWkt(std::stoi(info.actualSrs.substr(5)));
    auto memDataset = makeMemDataset(memDriver, width, height, GDT_Int32, geotransform, projectionWkt);
    GDALRasterBand* band = memDataset->GetRasterBand(1);
    band->SetNoDataValue(0);
    if (band->RasterIO(
            GF_Write,
            0,
            0,
            width,
            height,
            const_cast<int*>(resolvedValues.data()),
            width,
            height,
            GDT_Int32,
            0,
            0,
            nullptr) != CE_None) {
        throw std::runtime_error("矢量分类输出写入内存栅格失败");
    }

    const int classField = getFieldIndexOrThrow(layer_, "class_value");
    GIntBig maxExistingFid = -1;
    layer_->ResetReading();
    OGRFeature* existing = nullptr;
    while ((existing = layer_->GetNextFeature()) != nullptr) {
        maxExistingFid = std::max(maxExistingFid, existing->GetFID());
        OGRFeature::DestroyFeature(existing);
    }

    if (GDALPolygonize(
            GDALRasterBand::ToHandle(band),
            nullptr,
            OGRLayer::ToHandle(layer_),
            classField,
            nullptr,
            nullptr,
            nullptr) != CE_None) {
        throw std::runtime_error("矢量分类输出面矢量化失败");
    }

    const int featureIdIndex = getFieldIndexOrThrow(layer_, "feature_id");
    const int featureNameIndex = getFieldIndexOrThrow(layer_, "feature_name");
    const int classNameIndex = getFieldIndexOrThrow(layer_, "class_name");
    const int pixelCountIndex = getFieldIndexOrThrow(layer_, "pixel_count");
    const int areaIndex = getFieldIndexOrThrow(layer_, "area");
    const int actualSrsIndex = getFieldIndexOrThrow(layer_, "actual_srs");

    layer_->ResetReading();
    OGRFeature* feature = nullptr;
    while ((feature = layer_->GetNextFeature()) != nullptr) {
        if (feature->GetFID() <= maxExistingFid) {
            OGRFeature::DestroyFeature(feature);
            continue;
        }

        const int classValue = feature->GetFieldAsInteger(classField);
        if (classValue == 0) {
            const GIntBig fid = feature->GetFID();
            OGRFeature::DestroyFeature(feature);
            layer_->DeleteFeature(fid);
            continue;
        }

        const auto it = info.classMap->find(classValue);
        if (it == info.classMap->end()) {
            const GIntBig fid = feature->GetFID();
            OGRFeature::DestroyFeature(feature);
            layer_->DeleteFeature(fid);
            continue;
        }

        OGRGeometry* geometry = feature->GetGeometryRef();
        const double area = geometry ? OGR_G_Area(OGRGeometry::ToHandle(geometry)) : 0.0;
        const long long pixelCount = info.pixelArea <= 0.0 ? 0LL : static_cast<long long>(std::llround(area / info.pixelArea));

        feature->SetField(featureIdIndex, info.featureId.c_str());
        feature->SetField(featureNameIndex, info.featureName.c_str());
        feature->SetField(classNameIndex, it->second.c_str());
        feature->SetField(pixelCountIndex, pixelCount);
        feature->SetField(areaIndex, area);
        feature->SetField(actualSrsIndex, info.actualSrs.c_str());

        if (layer_->SetFeature(feature) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(feature);
            throw std::runtime_error("更新矢量分类输出要素失败");
        }

        OGRFeature::DestroyFeature(feature);
    }
}

FeatureRasterOutputWriter::FeatureRasterOutputWriter(
    const std::string& path,
    int targetEpsg,
    int width,
    int height,
    const std::array<double, 6>& geotransform) {
    ensureParentDirectory(path);

    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (driver == nullptr) {
        throw std::runtime_error("无法创建栅格分类输出驱动");
    }

    char const* options[] = {"COMPRESS=LZW", nullptr};
    GDALDataset* rawDataset = driver->Create(path.c_str(), width, height, 1, GDT_Int32, const_cast<char**>(options));
    if (rawDataset == nullptr) {
        throw std::runtime_error("无法创建栅格分类输出");
    }
    dataset_.reset(rawDataset);

    dataset_->SetGeoTransform(const_cast<double*>(geotransform.data()));
    const std::string projectionWkt = buildTargetProjectionWkt(targetEpsg);
    dataset_->SetProjection(projectionWkt.c_str());

    GDALRasterBand* band = dataset_->GetRasterBand(1);
    band->SetNoDataValue(0);
    band->Fill(0.0);
}

void FeatureRasterOutputWriter::writeWindow(const std::vector<int>& values, int xoff, int yoff, int width, int height) {
    if (!dataset_) {
        throw std::runtime_error("栅格分类输出未初始化");
    }

    GDALRasterBand* band = dataset_->GetRasterBand(1);
    std::vector<int> current(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    if (band->RasterIO(GF_Read, xoff, yoff, width, height, current.data(), width, height, GDT_Int32, 0, 0, nullptr) != CE_None) {
        throw std::runtime_error("读取栅格分类输出窗口失败");
    }

    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i] != 0) {
            current[i] = values[i];
        }
    }

    if (band->RasterIO(GF_Write, xoff, yoff, width, height, current.data(), width, height, GDT_Int32, 0, 0, nullptr) != CE_None) {
        throw std::runtime_error("写入栅格分类输出窗口失败");
    }
}

gis::framework::Result buildTaskFromParams(
    const std::map<std::string, gis::framework::ParamValue>& params,
    FeatureStatsTask& task) {
    task.vectorPath = gis::framework::getParam<std::string>(params, "vector", "");
    task.featureIdField = gis::framework::getParam<std::string>(params, "feature_id_field", "");
    task.featureNameField = gis::framework::getParam<std::string>(params, "feature_name_field", "");
    task.classMapPath = gis::framework::getParam<std::string>(params, "class_map", "");
    task.outputPath = gis::framework::getParam<std::string>(params, "output", "");
    task.targetEpsg = gis::framework::getParam<int>(params, "target_epsg", 0);
    task.outputFormat = detectOutputFormat(task.outputPath);
    task.vectorOutputPath = gis::framework::getParam<std::string>(params, "vector_output", "");
    task.rasterOutputPath = gis::framework::getParam<std::string>(params, "raster_output", "");

    const std::string rastersText = gis::framework::getParam<std::string>(params, "rasters", "");
    const std::string bandsText = gis::framework::getParam<std::string>(params, "bands", "");
    const std::string nodatasText = gis::framework::getParam<std::string>(params, "nodatas", "");

    const auto rasterPaths = splitCsv(rastersText);
    if (rasterPaths.empty()) {
        return gis::framework::Result::fail("rasters 不能为空");
    }

    std::vector<int> bands;
    if (!bandsText.empty()) {
        std::string error;
        if (!parseIntList(bandsText, bands, error)) {
            return gis::framework::Result::fail(error);
        }
    }

    std::vector<double> nodatas;
    if (!nodatasText.empty()) {
        std::string error;
        if (!parseDoubleList(nodatasText, nodatas, error)) {
            return gis::framework::Result::fail(error);
        }
    }

    if (!bands.empty() && bands.size() != rasterPaths.size()) {
        return gis::framework::Result::fail("bands 数量必须与 rasters 一致");
    }
    if (!nodatas.empty() && nodatas.size() != rasterPaths.size()) {
        return gis::framework::Result::fail("nodatas 数量必须与 rasters 一致");
    }
    if (task.outputFormat.empty()) {
        return gis::framework::Result::fail("output 目前只支持 .json 或 .csv");
    }

    task.rasters.clear();
    task.rasters.reserve(rasterPaths.size());
    for (std::size_t i = 0; i < rasterPaths.size(); ++i) {
        RasterTaskInput input;
        input.path = rasterPaths[i];
        input.band = bands.empty() ? 1 : bands[i];
        input.nodata = nodatas.empty() ? 0.0 : nodatas[i];
        task.rasters.push_back(input);
    }

    return gis::framework::Result::ok();
}

} // namespace

std::vector<gis::framework::ParamSpec> ClassificationPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "当前固定为 feature_stats",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0}, {"feature_stats"}
        },
        gis::framework::ParamSpec{
            "vector", "输入面矢量", "参与统计的面矢量文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "feature_id_field", "要素 ID 字段", "可选，要素唯一标识字段名",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "feature_name_field", "要素名称字段", "可选，要素名称字段名",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "class_map", "分类映射", "分类值到分类名称的 JSON 映射文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "rasters", "分类栅格列表", "多个分类栅格路径，使用逗号分隔，顺序即优先级",
            gis::framework::ParamType::String, true, std::string{}
        },
        gis::framework::ParamSpec{
            "bands", "波段列表", "与 rasters 对应的波段列表，逗号分隔，默认全部为 1",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "nodatas", "NoData 列表", "与 rasters 对应的 NoData 列表，逗号分隔，默认全部为 0",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "target_epsg", "目标 EPSG", "可选，显式指定目标投影坐标系",
            gis::framework::ParamType::Int, false, int{0}
        },
        gis::framework::ParamSpec{
            "output", "统计输出", "输出统计结果路径，当前只支持 .json 或 .csv",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "vector_output", "分类面输出", "可选，输出分类面结果，当前建议使用 .gpkg",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "raster_output", "分类栅格输出", "可选，输出分类栅格结果，当前建议使用 .tif",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
    };
}

gis::framework::Result ClassificationPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    try {
        const std::string action = gis::framework::getParam<std::string>(params, "action", "");
        if (action == "feature_stats") {
            return doFeatureStats(params, progress);
        }
        return gis::framework::Result::fail("Unknown action: " + action);
    } catch (const std::exception& ex) {
        return gis::framework::Result::fail(ex.what());
    }
}

gis::framework::Result ClassificationPlugin::doFeatureStats(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    progress.onMessage("正在准备地物分类统计任务...");
    progress.onProgress(0.05);

    FeatureStatsTask task;
    auto parseResult = buildTaskFromParams(params, task);
    if (!parseResult.success) {
        return parseResult;
    }

    progress.onMessage("正在加载分类映射与矢量要素...");
    const auto classMap = loadClassMap(task.classMapPath);
    const auto vectorData = openVectorData(task.vectorPath, task.featureIdField, task.featureNameField);
    if (vectorData.epsg == 0) {
        throw std::runtime_error("矢量缺少有效 EPSG，当前版本暂不支持");
    }
    progress.onProgress(0.2);

    progress.onMessage("正在检查栅格输入...");
    std::vector<RasterInspectInfo> rasterInfos;
    rasterInfos.reserve(task.rasters.size());
    for (const auto& raster : task.rasters) {
        rasterInfos.push_back(inspectRaster(raster));
    }
    progress.onProgress(0.35);

    progress.onMessage("正在决策目标坐标系与目标网格...");
    const auto targetSrs = resolveTargetSrs(task.targetEpsg, rasterInfos);
    std::vector<RasterGridInfo> grids;
    grids.reserve(rasterInfos.size());
    for (const auto& rasterInfo : rasterInfos) {
        grids.push_back(suggestTargetGrid(rasterInfo, targetSrs.epsg));
    }
    const auto targetGrid = buildTargetGrid(grids);
    const std::string targetProjectionWkt = buildTargetProjectionWkt(targetSrs.epsg);
    const double pixelArea = std::abs(targetGrid.pixelWidth * targetGrid.pixelHeight);
    progress.onProgress(0.5);

    FeatureWindow globalWindow;
    bool hasGlobalWindow = false;
    if (!task.rasterOutputPath.empty()) {
        for (const auto& feature : vectorData.features) {
            auto geometry = cloneGeometryToTarget(feature.geometry.get(), vectorData.epsg, targetSrs.epsg);
            const auto window = buildFeatureWindow(*geometry, targetGrid);
            if (window.width <= 0 || window.height <= 0) {
                continue;
            }
            if (!hasGlobalWindow) {
                globalWindow = window;
                hasGlobalWindow = true;
            } else {
                globalWindow = mergeWindows(globalWindow, window, targetGrid);
            }
        }
    }

    std::unique_ptr<FeatureVectorOutputWriter> vectorWriter;
    if (!task.vectorOutputPath.empty()) {
        vectorWriter = std::make_unique<FeatureVectorOutputWriter>(task.vectorOutputPath, targetSrs.epsg, "feature_classes");
    }

    std::unique_ptr<FeatureRasterOutputWriter> rasterWriter;
    if (!task.rasterOutputPath.empty() && hasGlobalWindow) {
        rasterWriter = std::make_unique<FeatureRasterOutputWriter>(
            task.rasterOutputPath,
            targetSrs.epsg,
            globalWindow.width,
            globalWindow.height,
            globalWindow.geotransform);
    }

    progress.onMessage("正在执行地物分类统计...");
    FeatureStatsResultData resultData;
    resultData.actualSrs = targetSrs.epsgText;

    std::vector<int> nodataValues;
    nodataValues.reserve(task.rasters.size());
    for (const auto& raster : task.rasters) {
        nodataValues.push_back(static_cast<int>(std::llround(raster.nodata)));
    }

    std::size_t processed = 0;
    for (const auto& feature : vectorData.features) {
        auto geometry = cloneGeometryToTarget(feature.geometry.get(), vectorData.epsg, targetSrs.epsg);
        const auto window = buildFeatureWindow(*geometry, targetGrid);

        if (window.width <= 0 || window.height <= 0) {
            resultData.records.push_back(buildEmptyRecord(feature, targetSrs.epsgText));
            ++processed;
            progress.onProgress(0.5 + 0.35 * static_cast<double>(processed) / static_cast<double>(vectorData.features.size()));
            continue;
        }

        const auto featureMask = buildFeatureMask(*geometry, window, targetProjectionWkt);

        std::vector<std::vector<int>> rasterWindows;
        rasterWindows.reserve(task.rasters.size());
        for (const auto& raster : task.rasters) {
            rasterWindows.push_back(readRasterAsTargetWindow(raster, window, targetProjectionWkt));
        }

        const auto resolvedValues = resolvePixelsByPriority(rasterWindows, nodataValues, featureMask, classMap);
        auto records = countPixelsByPriority(rasterWindows, nodataValues, featureMask, classMap, pixelArea);
        for (auto& record : records) {
            record.featureId = feature.featureId;
            record.featureName = feature.featureName;
            record.actualSrs = targetSrs.epsgText;
        }

        if (records.size() == 1 && records[0].classValue == 0) {
            records[0] = buildEmptyRecord(feature, targetSrs.epsgText);
        }

        if (vectorWriter) {
            vectorWriter->writeResolvedRaster(
                resolvedValues,
                window.width,
                window.height,
                window.geotransform,
                {feature.featureId, feature.featureName, targetSrs.epsgText, pixelArea, &classMap});
        }

        if (rasterWriter && hasGlobalWindow) {
            rasterWriter->writeWindow(
                resolvedValues,
                window.xoff - globalWindow.xoff,
                window.yoff - globalWindow.yoff,
                window.width,
                window.height);
        }

        resultData.records.insert(resultData.records.end(), records.begin(), records.end());
        ++processed;
        progress.onProgress(0.5 + 0.35 * static_cast<double>(processed) / static_cast<double>(vectorData.features.size()));
    }

    appendSummaryRecords(resultData);

    progress.onMessage("正在写出统计结果...");
    writeResult(task, resultData);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("地物分类统计完成", task.outputPath);
    result.metadata["action"] = "feature_stats";
    result.metadata["vector"] = task.vectorPath;
    result.metadata["class_map"] = task.classMapPath;
    result.metadata["raster_count"] = std::to_string(task.rasters.size());
    result.metadata["output"] = task.outputPath;
    result.metadata["stage"] = "statistics_ready";
    result.metadata["feature_count"] = std::to_string(vectorData.features.size());
    result.metadata["resolved_target_epsg"] = std::to_string(targetSrs.epsg);
    result.metadata["grid_pixel_width"] = std::to_string(targetGrid.pixelWidth);
    result.metadata["grid_pixel_height"] = std::to_string(targetGrid.pixelHeight);
    result.metadata["record_count"] = std::to_string(resultData.records.size());
    result.metadata["vector_output"] = task.vectorOutputPath;
    result.metadata["raster_output"] = task.rasterOutputPath;
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::ClassificationPlugin)


