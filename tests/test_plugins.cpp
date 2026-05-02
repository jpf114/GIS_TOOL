#include <gtest/gtest.h>
#include <gis/framework/plugin.h>
#include <gis/framework/plugin_manager.h>
#include <gis/framework/param_spec.h>
#include <gis/framework/result.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>
#include <gis/core/progress.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_error.h>
#include <opencv2/opencv.hpp>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "test_support.h"

namespace fs = std::filesystem;

static fs::path getTestDir() {
    return gis::tests::defaultTestOutputDir("test_e2e_output");
}

static std::string utf8PathString(const fs::path& path) {
    return path.u8string();
}

static fs::path getPluginDir() {
    return gis::tests::testPluginDir();
}

class PluginTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        gis::core::initGDAL();
        gis::tests::ensureDirectory(getTestDir());
    }

    void SetUp() override {
        mgr_.loadFromDirectory(getPluginDir().string());
    }

    gis::framework::PluginManager mgr_;
    gis::core::CliProgress progress_;
};

namespace {

std::mutex g_gdalWarningMutex;
std::vector<std::string> g_gdalWarnings;
std::vector<std::string> g_gdalErrors;

void captureGdalWarning(CPLErr errClass, CPLErrorNum errNo, const char* msg) {
    if (msg && errClass == CE_Warning) {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        g_gdalWarnings.emplace_back(msg);
    }
    if (msg && errClass >= CE_Failure) {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        g_gdalErrors.emplace_back(msg);
    }
    CPLDefaultErrorHandler(errClass, errNo, msg);
}

} // namespace

static std::string createTestRaster(const std::string& name, int w = 100, int h = 100,
                                     int bands = 1, GDALDataType dt = GDT_Float32) {
    std::string path = (getTestDir() / name).string();
    auto ds = gis::core::createRaster(path, w, h, bands, dt);
    double adfGT[6] = {116.0, 0.001, 0.0, 40.0, 0.0, -0.001};
    ds->SetGeoTransform(adfGT);
    for (int b = 1; b <= bands; ++b) {
        auto* band = ds->GetRasterBand(b);
        if (dt == GDT_Float32) {
            std::vector<float> data(w * h);
            for (int i = 0; i < w * h; ++i) data[i] = static_cast<float>(i % 256);
            band->RasterIO(GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0, 0);
        } else if (dt == GDT_Byte) {
            std::vector<uint8_t> data(w * h);
            for (int i = 0; i < w * h; ++i) data[i] = static_cast<uint8_t>(i % 256);
            band->RasterIO(GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Byte, 0, 0);
        }
        band->FlushCache();
    }
    return path;
}

static std::string createPatternRaster(const std::string& name, int w = 96, int h = 96) {
    std::string path = (getTestDir() / name).string();
    auto ds = gis::core::createRaster(path, w, h, 1, GDT_Byte);
    double adfGT[6] = {116.0, 0.001, 0.0, 40.0, 0.0, -0.001};
    ds->SetGeoTransform(adfGT);

    std::vector<uint8_t> data(w * h, 0);
    for (int y = 8; y < h - 8; ++y) {
        for (int x = 8; x < w - 8; ++x) {
            if ((x >= 18 && x <= 34 && y >= 18 && y <= 34) ||
                (x >= 54 && x <= 78 && y >= 20 && y <= 28) ||
                (x >= 52 && x <= 60 && y >= 20 && y <= 70) ||
                ((x % 12) <= 2 && (y % 12) <= 2 && x >= 12 && y >= 12) ||
                ((x + 2 * y) % 19 == 0 && x >= 10 && y >= 10) ||
                (x == y) ||
                (x + y == w - 1)) {
                data[y * w + x] = 255;
            }
        }
    }

    auto* band = ds->GetRasterBand(1);
    band->RasterIO(GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Byte, 0, 0);
    band->FlushCache();
    return path;
}

static std::string createConstantRaster(const std::string& name,
                                        int w,
                                        int h,
                                        float value,
                                        double originX = 116.0,
                                        double originY = 40.0,
                                        double pixelSize = 0.001) {
    std::string path = (getTestDir() / name).string();
    auto ds = gis::core::createRaster(path, w, h, 1, GDT_Float32);
    double adfGT[6] = {originX, pixelSize, 0.0, originY, 0.0, -pixelSize};
    ds->SetGeoTransform(adfGT);

    std::vector<float> data(w * h, value);
    auto* band = ds->GetRasterBand(1);
    band->RasterIO(GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0, 0);
    band->FlushCache();
    return path;
}

static std::string createMultiBandConstantRaster(const std::string& name,
                                                 int w,
                                                 int h,
                                                 const std::vector<float>& values,
                                                 double originX = 116.0,
                                                 double originY = 40.0,
                                                 double pixelSize = 0.001) {
    std::string path = (getTestDir() / name).string();
    auto ds = gis::core::createRaster(path, w, h, static_cast<int>(values.size()), GDT_Float32);
    double adfGT[6] = {originX, pixelSize, 0.0, originY, 0.0, -pixelSize};
    ds->SetGeoTransform(adfGT);

    for (size_t bandIndex = 0; bandIndex < values.size(); ++bandIndex) {
        std::vector<float> data(w * h, values[bandIndex]);
        auto* band = ds->GetRasterBand(static_cast<int>(bandIndex) + 1);
        band->RasterIO(GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0, 0);
        band->FlushCache();
    }

    return path;
}

static std::string createTerrainRaster(const std::string& name, int w = 48, int h = 48) {
    std::string path = (getTestDir() / name).string();
    auto ds = gis::core::createRaster(path, w, h, 1, GDT_Float32);
    double adfGT[6] = {116.0, 0.001, 0.0, 40.0, 0.0, -0.001};
    ds->SetGeoTransform(adfGT);

    std::vector<float> data(w * h, 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            data[y * w + x] = static_cast<float>(x * 1.5 + y * 0.8 + ((x * y) % 11) * 0.2);
        }
    }

    auto* band = ds->GetRasterBand(1);
    band->RasterIO(GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0, 0);
    band->FlushCache();
    return path;
}

static std::string createSinkRaster(const std::string& name, int w = 9, int h = 9) {
    std::string path = (getTestDir() / name).string();
    auto ds = gis::core::createRaster(path, w, h, 1, GDT_Float32);
    double adfGT[6] = {116.0, 0.001, 0.0, 40.0, 0.0, -0.001};
    ds->SetGeoTransform(adfGT);

    std::vector<float> data(w * h, 10.0f);
    data[(h / 2) * w + (w / 2)] = 2.0f;

    auto* band = ds->GetRasterBand(1);
    band->RasterIO(GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0, 0);
    band->FlushCache();
    return path;
}

static std::string createEastDownhillRaster(const std::string& name, int w = 9, int h = 9) {
    std::string path = (getTestDir() / name).string();
    auto ds = gis::core::createRaster(path, w, h, 1, GDT_Float32);
    double adfGT[6] = {116.0, 0.001, 0.0, 40.0, 0.0, -0.001};
    ds->SetGeoTransform(adfGT);

    std::vector<float> data(w * h, 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            data[y * w + x] = static_cast<float>(w - x);
        }
    }

    auto* band = ds->GetRasterBand(1);
    band->RasterIO(GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0, 0);
    band->FlushCache();
    return path;
}

static std::string createProjectedClassRaster(const std::string& name,
                                              int w,
                                              int h,
                                              int value,
                                              int epsg,
                                              double originX,
                                              double originY,
                                              double pixelSize) {
    std::string path = (getTestDir() / name).string();
    auto ds = gis::core::createRaster(path, w, h, 1, GDT_Int16);
    double adfGT[6] = {originX, pixelSize, 0.0, originY, 0.0, -pixelSize};
    ds->SetGeoTransform(adfGT);

    OGRSpatialReference srs;
    EXPECT_EQ(srs.importFromEPSG(epsg), OGRERR_NONE);
    char* wkt = nullptr;
    EXPECT_EQ(srs.exportToWkt(&wkt), OGRERR_NONE);
    EXPECT_NE(wkt, nullptr);
    if (wkt) {
        ds->SetProjection(wkt);
        CPLFree(wkt);
    }

    std::vector<int16_t> data(w * h, static_cast<int16_t>(value));
    auto* band = ds->GetRasterBand(1);
    EXPECT_NE(band, nullptr);
    if (band) {
        EXPECT_EQ(band->RasterIO(GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Int16, 0, 0), CE_None);
        band->SetNoDataValue(0);
        band->FlushCache();
    }
    return path;
}

static std::string createPolygonVectorDataset(const std::string& name, int epsg) {
    const std::string path = utf8PathString(getTestDir() / name);
    auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    EXPECT_NE(driver, nullptr);
    auto* ds = driver->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    EXPECT_NE(ds, nullptr);

    OGRSpatialReference srs;
    EXPECT_EQ(srs.importFromEPSG(epsg), OGRERR_NONE);
    auto* layer = ds->CreateLayer("features", &srs, wkbPolygon, nullptr);
    EXPECT_NE(layer, nullptr);

    OGRFieldDefn idField("id", OFTString);
    OGRFieldDefn nameField("name", OFTString);
    EXPECT_EQ(layer->CreateField(&idField), OGRERR_NONE);
    EXPECT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

    auto* feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
    EXPECT_NE(feature, nullptr);
    feature->SetField("id", "f1");
    feature->SetField("name", "area1");

    OGRPolygon polygon;
    OGRLinearRing ring;
    ring.addPoint(0, 0);
    ring.addPoint(0, 100);
    ring.addPoint(100, 100);
    ring.addPoint(100, 0);
    ring.addPoint(0, 0);
    polygon.addRing(&ring);
    feature->SetGeometry(&polygon);

    EXPECT_EQ(layer->CreateFeature(feature), OGRERR_NONE);
    OGRFeature::DestroyFeature(feature);
    GDALClose(ds);
    return path;
}

static std::string createClassMapJson(const std::string& name) {
    const std::string path = utf8PathString(getTestDir() / name);
    std::ofstream ofs(path, std::ios::binary);
    ofs << "{\n"
        << "  \"1\": \"class1\",\n"
        << "  \"2\": \"class2\"\n"
        << "}\n";
    return path;
}

static void fillIntRasterRect(const std::string& path,
                              int xOff,
                              int yOff,
                              int width,
                              int height,
                              int16_t value) {
    auto ds = gis::core::openRaster(path, false);
    ASSERT_NE(ds, nullptr);

    std::vector<int16_t> data(width * height, value);
    auto* band = ds->GetRasterBand(1);
    ASSERT_NE(band, nullptr);
    ASSERT_EQ(band->RasterIO(GF_Write, xOff, yOff, width, height,
        data.data(), width, height, GDT_Int16, 0, 0), CE_None);
    band->FlushCache();
}

static std::string readTextFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

static void fillRasterRect(const std::string& path,
                           int xOff,
                           int yOff,
                           int width,
                           int height,
                           float value) {
    auto ds = gis::core::openRaster(path, false);
    ASSERT_NE(ds, nullptr);

    std::vector<float> data(width * height, value);
    auto* band = ds->GetRasterBand(1);
    ASSERT_NE(band, nullptr);
    ASSERT_EQ(band->RasterIO(GF_Write, xOff, yOff, width, height,
        data.data(), width, height, GDT_Float32, 0, 0), CE_None);
    band->FlushCache();
}

static float readRasterPixel(const std::string& path, int x, int y) {
    auto ds = gis::core::openRaster(path, true);
    EXPECT_NE(ds, nullptr);
    auto* band = ds->GetRasterBand(1);
    EXPECT_NE(band, nullptr);

    float value = 0.0f;
    EXPECT_EQ(band->RasterIO(GF_Read, x, y, 1, 1, &value, 1, 1, GDT_Float32, 0, 0), CE_None);
    return value;
}

TEST_F(PluginTest, PluginManagerScan) {
    EXPECT_GE(mgr_.plugins().size(), 4u);
}

TEST_F(PluginTest, PluginManagerFind) {
    auto* p = mgr_.find("projection");
    if (p) {
        EXPECT_EQ(p->name(), "projection");
        EXPECT_FALSE(p->paramSpecs().empty());
    }
}

TEST_F(PluginTest, ProjectionPluginParams) {
    auto* p = mgr_.find("projection");
    if (p) {
        auto specs = p->paramSpecs();
        bool hasAction = false;
        for (auto& s : specs) {
            if (s.key == "action") hasAction = true;
        }
        EXPECT_TRUE(hasAction);
    }
}

TEST_F(PluginTest, ProjectionVectorReprojectExecution) {
    auto* p = mgr_.find("projection");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "projection_vector_input.geojson");
    const std::string output = utf8PathString(getTestDir() / "projection_vector_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GeoJSON");
        ASSERT_NE(driver, nullptr);

        GDALDataset* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);

        OGRSpatialReference srs;
        ASSERT_EQ(srs.SetFromUserInput("EPSG:4326"), OGRERR_NONE);

        OGRLayer* layer = ds->CreateLayer("input", &srs, wkbLineString, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        OGRFeature* feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
        ASSERT_NE(feature, nullptr);
        feature->SetField("name", "road");

        auto geometry = std::unique_ptr<OGRGeometry>(
            OGRGeometryFactory::createGeometry(wkbLineString));
        auto* line = geometry->toLineString();
        line->addPoint(116.38, 39.90);
        line->addPoint(116.42, 39.92);
        feature->SetGeometry(geometry.get());

        ASSERT_EQ(layer->CreateFeature(feature), OGRERR_NONE);
        OGRFeature::DestroyFeature(feature);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("reproject");
    params["input"] = input;
    params["output"] = output;
    params["dst_srs"] = std::string("EPSG:3857");

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(),
        GDAL_OF_READONLY | GDAL_OF_VECTOR,
        nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);

    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(), 1);

    const OGRSpatialReference* outSrs = outLayer->GetSpatialRef();
    ASSERT_NE(outSrs, nullptr);
    EXPECT_TRUE(outSrs->IsProjected());

    OGREnvelope extent;
    ASSERT_EQ(outLayer->GetExtent(&extent, TRUE), OGRERR_NONE);
    EXPECT_GT(std::abs(extent.MaxX), 1000.0);
    EXPECT_GT(std::abs(extent.MaxY), 1000.0);

    GDALClose(outDs);
}

TEST_F(PluginTest, VectorPluginParams) {
    auto* p = mgr_.find("vector");
    if (p) {
        auto specs = p->paramSpecs();
        EXPECT_GE(specs.size(), 5u);
    }
}

TEST_F(PluginTest, RasterRenderPluginParams) {
    auto* p = mgr_.find("raster_render");
    if (p) {
        auto specs = p->paramSpecs();
        bool hasAction = false;
        for (auto& s : specs) {
            if (s.key == "action") hasAction = true;
        }
        EXPECT_TRUE(hasAction);
    }
}

TEST_F(PluginTest, FeatureStatsPluginParams) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    const auto specs = p->paramSpecs();
    bool hasRasters = false;
    bool hasTargetEpsg = false;
    bool hasVectorOutput = false;
    bool hasRasterOutput = false;
    for (const auto& spec : specs) {
        if (spec.key == "rasters") {
            hasRasters = true;
        }
        if (spec.key == "target_epsg") {
            hasTargetEpsg = true;
        }
        if (spec.key == "vector_output") {
            hasVectorOutput = true;
        }
        if (spec.key == "raster_output") {
            hasRasterOutput = true;
        }
    }

    EXPECT_TRUE(hasRasters);
    EXPECT_TRUE(hasTargetEpsg);
    EXPECT_TRUE(hasVectorOutput);
    EXPECT_TRUE(hasRasterOutput);
}

TEST_F(PluginTest, ProcessingPluginParams) {
    auto* p = mgr_.find("processing");
    if (p) {
        auto specs = p->paramSpecs();
        bool hasEdgeAction = false;
        for (auto& s : specs) {
            if (s.key == "edge_method") hasEdgeAction = true;
        }
        EXPECT_TRUE(hasEdgeAction);
    }
}

TEST_F(PluginTest, FeatureStatsRunResolvesProjectedTargetSrsAndFinestGrid) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    const std::string vectorPath = createPolygonVectorDataset("feature_stats_projected_vector.gpkg", 3857);
    const std::string classMapPath = createClassMapJson("feature_stats_projected_class_map.json");
    const std::string raster1 = createProjectedClassRaster(
        "feature_stats_projected_raster_30m.tif", 10, 10, 1, 3857, 0.0, 100.0, 30.0);
    const std::string raster2 = createProjectedClassRaster(
        "feature_stats_projected_raster_10m.tif", 10, 10, 2, 3857, 0.0, 100.0, 10.0);
    const std::string output = utf8PathString(getTestDir() / "feature_stats_projected_result.json");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("feature_stats");
    params["vector"] = vectorPath;
    params["feature_id_field"] = std::string("id");
    params["feature_name_field"] = std::string("name");
    params["class_map"] = classMapPath;
    params["rasters"] = raster1 + "," + raster2;
    params["nodatas"] = std::string("0,0");
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("resolved_target_epsg"), "3857");
    EXPECT_EQ(result.metadata.at("feature_count"), "1");
    EXPECT_NEAR(std::stod(result.metadata.at("grid_pixel_width")), 10.0, 1e-6);
    EXPECT_NEAR(std::stod(result.metadata.at("grid_pixel_height")), 10.0, 1e-6);

    const std::string content = readTextFile(output);
    EXPECT_NE(content.find("\"class_value\": 1"), std::string::npos);
    EXPECT_NE(content.find("\"pixel_count\": 100"), std::string::npos);
}

TEST_F(PluginTest, FeatureStatsRunRejectsGeographicInputsWithoutTargetEpsg) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    const std::string vectorPath = createPolygonVectorDataset("feature_stats_geo_vector.gpkg", 4326);
    const std::string classMapPath = createClassMapJson("feature_stats_geo_class_map.json");
    const std::string raster = createProjectedClassRaster(
        "feature_stats_geo_raster.tif", 10, 10, 1, 4326, 110.0, 40.0, 0.01);
    const std::string output = utf8PathString(getTestDir() / "feature_stats_geo_result.json");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("feature_stats");
    params["vector"] = vectorPath;
    params["class_map"] = classMapPath;
    params["rasters"] = raster;
    params["nodatas"] = std::string("0");
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("必须显式提供 target_epsg"), std::string::npos);
}

TEST_F(PluginTest, FeatureStatsRunWritesPriorityStatisticsJson) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    const std::string vectorPath = createPolygonVectorDataset("feature_stats_priority_vector.gpkg", 3857);
    const std::string classMapPath = createClassMapJson("feature_stats_priority_class_map.json");
    const std::string highPriority = createProjectedClassRaster(
        "feature_stats_priority_high.tif", 10, 10, 0, 3857, 0.0, 100.0, 10.0);
    const std::string lowPriority = createProjectedClassRaster(
        "feature_stats_priority_low.tif", 10, 10, 2, 3857, 0.0, 100.0, 10.0);
    const std::string output = utf8PathString(getTestDir() / "feature_stats_priority_result.json");

    fillIntRasterRect(highPriority, 0, 0, 4, 4, 1);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("feature_stats");
    params["vector"] = vectorPath;
    params["feature_id_field"] = std::string("id");
    params["feature_name_field"] = std::string("name");
    params["class_map"] = classMapPath;
    params["rasters"] = highPriority + "," + lowPriority;
    params["nodatas"] = std::string("0,0");
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("record_count"), "4");

    const std::string content = readTextFile(output);
    EXPECT_NE(content.find("\"class_value\": 1"), std::string::npos);
    EXPECT_NE(content.find("\"class_value\": 2"), std::string::npos);
    EXPECT_NE(content.find("\"pixel_count\": 16"), std::string::npos);
    EXPECT_NE(content.find("\"pixel_count\": 84"), std::string::npos);
    EXPECT_NE(content.find("\"feature_id\": \"__summary__\""), std::string::npos);
}

TEST_F(PluginTest, FeatureStatsRunWritesVectorAndRasterOutputs) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    const std::string vectorPath = createPolygonVectorDataset("feature_stats_output_vector.gpkg", 3857);
    const std::string classMapPath = createClassMapJson("feature_stats_output_class_map.json");
    const std::string highPriority = createProjectedClassRaster(
        "feature_stats_output_high.tif", 10, 10, 0, 3857, 0.0, 100.0, 10.0);
    const std::string lowPriority = createProjectedClassRaster(
        "feature_stats_output_low.tif", 10, 10, 2, 3857, 0.0, 100.0, 10.0);
    const std::string output = utf8PathString(getTestDir() / "feature_stats_output_result.json");
    const std::string vectorOutput = utf8PathString(getTestDir() / "feature_stats_output_classes.gpkg");
    const std::string rasterOutput = utf8PathString(getTestDir() / "feature_stats_output_classes.tif");

    fillIntRasterRect(highPriority, 0, 0, 4, 4, 1);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("feature_stats");
    params["vector"] = vectorPath;
    params["feature_id_field"] = std::string("id");
    params["feature_name_field"] = std::string("name");
    params["class_map"] = classMapPath;
    params["rasters"] = highPriority + "," + lowPriority;
    params["nodatas"] = std::string("0,0");
    params["output"] = output;
    params["vector_output"] = vectorOutput;
    params["raster_output"] = rasterOutput;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(vectorOutput));
    EXPECT_TRUE(fs::exists(rasterOutput));
    EXPECT_EQ(result.metadata.at("vector_output"), vectorOutput);
    EXPECT_EQ(result.metadata.at("raster_output"), rasterOutput);

    GDALDataset* vectorDs = static_cast<GDALDataset*>(GDALOpenEx(
        vectorOutput.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(vectorDs, nullptr);
    auto* layer = vectorDs->GetLayer(0);
    ASSERT_NE(layer, nullptr);
    EXPECT_GE(layer->GetFeatureCount(), 2);

    const int featureIdIndex = layer->GetLayerDefn()->GetFieldIndex("feature_id");
    const int classValueIndex = layer->GetLayerDefn()->GetFieldIndex("class_value");
    const int classNameIndex = layer->GetLayerDefn()->GetFieldIndex("class_name");
    const int pixelCountIndex = layer->GetLayerDefn()->GetFieldIndex("pixel_count");
    const int areaIndex = layer->GetLayerDefn()->GetFieldIndex("area");
    ASSERT_GE(featureIdIndex, 0);
    ASSERT_GE(classValueIndex, 0);
    ASSERT_GE(classNameIndex, 0);
    ASSERT_GE(pixelCountIndex, 0);
    ASSERT_GE(areaIndex, 0);

    std::map<int, long long> pixelCountsByClass;
    layer->ResetReading();
    OGRFeature* feature = nullptr;
    while ((feature = layer->GetNextFeature()) != nullptr) {
        EXPECT_STREQ(feature->GetFieldAsString(featureIdIndex), "f1");
        const int classValue = feature->GetFieldAsInteger(classValueIndex);
        const long long pixelCount = feature->GetFieldAsInteger64(pixelCountIndex);
        pixelCountsByClass[classValue] += pixelCount;
        EXPECT_GT(feature->GetFieldAsDouble(areaIndex), 0.0);
        EXPECT_FALSE(std::string(feature->GetFieldAsString(classNameIndex)).empty());
        OGRFeature::DestroyFeature(feature);
    }

    EXPECT_EQ(pixelCountsByClass[1], 16);
    EXPECT_EQ(pixelCountsByClass[2], 84);
    GDALClose(vectorDs);

    auto rasterDs = gis::core::openRaster(rasterOutput, false);
    ASSERT_NE(rasterDs, nullptr);
    EXPECT_EQ(rasterDs->GetRasterXSize(), 10);
    EXPECT_EQ(rasterDs->GetRasterYSize(), 10);

    auto* band = rasterDs->GetRasterBand(1);
    ASSERT_NE(band, nullptr);
    std::vector<int> rasterValues(100, 0);
    ASSERT_EQ(band->RasterIO(GF_Read, 0, 0, 10, 10, rasterValues.data(), 10, 10, GDT_Int32, 0, 0, nullptr), CE_None);

    int class1Count = 0;
    int class2Count = 0;
    for (int value : rasterValues) {
        if (value == 1) {
            ++class1Count;
        } else if (value == 2) {
            ++class2Count;
        }
    }

    EXPECT_EQ(class1Count, 16);
    EXPECT_EQ(class2Count, 84);
}

TEST_F(PluginTest, AllPluginsHaveActions) {
    for (auto& plugin : mgr_.plugins()) {
        auto specs = plugin->paramSpecs();
        bool hasAction = false;
        for (auto& s : specs) {
            if (s.key == "action" && s.type == gis::framework::ParamType::Enum) {
                hasAction = true;
                EXPECT_FALSE(s.enumValues.empty()) << "Plugin " << plugin->name() << " has empty action enum";
            }
        }
        EXPECT_TRUE(hasAction) << "Plugin " << plugin->name() << " missing action param";
    }
}

// ── End-to-End Execution Tests ──

TEST_F(PluginTest, ProcessingStatsExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createTestRaster("e2e_stats_input.tif", 50, 50);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("stats");
    params["input"] = input;
    params["band"] = 1;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Stats failed: " << result.message;
    EXPECT_FALSE(result.message.empty());
    EXPECT_TRUE(result.metadata.count("min") > 0);
    EXPECT_TRUE(result.metadata.count("max") > 0);
    EXPECT_TRUE(result.metadata.count("mean") > 0);
}

TEST_F(PluginTest, ProcessingThresholdExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createTestRaster("e2e_thresh_input.tif", 50, 50);
    std::string output = (getTestDir() / "e2e_thresh_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("threshold");
    params["input"] = input;
    params["output"] = output;
    params["method"] = std::string("binary");
    params["threshold_value"] = 128.0;
    params["band"] = 1;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Threshold failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
}

TEST_F(PluginTest, ProcessingFilterExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createTestRaster("e2e_filter_input.tif", 50, 50);
    std::string output = (getTestDir() / "e2e_filter_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("filter");
    params["input"] = input;
    params["output"] = output;
    params["filter_type"] = std::string("gaussian");
    params["band"] = 1;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Filter failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
}

TEST_F(PluginTest, ProcessingEdgeExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createTestRaster("e2e_edge_input.tif", 50, 50);
    std::string output = (getTestDir() / "e2e_edge_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("edge");
    params["input"] = input;
    params["output"] = output;
    params["edge_method"] = std::string("canny");
    params["band"] = 1;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Edge detection failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
}

TEST_F(PluginTest, ProcessingEnhanceExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createTestRaster("e2e_enhance_input.tif", 50, 50);
    std::string output = (getTestDir() / "e2e_enhance_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("enhance");
    params["input"] = input;
    params["output"] = output;
    params["enhance_type"] = std::string("equalize");
    params["band"] = 1;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Enhance failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
}

TEST_F(PluginTest, ProcessingSkeletonExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createPatternRaster("e2e_skeleton_input.tif");
    std::string output = (getTestDir() / "e2e_skeleton_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("skeleton");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Skeleton failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_GT(readRasterPixel(output, 48, 48), 0.0f);
}

TEST_F(PluginTest, ProcessingConnectedComponentsExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createPatternRaster("e2e_connected_components_input.tif");
    std::string output = (getTestDir() / "e2e_connected_components_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("connected_components");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Connected components failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_GT(std::stoi(result.metadata.at("component_count")), 1);
    EXPECT_GT(readRasterPixel(output, 26, 26), 0.0f);
}

TEST_F(PluginTest, ProcessingContourExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createTestRaster("e2e_contour_input.tif", 50, 50);
    std::string output = (getTestDir() / "e2e_contour_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("contour");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Contour failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
}

TEST_F(PluginTest, ProcessingTemplateMatchExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    const std::string input = createPatternRaster("e2e_template_match_input.tif");
    const std::string templatePath = utf8PathString(getTestDir() / "e2e_template_match_template.tif");
    const std::string output = utf8PathString(getTestDir() / "e2e_template_match_output.tif");

    auto inputDs = gis::core::openRaster(input, true);
    ASSERT_NE(inputDs, nullptr);
    cv::Mat src = gis::core::gdalBandToMat(inputDs.get(), 1);
    cv::Mat tpl = src(cv::Rect(18, 18, 16, 16)).clone();
    gis::core::matToGdalTiff(tpl, input, templatePath, 1);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("template_match");
    params["input"] = input;
    params["template_file"] = templatePath;
    params["output"] = output;
    params["match_method"] = std::string("ccoeff_normed");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    ASSERT_TRUE(result.metadata.count("match_x"));
    ASSERT_TRUE(result.metadata.count("match_y"));
}

TEST_F(PluginTest, ProcessingPansharpenExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    const std::string input = createMultiBandConstantRaster(
        "e2e_pansharpen_ms_input.tif", 30, 30, {30.0f, 50.0f, 70.0f});
    const std::string pan = createConstantRaster("e2e_pansharpen_pan_input.tif", 30, 30, 120.0f);
    const std::string output = utf8PathString(getTestDir() / "e2e_pansharpen_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("pansharpen");
    params["input"] = input;
    params["pan_file"] = pan;
    params["output"] = output;
    params["pan_method"] = std::string("brovey");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    auto ds = gis::core::openRaster(output, true);
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->GetRasterCount(), 3);
}

TEST_F(PluginTest, ProcessingMissingInput) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("stats");

    auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
}

TEST_F(PluginTest, ProcessingUnknownAction) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("nonexistent_action");
    params["input"] = std::string("dummy.tif");

    auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
}

TEST_F(PluginTest, RasterInspectInfoExecution) {
    auto* p = mgr_.find("raster_inspect");
    if (!p) GTEST_SKIP() << "raster_inspect plugin not loaded";

    std::string input = createTestRaster("e2e_util_info_input.tif", 30, 30);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("info");
    params["input"] = input;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Raster inspect info failed: " << result.message;
}

TEST_F(PluginTest, RasterManageOverviewsExecution) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("e2e_util_overviews_input.tif", 128, 128);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("overviews");
    params["input"] = input;
    params["levels"] = std::string("2 4");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;

    auto ds = gis::core::openRaster(input, true);
    ASSERT_NE(ds, nullptr);
    EXPECT_GE(ds->GetRasterBand(1)->GetOverviewCount(), 1);
}

TEST_F(PluginTest, RasterManageNoDataExecution) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("e2e_util_nodata_input.tif", 32, 32);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("nodata");
    params["input"] = input;
    params["band"] = 1;
    params["nodata_value"] = -99.0;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;

    auto ds = gis::core::openRaster(input, true);
    ASSERT_NE(ds, nullptr);
    bool hasNoData = false;
    const double noDataValue = gis::core::getNoDataValue(ds.get(), 1, &hasNoData);
    EXPECT_TRUE(hasNoData);
    EXPECT_DOUBLE_EQ(noDataValue, -99.0);
}

TEST_F(PluginTest, RasterInspectHistogramExecution) {
    auto* p = mgr_.find("raster_inspect");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("e2e_util_hist_input.tif", 32, 32);
    const std::string output = utf8PathString(getTestDir() / "e2e_util_hist_output.json");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("histogram");
    params["input"] = input;
    params["output"] = output;
    params["bins"] = 16;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("bins"), "16");
}

TEST_F(PluginTest, RasterRenderColormapExecution) {
    auto* p = mgr_.find("raster_render");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("e2e_util_colormap_input.tif", 40, 40);
    const std::string output = utf8PathString(getTestDir() / "e2e_util_colormap_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("colormap");
    params["input"] = input;
    params["output"] = output;
    params["cmap"] = std::string("jet");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    auto ds = gis::core::openRaster(output, true);
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->GetRasterCount(), 3);
}

TEST_F(PluginTest, SpindexNdviExecution) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);

    const std::string input = createMultiBandConstantRaster(
        "e2e_spindex_ndvi_input.tif", 24, 24, {10.0f, 20.0f, 30.0f, 70.0f});
    const std::string output = utf8PathString(getTestDir() / "e2e_spindex_ndvi_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("ndvi");
    params["input"] = input;
    params["output"] = output;
    params["red_band"] = 3;
    params["nir_band"] = 4;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_TRUE(result.metadata.count("ndvi_min") > 0);
    EXPECT_TRUE(result.metadata.count("ndvi_max") > 0);
}

TEST_F(PluginTest, SpindexOtherIndicesExecution) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);

    const std::string input = createMultiBandConstantRaster(
        "e2e_spindex_multi_input.tif", 24, 24, {10.0f, 20.0f, 30.0f, 70.0f, 90.0f, 110.0f});

    struct CaseSpec {
        std::string action;
        std::string outputName;
        std::vector<std::pair<std::string, gis::framework::ParamValue>> extraParams;
        float expectedValue;
    };

    const std::vector<CaseSpec> cases = {
        {"gndvi", "e2e_spindex_gndvi_output.tif", {{"green_band", 2}, {"nir_band", 4}}, 50.0f / 90.0f},
        {"ndwi", "e2e_spindex_ndwi_output.tif", {{"green_band", 2}, {"nir_band", 4}}, -50.0f / 90.0f},
        {"mndwi", "e2e_spindex_mndwi_output.tif", {{"green_band", 2}, {"swir1_band", 5}}, -70.0f / 110.0f},
        {"ndbi", "e2e_spindex_ndbi_output.tif", {{"swir1_band", 5}, {"nir_band", 4}}, 20.0f / 160.0f},
        {"arvi", "e2e_spindex_arvi_output.tif", {{"blue_band", 1}, {"red_band", 3}, {"nir_band", 4}}, 20.0f / 120.0f},
        {"nbr", "e2e_spindex_nbr_output.tif", {{"nir_band", 4}, {"swir2_band", 6}}, -40.0f / 180.0f},
        {"awei", "e2e_spindex_awei_output.tif", {{"green_band", 2}, {"nir_band", 4}, {"swir1_band", 5}, {"swir2_band", 6}}, -600.0f},
        {"ui", "e2e_spindex_ui_output.tif", {{"nir_band", 4}, {"swir2_band", 6}}, 40.0f / 180.0f},
        {"bi", "e2e_spindex_bi_output.tif", {{"red_band", 3}, {"nir_band", 4}}, 1.0f / 5785.6136f},
        {"savi", "e2e_spindex_savi_output.tif", {{"red_band", 3}, {"nir_band", 4}, {"l_value", 0.5}}, 60.0f / 100.5f},
        {"evi", "e2e_spindex_evi_output.tif", {{"blue_band", 1}, {"red_band", 3}, {"nir_band", 4}, {"g_value", 2.5}, {"c1", 6.0}, {"c2", 7.5}, {"l_value", 1.0}}, 100.0f / 176.0f},
    };

    for (const auto& testCase : cases) {
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = testCase.action;
        params["input"] = input;
        const std::string output = utf8PathString(getTestDir() / testCase.outputName);
        params["output"] = output;
        for (const auto& [key, value] : testCase.extraParams) {
            params[key] = value;
        }

        const auto result = p->execute(params, progress_);
        EXPECT_TRUE(result.success) << testCase.action << ": " << result.message;
        EXPECT_TRUE(fs::exists(output)) << testCase.action;
        EXPECT_NEAR(readRasterPixel(output, 5, 5), testCase.expectedValue, 1e-4f) << testCase.action;
    }
}

TEST_F(PluginTest, SpindexCustomIndexExecution) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);

    const std::string input = createMultiBandConstantRaster(
        "e2e_spindex_custom_index_input.tif", 24, 24, {10.0f, 20.0f, 30.0f, 70.0f});
    const std::string output = utf8PathString(getTestDir() / "e2e_spindex_custom_index_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("custom_index");
    params["input"] = input;
    params["output"] = output;
    params["expression"] = std::string("(B4-B1)/(B4+B1)");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("expression"), "(B4-B1)/(B4+B1)");
    EXPECT_NEAR(readRasterPixel(output, 5, 5), 60.0f / 80.0f, 1e-4f);
}

TEST_F(PluginTest, SpindexCustomIndexAliasExecution) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);

    const std::string input = createMultiBandConstantRaster(
        "e2e_spindex_custom_index_alias_input.tif", 24, 24, {10.0f, 20.0f, 30.0f, 70.0f, 90.0f});
    const std::string output = utf8PathString(getTestDir() / "e2e_spindex_custom_index_alias_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("custom_index");
    params["input"] = input;
    params["output"] = output;
    params["expression"] = std::string("(NIR-RED)/(NIR+RED) + (SWIR1-GREEN)/(SWIR1+GREEN)");
    params["green_band"] = 2;
    params["red_band"] = 3;
    params["nir_band"] = 4;
    params["swir1_band"] = 5;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("nir_band"), "4");
    const float expected = (40.0f / 100.0f) + (70.0f / 110.0f);
    EXPECT_NEAR(readRasterPixel(output, 5, 5), expected, 1e-4f);
}

TEST_F(PluginTest, SpindexCustomIndexPresetExecution) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);

    const std::string input = createMultiBandConstantRaster(
        "e2e_spindex_custom_index_preset_input.tif", 24, 24, {10.0f, 20.0f, 30.0f, 70.0f});
    const std::string output = utf8PathString(getTestDir() / "e2e_spindex_custom_index_preset_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("custom_index");
    params["input"] = input;
    params["output"] = output;
    params["preset"] = std::string("ndvi_alias");
    params["red_band"] = 3;
    params["nir_band"] = 4;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("preset"), "ndvi_alias");
    EXPECT_EQ(result.metadata.at("expression"), "(NIR-RED)/(NIR+RED)");
    EXPECT_NEAR(readRasterPixel(output, 5, 5), 40.0f / 100.0f, 1e-4f);
}

TEST_F(PluginTest, ProjectionInfoExecution) {
    auto* p = mgr_.find("projection");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("projection_info_input.tif", 20, 10);
    {
        auto ds = gis::core::openRaster(input, false);
        ASSERT_NE(ds, nullptr);
        OGRSpatialReference srs;
        ASSERT_EQ(srs.SetFromUserInput("EPSG:4326"), OGRERR_NONE);
        char* wkt = nullptr;
        ASSERT_EQ(srs.exportToWkt(&wkt), OGRERR_NONE);
        ASSERT_NE(wkt, nullptr);
        ASSERT_EQ(ds->SetProjection(wkt), CE_None);
        CPLFree(wkt);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("info");
    params["input"] = input;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.metadata.at("width"), "20");
    EXPECT_EQ(result.metadata.at("height"), "10");
    EXPECT_EQ(result.metadata.at("bands"), "1");
}

TEST_F(PluginTest, ProjectionTransformExecution) {
    auto* p = mgr_.find("projection");
    ASSERT_NE(p, nullptr);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("transform");
    params["src_srs"] = std::string("EPSG:4326");
    params["dst_srs"] = std::string("EPSG:3857");
    params["x"] = 116.0;
    params["y"] = 40.0;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.metadata.count("dst_x"));
    ASSERT_TRUE(result.metadata.count("dst_y"));
    EXPECT_GT(std::abs(std::stod(result.metadata.at("dst_x"))), 1000.0);
    EXPECT_GT(std::abs(std::stod(result.metadata.at("dst_y"))), 1000.0);
}

TEST_F(PluginTest, ProjectionAssignSrsExecution) {
    auto* p = mgr_.find("projection");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("projection_assign_input.tif", 16, 16);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("assign_srs");
    params["input"] = input;
    params["srs"] = std::string("EPSG:3857");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;

    auto ds = gis::core::openRaster(input, true);
    ASSERT_NE(ds, nullptr);
    EXPECT_FALSE(gis::core::getSRSWKT(ds.get()).empty());
}

TEST_F(PluginTest, VectorInfoExecution) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string shpPath = (getTestDir() / "e2e_vector_test.shp").string();
    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(shpPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(4326);
        auto* layer = ds->CreateLayer("test", srs.get(), wkbPoint);
        ASSERT_NE(layer, nullptr);
        auto* featDefn = layer->GetLayerDefn();
        for (int i = 0; i < 5; ++i) {
            auto* feat = OGRFeature::CreateFeature(featDefn);
            OGRPoint pt(116.0 + i * 0.01, 40.0 + i * 0.01);
            feat->SetGeometry(&pt);
            layer->CreateFeature(feat);
            OGRFeature::DestroyFeature(feat);
        }
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("info");
    params["input"] = shpPath;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Vector info failed: " << result.message;
}

TEST_F(PluginTest, VectorBufferExecutionReportsMetadata) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string shpPath = (getTestDir() / "e2e_buffer_input.shp").string();
    std::string output = (getTestDir() / "e2e_buffer_output.gpkg").string();
    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(shpPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("buffer_test", srs.get(), wkbLineString);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn fieldDefn("name", OFTString);
        ASSERT_EQ(layer->CreateField(&fieldDefn), OGRERR_NONE);

        auto* featDefn = layer->GetLayerDefn();
        for (int i = 0; i < 200; ++i) {
            auto* feat = OGRFeature::CreateFeature(featDefn);
            OGRLineString line;
            line.addPoint(12940000 + i * 20, 4852000);
            line.addPoint(12940010 + i * 20, 4852100);
            line.addPoint(12940030 + i * 20, 4852200);
            feat->SetGeometry(&line);
            feat->SetField("name", "road");
            ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
            OGRFeature::DestroyFeature(feat);
        }
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("buffer");
    params["input"] = shpPath;
    params["output"] = output;
    params["distance"] = 25.0;

    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        g_gdalWarnings.clear();
        g_gdalErrors.clear();
    }

    CPLPushErrorHandler(captureGdalWarning);
    auto result = p->execute(params, progress_);
    CPLPopErrorHandler();

    EXPECT_TRUE(result.success) << "Buffer failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "200");
    EXPECT_EQ(result.metadata.at("distance"), "25");
    EXPECT_EQ(result.metadata.at("srs_type"), "projected");
    EXPECT_EQ(result.metadata.at("output_format"), "GPKG");
    EXPECT_TRUE(result.metadata.count("elapsed_ms") > 0);

    bool hasMissingExtensionsError = false;
    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        for (const auto& error : g_gdalErrors) {
            if (error.find("gpkg_extensions") != std::string::npos) {
                hasMissingExtensionsError = true;
                break;
            }
        }
    }
    EXPECT_FALSE(hasMissingExtensionsError);

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "road");
    OGRFeature::DestroyFeature(outFeat);

    GDALClose(outDs);
}

TEST_F(PluginTest, VectorBufferRejectsGeographicSrs) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string shpPath = (getTestDir() / "e2e_buffer_geo_input.shp").string();
    std::string output = (getTestDir() / "e2e_buffer_geo_output.geojson").string();
    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(shpPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(4326);
        auto* layer = ds->CreateLayer("buffer_geo_test", srs.get(), wkbPoint);
        ASSERT_NE(layer, nullptr);

        auto* featDefn = layer->GetLayerDefn();
        auto* feat = OGRFeature::CreateFeature(featDefn);
        OGRPoint pt(116.4, 39.9);
        feat->SetGeometry(&pt);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("buffer");
    params["input"] = shpPath;
    params["output"] = output;
    params["distance"] = 100.0;

    auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("projected"), std::string::npos);
}

TEST_F(PluginTest, VectorBufferAvoidsMultiPolygonWarning) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_buffer_multi_input.gpkg").string();
    std::string output = (getTestDir() / "e2e_buffer_multi_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("buffer_multi_test", srs.get(), wkbMultiLineString, nullptr);
        ASSERT_NE(layer, nullptr);

        auto* featDefn = layer->GetLayerDefn();
        auto* feat = OGRFeature::CreateFeature(featDefn);
        OGRMultiLineString multiLine;
        OGRLineString line1;
        line1.addPoint(0, 0);
        line1.addPoint(10, 0);
        OGRLineString line2;
        line2.addPoint(100, 0);
        line2.addPoint(110, 0);
        multiLine.addGeometry(&line1);
        multiLine.addGeometry(&line2);
        feat->SetGeometry(&multiLine);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("buffer");
    params["input"] = input;
    params["output"] = output;
    params["distance"] = 20.0;

    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        g_gdalWarnings.clear();
        g_gdalErrors.clear();
    }

    CPLPushErrorHandler(captureGdalWarning);
    auto result = p->execute(params, progress_);
    CPLPopErrorHandler();

    EXPECT_TRUE(result.success) << "Buffer failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    bool hasMultiPolygonWarning = false;
    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        for (const auto& warning : g_gdalWarnings) {
            if (warning.find("geometry type POLYGON") != std::string::npos &&
                warning.find("MULTIPOLYGON") != std::string::npos) {
                hasMultiPolygonWarning = true;
                break;
            }
        }
    }
    EXPECT_FALSE(hasMultiPolygonWarning);

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(wkbFlatten(outLayer->GetGeomType()), wkbMultiPolygon);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    OGRGeometry* outGeom = outFeat->GetGeometryRef();
    ASSERT_NE(outGeom, nullptr);
    EXPECT_EQ(wkbFlatten(outGeom->getGeometryType()), wkbMultiPolygon);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, ProcessingHoughExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createTestRaster("e2e_hough_input.tif", 50, 50);
    std::string output = (getTestDir() / "e2e_hough_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("hough");
    params["input"] = input;
    params["output"] = output;
    params["hough_type"] = std::string("lines");
    params["band"] = 1;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Hough failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_TRUE(result.metadata.count("detect_count") > 0);
}

TEST_F(PluginTest, ProcessingWatershedExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createTestRaster("e2e_watershed_input.tif", 50, 50);
    std::string output = (getTestDir() / "e2e_watershed_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("watershed");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Watershed failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_TRUE(result.metadata.count("segment_count") > 0);
}

TEST_F(PluginTest, ProcessingKMeansExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createTestRaster("e2e_kmeans_input.tif", 30, 30, 3);
    std::string output = (getTestDir() / "e2e_kmeans_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("kmeans");
    params["input"] = input;
    params["output"] = output;
    params["k"] = 3;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "KMeans failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_TRUE(result.metadata.count("compactness") > 0);
}

TEST_F(PluginTest, VectorDissolveExecution) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string shpPath = (getTestDir() / "e2e_dissolve_input.shp").string();
    std::string output = (getTestDir() / "e2e_dissolve_output.geojson").string();
    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(shpPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("test", srs.get(), wkbPolygon);
        ASSERT_NE(layer, nullptr);
        OGRFieldDefn fieldDefn("type", OFTString);
        layer->CreateField(&fieldDefn);

        auto* featDefn = layer->GetLayerDefn();
        auto* feat1 = OGRFeature::CreateFeature(featDefn);
        OGRPolygon poly1;
        OGRLinearRing ring1;
        ring1.addPoint(0.0, 0.0); ring1.addPoint(100.0, 0.0);
        ring1.addPoint(100.0, 100.0); ring1.addPoint(0.0, 100.0);
        ring1.addPoint(0.0, 0.0);
        poly1.addRing(&ring1);
        feat1->SetGeometry(&poly1);
        feat1->SetField("type", "A");
        layer->CreateFeature(feat1);
        OGRFeature::DestroyFeature(feat1);

        auto* feat2 = OGRFeature::CreateFeature(featDefn);
        OGRPolygon poly2;
        OGRLinearRing ring2;
        ring2.addPoint(100.0, 0.0); ring2.addPoint(200.0, 0.0);
        ring2.addPoint(200.0, 100.0); ring2.addPoint(100.0, 100.0);
        ring2.addPoint(100.0, 0.0);
        poly2.addRing(&ring2);
        feat2->SetGeometry(&poly2);
        feat2->SetField("type", "A");
        layer->CreateFeature(feat2);
        OGRFeature::DestroyFeature(feat2);

        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("dissolve");
    params["input"] = shpPath;
    params["output"] = output;
    params["dissolve_field"] = std::string("type");

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Dissolve failed: " << result.message;
}

TEST_F(PluginTest, VectorClipRejectsMismatchedSrs) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_clip_input.shp").string();
    std::string clip = (getTestDir() / "e2e_clip_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_clip_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);

        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto srs1 = std::make_unique<OGRSpatialReference>();
        srs1->importFromEPSG(3857);
        auto* layer1 = ds1->CreateLayer("input", srs1.get(), wkbPolygon);
        ASSERT_NE(layer1, nullptr);
        auto* featDefn1 = layer1->GetLayerDefn();
        auto* feat1 = OGRFeature::CreateFeature(featDefn1);
        OGRPolygon poly1;
        OGRLinearRing ring1;
        ring1.addPoint(0, 0); ring1.addPoint(100, 0); ring1.addPoint(100, 100); ring1.addPoint(0, 100); ring1.addPoint(0, 0);
        poly1.addRing(&ring1);
        feat1->SetGeometry(&poly1);
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);

        auto* ds2 = driver->Create(clip.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto srs2 = std::make_unique<OGRSpatialReference>();
        srs2->importFromEPSG(4326);
        auto* layer2 = ds2->CreateLayer("clip", srs2.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);
        auto* featDefn2 = layer2->GetLayerDefn();
        auto* feat2 = OGRFeature::CreateFeature(featDefn2);
        OGRPolygon poly2;
        OGRLinearRing ring2;
        ring2.addPoint(116, 39); ring2.addPoint(117, 39); ring2.addPoint(117, 40); ring2.addPoint(116, 40); ring2.addPoint(116, 39);
        poly2.addRing(&ring2);
        feat2->SetGeometry(&poly2);
        ASSERT_EQ(layer2->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);
        GDALClose(ds2);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("clip");
    params["input"] = input;
    params["clip_vector"] = clip;
    params["output"] = output;

    auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("CRS"), std::string::npos);
}

TEST_F(PluginTest, VectorUnionRejectsMismatchedSrs) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_union_input.shp").string();
    std::string overlay = (getTestDir() / "e2e_union_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_union_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);

        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto srs1 = std::make_unique<OGRSpatialReference>();
        srs1->importFromEPSG(3857);
        auto* layer1 = ds1->CreateLayer("input", srs1.get(), wkbPolygon);
        ASSERT_NE(layer1, nullptr);
        auto* featDefn1 = layer1->GetLayerDefn();
        auto* feat1 = OGRFeature::CreateFeature(featDefn1);
        OGRPolygon poly1;
        OGRLinearRing ring1;
        ring1.addPoint(0, 0); ring1.addPoint(100, 0); ring1.addPoint(100, 100); ring1.addPoint(0, 100); ring1.addPoint(0, 0);
        poly1.addRing(&ring1);
        feat1->SetGeometry(&poly1);
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);

        auto* ds2 = driver->Create(overlay.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto srs2 = std::make_unique<OGRSpatialReference>();
        srs2->importFromEPSG(4326);
        auto* layer2 = ds2->CreateLayer("overlay", srs2.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);
        auto* featDefn2 = layer2->GetLayerDefn();
        auto* feat2 = OGRFeature::CreateFeature(featDefn2);
        OGRPolygon poly2;
        OGRLinearRing ring2;
        ring2.addPoint(116, 39); ring2.addPoint(117, 39); ring2.addPoint(117, 40); ring2.addPoint(116, 40); ring2.addPoint(116, 39);
        poly2.addRing(&ring2);
        feat2->SetGeometry(&poly2);
        ASSERT_EQ(layer2->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);
        GDALClose(ds2);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("union");
    params["input"] = input;
    params["overlay_vector"] = overlay;
    params["output"] = output;

    auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("CRS"), std::string::npos);
}

TEST_F(PluginTest, VectorUnionLinePolygonOutputsSplitLinesOnly) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_union_line_input.shp").string();
    std::string overlay = (getTestDir() / "e2e_union_line_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_union_line_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);

        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto srs1 = std::make_unique<OGRSpatialReference>();
        srs1->importFromEPSG(3857);
        auto* layer1 = ds1->CreateLayer("input", srs1.get(), wkbLineString);
        ASSERT_NE(layer1, nullptr);
        OGRFieldDefn fieldDefn("name", OFTString);
        ASSERT_EQ(layer1->CreateField(&fieldDefn), OGRERR_NONE);
        auto* featDefn1 = layer1->GetLayerDefn();
        auto* feat1 = OGRFeature::CreateFeature(featDefn1);
        OGRLineString line;
        line.addPoint(0, 50);
        line.addPoint(100, 50);
        feat1->SetGeometry(&line);
        feat1->SetField("name", "road");
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);

        auto* ds2 = driver->Create(overlay.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto srs2 = std::make_unique<OGRSpatialReference>();
        srs2->importFromEPSG(3857);
        auto* layer2 = ds2->CreateLayer("overlay", srs2.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);
        auto* featDefn2 = layer2->GetLayerDefn();
        auto* feat2 = OGRFeature::CreateFeature(featDefn2);
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(25, 25);
        ring.addPoint(75, 25);
        ring.addPoint(75, 75);
        ring.addPoint(25, 75);
        ring.addPoint(25, 25);
        poly.addRing(&ring);
        feat2->SetGeometry(&poly);
        ASSERT_EQ(layer2->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);
        GDALClose(ds2);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("union");
    params["input"] = input;
    params["overlay_vector"] = overlay;
    params["output"] = output;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Union failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);

    int featureCount = 0;
    outLayer->ResetReading();
    OGRFeature* outFeat = nullptr;
    while ((outFeat = outLayer->GetNextFeature()) != nullptr) {
        ++featureCount;
        OGRGeometry* outGeom = outFeat->GetGeometryRef();
        ASSERT_NE(outGeom, nullptr);
        auto outType = wkbFlatten(outGeom->getGeometryType());
        EXPECT_EQ(outType, wkbMultiLineString)
            << "Unexpected geometry type: " << OGRGeometryTypeToName(outGeom->getGeometryType());
        OGRFeature::DestroyFeature(outFeat);
    }

    EXPECT_EQ(featureCount, 3);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorUnionRejectsHighlyOverlappedMixedDimensionOverlay) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_union_overlap_input.shp").string();
    std::string overlay = (getTestDir() / "e2e_union_overlap_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_union_overlap_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);

        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto srs1 = std::make_unique<OGRSpatialReference>();
        srs1->importFromEPSG(3857);
        auto* layer1 = ds1->CreateLayer("input", srs1.get(), wkbLineString);
        ASSERT_NE(layer1, nullptr);
        auto* featDefn1 = layer1->GetLayerDefn();
        auto* feat1 = OGRFeature::CreateFeature(featDefn1);
        OGRLineString line;
        line.addPoint(0, 50);
        line.addPoint(100, 50);
        feat1->SetGeometry(&line);
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);

        auto* ds2 = driver->Create(overlay.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto srs2 = std::make_unique<OGRSpatialReference>();
        srs2->importFromEPSG(3857);
        auto* layer2 = ds2->CreateLayer("overlay", srs2.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);

        auto createPolygonFeature = [&](double minX, double minY, double maxX, double maxY) {
            auto* feat = OGRFeature::CreateFeature(layer2->GetLayerDefn());
            OGRPolygon poly;
            OGRLinearRing ring;
            ring.addPoint(minX, minY);
            ring.addPoint(maxX, minY);
            ring.addPoint(maxX, maxY);
            ring.addPoint(minX, maxY);
            ring.addPoint(minX, minY);
            poly.addRing(&ring);
            feat->SetGeometry(&poly);
            ASSERT_EQ(layer2->CreateFeature(feat), OGRERR_NONE);
            OGRFeature::DestroyFeature(feat);
        };

        createPolygonFeature(10, 10, 90, 90);
        createPolygonFeature(20, 20, 80, 80);
        createPolygonFeature(30, 30, 70, 70);
        GDALClose(ds2);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("union");
    params["input"] = input;
    params["overlay_vector"] = overlay;
    params["output"] = output;

    auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("overlap"), std::string::npos);
}

TEST_F(PluginTest, VectorDifferenceRejectsMismatchedSrs) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_difference_input.shp").string();
    std::string overlay = (getTestDir() / "e2e_difference_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_difference_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);

        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto srs1 = std::make_unique<OGRSpatialReference>();
        srs1->importFromEPSG(3857);
        auto* layer1 = ds1->CreateLayer("input", srs1.get(), wkbPolygon);
        ASSERT_NE(layer1, nullptr);
        auto* featDefn1 = layer1->GetLayerDefn();
        auto* feat1 = OGRFeature::CreateFeature(featDefn1);
        OGRPolygon poly1;
        OGRLinearRing ring1;
        ring1.addPoint(0, 0); ring1.addPoint(100, 0); ring1.addPoint(100, 100); ring1.addPoint(0, 100); ring1.addPoint(0, 0);
        poly1.addRing(&ring1);
        feat1->SetGeometry(&poly1);
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);

        auto* ds2 = driver->Create(overlay.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto srs2 = std::make_unique<OGRSpatialReference>();
        srs2->importFromEPSG(4326);
        auto* layer2 = ds2->CreateLayer("overlay", srs2.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);
        auto* featDefn2 = layer2->GetLayerDefn();
        auto* feat2 = OGRFeature::CreateFeature(featDefn2);
        OGRPolygon poly2;
        OGRLinearRing ring2;
        ring2.addPoint(116, 39); ring2.addPoint(117, 39); ring2.addPoint(117, 40); ring2.addPoint(116, 40); ring2.addPoint(116, 39);
        poly2.addRing(&ring2);
        feat2->SetGeometry(&poly2);
        ASSERT_EQ(layer2->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);
        GDALClose(ds2);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("difference");
    params["input"] = input;
    params["overlay_vector"] = overlay;
    params["output"] = output;

    auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("CRS"), std::string::npos);
}

TEST_F(PluginTest, VectorDifferenceKeepsLinearGeometryOutput) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_difference_line_input.shp").string();
    std::string overlay = (getTestDir() / "e2e_difference_line_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_difference_line_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);

        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto srs1 = std::make_unique<OGRSpatialReference>();
        srs1->importFromEPSG(3857);
        auto* layer1 = ds1->CreateLayer("input", srs1.get(), wkbLineString);
        ASSERT_NE(layer1, nullptr);
        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer1->CreateField(&nameField), OGRERR_NONE);
        auto* featDefn1 = layer1->GetLayerDefn();
        auto* feat1 = OGRFeature::CreateFeature(featDefn1);
        OGRLineString line;
        line.addPoint(0, 0);
        line.addPoint(200, 0);
        feat1->SetGeometry(&line);
        feat1->SetField("name", "road");
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);

        auto* ds2 = driver->Create(overlay.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto srs2 = std::make_unique<OGRSpatialReference>();
        srs2->importFromEPSG(3857);
        auto* layer2 = ds2->CreateLayer("overlay", srs2.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);
        auto* featDefn2 = layer2->GetLayerDefn();
        auto* feat2 = OGRFeature::CreateFeature(featDefn2);
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(80, -20);
        ring.addPoint(120, -20);
        ring.addPoint(120, 20);
        ring.addPoint(80, 20);
        ring.addPoint(80, -20);
        poly.addRing(&ring);
        feat2->SetGeometry(&poly);
        ASSERT_EQ(layer2->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);
        GDALClose(ds2);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("difference");
    params["input"] = input;
    params["overlay_vector"] = overlay;
    params["output"] = output;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Difference failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    auto layerType = wkbFlatten(outLayer->GetGeomType());
    EXPECT_EQ(layerType, wkbMultiLineString)
        << "Unexpected layer geometry type: " << OGRGeometryTypeToName(outLayer->GetGeomType());
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    OGRGeometry* outGeom = outFeat->GetGeometryRef();
    ASSERT_NE(outGeom, nullptr);
    auto outType = wkbFlatten(outGeom->getGeometryType());
    EXPECT_EQ(outType, wkbMultiLineString)
        << "Unexpected geometry type: " << OGRGeometryTypeToName(outGeom->getGeometryType());
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorDifferenceWithoutOverlapAvoidsMultiGeometryWarning) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_difference_no_overlap_input.shp").string();
    std::string overlay = (getTestDir() / "e2e_difference_no_overlap_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_difference_no_overlap_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);

        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto srs1 = std::make_unique<OGRSpatialReference>();
        srs1->importFromEPSG(3857);
        auto* layer1 = ds1->CreateLayer("input", srs1.get(), wkbLineString);
        ASSERT_NE(layer1, nullptr);
        auto* featDefn1 = layer1->GetLayerDefn();
        auto* feat1 = OGRFeature::CreateFeature(featDefn1);
        OGRLineString line;
        line.addPoint(0, 0);
        line.addPoint(200, 0);
        feat1->SetGeometry(&line);
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);

        auto* ds2 = driver->Create(overlay.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto srs2 = std::make_unique<OGRSpatialReference>();
        srs2->importFromEPSG(3857);
        auto* layer2 = ds2->CreateLayer("overlay", srs2.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);
        auto* featDefn2 = layer2->GetLayerDefn();
        auto* feat2 = OGRFeature::CreateFeature(featDefn2);
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(500, -20);
        ring.addPoint(520, -20);
        ring.addPoint(520, 20);
        ring.addPoint(500, 20);
        ring.addPoint(500, -20);
        poly.addRing(&ring);
        feat2->SetGeometry(&poly);
        ASSERT_EQ(layer2->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);
        GDALClose(ds2);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("difference");
    params["input"] = input;
    params["overlay_vector"] = overlay;
    params["output"] = output;

    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        g_gdalWarnings.clear();
    }

    CPLPushErrorHandler(captureGdalWarning);
    auto result = p->execute(params, progress_);
    CPLPopErrorHandler();

    EXPECT_TRUE(result.success) << "Difference failed: " << result.message;

    bool hasGeometryTypeWarning = false;
    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        for (const auto& warning : g_gdalWarnings) {
            if (warning.find("geometry type MULTILINESTRING") != std::string::npos) {
                hasGeometryTypeWarning = true;
                break;
            }
        }
    }
    EXPECT_FALSE(hasGeometryTypeWarning);
}

TEST_F(PluginTest, VectorClipRepairsInvalidOverlayGeometry) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_clip_invalid_input.shp").string();
    std::string clip = (getTestDir() / "e2e_clip_invalid_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_clip_invalid_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);

        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto srs1 = std::make_unique<OGRSpatialReference>();
        srs1->importFromEPSG(3857);
        auto* layer1 = ds1->CreateLayer("input", srs1.get(), wkbLineString);
        ASSERT_NE(layer1, nullptr);
        auto* featDefn1 = layer1->GetLayerDefn();
        auto* feat1 = OGRFeature::CreateFeature(featDefn1);
        OGRLineString line;
        line.addPoint(0, 0);
        line.addPoint(200, 200);
        feat1->SetGeometry(&line);
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);

        auto* ds2 = driver->Create(clip.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto srs2 = std::make_unique<OGRSpatialReference>();
        srs2->importFromEPSG(3857);
        auto* layer2 = ds2->CreateLayer("clip", srs2.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);
        auto* featDefn2 = layer2->GetLayerDefn();
        auto* feat2 = OGRFeature::CreateFeature(featDefn2);
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(50, 50);
        ring.addPoint(150, 150);
        ring.addPoint(150, 50);
        ring.addPoint(50, 150);
        ring.addPoint(50, 50);
        poly.addRing(&ring);
        feat2->SetGeometry(&poly);
        ASSERT_EQ(layer2->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);
        GDALClose(ds2);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("clip");
    params["input"] = input;
    params["clip_vector"] = clip;
    params["output"] = output;

    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        g_gdalWarnings.clear();
    }

    CPLPushErrorHandler(captureGdalWarning);
    auto result = p->execute(params, progress_);
    CPLPopErrorHandler();
    EXPECT_TRUE(result.success) << "Clip failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));

    bool hasGeometryTypeWarning = false;
    bool hasSelfIntersectionWarning = false;
    bool hasDuplicateFidError = false;
    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        for (const auto& warning : g_gdalWarnings) {
            if (warning.find("geometry type LINESTRING") != std::string::npos ||
                warning.find("geometry type MULTILINESTRING") != std::string::npos) {
                hasGeometryTypeWarning = true;
            }
            if (warning.find("Self-intersection") != std::string::npos) {
                hasSelfIntersectionWarning = true;
            }
        }
        for (const auto& error : g_gdalErrors) {
            if (error.find("UNIQUE constraint failed") != std::string::npos &&
                error.find(".fid") != std::string::npos) {
                hasDuplicateFidError = true;
            }
        }
    }
    EXPECT_FALSE(hasGeometryTypeWarning);
    EXPECT_FALSE(hasSelfIntersectionWarning);
    EXPECT_FALSE(hasDuplicateFidError);
}

TEST_F(PluginTest, VectorFilterShapefileAsciiOutputReopens) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_filter_ascii_input.shp").string();
    std::string output = (getTestDir() / "e2e_filter_ascii_output.shp").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(4326);
        auto* layer = ds->CreateLayer("roads", srs.get(), wkbLineString);
        ASSERT_NE(layer, nullptr);
        OGRFieldDefn fieldDefn("name", OFTString);
        ASSERT_EQ(layer->CreateField(&fieldDefn), OGRERR_NONE);
        auto* featDefn = layer->GetLayerDefn();
        auto* feat = OGRFeature::CreateFeature(featDefn);
        OGRLineString line;
        line.addPoint(116.1, 39.9);
        line.addPoint(116.2, 40.0);
        feat->SetGeometry(&line);
        feat->SetField("name", "road");
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("filter");
    params["input"] = input;
    params["output"] = output;
    params["extent"] = std::array<double, 4>{116.0, 39.8, 116.3, 40.1};

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Filter failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(FALSE), 1);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorFilterShapefileChineseOutputReopens) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_filter_cn_input.shp").string();
    std::string output = (getTestDir() / "e2e_filter_cn_output.shp").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(4326);
        auto* layer = ds->CreateLayer("roads", srs.get(), wkbLineString);
        ASSERT_NE(layer, nullptr);
        OGRFieldDefn fieldDefn("name", OFTString);
        ASSERT_EQ(layer->CreateField(&fieldDefn), OGRERR_NONE);
        auto* featDefn = layer->GetLayerDefn();
        auto* feat = OGRFeature::CreateFeature(featDefn);
        OGRLineString line;
        line.addPoint(116.1, 39.9);
        line.addPoint(116.2, 40.0);
        feat->SetGeometry(&line);
        feat->SetField("name", "道路");
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("filter");
    params["input"] = input;
    params["output"] = output;
    params["extent"] = std::array<double, 4>{116.0, 39.8, 116.3, 40.1};

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Filter failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(FALSE), 1);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorInfoSupportsChinesePath) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    fs::path asciiPath = getTestDir() / "e2e_chinese_path_ascii.shp";
    fs::path chinesePath = getTestDir() / "中文道路数据.shp";

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(asciiPath.string().c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(4326);
        auto* layer = ds->CreateLayer("roads", srs.get(), wkbLineString);
        ASSERT_NE(layer, nullptr);
        auto* featDefn = layer->GetLayerDefn();
        auto* feat = OGRFeature::CreateFeature(featDefn);
        OGRLineString line;
        line.addPoint(116.1, 39.9);
        line.addPoint(116.2, 40.0);
        feat->SetGeometry(&line);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    for (const char* ext : {".shp", ".shx", ".dbf", ".prj"}) {
        fs::path src = asciiPath;
        src.replace_extension(ext);
        fs::path dst = chinesePath;
        dst.replace_extension(ext);
        if (fs::exists(dst)) {
            fs::remove(dst);
        }
        fs::rename(src, dst);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("info");
    params["input"] = utf8PathString(chinesePath);

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Info failed: " << result.message;
}

TEST_F(PluginTest, VectorBufferSupportsChinesePath) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    fs::path asciiPath = getTestDir() / "e2e_buffer_chinese_ascii.shp";
    fs::path chinesePath = getTestDir() / "中文道路缓冲输入.shp";
    std::string output = utf8PathString(getTestDir() / "e2e_buffer_chinese_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(asciiPath.string().c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("roads", srs.get(), wkbLineString);
        ASSERT_NE(layer, nullptr);
        OGRFieldDefn fieldDefn("name", OFTString);
        ASSERT_EQ(layer->CreateField(&fieldDefn), OGRERR_NONE);
        auto* featDefn = layer->GetLayerDefn();
        for (int i = 0; i < 3; ++i) {
            auto* feat = OGRFeature::CreateFeature(featDefn);
            OGRLineString line;
            line.addPoint(i * 100.0, 0.0);
            line.addPoint(i * 100.0 + 50.0, 20.0);
            feat->SetGeometry(&line);
            feat->SetField("name", "road");
            ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
            OGRFeature::DestroyFeature(feat);
        }
        GDALClose(ds);
    }

    for (const char* ext : {".shp", ".shx", ".dbf", ".prj"}) {
        fs::path src = asciiPath;
        src.replace_extension(ext);
        fs::path dst = chinesePath;
        dst.replace_extension(ext);
        if (fs::exists(dst)) {
            fs::remove(dst);
        }
        fs::rename(src, dst);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("buffer");
    params["input"] = utf8PathString(chinesePath);
    params["output"] = output;
    params["distance"] = 15.0;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Buffer failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "3");
}

TEST_F(PluginTest, VectorDissolveRejectsGeographicSrs) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_dissolve_geo_input.shp").string();
    std::string output = (getTestDir() / "e2e_dissolve_geo_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(4326);
        auto* layer = ds->CreateLayer("input", srs.get(), wkbPolygon);
        ASSERT_NE(layer, nullptr);
        OGRFieldDefn fieldDefn("type", OFTString);
        ASSERT_EQ(layer->CreateField(&fieldDefn), OGRERR_NONE);
        auto* featDefn = layer->GetLayerDefn();
        auto* feat = OGRFeature::CreateFeature(featDefn);
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(116, 39); ring.addPoint(116.1, 39); ring.addPoint(116.1, 39.1); ring.addPoint(116, 39.1); ring.addPoint(116, 39);
        poly.addRing(&ring);
        feat->SetGeometry(&poly);
        feat->SetField("type", "A");
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("dissolve");
    params["input"] = input;
    params["output"] = output;
    params["dissolve_field"] = std::string("type");

    auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("projected"), std::string::npos);
}

TEST_F(PluginTest, VectorDissolveRepairsInvalidPolygonGeometry) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_dissolve_invalid_input.shp").string();
    std::string output = (getTestDir() / "e2e_dissolve_invalid_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("input", srs.get(), wkbPolygon);
        ASSERT_NE(layer, nullptr);
        OGRFieldDefn fieldDefn("type", OFTString);
        ASSERT_EQ(layer->CreateField(&fieldDefn), OGRERR_NONE);

        auto* featDefn = layer->GetLayerDefn();
        auto createInvalidFeature = [&](double offsetX) {
            auto* feat = OGRFeature::CreateFeature(featDefn);
            OGRPolygon poly;
            OGRLinearRing ring;
            ring.addPoint(0 + offsetX, 0);
            ring.addPoint(100 + offsetX, 100);
            ring.addPoint(100 + offsetX, 0);
            ring.addPoint(0 + offsetX, 100);
            ring.addPoint(0 + offsetX, 0);
            poly.addRing(&ring);
            feat->SetGeometry(&poly);
            feat->SetField("type", "A");
            ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
            OGRFeature::DestroyFeature(feat);
        };

        createInvalidFeature(0);
        createInvalidFeature(20);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("dissolve");
    params["input"] = input;
    params["output"] = output;
    params["dissolve_field"] = std::string("type");

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Dissolve failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    OGRGeometry* outGeom = outFeat->GetGeometryRef();
    ASSERT_NE(outGeom, nullptr);
    EXPECT_EQ(wkbFlatten(outGeom->getGeometryType()), wkbMultiPolygon)
        << "Unexpected geometry type: " << OGRGeometryTypeToName(outGeom->getGeometryType());
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorClipKeepsOnlyLinearOverlapSegments) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string input = (getTestDir() / "e2e_clip_line_input.shp").string();
    std::string clip = (getTestDir() / "e2e_clip_line_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_clip_line_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);

        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto srs1 = std::make_unique<OGRSpatialReference>();
        srs1->importFromEPSG(3857);
        auto* layer1 = ds1->CreateLayer("input", srs1.get(), wkbLineString);
        ASSERT_NE(layer1, nullptr);
        auto* featDefn1 = layer1->GetLayerDefn();
        auto* feat1 = OGRFeature::CreateFeature(featDefn1);
        OGRLineString line;
        line.addPoint(0, 0);
        line.addPoint(200, 0);
        feat1->SetGeometry(&line);
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);

        auto* ds2 = driver->Create(clip.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto srs2 = std::make_unique<OGRSpatialReference>();
        srs2->importFromEPSG(3857);
        auto* layer2 = ds2->CreateLayer("clip", srs2.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);
        auto* featDefn2 = layer2->GetLayerDefn();
        auto* feat2 = OGRFeature::CreateFeature(featDefn2);
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(80, -20);
        ring.addPoint(120, -20);
        ring.addPoint(120, 20);
        ring.addPoint(80, 20);
        ring.addPoint(80, -20);
        poly.addRing(&ring);
        feat2->SetGeometry(&poly);
        ASSERT_EQ(layer2->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);
        GDALClose(ds2);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("clip");
    params["input"] = input;
    params["clip_vector"] = clip;
    params["output"] = output;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Clip failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);

    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    OGRGeometry* outGeom = outFeat->GetGeometryRef();
    ASSERT_NE(outGeom, nullptr);
    auto outType = wkbFlatten(outGeom->getGeometryType());
    EXPECT_EQ(outType, wkbMultiLineString)
        << "Unexpected geometry type: " << OGRGeometryTypeToName(outGeom->getGeometryType());

    const auto* multiLine = outGeom->toMultiLineString();
    ASSERT_NE(multiLine, nullptr);
    ASSERT_EQ(multiLine->getNumGeometries(), 1);
    const auto* clippedLine = multiLine->getGeometryRef(0)->toLineString();
    ASSERT_NE(clippedLine, nullptr);
    EXPECT_DOUBLE_EQ(clippedLine->getX(0), 80.0);
    EXPECT_DOUBLE_EQ(clippedLine->getY(0), 0.0);
    EXPECT_DOUBLE_EQ(clippedLine->getX(clippedLine->getNumPoints() - 1), 120.0);
    EXPECT_DOUBLE_EQ(clippedLine->getY(clippedLine->getNumPoints() - 1), 0.0);

    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorConvertExecution) {
    auto* p = mgr_.find("vector");
    if (!p) GTEST_SKIP() << "vector plugin not loaded";

    std::string shpPath = (getTestDir() / "e2e_convert_input.shp").string();
    std::string output = (getTestDir() / "e2e_convert_output.geojson").string();
    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(shpPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(4326);
        auto* layer = ds->CreateLayer("test", srs.get(), wkbPoint);
        ASSERT_NE(layer, nullptr);
        auto* featDefn = layer->GetLayerDefn();
        for (int i = 0; i < 3; ++i) {
            auto* feat = OGRFeature::CreateFeature(featDefn);
            OGRPoint pt(116.0 + i * 0.01, 40.0 + i * 0.01);
            feat->SetGeometry(&pt);
            layer->CreateFeature(feat);
            OGRFeature::DestroyFeature(feat);
        }
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("convert");
    params["input"] = shpPath;
    params["output"] = output;
    params["format"] = std::string("GeoJSON");

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Convert failed: " << result.message;
}

TEST_F(PluginTest, VectorConvertCreatesMissingParentDirectory) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    std::string shpPath = (getTestDir() / "e2e_convert_parent_input.shp").string();
    const fs::path outputDir = getTestDir() / "nested_convert_output";
    const std::string output = utf8PathString(outputDir / "roads.geojson");
    fs::remove_all(outputDir);
    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(shpPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(4326);
        auto* layer = ds->CreateLayer("test", srs.get(), wkbPoint);
        ASSERT_NE(layer, nullptr);
        auto* featDefn = layer->GetLayerDefn();
        auto* feat = OGRFeature::CreateFeature(featDefn);
        OGRPoint pt(116.3, 39.9);
        feat->SetGeometry(&pt);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("convert");
    params["input"] = shpPath;
    params["output"] = output;
    params["format"] = std::string("GeoJSON");

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
}

TEST_F(PluginTest, VectorSimplifyExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_simplify_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_simplify_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("roads", srs.get(), wkbLineString, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRLineString line;
        line.addPoint(0, 0);
        line.addPoint(10, 0.5);
        line.addPoint(20, -0.5);
        line.addPoint(30, 0.4);
        line.addPoint(40, 0);
        feat->SetGeometry(&line);
        feat->SetField("name", "main_road");
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("simplify");
    params["input"] = input;
    params["output"] = output;
    params["tolerance"] = 2.0;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "1");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "main_road");
    auto* outLine = outFeat->GetGeometryRef()->toLineString();
    ASSERT_NE(outLine, nullptr);
    EXPECT_LT(outLine->getNumPoints(), 5);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorRepairExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_repair_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_repair_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("parcels", srs.get(), wkbPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(0, 0);
        ring.addPoint(50, 50);
        ring.addPoint(50, 0);
        ring.addPoint(0, 50);
        ring.addPoint(0, 0);
        poly.addRing(&ring);
        feat->SetGeometry(&poly);
        feat->SetField("name", "invalid");
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("repair");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("repaired_count"), "1");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "invalid");
    OGRGeometry* outGeom = outFeat->GetGeometryRef();
    ASSERT_NE(outGeom, nullptr);
    EXPECT_TRUE(outGeom->IsValid());
    EXPECT_FALSE(outGeom->IsEmpty());
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorGeomMetricsExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_geom_metrics_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_geom_metrics_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("parcels", srs.get(), wkbPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(0, 0);
        ring.addPoint(100, 0);
        ring.addPoint(100, 50);
        ring.addPoint(0, 50);
        ring.addPoint(0, 0);
        poly.addRing(&ring);
        feat->SetGeometry(&poly);
        feat->SetField("name", "rect");
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("geom_metrics");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "1");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "rect");
    EXPECT_NEAR(outFeat->GetFieldAsDouble("geom_area"), 5000.0, 1e-6);
    EXPECT_NEAR(outFeat->GetFieldAsDouble("geom_length"), 300.0, 1e-6);
    EXPECT_GT(outFeat->GetFieldAsDouble("compactness"), 0.0);
    EXPECT_GT(outFeat->GetFieldAsDouble("circularity"), 0.0);
    EXPECT_GE(outFeat->GetFieldAsDouble("orientation"), 0.0);
    EXPECT_LE(outFeat->GetFieldAsDouble("orientation"), 180.0);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorNearestExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_nearest_input.gpkg");
    const std::string target = utf8PathString(getTestDir() / "e2e_nearest_target.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_nearest_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);

        auto* inputDs = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(inputDs, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* inputLayer = inputDs->CreateLayer("input", srs.get(), wkbPoint, nullptr);
        ASSERT_NE(inputLayer, nullptr);
        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(inputLayer->CreateField(&nameField), OGRERR_NONE);
        auto* inputFeat = OGRFeature::CreateFeature(inputLayer->GetLayerDefn());
        OGRPoint inputPoint(0, 0);
        inputFeat->SetGeometry(&inputPoint);
        inputFeat->SetField("name", "src");
        ASSERT_EQ(inputLayer->CreateFeature(inputFeat), OGRERR_NONE);
        OGRFeature::DestroyFeature(inputFeat);
        GDALClose(inputDs);

        auto* targetDs = driver->Create(target.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(targetDs, nullptr);
        auto* targetLayer = targetDs->CreateLayer("target", srs.get(), wkbPoint, nullptr);
        ASSERT_NE(targetLayer, nullptr);
        OGRFieldDefn targetNameField("label", OFTString);
        ASSERT_EQ(targetLayer->CreateField(&targetNameField), OGRERR_NONE);

        auto* nearFeat = OGRFeature::CreateFeature(targetLayer->GetLayerDefn());
        OGRPoint nearPoint(3, 4);
        nearFeat->SetGeometry(&nearPoint);
        nearFeat->SetField("label", "A");
        ASSERT_EQ(targetLayer->CreateFeature(nearFeat), OGRERR_NONE);
        OGRFeature::DestroyFeature(nearFeat);

        auto* farFeat = OGRFeature::CreateFeature(targetLayer->GetLayerDefn());
        OGRPoint farPoint(20, 20);
        farFeat->SetGeometry(&farPoint);
        farFeat->SetField("label", "B");
        ASSERT_EQ(targetLayer->CreateFeature(farFeat), OGRERR_NONE);
        OGRFeature::DestroyFeature(farFeat);
        GDALClose(targetDs);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("nearest");
    params["input"] = input;
    params["output"] = output;
    params["nearest_vector"] = target;
    params["nearest_field"] = std::string("label");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "1");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_NEAR(outFeat->GetFieldAsDouble("nearest_dist"), 5.0, 1e-6);
    EXPECT_GE(outFeat->GetFieldAsInteger64("nearest_fid"), 1);
    EXPECT_STREQ(outFeat->GetFieldAsString("nearest_val"), "A");
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorAdjacencyExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_adjacency_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_adjacency_output.csv");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("parcels", srs.get(), wkbPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        auto makeRect = [&](double minX, double minY, double maxX, double maxY) {
            OGRPolygon poly;
            OGRLinearRing ring;
            ring.addPoint(minX, minY);
            ring.addPoint(maxX, minY);
            ring.addPoint(maxX, maxY);
            ring.addPoint(minX, maxY);
            ring.addPoint(minX, minY);
            poly.addRing(&ring);
            return poly;
        };

        auto* feat1 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly1 = makeRect(0, 0, 10, 10);
        feat1->SetGeometry(&poly1);
        ASSERT_EQ(layer->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);

        auto* feat2 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly2 = makeRect(10, 0, 20, 10);
        feat2->SetGeometry(&poly2);
        ASSERT_EQ(layer->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);

        auto* feat3 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly3 = makeRect(30, 0, 40, 10);
        feat3->SetGeometry(&poly3);
        ASSERT_EQ(layer->CreateFeature(feat3), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat3);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("adjacency");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("relation_count"), "1");

    std::ifstream ifs(fs::u8path(output));
    ASSERT_TRUE(ifs.is_open());
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(ifs, line)) {
        lines.push_back(line);
    }
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "source_fid,target_fid,shared_length");
    EXPECT_NE(lines[1].find(",10"), std::string::npos);
}

TEST_F(PluginTest, VectorOverlapCheckExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_overlap_check_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_overlap_check_output.csv");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("parcels", srs.get(), wkbPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        auto makeRect = [&](double minX, double minY, double maxX, double maxY) {
            OGRPolygon poly;
            OGRLinearRing ring;
            ring.addPoint(minX, minY);
            ring.addPoint(maxX, minY);
            ring.addPoint(maxX, maxY);
            ring.addPoint(minX, maxY);
            ring.addPoint(minX, minY);
            poly.addRing(&ring);
            return poly;
        };

        auto* feat1 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly1 = makeRect(0, 0, 10, 10);
        feat1->SetGeometry(&poly1);
        ASSERT_EQ(layer->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);

        auto* feat2 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly2 = makeRect(5, 0, 15, 10);
        feat2->SetGeometry(&poly2);
        ASSERT_EQ(layer->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);

        auto* feat3 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly3 = makeRect(30, 0, 40, 10);
        feat3->SetGeometry(&poly3);
        ASSERT_EQ(layer->CreateFeature(feat3), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat3);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("overlap_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("relation_count"), "1");

    std::ifstream ifs(fs::u8path(output));
    ASSERT_TRUE(ifs.is_open());
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(ifs, line)) {
        lines.push_back(line);
    }
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "source_fid,target_fid,overlap_area");
    EXPECT_NE(lines[1].find(",50"), std::string::npos);
}

TEST_F(PluginTest, VectorTopologyCheckExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_topology_check_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_topology_check_output.csv");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("parcels", srs.get(), wkbPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        auto makeRect = [&](double minX, double minY, double maxX, double maxY) {
            OGRPolygon poly;
            OGRLinearRing ring;
            ring.addPoint(minX, minY);
            ring.addPoint(maxX, minY);
            ring.addPoint(maxX, maxY);
            ring.addPoint(minX, maxY);
            ring.addPoint(minX, minY);
            poly.addRing(&ring);
            return poly;
        };

        auto* feat1 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly1 = makeRect(0, 0, 10, 10);
        feat1->SetGeometry(&poly1);
        ASSERT_EQ(layer->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);

        auto* feat2 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly2 = makeRect(0, 0, 10, 10);
        feat2->SetGeometry(&poly2);
        ASSERT_EQ(layer->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);

        auto* feat3 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly3 = makeRect(5, 0, 15, 10);
        feat3->SetGeometry(&poly3);
        ASSERT_EQ(layer->CreateFeature(feat3), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat3);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("topology_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("issue_count"), "3");

    std::ifstream ifs(fs::u8path(output));
    ASSERT_TRUE(ifs.is_open());
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(ifs, line)) {
        lines.push_back(line);
    }
    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(lines[0], "source_fid,target_fid,issue_type,issue_value");
    int duplicateCount = 0;
    int overlapCount = 0;
    for (size_t i = 1; i < lines.size(); ++i) {
        if (lines[i].find("duplicate_geometry") != std::string::npos) {
            ++duplicateCount;
        }
        if (lines[i].find("overlap,50") != std::string::npos) {
            ++overlapCount;
        }
    }
    EXPECT_EQ(duplicateCount, 1);
    EXPECT_EQ(overlapCount, 2);
}

TEST_F(PluginTest, VectorConvexHullExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_convex_hull_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_convex_hull_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("shapes", srs.get(), wkbPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(0, 0);
        ring.addPoint(10, 0);
        ring.addPoint(10, 2);
        ring.addPoint(2, 2);
        ring.addPoint(2, 10);
        ring.addPoint(0, 10);
        ring.addPoint(0, 0);
        poly.addRing(&ring);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", "A");
        feat->SetGeometry(&poly);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("convex_hull");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "1");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "A");
    ASSERT_NE(outFeat->GetGeometryRef(), nullptr);
    EXPECT_NEAR(outFeat->GetGeometryRef()->toPolygon()->get_Area(), 68.0, 1e-6);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorCentroidExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_centroid_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_centroid_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("shapes", srs.get(), wkbPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(0, 0);
        ring.addPoint(10, 0);
        ring.addPoint(10, 10);
        ring.addPoint(0, 10);
        ring.addPoint(0, 0);
        poly.addRing(&ring);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", "A");
        feat->SetGeometry(&poly);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("centroid");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "1");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "A");
    auto* centroid = outFeat->GetGeometryRef()->toPoint();
    ASSERT_NE(centroid, nullptr);
    EXPECT_NEAR(centroid->getX(), 5.0, 1e-6);
    EXPECT_NEAR(centroid->getY(), 5.0, 1e-6);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorEnvelopeExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_envelope_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_envelope_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("shapes", srs.get(), wkbPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(0, 0);
        ring.addPoint(10, 0);
        ring.addPoint(10, 2);
        ring.addPoint(2, 2);
        ring.addPoint(2, 10);
        ring.addPoint(0, 10);
        ring.addPoint(0, 0);
        poly.addRing(&ring);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", "A");
        feat->SetGeometry(&poly);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("envelope");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "1");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "A");
    ASSERT_NE(outFeat->GetGeometryRef(), nullptr);
    EXPECT_NEAR(outFeat->GetGeometryRef()->toPolygon()->get_Area(), 100.0, 1e-6);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorBoundaryExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_boundary_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_boundary_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("shapes", srs.get(), wkbPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(0, 0);
        ring.addPoint(10, 0);
        ring.addPoint(10, 10);
        ring.addPoint(0, 10);
        ring.addPoint(0, 0);
        poly.addRing(&ring);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", "A");
        feat->SetGeometry(&poly);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("boundary");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "1");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "A");
    ASSERT_NE(outFeat->GetGeometryRef(), nullptr);
    const auto* outGeom = outFeat->GetGeometryRef();
    const auto flatType = wkbFlatten(outGeom->getGeometryType());
    EXPECT_TRUE(flatType == wkbLineString || flatType == wkbMultiLineString);
    double length = 0.0;
    if (flatType == wkbLineString) {
        length = outGeom->toLineString()->get_Length();
    } else {
        length = outGeom->toMultiLineString()->get_Length();
    }
    EXPECT_NEAR(length, 40.0, 1e-6);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorMultipartCheckExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_multipart_check_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_multipart_check_output.csv");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("shapes", srs.get(), wkbMultiPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRMultiPolygon multiPoly;
        OGRPolygon poly1;
        OGRLinearRing ring1;
        ring1.addPoint(0, 0);
        ring1.addPoint(5, 0);
        ring1.addPoint(5, 5);
        ring1.addPoint(0, 5);
        ring1.addPoint(0, 0);
        poly1.addRing(&ring1);
        multiPoly.addGeometry(&poly1);

        OGRPolygon poly2;
        OGRLinearRing ring2;
        ring2.addPoint(10, 0);
        ring2.addPoint(15, 0);
        ring2.addPoint(15, 5);
        ring2.addPoint(10, 5);
        ring2.addPoint(10, 0);
        poly2.addRing(&ring2);
        multiPoly.addGeometry(&poly2);

        auto* feat1 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat1->SetGeometry(&multiPoly);
        ASSERT_EQ(layer->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);

        OGRMultiPolygon singleAsMulti;
        OGRPolygon poly3;
        OGRLinearRing ring3;
        ring3.addPoint(20, 0);
        ring3.addPoint(25, 0);
        ring3.addPoint(25, 5);
        ring3.addPoint(20, 5);
        ring3.addPoint(20, 0);
        poly3.addRing(&ring3);
        singleAsMulti.addGeometry(&poly3);

        auto* feat2 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat2->SetGeometry(&singleAsMulti);
        ASSERT_EQ(layer->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("multipart_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("issue_count"), "2");

    std::ifstream ifs(fs::u8path(output));
    ASSERT_TRUE(ifs.is_open());
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(ifs, line)) {
        lines.push_back(line);
    }
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "source_fid,geom_type,part_count");
    EXPECT_NE(lines[1].find("MultiPolygon"), std::string::npos);
    EXPECT_NE(lines[1].find(",2"), std::string::npos);
    EXPECT_NE(lines[2].find("MultiPolygon"), std::string::npos);
    EXPECT_NE(lines[2].find(",1"), std::string::npos);
}

TEST_F(PluginTest, VectorSinglepartExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_singlepart_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_singlepart_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("shapes", srs.get(), wkbMultiPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        OGRMultiPolygon multiPoly;
        OGRPolygon poly1;
        OGRLinearRing ring1;
        ring1.addPoint(0, 0);
        ring1.addPoint(5, 0);
        ring1.addPoint(5, 5);
        ring1.addPoint(0, 5);
        ring1.addPoint(0, 0);
        poly1.addRing(&ring1);
        multiPoly.addGeometry(&poly1);

        OGRPolygon poly2;
        OGRLinearRing ring2;
        ring2.addPoint(10, 0);
        ring2.addPoint(15, 0);
        ring2.addPoint(15, 5);
        ring2.addPoint(10, 5);
        ring2.addPoint(10, 0);
        poly2.addRing(&ring2);
        multiPoly.addGeometry(&poly2);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", "A");
        feat->SetGeometry(&multiPoly);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("singlepart");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "2");
    EXPECT_EQ(result.metadata.at("source_count"), "1");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(), 2);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "A");
    EXPECT_EQ(wkbFlatten(outFeat->GetGeometryRef()->getGeometryType()), wkbPolygon);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorVerticesExtractExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_vertices_extract_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_vertices_extract_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("shapes", srs.get(), wkbLineString, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        OGRLineString line;
        line.addPoint(0, 0);
        line.addPoint(5, 5);
        line.addPoint(10, 0);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", "A");
        feat->SetGeometry(&line);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("vertices_extract");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "3");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(), 3);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "A");
    EXPECT_EQ(outFeat->GetFieldAsInteger("vertex_idx"), 0);
    EXPECT_EQ(wkbFlatten(outFeat->GetGeometryRef()->getGeometryType()), wkbPoint);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorEndpointsExtractExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_endpoints_extract_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_endpoints_extract_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("shapes", srs.get(), wkbLineString, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        OGRLineString line;
        line.addPoint(0, 0);
        line.addPoint(5, 5);
        line.addPoint(10, 0);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", "A");
        feat->SetGeometry(&line);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("endpoints_extract");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "2");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(), 2);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "A");
    EXPECT_EQ(outFeat->GetFieldAsInteger("endpoint_type"), 0);
    EXPECT_EQ(wkbFlatten(outFeat->GetGeometryRef()->getGeometryType()), wkbPoint);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorMidpointsExtractExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_midpoints_extract_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_midpoints_extract_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("shapes", srs.get(), wkbLineString, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        OGRLineString line;
        line.addPoint(0, 0);
        line.addPoint(10, 0);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", "A");
        feat->SetGeometry(&line);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("midpoints_extract");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "1");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "A");
    const auto* point = outFeat->GetGeometryRef()->toPoint();
    ASSERT_NE(point, nullptr);
    EXPECT_NEAR(point->getX(), 5.0, 1e-6);
    EXPECT_NEAR(point->getY(), 0.0, 1e-6);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorInteriorPointExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_interior_point_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_interior_point_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("shapes", srs.get(), wkbPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(0, 0);
        ring.addPoint(10, 0);
        ring.addPoint(10, 10);
        ring.addPoint(0, 10);
        ring.addPoint(0, 0);
        poly.addRing(&ring);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", "A");
        feat->SetGeometry(&poly);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("interior_point");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "1");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "A");
    const auto* point = outFeat->GetGeometryRef()->toPoint();
    ASSERT_NE(point, nullptr);
    EXPECT_GE(point->getX(), 0.0);
    EXPECT_LE(point->getX(), 10.0);
    EXPECT_GE(point->getY(), 0.0);
    EXPECT_LE(point->getY(), 10.0);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorDuplicatePointCheckExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_duplicate_point_check_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_duplicate_point_check_output.csv");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("points", srs.get(), wkbPoint, nullptr);
        ASSERT_NE(layer, nullptr);

        auto addPoint = [&](double x, double y) {
            OGRPoint point(x, y);
            auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
            feat->SetGeometry(&point);
            ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
            OGRFeature::DestroyFeature(feat);
        };

        addPoint(0, 0);
        addPoint(1, 1);
        addPoint(0, 0);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("duplicate_point_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("issue_count"), "1");

    std::ifstream ifs(fs::u8path(output));
    ASSERT_TRUE(ifs.is_open());
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(ifs, line)) {
        lines.push_back(line);
    }
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "source_fid,duplicate_fid,x,y");
    std::stringstream ss(lines[1]);
    std::vector<std::string> fields;
    while (std::getline(ss, line, ',')) {
        fields.push_back(line);
    }
    ASSERT_EQ(fields.size(), 4u);
    EXPECT_EQ(fields[2], "0");
    EXPECT_EQ(fields[3], "0");
}

TEST_F(PluginTest, VectorHoleCheckExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_hole_check_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_hole_check_output.csv");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("polygons", srs.get(), wkbPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRPolygon withHole;
        OGRLinearRing outer;
        outer.addPoint(0, 0);
        outer.addPoint(10, 0);
        outer.addPoint(10, 10);
        outer.addPoint(0, 10);
        outer.addPoint(0, 0);
        withHole.addRing(&outer);

        OGRLinearRing inner;
        inner.addPoint(2, 2);
        inner.addPoint(4, 2);
        inner.addPoint(4, 4);
        inner.addPoint(2, 4);
        inner.addPoint(2, 2);
        withHole.addRing(&inner);

        auto* feat1 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat1->SetGeometry(&withHole);
        ASSERT_EQ(layer->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);

        OGRPolygon noHole;
        OGRLinearRing ring;
        ring.addPoint(20, 0);
        ring.addPoint(30, 0);
        ring.addPoint(30, 10);
        ring.addPoint(20, 10);
        ring.addPoint(20, 0);
        noHole.addRing(&ring);

        auto* feat2 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat2->SetGeometry(&noHole);
        ASSERT_EQ(layer->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("hole_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("issue_count"), "1");

    std::ifstream ifs(fs::u8path(output));
    ASSERT_TRUE(ifs.is_open());
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(ifs, line)) {
        lines.push_back(line);
    }
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0], "source_fid,geom_type,hole_count");
    EXPECT_NE(lines[1].find("Polygon"), std::string::npos);
    EXPECT_NE(lines[1].find(",1"), std::string::npos);
}

TEST_F(PluginTest, VectorDanglingEndpointCheckExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_dangling_endpoint_check_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_dangling_endpoint_check_output.csv");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("lines", srs.get(), wkbLineString, nullptr);
        ASSERT_NE(layer, nullptr);

        auto addLine = [&](std::initializer_list<std::pair<double, double>> points) {
            OGRLineString line;
            for (const auto& [x, y] : points) {
                line.addPoint(x, y);
            }
            auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
            feat->SetGeometry(&line);
            ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
            OGRFeature::DestroyFeature(feat);
        };

        addLine({{0, 0}, {1, 0}});
        addLine({{1, 0}, {2, 0}});
        addLine({{10, 0}, {11, 0}});
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("dangling_endpoint_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("issue_count"), "4");

    std::ifstream ifs(fs::u8path(output));
    ASSERT_TRUE(ifs.is_open());
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(ifs, line)) {
        lines.push_back(line);
    }
    ASSERT_EQ(lines.size(), 5u);
    EXPECT_EQ(lines[0], "source_fid,endpoint_type,x,y");
    EXPECT_NE(lines[1].find(",0,0,0"), std::string::npos);
    EXPECT_NE(lines[2].find(",1,2,0"), std::string::npos);
    EXPECT_NE(lines[3].find(",0,10,0"), std::string::npos);
    EXPECT_NE(lines[4].find(",1,11,0"), std::string::npos);
}

TEST_F(PluginTest, VectorSliverRemoveExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_sliver_remove_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_sliver_remove_output.gpkg");

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("polygons", srs.get(), wkbMultiPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRMultiPolygon mixed;

        OGRPolygon largePolygon;
        OGRLinearRing largeRing;
        largeRing.addPoint(0, 0);
        largeRing.addPoint(10, 0);
        largeRing.addPoint(10, 10);
        largeRing.addPoint(0, 10);
        largeRing.addPoint(0, 0);
        largePolygon.addRing(&largeRing);
        mixed.addGeometry(&largePolygon);

        OGRPolygon smallPolygon;
        OGRLinearRing smallRing;
        smallRing.addPoint(20, 0);
        smallRing.addPoint(21, 0);
        smallRing.addPoint(21, 1);
        smallRing.addPoint(20, 1);
        smallRing.addPoint(20, 0);
        smallPolygon.addRing(&smallRing);
        mixed.addGeometry(&smallPolygon);

        auto* feat1 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat1->SetGeometry(&mixed);
        ASSERT_EQ(layer->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);

        OGRMultiPolygon onlySmall;
        OGRPolygon smallOnlyPolygon;
        OGRLinearRing smallOnlyRing;
        smallOnlyRing.addPoint(30, 0);
        smallOnlyRing.addPoint(31, 0);
        smallOnlyRing.addPoint(31, 1);
        smallOnlyRing.addPoint(30, 1);
        smallOnlyRing.addPoint(30, 0);
        smallOnlyPolygon.addRing(&smallOnlyRing);
        onlySmall.addGeometry(&smallOnlyPolygon);

        auto* feat2 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat2->SetGeometry(&onlySmall);
        ASSERT_EQ(layer->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("sliver_remove");
    params["input"] = input;
    params["output"] = output;
    params["min_area"] = 10.0;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("feature_count"), "1");
    EXPECT_EQ(result.metadata.at("removed_part_count"), "2");
    EXPECT_EQ(result.metadata.at("removed_feature_count"), "1");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(), 1);

    outLayer->ResetReading();
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    OGRGeometry* outGeom = outFeat->GetGeometryRef();
    ASSERT_NE(outGeom, nullptr);
    ASSERT_EQ(wkbFlatten(outGeom->getGeometryType()), wkbMultiPolygon);
    EXPECT_NEAR(outGeom->toMultiPolygon()->get_Area(), 100.0, 1e-6);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, RasterMathBandMathExecution) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);

    std::string input = createTestRaster("e2e_bandmath_input.tif", 30, 30, 2);
    std::string output = (getTestDir() / "e2e_bandmath_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("band_math");
    params["input"] = input;
    params["output"] = output;
    params["expression"] = std::string("B1+B2");

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Band math failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
}

TEST_F(PluginTest, CuttingClipRejectsMissingExtentAndCutline) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("cutting_clip_input.tif", 32, 24);
    const std::string output = utf8PathString(getTestDir() / "cutting_clip_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("clip");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Either extent or cutline must be specified"), std::string::npos);
}

TEST_F(PluginTest, CuttingClipByExtentProducesExpectedWindow) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    const std::string input = createConstantRaster("cutting_clip_extent_input.tif", 20, 10, 5.0f);
    const std::string output = utf8PathString(getTestDir() / "cutting_clip_extent_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("clip");
    params["input"] = input;
    params["output"] = output;
    params["extent"] = std::array<double, 4>{116.005, 39.994, 116.012, 39.998};

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;

    auto outDs = gis::core::openRaster(output, true);
    ASSERT_NE(outDs, nullptr);
    EXPECT_EQ(outDs->GetRasterXSize(), 7);
    EXPECT_EQ(outDs->GetRasterYSize(), 4);
}

TEST_F(PluginTest, CuttingSplitExecutionCreatesTiles) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("cutting_split_input.tif", 30, 20);
    const fs::path outputDir = getTestDir() / "cutting_split_tiles";
    fs::remove_all(outputDir);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("split");
    params["input"] = input;
    params["output"] = utf8PathString(outputDir);
    params["tile_size"] = 16;
    params["overlap"] = 0;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(outputDir));

    size_t tileCount = 0;
    for (const auto& entry : fs::directory_iterator(outputDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".tif") {
            ++tileCount;
        }
    }
    EXPECT_EQ(tileCount, 4u);
}

TEST_F(PluginTest, CuttingMosaicProducesCombinedExtentAndValues) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    const std::string left = createConstantRaster("cutting_mosaic_left.tif", 10, 6, 10.0f, 116.0, 40.0);
    const std::string right = createConstantRaster("cutting_mosaic_right.tif", 10, 6, 30.0f, 116.010, 40.0);
    const std::string output = utf8PathString(getTestDir() / "cutting_mosaic_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("mosaic");
    params["input"] = left + "," + right;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;

    auto outDs = gis::core::openRaster(output, true);
    ASSERT_NE(outDs, nullptr);
    EXPECT_EQ(outDs->GetRasterXSize(), 20);
    EXPECT_EQ(outDs->GetRasterYSize(), 6);
    EXPECT_NEAR(readRasterPixel(output, 2, 2), 10.0f, 1e-4f);
    EXPECT_NEAR(readRasterPixel(output, 15, 2), 30.0f, 1e-4f);
}

TEST_F(PluginTest, CuttingMergeBandsCreatesMultiBandRaster) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    const std::string input1 = createTestRaster("cutting_merge_band1.tif", 24, 24, 1);
    const std::string input2 = createTestRaster("cutting_merge_band2.tif", 24, 24, 1);
    const std::string output = utf8PathString(getTestDir() / "cutting_merge_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("merge_bands");
    params["input"] = input1 + "," + input2;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;

    auto outDs = gis::core::openRaster(output, true);
    ASSERT_NE(outDs, nullptr);
    EXPECT_EQ(outDs->GetRasterCount(), 2);
}

TEST_F(PluginTest, CuttingMergeBandsCombinesInputAndBandsList) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    const std::string input1 = createTestRaster("cutting_merge_mixed_band1.tif", 24, 24, 1);
    const std::string input2 = createTestRaster("cutting_merge_mixed_band2.tif", 24, 24, 1);
    const std::string input3 = createTestRaster("cutting_merge_mixed_band3.tif", 24, 24, 1);
    const std::string output = utf8PathString(getTestDir() / "cutting_merge_mixed_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("merge_bands");
    params["input"] = input1;
    params["bands"] = input2 + "," + input3;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;

    auto outDs = gis::core::openRaster(output, true);
    ASSERT_NE(outDs, nullptr);
    EXPECT_EQ(outDs->GetRasterCount(), 3);
}

TEST_F(PluginTest, TerrainSlopeAspectAndHillshadeExecution) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    const std::string input = createTerrainRaster("terrain_input.tif");
    const std::string slopeOutput = utf8PathString(getTestDir() / "terrain_slope_output.tif");
    const std::string aspectOutput = utf8PathString(getTestDir() / "terrain_aspect_output.tif");
    const std::string hillshadeOutput = utf8PathString(getTestDir() / "terrain_hillshade_output.tif");
    const std::string tpiOutput = utf8PathString(getTestDir() / "terrain_tpi_output.tif");
    const std::string curvatureOutput = utf8PathString(getTestDir() / "terrain_curvature_output.tif");
    const std::string roughnessOutput = utf8PathString(getTestDir() / "terrain_roughness_output.tif");

    std::map<std::string, gis::framework::ParamValue> slopeParams;
    slopeParams["action"] = std::string("slope");
    slopeParams["input"] = input;
    slopeParams["output"] = slopeOutput;
    slopeParams["band"] = 1;
    slopeParams["z_factor"] = 1.0;

    std::map<std::string, gis::framework::ParamValue> aspectParams = slopeParams;
    aspectParams["action"] = std::string("aspect");
    aspectParams["output"] = aspectOutput;

    std::map<std::string, gis::framework::ParamValue> hillshadeParams = slopeParams;
    hillshadeParams["action"] = std::string("hillshade");
    hillshadeParams["output"] = hillshadeOutput;
    hillshadeParams["azimuth"] = 315.0;
    hillshadeParams["altitude"] = 45.0;

    std::map<std::string, gis::framework::ParamValue> tpiParams = slopeParams;
    tpiParams["action"] = std::string("tpi");
    tpiParams["output"] = tpiOutput;

    std::map<std::string, gis::framework::ParamValue> curvatureParams = slopeParams;
    curvatureParams["action"] = std::string("curvature");
    curvatureParams["output"] = curvatureOutput;

    std::map<std::string, gis::framework::ParamValue> roughnessParams = slopeParams;
    roughnessParams["action"] = std::string("roughness");
    roughnessParams["output"] = roughnessOutput;

    const auto slopeResult = p->execute(slopeParams, progress_);
    const auto aspectResult = p->execute(aspectParams, progress_);
    const auto hillshadeResult = p->execute(hillshadeParams, progress_);
    const auto tpiResult = p->execute(tpiParams, progress_);
    const auto curvatureResult = p->execute(curvatureParams, progress_);
    const auto roughnessResult = p->execute(roughnessParams, progress_);

    EXPECT_TRUE(slopeResult.success) << slopeResult.message;
    EXPECT_TRUE(aspectResult.success) << aspectResult.message;
    EXPECT_TRUE(hillshadeResult.success) << hillshadeResult.message;
    EXPECT_TRUE(tpiResult.success) << tpiResult.message;
    EXPECT_TRUE(curvatureResult.success) << curvatureResult.message;
    EXPECT_TRUE(roughnessResult.success) << roughnessResult.message;
    EXPECT_TRUE(fs::exists(slopeOutput));
    EXPECT_TRUE(fs::exists(aspectOutput));
    EXPECT_TRUE(fs::exists(hillshadeOutput));
    EXPECT_TRUE(fs::exists(tpiOutput));
    EXPECT_TRUE(fs::exists(curvatureOutput));
    EXPECT_TRUE(fs::exists(roughnessOutput));
    EXPECT_TRUE(std::isfinite(readRasterPixel(tpiOutput, 24, 24)));
    EXPECT_TRUE(std::isfinite(readRasterPixel(curvatureOutput, 24, 24)));
    EXPECT_GT(readRasterPixel(roughnessOutput, 24, 24), 0.01f);
}

TEST_F(PluginTest, TerrainFillSinksAndFlowDirectionExecution) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    const std::string sinkInput = createSinkRaster("terrain_sink_input.tif");
    const std::string flowInput = createEastDownhillRaster("terrain_flow_input.tif");
    const std::string fillOutput = utf8PathString(getTestDir() / "terrain_fill_sinks_output.tif");
    const std::string flowOutput = utf8PathString(getTestDir() / "terrain_flow_direction_output.tif");

    std::map<std::string, gis::framework::ParamValue> fillParams;
    fillParams["action"] = std::string("fill_sinks");
    fillParams["input"] = sinkInput;
    fillParams["output"] = fillOutput;
    fillParams["band"] = 1;
    fillParams["z_factor"] = 1.0;

    std::map<std::string, gis::framework::ParamValue> flowParams;
    flowParams["action"] = std::string("flow_direction");
    flowParams["input"] = flowInput;
    flowParams["output"] = flowOutput;
    flowParams["band"] = 1;
    flowParams["z_factor"] = 1.0;

    const auto fillResult = p->execute(fillParams, progress_);
    const auto flowResult = p->execute(flowParams, progress_);

    EXPECT_TRUE(fillResult.success) << fillResult.message;
    EXPECT_TRUE(flowResult.success) << flowResult.message;
    EXPECT_TRUE(fs::exists(fillOutput));
    EXPECT_TRUE(fs::exists(flowOutput));
    EXPECT_NEAR(readRasterPixel(fillOutput, 4, 4), 10.0f, 1e-4f);
    EXPECT_NEAR(readRasterPixel(flowOutput, 4, 4), 1.0f, 1e-4f);
}

TEST_F(PluginTest, TerrainFlowAccumulationExecution) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    const std::string input = createEastDownhillRaster("terrain_accumulation_input.tif");
    const std::string output = utf8PathString(getTestDir() / "terrain_flow_accumulation_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("flow_accumulation");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["z_factor"] = 1.0;

    const auto result = p->execute(params, progress_);

    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_NEAR(readRasterPixel(output, 1, 4), 1.0f, 1e-4f);
    EXPECT_NEAR(readRasterPixel(output, 4, 4), 4.0f, 1e-4f);
    EXPECT_NEAR(readRasterPixel(output, 7, 4), 7.0f, 1e-4f);
}

TEST_F(PluginTest, TerrainStreamExtractExecution) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    const std::string input = createEastDownhillRaster("terrain_stream_input.tif");
    const std::string output = utf8PathString(getTestDir() / "terrain_stream_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("stream_extract");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["z_factor"] = 1.0;
    params["accum_threshold"] = 4.0;

    const auto result = p->execute(params, progress_);

    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_NEAR(readRasterPixel(output, 3, 4), 0.0f, 1e-4f);
    EXPECT_NEAR(readRasterPixel(output, 4, 4), 1.0f, 1e-4f);
    EXPECT_NEAR(readRasterPixel(output, 7, 4), 1.0f, 1e-4f);
}

TEST_F(PluginTest, TerrainWatershedExecution) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    const std::string input = createEastDownhillRaster("terrain_watershed_input.tif");
    const std::string output = utf8PathString(getTestDir() / "terrain_watershed_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("watershed");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["z_factor"] = 1.0;

    const auto result = p->execute(params, progress_);

    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_NEAR(readRasterPixel(output, 1, 4), readRasterPixel(output, 7, 4), 1e-4f);
    EXPECT_NE(readRasterPixel(output, 4, 3), readRasterPixel(output, 4, 4));
}

TEST_F(PluginTest, TerrainProfileExtractExecution) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    const std::string input = createTerrainRaster("terrain_profile_input.tif");
    const std::string output = utf8PathString(getTestDir() / "terrain_profile_output.csv");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("profile_extract");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["profile_path"] = std::string("116.001,39.999;116.010,39.992;116.020,39.985");

    const auto result = p->execute(params, progress_);

    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    ASSERT_TRUE(result.metadata.count("sample_count"));
    EXPECT_GT(std::stoi(result.metadata.at("sample_count")), 2);

    const std::string content = readTextFile(output);
    EXPECT_NE(content.find("distance,x,y,elevation"), std::string::npos);
    EXPECT_NE(content.find("116.001"), std::string::npos);
}

TEST_F(PluginTest, TerrainViewshedExecution) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    const std::string input = createConstantRaster("terrain_viewshed_input.tif", 32, 32, 10.0f);
    const std::string output = utf8PathString(getTestDir() / "terrain_viewshed_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("viewshed");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["observer_x"] = 116.016;
    params["observer_y"] = 39.984;
    params["observer_height"] = 2.0;
    params["target_height"] = 0.0;
    params["max_distance"] = 0.0;

    const auto result = p->execute(params, progress_);

    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "viewshed");
    EXPECT_NEAR(readRasterPixel(output, 16, 16), 255.0f, 1e-4f);
    EXPECT_NEAR(readRasterPixel(output, 0, 0), 255.0f, 1e-4f);
}

TEST_F(PluginTest, TerrainViewshedMultiExecution) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    const std::string input = createConstantRaster("terrain_viewshed_multi_input.tif", 32, 32, 10.0f);
    const std::string output = utf8PathString(getTestDir() / "terrain_viewshed_multi_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("viewshed_multi");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["observer_points"] = std::string("116.006,39.994;116.026,39.974");
    params["observer_height"] = 2.0;
    params["target_height"] = 0.0;
    params["max_distance"] = 0.0;

    const auto result = p->execute(params, progress_);

    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "viewshed_multi");
    EXPECT_EQ(result.metadata.at("observer_count"), "2");
    EXPECT_NEAR(readRasterPixel(output, 5, 5), 255.0f, 1e-4f);
    EXPECT_NEAR(readRasterPixel(output, 25, 25), 255.0f, 1e-4f);
}

TEST_F(PluginTest, TerrainCutFillExecution) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    const std::string reference = createConstantRaster("terrain_cut_fill_reference.tif", 10, 10, 10.0f, 116.0, 40.0, 1.0);
    const std::string input = createConstantRaster("terrain_cut_fill_input.tif", 10, 10, 12.0f, 116.0, 40.0, 1.0);
    const std::string output = utf8PathString(getTestDir() / "terrain_cut_fill_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("cut_fill");
    params["reference"] = reference;
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;

    const auto result = p->execute(params, progress_);

    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "cut_fill");
    EXPECT_NEAR(readRasterPixel(output, 5, 5), 2.0f, 1e-4f);
    EXPECT_NEAR(std::stod(result.metadata.at("fill_volume")), 200.0, 1e-6);
    EXPECT_NEAR(std::stod(result.metadata.at("cut_volume")), 0.0, 1e-6);
    EXPECT_NEAR(std::stod(result.metadata.at("net_volume")), 200.0, 1e-6);
}

TEST_F(PluginTest, TerrainReservoirVolumeExecution) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    const std::string input = createConstantRaster("terrain_reservoir_input.tif", 10, 10, 10.0f, 116.0, 40.0, 1.0);
    const std::string output = utf8PathString(getTestDir() / "terrain_reservoir_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("reservoir_volume");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["water_level"] = 13.0;

    const auto result = p->execute(params, progress_);

    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "reservoir_volume");
    EXPECT_NEAR(readRasterPixel(output, 5, 5), 3.0f, 1e-4f);
    EXPECT_NEAR(std::stod(result.metadata.at("reservoir_area")), 100.0, 1e-6);
    EXPECT_NEAR(std::stod(result.metadata.at("reservoir_volume")), 300.0, 1e-6);
}

TEST_F(PluginTest, MatchingDetectExecutionWritesJsonAndMetadata) {
    auto* p = mgr_.find("matching");
    ASSERT_NE(p, nullptr);

    const std::string input = createPatternRaster("matching_detect_input.tif");
    const std::string output = utf8PathString(getTestDir() / "matching_detect_output.json");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("detect");
    params["input"] = input;
    params["output"] = output;
    params["method"] = std::string("orb");
    params["max_points"] = 200;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    ASSERT_TRUE(result.metadata.count("keypoint_count"));
    EXPECT_GT(std::stoi(result.metadata.at("keypoint_count")), 0);

    std::ifstream ifs(output);
    ASSERT_TRUE(ifs.is_open());
    const std::string content((std::istreambuf_iterator<char>(ifs)),
        std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("\"count\":"), std::string::npos);
}

TEST_F(PluginTest, MatchingMatchExecutionWritesJsonAndMetadata) {
    auto* p = mgr_.find("matching");
    ASSERT_NE(p, nullptr);

    const std::string reference = createPatternRaster("matching_match_ref.tif");
    const std::string input = reference;
    const std::string output = utf8PathString(getTestDir() / "matching_match_output.json");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("match");
    params["reference"] = reference;
    params["input"] = input;
    params["output"] = output;
    params["method"] = std::string("orb");
    params["match_method"] = std::string("bf");
    params["max_points"] = 200;
    params["ratio_test"] = 0.95;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    ASSERT_TRUE(result.metadata.count("match_count"));
    EXPECT_GT(std::stoi(result.metadata.at("match_count")), 0);
}

TEST_F(PluginTest, MatchingCornerExecutionWritesJsonAndMetadata) {
    auto* p = mgr_.find("matching");
    ASSERT_NE(p, nullptr);

    const std::string input = createPatternRaster("matching_corner_input.tif");
    const std::string output = utf8PathString(getTestDir() / "matching_corner_output.json");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("corner");
    params["input"] = input;
    params["output"] = output;
    params["corner_method"] = std::string("shi_tomasi");
    params["max_corners"] = 50;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    ASSERT_TRUE(result.metadata.count("corner_count"));
    EXPECT_GT(std::stoi(result.metadata.at("corner_count")), 0);
}

TEST_F(PluginTest, MatchingChangeRejectsMismatchedRasterSizes) {
    auto* p = mgr_.find("matching");
    ASSERT_NE(p, nullptr);

    const std::string reference = createTestRaster("matching_change_ref.tif", 20, 20);
    const std::string input = createTestRaster("matching_change_input.tif", 28, 20);
    const std::string output = utf8PathString(getTestDir() / "matching_change_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("change");
    params["reference"] = reference;
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("Image sizes do not match"), std::string::npos);
}

TEST_F(PluginTest, MatchingChangeExecutionReportsChangedPixels) {
    auto* p = mgr_.find("matching");
    ASSERT_NE(p, nullptr);

    const std::string reference = createConstantRaster("matching_change_success_ref.tif", 20, 20, 10.0f);
    const std::string input = createConstantRaster("matching_change_success_input.tif", 20, 20, 10.0f);
    fillRasterRect(input, 5, 6, 4, 3, 80.0f);
    const std::string output = utf8PathString(getTestDir() / "matching_change_success_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("change");
    params["reference"] = reference;
    params["input"] = input;
    params["output"] = output;
    params["change_method"] = std::string("differencing");
    params["threshold"] = 1.0;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    ASSERT_TRUE(result.metadata.count("changed_pixels"));
    ASSERT_TRUE(result.metadata.count("change_ratio"));
    EXPECT_EQ(std::stoi(result.metadata.at("changed_pixels")), 12);
    EXPECT_NEAR(std::stod(result.metadata.at("change_ratio")), 12.0 / 400.0, 1e-6);
}

TEST_F(PluginTest, MatchingRegisterExecutionProducesAlignedOutput) {
    auto* p = mgr_.find("matching");
    ASSERT_NE(p, nullptr);

    const std::string reference = createPatternRaster("matching_register_ref.tif");
    const std::string input = reference;
    const std::string output = utf8PathString(getTestDir() / "matching_register_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("register");
    params["reference"] = reference;
    params["input"] = input;
    params["output"] = output;
    params["method"] = std::string("orb");
    params["match_method"] = std::string("bf");
    params["transform"] = std::string("affine");
    params["max_points"] = 200;
    params["ratio_test"] = 0.95;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    ASSERT_TRUE(result.metadata.count("match_count"));
    EXPECT_GE(std::stoi(result.metadata.at("match_count")), 4);

    auto outDs = gis::core::openRaster(output, true);
    ASSERT_NE(outDs, nullptr);
    EXPECT_EQ(outDs->GetRasterXSize(), 96);
    EXPECT_EQ(outDs->GetRasterYSize(), 96);
}

TEST_F(PluginTest, MatchingPluginDebugLightweightActionsFailFast) {
    auto* p = mgr_.find("matching");
    ASSERT_NE(p, nullptr);

    std::map<std::string, gis::framework::ParamValue> eccParams;
    eccParams["action"] = std::string("ecc_register");

    std::map<std::string, gis::framework::ParamValue> stitchParams;
    stitchParams["action"] = std::string("stitch");

    const auto eccResult = p->execute(eccParams, progress_);
    const auto stitchResult = p->execute(stitchParams, progress_);

#ifdef _DEBUG
    EXPECT_FALSE(eccResult.success);
    EXPECT_NE(eccResult.message.find("Debug lightweight mode"), std::string::npos);
    EXPECT_FALSE(stitchResult.success);
    EXPECT_NE(stitchResult.message.find("Debug lightweight mode"), std::string::npos);
#else
    EXPECT_FALSE(eccResult.success);
    EXPECT_NE(eccResult.message.find("reference is required"), std::string::npos);
    EXPECT_FALSE(stitchResult.success);
    EXPECT_NE(stitchResult.message.find("input is required"), std::string::npos);
#endif
}
