#include "vector_plugin.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace gis::plugins {

std::vector<gis::framework::ParamSpec> VectorPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"info", "filter", "buffer", "clip", "rasterize", "polygonize", "convert"}
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

    return gis::framework::Result::fail("Unknown action: " + action);
}

static GDALDataset* openVector(const std::string& path, bool readOnly = true) {
    return (GDALDataset*)GDALOpenEx(
        path.c_str(),
        readOnly ? GDAL_OF_READONLY : GDAL_OF_UPDATE,
        nullptr, nullptr, nullptr);
}

static OGRLayer* getLayer(GDALDataset* ds, const std::string& layerName) {
    if (!layerName.empty()) {
        OGRLayer* layer = ds->GetLayerByName(layerName.c_str());
        return layer;
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

static std::string getDriverExtension(const std::string& format) {
    if (format == "GeoJSON")        return ".geojson";
    if (format == "ESRI Shapefile") return ".shp";
    if (format == "GPKG")           return ".gpkg";
    if (format == "KML")            return ".kml";
    if (format == "CSV")            return ".csv";
    return "";
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

        OGRSpatialReference* srs = layer->GetSpatialRef();
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

    auto result = gis::framework::Result::ok(oss.str());
    result.metadata["driver"] = ds->GetDriver()->GetDescription();
    return result;
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
    if (!srcDS) {
        return gis::framework::Result::fail("Cannot open vector file: " + input);
    }

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    if (!srcLayer) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    if (!where.empty()) {
        srcLayer->SetAttributeFilter(where.c_str());
    }

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

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) {
                fs::remove(entry.path());
            }
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef();
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(), srcSRS, srcLayer->GetGeomType(), nullptr);
    if (!dstLayer) {
        GDALClose(dstDS);
        GDALClose(srcDS);
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
            OGRFeature::DestroyFeature(dstFeat);
            OGRFeature::DestroyFeature(feat);
            GDALClose(dstDS);
            GDALClose(srcDS);
            return gis::framework::Result::fail("Failed to write feature");
        }
        OGRFeature::DestroyFeature(dstFeat);
        OGRFeature::DestroyFeature(feat);
        count++;
    }

    GDALClose(dstDS);
    GDALClose(srcDS);
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Filter completed: " + std::to_string(count) + " features written", output);
}

gis::framework::Result VectorPlugin::doBuffer(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    double distance = gis::framework::getParam<double>(params, "distance", 100.0);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) {
        return gis::framework::Result::fail("Cannot open vector file: " + input);
    }

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    if (!srcLayer) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
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

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) {
                fs::remove(entry.path());
            }
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef();
    OGRLayer* dstLayer = dstDS->CreateLayer(
        (std::string(srcLayer->GetName()) + "_buffer").c_str(),
        srcSRS, wkbPolygon, nullptr);
    if (!dstLayer) {
        GDALClose(dstDS);
        GDALClose(srcDS);
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
    int total = static_cast<int>(srcLayer->GetFeatureCount(FALSE));
    if (total <= 0) total = 1;

    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (geom) {
            OGRGeometry* bufferGeom = geom->Buffer(distance);
            if (bufferGeom) {
                OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
                dstFeat->SetFrom(feat);
                dstFeat->SetGeometry(bufferGeom);
                if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
                    OGRFeature::DestroyFeature(dstFeat);
                    OGRFeature::DestroyFeature(feat);
                    delete bufferGeom;
                    GDALClose(dstDS);
                    GDALClose(srcDS);
                    return gis::framework::Result::fail("Failed to write buffer feature");
                }
                OGRFeature::DestroyFeature(dstFeat);
                delete bufferGeom;
            }
        }
        OGRFeature::DestroyFeature(feat);
        count++;
        if (count % 100 == 0) {
            progress.onProgress(0.3 + 0.6 * static_cast<double>(count) / total);
        }
    }

    GDALClose(dstDS);
    GDALClose(srcDS);
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Buffer completed: " + std::to_string(count) + " features processed", output);
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
    if (!srcDS) {
        return gis::framework::Result::fail("Cannot open input vector: " + input);
    }

    GDALDataset* clipDS = openVector(clipVector, true);
    if (!clipDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot open clip vector: " + clipVector);
    }

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    OGRLayer* clipLayer = clipDS->GetLayer(0);
    if (!srcLayer || !clipLayer) {
        GDALClose(clipDS);
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find required layers");
    }

    std::vector<OGRGeometry*> clipGeoms;
    clipLayer->ResetReading();
    OGRFeature* clipFeat;
    while ((clipFeat = clipLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = clipFeat->GetGeometryRef();
        if (geom) {
            clipGeoms.push_back(geom->clone());
        }
        OGRFeature::DestroyFeature(clipFeat);
    }
    GDALClose(clipDS);

    if (clipGeoms.empty()) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("No geometries found in clip vector");
    }

    OGRGeometry* clipUnion = clipGeoms[0];
    for (size_t i = 1; i < clipGeoms.size(); ++i) {
        OGRGeometry* newUnion = clipUnion->Union(clipGeoms[i]);
        delete clipUnion;
        clipUnion = newUnion;
        delete clipGeoms[i];
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
        delete clipUnion;
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) {
                fs::remove(entry.path());
            }
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        delete clipUnion;
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef();
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(), srcSRS, srcLayer->GetGeomType(), nullptr);
    if (!dstLayer) {
        GDALClose(dstDS);
        delete clipUnion;
        GDALClose(srcDS);
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
        OGRGeometry* geom = feat->GetGeometryRef();
        if (geom && geom->Intersects(clipUnion)) {
            OGRGeometry* clipped = geom->Intersection(clipUnion);
            if (clipped && !clipped->IsEmpty()) {
                OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
                dstFeat->SetFrom(feat);
                dstFeat->SetGeometry(clipped);
                dstLayer->CreateFeature(dstFeat);
                OGRFeature::DestroyFeature(dstFeat);
                count++;
            }
            delete clipped;
        }
        OGRFeature::DestroyFeature(feat);
    }

    delete clipUnion;
    GDALClose(dstDS);
    GDALClose(srcDS);
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
    if (!srcDS) {
        return gis::framework::Result::fail("Cannot open vector file: " + input);
    }

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
    adfGT[0] = extent.MinX;
    adfGT[1] = resolution;
    adfGT[2] = 0;
    adfGT[3] = extent.MaxY;
    adfGT[4] = 0;
    adfGT[5] = -resolution;
    dstDS->SetGeoTransform(adfGT);

    OGRSpatialReference* srs = layer->GetSpatialRef();
    if (srs) {
        char* wkt = nullptr;
        srs->exportToWkt(&wkt);
        if (wkt) {
            dstDS->SetProjection(wkt);
            CPLFree(wkt);
        }
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
        GDALClose(dstDS);
        GDALClose(srcDS);
        return gis::framework::Result::fail("Failed to create rasterize options");
    }

    int errCode = 0;
    GDALDatasetH dstHandle = GDALRasterize(output.c_str(),
        static_cast<GDALDatasetH>(dstDS),
        static_cast<GDALDatasetH>(srcDS),
        rasterizeOpts, &errCode);
    GDALRasterizeOptionsFree(rasterizeOpts);

    if (!dstHandle || errCode) {
        GDALClose(dstDS);
        GDALClose(srcDS);
        return gis::framework::Result::fail("Rasterize failed: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALClose(dstHandle);
    GDALClose(srcDS);
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
    if (!srcDS) {
        return gis::framework::Result::fail("Cannot open raster file: " + input);
    }

    GDALRasterBand* rasterBand = srcDS->GetRasterBand(band);
    if (!rasterBand) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    progress.onProgress(0.2);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) {
        return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) {
                fs::remove(entry.path());
            }
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srs = nullptr;
    const char* pszWKT = srcDS->GetProjectionRef();
    if (pszWKT && pszWKT[0] != '\0') {
        srs = new OGRSpatialReference();
        srs->importFromWkt(pszWKT);
    }

    OGRLayer* dstLayer = dstDS->CreateLayer("polygonized", srs, wkbPolygon, nullptr);
    if (!dstLayer) {
        GDALClose(dstDS);
        if (srs) delete srs;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    OGRFieldDefn fieldDefn("DN", OFTInteger);
    dstLayer->CreateField(&fieldDefn);

    progress.onProgress(0.3);

    GDALPolygonize(static_cast<GDALRasterBandH>(rasterBand),
        nullptr, static_cast<OGRLayerH>(dstLayer), 0, nullptr, nullptr);

    if (srs) delete srs;
    GDALClose(dstDS);
    progress.onProgress(1.0);

    GIntBig featureCount = dstLayer->GetFeatureCount(FALSE);
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
    if (!srcDS) {
        return gis::framework::Result::fail("Cannot open vector file: " + input);
    }

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
            if (entry.path().stem() == fs::path(output).stem()) {
                fs::remove(entry.path());
            }
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef();
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(), srcSRS, srcLayer->GetGeomType(), nullptr);
    if (!dstLayer) {
        GDALClose(dstDS);
        GDALClose(srcDS);
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
            OGRFeature::DestroyFeature(dstFeat);
            OGRFeature::DestroyFeature(feat);
            GDALClose(dstDS);
            GDALClose(srcDS);
            return gis::framework::Result::fail("Failed to write feature");
        }
        OGRFeature::DestroyFeature(dstFeat);
        OGRFeature::DestroyFeature(feat);
        count++;
    }

    GDALClose(dstDS);
    GDALClose(srcDS);
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Convert completed: " + std::to_string(count) + " features converted to " + format, output);
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::VectorPlugin)
