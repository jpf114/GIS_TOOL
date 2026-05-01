#include "vector_plugin.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <gdal_alg.h>
#include <gdal_utils.h>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace gis::plugins {

namespace fs = std::filesystem;

namespace {

bool containsNonAscii(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch >= 0x80;
    });
}

bool isShapefilePath(const std::string& path) {
    fs::path fsPath = fs::u8path(path);
    std::string ext = fsPath.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".shp";
}

#ifdef _WIN32
std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int sizeNeeded = MultiByteToWideChar(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        nullptr, 0);
    if (sizeNeeded <= 0) {
        return {};
    }

    std::wstring wide(sizeNeeded, L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        wide.data(), sizeNeeded);
    return wide;
}

std::string makeAsciiShapefileMirror(const std::string& sourcePath) {
    const fs::path sourceFsPath(utf8ToWide(sourcePath));
    if (sourceFsPath.empty() || !fs::exists(sourceFsPath)) {
        return sourcePath;
    }

    std::error_code ec;
    const auto sourceFileSize = fs::file_size(sourceFsPath, ec);
    const auto sourceWriteTime = fs::last_write_time(sourceFsPath, ec);
    const auto cacheKey = std::to_string(std::hash<std::string>{}(sourcePath)) + "_" +
        std::to_string(static_cast<unsigned long long>(sourceFileSize)) + "_" +
        std::to_string(sourceWriteTime.time_since_epoch().count());
    const fs::path tempDir = fs::temp_directory_path() / "gis_tool_shapefile_utf8" / cacheKey;
    fs::create_directories(tempDir, ec);

    const fs::path tempBase = tempDir / "input.shp";
    if (fs::exists(tempBase)) {
        return tempBase.string();
    }

    const std::vector<std::wstring> sidecarExts = {
        L".shp", L".shx", L".dbf", L".prj", L".cpg", L".qix", L".fix", L".sbn", L".sbx"
    };

    auto copyOrLinkFile = [](const fs::path& src, const fs::path& dst) {
        std::error_code localEc;
        fs::remove(dst, localEc);
        localEc.clear();
        fs::create_hard_link(src, dst, localEc);
        if (!localEc) {
            return true;
        }

        localEc.clear();
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, localEc);
        return !localEc;
    };

    for (const auto& ext : sidecarExts) {
        fs::path src = sourceFsPath;
        src.replace_extension(ext);
        if (!fs::exists(src)) {
            continue;
        }

        fs::path dst = tempBase;
        dst.replace_extension(ext);
        if (!copyOrLinkFile(src, dst)) {
            return sourcePath;
        }
    }

    if (!fs::exists(tempBase)) {
        return sourcePath;
    }

    return tempBase.string();
}
#endif

std::string resolveVectorInputPath(const std::string& path) {
#ifdef _WIN32
    if (containsNonAscii(path) && isShapefilePath(path)) {
        return makeAsciiShapefileMirror(path);
    }
#endif
    return path;
}

} // namespace

std::vector<gis::framework::ParamSpec> VectorPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"info", "filter", "buffer", "clip", "rasterize", "polygonize", "convert", "union", "difference", "dissolve"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入矢量/栅格文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出文件", "输出文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "layer", "图层名", "操作的图层名(默认第一个图层)",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "where", "属性过滤", "SQL WHERE 条件表达式，如 population > 10000",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "extent", "空间范围", "空间过滤范围(xmin,ymin,xmax,ymax)",
            gis::framework::ParamType::Extent, false, std::array<double,4>{0,0,0,0}
        },
        gis::framework::ParamSpec{
            "distance", "缓冲距离", "缓冲区分析的距离(地图单位)",
            gis::framework::ParamType::Double, false, double{100.0}
        },
        gis::framework::ParamSpec{
            "clip_vector", "裁切矢量", "用于裁切的矢量文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "resolution", "栅格分辨率", "矢量转栅格时的像元大小",
            gis::framework::ParamType::Double, false, double{1.0}
        },
        gis::framework::ParamSpec{
            "attribute", "属性字段", "栅格化时写入的属性字段名(不指定则写入 1)",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "band", "波段序号", "栅格转矢量时使用的波段序号(从 1 开始)",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "format", "输出格式", "格式转换的目标格式(GeoJSON/ESRI Shapefile/GPKG/KML/CSV)",
            gis::framework::ParamType::Enum, false, std::string{"GeoJSON"},
            int{0}, int{0},
            {"GeoJSON", "ESRI Shapefile", "GPKG", "KML", "CSV"}
        },
        gis::framework::ParamSpec{
            "overlay_vector", "叠加矢量", "并集/差集操作的第二个矢量文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "dissolve_field", "融合字段", "融合操作按该字段值合并相邻多边形(不指定则全部合并)",
            gis::framework::ParamType::String, false, std::string{}
        },
    };
}

gis::framework::Result VectorPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string action = gis::framework::getParam<std::string>(params, "action", "");

    if (action == "info")       return doInfo(params, progress);
    if (action == "filter")     return doFilter(params, progress);
    if (action == "buffer")     return doBuffer(params, progress);
    if (action == "clip")       return doClip(params, progress);
    if (action == "rasterize")  return doRasterize(params, progress);
    if (action == "polygonize") return doPolygonize(params, progress);
    if (action == "convert")    return doConvert(params, progress);
    if (action == "union")      return doUnion(params, progress);
    if (action == "difference") return doDifference(params, progress);
    if (action == "dissolve")   return doDissolve(params, progress);

    return gis::framework::Result::fail("Unknown action: " + action);
}

static GDALDataset* openVector(const std::string& path, bool readOnly = true) {
    const std::string resolvedPath = resolveVectorInputPath(path);
    return (GDALDataset*)GDALOpenEx(
        resolvedPath.c_str(),
        (readOnly ? GDAL_OF_READONLY : GDAL_OF_UPDATE) | GDAL_OF_VECTOR,
        nullptr, nullptr, nullptr);
}

static OGRLayer* getLayer(GDALDataset* ds, const std::string& layerName) {
    if (!layerName.empty()) {
        if (auto* layer = ds->GetLayerByName(layerName.c_str())) {
            return layer;
        }
        if (ds->GetLayerCount() == 1) {
            return ds->GetLayer(0);
        }
        return nullptr;
    }
    return ds->GetLayer(0);
}

static std::string geometryTypeName(OGRwkbGeometryType type) {
    switch (wkbFlatten(type)) {
        case wkbPoint:           return "Point";
        case wkbMultiPoint:      return "MultiPoint";
        case wkbLineString:      return "LineString";
        case wkbMultiLineString: return "MultiLineString";
        case wkbPolygon:         return "Polygon";
        case wkbMultiPolygon:    return "MultiPolygon";
        case wkbGeometryCollection: return "GeometryCollection";
        default:                 return "Unknown";
    }
}

static GDALDriver* getDriverForFormat(const std::string& format) {
    if (format == "GeoJSON")         return GetGDALDriverManager()->GetDriverByName("GeoJSON");
    if (format == "ESRI Shapefile")  return GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
    if (format == "GPKG")            return GetGDALDriverManager()->GetDriverByName("GPKG");
    if (format == "KML")             return GetGDALDriverManager()->GetDriverByName("KML");
    if (format == "CSV")             return GetGDALDriverManager()->GetDriverByName("CSV");
    return nullptr;
}

static std::string detectSrsType(const OGRSpatialReference* srs) {
    if (!srs) {
        return "unknown";
    }
    if (srs->IsGeographic()) {
        return "geographic";
    }
    if (srs->IsProjected()) {
        return "projected";
    }
    return "unknown";
}

static bool areSameCrs(const OGRSpatialReference* lhs, const OGRSpatialReference* rhs) {
    if (!lhs || !rhs) {
        return lhs == rhs;
    }
    return lhs->IsSame(rhs);
}

static gis::framework::Result failProjectedRequired(const std::string& action) {
    return gis::framework::Result::fail(
        action + " requires a projected coordinate system. Please reproject the input layer before processing.");
}

static gis::framework::Result failCrsMismatch(const std::string& action) {
    return gis::framework::Result::fail(
        action + " requires input layers to use the same CRS.");
}

static gis::framework::Result failInvalidGeometry(const std::string& action) {
    return gis::framework::Result::fail(
        action + " requires valid overlay geometries and automatic repair failed.");
}

class ScopedQuietGdalErrorHandler {
public:
    ScopedQuietGdalErrorHandler() {
        CPLPushErrorHandler(CPLQuietErrorHandler);
    }

    ~ScopedQuietGdalErrorHandler() {
        CPLPopErrorHandler();
    }
};

static OGRGeometry* cloneNormalizedGeometry(const OGRGeometry* geom) {
    if (!geom) {
        return nullptr;
    }

    OGRGeometry* cloned = geom->clone();
    if (!cloned || cloned->IsEmpty()) {
        return cloned;
    }

    bool isValid = false;
    {
        ScopedQuietGdalErrorHandler quietErrors;
        isValid = cloned->IsValid();
    }
    if (isValid) {
        return cloned;
    }

    OGRGeometry* fixed = nullptr;
    {
        ScopedQuietGdalErrorHandler quietErrors;
        fixed = cloned->MakeValid();
    }
    delete cloned;
    if (!fixed || fixed->IsEmpty()) {
        delete fixed;
        return nullptr;
    }

    return fixed;
}

static OGRGeometry* buildNormalizedUnion(OGRLayer* layer) {
    if (!layer) {
        return nullptr;
    }

    std::vector<OGRGeometry*> geoms;
    layer->ResetReading();
    OGRFeature* feat = nullptr;
    while ((feat = layer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = cloneNormalizedGeometry(feat->GetGeometryRef());
        if (geom) {
            geoms.push_back(geom);
        }
        OGRFeature::DestroyFeature(feat);
    }

    if (geoms.empty()) {
        return nullptr;
    }

    OGRGeometry* merged = geoms[0];
    for (size_t i = 1; i < geoms.size(); ++i) {
        OGRGeometry* next = merged ? merged->Union(geoms[i]) : nullptr;
        delete merged;
        delete geoms[i];
        if (!next || next->IsEmpty()) {
            delete next;
            return nullptr;
        }
        merged = next;
    }

    return merged;
}

struct OverlayGeometryEntry {
    std::unique_ptr<OGRGeometry> geometry;
    OGREnvelope envelope{};
};

static OGRGeometry* unionGeometryList(std::vector<OGRGeometry*>& geoms);

static std::vector<OverlayGeometryEntry> collectNormalizedGeometries(OGRLayer* layer) {
    std::vector<OverlayGeometryEntry> entries;
    if (!layer) {
        return entries;
    }

    layer->ResetReading();
    OGRFeature* feat = nullptr;
    while ((feat = layer->GetNextFeature()) != nullptr) {
        std::unique_ptr<OGRGeometry> geom(cloneNormalizedGeometry(feat->GetGeometryRef()));
        if (geom && !geom->IsEmpty()) {
            OverlayGeometryEntry entry;
            geom->getEnvelope(&entry.envelope);
            entry.geometry = std::move(geom);
            entries.push_back(std::move(entry));
        }
        OGRFeature::DestroyFeature(feat);
    }

    return entries;
}

static bool envelopesIntersect(const OGREnvelope& lhs, const OGREnvelope& rhs) {
    return !(lhs.MaxX < rhs.MinX || lhs.MinX > rhs.MaxX || lhs.MaxY < rhs.MinY || lhs.MinY > rhs.MaxY);
}

static double computeEnvelopeOverlapRatio(const std::vector<OverlayGeometryEntry>& entries) {
    if (entries.size() < 2) {
        return 0.0;
    }

    size_t overlapPairs = 0;
    size_t totalPairs = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        for (size_t j = i + 1; j < entries.size(); ++j) {
            ++totalPairs;
            if (envelopesIntersect(entries[i].envelope, entries[j].envelope)) {
                ++overlapPairs;
            }
        }
    }

    return totalPairs == 0 ? 0.0 : static_cast<double>(overlapPairs) / static_cast<double>(totalPairs);
}

static OGRGeometry* buildCandidateOverlayGeometry(
    const std::vector<OverlayGeometryEntry>& entries,
    const OGRGeometry* target) {
    if (!target) {
        return nullptr;
    }

    OGREnvelope targetEnvelope;
    target->getEnvelope(&targetEnvelope);

    OGRGeometry* merged = nullptr;
    for (const auto& entry : entries) {
        if (!envelopesIntersect(entry.envelope, targetEnvelope)) {
            continue;
        }

        if (!merged) {
            merged = entry.geometry->clone();
            continue;
        }

        OGRGeometry* next = merged->Union(entry.geometry.get());
        delete merged;
        merged = next;
        if (!merged || merged->IsEmpty()) {
            delete merged;
            return nullptr;
        }
    }

    return merged;
}

static OGRGeometry* buildMergedOverlayGeometry(
    const std::vector<OverlayGeometryEntry>& entries) {
    std::vector<OGRGeometry*> geometries;
    geometries.reserve(entries.size());
    for (const auto& entry : entries) {
        if (!entry.geometry || entry.geometry->IsEmpty()) {
            continue;
        }
        geometries.push_back(entry.geometry->clone());
    }
    return unionGeometryList(geometries);
}

static std::vector<const OverlayGeometryEntry*> collectCandidateOverlayEntries(
    const std::vector<OverlayGeometryEntry>& entries,
    const OGRGeometry* target) {
    std::vector<const OverlayGeometryEntry*> candidates;
    if (!target) {
        return candidates;
    }

    OGREnvelope targetEnvelope;
    target->getEnvelope(&targetEnvelope);
    for (const auto& entry : entries) {
        if (envelopesIntersect(entry.envelope, targetEnvelope)) {
            candidates.push_back(&entry);
        }
    }
    return candidates;
}

static void removeExistingVectorOutput(const std::string& output, const std::string& format) {
    namespace fs = std::filesystem;
    if (!fs::exists(output)) {
        return;
    }

    if (format == "ESRI Shapefile") {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) {
                fs::remove(entry.path());
            }
        }
        return;
    }

    fs::remove(output);
}

static void ensureParentDirectoryForFile(const std::string& output) {
    const fs::path outputPath = fs::u8path(output);
    if (!outputPath.has_parent_path()) {
        return;
    }

    fs::create_directories(outputPath.parent_path());
}

static std::string escapeSqlLiteral(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        escaped.push_back(ch);
        if (ch == '\'') {
            escaped.push_back('\'');
        }
    }
    return escaped;
}

static std::vector<std::string> buildGpkgFastWriteLayerOptions(bool useTransactions) {
    std::vector<std::string> options;
    if (useTransactions) {
        options.emplace_back("SPATIAL_INDEX=NO");
    }
    return options;
}

static std::vector<char*> buildLayerOptionPointers(std::vector<std::string>& options) {
    std::vector<char*> pointers;
    for (auto& option : options) {
        pointers.push_back(option.data());
    }
    pointers.push_back(nullptr);
    return pointers;
}

static void ensureGpkgExtensionsTable(GDALDataset* dataset) {
    if (!dataset) {
        return;
    }

    const char* createExtensionsSql =
        "CREATE TABLE IF NOT EXISTS gpkg_extensions ("
        "table_name TEXT,"
        "column_name TEXT,"
        "extension_name TEXT NOT NULL,"
        "definition TEXT NOT NULL,"
        "scope TEXT NOT NULL,"
        "CONSTRAINT ge_tce UNIQUE (table_name, column_name, extension_name)"
        ")";
    if (OGRLayer* sqlResult = dataset->ExecuteSQL(createExtensionsSql, nullptr, "SQLITE")) {
        dataset->ReleaseResultSet(sqlResult);
    }
}

static void addSpatialIndexIfNeeded(
    GDALDataset* dataset,
    OGRLayer* layer,
    bool useTransactions,
    const std::string& layerName) {
    if (!dataset || !layer || !useTransactions) {
        return;
    }

    const char* geometryColumn = layer->GetGeometryColumn();
    const std::string geometryColumnName =
        (geometryColumn && *geometryColumn) ? geometryColumn : "geom";
    ensureGpkgExtensionsTable(dataset);
    const std::string addSpatialIndexSql =
        "SELECT gpkgAddSpatialIndex('" + escapeSqlLiteral(layerName) +
        "', '" + escapeSqlLiteral(geometryColumnName) + "')";
    if (OGRLayer* sqlResult = dataset->ExecuteSQL(addSpatialIndexSql.c_str(), nullptr, "SQLITE")) {
        dataset->ReleaseResultSet(sqlResult);
    }
}

static OGRwkbGeometryType resultLayerGeometryType(OGRwkbGeometryType srcType) {
    switch (wkbFlatten(srcType)) {
        case wkbPoint:
        case wkbMultiPoint:
            return wkbMultiPoint;
        case wkbLineString:
        case wkbMultiLineString:
            return wkbMultiLineString;
        case wkbPolygon:
        case wkbMultiPolygon:
            return wkbMultiPolygon;
        default:
            return wkbUnknown;
    }
}

static int geometryDimensionRank(OGRwkbGeometryType type) {
    switch (wkbFlatten(type)) {
        case wkbPoint:
        case wkbMultiPoint:
            return 0;
        case wkbLineString:
        case wkbMultiLineString:
            return 1;
        case wkbPolygon:
        case wkbMultiPolygon:
            return 2;
        default:
            return -1;
    }
}

static bool isLinearGeometryType(OGRwkbGeometryType type) {
    const auto flatType = wkbFlatten(type);
    return flatType == wkbLineString || flatType == wkbMultiLineString;
}

static OGRwkbGeometryType effectiveLayerGeometryType(OGRLayer* layer) {
    if (!layer) {
        return wkbUnknown;
    }

    const OGRwkbGeometryType declaredType = layer->GetGeomType();
    if (geometryDimensionRank(declaredType) >= 0) {
        return declaredType;
    }

    layer->ResetReading();
    OGRFeature* feature = nullptr;
    while ((feature = layer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feature->GetGeometryRef();
        if (geom && !geom->IsEmpty()) {
            const OGRwkbGeometryType detectedType = geom->getGeometryType();
            OGRFeature::DestroyFeature(feature);
            layer->ResetReading();
            return detectedType;
        }
        OGRFeature::DestroyFeature(feature);
    }
    layer->ResetReading();
    return declaredType;
}

static void collectLinearGeometryParts(const OGRGeometry* geom, std::vector<OGRGeometry*>& parts) {
    if (!geom || geom->IsEmpty()) {
        return;
    }

    const auto flatType = wkbFlatten(geom->getGeometryType());
    if (flatType == wkbLineString) {
        parts.push_back(geom->clone());
        return;
    }

    if (flatType == wkbMultiLineString) {
        const auto* multiLine = geom->toMultiLineString();
        for (int i = 0; i < multiLine->getNumGeometries(); ++i) {
            parts.push_back(multiLine->getGeometryRef(i)->clone());
        }
        return;
    }

    if (flatType == wkbGeometryCollection) {
        const auto* collection = geom->toGeometryCollection();
        for (int i = 0; i < collection->getNumGeometries(); ++i) {
            collectLinearGeometryParts(collection->getGeometryRef(i), parts);
        }
    }
}

static bool writeGeometryFeature(
    OGRLayer* layer,
    OGRGeometry* geom,
    const std::function<void(OGRFeature*)>& fillAttributes = {});

static bool writeGeometryFeatureOwned(
    OGRLayer* layer,
    std::unique_ptr<OGRGeometry> geom,
    const std::function<void(OGRFeature*)>& fillAttributes = {});

static std::unique_ptr<OGRGeometry> promoteGeometryToLayerType(
    OGRLayer* layer,
    OGRGeometry* geom) {
    if (!layer || !geom) {
        return nullptr;
    }

    const auto targetType = wkbFlatten(layer->GetGeomType());
    if (targetType == wkbMultiLineString && wkbFlatten(geom->getGeometryType()) == wkbLineString) {
        auto multi = std::make_unique<OGRMultiLineString>();
        multi->addGeometry(geom);
        return multi;
    }

    if (targetType == wkbMultiPolygon && wkbFlatten(geom->getGeometryType()) == wkbPolygon) {
        auto multi = std::make_unique<OGRMultiPolygon>();
        multi->addGeometry(geom);
        return multi;
    }

    return nullptr;
}

static bool writeUnionFeature(OGRLayer* layer, OGRGeometry* geom, const char* sourceValue) {
    return writeGeometryFeature(layer, geom, [sourceValue](OGRFeature* feature) {
        feature->SetField("source", sourceValue);
    });
}

static bool writeUnionFeatureOwned(
    OGRLayer* layer,
    std::unique_ptr<OGRGeometry> geom,
    const char* sourceValue) {
    return writeGeometryFeatureOwned(layer, std::move(geom), [sourceValue](OGRFeature* feature) {
        feature->SetField("source", sourceValue);
    });
}

static bool writeGeometryFeature(
    OGRLayer* layer,
    OGRGeometry* geom,
    const std::function<void(OGRFeature*)>& fillAttributes) {
    if (!layer || !geom || geom->IsEmpty()) {
        return true;
    }

    OGRFeature* dstFeat = OGRFeature::CreateFeature(layer->GetLayerDefn());
    if (fillAttributes) {
        fillAttributes(dstFeat);
    }
    std::unique_ptr<OGRGeometry> promoted = promoteGeometryToLayerType(layer, geom);
    dstFeat->SetGeometry(promoted ? promoted.get() : geom);
    dstFeat->SetFID(OGRNullFID);
    const bool ok = layer->CreateFeature(dstFeat) == OGRERR_NONE;
    OGRFeature::DestroyFeature(dstFeat);
    return ok;
}

static bool writeGeometryFeatureOwned(
    OGRLayer* layer,
    std::unique_ptr<OGRGeometry> geom,
    const std::function<void(OGRFeature*)>& fillAttributes) {
    if (!layer || !geom || geom->IsEmpty()) {
        return true;
    }

    OGRFeature* dstFeat = OGRFeature::CreateFeature(layer->GetLayerDefn());
    if (fillAttributes) {
        fillAttributes(dstFeat);
    }
    std::unique_ptr<OGRGeometry> promoted = promoteGeometryToLayerType(layer, geom.get());
    if (promoted) {
        geom.reset();
        dstFeat->SetGeometryDirectly(promoted.release());
    } else {
        dstFeat->SetGeometryDirectly(geom.release());
    }
    dstFeat->SetFID(OGRNullFID);
    const bool ok = layer->CreateFeature(dstFeat) == OGRERR_NONE;
    OGRFeature::DestroyFeature(dstFeat);
    return ok;
}

static void copyFeatureFieldsOnly(
    OGRFeature* dstFeature,
    const OGRFeature* srcFeature,
    const std::vector<int>& fieldMap) {
    if (!dstFeature || !srcFeature) {
        return;
    }

    if (!fieldMap.empty()) {
        dstFeature->SetFieldsFrom(srcFeature, fieldMap.data(), TRUE);
    }
    dstFeature->SetFID(OGRNullFID);
}

static bool writeFeatureWithCopiedFields(
    OGRLayer* layer,
    const OGRFeature* sourceFeature,
    OGRGeometry* geom,
    const std::vector<int>* fieldMap = nullptr) {
    return writeGeometryFeature(layer, geom, [sourceFeature, fieldMap](OGRFeature* feature) {
        if (sourceFeature) {
            if (!fieldMap) {
                feature->SetFrom(sourceFeature, TRUE);
                feature->SetFID(OGRNullFID);
            } else {
                copyFeatureFieldsOnly(feature, sourceFeature, *fieldMap);
            }
        }
    });
}

static bool writeFeatureWithCopiedFieldsOwned(
    OGRLayer* layer,
    const OGRFeature* sourceFeature,
    std::unique_ptr<OGRGeometry> geom,
    const std::vector<int>* fieldMap = nullptr) {
    return writeGeometryFeatureOwned(layer, std::move(geom), [sourceFeature, fieldMap](OGRFeature* feature) {
        if (sourceFeature) {
            if (!fieldMap) {
                feature->SetFrom(sourceFeature, TRUE);
                feature->SetFID(OGRNullFID);
            } else {
                copyFeatureFieldsOnly(feature, sourceFeature, *fieldMap);
            }
        }
    });
}

static bool isLinePolygonPracticalUnionCase(OGRLayer* srcLayer, OGRLayer* overlayLayer) {
    if (!srcLayer || !overlayLayer) {
        return false;
    }

    return isLinearGeometryType(srcLayer->GetGeomType())
        && geometryDimensionRank(overlayLayer->GetGeomType()) == 2;
}

static int detectOverlayGeometryDimensionRank(const std::vector<OverlayGeometryEntry>& entries) {
    int maxRank = -1;
    for (const auto& entry : entries) {
        if (!entry.geometry) {
            continue;
        }
        maxRank = (std::max)(maxRank, geometryDimensionRank(entry.geometry->getGeometryType()));
    }
    return maxRank;
}

static bool shouldRejectHighlyOverlappedMixedUnion(const std::vector<OverlayGeometryEntry>& entries) {
    if (entries.size() < 3) {
        return false;
    }

    return computeEnvelopeOverlapRatio(entries) >= 0.25;
}

static OGRGeometry* unionGeometryRange(std::vector<OGRGeometry*>& geoms, size_t begin, size_t end) {
    if (begin >= end) {
        return nullptr;
    }
    if (end - begin == 1) {
        return geoms[begin];
    }

    const size_t mid = begin + (end - begin) / 2;
    OGRGeometry* left = unionGeometryRange(geoms, begin, mid);
    OGRGeometry* right = unionGeometryRange(geoms, mid, end);
    if (!left) {
        return right;
    }
    if (!right) {
        return left;
    }

    OGRGeometry* merged = left->Union(right);
    delete left;
    delete right;
    return merged;
}

static OGRGeometry* unionGeometryList(std::vector<OGRGeometry*>& geoms) {
    if (geoms.empty()) {
        return nullptr;
    }
    return unionGeometryRange(geoms, 0, geoms.size());
}

struct SegmentedLinePart {
    OGRGeometry* geometry = nullptr;
    bool overlapsOverlay = false;
};

static std::vector<SegmentedLinePart> splitLineByOverlayEntries(
    const OGRGeometry* geom,
    const std::vector<const OverlayGeometryEntry*>& candidates) {
    std::vector<SegmentedLinePart> segments;
    if (!geom || geom->IsEmpty()) {
        return segments;
    }

    segments.push_back({geom->clone(), false});
    for (const auto* entry : candidates) {
        std::vector<SegmentedLinePart> nextSegments;
        for (auto& segment : segments) {
            if (!segment.geometry || segment.geometry->IsEmpty()) {
                delete segment.geometry;
                continue;
            }

            if (segment.overlapsOverlay || !segment.geometry->Intersects(entry->geometry.get())) {
                nextSegments.push_back(segment);
                continue;
            }

            std::vector<OGRGeometry*> outsideParts;
            collectLinearGeometryParts(segment.geometry->Difference(entry->geometry.get()), outsideParts);
            for (auto* part : outsideParts) {
                nextSegments.push_back({part, false});
            }

            std::vector<OGRGeometry*> insideParts;
            collectLinearGeometryParts(segment.geometry->Intersection(entry->geometry.get()), insideParts);
            for (auto* part : insideParts) {
                nextSegments.push_back({part, true});
            }

            delete segment.geometry;
        }
        segments = std::move(nextSegments);
    }

    return segments;
}

static OGRGeometry* buildMergedSegmentGeometry(
    const std::vector<SegmentedLinePart>& segments,
    bool overlapsOverlay) {
    std::vector<OGRGeometry*> matchedParts;
    for (const auto& segment : segments) {
        if (!segment.geometry || segment.geometry->IsEmpty() || segment.overlapsOverlay != overlapsOverlay) {
            continue;
        }
        matchedParts.push_back(segment.geometry->clone());
    }

    if (matchedParts.empty()) {
        return nullptr;
    }

    if (matchedParts.size() == 1) {
        return matchedParts[0];
    }

    auto* merged = new OGRMultiLineString();
    for (auto* part : matchedParts) {
        merged->addGeometry(part);
        delete part;
    }
    return merged;
}

#include "vector_plugin_info_filter.inc"

#include "vector_plugin_buffer_clip.inc"

#include "vector_plugin_raster_convert.inc"

#include "vector_plugin_overlay.inc"

#include "vector_plugin_dissolve.inc"

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::VectorPlugin)

