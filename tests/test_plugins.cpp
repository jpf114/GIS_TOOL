#include <gtest/gtest.h>
#include <gis/framework/plugin.h>
#include <gis/framework/plugin_manager.h>
#include <gis/framework/param_spec.h>
#include <gis/framework/result.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/progress.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_error.h>
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

TEST_F(PluginTest, UtilityPluginParams) {
    auto* p = mgr_.find("utility");
    if (p) {
        auto specs = p->paramSpecs();
        bool hasAction = false;
        for (auto& s : specs) {
            if (s.key == "action") hasAction = true;
        }
        EXPECT_TRUE(hasAction);
    }
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

TEST_F(PluginTest, UtilityInfoExecution) {
    auto* p = mgr_.find("utility");
    if (!p) GTEST_SKIP() << "utility plugin not loaded";

    std::string input = createTestRaster("e2e_util_info_input.tif", 30, 30);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("info");
    params["input"] = input;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Utility info failed: " << result.message;
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

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Buffer failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata["feature_count"], "200");
    EXPECT_EQ(result.metadata["distance"], "25");
    EXPECT_EQ(result.metadata["srs_type"], "projected");
    EXPECT_EQ(result.metadata["output_format"], "GPKG");
    EXPECT_TRUE(result.metadata.count("elapsed_ms") > 0);

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
    EXPECT_EQ(result.metadata["feature_count"], "3");
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

TEST_F(PluginTest, ProcessingBandMathExecution) {
    auto* p = mgr_.find("processing");
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
