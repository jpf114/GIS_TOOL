#include "classification_plugin.h"

#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>

#include <cpl_conv.h>
#include <gdal_alg.h>
#include <gdal_priv.h>
#include <gdalwarper.h>
#include <ogrsf_frmts.h>
#include <opencv2/core.hpp>
#include <opencv2/ml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <limits>
#include <set>
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

struct SvmTask {
    std::string inputPath;
    std::vector<int> bands;
    std::string trainingCsvPath;
    std::string labelColumn = "label";
    std::string outputPath;
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
    void writeMetadata(const gis::core::ProcessingMetadata& metadata);

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
    void writeMetadata(const gis::core::ProcessingMetadata& metadata);

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
                error = "bands 娑擃厼鐡ㄩ崷銊︽￥閺佸牊鏆ｉ弫? " + item;
                return false;
            }
            values.push_back(value);
        } catch (...) {
            error = "bands 娑擃厼鐡ㄩ崷銊︽￥閺佸牊鏆ｉ弫? " + item;
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
                error = "nodatas 娑擃厼鐡ㄩ崷銊︽￥閺佸牊鏆熺€? " + item;
                return false;
            }
            values.push_back(value);
        } catch (...) {
            error = "nodatas 娑擃厼鐡ㄩ崷銊︽￥閺佸牊鏆熺€? " + item;
            return false;
        }
    }
    return true;
}

bool parseCsvHeaderAndRows(
    const std::string& path,
    std::vector<std::string>& headers,
    std::vector<std::vector<std::string>>& rows,
    std::string& error) {
    headers.clear();
    rows.clear();

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        error = "閺冪姵纭堕幍鎾崇磻鐠侇厾绮岄弽閿嬫拱 CSV: " + path;
        return false;
    }

    std::string line;
    if (!std::getline(ifs, line)) {
        error = "鐠侇厾绮岄弽閿嬫拱 CSV 娑撹櫣鈹? " + path;
        return false;
    }

    headers = splitCsv(line);
    if (headers.empty()) {
        error = "Training CSV header is missing: " + path;
        return false;
    }

    while (std::getline(ifs, line)) {
        auto cols = splitCsv(line);
        if (cols.empty()) {
            continue;
        }
        if (cols.size() != headers.size()) {
            error = "Training CSV column count is inconsistent.";
            return false;
        }
        rows.push_back(std::move(cols));
    }

    if (rows.empty()) {
        error = "Training CSV does not contain any sample rows: " + path;
        return false;
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
        throw std::runtime_error("Failed to import target EPSG.");
    }

    char* wkt = nullptr;
    if (srs.exportToWkt(&wkt) != OGRERR_NONE || wkt == nullptr) {
        throw std::runtime_error("Failed to export target projection to WKT.");
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

gis::core::ProcessingMetadata buildProcessingMetadata(
    GDALDataset* sourceDs,
    const std::string& inputPath,
    const std::string& algorithm) {
    gis::core::ProcessingMetadata metadata;
    metadata.sourceFile = inputPath;
    metadata.processingAlgorithm = algorithm;
    if (sourceDs) {
        const char* proj = sourceDs->GetProjectionRef();
        if (proj && proj[0] != '\0') {
            OGRSpatialReference srs;
            if (srs.importFromWkt(proj) == OGRERR_NONE) {
                srs.AutoIdentifyEPSG();
                int epsg = tryGetAuthorityEpsg(&srs);
                if (epsg > 0) {
                    metadata.sourceCrs = "EPSG:" + std::to_string(epsg);
                }
            }
        }
    }
    return metadata;
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
        throw std::runtime_error("Failed to create in-memory raster dataset.");
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
        throw std::runtime_error("Failed to open raster: " + input.path);
    }

    GDALRasterBand* band = dataset->GetRasterBand(input.band);
    if (band == nullptr) {
        throw std::runtime_error("Raster band is missing: " + input.path);
    }

    const GDALDataType dataType = band->GetRasterDataType();
    const bool supported =
        dataType == GDT_Byte ||
        dataType == GDT_Int16 ||
        dataType == GDT_UInt16 ||
        dataType == GDT_Int32 ||
        dataType == GDT_UInt32;
    if (!supported) {
        throw std::runtime_error("Unsupported raster data type: " + input.path);
    }

    double gt[6] = {};
    if (dataset->GetGeoTransform(gt) != CE_None) {
        throw std::runtime_error("Failed to read raster geotransform: " + input.path);
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
        throw std::runtime_error("Failed to open vector dataset: " + path);
    }
    GdalDatasetPtr dataset(rawDataset, GDALClose);

    OGRLayer* layer = dataset->GetLayer(0);
    if (layer == nullptr) {
        throw std::runtime_error("Vector layer is missing: " + path);
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
            throw std::runtime_error("Vector feature geometry is missing.");
        }

        result.features.push_back(std::move(item));
    }

    if (result.features.empty()) {
        throw std::runtime_error("Vector dataset does not contain any features.");
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
                throw std::runtime_error("Input rasters use different projected EPSG codes.");
            }
        }
        if (raster.srs.isGeographic) {
            hasGeographic = true;
        }
    }

    if (configTargetEpsg > 0) {
        if (hasProjected && projectedEpsg != 0 && projectedEpsg != configTargetEpsg) {
            throw std::runtime_error("Configured target_epsg conflicts with projected raster EPSG.");
        }
        return {configTargetEpsg, "EPSG:" + std::to_string(configTargetEpsg)};
    }

    if (!hasProjected && hasGeographic) {
        throw std::runtime_error("地理坐标栅格必须显式提供 target_epsg。");
    }

    if (projectedEpsg <= 0) {
        throw std::runtime_error("Failed to determine a projected target EPSG.");
    }

    return {projectedEpsg, "EPSG:" + std::to_string(projectedEpsg)};
}

RasterGridInfo suggestTargetGrid(const RasterInspectInfo& raster, int targetEpsg) {
    if (raster.srs.isProjected && raster.srs.epsg == targetEpsg) {
        return raster.grid;
    }

    GDALDataset* rawSource = static_cast<GDALDataset*>(GDALOpen(raster.input.path.c_str(), GA_ReadOnly));
    if (rawSource == nullptr) {
        throw std::runtime_error("Failed to reopen source raster: " + raster.input.path);
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
        throw std::runtime_error("Failed to build warped VRT: " + raster.input.path);
    }
    GdalDatasetPtr warped(rawWarped, GDALClose);

    double gt[6] = {};
    if (warped->GetGeoTransform(gt) != CE_None) {
        throw std::runtime_error("Failed to read warped raster geotransform.");
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
        throw std::runtime_error("No candidate target grids were provided.");
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
        throw std::runtime_error("Source geometry is null.");
    }

    OgrGeometryPtr cloned(geometry->clone());
    if (!cloned) {
        throw std::runtime_error("閻垽鍣洪崙鐘辩秿婢跺秴鍩楁径杈Е");
    }

    if (sourceEpsg == 0 || sourceEpsg == targetEpsg) {
        return cloned;
    }

    OGRSpatialReference sourceSrs;
    OGRSpatialReference targetSrs;
    if (sourceSrs.importFromEPSG(sourceEpsg) != OGRERR_NONE || targetSrs.importFromEPSG(targetEpsg) != OGRERR_NONE) {
        throw std::runtime_error("Failed to import geometry SRS.");
    }

    OGRCoordinateTransformation* transform = OGRCreateCoordinateTransformation(&sourceSrs, &targetSrs);
    if (transform == nullptr) {
        throw std::runtime_error("Failed to create coordinate transformation.");
    }

    const OGRErr transformResult = cloned->transform(transform);
    OCTDestroyCoordinateTransformation(reinterpret_cast<OGRCoordinateTransformationH>(transform));
    if (transformResult != OGRERR_NONE) {
        throw std::runtime_error("Failed to transform geometry to target SRS.");
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
        throw std::runtime_error("閺冪姵纭堕懢宄板絿閸愬懎鐡ㄦす鍗炲З");
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
        throw std::runtime_error("Failed to rasterize feature mask.");
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
        throw std::runtime_error("Failed to read feature mask from memory dataset.");
    }

    return mask;
}

std::vector<int> readRasterAsTargetWindow(
    const RasterTaskInput& rasterInput,
    const FeatureWindow& window,
    const std::string& targetProjectionWkt) {
    GDALDataset* rawSource = static_cast<GDALDataset*>(GDALOpen(rasterInput.path.c_str(), GA_ReadOnly));
    if (rawSource == nullptr) {
        throw std::runtime_error("Failed to open raster for reprojection: " + rasterInput.path);
    }
    GdalDatasetPtr source(rawSource, GDALClose);

    GDALRasterBand* sourceBand = source->GetRasterBand(rasterInput.band);
    if (sourceBand == nullptr) {
        throw std::runtime_error("閺嶅懏鐗稿▔銏☆唽娑撳秴鐡ㄩ崷? " + rasterInput.path);
    }

    GDALDriver* memDriver = GetGDALDriverManager()->GetDriverByName("MEM");
    if (memDriver == nullptr) {
        throw std::runtime_error("閺冪姵纭堕懢宄板絿閸愬懎鐡ㄦす鍗炲З");
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
        throw std::runtime_error("Failed to reproject raster into target window: " + rasterInput.path);
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
        throw std::runtime_error("Failed to read reprojected raster window: " + rasterInput.path);
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
        summary.featureName = "summary";
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
        throw std::runtime_error("Failed to create CSV output: " + path);
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
        throw std::runtime_error("Failed to create JSON output: " + path);
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
    throw std::runtime_error("Unsupported output format.");
}

std::map<int, std::string> loadClassMap(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open class map: " + path);
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
        throw std::runtime_error("Class map is empty or invalid.");
    }

    return classMap;
}

int getFieldIndexOrThrow(OGRLayer* layer, const char* fieldName) {
    const int index = layer->GetLayerDefn()->GetFieldIndex(fieldName);
    if (index < 0) {
        throw std::runtime_error("鏉堟挸鍤€涙顔岀紓鍝勩亼");
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
        throw std::runtime_error("GPKG driver is unavailable.");
    }

    GDALDataset* rawDataset = driver->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (rawDataset == nullptr) {
        throw std::runtime_error("Failed to create vector output dataset.");
    }
    dataset_.reset(rawDataset);

    OGRSpatialReference srs;
    if (srs.importFromEPSG(targetEpsg) != OGRERR_NONE) {
        throw std::runtime_error("Failed to import target EPSG for vector output.");
    }

    layer_ = dataset_->CreateLayer(layerName.c_str(), &srs, wkbPolygon, nullptr);
    if (layer_ == nullptr) {
        throw std::runtime_error("閺冪姵纭堕崚娑樼紦閻垽鍣洪崚鍡欒鏉堟挸鍤崶鎯х湴");
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
        throw std::runtime_error("Failed to create vector output fields.");
    }
}

void FeatureVectorOutputWriter::writeResolvedRaster(
    const std::vector<int>& resolvedValues,
    int width,
    int height,
    const std::array<double, 6>& geotransform,
    const PolygonRecordInfo& info) {
    if (layer_ == nullptr || info.classMap == nullptr) {
        throw std::runtime_error("Vector output writer is not initialized.");
    }

    GDALDriver* memDriver = GetGDALDriverManager()->GetDriverByName("MEM");
    if (memDriver == nullptr) {
        throw std::runtime_error("閺冪姵纭堕懢宄板絿閸愬懎鐡ㄦす鍗炲З");
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
        throw std::runtime_error("Failed to write resolved raster values into memory dataset.");
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
        throw std::runtime_error("Failed to polygonize resolved raster.");
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
            throw std::runtime_error("Failed to update vector output feature fields.");
        }

        OGRFeature::DestroyFeature(feature);
    }
}

void FeatureVectorOutputWriter::writeMetadata(const gis::core::ProcessingMetadata& metadata) {
    if (dataset_) {
        gis::core::writeProcessingMetadata(dataset_.get(), metadata);
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
        throw std::runtime_error("GTiff driver is unavailable.");
    }

    char const* options[] = {"COMPRESS=LZW", nullptr};
    GDALDataset* rawDataset = driver->Create(path.c_str(), width, height, 1, GDT_Int32, const_cast<char**>(options));
    if (rawDataset == nullptr) {
        throw std::runtime_error("Failed to create raster output dataset.");
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
        throw std::runtime_error("Raster output writer is not initialized.");
    }

    GDALRasterBand* band = dataset_->GetRasterBand(1);
    std::vector<int> current(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    if (band->RasterIO(GF_Read, xoff, yoff, width, height, current.data(), width, height, GDT_Int32, 0, 0, nullptr) != CE_None) {
        throw std::runtime_error("Failed to read raster output window.");
    }

    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i] != 0) {
            current[i] = values[i];
        }
    }

    if (band->RasterIO(GF_Write, xoff, yoff, width, height, current.data(), width, height, GDT_Int32, 0, 0, nullptr) != CE_None) {
        throw std::runtime_error("Failed to write raster output window.");
    }
}

void FeatureRasterOutputWriter::writeMetadata(const gis::core::ProcessingMetadata& metadata) {
    if (dataset_) {
        gis::core::writeProcessingMetadata(dataset_.get(), metadata);
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
        return gis::framework::Result::fail("rasters is required");
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
        return gis::framework::Result::fail("bands count must match rasters count");
    }
    if (!nodatas.empty() && nodatas.size() != rasterPaths.size()) {
        return gis::framework::Result::fail("nodatas count must match rasters count");
    }
    if (task.outputFormat.empty()) {
        return gis::framework::Result::fail("output must use .json or .csv");
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
            "action", "动作", "Classification plugin action",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0}, {"feature_stats", "svm_classify", "random_forest_classify", "max_likelihood_classify", "accuracy_assessment"}
        },
        gis::framework::ParamSpec{
            "input", "输入栅格", "Primary raster input for classification actions",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "vector", "输入矢量", "Vector features used by feature statistics",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "feature_id_field", "要素ID字段", "Optional field used as feature id",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "feature_name_field", "要素名称字段", "Optional field used as feature name",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "class_map", "类别映射", "JSON file mapping class values to class names",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "rasters", "分类栅格列表", "Comma-separated raster paths ordered by priority",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "bands", "波段列表", "Comma-separated band indexes matching rasters",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "nodatas", "NoData列表", "Comma-separated nodata values matching rasters",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "target_epsg", "目标EPSG", "Explicit projected EPSG used for reprojection and area stats",
            gis::framework::ParamType::Int, false, int{0},
            int{0}, int{99999}
        },
        gis::framework::ParamSpec{
            "output", "统计输出", "Feature statistics output path (.json or .csv)",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "vector_output", "矢量输出", "Optional polygonized vector output (.gpkg)",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "raster_output", "栅格输出", "Optional resolved raster output (.tif or .tiff)",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "training_csv", "训练CSV", "Training sample table with a label column",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "label_column", "标签列", "Label column name in training CSV",
            gis::framework::ParamType::String, false, std::string{"label"}
        },
        gis::framework::ParamSpec{
            "svm_c", "SVM C", "Penalty parameter for SVM C_SVC",
            gis::framework::ParamType::Double, false, double{2.0},
            double{0.001}, double{1e6}
        },
        gis::framework::ParamSpec{
            "svm_gamma", "SVM Gamma", "Gamma parameter for SVM RBF kernel",
            gis::framework::ParamType::Double, false, double{0.5},
            double{0.0}, double{1e6}
        },
        gis::framework::ParamSpec{
            "rf_max_depth", "RF最大深度", "Maximum tree depth for random forest",
            gis::framework::ParamType::Int, false, int{10},
            int{1}, int{100}
        },
        gis::framework::ParamSpec{
            "rf_tree_count", "RF树数量", "Number of trees for random forest",
            gis::framework::ParamType::Int, false, int{100},
            int{1}, int{10000}
        },
        gis::framework::ParamSpec{
            "rf_min_sample_count", "RF最小样本数", "Minimum sample count per split for random forest",
            gis::framework::ParamType::Int, false, int{2},
            int{1}, int{1000}
        },
        gis::framework::ParamSpec{
            "classified_raster", "分类结果栅格", "Raster produced by a classification workflow",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "reference_raster", "参考栅格", "Reference raster used by accuracy assessment",
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
        if (action == "svm_classify") {
            return doSvmClassify(params, progress);
        }
        if (action == "random_forest_classify") {
            return doRandomForestClassify(params, progress);
        }
        if (action == "max_likelihood_classify") {
            return doMaxLikelihoodClassify(params, progress);
        }
        if (action == "accuracy_assessment") {
            return doAccuracyAssessment(params, progress);
        }
        return gis::framework::Result::fail("Unknown action: " + action);
    } catch (const std::exception& ex) {
        return gis::framework::Result::fail(ex.what());
    }
}

gis::framework::Result ClassificationPlugin::doFeatureStats(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    progress.onMessage("Running feature statistics...");
    progress.throwIfCancelled();
    progress.onProgress(0.05);

    FeatureStatsTask task;
    auto parseResult = buildTaskFromParams(params, task);
    if (!parseResult.success) {
        return parseResult;
    }

    progress.onMessage("Loading class map and vector features...");
    const auto classMap = loadClassMap(task.classMapPath);
    const auto vectorData = openVectorData(task.vectorPath, task.featureIdField, task.featureNameField);
    if (vectorData.epsg == 0) {
        throw std::runtime_error("Vector layer is missing a valid EPSG code.");
    }
    progress.throwIfCancelled();
    progress.onProgress(0.2);

    progress.onMessage("Inspecting raster inputs...");
    std::vector<RasterInspectInfo> rasterInfos;
    rasterInfos.reserve(task.rasters.size());
    for (const auto& raster : task.rasters) {
        rasterInfos.push_back(inspectRaster(raster));
    }
    progress.throwIfCancelled();
    progress.onProgress(0.35);

    progress.onMessage("Resolving target projection and grid...");
    const auto targetSrs = resolveTargetSrs(task.targetEpsg, rasterInfos);
    std::vector<RasterGridInfo> grids;
    grids.reserve(rasterInfos.size());
    for (const auto& rasterInfo : rasterInfos) {
        grids.push_back(suggestTargetGrid(rasterInfo, targetSrs.epsg));
    }
    const auto targetGrid = buildTargetGrid(grids);
    const std::string targetProjectionWkt = buildTargetProjectionWkt(targetSrs.epsg);
    const double pixelArea = std::abs(targetGrid.pixelWidth * targetGrid.pixelHeight);
    progress.throwIfCancelled();
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

    progress.onMessage("濮濓絽婀幍褑顢戦崷鎵⒖閸掑棛琚紒鐔活吀...");
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
            progress.throwIfCancelled();
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
        progress.throwIfCancelled();
        progress.onProgress(0.5 + 0.35 * static_cast<double>(processed) / static_cast<double>(vectorData.features.size()));
    }

    appendSummaryRecords(resultData);

    gis::core::ProcessingMetadata fsMetadata;
    fsMetadata.sourceFile = task.vectorPath;
    fsMetadata.sourceCrs = targetSrs.epsgText;
    fsMetadata.processingAlgorithm = "classification.feature_stats";
    fsMetadata.algorithmParams["raster_count"] = std::to_string(task.rasters.size());

    if (rasterWriter) {
        rasterWriter->writeMetadata(fsMetadata);
    }
    if (vectorWriter) {
        vectorWriter->writeMetadata(fsMetadata);
    }

    progress.onMessage("Writing feature statistics outputs...");
    writeResult(task, resultData);
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("Feature statistics completed", task.outputPath);
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

gis::framework::Result ClassificationPlugin::doSvmClassify(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    SvmTask task;
    task.inputPath = gis::framework::getParam<std::string>(params, "input", "");
    task.trainingCsvPath = gis::framework::getParam<std::string>(params, "training_csv", "");
    task.labelColumn = gis::framework::getParam<std::string>(params, "label_column", "label");
    task.outputPath = gis::framework::getParam<std::string>(params, "output", "");

    if (task.inputPath.empty()) {
        return gis::framework::Result::fail("input is required");
    }
    if (task.trainingCsvPath.empty()) {
        return gis::framework::Result::fail("training_csv is required");
    }
    if (task.outputPath.empty()) {
        return gis::framework::Result::fail("output is required");
    }

    std::string bandError;
    if (!parseIntList(gis::framework::getParam<std::string>(params, "bands", ""), task.bands, bandError)) {
        return gis::framework::Result::fail(bandError);
    }

    progress.onMessage("濮濓絽婀拠璇插絿鐠侇厾绮岄弽閿嬫拱...");
    progress.throwIfCancelled();
    progress.onProgress(0.1);

    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    std::string csvError;
    if (!parseCsvHeaderAndRows(task.trainingCsvPath, headers, rows, csvError)) {
        return gis::framework::Result::fail(csvError);
    }

    int labelIndex = -1;
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == task.labelColumn) {
            labelIndex = static_cast<int>(i);
            break;
        }
    }
    if (labelIndex < 0) {
        return gis::framework::Result::fail("Training CSV is missing label column: " + task.labelColumn);
    }

    std::vector<int> featureIndices;
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (static_cast<int>(i) != labelIndex) {
            featureIndices.push_back(static_cast<int>(i));
        }
    }
    if (featureIndices.empty()) {
        return gis::framework::Result::fail("Training CSV does not contain any feature columns.");
    }

    auto ds = gis::core::openRaster(task.inputPath, true);
    if (!ds) {
        return gis::framework::Result::fail("Failed to open input raster: " + task.inputPath);
    }

    if (task.bands.empty()) {
        for (int bandIndex = 1; bandIndex <= ds->GetRasterCount(); ++bandIndex) {
            task.bands.push_back(bandIndex);
        }
    }
    if (featureIndices.size() != task.bands.size()) {
        return gis::framework::Result::fail("Training CSV feature count must match bands count.");
    }

    cv::Mat trainingSamples(static_cast<int>(rows.size()), static_cast<int>(featureIndices.size()), CV_32F);
    cv::Mat trainingLabels(static_cast<int>(rows.size()), 1, CV_32S);
    std::set<int> classValues;
    for (int rowIndex = 0; rowIndex < static_cast<int>(rows.size()); ++rowIndex) {
        try {
            trainingLabels.at<int>(rowIndex, 0) = std::stoi(rows[rowIndex][labelIndex]);
            classValues.insert(trainingLabels.at<int>(rowIndex, 0));
            for (int featureIndex = 0; featureIndex < static_cast<int>(featureIndices.size()); ++featureIndex) {
                trainingSamples.at<float>(rowIndex, featureIndex) =
                    std::stof(rows[rowIndex][featureIndices[featureIndex]]);
            }
        } catch (...) {
            return gis::framework::Result::fail("Training CSV contains invalid numeric values.");
        }
    }

    progress.onMessage("濮濓絽婀拋顓犵矊 SVM 閸掑棛琚崳?..");
    progress.throwIfCancelled();
    progress.onProgress(0.3);

    auto svm = cv::ml::SVM::create();
    svm->setType(cv::ml::SVM::C_SVC);
    svm->setKernel(cv::ml::SVM::RBF);
    double svmC = gis::framework::getParam<double>(params, "svm_c", 2.0);
    double svmGamma = gis::framework::getParam<double>(params, "svm_gamma", 0.5);
    svm->setC(svmC);
    svm->setGamma(svmGamma);
    svm->setTermCriteria(cv::TermCriteria(cv::TermCriteria::MAX_ITER, 1000, 1e-6));
    if (!svm->train(trainingSamples, cv::ml::ROW_SAMPLE, trainingLabels)) {
        return gis::framework::Result::fail("SVM 鐠侇厾绮屾径杈Е");
    }

    progress.onMessage("Reading raster bands...");
    progress.throwIfCancelled();
    progress.onProgress(0.45);

    std::vector<cv::Mat> bandMats;
    bandMats.reserve(task.bands.size());
    for (int bandIndex : task.bands) {
        if (bandIndex <= 0 || bandIndex > ds->GetRasterCount()) {
            return gis::framework::Result::fail("bands 娑擃厼鐡ㄩ崷銊︽￥閺佸牊灏濆▓闈涘娇");
        }
        bandMats.push_back(gis::core::gdalBandToMat(ds.get(), bandIndex));
    }

    const int width = ds->GetRasterXSize();
    const int height = ds->GetRasterYSize();
    cv::Mat predictSamples(width * height, static_cast<int>(task.bands.size()), CV_32F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int sampleIndex = y * width + x;
            for (int bandOffset = 0; bandOffset < static_cast<int>(bandMats.size()); ++bandOffset) {
                predictSamples.at<float>(sampleIndex, bandOffset) = bandMats[bandOffset].at<float>(y, x);
            }
        }
    }

    progress.onMessage("濮濓絽婀幍褑顢戦弽鍛壐閸掑棛琚?..");
    progress.throwIfCancelled();
    progress.onProgress(0.7);

    cv::Mat responses;
    svm->predict(predictSamples, responses);
    cv::Mat result(height, width, CV_32F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            result.at<float>(y, x) = responses.at<float>(y * width + x, 0);
        }
    }

    progress.onMessage("Writing classification raster...");
    gis::core::matToGdalTiff(result, ds.get(), task.outputPath, task.bands.front());
    auto svmMetadata = buildProcessingMetadata(ds.get(), task.inputPath, "classification.svm_classify");
    svmMetadata.algorithmParams["svm_c"] = std::to_string(svmC);
    svmMetadata.algorithmParams["svm_gamma"] = std::to_string(svmGamma);
    gis::core::writeProcessingMetadata(task.outputPath, svmMetadata);
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto execResult = gis::framework::Result::ok("SVM classification completed", task.outputPath);
    execResult.metadata["action"] = "svm_classify";
    execResult.metadata["input"] = task.inputPath;
    execResult.metadata["training_csv"] = task.trainingCsvPath;
    execResult.metadata["band_count"] = std::to_string(task.bands.size());
    execResult.metadata["sample_count"] = std::to_string(rows.size());
    execResult.metadata["class_count"] = std::to_string(classValues.size());
    return execResult;
}

gis::framework::Result ClassificationPlugin::doRandomForestClassify(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    SvmTask task;
    task.inputPath = gis::framework::getParam<std::string>(params, "input", "");
    task.trainingCsvPath = gis::framework::getParam<std::string>(params, "training_csv", "");
    task.labelColumn = gis::framework::getParam<std::string>(params, "label_column", "label");
    task.outputPath = gis::framework::getParam<std::string>(params, "output", "");

    if (task.inputPath.empty()) {
        return gis::framework::Result::fail("input is required");
    }
    if (task.trainingCsvPath.empty()) {
        return gis::framework::Result::fail("training_csv is required");
    }
    if (task.outputPath.empty()) {
        return gis::framework::Result::fail("output is required");
    }

    std::string bandError;
    if (!parseIntList(gis::framework::getParam<std::string>(params, "bands", ""), task.bands, bandError)) {
        return gis::framework::Result::fail(bandError);
    }

    progress.onMessage("濮濓絽婀拠璇插絿鐠侇厾绮岄弽閿嬫拱...");
    progress.throwIfCancelled();
    progress.onProgress(0.1);

    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    std::string csvError;
    if (!parseCsvHeaderAndRows(task.trainingCsvPath, headers, rows, csvError)) {
        return gis::framework::Result::fail(csvError);
    }

    int labelIndex = -1;
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == task.labelColumn) {
            labelIndex = static_cast<int>(i);
            break;
        }
    }
    if (labelIndex < 0) {
        return gis::framework::Result::fail("Training CSV is missing label column: " + task.labelColumn);
    }

    std::vector<int> featureIndices;
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (static_cast<int>(i) != labelIndex) {
            featureIndices.push_back(static_cast<int>(i));
        }
    }
    if (featureIndices.empty()) {
        return gis::framework::Result::fail("Training CSV does not contain any feature columns.");
    }

    auto ds = gis::core::openRaster(task.inputPath, true);
    if (!ds) {
        return gis::framework::Result::fail("Failed to open input raster: " + task.inputPath);
    }

    if (task.bands.empty()) {
        for (int bandIndex = 1; bandIndex <= ds->GetRasterCount(); ++bandIndex) {
            task.bands.push_back(bandIndex);
        }
    }
    if (featureIndices.size() != task.bands.size()) {
        return gis::framework::Result::fail("Training CSV feature count must match bands count.");
    }

    cv::Mat trainingSamples(static_cast<int>(rows.size()), static_cast<int>(featureIndices.size()), CV_32F);
    cv::Mat trainingLabels(static_cast<int>(rows.size()), 1, CV_32S);
    std::set<int> classValues;
    for (int rowIndex = 0; rowIndex < static_cast<int>(rows.size()); ++rowIndex) {
        try {
            trainingLabels.at<int>(rowIndex, 0) = std::stoi(rows[rowIndex][labelIndex]);
            classValues.insert(trainingLabels.at<int>(rowIndex, 0));
            for (int featureIndex = 0; featureIndex < static_cast<int>(featureIndices.size()); ++featureIndex) {
                trainingSamples.at<float>(rowIndex, featureIndex) =
                    std::stof(rows[rowIndex][featureIndices[featureIndex]]);
            }
        } catch (...) {
            return gis::framework::Result::fail("Training CSV contains invalid numeric values.");
        }
    }

    progress.onMessage("濮濓絽婀拋顓犵矊闂呭繑婧€濡喗鐏勯崚鍡欒閸?..");
    progress.throwIfCancelled();
    progress.onProgress(0.3);

    int rfMaxDepth = gis::framework::getParam<int>(params, "rf_max_depth", 10);
    int rfTreeCount = gis::framework::getParam<int>(params, "rf_tree_count", 100);
    int rfMinSampleCount = gis::framework::getParam<int>(params, "rf_min_sample_count", 2);

    auto forest = cv::ml::RTrees::create();
    forest->setMaxDepth(rfMaxDepth);
    forest->setMinSampleCount(rfMinSampleCount);
    forest->setRegressionAccuracy(0.0f);
    forest->setUseSurrogates(false);
    forest->setMaxCategories(std::max(2, static_cast<int>(classValues.size())));
    forest->setTermCriteria(cv::TermCriteria(cv::TermCriteria::MAX_ITER, rfTreeCount, 0));
    if (!forest->train(trainingSamples, cv::ml::ROW_SAMPLE, trainingLabels)) {
        return gis::framework::Result::fail("Random forest training failed.");
    }

    progress.onMessage("Reading raster bands...");
    progress.throwIfCancelled();
    progress.onProgress(0.45);

    std::vector<cv::Mat> bandMats;
    bandMats.reserve(task.bands.size());
    for (int bandIndex : task.bands) {
        if (bandIndex <= 0 || bandIndex > ds->GetRasterCount()) {
            return gis::framework::Result::fail("bands 娑擃厼鐡ㄩ崷銊︽￥閺佸牊灏濆▓闈涘娇");
        }
        bandMats.push_back(gis::core::gdalBandToMat(ds.get(), bandIndex));
    }

    const int width = ds->GetRasterXSize();
    const int height = ds->GetRasterYSize();
    cv::Mat predictSamples(width * height, static_cast<int>(task.bands.size()), CV_32F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int sampleIndex = y * width + x;
            for (int bandOffset = 0; bandOffset < static_cast<int>(bandMats.size()); ++bandOffset) {
                predictSamples.at<float>(sampleIndex, bandOffset) = bandMats[bandOffset].at<float>(y, x);
            }
        }
    }

    progress.onMessage("濮濓絽婀幍褑顢戦弽鍛壐閸掑棛琚?..");
    progress.throwIfCancelled();
    progress.onProgress(0.7);

    cv::Mat responses;
    forest->predict(predictSamples, responses);
    cv::Mat result(height, width, CV_32F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            result.at<float>(y, x) = responses.at<float>(y * width + x);
        }
    }

    progress.onMessage("Writing classification raster...");
    gis::core::matToGdalTiff(result, ds.get(), task.outputPath, task.bands.front());
    auto rfMetadata = buildProcessingMetadata(ds.get(), task.inputPath, "classification.random_forest_classify");
    rfMetadata.algorithmParams["rf_max_depth"] = std::to_string(rfMaxDepth);
    rfMetadata.algorithmParams["rf_tree_count"] = std::to_string(rfTreeCount);
    rfMetadata.algorithmParams["rf_min_sample_count"] = std::to_string(rfMinSampleCount);
    gis::core::writeProcessingMetadata(task.outputPath, rfMetadata);
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto execResult = gis::framework::Result::ok("Random forest classification completed", task.outputPath);
    execResult.metadata["action"] = "random_forest_classify";
    execResult.metadata["input"] = task.inputPath;
    execResult.metadata["training_csv"] = task.trainingCsvPath;
    execResult.metadata["band_count"] = std::to_string(task.bands.size());
    execResult.metadata["sample_count"] = std::to_string(rows.size());
    execResult.metadata["class_count"] = std::to_string(classValues.size());
    return execResult;
}

gis::framework::Result ClassificationPlugin::doMaxLikelihoodClassify(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    SvmTask task;
    task.inputPath = gis::framework::getParam<std::string>(params, "input", "");
    task.trainingCsvPath = gis::framework::getParam<std::string>(params, "training_csv", "");
    task.labelColumn = gis::framework::getParam<std::string>(params, "label_column", "label");
    task.outputPath = gis::framework::getParam<std::string>(params, "output", "");

    if (task.inputPath.empty()) {
        return gis::framework::Result::fail("input is required");
    }
    if (task.trainingCsvPath.empty()) {
        return gis::framework::Result::fail("training_csv is required");
    }
    if (task.outputPath.empty()) {
        return gis::framework::Result::fail("output is required");
    }

    std::string bandError;
    if (!parseIntList(gis::framework::getParam<std::string>(params, "bands", ""), task.bands, bandError)) {
        return gis::framework::Result::fail(bandError);
    }

    progress.onMessage("濮濓絽婀拠璇插絿鐠侇厾绮岄弽閿嬫拱...");
    progress.throwIfCancelled();
    progress.onProgress(0.1);

    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    std::string csvError;
    if (!parseCsvHeaderAndRows(task.trainingCsvPath, headers, rows, csvError)) {
        return gis::framework::Result::fail(csvError);
    }

    int labelIndex = -1;
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == task.labelColumn) {
            labelIndex = static_cast<int>(i);
            break;
        }
    }
    if (labelIndex < 0) {
        return gis::framework::Result::fail("Training CSV is missing label column: " + task.labelColumn);
    }

    std::vector<int> featureIndices;
    for (std::size_t i = 0; i < headers.size(); ++i) {
        if (static_cast<int>(i) != labelIndex) {
            featureIndices.push_back(static_cast<int>(i));
        }
    }
    if (featureIndices.empty()) {
        return gis::framework::Result::fail("Training CSV does not contain any feature columns.");
    }

    auto ds = gis::core::openRaster(task.inputPath, true);
    if (!ds) {
        return gis::framework::Result::fail("Failed to open input raster: " + task.inputPath);
    }

    if (task.bands.empty()) {
        for (int bandIndex = 1; bandIndex <= ds->GetRasterCount(); ++bandIndex) {
            task.bands.push_back(bandIndex);
        }
    }
    if (featureIndices.size() != task.bands.size()) {
        return gis::framework::Result::fail("Training CSV feature count must match bands count.");
    }

    std::map<int, std::vector<cv::Mat>> classSamples;
    for (const auto& row : rows) {
        try {
            const int label = std::stoi(row[labelIndex]);
            cv::Mat sample(1, static_cast<int>(featureIndices.size()), CV_32F);
            for (int featureIndex = 0; featureIndex < static_cast<int>(featureIndices.size()); ++featureIndex) {
                sample.at<float>(0, featureIndex) = std::stof(row[featureIndices[featureIndex]]);
            }
            classSamples[label].push_back(sample);
        } catch (...) {
            return gis::framework::Result::fail("Training CSV contains invalid numeric values.");
        }
    }

    if (classSamples.size() < 2) {
        return gis::framework::Result::fail("At least two classes are required for max likelihood classification.");
    }

    struct ClassStats {
        int label = 0;
        cv::Mat mean;
        cv::Mat invCov;
        double logDet = 0.0;
    };
    std::vector<ClassStats> stats;
    stats.reserve(classSamples.size());

    progress.onMessage("Estimating class statistics...");
    progress.throwIfCancelled();
    progress.onProgress(0.3);
    for (const auto& [label, samples] : classSamples) {
        cv::Mat sampleMat(static_cast<int>(samples.size()), static_cast<int>(featureIndices.size()), CV_32F);
        for (int i = 0; i < static_cast<int>(samples.size()); ++i) {
            samples[i].copyTo(sampleMat.row(i));
        }

        cv::Mat mean;
        cv::reduce(sampleMat, mean, 0, cv::REDUCE_AVG, CV_32F);
        cv::Mat centered = sampleMat - cv::repeat(mean, sampleMat.rows, 1);
        cv::Mat cov = (centered.t() * centered) / std::max(1, sampleMat.rows - 1);
        cov += cv::Mat::eye(cov.rows, cov.cols, CV_32F) * 1e-6f;

        double det = cv::determinant(cov);
        if (det <= 0.0) {
            det = 1e-6;
        }

        ClassStats item;
        item.label = label;
        item.mean = mean;
        item.invCov = cov.inv();
        item.logDet = std::log(det);
        stats.push_back(std::move(item));
    }

    progress.onMessage("Reading raster bands...");
    progress.throwIfCancelled();
    progress.onProgress(0.45);

    std::vector<cv::Mat> bandMats;
    bandMats.reserve(task.bands.size());
    for (int bandIndex : task.bands) {
        if (bandIndex <= 0 || bandIndex > ds->GetRasterCount()) {
            return gis::framework::Result::fail("bands 娑擃厼鐡ㄩ崷銊︽￥閺佸牊灏濆▓闈涘娇");
        }
        bandMats.push_back(gis::core::gdalBandToMat(ds.get(), bandIndex));
    }

    const int width = ds->GetRasterXSize();
    const int height = ds->GetRasterYSize();
    cv::Mat result(height, width, CV_32F);
    cv::Mat sample(1, static_cast<int>(task.bands.size()), CV_32F);

    progress.onMessage("濮濓絽婀幍褑顢戦張鈧径褌鎶€閻掕泛鍨庣猾?..");
    progress.throwIfCancelled();
    progress.onProgress(0.7);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int bandOffset = 0; bandOffset < static_cast<int>(bandMats.size()); ++bandOffset) {
                sample.at<float>(0, bandOffset) = bandMats[bandOffset].at<float>(y, x);
            }

            double bestScore = -std::numeric_limits<double>::infinity();
            int bestLabel = stats.front().label;
            for (const auto& item : stats) {
                cv::Mat diff = sample - item.mean;
                cv::Mat term = diff * item.invCov * diff.t();
                const double score = -0.5 * (term.at<float>(0, 0) + item.logDet);
                if (score > bestScore) {
                    bestScore = score;
                    bestLabel = item.label;
                }
            }
            result.at<float>(y, x) = static_cast<float>(bestLabel);
        }
    }

    progress.onMessage("Writing classification raster...");
    gis::core::matToGdalTiff(result, ds.get(), task.outputPath, task.bands.front());
    auto mlMetadata = buildProcessingMetadata(ds.get(), task.inputPath, "classification.max_likelihood_classify");
    gis::core::writeProcessingMetadata(task.outputPath, mlMetadata);
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto execResult = gis::framework::Result::ok("Max likelihood classification completed", task.outputPath);
    execResult.metadata["action"] = "max_likelihood_classify";
    execResult.metadata["input"] = task.inputPath;
    execResult.metadata["training_csv"] = task.trainingCsvPath;
    execResult.metadata["band_count"] = std::to_string(task.bands.size());
    execResult.metadata["sample_count"] = std::to_string(rows.size());
    execResult.metadata["class_count"] = std::to_string(classSamples.size());
    return execResult;
}

gis::framework::Result ClassificationPlugin::doAccuracyAssessment(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string classifiedPath = gis::framework::getParam<std::string>(params, "classified_raster", "");
    const std::string referencePath = gis::framework::getParam<std::string>(params, "reference_raster", "");
    const std::string outputPath = gis::framework::getParam<std::string>(params, "output", "");

    if (classifiedPath.empty()) return gis::framework::Result::fail("classified_raster is required");
    if (referencePath.empty()) return gis::framework::Result::fail("reference_raster is required");
    if (outputPath.empty()) return gis::framework::Result::fail("output is required");

    progress.throwIfCancelled();
    progress.onProgress(0.05);

    auto classifiedDs = gis::core::openRaster(classifiedPath, true);
    if (!classifiedDs) return gis::framework::Result::fail("Cannot open classified raster: " + classifiedPath);

    auto referenceDs = gis::core::openRaster(referencePath, true);
    if (!referenceDs) return gis::framework::Result::fail("Cannot open reference raster: " + referencePath);

    auto* classifiedBand = classifiedDs->GetRasterBand(1);
    auto* referenceBand = referenceDs->GetRasterBand(1);
    if (!classifiedBand || !referenceBand) return gis::framework::Result::fail("Cannot get raster band 1");

    int width = classifiedDs->GetRasterXSize();
    int height = classifiedDs->GetRasterYSize();
    int refWidth = referenceDs->GetRasterXSize();
    int refHeight = referenceDs->GetRasterYSize();

    if (width != refWidth || height != refHeight) {
        return gis::framework::Result::fail(
            "Raster dimensions do not match: classified " +
            std::to_string(width) + "x" + std::to_string(height) +
            " vs reference " + std::to_string(refWidth) + "x" + std::to_string(refHeight));
    }

    progress.throwIfCancelled();
    progress.onProgress(0.1);

    int classifiedNoData = static_cast<int>(classifiedBand->GetNoDataValue());
    int referenceNoData = static_cast<int>(referenceBand->GetNoDataValue());

    std::vector<int> classifiedBuf(static_cast<size_t>(width));
    std::vector<int> referenceBuf(static_cast<size_t>(width));

    std::map<int, std::map<int, int64_t>> confusionMatrix;
    std::set<int> allClasses;
    int64_t totalValid = 0;
    int64_t totalCorrect = 0;

    for (int y = 0; y < height; ++y) {
        classifiedBand->RasterIO(GF_Read, 0, y, width, 1,
                                 classifiedBuf.data(), width, 1, GDT_Int32, 0, 0);
        referenceBand->RasterIO(GF_Read, 0, y, width, 1,
                                referenceBuf.data(), width, 1, GDT_Int32, 0, 0);

        for (int x = 0; x < width; ++x) {
            int cVal = classifiedBuf[static_cast<size_t>(x)];
            int rVal = referenceBuf[static_cast<size_t>(x)];

            if (cVal == classifiedNoData || rVal == referenceNoData) continue;

            allClasses.insert(rVal);
            allClasses.insert(cVal);
            confusionMatrix[rVal][cVal]++;
            totalValid++;
            if (cVal == rVal) totalCorrect++;
        }

        if (y % 100 == 0) {
            double prog = 0.1 + 0.7 * static_cast<double>(y) / static_cast<double>(height);
            progress.onProgress(prog);
            progress.throwIfCancelled();
        }
    }

    progress.onProgress(0.85);
    progress.throwIfCancelled();

    double overallAccuracy = totalValid > 0 ? static_cast<double>(totalCorrect) / static_cast<double>(totalValid) : 0.0;

    std::vector<int> sortedClasses(allClasses.begin(), allClasses.end());
    size_t n = sortedClasses.size();

    std::map<int, double> producersAccuracy;
    std::map<int, double> usersAccuracy;

    for (size_t i = 0; i < n; ++i) {
        int cls = sortedClasses[i];
        int64_t colSum = 0;
        int64_t rowSum = 0;
        for (size_t j = 0; j < n; ++j) {
            colSum += confusionMatrix[sortedClasses[j]][cls];
            rowSum += confusionMatrix[cls][sortedClasses[j]];
        }
        producersAccuracy[cls] = colSum > 0 ? static_cast<double>(confusionMatrix[cls][cls]) / static_cast<double>(colSum) : 0.0;
        usersAccuracy[cls] = rowSum > 0 ? static_cast<double>(confusionMatrix[cls][cls]) / static_cast<double>(rowSum) : 0.0;
    }

    double kappa = 0.0;
    if (totalValid > 0) {
        double pe = 0.0;
        for (size_t i = 0; i < n; ++i) {
            int cls = sortedClasses[i];
            int64_t colSum = 0;
            int64_t rowSum = 0;
            for (size_t j = 0; j < n; ++j) {
                colSum += confusionMatrix[sortedClasses[j]][cls];
                rowSum += confusionMatrix[cls][sortedClasses[j]];
            }
            pe += static_cast<double>(colSum) * static_cast<double>(rowSum);
        }
        pe /= static_cast<double>(totalValid) * static_cast<double>(totalValid);
        kappa = (overallAccuracy - pe) / (1.0 - pe);
    }

    progress.onProgress(0.9);
    progress.throwIfCancelled();

    std::ostringstream json;
    json << "{\n";
    json << "  \"confusion_matrix\": {\n";
    for (size_t i = 0; i < n; ++i) {
        int rowCls = sortedClasses[i];
        json << "    \"" << rowCls << "\": {";
        for (size_t j = 0; j < n; ++j) {
            int colCls = sortedClasses[j];
            json << "\"" << colCls << "\": " << confusionMatrix[rowCls][colCls];
            if (j + 1 < n) json << ", ";
        }
        json << "}";
        if (i + 1 < n) json << ",";
        json << "\n";
    }
    json << "  },\n";

    json << "  \"class_names\": [";
    for (size_t i = 0; i < n; ++i) {
        json << sortedClasses[i];
        if (i + 1 < n) json << ", ";
    }
    json << "],\n";

    json << "  \"overall_accuracy\": " << std::fixed << std::setprecision(6) << overallAccuracy << ",\n";
    json << "  \"kappa\": " << std::fixed << std::setprecision(6) << kappa << ",\n";

    json << "  \"producers_accuracy\": {";
    for (size_t i = 0; i < n; ++i) {
        json << "\"" << sortedClasses[i] << "\": " << std::fixed << std::setprecision(6) << producersAccuracy[sortedClasses[i]];
        if (i + 1 < n) json << ", ";
    }
    json << "},\n";

    json << "  \"users_accuracy\": {";
    for (size_t i = 0; i < n; ++i) {
        json << "\"" << sortedClasses[i] << "\": " << std::fixed << std::setprecision(6) << usersAccuracy[sortedClasses[i]];
        if (i + 1 < n) json << ", ";
    }
    json << "},\n";

    json << "  \"total_samples\": " << totalValid << ",\n";
    json << "  \"correct_samples\": " << totalCorrect << "\n";
    json << "}\n";

    {
        std::ofstream ofs(outputPath);
        if (!ofs.is_open()) {
            return gis::framework::Result::fail("Cannot write output: " + outputPath);
        }
        ofs << json.str();
    }

    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "缁儳瀹崇拠鍕強鐎瑰本鍨? OA=" + std::to_string(overallAccuracy) +
        " Kappa=" + std::to_string(kappa), outputPath);
    result.metadata["action"] = "accuracy_assessment";
    result.metadata["overall_accuracy"] = std::to_string(overallAccuracy);
    result.metadata["kappa"] = std::to_string(kappa);
    result.metadata["total_samples"] = std::to_string(totalValid);
    result.metadata["correct_samples"] = std::to_string(totalCorrect);
    result.metadata["class_count"] = std::to_string(n);
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::ClassificationPlugin)




