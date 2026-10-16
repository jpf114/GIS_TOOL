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
#include <QJsonDocument>
#include <QJsonObject>
#include <opencv2/core.hpp>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "raster_fixture.h"
#include "test_support.h"
// R-T7-9 的用例要核对展示层的 per-action 白名单；gis_tests 把 gui_data_support.cpp 编进同一个
// 可执行文件，gui 头文件不在 include 路径中，故沿用 test_coverage_meta.cpp:5 的相对路径写法。
#include "../src/gui/gui_data_support.h"

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

// 每个波段沿 x 方向做线性梯度：value(x, y) = base + step * x（同一列上所有行取值相同）。
// Task 3 fix round 1（F5）加它的原因：常量栅格让 brightValue == darkValue，percentile_stretch
// 会走 georef_plugin.cpp:885-890 的 cv::Mat::zeros 退化分支，拉伸换算 :887 从未被执行。
// 8x8 时 1% 分位落在 sorted[1]（也就是 x=0 那一列）、99% 分位落在 sorted[62]（x=7 那一列），
// 于是分母 = |step * 7| != 0，换算分支真的被执行，输出只取决于 x：
//   band = {base, step}: out(x) = clamp((base + step*x - dark) / (bright - dark))。
static std::string createMultiBandGradientRaster(const std::string& name,
                                                 int w,
                                                 int h,
                                                 const std::vector<std::pair<float, float>>& baseAndStep,
                                                 double originX = 116.0,
                                                 double originY = 40.0,
                                                 double pixelSize = 0.001) {
    std::string path = (getTestDir() / name).string();
    auto ds = gis::core::createRaster(path, w, h, static_cast<int>(baseAndStep.size()), GDT_Float32);
    double adfGT[6] = {originX, pixelSize, 0.0, originY, 0.0, -pixelSize};
    ds->SetGeoTransform(adfGT);

    for (size_t bandIndex = 0; bandIndex < baseAndStep.size(); ++bandIndex) {
        const float base = baseAndStep[bandIndex].first;
        const float step = baseAndStep[bandIndex].second;
        std::vector<float> data(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                data[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                    base + step * static_cast<float>(x);
            }
        }
        auto* band = ds->GetRasterBand(static_cast<int>(bandIndex) + 1);
        band->RasterIO(GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0, 0);
        band->FlushCache();
    }

    return path;
}

static std::string createTwoClassTrainingRaster(const std::string& name, int w = 24, int h = 12) {
    std::string path = (getTestDir() / name).string();
    auto ds = gis::core::createRaster(path, w, h, 2, GDT_Float32);
    double adfGT[6] = {116.0, 0.001, 0.0, 40.0, 0.0, -0.001};
    ds->SetGeoTransform(adfGT);

    std::vector<float> band1(w * h, 0.0f);
    std::vector<float> band2(w * h, 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool leftClass = x < (w / 2);
            band1[y * w + x] = leftClass ? 10.0f : 200.0f;
            band2[y * w + x] = leftClass ? 20.0f : 180.0f;
        }
    }

    ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, w, h, band1.data(), w, h, GDT_Float32, 0, 0);
    ds->GetRasterBand(2)->RasterIO(GF_Write, 0, 0, w, h, band2.data(), w, h, GDT_Float32, 0, 0);
    ds->GetRasterBand(1)->FlushCache();
    ds->GetRasterBand(2)->FlushCache();
    return path;
}

static std::string createSvmTrainingCsv(const std::string& name) {
    const std::string path = utf8PathString(getTestDir() / name);
    std::ofstream ofs(path, std::ios::binary);
    ofs << "label,b1,b2\n";
    ofs << "1,10,20\n";
    ofs << "1,12,18\n";
    ofs << "2,200,180\n";
    ofs << "2,195,185\n";
    return path;
}

static std::string createGcpCsv(const std::string& name) {
    const std::string path = utf8PathString(getTestDir() / name);
    std::ofstream ofs(path, std::ios::binary);
    ofs << "pixel_x,pixel_y,map_x,map_y\n";
    ofs << "0,0,120,30\n";
    ofs << "9,0,129,30\n";
    ofs << "0,5,120,25\n";
    ofs << "9,5,129,25\n";
    return path;
}

static std::string createRadiometricMetadataFile(const std::string& name) {
    const std::string path = utf8PathString(getTestDir() / name);
    std::ofstream ofs(path, std::ios::binary);
    ofs << "RADIANCE_MULT_BAND_1 = 3.0\n";
    ofs << "RADIANCE_ADD_BAND_1 = 2.0\n";
    return path;
}

static std::string createRpcTestRaster(const std::string& name) {
    const int w = 10;
    const int h = 6;
    const std::string path = utf8PathString(getTestDir() / name);
    auto ds = gis::core::createRaster(path, w, h, 1, GDT_Float32);

    std::vector<float> data(w * h, 9.0f);
    auto* band = ds->GetRasterBand(1);
    EXPECT_NE(band, nullptr);
    EXPECT_EQ(band->RasterIO(GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0, 0), CE_None);
    band->FlushCache();

    EXPECT_EQ(ds->SetMetadataItem("ERR_BIAS", "0", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem("ERR_RAND", "0", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem("LINE_OFF", "2.5", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem("SAMP_OFF", "4.5", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem("LAT_OFF", "29.9975", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem("LONG_OFF", "120.0045", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem("HEIGHT_OFF", "0", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem("LINE_SCALE", "2.5", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem("SAMP_SCALE", "4.5", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem("LAT_SCALE", "0.0025", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem("LONG_SCALE", "0.0045", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem("HEIGHT_SCALE", "1", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem(
        "LINE_NUM_COEFF",
        "0 0 -1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem(
        "LINE_DEN_COEFF",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem(
        "SAMP_NUM_COEFF",
        "0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", "RPC"), CE_None);
    EXPECT_EQ(ds->SetMetadataItem(
        "SAMP_DEN_COEFF",
        "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", "RPC"), CE_None);
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

static float readRasterPixel(const std::string& path, int bandIndex, int x, int y) {
    auto ds = gis::core::openRaster(path, true);
    EXPECT_NE(ds, nullptr);
    auto* band = ds->GetRasterBand(bandIndex);
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
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->name(), "projection");
    EXPECT_FALSE(p->paramSpecs().empty());
}

TEST_F(PluginTest, ProjectionPluginParams) {
    auto* p = mgr_.find("projection");
    ASSERT_NE(p, nullptr);
    auto specs = p->paramSpecs();
    bool hasAction = false;
    for (auto& s : specs) {
        if (s.key == "action") hasAction = true;
    }
    EXPECT_TRUE(hasAction);
}

TEST_F(PluginTest, ProjectionVectorReprojectExecution) {
    auto* p = mgr_.find("projection");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "projection_vector_input.geojson");
    const std::string output = utf8PathString(getTestDir() / "projection_vector_output.gpkg");

    std::filesystem::remove(std::filesystem::path(input));
    std::filesystem::remove(std::filesystem::path(output));

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
    ASSERT_NE(p, nullptr);
    auto specs = p->paramSpecs();
    EXPECT_GE(specs.size(), 5u);
}

TEST_F(PluginTest, RasterRenderPluginParams) {
    auto* p = mgr_.find("raster_render");
    ASSERT_NE(p, nullptr);
    auto specs = p->paramSpecs();
    bool hasAction = false;
    for (auto& s : specs) {
        if (s.key == "action") hasAction = true;
    }
    EXPECT_TRUE(hasAction);
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

TEST_F(PluginTest, ClassificationSupervisedParamsDoNotRequireFeatureStatsInputs) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    const auto specs = p->paramSpecs();
    const std::map<std::string, gis::framework::ParamValue> params = {
        {"action", std::string("svm_classify")},
        {"input", std::string("dummy_input.tif")},
        {"training_csv", std::string("dummy_samples.csv")},
        {"output", std::string("dummy_output.tif")},
        {"bands", std::string("1,2")}
    };

    EXPECT_TRUE(gis::framework::validateParams(specs, params).empty());
}

TEST_F(PluginTest, ProcessingPluginParams) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);
    auto specs = p->paramSpecs();
    bool hasEdgeAction = false;
    for (auto& s : specs) {
        if (s.key == "edge_method") hasEdgeAction = true;
    }
    EXPECT_TRUE(hasEdgeAction);
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

TEST_F(PluginTest, ClassificationSvmExecution) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    const std::string input = createTwoClassTrainingRaster("classification_svm_input.tif");
    const std::string trainingCsv = createSvmTrainingCsv("classification_svm_samples.csv");
    const std::string output = utf8PathString(getTestDir() / "classification_svm_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("svm_classify");
    params["input"] = input;
    params["training_csv"] = trainingCsv;
    params["output"] = output;
    params["bands"] = std::string("1,2");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "svm_classify");
    EXPECT_EQ(result.metadata.at("sample_count"), "4");
    EXPECT_EQ(result.metadata.at("class_count"), "2");
    EXPECT_NEAR(readRasterPixel(output, 2, 2), 1.0f, 1e-4f);
    EXPECT_NEAR(readRasterPixel(output, 20, 2), 2.0f, 1e-4f);
}

TEST_F(PluginTest, ClassificationRandomForestExecution) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    const std::string input = createTwoClassTrainingRaster("classification_rf_input.tif");
    const std::string trainingCsv = createSvmTrainingCsv("classification_rf_samples.csv");
    const std::string output = utf8PathString(getTestDir() / "classification_rf_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("random_forest_classify");
    params["input"] = input;
    params["training_csv"] = trainingCsv;
    params["output"] = output;
    params["bands"] = std::string("1,2");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "random_forest_classify");
    EXPECT_EQ(result.metadata.at("sample_count"), "4");
    EXPECT_EQ(result.metadata.at("class_count"), "2");
    EXPECT_NEAR(readRasterPixel(output, 2, 2), 1.0f, 1e-4f);
    EXPECT_NEAR(readRasterPixel(output, 20, 2), 2.0f, 1e-4f);
}

TEST_F(PluginTest, ClassificationMaxLikelihoodExecution) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    const std::string input = createTwoClassTrainingRaster("classification_ml_input.tif");
    const std::string trainingCsv = createSvmTrainingCsv("classification_ml_samples.csv");
    const std::string output = utf8PathString(getTestDir() / "classification_ml_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("max_likelihood_classify");
    params["input"] = input;
    params["training_csv"] = trainingCsv;
    params["output"] = output;
    params["bands"] = std::string("1,2");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "max_likelihood_classify");
    EXPECT_EQ(result.metadata.at("sample_count"), "4");
    EXPECT_EQ(result.metadata.at("class_count"), "2");
    EXPECT_NEAR(readRasterPixel(output, 2, 2), 1.0f, 1e-4f);
    EXPECT_NEAR(readRasterPixel(output, 20, 2), 2.0f, 1e-4f);
}

TEST_F(PluginTest, GeorefDosCorrectionExecution) {
    auto* p = mgr_.find("georef");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("georef_dos_input.tif", 16, 16);
    const std::string output = utf8PathString(getTestDir() / "georef_dos_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("dos_correction");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "dos_correction");
    EXPECT_NEAR(readRasterPixel(output, 5, 0), 5.0f / 255.0f, 1e-4f);
}

TEST_F(PluginTest, GeorefRadiometricCalibrationExecution) {
    auto* p = mgr_.find("georef");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("georef_radiometric_input.tif", 16, 16);
    const std::string output = utf8PathString(getTestDir() / "georef_radiometric_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("radiometric_calibration");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["gain"] = 2.0;
    params["offset"] = 5.0;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "radiometric_calibration");
    EXPECT_NEAR(readRasterPixel(output, 5, 0), 15.0f, 1e-4f);
}

TEST_F(PluginTest, GeorefRadiometricCalibrationExecutionWithMetadataFile) {
    auto* p = mgr_.find("georef");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("georef_radiometric_metadata_input.tif", 16, 16);
    const std::string metadataFile = createRadiometricMetadataFile("georef_radiometric_metadata.txt");
    const std::string output = utf8PathString(getTestDir() / "georef_radiometric_metadata_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("radiometric_calibration");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["metadata_file"] = metadataFile;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "radiometric_calibration");
    EXPECT_EQ(result.metadata.at("metadata_file"), metadataFile);
    EXPECT_NEAR(readRasterPixel(output, 5, 0), 17.0f, 1e-4f);
}

TEST_F(PluginTest, GeorefGcpRegisterExecution) {
    auto* p = mgr_.find("georef");
    ASSERT_NE(p, nullptr);

    const std::string input = createConstantRaster("georef_gcp_input.tif", 10, 6, 7.0f, 120.0, 30.0, 1.0);
    const std::string gcpFile = createGcpCsv("georef_gcps.csv");
    const std::string output = utf8PathString(getTestDir() / "georef_gcp_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("gcp_register");
    params["input"] = input;
    params["output"] = output;
    params["gcp_file"] = gcpFile;
    params["dst_srs"] = std::string("EPSG:4326");
    params["resample"] = std::string("nearest");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "gcp_register");
    EXPECT_EQ(result.metadata.at("gcp_count"), "4");
    EXPECT_NEAR(readRasterPixel(output, 2, 2), 7.0f, 1e-4f);

    auto ds = gis::core::openRaster(output, true);
    ASSERT_NE(ds, nullptr);

    double geotransform[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    ASSERT_EQ(ds->GetGeoTransform(geotransform), CE_None);
    EXPECT_NEAR(geotransform[0], 120.0, 1e-6);
    EXPECT_NEAR(geotransform[1], 1.0, 1e-6);
    EXPECT_NEAR(geotransform[3], 30.0, 1e-6);
    EXPECT_NEAR(geotransform[5], -1.0, 1e-6);

    OGRSpatialReference srs;
    ASSERT_EQ(srs.SetFromUserInput(ds->GetProjectionRef()), OGRERR_NONE);
    const char* authName = srs.GetAuthorityName(nullptr);
    const char* authCode = srs.GetAuthorityCode(nullptr);
    ASSERT_NE(authName, nullptr);
    ASSERT_NE(authCode, nullptr);
    EXPECT_STREQ(authName, "EPSG");
    EXPECT_STREQ(authCode, "4326");
}

TEST_F(PluginTest, GeorefCosineCorrectionExecution) {
    auto* p = mgr_.find("georef");
    ASSERT_NE(p, nullptr);

    const std::string input = createConstantRaster("georef_cosine_input.tif", 10, 6, 10.0f);
    const std::string slope = createConstantRaster("georef_cosine_slope.tif", 10, 6, 60.0f);
    const std::string aspect = createConstantRaster("georef_cosine_aspect.tif", 10, 6, 90.0f);
    const std::string output = utf8PathString(getTestDir() / "georef_cosine_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("cosine_correction");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["slope_raster"] = slope;
    params["aspect_raster"] = aspect;
    params["sun_zenith_deg"] = 30.0;
    params["sun_azimuth_deg"] = 180.0;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "cosine_correction");
    EXPECT_NEAR(readRasterPixel(output, 3, 3), 20.0f, 1e-3f);
}

TEST_F(PluginTest, GeorefMinnaertCorrectionExecution) {
    auto* p = mgr_.find("georef");
    ASSERT_NE(p, nullptr);

    const std::string input = createConstantRaster("georef_minnaert_input.tif", 10, 6, 10.0f);
    const std::string slope = createConstantRaster("georef_minnaert_slope.tif", 10, 6, 60.0f);
    const std::string aspect = createConstantRaster("georef_minnaert_aspect.tif", 10, 6, 90.0f);
    const std::string output = utf8PathString(getTestDir() / "georef_minnaert_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("minnaert_correction");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["slope_raster"] = slope;
    params["aspect_raster"] = aspect;
    params["sun_zenith_deg"] = 30.0;
    params["sun_azimuth_deg"] = 180.0;
    params["minnaert_k"] = 0.5;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "minnaert_correction");
    EXPECT_NEAR(readRasterPixel(output, 3, 3), 10.0f * std::sqrt(2.0f), 1e-3f);
}

TEST_F(PluginTest, GeorefCCorrectionExecution) {
    auto* p = mgr_.find("georef");
    ASSERT_NE(p, nullptr);

    const std::string input = createConstantRaster("georef_c_input.tif", 10, 6, 10.0f);
    const std::string slope = createConstantRaster("georef_c_slope.tif", 10, 6, 60.0f);
    const std::string aspect = createConstantRaster("georef_c_aspect.tif", 10, 6, 90.0f);
    const std::string output = utf8PathString(getTestDir() / "georef_c_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("c_correction");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["slope_raster"] = slope;
    params["aspect_raster"] = aspect;
    params["sun_zenith_deg"] = 30.0;
    params["sun_azimuth_deg"] = 180.0;
    params["c_value"] = 0.1;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "c_correction");
    EXPECT_NEAR(readRasterPixel(output, 3, 3), 10.0f * (0.8660254f + 0.1f) / (0.4330127f + 0.1f), 1e-3f);
}

TEST_F(PluginTest, GeorefQuacCorrectionExecution) {
    auto* p = mgr_.find("georef");
    ASSERT_NE(p, nullptr);

    const std::string input = createMultiBandConstantRaster(
        "georef_quac_input.tif", 8, 8, {20.0f, 60.0f, 100.0f});
    const std::string output = utf8PathString(getTestDir() / "georef_quac_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("percentile_stretch");
    params["input"] = input;
    params["output"] = output;
    params["dark_percentile"] = 1.0;
    params["bright_percentile"] = 99.0;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "percentile_stretch");
    EXPECT_EQ(result.metadata.at("band_count"), "3");

    auto ds = gis::core::openRaster(output, true);
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->GetRasterCount(), 3);
    EXPECT_NEAR(readRasterPixel(output, 1, 2, 2), 0.0f, 1e-6f);
    EXPECT_NEAR(readRasterPixel(output, 2, 2, 2), 0.0f, 1e-6f);
    EXPECT_NEAR(readRasterPixel(output, 3, 2, 2), 0.0f, 1e-6f);

    // F5（Task 3 fix round 1）：上面那个夹具每波段都是常量，darkPercentile 与 brightPercentile
    // 取到同一个值，插件走 georef_plugin.cpp:885-890 的 cv::Mat::zeros 退化分支，所以那三条
    // EXPECT_NEAR 钉住的是"退化分支写 0"，percentile_stretch 的换算 :887 一次都没被执行过。
    // 下面换成**分位数互不相同**的波段，把换算公式本身钉住（out = (v - dark) / (bright - dark)）：
    //   band 1: value(x) = 20 + 10*x  -> dark=20 (x=0 列), bright=90 (x=7 列), out(x) = x / 7
    //   band 2: value(x) = 100 - 10*x -> dark=30 (x=7 列), bright=100 (x=0 列), out(x) = 1 - x / 7
    // band 2 沿 x 递减，因此它同时钉住"上下界是按像元取值算出来的、不是按列号取的"。
    const std::string rampInput = createMultiBandGradientRaster(
        "georef_quac_ramp_input.tif", 8, 8, {{20.0f, 10.0f}, {100.0f, -10.0f}});
    const std::string rampOutput = utf8PathString(getTestDir() / "georef_quac_ramp_output.tif");

    std::map<std::string, gis::framework::ParamValue> rampParams;
    rampParams["action"] = std::string("percentile_stretch");
    rampParams["input"] = rampInput;
    rampParams["output"] = rampOutput;
    rampParams["dark_percentile"] = 1.0;
    rampParams["bright_percentile"] = 99.0;

    const auto rampResult = p->execute(rampParams, progress_);
    EXPECT_TRUE(rampResult.success) << rampResult.message;
    ASSERT_TRUE(fs::exists(rampOutput));
    EXPECT_EQ(rampResult.metadata.at("action"), "percentile_stretch");

    // 下界像元 0、上界像元 1、中间像元线性插值：三条同时成立才可能不是退化分支写出来的。
    EXPECT_NEAR(readRasterPixel(rampOutput, 1, 0, 0), 0.0f, 1e-6f);
    EXPECT_NEAR(readRasterPixel(rampOutput, 1, 3, 0), 3.0f / 7.0f, 1e-5f);
    EXPECT_NEAR(readRasterPixel(rampOutput, 1, 7, 0), 1.0f, 1e-6f);
    EXPECT_NEAR(readRasterPixel(rampOutput, 2, 0, 0), 1.0f, 1e-6f);
    EXPECT_NEAR(readRasterPixel(rampOutput, 2, 3, 0), 4.0f / 7.0f, 1e-5f);
    EXPECT_NEAR(readRasterPixel(rampOutput, 2, 7, 0), 0.0f, 1e-6f);
}

TEST_F(PluginTest, GeorefRpcOrthorectifyExecution) {
    auto* p = mgr_.find("georef");
    ASSERT_NE(p, nullptr);

    const std::string input = createRpcTestRaster("georef_rpc_input.tif");
    const std::string output = utf8PathString(getTestDir() / "georef_rpc_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("rpc_orthorectify");
    params["input"] = input;
    params["output"] = output;
    params["dst_srs"] = std::string("EPSG:4326");
    params["rpc_height"] = 0.0;
    params["resample"] = std::string("nearest");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "rpc_orthorectify");

    auto ds = gis::core::openRaster(output, true);
    ASSERT_NE(ds, nullptr);
    OGRSpatialReference srs;
    ASSERT_EQ(srs.SetFromUserInput(ds->GetProjectionRef()), OGRERR_NONE);
    const char* authName = srs.GetAuthorityName(nullptr);
    const char* authCode = srs.GetAuthorityCode(nullptr);
    ASSERT_NE(authName, nullptr);
    ASSERT_NE(authCode, nullptr);
    EXPECT_STREQ(authName, "EPSG");
    EXPECT_STREQ(authCode, "4326");
    EXPECT_NEAR(readRasterPixel(output, 1, 1), 9.0f, 1e-4f);
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

TEST_F(PluginTest, ProcessingGaborFilterExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createPatternRaster("e2e_gabor_filter_input.tif");
    std::string output = (getTestDir() / "e2e_gabor_filter_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("gabor_filter");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["kernel_size"] = 9;
    params["sigma"] = 2.0;
    params["gabor_theta"] = 0.0;
    params["gabor_lambda"] = 6.0;
    params["gabor_gamma"] = 0.5;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Gabor filter failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "gabor_filter");
    EXPECT_GT(readRasterPixel(output, 26, 26), 0.0f);
}

TEST_F(PluginTest, ProcessingGlcmTextureExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createPatternRaster("e2e_glcm_texture_input.tif");
    std::string output = (getTestDir() / "e2e_glcm_texture_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("glcm_texture");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["kernel_size"] = 5;
    params["glcm_metric"] = std::string("contrast");
    params["glcm_levels"] = 8;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "GLCM texture failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "glcm_texture");
    cv::Mat texture = gis::core::readBandAsMat(output, 1);
    double minValue = 0.0;
    double maxValue = 0.0;
    cv::minMaxLoc(texture, &minValue, &maxValue);
    EXPECT_GT(maxValue, 0.0);
}

TEST_F(PluginTest, ProcessingMeanShiftSegmentExecution) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::string input = createPatternRaster("e2e_mean_shift_input.tif");
    std::string output = (getTestDir() / "e2e_mean_shift_output.tif").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("mean_shift_filter");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["spatial_radius"] = 8.0;
    params["color_radius"] = 16.0;
    params["pyramid_level"] = 1;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Mean Shift failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "mean_shift_filter");
    cv::Mat shifted = gis::core::readBandAsMat(output, 1);
    double minValue = 0.0;
    double maxValue = 0.0;
    cv::minMaxLoc(shifted, &minValue, &maxValue);
    EXPECT_GT(maxValue - minValue, 0.0);
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
    if (!p) {
        FAIL() << "plugin not loaded: raster_inspect";
    }

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
    ds.reset();

    // Task 3 / R-T3-3：band 的默认值在 GUI、CLI 和插件兜底三条链路上必须是同一个 0（全波段）。
    // 注意 GeoTIFF 只有一个 TIFFTAG_GDAL_NODATA（整幅一个值），GDAL 自己会警告
    // "Setting nodata to X on band 1, but band 2 has nodata at Y … will be used for all bands"，
    // 所以"只写第 1 波段"在文件里观察不到隔离，能区分两条分支的是插件回显的分支消息；
    // 逐波段的数值断言则用来证明 band=0 确实把三个波段都写上了。
    const std::string band1Input = createMultiBandConstantRaster(
        "e2e_util_nodata_band1_input.tif", 16, 16, {11.0f, 22.0f, 33.0f});

    std::map<std::string, gis::framework::ParamValue> singleBandParams;
    singleBandParams["action"] = std::string("nodata");
    singleBandParams["input"] = band1Input;
    singleBandParams["band"] = 1;
    singleBandParams["nodata_value"] = -77.0;

    const auto singleBand = p->execute(singleBandParams, progress_);
    ASSERT_TRUE(singleBand.success) << singleBand.message;
    EXPECT_NE(singleBand.message.find("for band 1"), std::string::npos)
        << "band=1 必须走单波段分支，实际消息: " << singleBand.message;
    {
        auto checkDs = gis::core::openRaster(band1Input, true);
        ASSERT_NE(checkDs, nullptr);
        EXPECT_EQ(checkDs->GetRasterCount(), 3);
        bool band1Set = false;
        EXPECT_NEAR(gis::core::getNoDataValue(checkDs.get(), 1, &band1Set), -77.0, 1e-9);
        EXPECT_TRUE(band1Set);
        checkDs.reset();
    }

    // 全新一幅 3 波段输入：只给 band=0（GUI 全默认提交发出的就是它），三个波段都要拿到 NoData。
    const std::string allBandsInput = createMultiBandConstantRaster(
        "e2e_util_nodata_all_bands_input.tif", 16, 16, {11.0f, 22.0f, 33.0f});

    std::map<std::string, gis::framework::ParamValue> allBandParams;
    allBandParams["action"] = std::string("nodata");
    allBandParams["input"] = allBandsInput;
    allBandParams["band"] = 0;
    allBandParams["nodata_value"] = -88.0;

    const auto allBands = p->execute(allBandParams, progress_);
    ASSERT_TRUE(allBands.success) << allBands.message;
    EXPECT_NE(allBands.message.find("for all 3 bands"), std::string::npos)
        << "band=0 必须走全波段分支，实际消息: " << allBands.message;
    {
        auto checkDs = gis::core::openRaster(allBandsInput, true);
        ASSERT_NE(checkDs, nullptr);
        for (int b = 1; b <= 3; ++b) {
            bool set = false;
            EXPECT_NEAR(gis::core::getNoDataValue(checkDs.get(), b, &set), -88.0, 1e-9)
                << "第 " << b << " 波段";
            EXPECT_TRUE(set) << "第 " << b << " 波段必须写上 NoData";
        }
        checkDs.reset();
    }

    // 不传 band 时的兜底也必须与声明的默认值一致（0 = 全波段），否则 GUI 与 CLI 的"默认"不同义。
    const std::string defaultInput = createMultiBandConstantRaster(
        "e2e_util_nodata_default_input.tif", 16, 16, {11.0f, 22.0f, 33.0f});

    std::map<std::string, gis::framework::ParamValue> noBandParams;
    noBandParams["action"] = std::string("nodata");
    noBandParams["input"] = defaultInput;
    noBandParams["nodata_value"] = -66.0;

    const auto noBand = p->execute(noBandParams, progress_);
    ASSERT_TRUE(noBand.success) << noBand.message;
    EXPECT_NE(noBand.message.find("for all 3 bands"), std::string::npos)
        << "缺省 band 必须等价于 band=0，实际消息: " << noBand.message;
    {
        auto checkDs = gis::core::openRaster(defaultInput, true);
        ASSERT_NE(checkDs, nullptr);
        for (int b = 1; b <= 3; ++b) {
            bool set = false;
            EXPECT_NEAR(gis::core::getNoDataValue(checkDs.get(), b, &set), -66.0, 1e-9)
                << "第 " << b << " 波段";
            EXPECT_TRUE(set) << "第 " << b << " 波段";
        }
        checkDs.reset();
    }

    // 声明出来的默认值要和代码兜底是同一个值（这是 Task 3 契约不变量的插件侧一半）。
    const auto pluginSpecs = p->paramSpecs();
    const gis::framework::ParamSpec* bandSpec = nullptr;
    for (const auto& spec : pluginSpecs) {
        if (spec.key == "band") bandSpec = &spec;
    }
    ASSERT_NE(bandSpec, nullptr);
    const auto* bandDefault = std::get_if<int>(&bandSpec->defaultValue);
    ASSERT_NE(bandDefault, nullptr);
    EXPECT_EQ(*bandDefault, 0) << "raster_manage 的 band 默认值必须是 0（全波段），与描述和兜底一致";
}

TEST_F(PluginTest, RasterManageCogExecution) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("e2e_util_cog_input.tif", 64, 64);
    const std::string output = utf8PathString(getTestDir() / "e2e_util_cog_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("cog");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("format"), "COG");

    auto ds = gis::core::openRaster(output, true);
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->GetRasterXSize(), 64);
    EXPECT_EQ(ds->GetRasterYSize(), 64);
    EXPECT_EQ(ds->GetRasterCount(), 1);
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

TEST_F(PluginTest, RasterRenderHistogramMatchExecution) {
    auto* p = mgr_.find("raster_render");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("e2e_util_hist_match_input.tif", 40, 40);
    const std::string reference = createTerrainRaster("e2e_util_hist_match_reference.tif", 40, 40);
    const std::string output = utf8PathString(getTestDir() / "e2e_util_hist_match_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("histogram_match");
    params["input"] = input;
    params["reference"] = reference;
    params["output"] = output;
    params["band"] = 1;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("action"), "histogram_match");
    EXPECT_NE(readRasterPixel(output, 10, 10), readRasterPixel(input, 10, 10));
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
        {"ndmi", "e2e_spindex_ndmi_output.tif", {{"nir_band", 4}, {"swir1_band", 5}}, -20.0f / 160.0f},
        {"ndwi", "e2e_spindex_ndwi_output.tif", {{"green_band", 2}, {"nir_band", 4}}, -50.0f / 90.0f},
        {"mndwi", "e2e_spindex_mndwi_output.tif", {{"green_band", 2}, {"swir1_band", 5}}, -70.0f / 110.0f},
        {"ndbi", "e2e_spindex_ndbi_output.tif", {{"swir1_band", 5}, {"nir_band", 4}}, 20.0f / 160.0f},
        {"bsi", "e2e_spindex_bsi_output.tif", {{"blue_band", 1}, {"red_band", 3}, {"nir_band", 4}, {"swir1_band", 5}}, 40.0f / 200.0f},
        {"arvi", "e2e_spindex_arvi_output.tif", {{"blue_band", 1}, {"red_band", 3}, {"nir_band", 4}}, 20.0f / 120.0f},
        {"nbr", "e2e_spindex_nbr_output.tif", {{"nir_band", 4}, {"swir2_band", 6}}, -40.0f / 180.0f},
        {"awei", "e2e_spindex_awei_output.tif", {{"green_band", 2}, {"nir_band", 4}, {"swir1_band", 5}, {"swir2_band", 6}}, -600.0f},
        {"ui", "e2e_spindex_ui_output.tif", {{"nir_band", 4}, {"swir2_band", 6}}, 40.0f / 180.0f},
        {"bi", "e2e_spindex_bi_output.tif", {{"red_band", 3}, {"nir_band", 4}}, 1.0f / 5785.6136f},
        {"savi", "e2e_spindex_savi_output.tif", {{"red_band", 3}, {"nir_band", 4}, {"l_value", 0.5}}, 60.0f / 100.5f},
        {"evi", "e2e_spindex_evi_output.tif", {{"blue_band", 1}, {"red_band", 3}, {"nir_band", 4}, {"g_value", 2.5}, {"c1", 6.0}, {"c2", 7.5}, {"l_value", 1.0}}, 100.0f / 176.0f},
        {"osavi", "e2e_spindex_osavi_output.tif", {{"red_band", 3}, {"nir_band", 4}}, 46.4f / 100.16f},
        {"evi2", "e2e_spindex_evi2_output.tif", {{"red_band", 3}, {"nir_band", 4}}, 100.0f / 143.0f},
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

TEST_F(PluginTest, SpindexCustomIndexNewPresetExecution) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);

    const std::string input = createMultiBandConstantRaster(
        "e2e_spindex_custom_index_new_preset_input.tif", 24, 24, {10.0f, 20.0f, 30.0f, 70.0f, 90.0f});
    const std::string output = utf8PathString(getTestDir() / "e2e_spindex_custom_index_new_preset_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("custom_index");
    params["input"] = input;
    params["output"] = output;
    params["preset"] = std::string("ndmi_alias");
    params["nir_band"] = 4;
    params["swir1_band"] = 5;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("preset"), "ndmi_alias");
    EXPECT_EQ(result.metadata.at("expression"), "(NIR-SWIR1)/(NIR+SWIR1)");
    EXPECT_NEAR(readRasterPixel(output, 5, 5), -20.0f / 160.0f, 1e-4f);
}

// Task 5：custom_index 接入表达式引擎后，除零必须被计数而不是静默写 0。
// 4 波段常量 B3(RED)=0.5、B4(NIR)=-0.5 ⇒ 分母 NIR+RED 恒为 0，16 个像元全部触发。
TEST_F(PluginTest, CustomIndexReportsDivideByZeroInsteadOfSilentZero) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);
    const std::string input = createMultiBandConstantRaster(
        "spi_divzero_input.tif", 4, 4, {10.0f, 20.0f, 0.5f, -0.5f});
    const std::string output = utf8PathString(getTestDir() / "spi_divzero_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("custom_index");
    params["input"] = input;
    params["output"] = output;
    params["expression"] = std::string("(NIR-RED)/(NIR+RED)");
    params["red_band"] = 3;
    params["nir_band"] = 4;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    const auto it = result.metadata.find("divide_by_zero_pixels");
    ASSERT_NE(it, result.metadata.end());
    EXPECT_EQ(it->second, "16");                       // 4x4 全部触发除零，必须被计数而非静默
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 0), 0.0f, 1e-6f);
}

// 接上新引擎后 custom_index 也必须显式失败。SWIR2 特意留在表外（R-T5-3）：
// 给 custom_index 补 SWIR2 会扩大可接受表达式集合、改变成败边界，是 spec 未授权的新行为，
// 所以这条用例同时是把"别名表不含 SWIR2"钉住的回归钉——将来谁补上 SWIR2，它当场变红。
TEST_F(PluginTest, CustomIndexWithUnknownVariableFailsExplicitly) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);
    const std::string input = createMultiBandConstantRaster(
        "spi_unknown_input.tif", 2, 2, {10.0f, 20.0f, 30.0f, 70.0f, 90.0f});
    const std::string output = utf8PathString(getTestDir() / "spi_unknown_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("custom_index");
    params["input"] = input;
    params["output"] = output;
    params["expression"] = std::string("SWIR2*2");

    const auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("SWIR2"), std::string::npos) << result.message;
}

// R-T5-4 的钳制保留断言：表达式不含 BLUE/GREEN/SWIR1 时，resolveOptionalAliasBand 用
// std::min(defaultValue, bandCount) 兜底（blue→1、green→2、swir1→5 在 4 波段上被钳到 4）。
// 丢掉这套钳制的话 swir1_band=5 会走进 getBandIndex 的越界抛出，本用例当场变红。
TEST_F(PluginTest, CustomIndexClampsUnusedAliasBands) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);
    const std::string input = createMultiBandConstantRaster(
        "spi_alias_clamp_input.tif", 4, 4, {10.0f, 20.0f, 30.0f, 70.0f});
    const std::string output = utf8PathString(getTestDir() / "spi_alias_clamp_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("custom_index");
    params["input"] = input;
    params["output"] = output;
    params["expression"] = std::string("(NIR-RED)/(NIR+RED)");
    params["red_band"] = 3;
    params["nir_band"] = 4;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.metadata.at("blue_band"), "1");
    EXPECT_EQ(result.metadata.at("green_band"), "2");
    EXPECT_EQ(result.metadata.at("swir1_band"), "4");   // 默认 5 被钳到 4 波段
    EXPECT_NEAR(readRasterPixel(output, 2, 2), 40.0f / 100.0f, 1e-4f);
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

TEST_F(PluginTest, ProjectionTransformParamsDoNotRequireInput) {
    auto* p = mgr_.find("projection");
    ASSERT_NE(p, nullptr);

    const auto specs = p->paramSpecs();
    const std::map<std::string, gis::framework::ParamValue> params = {
        {"action", std::string("transform")},
        {"src_srs", std::string("EPSG:4326")},
        {"dst_srs", std::string("EPSG:3857")},
        {"x", 116.0},
        {"y", 40.0}
    };

    EXPECT_TRUE(gis::framework::validateParams(specs, params).empty());
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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    params["kmeans_k"] = 3;
    params["kmeans_attempts"] = 3;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "KMeans failed: " << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_TRUE(result.metadata.count("compactness") > 0);
}

TEST_F(PluginTest, VectorDissolveExecution) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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

TEST_F(PluginTest, VectorIntersectRejectsMismatchedSrs) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    std::string input = (getTestDir() / "e2e_intersect_input.shp").string();
    std::string overlay = (getTestDir() / "e2e_intersect_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_intersect_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);

        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto srs1 = std::make_unique<OGRSpatialReference>();
        srs1->importFromEPSG(3857);
        auto* layer1 = ds1->CreateLayer("input", srs1.get(), wkbPolygon);
        ASSERT_NE(layer1, nullptr);
        auto* feat1 = OGRFeature::CreateFeature(layer1->GetLayerDefn());
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
        auto* feat2 = OGRFeature::CreateFeature(layer2->GetLayerDefn());
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
    params["action"] = std::string("intersect");
    params["input"] = input;
    params["overlay_vector"] = overlay;
    params["output"] = output;

    auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("CRS"), std::string::npos);
}

TEST_F(PluginTest, VectorIntersectOutputsOverlappedPolygon) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    std::string input = (getTestDir() / "e2e_intersect_polygon_input.shp").string();
    std::string overlay = (getTestDir() / "e2e_intersect_polygon_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_intersect_polygon_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);

        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer1 = ds1->CreateLayer("input", srs.get(), wkbPolygon);
        ASSERT_NE(layer1, nullptr);
        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer1->CreateField(&nameField), OGRERR_NONE);
        auto* feat1 = OGRFeature::CreateFeature(layer1->GetLayerDefn());
        OGRPolygon poly1;
        OGRLinearRing ring1;
        ring1.addPoint(0, 0); ring1.addPoint(100, 0); ring1.addPoint(100, 100); ring1.addPoint(0, 100); ring1.addPoint(0, 0);
        poly1.addRing(&ring1);
        feat1->SetGeometry(&poly1);
        feat1->SetField("name", "zone_a");
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);

        auto* ds2 = driver->Create(overlay.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto* layer2 = ds2->CreateLayer("overlay", srs.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);
        auto* feat2 = OGRFeature::CreateFeature(layer2->GetLayerDefn());
        OGRPolygon poly2;
        OGRLinearRing ring2;
        ring2.addPoint(50, 50); ring2.addPoint(150, 50); ring2.addPoint(150, 150); ring2.addPoint(50, 150); ring2.addPoint(50, 50);
        poly2.addRing(&ring2);
        feat2->SetGeometry(&poly2);
        ASSERT_EQ(layer2->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);
        GDALClose(ds2);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("intersect");
    params["input"] = input;
    params["overlay_vector"] = overlay;
    params["output"] = output;

    auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Intersect failed: " << result.message;
    EXPECT_NE(result.message.find("1 features"), std::string::npos);
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(), 1);
    EXPECT_EQ(wkbFlatten(outLayer->GetGeomType()), wkbMultiPolygon);

    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "zone_a");
    OGRGeometry* outGeom = outFeat->GetGeometryRef();
    ASSERT_NE(outGeom, nullptr);
    EXPECT_EQ(wkbFlatten(outGeom->getGeometryType()), wkbMultiPolygon);
    EXPECT_NEAR(outGeom->toMultiPolygon()->get_Area(), 2500.0, 1e-6);
    OGREnvelope envelope;
    outGeom->getEnvelope(&envelope);
    EXPECT_NEAR(envelope.MinX, 50.0, 1e-6);
    EXPECT_NEAR(envelope.MinY, 50.0, 1e-6);
    EXPECT_NEAR(envelope.MaxX, 100.0, 1e-6);
    EXPECT_NEAR(envelope.MaxY, 100.0, 1e-6);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorSpatialJoinRejectsMismatchedSrs) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    std::string input = (getTestDir() / "e2e_spatial_join_input.shp").string();
    std::string join = (getTestDir() / "e2e_spatial_join_join.shp").string();
    std::string output = (getTestDir() / "e2e_spatial_join_output.gpkg").string();

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);

        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto srs1 = std::make_unique<OGRSpatialReference>();
        srs1->importFromEPSG(3857);
        auto* layer1 = ds1->CreateLayer("input", srs1.get(), wkbPolygon);
        ASSERT_NE(layer1, nullptr);
        auto* feat1 = OGRFeature::CreateFeature(layer1->GetLayerDefn());
        OGRPolygon poly1;
        OGRLinearRing ring1;
        ring1.addPoint(0, 0);
        ring1.addPoint(100, 0);
        ring1.addPoint(100, 100);
        ring1.addPoint(0, 100);
        ring1.addPoint(0, 0);
        poly1.addRing(&ring1);
        feat1->SetGeometry(&poly1);
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);

        auto* ds2 = driver->Create(join.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto srs2 = std::make_unique<OGRSpatialReference>();
        srs2->importFromEPSG(4326);
        auto* layer2 = ds2->CreateLayer("join", srs2.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);
        auto* feat2 = OGRFeature::CreateFeature(layer2->GetLayerDefn());
        OGRPolygon poly2;
        OGRLinearRing ring2;
        ring2.addPoint(0, 0);
        ring2.addPoint(1, 0);
        ring2.addPoint(1, 1);
        ring2.addPoint(0, 1);
        ring2.addPoint(0, 0);
        poly2.addRing(&ring2);
        feat2->SetGeometry(&poly2);
        ASSERT_EQ(layer2->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);
        GDALClose(ds2);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("spatial_join");
    params["input"] = input;
    params["join_vector"] = join;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("CRS"), std::string::npos);
}

TEST_F(PluginTest, VectorSpatialJoinWritesJoinCountAndValue) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_spatial_join_input.gpkg");
    const std::string join = utf8PathString(getTestDir() / "e2e_spatial_join_join.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_spatial_join_output.gpkg");

    auto makeRect = [](double minX, double minY, double maxX, double maxY) {
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

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);

        auto* inputDs = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(inputDs, nullptr);
        OGRSpatialReference srs;
        ASSERT_EQ(srs.importFromEPSG(3857), OGRERR_NONE);
        auto* inputLayer = inputDs->CreateLayer("source", &srs, wkbPolygon, nullptr);
        ASSERT_NE(inputLayer, nullptr);
        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(inputLayer->CreateField(&nameField), OGRERR_NONE);

        auto* srcA = OGRFeature::CreateFeature(inputLayer->GetLayerDefn());
        srcA->SetField("name", "src_a");
        OGRPolygon srcAPoly = makeRect(0, 0, 100, 100);
        srcA->SetGeometry(&srcAPoly);
        ASSERT_EQ(inputLayer->CreateFeature(srcA), OGRERR_NONE);
        OGRFeature::DestroyFeature(srcA);

        auto* srcB = OGRFeature::CreateFeature(inputLayer->GetLayerDefn());
        srcB->SetField("name", "src_b");
        OGRPolygon srcBPoly = makeRect(150, 0, 250, 100);
        srcB->SetGeometry(&srcBPoly);
        ASSERT_EQ(inputLayer->CreateFeature(srcB), OGRERR_NONE);
        OGRFeature::DestroyFeature(srcB);
        GDALClose(inputDs);

        auto* joinDs = driver->Create(join.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(joinDs, nullptr);
        auto* joinLayer = joinDs->CreateLayer("join", &srs, wkbPolygon, nullptr);
        ASSERT_NE(joinLayer, nullptr);
        OGRFieldDefn labelField("label", OFTString);
        ASSERT_EQ(joinLayer->CreateField(&labelField), OGRERR_NONE);

        auto* joinFeat = OGRFeature::CreateFeature(joinLayer->GetLayerDefn());
        joinFeat->SetField("label", "join_a");
        OGRPolygon joinPoly = makeRect(50, 50, 120, 120);
        joinFeat->SetGeometry(&joinPoly);
        ASSERT_EQ(joinLayer->CreateFeature(joinFeat), OGRERR_NONE);
        OGRFeature::DestroyFeature(joinFeat);
        GDALClose(joinDs);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("spatial_join");
    params["input"] = input;
    params["join_vector"] = join;
    params["join_field"] = std::string("label");
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
    EXPECT_EQ(outLayer->GetFeatureCount(FALSE), 2);

    std::map<std::string, std::pair<int, std::string>> actual;
    outLayer->ResetReading();
    OGRFeature* outFeat = nullptr;
    while ((outFeat = outLayer->GetNextFeature()) != nullptr) {
        actual[outFeat->GetFieldAsString("name")] = {
            outFeat->GetFieldAsInteger("join_count"),
            outFeat->GetFieldAsString("join_value")
        };
        OGRFeature::DestroyFeature(outFeat);
    }

    ASSERT_EQ(actual.size(), 2u);
    EXPECT_EQ(actual["src_a"].first, 1);
    EXPECT_EQ(actual["src_a"].second, "join_a");
    EXPECT_EQ(actual["src_b"].first, 0);
    EXPECT_TRUE(actual["src_b"].second.empty());
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorClipRepairsInvalidOverlayGeometry) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

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

// --- Task A0：矢量几何所有权/释放路径的回归用例 -------------------------------
//
// 这些用例覆盖 cloneNormalizedGeometry（IsValid/MakeValid）产出的几何对象在
// clip / difference / repair / dissolve 中的所有权移交：写者
// （writeFeatureWithCopiedFieldsOwned 系列）一律接管传入几何，无论写入成功与否
// 都会在内部销毁它。调用方若在移交后再对同一裸指针 delete，就是二次释放，
// Debug 构建表现为非法释放异常，Release 构建表现为 0xc0000374（STATUS_HEAP_CORRUPTION）。

// 判断一条 CPL 错误消息是否为"驱动拒绝把 POINT 写进 MULTIPOINT 图层"。
// 实际消息形如：Attempt to write non-multipoint (POINT) geometry to multipoint shapefile.
// 两个 RejectedFeatureWrite 用例用它把"写入确实被拒绝"这一前提显式钉住（否则前提被
// Task 20 消除后，用例只是走正常路径、静默失去回归价值）。
static bool a0IsMultipointWriteRejection(const std::string& message) {
    std::string lowered;
    lowered.reserve(message.size());
    for (unsigned char c : message) {
        lowered.push_back(static_cast<char>(std::tolower(c)));
    }
    return lowered.find("attempt to write") != std::string::npos &&
           lowered.find("multipoint") != std::string::npos;
}

static std::string a0MakePointLayer(const std::string& name, double x, double y) {
    namespace fs = std::filesystem;
    const std::string path = utf8PathString(getTestDir() / name);
    auto* drv = GetGDALDriverManager()->GetDriverByName("GPKG");
    EXPECT_NE(drv, nullptr);
    auto* ds = drv->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    EXPECT_NE(ds, nullptr);
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);
    auto* layer = ds->CreateLayer("pts", srs.get(), wkbPoint, nullptr);
    EXPECT_NE(layer, nullptr);
    if (layer) {
        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        auto* point = new OGRPoint(x, y);
        feat->SetGeometryDirectly(point);
        EXPECT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
    }
    GDALClose(ds);
    return path;
}

static std::string a0MakeClipPolygonLayer(const std::string& name) {
    namespace fs = std::filesystem;
    const std::string path = utf8PathString(getTestDir() / name);
    auto* drv = GetGDALDriverManager()->GetDriverByName("GPKG");
    EXPECT_NE(drv, nullptr);
    auto* ds = drv->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    EXPECT_NE(ds, nullptr);
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);
    auto* layer = ds->CreateLayer("poly", srs.get(), wkbPolygon, nullptr);
    EXPECT_NE(layer, nullptr);
    if (layer) {
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(0, 0);
        ring.addPoint(200, 0);
        ring.addPoint(200, 200);
        ring.addPoint(0, 200);
        ring.addPoint(0, 0);
        poly.addRing(&ring);
        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetGeometry(&poly);
        EXPECT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
    }
    GDALClose(ds);
    return path;
}

// 目标图层拒绝写入时，clip 不得二次释放已移交写者的几何。
//
// 本用例的前提（必须成立才有覆盖价值）：输出图层被 resultLayerGeometryType(wkbPoint)
// 声明为 MULTIPOINT（vector_plugin_common.inc:389-392），clip 对点图层产出的是 POINT，
// 而 promoteGeometryToLayerType（vector_plugin_overlay_support.inc:14-29）不提升 Point
// → Shapefile 驱动拒绝写入 → 写出要素数为 0，正好命中"移交后不再 delete"的释放分支。
//
// ⚠ Task 20 修正 resultLayerGeometryType(wkbPoint) 之后本用例前提消失，届时写入会成功
// （written == 1、不再有驱动写失败消息），下面的两条前提断言会红——那时必须把它们
// 改成正向断言，而不是删掉断言让用例静默空跑。
TEST_F(PluginTest, VectorClipRejectedFeatureWriteDoesNotCorruptHeap) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = a0MakePointLayer("a0_clip_reject_input.gpkg", 100.0, 100.0);
    const std::string clip = a0MakeClipPolygonLayer("a0_clip_reject_overlay.gpkg");
    const std::string output = utf8PathString(getTestDir() / "a0_clip_reject_output.shp");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("clip");
    params["input"] = input;
    params["clip_vector"] = clip;
    params["output"] = output;

    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        g_gdalWarnings.clear();
        g_gdalErrors.clear();
    }
    CPLPushErrorHandler(captureGdalWarning);
    const auto result = p->execute(params, progress_);
    CPLPopErrorHandler();
    EXPECT_TRUE(result.success) << "Clip failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    bool rejectedWrite = false;
    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        for (const auto& error : g_gdalErrors) {
            if (a0IsMultipointWriteRejection(error)) {
                rejectedWrite = true;
                break;
            }
        }
    }
    EXPECT_TRUE(rejectedWrite)
        << "前提已消失：没有捕获到驱动拒绝把 POINT 写入 MULTIPOINT 图层的错误，"
           "本用例不再覆盖失败释放分支，须改成正向断言（见用例头注释）";

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    const GIntBig written = outLayer->GetFeatureCount(FALSE);
    EXPECT_EQ(written, 0) << "前提已消失：POINT 被 MULTIPOINT 图层拒绝时写出数必须是 0，"
                             "改成正向断言请同步更新用例头注释";
    outLayer->ResetReading();
    OGRFeature* outFeat = nullptr;
    while ((outFeat = outLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* outGeom = outFeat->GetGeometryRef();
        if (outGeom == nullptr) {
            // 不能用 ASSERT_*：它会直接结束整个用例，跳过 DestroyFeature 与 GDALClose，
            // .shp 保持打开 → 紧随其后的第二轮 execute 因文件占用再挂一次，
            // 把"1 个几何为空"放大成两个失败且原因难读。
            OGRFeature::DestroyFeature(outFeat);
            ADD_FAILURE() << "Clip 输出要素缺少几何，结果不自洽";
            break;
        }
        EXPECT_TRUE(outGeom->IsValid());
        OGRFeature::DestroyFeature(outFeat);
    }
    GDALClose(outDs);

    // 堆若已被破坏，后续 GDAL/GEOS 分配释放通常在此处才暴露，因此再跑一轮。
    const auto again = p->execute(params, progress_);
    EXPECT_TRUE(again.success) << "Second clip pass failed: " << again.message;
}

// 同样的所有权移交路径：difference 在写入被拒绝时不得二次释放。
// 前提与"前提消失即红"的要求同 VectorClipRejectedFeatureWriteDoesNotCorruptHeap：
// 输出图层为 MULTIPOINT、difference 对点图层产出 POINT、驱动拒绝写入 → written == 0。
// ⚠ Task 20 修正 resultLayerGeometryType(wkbPoint) 后须把两条前提断言改成正向断言。
TEST_F(PluginTest, VectorDifferenceRejectedFeatureWriteDoesNotCorruptHeap) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = a0MakePointLayer("a0_diff_reject_input.gpkg", 1000.0, 1000.0);
    const std::string overlay = a0MakeClipPolygonLayer("a0_diff_reject_overlay.gpkg");
    const std::string output = utf8PathString(getTestDir() / "a0_diff_reject_output.shp");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("difference");
    params["input"] = input;
    params["overlay_vector"] = overlay;
    params["output"] = output;

    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        g_gdalWarnings.clear();
        g_gdalErrors.clear();
    }
    CPLPushErrorHandler(captureGdalWarning);
    const auto result = p->execute(params, progress_);
    CPLPopErrorHandler();
    EXPECT_TRUE(result.success) << "Difference failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    bool rejectedWrite = false;
    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        for (const auto& error : g_gdalErrors) {
            if (a0IsMultipointWriteRejection(error)) {
                rejectedWrite = true;
                break;
            }
        }
    }
    EXPECT_TRUE(rejectedWrite)
        << "前提已消失：没有捕获到驱动拒绝把 POINT 写入 MULTIPOINT 图层的错误，"
           "本用例不再覆盖失败释放分支，须改成正向断言（见用例头注释）";

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    const GIntBig written = outLayer->GetFeatureCount(FALSE);
    EXPECT_EQ(written, 0) << "前提已消失：POINT 被 MULTIPOINT 图层拒绝时写出数必须是 0，"
                             "改成正向断言请同步更新用例头注释";
    outLayer->ResetReading();
    OGRFeature* outFeat = nullptr;
    while ((outFeat = outLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* outGeom = outFeat->GetGeometryRef();
        if (outGeom == nullptr) {
            // 同 clip 用例：这里用 ASSERT_* 会跳过 DestroyFeature/GDALClose，
            // 输出 .shp 保持打开会让紧接着的第二轮 execute 因文件占用再失败一次。
            OGRFeature::DestroyFeature(outFeat);
            ADD_FAILURE() << "Difference 输出要素缺少几何，结果不自洽";
            break;
        }
        EXPECT_TRUE(outGeom->IsValid());
        OGRFeature::DestroyFeature(outFeat);
    }
    GDALClose(outDs);

    const auto again = p->execute(params, progress_);
    EXPECT_TRUE(again.success) << "Second difference pass failed: " << again.message;
}

// 自相交多边形连续走 repair → dissolve → repair 链：规范化产物的所有权必须始终正确，
// 任何二次释放都会在这条链上以堆损坏的形式暴露。
TEST_F(PluginTest, VectorRepairDissolveInvalidPolygonChainKeepsGeometryValid) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "a0_chain_input.gpkg");
    const std::string repaired = utf8PathString(getTestDir() / "a0_chain_repaired.gpkg");
    const std::string dissolved = utf8PathString(getTestDir() / "a0_chain_dissolved.gpkg");
    const std::string repairedAgain = utf8PathString(getTestDir() / "a0_chain_repaired2.gpkg");

    {
        auto* drv = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(drv, nullptr);
        auto* ds = drv->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("parcels", srs.get(), wkbPolygon, nullptr);
        ASSERT_NE(layer, nullptr);
        OGRFieldDefn typeField("type", OFTString);
        ASSERT_EQ(layer->CreateField(&typeField), OGRERR_NONE);

        // 3 个自相交（蝴蝶结）多边形，分属 2 个溶解组。
        const double offsets[] = {0.0, 20.0, 120.0};
        const char* types[] = {"A", "A", "B"};
        for (int i = 0; i < 3; ++i) {
            auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
            OGRPolygon poly;
            OGRLinearRing ring;
            ring.addPoint(0 + offsets[i], 0);
            ring.addPoint(100 + offsets[i], 100);
            ring.addPoint(100 + offsets[i], 0);
            ring.addPoint(0 + offsets[i], 100);
            ring.addPoint(0 + offsets[i], 0);
            poly.addRing(&ring);
            EXPECT_FALSE(poly.IsValid());
            feat->SetGeometry(&poly);
            feat->SetField("type", types[i]);
            ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
            OGRFeature::DestroyFeature(feat);
        }
        GDALClose(ds);
    }

    const auto run = [&](const char* action, const std::string& in, const std::string& out,
                         const std::string& field) {
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string(action);
        params["input"] = in;
        params["output"] = out;
        if (!field.empty()) {
            params["dissolve_field"] = field;
        }
        return p->execute(params, progress_);
    };

    const auto repair1 = run("repair", input, repaired, "");
    EXPECT_TRUE(repair1.success) << repair1.message;
    EXPECT_EQ(repair1.metadata.at("repaired_count"), "3");
    EXPECT_EQ(repair1.metadata.at("skipped_count"), "0");

    const auto dissolve1 = run("dissolve", input, dissolved, "type");
    EXPECT_TRUE(dissolve1.success) << dissolve1.message;

    const auto repair2 = run("repair", dissolved, repairedAgain, "");
    EXPECT_TRUE(repair2.success) << repair2.message;

    auto checkValid = [&](const std::string& path, GIntBig expectedAtLeast) {
        GDALDataset* ds = static_cast<GDALDataset*>(GDALOpenEx(
            path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
        ASSERT_NE(ds, nullptr) << path;
        OGRLayer* layer = ds->GetLayer(0);
        ASSERT_NE(layer, nullptr) << path;
        EXPECT_GE(layer->GetFeatureCount(FALSE), expectedAtLeast) << path;
        layer->ResetReading();
        OGRFeature* feat = nullptr;
        while ((feat = layer->GetNextFeature()) != nullptr) {
            OGRGeometry* geom = feat->GetGeometryRef();
            ASSERT_NE(geom, nullptr) << path;
            EXPECT_FALSE(geom->IsEmpty()) << path;
            bool valid = false;
            {
                CPLPushErrorHandler(CPLQuietErrorHandler);
                valid = geom->IsValid();
                CPLPopErrorHandler();
            }
            EXPECT_TRUE(valid) << path << " geometry still invalid";
            const auto flat = wkbFlatten(geom->getGeometryType());
            EXPECT_TRUE(flat == wkbPolygon || flat == wkbMultiPolygon)
                << path << " unexpected type " << OGRGeometryTypeToName(geom->getGeometryType());
            // get_Area() 声明在 OGRCurvePolygon/OGRPolygon/OGRMultiPolygon 上，OGRGeometry 基类没有，
            // 因此按上面已校验的扁平类型下推成具体多边形类再取面积（与本文件其它用例一致写法）。
            double area = 0.0;
            if (flat == wkbPolygon) {
                OGRPolygon* poly = geom->toPolygon();
                ASSERT_NE(poly, nullptr) << path;
                if (poly) area = poly->get_Area();
            } else if (flat == wkbMultiPolygon) {
                OGRMultiPolygon* multi = geom->toMultiPolygon();
                ASSERT_NE(multi, nullptr) << path;
                if (multi) area = multi->get_Area();
            }
            EXPECT_GT(area, 0.0) << path << " zero-area geometry";
            OGRFeature::DestroyFeature(feat);
        }
        GDALClose(ds);
    };

    checkValid(repaired, 3);
    checkValid(dissolved, 2);
    checkValid(repairedAgain, 2);
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

// ============================================================================
// Task 5：band_math 接入表达式引擎（消除旧 evalExpression 的静默 0）
//
// 覆盖 spec H1 的两条口径：负操作数算得对且符号正确、未知变量显式失败；
// 外加本任务的除零计数口径（R-T5-2：保持写 0.0，但必须计数进 metadata，不新增 NoData 语义）。
// 夹具按 R-T5-1 用 writeConstantBandsRaster（writeFloatRaster 只建 1 个波段，表达不出 B2）。
// ============================================================================

// R-T5-1：同一幅两波段常量栅格上跑两个互为镜像的表达式，
// 分别钉住"负操作数参与运算得正值"与"得负值"两个方向——旧求值器两种都静默交回 0.0。
TEST_F(PluginTest, BandMathNegativeBandsComputeWithCorrectSign) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.hasNoData = true;
    options.noData = -9999.0;
    // band1(B1) ≡ -0.25、band2(B2) ≡ 0.75，2 列 1 行。
    const std::string input = gis::tests::writeConstantBandsRaster(
        getTestDir(), "rm_neg_input.tif", 2, 1, {-0.25f, 0.75f}, options);
    ASSERT_FALSE(input.empty());

    // (B2-B1)/(B2+B1) = (0.75-(-0.25))/(0.75+(-0.25)) = 1.0/0.5 = 2.0
    {
        const std::string output = utf8PathString(getTestDir() / "rm_neg_output.tif");
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("band_math");
        params["input"] = input;
        params["output"] = output;
        params["expression"] = std::string("(B2-B1)/(B2+B1)");

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << result.message;
        EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 0), 2.0f, 1e-5f);
        EXPECT_NEAR(gis::tests::readPixel(output, 1, 1, 0), 2.0f, 1e-5f);
    }

    // 镜像式 (B1-B2)/(B1+B2) = -1.0/0.5 = -2.0：符号必须翻过来，
    // 只有正方向那条断言无法区分"负号被丢掉"和"负号算对"。
    {
        const std::string output = utf8PathString(getTestDir() / "rm_neg_mirror_output.tif");
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("band_math");
        params["input"] = input;
        params["output"] = output;
        params["expression"] = std::string("(B1-B2)/(B1+B2)");

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << result.message;
        EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 0), -2.0f, 1e-5f);
        EXPECT_NEAR(gis::tests::readPixel(output, 1, 1, 0), -2.0f, 1e-5f);
    }
}

TEST_F(PluginTest, BandMathWithUnknownVariableFailsExplicitly) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);
    const std::string input = createTestRaster("rm_unknown_input.tif", 2, 2, 2);
    const std::string output = utf8PathString(getTestDir() / "rm_unknown_output.tif");

    // M-T5-a：先删掉任何一次运行留下的陈旧输出，否则下面的"没有写出"会被旧文件骗成假通过。
    std::error_code removeEc;
    fs::remove(output, removeEc);
    EXPECT_FALSE(fs::exists(output)) << "stale output not removed: " << output;

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("band_math");
    params["input"] = input;
    params["output"] = output;
    params["expression"] = std::string("B9*2");

    const auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.message.find("B9"), std::string::npos) << result.message;
    // M-T5-a：钉住不变式"表达式非法时根本不写出栅格"（doBandMath 在编译失败处就 return，
    // 位于写出之前）。此前只有 !result.success 被断言，写出时机变了也没人发现。
    EXPECT_FALSE(fs::exists(output)) << "invalid expression must not produce an output raster";
}

// 计数必须"等于失败的像元数"，而不是碰巧等于总像元数：
// 这里 8x4 = 32 个像元里只有 x=2 那一列（B2 沿 x 每步 0.5，故 B2≡1.0）使分母 B2-1 为 0，
// 所以期望值是 4 而不是 32；同一条用例还顺手钉住负值结果（x=0 得 -1、x=1 得 -2）。
TEST_F(PluginTest, BandMathDivideByZeroCountEqualsFailingPixelsOnly) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);

    // band1 = 1.0 + 0.0*x（恒 1）；band2 = 0.0 + 0.5*x（x=0..7 → 0,0.5,1.0,...,3.5，逐列精确可表示）。
    const std::string input = createMultiBandGradientRaster(
        "rm_partial_divzero_input.tif", 8, 4, {{1.0f, 0.0f}, {0.0f, 0.5f}});
    const std::string output = utf8PathString(getTestDir() / "rm_partial_divzero_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("band_math");
    params["input"] = input;
    params["output"] = output;
    params["expression"] = std::string("B1/(B2-1)");

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    const auto it = result.metadata.find("divide_by_zero_pixels");
    ASSERT_NE(it, result.metadata.end());
    EXPECT_EQ(it->second, "4");                     // 32 个像元里只有 4 个除零
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 2, 0), 0.0f, 1e-6f);   // 除零像元仍写 0.0（R-T5-2）
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 3, 0), 2.0f, 1e-5f);   // 1/(1.5-1)
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 1, 0), -2.0f, 1e-5f);  // 1/(0.5-1)
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 0), -1.0f, 1e-5f);  // 1/(0-1)
}

// M-T5-b：除法失败让**整个像元**失效（写 0.0），而不是交回"用 0.0 继续算"的部分结果。
// 本批次有意设计（不是回归）：旧引擎的 evalExpression 把除零当普通 0 继续算，
// `5-B1/B2` 于 {2,0} 会交回 5-0=5.0 这种"看似合理"的脏值并写进栅格；Task 4 把除零改成
// "置 evaluationFailed 并立即 return 0.0"（引擎级证据 test_core.cpp:684 起），
// 口径是"求值失败的像元不可信，整像元归 0"。此前只有引擎级用例钉着，插件级无人看守，
// 而这条取值变化是**用户可见**的，故在此固定。键名 divide_by_zero_pixels 由计划规定，不改名。
TEST_F(PluginTest, BandMathDivisionFailureInvalidatesWholePixel) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);

    // B1 ≡ 2.0、B2 ≡ 0.0 ⇒ 5-2/0 的除数正好落在 kNearZeroDivisorEpsilon 之下。
    const std::string input = gis::tests::writeConstantBandsRaster(
        getTestDir(), "rm_wholepixel_divzero_input.tif", 2, 1, {2.0f, 0.0f});
    ASSERT_FALSE(input.empty());
    const std::string output = utf8PathString(getTestDir() / "rm_wholepixel_divzero_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("band_math");
    params["input"] = input;
    params["output"] = output;
    params["expression"] = std::string("5-B1/B2");

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.metadata.at("divide_by_zero_pixels"), "2");   // 整像元失败 ⇒ 两个像元都计数
    // 关键鉴别值：除法若只是"把 0.0 压栈继续算"，这里会得到 5.0（旧引擎的交回值）。
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 0), 0.0f, 1e-6f);
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 1, 0), 0.0f, 1e-6f);
}

// 追加义务 2：把 kNearZeroDivisorEpsilon=1e-15 的边界两侧都通过插件调用钉住。
// 取 1e-15 的 ±1e-6 相对偏移（约 ±9 个 float32 ulp），既"紧贴阈值"又远离浮点量化：
// Float32 在 1e-15 附近的 ulp≈1.06e-22，而这里与阈值的差是 1e-21，量化误差不可能把两侧翻过来。
// 前提仍是**绝对**判据 ⇒ 波段量级 ≳1e-14；改成相对判据是输出语义变更，需先写进 spec。
TEST_F(PluginTest, BandMathDivisorThresholdBoundaryIsPinned) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);

    // 刚刚高于阈值：分母 1.000001e-15 → 正常求值得 ~1e15，除零计数为 0。
    const std::string aboveInput = gis::tests::writeConstantBandsRaster(
        getTestDir(), "rm_eps_above_input.tif", 2, 1, {1.0f, 1.000001e-15f});
    ASSERT_FALSE(aboveInput.empty());
    const std::string aboveOutput = utf8PathString(getTestDir() / "rm_eps_above_output.tif");
    {
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("band_math");
        params["input"] = aboveInput;
        params["output"] = aboveOutput;
        params["expression"] = std::string("B1/B2");

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << result.message;
        EXPECT_EQ(result.metadata.at("divide_by_zero_pixels"), "0");
        // 旧实现在这里连值都算不出来：std::to_string 只有 6 位小数，1e-15 被替换成 "0.000000"。
        EXPECT_NEAR(gis::tests::readPixel(aboveOutput, 1, 0, 0), 1.0f / 1.000001e-15f, 1e9f);
        EXPECT_NEAR(gis::tests::readPixel(aboveOutput, 1, 1, 0), 1.0f / 1.000001e-15f, 1e9f);
    }

    // 刚刚低于阈值：分母 0.999999e-15 → evaluationFailed，写 0.0 且两个像元都被计数。
    const std::string belowInput = gis::tests::writeConstantBandsRaster(
        getTestDir(), "rm_eps_below_input.tif", 2, 1, {1.0f, 0.999999e-15f});
    ASSERT_FALSE(belowInput.empty());
    const std::string belowOutput = utf8PathString(getTestDir() / "rm_eps_below_output.tif");
    {
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("band_math");
        params["input"] = belowInput;
        params["output"] = belowOutput;
        params["expression"] = std::string("B1/B2");

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << result.message;
        EXPECT_EQ(result.metadata.at("divide_by_zero_pixels"), "2");
        EXPECT_NEAR(gis::tests::readPixel(belowOutput, 1, 0, 0), 0.0f, 1e-6f);
    }
}

// 追加义务 4 的插件侧证据：这四种形态旧求值器静默接受（返回相邻操作数或 0.0），
// 接上新引擎后都是编译期硬错误 ⇒ 插件必须返回 Result::fail。
TEST_F(PluginTest, BandMathRejectsShapesOldEvaluatorSilentlyAccepted) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);
    const std::string input = createTestRaster("rm_legacy_shapes_input.tif", 2, 2, 2);

    const char* shapes[] = {"B1(B2+1)", "(B1)(B2)", "()", "(B1+)"};
    const std::string output = utf8PathString(getTestDir() / "rm_legacy_shapes_output.tif");
    for (const char* shape : shapes) {
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("band_math");
        params["input"] = input;
        params["output"] = output;
        params["expression"] = std::string(shape);

        // M-T5-a：删除陈旧输出并断"失败即不写盘"（同 BandMathWithUnknownVariableFailsExplicitly）。
        std::error_code removeEc;
        fs::remove(output, removeEc);
        EXPECT_FALSE(fs::exists(output)) << shape;

        const auto result = p->execute(params, progress_);
        EXPECT_FALSE(result.success) << shape;
        EXPECT_FALSE(result.message.empty()) << shape;
        EXPECT_FALSE(fs::exists(output))
            << "invalid expression must not produce an output raster: " << shape;
    }
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
    const std::string profileCurvatureOutput = utf8PathString(getTestDir() / "terrain_profile_curvature_output.tif");
    const std::string planCurvatureOutput = utf8PathString(getTestDir() / "terrain_plan_curvature_output.tif");
    const std::string triOutput = utf8PathString(getTestDir() / "terrain_tri_output.tif");
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

    std::map<std::string, gis::framework::ParamValue> profileCurvatureParams = slopeParams;
    profileCurvatureParams["action"] = std::string("profile_curvature");
    profileCurvatureParams["output"] = profileCurvatureOutput;

    std::map<std::string, gis::framework::ParamValue> planCurvatureParams = slopeParams;
    planCurvatureParams["action"] = std::string("plan_curvature");
    planCurvatureParams["output"] = planCurvatureOutput;

    std::map<std::string, gis::framework::ParamValue> triParams = slopeParams;
    triParams["action"] = std::string("tri");
    triParams["output"] = triOutput;

    std::map<std::string, gis::framework::ParamValue> roughnessParams = slopeParams;
    roughnessParams["action"] = std::string("roughness");
    roughnessParams["output"] = roughnessOutput;

    const auto slopeResult = p->execute(slopeParams, progress_);
    const auto aspectResult = p->execute(aspectParams, progress_);
    const auto hillshadeResult = p->execute(hillshadeParams, progress_);
    const auto tpiResult = p->execute(tpiParams, progress_);
    const auto curvatureResult = p->execute(curvatureParams, progress_);
    const auto profileCurvatureResult = p->execute(profileCurvatureParams, progress_);
    const auto planCurvatureResult = p->execute(planCurvatureParams, progress_);
    const auto triResult = p->execute(triParams, progress_);
    const auto roughnessResult = p->execute(roughnessParams, progress_);

    EXPECT_TRUE(slopeResult.success) << slopeResult.message;
    EXPECT_TRUE(aspectResult.success) << aspectResult.message;
    EXPECT_TRUE(hillshadeResult.success) << hillshadeResult.message;
    EXPECT_TRUE(tpiResult.success) << tpiResult.message;
    EXPECT_TRUE(curvatureResult.success) << curvatureResult.message;
    EXPECT_TRUE(profileCurvatureResult.success) << profileCurvatureResult.message;
    EXPECT_TRUE(planCurvatureResult.success) << planCurvatureResult.message;
    EXPECT_TRUE(triResult.success) << triResult.message;
    EXPECT_TRUE(roughnessResult.success) << roughnessResult.message;
    EXPECT_TRUE(fs::exists(slopeOutput));
    EXPECT_TRUE(fs::exists(aspectOutput));
    EXPECT_TRUE(fs::exists(hillshadeOutput));
    EXPECT_TRUE(fs::exists(tpiOutput));
    EXPECT_TRUE(fs::exists(curvatureOutput));
    EXPECT_TRUE(fs::exists(profileCurvatureOutput));
    EXPECT_TRUE(fs::exists(planCurvatureOutput));
    EXPECT_TRUE(fs::exists(triOutput));
    EXPECT_TRUE(fs::exists(roughnessOutput));
    EXPECT_TRUE(std::isfinite(readRasterPixel(tpiOutput, 24, 24)));
    EXPECT_TRUE(std::isfinite(readRasterPixel(curvatureOutput, 24, 24)));
    EXPECT_TRUE(std::isfinite(readRasterPixel(profileCurvatureOutput, 24, 24)));
    EXPECT_TRUE(std::isfinite(readRasterPixel(planCurvatureOutput, 24, 24)));
    EXPECT_GT(readRasterPixel(triOutput, 24, 24), 0.01f);
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

TEST_F(PluginTest, RasterMathReclassifyExecution) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("e2e_reclassify_input.tif", 30, 30);
    const std::string output = utf8PathString(getTestDir() / "e2e_reclassify_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("reclassify");
    params["input"] = input;
    params["output"] = output;
    params["reclass_rules"] = std::string("0,127:1\n128,255:2");
    params["default_value"] = 0.0;
    params["keep_unmatched"] = false;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));

    EXPECT_NEAR(readRasterPixel(output, 0, 0), 1.0f, 1e-4f);
    EXPECT_NEAR(readRasterPixel(output, 20, 5), 2.0f, 1e-4f);
}

TEST_F(PluginTest, RasterMathRasterOverlayExecution) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);

    const std::string inputA = createConstantRaster("e2e_overlay_input_a.tif", 20, 20, 10.0f);
    const std::string inputB = createConstantRaster("e2e_overlay_input_b.tif", 20, 20, 30.0f);
    const std::string output = utf8PathString(getTestDir() / "e2e_overlay_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("raster_overlay");
    params["input"] = inputA;
    params["overlay_input"] = inputB;
    params["output"] = output;
    params["overlay_method"] = std::string("sum");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_NEAR(readRasterPixel(output, 5, 5), 40.0f, 1e-4f);
}

// ============================================================================
// Task 6：多波段输出修复（reclassify / raster_overlay）
//
// 共同失效形态：这两个 action 在波段循环里逐波段调 matToGdalTiff(..., bandIndex)，
// 而该函数每次都 createRaster(..., 1, ...) 重写同一个文件 ⇒ N 波段输入只剩最后一次的
// 1 波段输出。修法是累积 std::vector<cv::Mat> 后一次 matsToGdalTiff 写出。
// reclassify 还多一条：interval 规则必须逐波段独立（不跨波段累积到同一个 rules 向量）。
// ============================================================================

// R-T6-2：用**区间网格错位**的数据制造真正的跨波段冲突（step=5，keep_unmatched=false）。
// interval 的规则生成本就逐波段算 bandMin/bandMax 并逐波段重置 classIdx，唯一缺陷是所有
// 规则被 push_back 进同一个 rules 向量，而 applyReclassRules 是首个匹配即返回：
//   band1 = {0, 9} → 自身规则 [0,5]→1、[5,10]→2
//   band2 = {7, 7} → 自身规则 [5,10]→1
// 修复后（逐波段独立）band2 两个像元都是 1.0；
// 修复前（跨波段累积 + 首匹配）band2 的 7 先命中 band1 推入的 [5,10]→2 ⇒ 读到 2.0，断言红。
// 波段数断言与 band2 的取值断言各自独立锁定 Produces 的两句话。
TEST_F(PluginTest, ReclassifyKeepsAllBandsAndPerBandIntervalRules) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);

    const std::string input = gis::tests::writeFloatBandsRaster(
        getTestDir(), "rm_reclass_multi_input.tif", 2, 1, {{0.0f, 9.0f}, {7.0f, 7.0f}});
    ASSERT_FALSE(input.empty());
    // 夹具自检：数据进得去（否则下面的红会错归到夹具头上，而不是插件）。
    ASSERT_EQ(gis::tests::readBandCount(input), 2);
    ASSERT_NEAR(gis::tests::readPixel(input, 1, 1, 0), 9.0f, 1e-6f);
    ASSERT_NEAR(gis::tests::readPixel(input, 2, 0, 0), 7.0f, 1e-6f);

    const std::string output = utf8PathString(getTestDir() / "rm_reclass_multi_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("reclassify");
    params["input"] = input;
    params["output"] = output;
    params["reclass_mode"] = std::string("interval");
    params["interval_step"] = 5.0;
    params["keep_unmatched"] = false;
    params["default_value"] = 0.0;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    // Produces 第 1 句：输出波段数 == 输入波段数（旧实现每波段覆盖写 ⇒ 只剩 1 个波段）。
    EXPECT_EQ(gis::tests::readBandCount(output), 2);
    // band1 用自身规则：0 → 类 1、9 → 类 2（修复前后同值，钉住"逐波段规则本身算得对"）。
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 0), 1.0f, 1e-6f);
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 1, 0), 2.0f, 1e-6f);
    // Produces 第 2 句（鉴别力在这两条）：band2 只能用自身的 [5,10]→1；被 band1 的规则污染则读回 2.0。
    EXPECT_NEAR(gis::tests::readPixel(output, 2, 0, 0), 1.0f, 1e-6f);
    EXPECT_NEAR(gis::tests::readPixel(output, 2, 1, 0), 1.0f, 1e-6f);
}

// F1（fix round 1，Important）：**零规则波段不得静默原值透传**。
// 区间规则改成逐波段独立之后，"某个波段一条规则都不生成"只有聚合判据
// （totalRules == 0）看得见——只要别的波段产出了规则就放行，于是该波段每个像元都落到
// applyReclassRules 的未匹配分支，输出原值（keep_unmatched=true）或 default_value，
// 结果 success=true、**没有任何诊断**。同一条件在单波段输入上本来就是硬错误，
// 多波段不能降级成静默。
// 数据构造：step=5、band2 恒为 10.0 ⇒ roundedMin = floor(10/5)*5 = 10，
// while (current < bandMax) 即 `10 < 10` 一次都不进循环 ⇒ band2 的规则集为空。
// 修复前本用例红在 ASSERT_FALSE(result.success)（band2 原值 10.0 透传出去，仍报成功）。
TEST_F(PluginTest, ReclassifyFailsWhenOneBandGeneratesNoIntervalRules) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);

    const std::string input = gis::tests::writeFloatBandsRaster(
        getTestDir(), "rm_reclass_zerorule_input.tif", 2, 1,
        {{0.0f, 9.0f}, {10.0f, 10.0f}});
    ASSERT_FALSE(input.empty());
    ASSERT_EQ(gis::tests::readBandCount(input), 2);
    // 夹具自检：band2 确实恒为 10.0（正好落在 step 的整倍数上），否则下面的红会错归到夹具。
    ASSERT_NEAR(gis::tests::readPixel(input, 2, 0, 0), 10.0f, 1e-6f);
    ASSERT_NEAR(gis::tests::readPixel(input, 2, 1, 0), 10.0f, 1e-6f);

    const std::string output = utf8PathString(getTestDir() / "rm_reclass_zerorule_output.tif");
    // 同 M-T5-a：先清掉历次运行留下的陈旧输出，否则"没有写出"会被旧文件骗成假通过。
    std::error_code removeEc;
    fs::remove(output, removeEc);
    EXPECT_FALSE(fs::exists(output)) << "stale output not removed: " << output;

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("reclassify");
    params["input"] = input;
    params["output"] = output;
    params["reclass_mode"] = std::string("interval");
    params["interval_step"] = 5.0;
    // 故意用 keep_unmatched=true（未匹配 ⇒ 原样输出），把"静默透传"这个形态摆到断言面前。
    params["keep_unmatched"] = true;
    params["default_value"] = 0.0;

    const auto result = p->execute(params, progress_);
    ASSERT_FALSE(result.success)
        << "band2 零规则却报成功＝原值透传，正是本轮要消灭的无诊断形态。message=" << result.message;
    // 消息要点名是哪个波段（口径镜像已有的逐波段错误 "Band N contains only NoData values"）。
    EXPECT_NE(result.message.find("Band 2"), std::string::npos) << result.message;
    EXPECT_NE(result.message.find("no interval rules"), std::string::npos) << result.message;
    // 必须是真正零规则的那个波段：波段的 0 基/1 基写错就会报成 Band 1（band1 有 2 条规则）。
    EXPECT_EQ(result.message.find("Band 1"), std::string::npos) << result.message;
    // 守卫生效在生成规则阶段，输出文件根本不该存在。
    EXPECT_FALSE(fs::exists(output));
}

// F4（fix round 1）需要断言进度文案里的规则数口径。PluginTest 的 progress_ 是 CliProgress，
// 消息只打到 stdout、断不了，所以这里加一个最小的记录型 reporter：
// isCancelled() 恒 false（与 CliProgress 同语义，throwIfCancelled 因此也恒不抛），
// 只把 onMessage 收进 vector，joined() 供子串断言。
class RecordingProgress : public gis::core::ProgressReporter {
public:
    void onProgress(double) override {}
    void onMessage(const std::string& msg) override { messages_.push_back(msg); }
    bool isCancelled() const override { return false; }

    // 全部消息拼成一段：断言只关心"这条文案出现过"，不依赖消息条数与顺序。
    std::string joined() const {
        std::string all;
        for (const auto& msg : messages_) {
            all += msg;
            all += '\n';
        }
        return all;
    }

private:
    std::vector<std::string> messages_;
};

// F3（fix round 1，Minor 1）：manual 模式的多波段展开 rulesPerBand.assign(bands, sharedRules)
// 是被本轮改动的代码路径，此前只有单波段用例（RasterMathReclassifyExecution）走过它。
// 这里用 2 波段 + 同一份共享规则钉住"每个波段都分到这一份、并且各自都被分类"：
// keep_unmatched=false + default_value=-1，所以"某波段没拿到规则"会输出 -1，
// 与合法类别值 1/2 不撞车（不会因为和"类别 0"这类边界撞值而漏判）。
//
// F7（fix round 1，Minor 6）：写出从 matToGdalTiff 换成 matsToGdalTiff 之后，它提供的头语义
// （投影照搬 + NoData 声明随波段带过去）没有任何断言覆盖，本用例补上。
// **不要**在这里试"逐波段不同 NoData"来加强鉴别力——实测（gdalinfo + 生成的 .aux.xml）表明：
// createRaster 出的多波段 GeoTIFF 是 INTERLEAVE=PIXEL 单 IFD，原生 GDAL_NODATA 标签全图只有一个，
// 逐波段 SetNoDataValue 只有**最后一个**留得住（各波段读回同一个值）；输入侧之所以能读出
// -9999/-1234 两个不同值，是 GDAL 把它们写进了 PAM 边车 .aux.xml。也就是说"逐波段不同的
// NoData"在这个输出形态下表达不出来，拿它当断言会钉住格式行为而不是插件行为（详见报告顾虑）。
// 因此这里断的是可观察的那部分：输出第 2 波段确实存在、并且带着 NoData 声明
// （旧覆盖写实现下输出只有 1 个波段，readNoDataValue(output, 2, …) 会当场 ADD_FAILURE）。
TEST_F(PluginTest, ReclassifyManualModeAppliesSharedRulesToEveryBand) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg4326;
    options.hasNoData = true;
    options.noData = -9999.0;
    const std::string input = gis::tests::writeFloatBandsRaster(
        getTestDir(), "rm_reclass_manual_multi_input.tif", 2, 1,
        {{1.0f, 200.0f}, {100.0f, 150.0f}}, options);
    ASSERT_FALSE(input.empty());
    ASSERT_EQ(gis::tests::readBandCount(input), 2);
    ASSERT_EQ(gis::tests::readCrsAuthorityCode(input), "EPSG:4326");
    double inNoData1 = 0.0;
    double inNoData2 = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(input, 1, inNoData1));
    ASSERT_TRUE(gis::tests::readNoDataValue(input, 2, inNoData2));
    ASSERT_DOUBLE_EQ(inNoData1, -9999.0);   // 参考数据集自己先对得上，下面的断言才有意义
    ASSERT_DOUBLE_EQ(inNoData2, -9999.0);

    const std::string output = utf8PathString(getTestDir() / "rm_reclass_manual_multi_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("reclassify");
    params["input"] = input;
    params["output"] = output;
    params["reclass_rules"] = std::string("0,127:1\n128,255:2");
    params["keep_unmatched"] = false;
    params["default_value"] = -1.0;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    // F3：两个波段都在（旧覆盖写只剩 1 个），且**两个波段各自**都被同一份共享规则分类。
    EXPECT_EQ(gis::tests::readBandCount(output), 2);
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 0), 1.0f, 1e-6f);   // band1: 1   → 类 1
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 1, 0), 2.0f, 1e-6f);   // band1: 200 → 类 2
    EXPECT_NEAR(gis::tests::readPixel(output, 2, 0, 0), 1.0f, 1e-6f);   // band2: 100 → 类 1
    EXPECT_NEAR(gis::tests::readPixel(output, 2, 1, 0), 2.0f, 1e-6f);   // band2: 150 → 类 2

    // F7：头语义——投影照搬 + 第 2 波段带着 NoData 声明写出来了。
    EXPECT_EQ(gis::tests::readCrsAuthorityCode(output), "EPSG:4326");
    double outNoData1 = 0.0;
    double outNoData2 = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 1, outNoData1));
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 2, outNoData2));
    EXPECT_DOUBLE_EQ(outNoData1, -9999.0);
    EXPECT_DOUBLE_EQ(outNoData2, -9999.0);
}

// F4（fix round 1，Minor 2）：进度文案里的规则数原来是"逐波段副本求和"，manual 模式下
// 所有波段共用同一份规则 ⇒ 2 条规则配 2 个波段会被说成 4 条，是假话（原来的数字才准确）。
// 选**按模式区分文案**（而不是全局按去重后的条数报）：
//   manual   → "N band(s) with M shared rule(s)"，M = 解析出的共享条数（同一份规则本就该报一次）；
//   interval → "N band(s) with M rule(s)"，M = 各波段独立生成条数之和（本来就是真数）。
// 弃用"去重计数"的理由：interval 下两个波段即使生成数值相同的规则集也是各自独立的类别划分，
// 去重会把它报少——去重只在 manual 下恰好正确，而逐模式口径在两种模式下都诚实。
TEST_F(PluginTest, ReclassifyProgressMessageCountsRulesPerMode) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);

    // --- manual：2 波段 × 同一份 2 条共享规则 ⇒ 期望 "2 band(s) with 2 shared rule(s)"。
    const std::string manualInput = gis::tests::writeFloatBandsRaster(
        getTestDir(), "rm_reclass_msg_manual_input.tif", 2, 1,
        {{1.0f, 200.0f}, {100.0f, 150.0f}});
    ASSERT_FALSE(manualInput.empty());
    std::map<std::string, gis::framework::ParamValue> manualParams;
    manualParams["action"] = std::string("reclassify");
    manualParams["input"] = manualInput;
    manualParams["output"] = utf8PathString(getTestDir() / "rm_reclass_msg_manual_output.tif");
    manualParams["reclass_rules"] = std::string("0,127:1\n128,255:2");
    manualParams["keep_unmatched"] = false;
    manualParams["default_value"] = 0.0;

    RecordingProgress manualProgress;
    const auto manualResult = p->execute(manualParams, manualProgress);
    ASSERT_TRUE(manualResult.success) << manualResult.message;
    const std::string manualMsg = manualProgress.joined();
    EXPECT_NE(manualMsg.find("2 band(s) with 2 shared rule(s)"), std::string::npos) << manualMsg;
    // 求和口径的假话（2 条 × 2 波段 = 4）不得再出现。
    EXPECT_EQ(manualMsg.find("4 rule(s)"), std::string::npos) << manualMsg;

    // --- interval：band1 {0,9} 生成 2 条、band2 {7,7} 生成 1 条 ⇒ 求和 3 条是真数，口径不变。
    const std::string intervalInput = gis::tests::writeFloatBandsRaster(
        getTestDir(), "rm_reclass_msg_interval_input.tif", 2, 1,
        {{0.0f, 9.0f}, {7.0f, 7.0f}});
    ASSERT_FALSE(intervalInput.empty());
    std::map<std::string, gis::framework::ParamValue> intervalParams;
    intervalParams["action"] = std::string("reclassify");
    intervalParams["input"] = intervalInput;
    intervalParams["output"] = utf8PathString(getTestDir() / "rm_reclass_msg_interval_output.tif");
    intervalParams["reclass_mode"] = std::string("interval");
    intervalParams["interval_step"] = 5.0;
    intervalParams["keep_unmatched"] = false;
    intervalParams["default_value"] = 0.0;

    RecordingProgress intervalProgress;
    const auto intervalResult = p->execute(intervalParams, intervalProgress);
    ASSERT_TRUE(intervalResult.success) << intervalResult.message;
    const std::string intervalMsg = intervalProgress.joined();
    EXPECT_NE(intervalMsg.find("2 band(s) with 3 rule(s)"), std::string::npos) << intervalMsg;
    // interval 下不得被套上 manual 的 "shared" 措辞（各波段规则并不同一份）。
    EXPECT_EQ(intervalMsg.find("shared rule(s)"), std::string::npos) << intervalMsg;
}

// R-T6-1：叠加操作的参数键是 overlay_method（paramSpecs 声明、doRasterOverlay 以默认 "max"
// 读取）。计划原文的 params["operation"] 既未声明也未读取，会静默落到 max，
// 于是 10/20 与 5 的期望值 5/15 变成 10/20 —— 用错键的用例必红在错因上，故按实测键名写。
TEST_F(PluginTest, RasterOverlayKeepsAllBands) {
    auto* p = mgr_.find("raster_math");
    ASSERT_NE(p, nullptr);

    const std::string a = createMultiBandConstantRaster("rm_ov_a.tif", 4, 4, {10.0f, 20.0f});
    const std::string b = createMultiBandConstantRaster("rm_ov_b.tif", 4, 4, {5.0f, 5.0f});
    const std::string output = utf8PathString(getTestDir() / "rm_ov_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("raster_overlay");
    params["input"] = a;
    params["overlay_input"] = b;
    params["output"] = output;
    params["overlay_method"] = std::string("subtract");

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    // bands = min(A 波段数, B 波段数) = 2 ⇒ 输出必须是 2 个波段（旧实现覆盖写 ⇒ 1 个）。
    EXPECT_EQ(gis::tests::readBandCount(output), 2);
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 0), 5.0f, 1e-6f);    // 10 - 5
    EXPECT_NEAR(gis::tests::readPixel(output, 2, 0, 0), 15.0f, 1e-6f);   // 20 - 5
}

TEST_F(PluginTest, RasterManageZonalStatsExecution) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("e2e_zonal_input.tif", 30, 30);
    const std::string vectorPath = createPolygonVectorDataset("e2e_zonal_zones.gpkg", 4326);
    const std::string output = utf8PathString(getTestDir() / "e2e_zonal_output.json");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("zonal_stats");
    params["input"] = input;
    params["vector"] = vectorPath;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
}

TEST_F(PluginTest, RasterManageProximityExecution) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const std::string input = createTestRaster("e2e_proximity_input.tif", 50, 50);
    const std::string output = utf8PathString(getTestDir() / "e2e_proximity_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("proximity");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("algorithm"), "euclidean_distance_cv");

    auto outDs = gis::core::openRaster(output, true);
    ASSERT_NE(outDs, nullptr);
    EXPECT_EQ(outDs->GetRasterXSize(), 50);
    EXPECT_EQ(outDs->GetRasterYSize(), 50);
}

// Task 7：Produces 口径是"每个像元到最近**目标**像元（值非 0 且非 NoData）的距离"。
// 修前的实现把"目标=255/背景=0"直接喂给 distanceTransform，而 OpenCV 度量的是非零像元
// 到最近零像元的距离，于是输出的是"到最近背景的距离"，方向正好相反。
//
// 目标像元的值故意取**负数**（R-T7-3）：修前的实现用 convertTo(CV_8UC1, 255.0) 做饱和转换，
// 负的波段值被压成 0，再经 THRESH_BINARY 后负值目标像元当成背景直接抹掉（负 DN、负反射率、
// 地形凹槽都是常见输入）。所以这一条用例同时锁住"方向搞反"和"负值目标被静默丢弃"两个同源症状：
// 只修方向、仍保留 convertTo 的错误实现下，本用例照样红（实测见 task-7-report.md §5.1）。
//
// options.pixelSize = 1.0（R-T7-1）：实现保留 `dist *= |gt[1]|` 的单位换算（经纬度口径归 Plan 2），
// 夹具默认 pixelSize=0.001 会让真值变成 3×0.001，与断言的像元单位距离对不上。
TEST_F(PluginTest, ProximityDistanceIsMeasuredToTargetNotBackground) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const int w = 11, h = 11;
    std::vector<float> data(static_cast<size_t>(w * h), 0.0f);
    data[5 * w + 5] = -3.0f;         // 目标像元（取负：见上面 R-T7-3 的饱和转换说明）
    data[0] = -9999.0f;              // NoData 像元，位置离目标 √50 个像元

    gis::tests::RasterFixtureOptions options;
    options.hasNoData = true;
    options.noData = -9999.0;
    options.pixelSize = 1.0;
    const std::string input = gis::tests::writeFloatRaster(
        getTestDir(), "prox_target_input.tif", w, h, data, options);
    ASSERT_FALSE(input.empty());

    const std::string output = utf8PathString(getTestDir() / "prox_target_output.tif");
    // PAM 边车按文件名匹配：残留的 .aux.xml 会把别处写过的 NoData 声明带进来，造成假绿/假红。
    std::error_code ec;
    fs::remove(output + ".aux.xml", ec);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("proximity");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    EXPECT_NEAR(gis::tests::readPixel(output, 1, 5, 5), 0.0f, 1e-6f);   // 目标自身
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 8, 5), 3.0f, 1e-6f);   // 到最近目标的距离

    // NoData 像元不得参与计算：它既不是目标、也不该拿到一个"到目标的距离"。
    // 这里断的是可观察的形态（N30）：输出声明了 NoData，且该像元的值就是这个 NoData。
    double noData = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 1, noData));
    EXPECT_DOUBLE_EQ(noData, -9999.0);
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 0), static_cast<float>(noData), 1e-6f);
}

// Task 7（R-T7-6）：修前的 `matToGdalTiff(..., b)` 在波段循环内，每个波段都重新创建整个文件，
// 前一个波段被覆盖掉 ⇒ 三波段输入最终只剩 1 个波段（与 Task 6 同型缺陷）。
// band1 全目标、band2 全背景、band3 全负值目标：波段数与"每个波段各自算自己的"一起被钉住。
TEST_F(PluginTest, ProximityOutputKeepsEveryInputBand) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.pixelSize = 1.0;
    const std::string input = gis::tests::writeConstantBandsRaster(
        getTestDir(), "prox_bands_input.tif", 4, 4, {1.0f, 0.0f, -2.0f}, options);
    ASSERT_FALSE(input.empty());
    ASSERT_EQ(gis::tests::readBandCount(input), 3);

    const std::string output = utf8PathString(getTestDir() / "prox_bands_output.tif");
    std::error_code ec;
    fs::remove(output + ".aux.xml", ec);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("proximity");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    EXPECT_EQ(gis::tests::readBandCount(output), 3);
    // 全目标波段：每个像元自身就是目标，距离恒为 0。
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 0), 0.0f, 1e-6f);
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 3, 3), 0.0f, 1e-6f);
    EXPECT_NEAR(gis::tests::readPixel(output, 3, 0, 0), 0.0f, 1e-6f);

    // R-T7-7：全背景波段（该波段没有任何目标像元）的期望值由**实测**得出，不是预测：
    // distanceTransform 的输入此时是全 255、一个零像元都没有，(0,0)/(3,3)/(1,2) 三处实测同为
    // 65536（三处读数一致这一事实本身就是本用例在钉的东西，取证见 task-7-report.md）。本用例按
    // 实测值照实钉住，不加特例分支把它修饰成 0。容差取 1e-2f：float32 在 2^16 附近的表示间隔
    // 本身就有 0.0078，比这更小的容差不稳定，而任何其它语义（0、真实距离）与该值的差都在数量级之上。
    //
    // 65536 不是算出来的距离，而是 OpenCV 的内部哨兵。出处：OpenCV 4.14.0 上游
    // modules/imgproc/src/distransform.cpp（行号按该版本的这个文件）：
    // :784 `static const int PRECISE_DIST_MAX = 1 << 16`、:872 `const unsigned int inf = UINT_MAX`、
    // :886/:904 `sqr_tab[i] = i >= PRECISE_DIST_MAX ? inf : i*i`、:887-888 把 sqr_tab[m..2m) 全设成 inf。
    // 列阶段 :760 把 dist 的起点设成 m-1，:764 在无零像元的列上只会递增 ⇒ dist 落进 [m, 2m) 而命中
    // :888 的 inf，:773 `dptr[0] = (float)sqr_tab[dist]` 于是把 (float)UINT_MAX 写进中间结果；行阶段
    // :859 `d[q] = std::sqrt(sqr_tab[std::abs(q - p)] + f[p])`。该式要严格等于 65536.0f 需要
    // |q-p| <= 27：加数是 float32（间隔 512）先舍一次、开方后再舍回 float32（间隔 0.0078125）又一次，
    // 按这两步次序逐值复算得 0..27 全是 65536.0f，28 起偏到 65536.0078125f。
    // 本用例是"整幅都没有目标"的形态：f[p] 对每个 p 恒等于同一个常量 (float)UINT_MAX，:815-852 建下
    // 包络时每个列号都留在包络上，:858 取到的 p 恒等于 q ⇒ |q-p| = 0 ⇒ 该形态的值与像元位置、与图像
    // 宽度都无关。这句"与宽度无关"只对**整幅无目标**的波段成立：某行只要有一部分像元带目标，
    // |q-p| 就可能 >= 28，65536 不再严格。
    // OpenCV 升版若把这条用例变红，先对照上面这段行号，它是内部哨兵而不是算法输出。
    EXPECT_NEAR(gis::tests::readPixel(output, 2, 0, 0), 65536.0f, 1e-2f);
    EXPECT_NEAR(gis::tests::readPixel(output, 2, 3, 3), 65536.0f, 1e-2f);
    EXPECT_NEAR(gis::tests::readPixel(output, 2, 1, 2), 65536.0f, 1e-2f);
}

// Task 7（R-T7-6 的鉴别力补强）：两个波段的目标各在自己波段的**不同列**上（band1 在 x=0，
// band2 在 x=4）。若实现把掩膜算到循环外、或各波段共用 band1 的数据（Task 6 修过的跨波段
// 污染同型问题），band2 会在 (0,2) 读到 0（被当成目标）而不是 4、在 (4,2) 读到 4 而不是 0；
// 变异实测见 task-7-report.md §5.2。
TEST_F(PluginTest, ProximityUsesEachBandOwnTargetsNotTheFirstBand) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const int w = 5, h = 5;
    const size_t n = static_cast<size_t>(w * h);
    std::vector<float> band1(n, 0.0f);
    std::vector<float> band2(n, 0.0f);
    band1[2 * w + 0] = 1.0f;   // band1 目标：x=0, y=2
    band2[2 * w + 4] = 1.0f;   // band2 目标：x=4, y=2

    gis::tests::RasterFixtureOptions options;
    options.pixelSize = 1.0;
    const std::string input = gis::tests::writeFloatBandsRaster(
        getTestDir(), "prox_perband_input.tif", w, h, {band1, band2}, options);
    ASSERT_FALSE(input.empty());

    const std::string output = utf8PathString(getTestDir() / "prox_perband_output.tif");
    std::error_code ec;
    fs::remove(output + ".aux.xml", ec);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("proximity");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    EXPECT_EQ(gis::tests::readBandCount(output), 2);
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 2), 0.0f, 1e-6f);   // band1 目标自身
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 4, 2), 4.0f, 1e-6f);   // 到 band1 自己的目标
    EXPECT_NEAR(gis::tests::readPixel(output, 2, 4, 2), 0.0f, 1e-6f);   // band2 目标自身
    EXPECT_NEAR(gis::tests::readPixel(output, 2, 0, 2), 4.0f, 1e-6f);   // 到 band2 自己的目标
}

// Task 7（R-T7-5）：NoData 赋值必须排在 max_distance 的 THRESH_TRUNC **之后**。
// NoData 故意取**正值** 9999 —— 先写 NoData 再截断时，9999 > max_distance 会被改写成
// max_distance，等于悄悄毁掉 NoData 标记（正的 NoData 值在 9999、3.4e38 这类产品里常见）。
TEST_F(PluginTest, ProximityMaxDistanceTruncationDoesNotClobberPositiveNoData) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const int w = 9, h = 1;
    std::vector<float> data(static_cast<size_t>(w * h), 0.0f);
    data[0] = 7.0f;      // 目标像元，位于 x=0
    data[8] = 9999.0f;   // NoData 像元，位于 x=8（到目标本征距离 8）

    gis::tests::RasterFixtureOptions options;
    options.hasNoData = true;
    options.noData = 9999.0;      // 正的 NoData：专门用来暴露"先写 NoData 再截断"的毁标记
    options.pixelSize = 1.0;
    const std::string input = gis::tests::writeFloatRaster(
        getTestDir(), "prox_maxdist_input.tif", w, h, data, options);
    ASSERT_FALSE(input.empty());

    const std::string output = utf8PathString(getTestDir() / "prox_maxdist_output.tif");
    std::error_code ec;
    fs::remove(output + ".aux.xml", ec);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("proximity");
    params["input"] = input;
    params["output"] = output;
    params["max_distance"] = 3.0;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 0), 0.0f, 1e-6f);   // 目标自身
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 2, 0), 2.0f, 1e-6f);   // 未超限：真实距离
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 5, 0), 3.0f, 1e-6f);   // 超限：截断为 max_distance
    // NoData 像元不得被截断改写成 3：它必须还是 9999。
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 8, 0), 9999.0f, 1e-6f);
}

// F2（审查发现 I2）：`dist *= std::abs(gt[1])` 这一行此前是零守护的。R-T7-1 让所有 proximity 用例
// 都设 pixelSize = 1.0，×1.0 是恒等，审查者把这一行整行删掉重建后 Debug 全量仍 526/526 全绿
// （build/task7_review/full_mutantA_debug.log:1055）。这一条**只**为那一行存在：pixelSize = 0.5 时
// 删掉它 (3,0) 读回 3.0、留着才读回 1.5，别的用例都在 1.0 上取不到这个差别（现有用例的 1.0 口径
// 仍属 Plan 2，不为这一条去改）。
TEST_F(PluginTest, ProximityDistanceIsScaledByPixelSize) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const int w = 4, h = 4;
    std::vector<float> data(static_cast<size_t>(w) * h, 0.0f);
    data[0] = 2.0f;   // 目标像元置于一角 (0,0)

    gis::tests::RasterFixtureOptions options;
    options.pixelSize = 0.5;
    const std::string input = gis::tests::writeFloatRaster(
        getTestDir(), "prox_scale_input.tif", w, h, data, options);
    ASSERT_FALSE(input.empty());

    const std::string output = utf8PathString(getTestDir() / "prox_scale_output.tif");
    std::error_code ec;
    fs::remove(output, ec);
    fs::remove(output + ".aux.xml", ec);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("proximity");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 0), 0.0f, 1e-6f);    // 目标自身不受缩放影响
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 3, 0), 1.5f, 1e-6f);    // 3 像元 × 0.5
}

// F3（审查发现 M1 之①）：整波段全是 NoData 的形态此前没有任何用例覆盖。
// 该波段每个像元都被 validMask 排除 ⇒ targetMask 全 0 ⇒ distanceTransform 落进"没有零像元可参照"
// 的分支给出 65536（出处见 ProximityOutputKeepsEveryInputBand 的注释），但最后
// dist.setTo(nodata, validMask == 0) 把整幅覆盖回来 ⇒ 正确行为是全为该波段的 NoData 值。
// 这条钉的是"覆盖回来"这一步；把它删掉，输出就是 65536 而不是 -9999。
TEST_F(PluginTest, ProximityAllNoDataBandWritesItsNoDataInsteadOfTheSentinel) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const int w = 4, h = 4;
    const size_t n = static_cast<size_t>(w) * h;
    std::vector<float> allNoData(n, -9999.0f);        // band1：整幅都是 NoData
    std::vector<float> withTarget(n, 0.0f);
    withTarget[2 * w + 1] = 3.0f;                    // band2：目标在 (1,2)

    gis::tests::RasterFixtureOptions options;
    options.hasNoData = true;
    options.noData = -9999.0;
    options.pixelSize = 1.0;
    const std::string input = gis::tests::writeFloatBandsRaster(
        getTestDir(), "prox_allnodata_input.tif", w, h, {allNoData, withTarget}, options);
    ASSERT_FALSE(input.empty());

    const std::string output = utf8PathString(getTestDir() / "prox_allnodata_output.tif");
    std::error_code ec;
    fs::remove(output, ec);
    fs::remove(output + ".aux.xml", ec);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("proximity");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    EXPECT_EQ(gis::tests::readBandCount(output), 2);
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 0, 0), -9999.0f, 1e-6f);
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 3, 3), -9999.0f, 1e-6f);   // 不是 65536

    // 同一次运行里另一波段照常算自己的距离：整幅 NoData 没污染兄弟波段。
    EXPECT_NEAR(gis::tests::readPixel(output, 2, 1, 2), 0.0f, 1e-6f);
    EXPECT_NEAR(gis::tests::readPixel(output, 2, 1, 0), 2.0f, 1e-6f);
}

// F1（审查发现 I1）的触发形态。本项目自己的多波段 GeoTIFF **造不出**互异的逐波段 NoData 声明：
// createRaster 用 Create(..., nullptr)（gdal_wrapper.cpp:63-64）⇒ 单 IFD、全文件只有一个
// TIFFTAG_GDAL_NODATA，逐波段 SetNoDataValue 只有最后一个留得住（实测见 task-7-report.md 的 PAM 取证）。
// 所以互异的逐波段声明只能来自 VRT / 外部多 IFD 输入：这里内联 VRT XML，每个波段引一个自己的
// 单波段子 GeoTIFF，子数据集各自声明自己的 NoData；hasNoData=false 表示该波段根本不声明。
struct ProximityVrtBand {
    float pixelValue = 0.0f;   // 写在 (4,2) 的像元值
    bool hasNoData = false;
    double noData = 0.0;
};

// 目标像元固定在 (0,0)（值 1.0），像元大小 1.0 ⇒ (4,0) 的距离恒为 4，可用来确认计算照常跑完。
static std::string writeProximityNoDataVrt(const std::string& stem, int width, int height,
                                           const std::vector<ProximityVrtBand>& bands) {
    const size_t n = static_cast<size_t>(width) * height;
    std::string xml = "<VRTDataset rasterXSize=\"" + std::to_string(width) +
                      "\" rasterYSize=\"" + std::to_string(height) + "\">\n";
    gis::tests::RasterFixtureOptions options;
    options.pixelSize = 1.0;
    for (size_t i = 0; i < bands.size(); ++i) {
        const auto& band = bands[i];
        std::vector<float> data(n, 0.0f);
        data[0] = 1.0f;
        data[static_cast<size_t>(2) * static_cast<size_t>(width) + 4] = band.pixelValue;
        options.hasNoData = band.hasNoData;
        options.noData = band.noData;
        const std::string subName = stem + "_b" + std::to_string(i + 1) + ".tif";
        if (gis::tests::writeFloatRaster(getTestDir(), subName, width, height, data, options).empty()) {
            return {};
        }
        std::error_code ec;
        fs::remove(utf8PathString(getTestDir() / subName) + ".aux.xml", ec);

        xml += "  <VRTRasterBand dataType=\"Float32\" band=\"" + std::to_string(i + 1) + "\">\n";
        if (band.hasNoData) {
            xml += "    <NoDataValue>" + std::to_string(band.noData) + "</NoDataValue>\n";
        }
        xml += "    <SimpleSource><SourceFilename relativeToVRT=\"1\">" + subName +
               "</SourceFilename><SourceBand>1</SourceBand>"
               "<SrcRect xOff=\"0\" yOff=\"0\" xSize=\"" + std::to_string(width) +
               "\" ySize=\"" + std::to_string(height) + "\"/>"
               "<DstRect xOff=\"0\" yOff=\"0\" xSize=\"" + std::to_string(width) +
               "\" ySize=\"" + std::to_string(height) + "\"/></SimpleSource>\n"
               "  </VRTRasterBand>\n";
    }
    xml += "</VRTDataset>\n";

    const std::string vrt = utf8PathString(getTestDir() / (stem + ".vrt"));
    std::error_code ec;
    fs::remove(vrt, ec);
    fs::remove(vrt + ".aux.xml", ec);
    std::ofstream os(vrt);
    os << xml;
    os.close();
    return vrt;
}

// F1（形态 a：各波段声明互不相同的值）。输入 band1 声明 -9999、band2 声明 500，两波段的
// NoData 像元各自写自己的值；输出只能是"一个共享声明"，于是 band1 的声明丢失，
// 而它的 NoData 像元物理值仍是 -9999 ⇒ 下游按文件声明把 -9999 当成合法距离读。
// 告警不失败：多源/同传感器产品的波段间差异是常见输入，硬失败会打断正常用法。
TEST_F(PluginTest, ProximityWarnsWhenBandNoDataDeclarationsDiffer) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const std::string vrt = writeProximityNoDataVrt("prox_nodata_differ", 5, 3, {
        {-9999.0f, true, -9999.0},
        {  500.0f, true,   500.0},
    });
    ASSERT_FALSE(vrt.empty());

    // 输入侧必须真的互异，否则这条用例测的就不是"分歧"。
    double inNoData1 = 0.0;
    double inNoData2 = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(vrt, 1, inNoData1));
    ASSERT_TRUE(gis::tests::readNoDataValue(vrt, 2, inNoData2));
    ASSERT_DOUBLE_EQ(inNoData1, -9999.0);
    ASSERT_DOUBLE_EQ(inNoData2, 500.0);

    const std::string output = utf8PathString(getTestDir() / "prox_nodata_differ_out.tif");
    std::error_code ec;
    fs::remove(output, ec);
    // 边车会把别处写过的声明带进来（实测：手放的 .aux.xml 让 band1 从 500 变成 777），
    // 而本用例断的正是"哪个声明留下来了"。
    fs::remove(output + ".aux.xml", ec);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("proximity");
    params["input"] = vrt;
    params["output"] = output;

    RecordingProgress recorder;
    const auto result = p->execute(params, recorder);
    ASSERT_TRUE(result.success) << result.message;

    EXPECT_EQ(result.metadata.count("nodata_divergent_band_count"), 1u);
    EXPECT_EQ(result.metadata.at("nodata_divergent_band_count"), "1");
    // 告警要能到用户手里：onMessage 经 qt_progress_reporter.h:13,23 直达 GUI 日志面板，
    // CLI/GUI 摘要都会遍历 Result::metadata（cli/main.cpp:240-245、gui_data_support.cpp:1924-1931）。
    EXPECT_NE(recorder.joined().find("nodata_divergent_band_count"), std::string::npos)
        << recorder.joined();

    EXPECT_EQ(gis::tests::readBandCount(output), 2);
    double outNoData1 = 0.0;
    double outNoData2 = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 1, outNoData1));
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 2, outNoData2));
    EXPECT_DOUBLE_EQ(outNoData1, outNoData2);      // 文件里只剩一个共享声明
    // R2-F6：这一句钉的是"band1 自己的声明没保住"，所以拿它和 band1 的**输入**声明比，而不是和某个
    // 字面量比——"等于 500"那种写法钉的是覆写顺序（后写胜出）这个实现选择，改天换成首个留得住就会红。
    EXPECT_NE(outNoData1, inNoData1);
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 4, 2), -9999.0f, 1e-6f);   // 像元还写着 band1 自己的值
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 4, 0), 4.0f, 1e-6f);       // 距离本身照常算出来了
}

// F1（形态 b：部分波段声明、部分未声明）。band2 根本不声明 NoData，但单 IFD 只有一个共享标签，
// 重开后 band2 也被贴上 band1 的 -9999 ⇒ 它那些合法值 -9999 会被下游当成 NoData。
// 只断"部分声明也算分歧"，与上一条分开放，是为了让失败归因能指到分歧判定的具体那一支。
TEST_F(PluginTest, ProximityWarnsWhenSomeBandsDoNotDeclareNoData) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const std::string vrt = writeProximityNoDataVrt("prox_nodata_partial", 5, 3, {
        {-9999.0f, true, -9999.0},
        {-9999.0f, false, 0.0},   // 不声明：-9999 在这个波段里是合法目标值
    });
    ASSERT_FALSE(vrt.empty());

    double inNoData1 = 0.0;
    double inNoData2 = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(vrt, 1, inNoData1));
    ASSERT_DOUBLE_EQ(inNoData1, -9999.0);
    EXPECT_FALSE(gis::tests::readNoDataValue(vrt, 2, inNoData2)) << "band2 本条用例的前提是不声明";

    const std::string output = utf8PathString(getTestDir() / "prox_nodata_partial_out.tif");
    std::error_code ec;
    fs::remove(output, ec);
    fs::remove(output + ".aux.xml", ec);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("proximity");
    params["input"] = vrt;
    params["output"] = output;

    RecordingProgress recorder;
    const auto result = p->execute(params, recorder);
    ASSERT_TRUE(result.success) << result.message;

    EXPECT_EQ(result.metadata.count("nodata_divergent_band_count"), 1u);
    EXPECT_EQ(result.metadata.at("nodata_divergent_band_count"), "1");
    EXPECT_NE(recorder.joined().find("nodata_divergent_band_count"), std::string::npos)
        << recorder.joined();

    EXPECT_EQ(gis::tests::readBandCount(output), 2);
    double outNoData2 = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 2, outNoData2));
    EXPECT_DOUBLE_EQ(outNoData2, -9999.0);   // 没声明的波段被贴上了 band1 的声明
}

// F1 的反方向（告警必须"分歧不存在时必不出"）：与上一条同构的 VRT，只是两个波段声明**同一个**值。
// 驱动、波段数、像元布局全都相同，唯一变量是声明是否一致，所以这条红了就只能怪"分歧判定写宽了"。
// 变异取证（无分歧也告警）见 task-7-report.md。
TEST_F(PluginTest, ProximityStaysSilentWhenNoDataDeclarationIsConsistent) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const std::string vrt = writeProximityNoDataVrt("prox_nodata_same", 5, 3, {
        {-9999.0f, true, -9999.0},
        {-9999.0f, true, -9999.0},
    });
    ASSERT_FALSE(vrt.empty());

    const std::string output = utf8PathString(getTestDir() / "prox_nodata_same_out.tif");
    std::error_code ec;
    fs::remove(output, ec);
    fs::remove(output + ".aux.xml", ec);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("proximity");
    params["input"] = vrt;
    params["output"] = output;

    RecordingProgress recorder;
    const auto result = p->execute(params, recorder);
    ASSERT_TRUE(result.success) << result.message;

    EXPECT_EQ(result.metadata.count("nodata_divergent_band_count"), 0u);
    EXPECT_EQ(recorder.joined().find("nodata_divergent_band_count"), std::string::npos)
        << recorder.joined();

    // 声明一致时输出是自洽的：共享声明就是各波段自己的值。
    EXPECT_EQ(gis::tests::readBandCount(output), 2);
    double outNoData = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 1, outNoData));
    EXPECT_DOUBLE_EQ(outNoData, -9999.0);
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 4, 2), -9999.0f, 1e-6f);
}

// R2-F2（fix round 2，形态 (A,A,B)）：三波段时"与第一个波段不一致"和"与存活声明不一致"不再数值相等，
// 现有的 2 波段用例区分不了这两种参照。输出里真正活下来的声明是**最后一个有声明的波段**
// （opencv_wrapper.cpp:96-104 只对源里声明了 NoData 的波段调 SetNoDataValue，而 Create(..., nullptr)
// 出的多波段 GeoTIFF 是单 IFD、只有一个 TIFFTAG_GDAL_NODATA，后写覆盖前写），所以 band1 与 band2
// 的两个 -9999 都会丢 ⇒ 计数必须是 2。参照若写成"第一个波段"，这条算出 1（少报）⇒ 对参照选错有牙。
TEST_F(PluginTest, ProximityCountsEveryBandWhoseNoDataDeclarationIsNotKept) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const std::string vrt = writeProximityNoDataVrt("prox_nodata_aab", 5, 3, {
        {-9999.0f, true, -9999.0},
        {-9999.0f, true, -9999.0},
        {  500.0f, true,   500.0},
    });
    ASSERT_FALSE(vrt.empty());

    // 输入侧必须真的是 (A,A,B)，否则这条测的就不是"存活声明之前的那两个波段"。
    double inNoData1 = 0.0;
    double inNoData2 = 0.0;
    double inNoData3 = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(vrt, 1, inNoData1));
    ASSERT_TRUE(gis::tests::readNoDataValue(vrt, 2, inNoData2));
    ASSERT_TRUE(gis::tests::readNoDataValue(vrt, 3, inNoData3));
    ASSERT_DOUBLE_EQ(inNoData1, -9999.0);
    ASSERT_DOUBLE_EQ(inNoData2, -9999.0);
    ASSERT_DOUBLE_EQ(inNoData3, 500.0);

    const std::string output = utf8PathString(getTestDir() / "prox_nodata_aab_out.tif");
    std::error_code ec;
    fs::remove(output, ec);
    // 边车会把别处写过的声明静默带进来（实测见 task-7-report.md 的 PAM 取证），而本用例断的正是
    // "哪个声明留下来了"；子文件的 .aux.xml 由 writeProximityNoDataVrt 自己清掉。
    fs::remove(output + ".aux.xml", ec);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("proximity");
    params["input"] = vrt;
    params["output"] = output;

    RecordingProgress recorder;
    const auto result = p->execute(params, recorder);
    ASSERT_TRUE(result.success) << result.message;

    EXPECT_EQ(result.metadata.count("nodata_divergent_band_count"), 1u);
    EXPECT_EQ(result.metadata.at("nodata_divergent_band_count"), "2");
    EXPECT_NE(recorder.joined().find("nodata_divergent_band_count"), std::string::npos)
        << recorder.joined();

    // 危害本身：三波段重开后只能报告同一个声明，band1 与 band2 各自的 -9999 都不在了，
    // 而最后一个有声明的波段（band3）保住了自己的 ⇒ 这正是计数所依据的参照。
    EXPECT_EQ(gis::tests::readBandCount(output), 3);
    double outNoData1 = 0.0;
    double outNoData2 = 0.0;
    double outNoData3 = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 1, outNoData1));
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 2, outNoData2));
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 3, outNoData3));
    EXPECT_DOUBLE_EQ(outNoData1, outNoData2);
    EXPECT_DOUBLE_EQ(outNoData1, outNoData3);
    EXPECT_DOUBLE_EQ(outNoData3, inNoData3);
    EXPECT_NE(outNoData1, inNoData1);
    EXPECT_NE(outNoData2, inNoData2);

    // 只告警不失败：band1 的目标在 (0,0)，(4,0) 的距离照常是 4。
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 4, 0), 4.0f, 1e-6f);
}

// R2-F2（fix round 2，形态 (A,B,B)）：与上一条方向相反。存活声明是 band3 的 500，band2 声明同一个值
// ⇒ 只有 band1 的 -9999 会丢，计数必须是 1。参照写成"第一个波段"时这条算出 2（多报），
// 于是两条用例一起把参照的两个方向都封住（缺任一条，另一种写错的参照都能全绿）。
TEST_F(PluginTest, ProximityDoesNotCountBandsWhoseNoDataDeclarationSurvives) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const std::string vrt = writeProximityNoDataVrt("prox_nodata_abb", 5, 3, {
        {-9999.0f, true, -9999.0},
        {  500.0f, true,   500.0},
        {  500.0f, true,   500.0},
    });
    ASSERT_FALSE(vrt.empty());

    double inNoData1 = 0.0;
    double inNoData2 = 0.0;
    double inNoData3 = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(vrt, 1, inNoData1));
    ASSERT_TRUE(gis::tests::readNoDataValue(vrt, 2, inNoData2));
    ASSERT_TRUE(gis::tests::readNoDataValue(vrt, 3, inNoData3));
    ASSERT_DOUBLE_EQ(inNoData1, -9999.0);
    ASSERT_DOUBLE_EQ(inNoData2, 500.0);
    ASSERT_DOUBLE_EQ(inNoData3, 500.0);

    const std::string output = utf8PathString(getTestDir() / "prox_nodata_abb_out.tif");
    std::error_code ec;
    fs::remove(output, ec);
    fs::remove(output + ".aux.xml", ec);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("proximity");
    params["input"] = vrt;
    params["output"] = output;

    RecordingProgress recorder;
    const auto result = p->execute(params, recorder);
    ASSERT_TRUE(result.success) << result.message;

    EXPECT_EQ(result.metadata.count("nodata_divergent_band_count"), 1u);
    EXPECT_EQ(result.metadata.at("nodata_divergent_band_count"), "1");
    EXPECT_NE(recorder.joined().find("nodata_divergent_band_count"), std::string::npos)
        << recorder.joined();

    // 三波段只剩一个共享声明（危害本身），且它等于 band2/band3 自己的声明 ⇒ 那两个波段没被骗。
    EXPECT_EQ(gis::tests::readBandCount(output), 3);
    double outNoData1 = 0.0;
    double outNoData2 = 0.0;
    double outNoData3 = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 1, outNoData1));
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 2, outNoData2));
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 3, outNoData3));
    EXPECT_DOUBLE_EQ(outNoData1, outNoData2);
    EXPECT_DOUBLE_EQ(outNoData2, outNoData3);
    EXPECT_DOUBLE_EQ(outNoData2, inNoData2);
    EXPECT_NE(outNoData1, inNoData1);

    EXPECT_NEAR(gis::tests::readPixel(output, 1, 4, 0), 4.0f, 1e-6f);
}

// R2-F5（fix round 2）：各波段都声明 NaN 型 NoData（外部产品里可能出现）时不得报分歧。
// `current.value != survivor.value` 对 NaN 恒真（IEEE 的 NaN != NaN），三波段全都声明 nan 会被判成
// "两个波段分歧"⇒ 假告警。这里用现成的 VRT 辅助把 NaN 写进 <NoDataValue>，没有为此扩夹具。
// 输入侧三条前置断言先钉住"VRT 确实把 nan 解析成了 NaN"：若哪天解析不出来了，本用例会红在前置，
// 不会静默退化成一条什么都没测的假绿。像元值不断：NoData 为 NaN 时 OpenCV compare 的语义不在
// 本用例要钉的范围里。
TEST_F(PluginTest, ProximityStaysSilentWhenEveryBandDeclaresNaNNoData) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const double nanNoData = std::numeric_limits<double>::quiet_NaN();
    const std::string vrt = writeProximityNoDataVrt("prox_nodata_allnan", 5, 3, {
        {0.0f, true, nanNoData},
        {0.0f, true, nanNoData},
        {0.0f, true, nanNoData},
    });
    ASSERT_FALSE(vrt.empty());

    double inNoData1 = 0.0;
    double inNoData2 = 0.0;
    double inNoData3 = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(vrt, 1, inNoData1))
        << "前提：VRT 要把 <NoDataValue>nan</NoDataValue> 解析成 NaN，否则这条用例什么都没测";
    ASSERT_TRUE(std::isnan(inNoData1)) << "band1 实测读到 " << inNoData1;
    ASSERT_TRUE(gis::tests::readNoDataValue(vrt, 2, inNoData2));
    ASSERT_TRUE(std::isnan(inNoData2)) << "band2 实测读到 " << inNoData2;
    ASSERT_TRUE(gis::tests::readNoDataValue(vrt, 3, inNoData3));
    ASSERT_TRUE(std::isnan(inNoData3)) << "band3 实测读到 " << inNoData3;

    const std::string output = utf8PathString(getTestDir() / "prox_nodata_allnan_out.tif");
    std::error_code ec;
    fs::remove(output, ec);
    fs::remove(output + ".aux.xml", ec);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("proximity");
    params["input"] = vrt;
    params["output"] = output;

    RecordingProgress recorder;
    const auto result = p->execute(params, recorder);
    ASSERT_TRUE(result.success) << result.message;

    EXPECT_EQ(result.metadata.count("nodata_divergent_band_count"), 0u);
    EXPECT_EQ(recorder.joined().find("nodata_divergent_band_count"), std::string::npos)
        << recorder.joined();
}

// R-T7-9：`doProximity` 只读 input/output/max_distance（取证：band/nodata_value 只在
// raster_manage_plugin.cpp 的 :207/:208 nodata 动作里读，levels 在 :81，vector/feature_id_field
// 在 :258/:260），但修前 gui_data_support.cpp 的 raster_manage 配置里没有 proximity 条目
// ⇒ mainwindow.cpp:523-527 把空集当"不设白名单"传下去，面板会渲染插件级全部 9 个参数，
// 其中六个对本动作无用。这正是批次 A 要消灭的"面板上有用了没用的参数"形态。
TEST_F(PluginTest, ProximityGuiPanelHidesParametersTheActionNeverReads) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const auto* config = gis::gui::findActionUiConfig("raster_manage", "proximity");
    ASSERT_NE(config, nullptr)
        << "proximity 没有 ActionUiConfig 条目：展示层没有白名单可用，面板会渲染全部插件参数";
    EXPECT_EQ(config->visibleKeys,
              (std::set<std::string>{"input", "output", "max_distance"}));
    EXPECT_EQ(config->requiredKeys, (std::set<std::string>{"input", "output"}));
    // R-T10-1：buildEffectiveGuiParamSpecs 无条件 continue 掉 "action"，白名单里写它既不生效
    // 又与既有约定分叉，所以这里把它当成"不得出现"来锁。
    EXPECT_EQ(config->visibleKeys.count("action"), 0u);

    const auto effective = gis::gui::buildEffectiveGuiParamSpecs(
        "raster_manage", "proximity", p->paramSpecs(), config->visibleKeys, config->requiredKeys);
    std::set<std::string> effectiveKeys;
    for (const auto& spec : effective) {
        effectiveKeys.insert(spec.key);
    }
    EXPECT_EQ(effectiveKeys, (std::set<std::string>{"input", "output", "max_distance"}));
    for (const char* deadKey : {"band", "levels", "resample", "nodata_value", "vector",
                                "feature_id_field"}) {
        EXPECT_EQ(effectiveKeys.count(deadKey), 0u) << deadKey << " 从未被 doProximity 读取";
    }

    // requiredKeys 是加严方向：input 在插件级本就必填，output 是 false 但 doProximity 对空
    // output 直接 fail，所以标成必填不会让 GUI 提交出 CLI 会拒的表单。
    for (const auto& spec : effective) {
        if (spec.key == "input" || spec.key == "output") {
            EXPECT_TRUE(spec.required) << spec.key;
        } else {
            EXPECT_FALSE(spec.required) << spec.key;
        }
    }

    EXPECT_NE(gis::gui::actionDisplayName("raster_manage", "proximity"), QStringLiteral("proximity"));
    EXPECT_FALSE(gis::gui::actionDescription("raster_manage", "proximity").isEmpty());
}

TEST_F(PluginTest, MatchingStitchExecution) {
    auto* p = mgr_.find("matching");
    ASSERT_NE(p, nullptr);

    const std::string img1 = createPatternRaster("e2e_stitch_img1.tif");
    const std::string img2 = createPatternRaster("e2e_stitch_img2.tif");
    const std::string output = utf8PathString(getTestDir() / "e2e_stitch_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("stitch");
    params["input"] = img1 + "," + img2;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    if (!result.success) {
        EXPECT_NE(result.message.find("lightweight"), std::string::npos)
            << "Unexpected failure: " << result.message;
        return;
    }
    EXPECT_TRUE(fs::exists(output));
    ASSERT_TRUE(result.metadata.count("input_count") > 0);
    EXPECT_EQ(result.metadata.at("input_count"), "2");
    ASSERT_TRUE(result.metadata.count("output_width") > 0);
    ASSERT_TRUE(result.metadata.count("output_height") > 0);
    EXPECT_GT(std::stoi(result.metadata.at("output_width")), 0);
    EXPECT_GT(std::stoi(result.metadata.at("output_height")), 0);
}

TEST_F(PluginTest, MatchingEccRegisterExecution) {
    auto* p = mgr_.find("matching");
    ASSERT_NE(p, nullptr);

    const std::string reference = createPatternRaster("e2e_ecc_ref.tif");
    const std::string input = reference;
    const std::string output = utf8PathString(getTestDir() / "e2e_ecc_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("ecc_register");
    params["reference"] = reference;
    params["input"] = input;
    params["output"] = output;
    params["ecc_motion"] = std::string("affine");
    params["ecc_iterations"] = 50;
    params["ecc_epsilon"] = 1e-5;

    const auto result = p->execute(params, progress_);
    if (!result.success) {
        EXPECT_NE(result.message.find("lightweight"), std::string::npos)
            << "Unexpected failure: " << result.message;
        return;
    }
    EXPECT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("motion_type"), "affine");
    ASSERT_TRUE(result.metadata.count("warp_matrix") > 0);

    auto outDs = gis::core::openRaster(output, true);
    ASSERT_NE(outDs, nullptr);
    EXPECT_EQ(outDs->GetRasterXSize(), 96);
    EXPECT_EQ(outDs->GetRasterYSize(), 96);
}

TEST_F(PluginTest, ClassificationAccuracyAssessmentExecution) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    const std::string classifiedPath = (getTestDir() / "e2e_accuracy_classified.tif").string();
    const std::string referencePath = (getTestDir() / "e2e_accuracy_reference.tif").string();
    const std::string outputPath = utf8PathString(getTestDir() / "e2e_accuracy_output.json");

    {
        auto classifiedDs = gis::core::createRaster(classifiedPath, 20, 10, 1, GDT_Int32);
        double adfGT[6] = {116.0, 0.001, 0.0, 40.0, 0.0, -0.001};
        classifiedDs->SetGeoTransform(adfGT);
        std::vector<int32_t> data(20 * 10);
        for (int i = 0; i < 20 * 10; ++i) data[i] = (i < 100) ? 1 : 2;
        classifiedDs->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, 20, 10,
            data.data(), 20, 10, GDT_Int32, 0, 0);
        classifiedDs->GetRasterBand(1)->FlushCache();
    }

    {
        auto referenceDs = gis::core::createRaster(referencePath, 20, 10, 1, GDT_Int32);
        double adfGT[6] = {116.0, 0.001, 0.0, 40.0, 0.0, -0.001};
        referenceDs->SetGeoTransform(adfGT);
        std::vector<int32_t> data(20 * 10);
        for (int i = 0; i < 20 * 10; ++i) data[i] = (i < 100) ? 1 : 2;
        referenceDs->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, 20, 10,
            data.data(), 20, 10, GDT_Int32, 0, 0);
        referenceDs->GetRasterBand(1)->FlushCache();
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("accuracy_assessment");
    params["classified_raster"] = classifiedPath;
    params["reference_raster"] = referencePath;
    params["output"] = outputPath;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_TRUE(fs::exists(outputPath));
    EXPECT_EQ(result.metadata.at("action"), "accuracy_assessment");
    EXPECT_NEAR(std::stod(result.metadata.at("overall_accuracy")), 1.0, 1e-6);
    ASSERT_TRUE(result.metadata.count("kappa") > 0);
    ASSERT_TRUE(result.metadata.count("class_count") > 0);
    EXPECT_EQ(result.metadata.at("class_count"), "2");
}

// Task 8：混淆矩阵按 confusionMatrix[参考值][分类值] 构建（classification_plugin.cpp:2151），
// 于是"参考类总数"是行和、"分类图类总数"是列和；修前 PA 取了列和、UA 取了行和，两者互换。
// 夹具刻意不含 0 值像元，把 PA/UA 归属和 NoData 判定缺陷解耦——本用例红只可能来自互换。
TEST_F(PluginTest, AccuracyAssessmentUsesStandardPaUa) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    // 参考: 1 1 / 2 2    分类: 1 2 / 2 2
    // 混淆(行=参考, 列=分类): class1 {1:1, 2:1}; class2 {1:0, 2:2}
    // 标准定义：PA(1)=1/2=0.5  PA(2)=2/2=1.0 ; UA(1)=1/1=1.0  UA(2)=2/3≈0.666667
    const std::string reference = gis::tests::writeFloatRaster(
        getTestDir(), "acc_ref.tif", 2, 2, {1.0f, 1.0f, 2.0f, 2.0f});
    const std::string classified = gis::tests::writeFloatRaster(
        getTestDir(), "acc_cls.tif", 2, 2, {1.0f, 2.0f, 2.0f, 2.0f});
    ASSERT_FALSE(reference.empty());
    ASSERT_FALSE(classified.empty());
    const std::string output = utf8PathString(getTestDir() / "acc_output.json");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("accuracy_assessment");
    params["classified_raster"] = classified;
    params["reference_raster"] = reference;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    std::ifstream ifs(output, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    // Qt 6 起 QByteArray 不再有迭代器区间构造，只能显式按 (data, size) 建。
    const std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    const QByteArray text(content.data(), static_cast<qsizetype>(content.size()));
    const QJsonDocument document = QJsonDocument::fromJson(text);
    ASSERT_TRUE(document.isObject()) << content;
    const QJsonObject root = document.object();
    ASSERT_TRUE(root.contains("producers_accuracy")) << content;
    ASSERT_TRUE(root.contains("users_accuracy"));

    const QJsonObject pa = root.value("producers_accuracy").toObject();
    const QJsonObject ua = root.value("users_accuracy").toObject();
    EXPECT_NEAR(pa.value("1").toDouble(), 0.5, 1e-6) << content;
    EXPECT_NEAR(pa.value("2").toDouble(), 1.0, 1e-6) << content;
    EXPECT_NEAR(ua.value("1").toDouble(), 1.0, 1e-6) << content;
    EXPECT_NEAR(ua.value("2").toDouble(), 2.0 / 3.0, 1e-6) << content;
}

// Task 8（R-T8-1）：GetNoDataValue() 不带 pbSuccess 时，"没定义 NoData"会被读成"NoData 就是 0"，
// 于是 0 类像元被整类剔除出混淆矩阵——改变的是 totalValid/总体精度/kappa，比 PA/UA 换名字更伤。
// 判据链：夹具先自证两个栅格都**没有**声明 NoData，再要求类别 0 出现在 PA/UA 与计数里。
TEST_F(PluginTest, AccuracyAssessmentKeepsZeroClassWhenNoDataUndefined) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    // 参考 {0,0,1,1}、分类 {0,1,1,1}，两栅格均未定义 NoData → 0 是合法类别，不是空洞
    // 混淆(行=参考): cls0=1, cls1=1；cls0 行和=2；cls1 行和=2；cls0 列和=1；cls1 列和=3
    // 正确：totalValid=4、overall=3/4=0.75，PA(0)=0.5，UA(0)=1.0，类别 0 必须出现在 PA/UA 里
    // 修前：0 值像元被当 NoData 丢掉 → total_samples=2、overall=1.0、类别 0 整个消失
    const std::string reference = gis::tests::writeFloatRaster(
        getTestDir(), "acc_zero_ref.tif", 2, 2, {0.0f, 0.0f, 1.0f, 1.0f});
    const std::string classified = gis::tests::writeFloatRaster(
        getTestDir(), "acc_zero_cls.tif", 2, 2, {0.0f, 1.0f, 1.0f, 1.0f});
    ASSERT_FALSE(reference.empty());
    ASSERT_FALSE(classified.empty());

    // PAM 边车按文件名匹配：残留的 .aux.xml 会把声明值带进来，让"未定义 NoData"这一前提失效。
    std::error_code ec;
    fs::remove(reference + ".aux.xml", ec);
    fs::remove(classified + ".aux.xml", ec);
    double declared = 0.0;
    EXPECT_FALSE(gis::tests::readNoDataValue(reference, 1, declared)) << reference;
    EXPECT_FALSE(gis::tests::readNoDataValue(classified, 1, declared)) << classified;

    const std::string output = utf8PathString(getTestDir() / "acc_zero_output.json");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("accuracy_assessment");
    params["classified_raster"] = classified;
    params["reference_raster"] = reference;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    std::ifstream ifs(output, std::ios::binary);
    ASSERT_TRUE(ifs.is_open());
    // Qt 6 起 QByteArray 不再有迭代器区间构造，只能显式按 (data, size) 建。
    const std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    const QByteArray text(content.data(), static_cast<qsizetype>(content.size()));
    const QJsonDocument document = QJsonDocument::fromJson(text);
    ASSERT_TRUE(document.isObject()) << content;
    const QJsonObject root = document.object();

    const QJsonObject pa = root.value("producers_accuracy").toObject();
    const QJsonObject ua = root.value("users_accuracy").toObject();
    ASSERT_TRUE(pa.contains("0")) << content;
    ASSERT_TRUE(ua.contains("0")) << content;
    EXPECT_NEAR(pa.value("0").toDouble(), 0.5, 1e-6) << content;
    EXPECT_NEAR(ua.value("0").toDouble(), 1.0, 1e-6) << content;
    // 类别 "1" 的值必须与第一个用例同值，防止"0 被塞回去但只塞了个数"。
    EXPECT_NEAR(pa.value("1").toDouble(), 1.0, 1e-6) << content;
    EXPECT_NEAR(ua.value("1").toDouble(), 2.0 / 3.0, 1e-6) << content;

    EXPECT_NEAR(root.value("overall_accuracy").toDouble(), 0.75, 1e-6) << content;
    EXPECT_EQ(result.metadata.at("total_samples"), "4") << content;
    EXPECT_EQ(result.metadata.at("class_count"), "2") << content;
}
