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

    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path tempDir = fs::temp_directory_path() / "gis_tool_shapefile_utf8" / std::to_string(timestamp);
    fs::create_directories(tempDir);

    const fs::path tempBase = tempDir / "input.shp";
    const std::vector<std::wstring> sidecarExts = {
        L".shp", L".shx", L".dbf", L".prj", L".cpg", L".qix", L".fix", L".sbn", L".sbx"
    };

    for (const auto& ext : sidecarExts) {
        fs::path src = sourceFsPath;
        src.replace_extension(ext);
        if (!fs::exists(src)) {
            continue;
        }

        fs::path dst = tempBase;
        dst.replace_extension(ext);
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
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
            "where", "属性过滤", "SQL WHERE条件表达式(如 population > 10000)",
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
            "resolution", "栅格分辨率", "矢量化转栅格的像元大小",
            gis::framework::ParamType::Double, false, double{1.0}
        },
        gis::framework::ParamSpec{
            "attribute", "属性字段", "栅格化时烧录的属性字段名(不指定则烧录值为1)",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "band", "波段序号", "栅格转矢量时使用的波段序号(从1开始)",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "format", "输出格式", "格式转换的目标格式(GeoJSON/ESRI Shapefile/GPKG/KML)",
            gis::framework::ParamType::Enum, false, std::string{"GeoJSON"},
            int{0}, int{0},
            {"GeoJSON", "ESRI Shapefile", "GPKG", "KML", "CSV"}
        },
        gis::framework::ParamSpec{
            "overlay_vector", "叠加矢量", "并集/差集操作的第二矢量文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "dissolve_field", "融合字段", "融合操作按此字段值合并相邻多边形(不指定则全部合并)",
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
        return ds->GetLayerByName(layerName.c_str());
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

static bool writeGeometryFeature(
    OGRLayer* layer,
    OGRGeometry* geom,
    const std::function<void(OGRFeature*)>& fillAttributes) {
    if (!layer || !geom || geom->IsEmpty()) {
        return true;
    }

    OGRFeature* dstFeat = OGRFeature::CreateFeature(layer->GetLayerDefn());
    std::unique_ptr<OGRGeometry> promoted = promoteGeometryToLayerType(layer, geom);

    dstFeat->SetGeometry(promoted ? promoted.get() : geom);
    if (fillAttributes) {
        fillAttributes(dstFeat);
    }
    const bool ok = layer->CreateFeature(dstFeat) == OGRERR_NONE;
    OGRFeature::DestroyFeature(dstFeat);
    return ok;
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

static std::vector<OGRGeometry*> splitLineByOverlayEntries(
    const OGRGeometry* geom,
    const std::vector<const OverlayGeometryEntry*>& candidates) {
    std::vector<OGRGeometry*> segments;
    if (!geom || geom->IsEmpty()) {
        return segments;
    }

    segments.push_back(geom->clone());
    for (const auto* entry : candidates) {
        std::vector<OGRGeometry*> nextSegments;
        for (auto* segment : segments) {
            if (!segment || segment->IsEmpty()) {
                delete segment;
                continue;
            }

            if (!segment->Intersects(entry->geometry.get())) {
                nextSegments.push_back(segment);
                continue;
            }

            collectLinearGeometryParts(segment->Difference(entry->geometry.get()), nextSegments);
            collectLinearGeometryParts(segment->Intersection(entry->geometry.get()), nextSegments);
            delete segment;
        }
        segments = std::move(nextSegments);
    }

    return segments;
}

gis::framework::Result VectorPlugin::doInfo(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");

    progress.onProgress(0.1);

    GDALDataset* ds = openVector(input, true);
    if (!ds) {
        return gis::framework::Result::fail("Cannot open vector file: " + input);
    }

    std::ostringstream oss;
    oss << "Vector Info: " << input << "\n";
    oss << "  Driver: " << ds->GetDriver()->GetDescription() << "\n";
    oss << "  Layers: " << ds->GetLayerCount() << "\n";

    int layerIdx = 0;
    while (auto* layer = ds->GetLayer(layerIdx)) {
        if (!layerName.empty() && layerName != layer->GetName()) {
            layerIdx++;
            continue;
        }

        oss << "\n  Layer [" << layerIdx << "]: " << layer->GetName() << "\n";

        OGRwkbGeometryType geomType = layer->GetGeomType();
        oss << "    Geometry: " << geometryTypeName(geomType) << "\n";

        OGREnvelope extent;
        if (layer->GetExtent(&extent, TRUE) == OGRERR_NONE) {
            oss << "    Extent:  " << extent.MinX << ", " << extent.MinY
                << " - " << extent.MaxX << ", " << extent.MaxY << "\n";
        }

        const OGRSpatialReference* srs = layer->GetSpatialRef();
        if (srs) {
            const char* authName = srs->GetAuthorityName(nullptr);
            const char* authCode = srs->GetAuthorityCode(nullptr);
            if (authName && authCode) {
                oss << "    CRS:     " << authName << ":" << authCode << "\n";
            } else {
                char* wkt = nullptr;
                srs->exportToWkt(&wkt);
                if (wkt) {
                    std::string wktStr(wkt);
                    if (wktStr.length() > 80) wktStr = wktStr.substr(0, 77) + "...";
                    oss << "    CRS:     " << wktStr << "\n";
                    CPLFree(wkt);
                }
            }
        } else {
            oss << "    CRS:     (none)\n";
        }

        GIntBig featureCount = layer->GetFeatureCount(FALSE);
        oss << "    Features: " << featureCount << "\n";

        auto* layerDefn = layer->GetLayerDefn();
        int fieldCount = layerDefn->GetFieldCount();
        oss << "    Fields:  " << fieldCount << "\n";
        for (int i = 0; i < fieldCount; ++i) {
            auto* fieldDefn = layerDefn->GetFieldDefn(i);
            oss << "      [" << i << "] " << fieldDefn->GetNameRef()
                << " (" << OGRFieldDefn::GetFieldTypeName(fieldDefn->GetType()) << ")\n";
        }

        layerIdx++;
    }

    GDALClose(ds);
    progress.onProgress(1.0);

    return gis::framework::Result::ok(oss.str());
}

gis::framework::Result VectorPlugin::doFilter(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string where = gis::framework::getParam<std::string>(params, "where", "");
    auto extentArr = gis::framework::getParam<std::array<double,4>>(params, "extent", {0,0,0,0});

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (where.empty() && extentArr[0] == 0 && extentArr[1] == 0 &&
        extentArr[2] == 0 && extentArr[3] == 0) {
        return gis::framework::Result::fail("At least one of --where or --extent must be specified");
    }

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    if (!srcLayer) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    if (!where.empty()) srcLayer->SetAttributeFilter(where.c_str());

    bool hasExtent = extentArr[0] != 0 || extentArr[1] != 0 ||
                     extentArr[2] != 0 || extentArr[3] != 0;
    if (hasExtent) {
        srcLayer->SetSpatialFilterRect(extentArr[0], extentArr[1], extentArr[2], extentArr[3]);
    }

    progress.onProgress(0.3);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";
    else if (ext == ".kml") outFormat = "KML";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    removeExistingVectorOutput(output, outFormat);

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(), srcSRS, resultLayerGeometryType(srcLayer->GetGeomType()), nullptr);
    if (!dstLayer) {
        GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    progress.onProgress(0.4);

    srcLayer->ResetReading();
    OGRFeature* feat;
    int count = 0;
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
        dstFeat->SetFrom(feat);
        if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
            GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
            return gis::framework::Result::fail("Failed to write feature");
        }
        OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
        count++;
    }

    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Filter completed: " + std::to_string(count) + " features written", output);
}

gis::framework::Result VectorPlugin::doBuffer(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const auto startedAt = std::chrono::steady_clock::now();

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    double distance = gis::framework::getParam<double>(params, "distance", 100.0);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    if (!srcLayer) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    const OGRSpatialReference* srcSRSRef = srcLayer->GetSpatialRef();
    const std::string srsType = detectSrsType(srcSRSRef);
    if (srsType == "geographic") {
        GDALClose(srcDS);
        return gis::framework::Result::fail(
            "Buffer requires a projected coordinate system. Please reproject the input layer before buffering.");
    }

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";
    else if (ext == ".kml") outFormat = "KML";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    removeExistingVectorOutput(output, outFormat);

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcSRSRef ? srcSRSRef->Clone() : nullptr;
    OGRLayer* dstLayer = dstDS->CreateLayer(
        (std::string(srcLayer->GetName()) + "_buffer").c_str(), srcSRS, wkbPolygon, nullptr);
    if (!dstLayer) {
        GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    progress.onProgress(0.3);

    srcLayer->ResetReading();
    auto* dstLayerDefn = dstLayer->GetLayerDefn();
    OGRFeature* feat;
    int count = 0;
    int total = static_cast<int>(srcLayer->GetFeatureCount(FALSE));
    if (total <= 0) total = 1;

    const bool useTransactions = outFormat == "GPKG";
    if (useTransactions) {
        dstLayer->StartTransaction();
    }

    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (geom) {
            OGRGeometry* bufferGeom = geom->Buffer(distance);
            if (bufferGeom) {
                OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayerDefn);
                dstFeat->SetFrom(feat);
                dstFeat->SetGeometry(bufferGeom);
                if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
                    OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete bufferGeom; GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
                    return gis::framework::Result::fail("Failed to write buffer feature");
                }
                OGRFeature::DestroyFeature(dstFeat);
                delete bufferGeom;
            }
        }
        OGRFeature::DestroyFeature(feat);
        count++;
        if (useTransactions && count % 1000 == 0) {
            dstLayer->CommitTransaction();
            dstLayer->StartTransaction();
        }
        if (count % 100 == 0) progress.onProgress(0.3 + 0.6 * static_cast<double>(count) / total);
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
    }
    dstLayer->SyncToDisk();
    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Buffer completed: " + std::to_string(count) + " features processed", output);
    result.metadata["feature_count"] = std::to_string(count);
    {
        std::ostringstream oss;
        oss << distance;
        result.metadata["distance"] = oss.str();
    }
    result.metadata["srs_type"] = srsType;
    result.metadata["output_format"] = outFormat;
    result.metadata["elapsed_ms"] = std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt).count());
    return result;
}

gis::framework::Result VectorPlugin::doClip(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string clipVector = gis::framework::getParam<std::string>(params, "clip_vector", "");

    if (input.empty())       return gis::framework::Result::fail("input is required");
    if (output.empty())      return gis::framework::Result::fail("output is required");
    if (clipVector.empty())  return gis::framework::Result::fail("clip_vector is required for clip operation");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open input vector: " + input);

    GDALDataset* clipDS = openVector(clipVector, true);
    if (!clipDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot open clip vector: " + clipVector);
    }

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    OGRLayer* clipLayer = clipDS->GetLayer(0);
    if (!srcLayer || !clipLayer) {
        GDALClose(clipDS); GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), clipLayer->GetSpatialRef())) {
        GDALClose(clipDS); GDALClose(srcDS);
        return failCrsMismatch("Clip");
    }

    auto clipEntries = collectNormalizedGeometries(clipLayer);
    GDALClose(clipDS);

    if (clipEntries.empty()) {
        GDALClose(srcDS);
        return failInvalidGeometry("Clip");
    }

    std::unique_ptr<OGRGeometry> mergedClipGeometry;
    if (clipEntries.size() <= 128) {
        mergedClipGeometry.reset(buildMergedOverlayGeometry(clipEntries));
    }

    progress.onProgress(0.3);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";
    else if (ext == ".kml") outFormat = "KML";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    removeExistingVectorOutput(output, outFormat);

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(), srcSRS, resultLayerGeometryType(srcLayer->GetGeomType()), nullptr);
    if (!dstLayer) {
        GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    progress.onProgress(0.4);

    srcLayer->ResetReading();
    OGRFeature* feat;
    int count = 0;
    const bool useTransactions = outFormat == "GPKG";
    if (useTransactions) {
        dstLayer->StartTransaction();
    }
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        OGRGeometry* overlayGeometry = mergedClipGeometry.get();
        std::unique_ptr<OGRGeometry> candidateOverlay;
        if (!overlayGeometry) {
            candidateOverlay.reset(buildCandidateOverlayGeometry(clipEntries, geom));
            overlayGeometry = candidateOverlay.get();
        }
        if (geom && overlayGeometry && geom->Intersects(overlayGeometry)) {
            OGRGeometry* clipped = overlayGeometry->Contains(geom)
                ? geom->clone()
                : geom->Intersection(overlayGeometry);
            if (clipped && !clipped->IsEmpty()) {
                OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
                dstFeat->SetFrom(feat);
                std::unique_ptr<OGRGeometry> promoted = promoteGeometryToLayerType(dstLayer, clipped);
                dstFeat->SetGeometry(promoted ? promoted.get() : clipped);
                dstLayer->CreateFeature(dstFeat);
                OGRFeature::DestroyFeature(dstFeat);
                count++;
                if (useTransactions && count % 1000 == 0) {
                    dstLayer->CommitTransaction();
                    dstLayer->StartTransaction();
                }
            }
            delete clipped;
        }
        OGRFeature::DestroyFeature(feat);
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
    }
    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Clip completed: " + std::to_string(count) + " features clipped", output);
}

gis::framework::Result VectorPlugin::doRasterize(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    double resolution = gis::framework::getParam<double>(params, "resolution", 1.0);
    std::string attribute = gis::framework::getParam<std::string>(params, "attribute", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (resolution <= 0) return gis::framework::Result::fail("resolution must be positive");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* layer = getLayer(srcDS, layerName);
    if (!layer) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    OGREnvelope extent;
    if (layer->GetExtent(&extent, TRUE) != OGRERR_NONE) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot get layer extent");
    }

    int xSize = static_cast<int>(std::ceil((extent.MaxX - extent.MinX) / resolution));
    int ySize = static_cast<int>(std::ceil((extent.MaxY - extent.MinY) / resolution));
    if (xSize <= 0 || ySize <= 0) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Invalid raster dimensions");
    }

    progress.onProgress(0.2);

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    GDALDataset* dstDS = drv->Create(output.c_str(), xSize, ySize, 1, GDT_Float32, nullptr);
    if (!dstDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output raster: " + output);
    }

    double adfGT[6];
    adfGT[0] = extent.MinX; adfGT[1] = resolution; adfGT[2] = 0;
    adfGT[3] = extent.MaxY; adfGT[4] = 0;          adfGT[5] = -resolution;
    dstDS->SetGeoTransform(adfGT);

    const OGRSpatialReference* srs = layer->GetSpatialRef();
    if (srs) {
        char* wkt = nullptr;
        srs->exportToWkt(&wkt);
        if (wkt) { dstDS->SetProjection(wkt); CPLFree(wkt); }
    }

    GDALRasterBand* band = dstDS->GetRasterBand(1);
    band->Fill(0);
    band->SetNoDataValue(0);

    progress.onProgress(0.3);

    std::vector<std::string> argStorage;
    if (!attribute.empty()) {
        argStorage.push_back("-a");
        argStorage.push_back(attribute);
    } else {
        argStorage.push_back("-burn");
        argStorage.push_back("1");
    }

    std::vector<const char*> rasterizeArgs;
    for (auto& a : argStorage) rasterizeArgs.push_back(a.c_str());
    rasterizeArgs.push_back(nullptr);

    GDALRasterizeOptions* rasterizeOpts = GDALRasterizeOptionsNew(
        const_cast<char**>(rasterizeArgs.data()), nullptr);
    if (!rasterizeOpts) {
        GDALClose(dstDS); GDALClose(srcDS);
        return gis::framework::Result::fail("Failed to create rasterize options");
    }

    int errCode = 0;
    GDALDatasetH dstHandle = GDALRasterize(output.c_str(),
        static_cast<GDALDatasetH>(dstDS),
        static_cast<GDALDatasetH>(srcDS),
        rasterizeOpts, &errCode);
    GDALRasterizeOptionsFree(rasterizeOpts);

    if (!dstHandle || errCode) {
        GDALClose(dstDS); GDALClose(srcDS);
        return gis::framework::Result::fail("Rasterize failed: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALClose(dstHandle); GDALClose(srcDS);
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Rasterize completed: " + std::to_string(xSize) + "x" + std::to_string(ySize) + " raster", output);
}

gis::framework::Result VectorPlugin::doPolygonize(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);

    auto srcDS = gis::core::openRaster(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open raster file: " + input);

    GDALRasterBand* rasterBand = srcDS->GetRasterBand(band);
    if (!rasterBand) return gis::framework::Result::fail("Cannot get band " + std::to_string(band));

    progress.onProgress(0.2);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) fs::remove(entry.path());
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) return gis::framework::Result::fail("Cannot create output file: " + output);

    OGRSpatialReference* srs = nullptr;
    const char* pszWKT = srcDS->GetProjectionRef();
    if (pszWKT && pszWKT[0] != '\0') {
        srs = new OGRSpatialReference();
        srs->importFromWkt(pszWKT);
    }

    OGRLayer* dstLayer = dstDS->CreateLayer("polygonized", srs, wkbPolygon, nullptr);
    if (!dstLayer) {
        GDALClose(dstDS); if (srs) delete srs;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    OGRFieldDefn fieldDefn("DN", OFTInteger);
    dstLayer->CreateField(&fieldDefn);

    progress.onProgress(0.3);

    GDALPolygonize(GDALRasterBand::ToHandle(rasterBand),
        nullptr, OGRLayer::ToHandle(dstLayer), 0, nullptr, nullptr, nullptr);

    GIntBig featureCount = dstLayer->GetFeatureCount(FALSE);

    if (srs) delete srs;
    GDALClose(dstDS);
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Polygonize completed: " + std::to_string(featureCount) + " polygons", output);
}

gis::framework::Result VectorPlugin::doConvert(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string format = gis::framework::getParam<std::string>(params, "format", "GeoJSON");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    if (!srcLayer) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    GDALDriver* drv = getDriverForFormat(format);
    if (!drv) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Unsupported format: " + format);
    }

    namespace fs = std::filesystem;
    if (format == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) fs::remove(entry.path());
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    OGRLayer* dstLayer = dstDS->CreateLayer(srcLayer->GetName(), srcSRS, srcLayer->GetGeomType(), nullptr);
    if (!dstLayer) {
        GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    progress.onProgress(0.3);

    srcLayer->ResetReading();
    OGRFeature* feat;
    int count = 0;
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
        dstFeat->SetFrom(feat);
        if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
            GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
            return gis::framework::Result::fail("Failed to write feature");
        }
        OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
        count++;
    }

    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Convert completed: " + std::to_string(count) + " features converted to " + format, output);
}

gis::framework::Result VectorPlugin::doUnion(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string overlayVector = gis::framework::getParam<std::string>(params, "overlay_vector", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (overlayVector.empty()) return gis::framework::Result::fail("overlay_vector is required for union");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open input: " + input);

    GDALDataset* overlayDS = openVector(overlayVector, true);
    if (!overlayDS) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot open overlay: " + overlayVector); }

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    OGRLayer* overlayLayer = overlayDS->GetLayer(0);
    if (!srcLayer || !overlayLayer) {
        GDALClose(overlayDS); GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), overlayLayer->GetSpatialRef())) {
        GDALClose(overlayDS); GDALClose(srcDS);
        return failCrsMismatch("Union");
    }

    auto overlayEntries = collectNormalizedGeometries(overlayLayer);
    const bool usePracticalMixedUnion =
        isLinearGeometryType(srcLayer->GetGeomType()) && detectOverlayGeometryDimensionRank(overlayEntries) == 2;
    GDALClose(overlayDS);

    if (overlayEntries.empty()) {
        GDALClose(srcDS);
        return failInvalidGeometry("Union");
    }

    if (usePracticalMixedUnion && shouldRejectHighlyOverlappedMixedUnion(overlayEntries)) {
        GDALClose(srcDS);
        return gis::framework::Result::fail(
            "Union detected heavily overlapped polygon overlays for mixed-dimension input. "
            "Please dissolve or simplify the overlay polygons first, or use clip/difference instead.");
    }

    progress.onProgress(0.3);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot get driver"); }

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) fs::remove(entry.path());
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot create output"); }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    const auto outputGeomType = usePracticalMixedUnion
        ? resultLayerGeometryType(srcLayer->GetGeomType())
        : wkbUnknown;
    OGRLayer* dstLayer = dstDS->CreateLayer("union", srcSRS, outputGeomType, nullptr);
    if (!dstLayer) {
        GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    OGRFieldDefn fieldDefn("source", OFTString);
    dstLayer->CreateField(&fieldDefn);

    progress.onProgress(0.4);

    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    int count = 0;
    const bool useTransactions = outFormat == "GPKG";
    if (useTransactions) {
        dstLayer->StartTransaction();
    }
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (usePracticalMixedUnion && geom) {
            const auto candidates = collectCandidateOverlayEntries(overlayEntries, geom);
            std::vector<OGRGeometry*> lineParts = splitLineByOverlayEntries(geom, candidates);

            for (size_t i = 0; i < lineParts.size(); ++i) {
                OGRGeometry* part = lineParts[i];
                if (!writeUnionFeature(dstLayer, part, "split")) {
                    for (size_t j = i; j < lineParts.size(); ++j) {
                        delete lineParts[j];
                    }
                    OGRFeature::DestroyFeature(feat);
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
                    return gis::framework::Result::fail("Failed to write union feature");
                }
                delete part;
                lineParts[i] = nullptr;
                count++;
                if (useTransactions && count % 1000 == 0) {
                    dstLayer->CommitTransaction();
                    dstLayer->StartTransaction();
                }
            }
        } else {
            std::unique_ptr<OGRGeometry> candidateOverlay(buildCandidateOverlayGeometry(overlayEntries, geom));
            if (geom && candidateOverlay && geom->Intersects(candidateOverlay.get())) {
            OGRGeometry* unionGeom = geom->Union(candidateOverlay.get());
            if (unionGeom && !unionGeom->IsEmpty()) {
                if (!writeUnionFeature(dstLayer, unionGeom, "union")) {
                    OGRFeature::DestroyFeature(feat);
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete unionGeom;
                    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
                    return gis::framework::Result::fail("Failed to write union feature");
                }
                count++;
                if (useTransactions && count % 1000 == 0) {
                    dstLayer->CommitTransaction();
                    dstLayer->StartTransaction();
                }
            }
            delete unionGeom;
            } else if (geom) {
                OGRGeometry* cloned = geom->clone();
                if (!writeUnionFeature(dstLayer, cloned, "input")) {
                    OGRFeature::DestroyFeature(feat);
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete cloned;
                    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
                    return gis::framework::Result::fail("Failed to write union feature");
                }
                delete cloned;
                count++;
                if (useTransactions && count % 1000 == 0) {
                    dstLayer->CommitTransaction();
                    dstLayer->StartTransaction();
                }
            }
        }
        OGRFeature::DestroyFeature(feat);
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
    }
    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Union completed: " + std::to_string(count) + " features", output);
}

gis::framework::Result VectorPlugin::doDifference(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string overlayVector = gis::framework::getParam<std::string>(params, "overlay_vector", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (overlayVector.empty()) return gis::framework::Result::fail("overlay_vector is required for difference");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open input: " + input);

    GDALDataset* overlayDS = openVector(overlayVector, true);
    if (!overlayDS) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot open overlay: " + overlayVector); }

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    OGRLayer* overlayLayer = overlayDS->GetLayer(0);
    if (!srcLayer || !overlayLayer) {
        GDALClose(overlayDS); GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), overlayLayer->GetSpatialRef())) {
        GDALClose(overlayDS); GDALClose(srcDS);
        return failCrsMismatch("Difference");
    }

    auto overlayEntries = collectNormalizedGeometries(overlayLayer);
    GDALClose(overlayDS);

    if (overlayEntries.empty()) {
        GDALClose(srcDS);
        return failInvalidGeometry("Difference");
    }

    std::unique_ptr<OGRGeometry> mergedOverlayGeometry;
    if (overlayEntries.size() <= 128) {
        mergedOverlayGeometry.reset(buildMergedOverlayGeometry(overlayEntries));
    }

    progress.onProgress(0.3);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot get driver"); }

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) fs::remove(entry.path());
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot create output"); }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    OGRLayer* dstLayer = dstDS->CreateLayer(
        "difference", srcSRS, resultLayerGeometryType(srcLayer->GetGeomType()), nullptr);
    if (!dstLayer) {
        GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    progress.onProgress(0.4);

    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    int count = 0;
    const bool useTransactions = outFormat == "GPKG";
    if (useTransactions) {
        dstLayer->StartTransaction();
    }
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (geom) {
            OGRGeometry* overlayGeometry = mergedOverlayGeometry.get();
            std::unique_ptr<OGRGeometry> candidateOverlay;
            if (!overlayGeometry) {
                candidateOverlay.reset(buildCandidateOverlayGeometry(overlayEntries, geom));
                overlayGeometry = candidateOverlay.get();
            }
            OGRGeometry* diffGeom = nullptr;
            if (!overlayGeometry || !geom->Intersects(overlayGeometry)) {
                diffGeom = geom->clone();
            } else if (!overlayGeometry->Contains(geom)) {
                diffGeom = geom->Difference(overlayGeometry);
            }
            if (diffGeom && !diffGeom->IsEmpty()) {
                OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
                dstFeat->SetFrom(feat);
                std::unique_ptr<OGRGeometry> promoted = promoteGeometryToLayerType(dstLayer, diffGeom);
                dstFeat->SetGeometry(promoted ? promoted.get() : diffGeom);
                dstLayer->CreateFeature(dstFeat);
                OGRFeature::DestroyFeature(dstFeat);
                count++;
                if (useTransactions && count % 1000 == 0) {
                    dstLayer->CommitTransaction();
                    dstLayer->StartTransaction();
                }
            }
            delete diffGeom;
        }
        OGRFeature::DestroyFeature(feat);
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
    }
    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Difference completed: " + std::to_string(count) + " features", output);
}

gis::framework::Result VectorPlugin::doDissolve(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string dissolveField = gis::framework::getParam<std::string>(params, "dissolve_field", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    if (!srcLayer) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot find layer"); }

    const OGRSpatialReference* srcSRSRef = srcLayer->GetSpatialRef();
    if (detectSrsType(srcSRSRef) == "geographic") {
        GDALClose(srcDS);
        return failProjectedRequired("Dissolve");
    }

    progress.onProgress(0.2);

    std::map<std::string, std::vector<OGRGeometry*>> groups;
    std::vector<OGRGeometry*> allGeoms;

    srcLayer->ResetReading();
    OGRFeature* feat;
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = cloneNormalizedGeometry(feat->GetGeometryRef());
        if (!geom) { OGRFeature::DestroyFeature(feat); continue; }

        if (!dissolveField.empty()) {
            int fieldIdx = srcLayer->GetLayerDefn()->GetFieldIndex(dissolveField.c_str());
            if (fieldIdx >= 0) {
                std::string val = feat->GetFieldAsString(fieldIdx);
                groups[val].push_back(geom);
            } else {
                allGeoms.push_back(geom);
            }
        } else {
            allGeoms.push_back(geom);
        }
        OGRFeature::DestroyFeature(feat);
    }

    progress.onProgress(0.4);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot get driver"); }

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) fs::remove(entry.path());
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot create output"); }

    OGRSpatialReference* srcSRS = srcSRSRef ? srcSRSRef->Clone() : nullptr;
    OGRLayer* dstLayer = dstDS->CreateLayer("dissolved", srcSRS, wkbMultiPolygon, nullptr);
    if (!dstLayer) {
        GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    if (!dissolveField.empty()) {
        OGRFieldDefn fieldDefn(dissolveField.c_str(), OFTString);
        dstLayer->CreateField(&fieldDefn);
    }

    progress.onProgress(0.5);

    int count = 0;
    const bool useTransactions = outFormat == "GPKG";
    if (useTransactions) {
        dstLayer->StartTransaction();
    }

    if (!dissolveField.empty()) {
        for (auto& [key, geoms] : groups) {
            if (geoms.empty()) continue;

            OGRGeometry* result = unionGeometryList(geoms);

            if (result && !result->IsEmpty()) {
                if (!writeGeometryFeature(dstLayer, result, [&](OGRFeature* feature) {
                        feature->SetField(dissolveField.c_str(), key.c_str());
                    })) {
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete result;
                    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
                    return gis::framework::Result::fail("Failed to write dissolve feature");
                }
                count++;
            }
            delete result;
        }
    } else {
        if (!allGeoms.empty()) {
            OGRGeometry* result = unionGeometryList(allGeoms);

            if (result && !result->IsEmpty()) {
                if (!writeGeometryFeature(dstLayer, result)) {
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete result;
                    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
                    return gis::framework::Result::fail("Failed to write dissolve feature");
                }
                count++;
            }
            delete result;
        }
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
    }
    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Dissolve completed: " + std::to_string(count) + " features", output);
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::VectorPlugin)
