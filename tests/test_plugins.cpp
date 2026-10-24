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
#include <algorithm>
#include <cmath>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "raster_fixture.h"
#include "test_support.h"
// R-T7-9 的用例要核对展示层的 per-action 白名单；gis_tests 把 gui_data_support.cpp 编进同一个
// 可执行文件，gui 头文件不在 include 路径中，故沿用 test_coverage_meta.cpp:5 的相对路径写法。
#include "../src/gui/gui_data_support.h"
// R-T10-9 的"渲染键"这一层要问真实 widget 树：判据是 ParamCardWidget::hasParam（entries_，
// 由 addParam 逐条填充）；ParamWidget::hasParam 只扫 specs_，断不出控件行是否真的建出来。
#include "../src/gui/param_widget.h"
#include "../src/gui/param_card_widget.h"

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

// Task 15 / R-T15-2：下面三条改前的红形态是**未捕获异常导致的进程级失败**——异常逃出 execute()，
// 不是 result.success == false 这种正常失败返回。
// processing_plugin.cpp 全文没有任何 try/catch（54df42c 与 17bfc76 各实测 grep 零命中），
// 而 OpenCV 4.14 的 cv::Scharr 走 getScharrKernels 里的 CV_Assert(dx>=0 && dy>=0 && dx+dy==1)
// ⇒ cv::Exception 未被任何供应商代码接住、抛进测试体，由 gtest 的用例级异常处理记该用例 FAILED
// （日志原文：`unknown file: error: C++ exception with description "OpenCV(4.14.0) ...deriv.cpp:66 ...`
//   `... thrown in the test body.`）。
// 但它**不终止整个测试可执行文件、也不截断整跑**：本工程 gtest_discover_tests 按 PRE_TEST 注册，
// 每条用例各自一个进程（注册后的日志每条都以 `Note: Google Test filter = <单条用例名>` 开头），所以改前那次
// 聚焦跑三条用例全部跑到、汇总行为 `33% tests passed, 2 tests failed out of 3`（红的两条是双向/无向那两条）。
// 修完后它们必须是正常 Result::fail / Result::ok。
TEST_F(PluginTest, ProcessingScharrRejectsBothDirectionsRequested) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    const std::string input = createPatternRaster("e2e_scharr_both_input.tif");
    const std::string output = utf8PathString(getTestDir() / "e2e_scharr_both_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("edge");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["edge_method"] = std::string("scharr");
    // 显式写两个方向，不靠 spec 默认值漂移来制造非法组合（sobel_dx/sobel_dy 在 processing_plugin 的
    // paramSpecs() 里声明的默认本就是 1/1，按键名去找，别抄行号）。
    params["sobel_dx"] = 1;
    params["sobel_dy"] = 1;

    const auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success)
        << "dx=1 且 dy=1 时 dx+dy==2，OpenCV 断言必抛；插件必须显式 fail 而不是把异常抛给调用方";
    EXPECT_NE(result.message.find("scharr"), std::string::npos) << result.message;
}

// R-T15-3：54df42c:483 把入参压成 sobelDx>0?1:0 / sobelDy>0?1:0 ⇒ (0,0) 同样违反 dx+dy==1、同样抛。
// 少了这条，"只挡双 1、放过 (0,0)"的实现能全绿，而它一进 cv::Scharr 就抛未捕获异常
// ⇒ 红形态与上面同：gtest 记该用例 FAILED（有汇总行、不截断整跑），不是 result.success == false。
TEST_F(PluginTest, ProcessingScharrRejectsNoDirectionRequested) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    const std::string input = createPatternRaster("e2e_scharr_none_input.tif");
    const std::string output = utf8PathString(getTestDir() / "e2e_scharr_none_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("edge");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["edge_method"] = std::string("scharr");
    params["sobel_dx"] = 0;
    params["sobel_dy"] = 0;

    const auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success)
        << "dx=0 且 dy=0 时 dx+dy==0，OpenCV 断言必抛；插件必须显式 fail";
    EXPECT_NE(result.message.find("scharr"), std::string::npos) << result.message;
}

// 正例：合法的单方向必须仍然成功（守住"守卫没有把能用的组合一起挡掉"）。
TEST_F(PluginTest, ProcessingScharrAcceptsSingleDirection) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    const std::string input = createPatternRaster("e2e_scharr_ok_input.tif");
    const std::string output = utf8PathString(getTestDir() / "e2e_scharr_ok_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("edge");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["edge_method"] = std::string("scharr");
    params["sobel_dx"] = 1;
    params["sobel_dy"] = 0;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
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

// Task 15 / R-T15-4：默认值单源锁的判据一律来自 paramSpecs()，不把 5/1.5/4 抄进测试码
// （抄进测试的仍是双源，只是换了个地方）。
// 两条纪律：
//   1) paramSpecs() **按值返回** vector ⇒ 必须先落到具名局部再取地址，
//      直接对临时量取 & 会悬空（Tier 2 实测踩过一次，MSVC Debug 堆读出 0xDDDDDDDD）；
//   2) 实现侧统一用 std::to_string 写 metadata ⇒ double 的形态是 "1.500000"，
//      期望值必须用同一个 std::to_string 生成，不能手拼 "1.5"。
static const gis::framework::ParamSpec* findParamSpec(
    const std::vector<gis::framework::ParamSpec>& specs, const std::string& key) {
    for (const auto& spec : specs) {
        if (spec.key == key) return &spec;
    }
    return nullptr;
}

static std::string specDefaultAsMetadataString(const gis::framework::ParamSpec& spec) {
    if (const auto* asInt = std::get_if<int>(&spec.defaultValue)) return std::to_string(*asInt);
    if (const auto* asDouble = std::get_if<double>(&spec.defaultValue)) return std::to_string(*asDouble);
    return {};
}

// gabor：不传 kernel_size/sigma ⇒ 兜底必须与 paramSpecs() 声明的默认值同值。
// 改前实现兜底是 9/2.0（提交 54df42c:1159-1160），spec 是 5/1.5（同一 paramSpecs() 里
// kernel_size / sigma 那两条，按键名找，别抄行号）⇒ 本用例红。
TEST_F(PluginTest, ProcessingGaborFallbackDefaultsMatchParamSpecs) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);
    const auto specs = p->paramSpecs();

    const auto* kernelSpec = findParamSpec(specs, "kernel_size");
    const auto* sigmaSpec = findParamSpec(specs, "sigma");
    ASSERT_NE(kernelSpec, nullptr) << "doGaborFilter 读的就是这个键，spec 被删掉时面板不再渲染它";
    ASSERT_NE(sigmaSpec, nullptr) << "doGaborFilter 读的就是这个键，spec 被删掉时面板不再渲染它";
    EXPECT_EQ(kernelSpec->type, gis::framework::ParamType::Int)
        << "类型漂移成 Double/String 后 getParam<int> 会静默回落兜底值，取值断言照样绿";
    EXPECT_EQ(sigmaSpec->type, gis::framework::ParamType::Double)
        << "类型漂移成 Int/String 后 getParam<double> 会静默回落兜底值，取值断言照样绿";

    const std::string expectedKernel = specDefaultAsMetadataString(*kernelSpec);
    const std::string expectedSigma = specDefaultAsMetadataString(*sigmaSpec);
    ASSERT_FALSE(expectedKernel.empty()) << "kernel_size 的 spec 默认值必须是 int 变体";
    ASSERT_FALSE(expectedSigma.empty()) << "sigma 的 spec 默认值必须是 double 变体";

    const std::string input = createPatternRaster("e2e_gabor_default_lock_input.tif");
    const std::string output = utf8PathString(getTestDir() / "e2e_gabor_default_lock_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("gabor_filter");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    // 故意不传 kernel_size / sigma：只有走实现兜底这条路径才能暴露"兜底与 spec 分叉"。

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.metadata.at("kernel_size"), expectedKernel)
        << "gabor 的 kernel_size 兜底与 paramSpecs() 声明的默认值分叉";
    EXPECT_EQ(result.metadata.at("sigma"), expectedSigma)
        << "gabor 的 sigma 兜底与 paramSpecs() 声明的默认值分叉";
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

// Task 15 / R-T15-1：connectivity 在 spec 里声明了（54df42c:67，默认 8、范围 [4,8]），
// 但 54df42c:1381 把 8 写死进 cv::connectedComponents —— 它是死参数，传 4 与传 8 产出相同。
// 夹具几何（4×4，行主序 index = y*width + x，width=4）：
//   data[0]=(x0,y0)  data[1]=(x1,y0)  data[6]=(x2,y1)  data[7]=(x3,y1)
//   块 A={(0,0),(1,0)}、块 B={(2,1),(3,1)}，A 的 (1,0) 与 B 的 (2,1) 是 dx=1,dy=1 ⇒ **仅对角相接**
//   ⇒ 8 连通合成 1 个组件、4 连通仍为 2 个组件。
// 计划原文的 [0],[1],[5],[6] 换算是 (0,0)(0,1)(1,1)(1,2)，其中 (0,1)~(1,1) 上下相邻 = 4 连通，
// 4 连通也会得 1 个组件，断 run(4)=="2" 在正确实现下必红 ⇒ 已按裁定改成 [0],[1],[6],[7]。
// 只锚 component_count（= std::max(0, componentCount-1)，不含背景 0 号标签）：
// 该 action 先 toUint8 再 THRESH_BINARY|THRESH_OTSU，把 Otsu 的具体取值写进断言
// 等于把 OpenCV 的实现细节当契约。
TEST_F(PluginTest, ProcessingConnectedComponentsHonoursConnectivityParameter) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::vector<float> data(16, 0.0f);
    data[0] = 255.0f;
    data[1] = 255.0f;
    data[6] = 255.0f;
    data[7] = 255.0f;
    gis::tests::RasterFixtureOptions options;
    options.dtype = GDT_Float32;
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "cc_diag_input.tif", 4, 4, data, options);

    // conn 为空 = 不传该键，让实现走自己的兜底（下面漂移锁要的就是这条路径）。
    auto run = [&](std::optional<int> conn, const char* outputName) {
        const std::string output = utf8PathString(getTestDir() / outputName);
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("connected_components");
        params["input"] = input;
        params["output"] = output;
        params["band"] = 1;
        if (conn.has_value()) params["connectivity"] = *conn;
        const auto result = p->execute(params, progress_);
        EXPECT_TRUE(result.success) << "connectivity=" << (conn.has_value() ? std::to_string(*conn) : std::string("<unset>")) << ": " << result.message;
        const auto it = result.metadata.find("component_count");
        EXPECT_NE(it, result.metadata.end());
        return it == result.metadata.end() ? std::string() : it->second;
    };

    EXPECT_EQ(run(8, "cc_diag_8_output.tif"), "1")
        << "8 连通把仅对角相接的两块并成一个组件";
    EXPECT_EQ(run(4, "cc_diag_4_output.tif"), "2")
        << "connectivity=4 时对角不再相连；返回 1 说明参数根本没被读";

    // 兜底漂移锁（R-T15-4 的判据来源纪律同样适用于 connectivity）：
    // 不传该键时的产出必须等于"按 spec 声明的默认值跑"的结果。
    // 期望值从 paramSpecs() 取，不把 8 抄进测试——抄进测试的仍是双源，只是换了个地方。
    // 注意 paramSpecs() 是**按值返回**的 vector：必须先落到具名局部再取地址，
    // 直接 for (const auto& s : p->paramSpecs()) 再留 &s 会悬空（MSVC Debug 堆读出来是 0xDDDDDDDD）。
    const auto connSpecs = p->paramSpecs();
    const gis::framework::ParamSpec* connSpec = nullptr;
    for (const auto& spec : connSpecs) {
        if (spec.key == "connectivity") connSpec = &spec;
    }
    ASSERT_NE(connSpec, nullptr)
        << "实现读的就是这个键；spec 被删掉时 GUI 面板不再渲染它、CLI 也不再校验它";
    EXPECT_EQ(connSpec->type, gis::framework::ParamType::Int)
        << "类型漂移成 Double/String 后 getParam<int> 会静默回落兜底值，上面的取值断言照样绿";
    const auto* declaredDefault = std::get_if<int>(&connSpec->defaultValue);
    ASSERT_NE(declaredDefault, nullptr) << "connectivity 的默认值必须是 int 变体";
    // 21-FixA part2 I-5：期望值不许从"被测的那个值"现推（反演自证）。
    // 原来这两行是 expectedFromSpec = *declaredDefault == 4 ? "2" : "1"，即 期望 = f(被测值)：
    //   谁把 spec 默认值从 8 改成 4（4-连通 ⇒ 对角不相连，正是上面 run(4,…)=="2" 在防的行为面），
    //   三元式就自动跟着变成 "2"，下面两条 EXPECT_EQ 全绿 ⇒ 这条"兜底漂移锁"只锁住了
    //   兜底 == spec 这一层关系，锁不住 spec 自己的取值漂移，而后者才是用户看得见的后果。
    // 同口径的先例：R-T17-9(2) 与 tms 字面量纪律；同文件 :2830-2831 就是先把 spec 的
    //   *declaredMin/*declaredMax 钉成字面 0.0/1.0 再谈关系。
    ASSERT_EQ(*declaredDefault, 8)
        << "connectivity 的 spec 默认值漂了：下面的期望是按 8 连通（对角相连 ⇒ 1 个组件）"
           "字面写死的，默认值一改这两条期望必须一起改，不许改回\"从 declaredDefault 现推\"";
    EXPECT_EQ(run(8, "cc_diag_specdefault_explicit_output.tif"), "1")
        << "显式传 spec 声明的默认值 8 ⇒ 仅对角相接的两块必须并成一个组件";
    EXPECT_EQ(run(std::nullopt, "cc_diag_specdefault_unset_output.tif"), "1")
        << "不传该键时的代码兜底与 paramSpecs() 声明的默认值分叉了（兜底漂移）";
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

// ------------------------------------------------------------------ Task 13 / spec A7（H7）
// pansharpen 的输出地理网格必须整体取自 PAN 数据集。
// WHY 断 gt[1]/gt[5] 而不是只断尺寸：改前 `panWidth != msWidth` 分支就已经把 MS 的 Mat
// cv::resize 到 PAN 的行列数，所以输出的**像元网格**本来就是 20x20；缺陷只在写出那一步把
// 参考数据集传成 msDS ⇒ "20x20 个像元、每像元 30 m"，地面覆盖被凭空放大一倍且全程无报错。
// 只有地理变换能区分改前改后，尺寸与波段数断言是为了挡住"顺手改成按 MS 网格输出"这种回退。
TEST_F(PluginTest, PansharpenOutputUsesPanGeoTransform) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    // MS 10x10@30m 与 PAN 20x20@15m 的地理包络都是 X[0,300] Y[0,300] ⇒ 不该触发范围不符提示。
    gis::tests::RasterFixtureOptions msOptions;
    msOptions.crs = gis::tests::FixtureCrs::Epsg3857;
    msOptions.originX = 0.0;
    msOptions.originY = 300.0;
    msOptions.pixelSize = 30.0;
    const std::string ms = gis::tests::writeConstantBandsRaster(
        getTestDir(), "t13_grid_ms_30m.tif", 10, 10, {40.0f, 60.0f, 80.0f, 100.0f}, msOptions);
    ASSERT_FALSE(ms.empty());

    gis::tests::RasterFixtureOptions panOptions;
    panOptions.crs = gis::tests::FixtureCrs::Epsg3857;
    panOptions.originX = 0.0;
    panOptions.originY = 300.0;
    panOptions.pixelSize = 15.0;
    const std::vector<float> panData(20 * 20, 100.0f);
    const std::string pan = gis::tests::writeFloatRaster(
        getTestDir(), "t13_grid_pan_15m.tif", 20, 20, panData, panOptions);
    ASSERT_FALSE(pan.empty());

    const std::string output = utf8PathString(getTestDir() / "t13_grid_output.tif");
    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("pansharpen");
    params["input"] = ms;
    params["pan_file"] = pan;
    params["output"] = output;
    params["pan_method"] = std::string("brovey");

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    auto ds = gis::core::openRaster(output, true);
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->GetRasterXSize(), 20);
    EXPECT_EQ(ds->GetRasterYSize(), 20);
    // 波段数来自融合结果的个数（= MS 的波段数 4），不是参考数据集 PAN 的 1 —— 这是刻意口径，
    // 否则 pansharpen 就退化成单波段输出了。
    EXPECT_EQ(gis::tests::readBandCount(output), 4);

    const auto gt = gis::tests::readGeoTransform(output);
    EXPECT_DOUBLE_EQ(gt[1], 15.0);      // 本任务的核心：像元尺寸 = PAN 的 15，而不是 MS 的 30
    EXPECT_DOUBLE_EQ(gt[5], -15.0);
    EXPECT_DOUBLE_EQ(gt[0], 0.0);       // 原点同样取自 PAN（两幅原点相同，此条只把契约写全）
    EXPECT_DOUBLE_EQ(gt[3], 300.0);
    EXPECT_EQ(gis::tests::readCrsAuthorityCode(output), "EPSG:3857");

    // R-T13-1 的**反侧**：包络一致时不得出现提示，否则"无条件塞警告"的假实现也能全绿。
    EXPECT_EQ(result.metadata.count("pan_ms_extent_mismatch"), 0u)
        << "PAN 与 MS 的包络同为 X[0,300] Y[0,300]，不该报范围不符：" << result.message;
    EXPECT_EQ(result.message.find("extent mismatch"), std::string::npos) << result.message;

    // R-T13-2：参考数据集换成 PAN 这件事要在 metadata 里留下可核对的痕迹（可观察后果见
    // PansharpenSharedNoDataTagFollowsPanBand1，那条才钉"值到底取自哪一幅"）。
    ASSERT_TRUE(result.metadata.count("nodata_source") > 0)
        << "必须记录 NoData 取自哪个参考波段，实际 metadata 键数=" << result.metadata.size();
    EXPECT_NE(result.metadata.at("nodata_source").find("pan_band1"), std::string::npos)
        << result.metadata.at("nodata_source");
}

// R-T13-1：把"cv::resize 只看像元行列、完全不读地理参考"这条隐含假设变成承重假设之后，
// 两幅范围不一致时产出的是"看着正确、整体错位"的影像 —— 比改前更隐蔽，所以必须**不静默**。
// WHY 不硬失败：同传感器的 PAN/MS 正射后常有厘米到米级差异，硬失败会打断正常用法
// ⇒ 用例同时钉住"提示出现"与"success 仍为 true"两侧。
TEST_F(PluginTest, PansharpenExtentMismatchWarnsWithoutFailing) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    // MS 10x10@30m、origin (0,300) ⇒ 包络 X[0,300] Y[0,300]，较大边长 300 m。
    gis::tests::RasterFixtureOptions msOptions;
    msOptions.crs = gis::tests::FixtureCrs::Epsg3857;
    msOptions.originX = 0.0;
    msOptions.originY = 300.0;
    msOptions.pixelSize = 30.0;
    const std::string ms = gis::tests::writeConstantBandsRaster(
        getTestDir(), "t13_extent_ms_30m.tif", 10, 10, {40.0f, 60.0f}, msOptions);
    ASSERT_FALSE(ms.empty());

    const std::vector<float> panData(20 * 20, 100.0f);
    auto writePanAtTop = [&](const std::string& fileName, double topY) {
        gis::tests::RasterFixtureOptions options;
        options.crs = gis::tests::FixtureCrs::Epsg3857;
        options.originX = 0.0;
        options.originY = topY;
        options.pixelSize = 15.0;
        return gis::tests::writeFloatRaster(getTestDir(), fileName, 20, 20, panData, options);
    };

    auto runPansharpen = [&](const std::string& pan, const std::string& output) {
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("pansharpen");
        params["input"] = ms;
        params["pan_file"] = pan;
        params["output"] = output;
        params["pan_method"] = std::string("brovey");
        return p->execute(params, progress_);
    };

    // 场景 A：PAN 整体向南偏 1 m（上边 299、下边 -1），包络边长仍是 300 m
    // ⇒ 相对差异 = 1 / 300 = 3.333...e-3，远大于 1e-6 容差。
    const std::string shiftedPan = writePanAtTop("t13_extent_pan_15m_shift_1m.tif", 299.0);
    ASSERT_FALSE(shiftedPan.empty());
    const std::string shiftedOutput = utf8PathString(getTestDir() / "t13_extent_shift_1m_output.tif");
    const auto shifted = runPansharpen(shiftedPan, shiftedOutput);

    EXPECT_TRUE(shifted.success) << "范围不符只告警、不得硬失败：" << shifted.message;
    ASSERT_TRUE(shifted.metadata.count("pan_ms_extent_mismatch") > 0)
        << "PAN 相对 MS 偏了 1 m（相对差异 1/300）却没有提示：" << shifted.message;
    // 值断言：只写"键存在"、或把差异算成绝对米数的假实现，在这里都会红。
    EXPECT_NEAR(std::stod(shifted.metadata.at("pan_ms_extent_mismatch")), 1.0 / 300.0, 1e-12)
        << "metadata 值必须是相对差异（包络边差 / 较大边长）："
        << shifted.metadata.at("pan_ms_extent_mismatch");
    EXPECT_NE(shifted.message.find("extent mismatch"), std::string::npos)
        << "result.message 必须拼接同一条提示（GUI 结果区只看得到 message）：" << shifted.message;

    // 提示不得改变产物本身：输出网格仍然是 PAN 的 15 m。
    const auto shiftedGt = gis::tests::readGeoTransform(shiftedOutput);
    EXPECT_DOUBLE_EQ(shiftedGt[1], 15.0);
    EXPECT_DOUBLE_EQ(shiftedGt[5], -15.0);

    // 场景 B：PAN 只偏 0.0001 m（相对差异 3.333e-7 < 1e-6）⇒ 不许报警。
    // 没有这一条，"只要尺寸不同就报警"（或把容差写死成 0）的实现同样全绿，
    // 而那种实现会把米级以下一切正常用法都刷成警告，提示本身变成噪声。
    const std::string tinyPan = writePanAtTop("t13_extent_pan_15m_shift_0p1mm.tif", 299.9999);
    ASSERT_FALSE(tinyPan.empty());
    const std::string tinyOutput = utf8PathString(getTestDir() / "t13_extent_shift_0p1mm_output.tif");
    const auto withinTolerance = runPansharpen(tinyPan, tinyOutput);
    ASSERT_TRUE(withinTolerance.success) << withinTolerance.message;
    EXPECT_EQ(withinTolerance.metadata.count("pan_ms_extent_mismatch"), 0u)
        << "0.1 mm 级偏差不该报警，实际值="
        << (withinTolerance.metadata.count("pan_ms_extent_mismatch")
                ? withinTolerance.metadata.at("pan_ms_extent_mismatch")
                : std::string{"<absent>"});
    EXPECT_EQ(withinTolerance.message.find("extent mismatch"), std::string::npos)
        << withinTolerance.message;
}

// -------------------------------------------- Task 13 fix round 1 / F-1（Important，审查 I-1）
// `pansharpenExtentMismatchRatio` 在**拿不到地理变换**时返回 -1（processing_plugin.cpp:715），
// 调用方据此既不写 metadata 键也不追加提示。这条降级分支此前只在命令行手动探过两次
// （一幅完全拿不到地理变换、一幅参考侧有变换输入侧没有，两次都不报警），ctest 里一条都没有 ⇒
// 谁把它改成"拿不到变换式也报警"、或反过来"静默到底"，全量照样绿。
//
// WHY 本用例不调用公共夹具（这是 F-1 的核心，别"顺手统一"掉）：
// tests/raster_fixture.h 的 applyFixtureHeader 对每一幅夹具栅格**恒** SetGeoTransform
// （raster_fixture.h:113-115），本文件的 createConstantRaster / createMultiBandConstantRaster
// 同样恒 SetGeoTransform ⇒ 夹具层面**造不出**"没有地理变换"的输入，这正是审查说的"测试失明"。
// 批次 A 的裁定是夹具为公共地基（还挂着 D1/D2/D4/D6 待修项），为一个分支加 option 会牵动
// 后续每个任务的夹具语义，所以这里直接 gis::core::createRaster() + RasterIO 裸写、**不调**
// SetGeoTransform。先例是本仓测试**今天就存在**的形态，不是某个未开工任务的计划文本：
// tests/test_core.cpp 的 :119、:134、:144、:178、:196 都是 gis::core::createRaster(...) 夹具外裸写，
// 其中 :144（GdalBandToMat）与 :178（ComputeBandStats）与本处同为 createRaster + 逐波段 RasterIO
// 且全程不设变换式；同文件 :162（MatToGdalTiff）也是裸写，只是它随后自设了一份变换式。
static std::string createRasterWithoutGeoTransform(const std::string& name, int w, int h, int bands,
                                                  float value) {
    const std::string path = utf8PathString(getTestDir() / name);
    auto ds = gis::core::createRaster(path, w, h, bands, GDT_Float32);
    if (!ds) return {};   // createRaster 实际不返回 null（失败即抛，见 raster_fixture.h 的 D1/D2 说明），
                          // 这条判空只是把"万一"转成"夹具返回空串"，让调用方的 ASSERT_FALSE 报得清楚。
    const std::vector<float> data(static_cast<size_t>(w) * static_cast<size_t>(h), value);
    for (int b = 1; b <= bands; ++b) {
        auto* band = ds->GetRasterBand(b);
        if (!band) return {};
        band->RasterIO(GF_Write, 0, 0, w, h, const_cast<float*>(data.data()), w, h, GDT_Float32, 0, 0);
        band->FlushCache();
    }
    return path;   // 全程没有 SetGeoTransform / SetProjection ⇒ 打开时 GetGeoTransform 应返回 CE_Failure
}

// 数据集**确实**取不到地理变换时返回 true。
// WHY 判据是返回值而不是数组内容：GDAL 在失败时同样会把默认单位变换填进数组，
// 只看 gt[1]==1 分不清"没有变换"和"变换恰好是单位阵"，那样这条前置检查就白写了。
static bool rasterHasNoGeoTransform(const std::string& path) {
    auto ds = gis::core::openRaster(path, true);
    if (!ds) return false;
    double gt[6];
    return ds->GetGeoTransform(gt) != CE_None;
}

TEST_F(PluginTest, PansharpenNoGeoTransformSkipsExtentCheck) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    std::error_code ec;
    // MS 10x10 三波段 + PAN 20x20 单波段：行列数不同 ⇒ 一定会走 cv::resize 那条
    // "只看行列、不看地理参考"的路径（正是 R-T13-1 要防的那条链路），只是两幅都没有变换可比。
    const std::string ms = createRasterWithoutGeoTransform("t13f1_nogt_ms.tif", 10, 10, 3, 60.0f);
    ASSERT_FALSE(ms.empty());
    const std::string pan = createRasterWithoutGeoTransform("t13f1_nogt_pan.tif", 20, 20, 1, 100.0f);
    ASSERT_FALSE(pan.empty());
    // PAM 边车按文件名匹配：残留的 .aux.xml 会把别处写过的变换式带进来，把"没有变换"变成"有变换"，
    // 让本用例假绿（同 PansharpenSharedNoDataTagFollowsPanBand1 与本文件 proximity/nodata 的既有口径）。
    fs::remove(ms + ".aux.xml", ec);
    fs::remove(pan + ".aux.xml", ec);

    // 前置条件自证（承重）：两幅输入**确实**取不到地理变换。
    // 没有这两条，将来任何人给上面的裸写夹具补一句 SetGeoTransform，本用例会**静默**变成
    // "差异为 0 时不报警"的另一种护栏（那个方向另有 PansharpenOutputUsesPanGeoTransform 在锁），
    // 而它要钉的 -1 分支又回到零覆盖状态。
    ASSERT_TRUE(rasterHasNoGeoTransform(ms)) << ms << " 应当没有地理变换，否则这条用例锁错了分支";
    ASSERT_TRUE(rasterHasNoGeoTransform(pan)) << pan << " 应当没有地理变换，否则这条用例锁错了分支";

    const std::string output = utf8PathString(getTestDir() / "t13f1_nogt_output.tif");
    // 本文件既有口径 M-T5-a（BandMathWithUnknownVariableFailsExplicitly、
    // ReclassifyFailsWhenOneBandGeneratesNoIntervalRules 两处同形）：**在断言产物存在之前**先清掉
    // 历次运行留下的陈旧输出。理由——本用例后面的 `EXPECT_TRUE(fs::exists(output))` 只看"有没有文件"，
    // 若上一次运行（甚至是改动前那份代码）留下过同名产物，那么"这次根本没写出来"也会被旧文件
    // 骗成假通过；清理必须放在 execute() 之前才有效，放在断言行旁边等于已经太晚。
    std::error_code removeEc;
    fs::remove(output, removeEc);
    EXPECT_FALSE(fs::exists(output)) << "stale output not removed: " << output;

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("pansharpen");
    params["input"] = ms;
    params["pan_file"] = pan;
    params["output"] = output;
    params["pan_method"] = std::string("brovey");   // 算法与地理无关，挑一个能跑通的分支即可

    const auto result = p->execute(params, progress_);

    // 断言之一：无从比较**不是失败**。拿不到地理参考就拒绝执行，会把今天照常能跑的
    // 无参考影像（BMP/截图/未地理编码的中间产物）整条链路断掉，那不是 R-T13-1 要的东西。
    EXPECT_TRUE(result.success) << "拿不到地理变换不得当成失败：" << result.message;
    // 断言之二：metadata 里**不得出现**这个键。"没得比"不等于"比出了问题"，
    // 写成 -1 会让下游把"无信息"读成"信息"（下游按 >容差 判读时 -1 恰好又被放过，等于埋雷）。
    EXPECT_EQ(result.metadata.count("pan_ms_extent_mismatch"), 0u)
        << "两幅都没有地理变换 ⇒ 范围校验应当整个跳过，实际值="
        << (result.metadata.count("pan_ms_extent_mismatch")
                ? result.metadata.at("pan_ms_extent_mismatch")
                : std::string{"<absent>"});
    // 断言之三：用户可见的 message 里也**不许**出现提示。只查 metadata 会漏掉"只往 message 里写"
    // 的实现，而 GUI 结果区看的恰恰是 message（三条缺一，实现就能各蒙对一半）。
    EXPECT_EQ(result.message.find("extent mismatch"), std::string::npos) << result.message;

    // 产物照常生成（"不报警"不等于"不干活"）。
    EXPECT_TRUE(fs::exists(output)) << result.message;

    // ---------------------------------------------------- Task 13 fix round 2 / F-2r（Important I-rr-1）
    // 上面那一段只钉住了守卫 `!pansharpenEnvelope(msDs, ms) || !pansharpenEnvelope(panDs, pan)`
    // （processing_plugin.cpp 的 pansharpenExtentMismatchRatio 开头）的**一个操作数**：两侧都拿不到变换式。
    // "只有一侧拿得到"同样是这条 -1 分支的入口，round 1 之后仍然零 ctest 护栏 —— 实测把短路或写成 `&&`
    // （= 只有两侧都缺才返回 -1）时，未补下面两段的全量零红（改前变异轮实测；K-21：结论入库、当时的条数不入库，要复算就跑当前全量），
    // 而命令行混合探针会给每一次"PAN 没有地理参考"的正常使用刷一句假的
    // `pan_ms_extent_mismatch: 1 (relative difference 1 over tolerance 1e-06)`：失败方拿不到四角、
    // 结构体停在默认的 {0,0,0,0}，与另一幅 300 m 的包络一比 ⇒ 边差 300 / 较大边长 300 = 1。
    // ⇒ 危害不是"少个提示"而是**凭空报出一个范围不一致**，把用户推向完全错误的方向；
    //   契约原文（发布说明已采用的口径）："两侧都没有地理变换、或只有一侧有变换时都不报警"。
    // 下面两段把混合形态的两个方向各钉一次：判据与三件断言沿用上面同一套（谓词 rasterHasNoGeoTransform、
    // 按 GetGeoTransform 的返回值判），不另造第二个判据、也不新建 TEST_F（用例数不变）。

    // 混合形态 A：MS 有地理变换（走夹具，包络 X[0,300] Y[0,300]）、PAN 没有（裸写）。
    gis::tests::RasterFixtureOptions mixAMsOptions;
    mixAMsOptions.crs = gis::tests::FixtureCrs::Epsg3857;
    mixAMsOptions.originX = 0.0;
    mixAMsOptions.originY = 300.0;
    mixAMsOptions.pixelSize = 30.0;
    const std::string mixAMs = gis::tests::writeConstantBandsRaster(
        getTestDir(), "t13f2r_mixA_ms_withgt.tif", 10, 10, {40.0f, 60.0f, 80.0f}, mixAMsOptions);
    ASSERT_FALSE(mixAMs.empty());
    const std::string mixAPan = createRasterWithoutGeoTransform("t13f2r_mixA_pan_nogt.tif", 20, 20, 1, 100.0f);
    ASSERT_FALSE(mixAPan.empty());
    fs::remove(mixAMs + ".aux.xml", ec);   // 同上：PAM 边车会把别处写过的变换式带进来，让"没有"变成"有"
    fs::remove(mixAPan + ".aux.xml", ec);

    // 前置自证（承重，与上面同一条谓词的正反两面）：**一个有、一个没有**。
    // 少了这两条，夹具一旦被人补/删一句 SetGeoTransform，本段会静默退化成"两侧都缺"或"两侧都有"，
    // 而那两个方向分别已有上面的段落和 PansharpenExtentMismatchWarnsWithoutFailing 在锁 ⇒ 混合入口又回到零覆盖。
    ASSERT_FALSE(rasterHasNoGeoTransform(mixAMs))
        << mixAMs << " 应当**有**地理变换，否则混合形态 A 退化成了两侧都缺（那是上面那一段的输入）";
    ASSERT_TRUE(rasterHasNoGeoTransform(mixAPan))
        << mixAPan << " 应当没有地理变换，否则混合形态 A 退化成了两侧都有（那是范围比对的另一条分支）";

    const std::string mixAOutput = utf8PathString(getTestDir() / "t13f2r_mixA_output.tif");
    std::map<std::string, gis::framework::ParamValue> mixAParams;
    mixAParams["action"] = std::string("pansharpen");
    mixAParams["input"] = mixAMs;
    mixAParams["pan_file"] = mixAPan;
    mixAParams["output"] = mixAOutput;
    mixAParams["pan_method"] = std::string("brovey");   // 算法与地理无关，挑一个能跑通的分支即可

    const auto mixA = p->execute(mixAParams, progress_);
    EXPECT_TRUE(mixA.success) << "一侧拿不到地理变换不得当成失败：" << mixA.message;
    EXPECT_EQ(mixA.metadata.count("pan_ms_extent_mismatch"), 0u)
        << "MS 有 / PAN 没有 ⇒ 没有可比对象，范围校验应当整个跳过，实际值="
        << (mixA.metadata.count("pan_ms_extent_mismatch")
                ? mixA.metadata.at("pan_ms_extent_mismatch")
                : std::string{"<absent>"});
    EXPECT_EQ(mixA.message.find("extent mismatch"), std::string::npos) << mixA.message;

    // 混合形态 B：镜像方向 —— PAN 有地理变换（夹具 20x20@15，包络同为 X[0,300] Y[0,300]）、MS 没有（裸写）。
    // WHY 要单独一段而不是只留 A：`||` 的两个操作数各自是独立入口，写成
    // `if (!pansharpenEnvelope(panDs, pan)) return -1.0;`（只看 PAN 侧）这类"半边"实现能骗过 A 但骗不过 B。
    const std::string mixBMsWithoutGt = createRasterWithoutGeoTransform("t13f2r_mixB_ms_nogt.tif", 10, 10, 3, 60.0f);
    ASSERT_FALSE(mixBMsWithoutGt.empty());
    const std::vector<float> mixBPanData(20 * 20, 100.0f);
    gis::tests::RasterFixtureOptions mixBPanOptions;
    mixBPanOptions.crs = gis::tests::FixtureCrs::Epsg3857;
    mixBPanOptions.originX = 0.0;
    mixBPanOptions.originY = 300.0;
    mixBPanOptions.pixelSize = 15.0;
    const std::string mixBPan = gis::tests::writeFloatRaster(
        getTestDir(), "t13f2r_mixB_pan_withgt.tif", 20, 20, mixBPanData, mixBPanOptions);
    ASSERT_FALSE(mixBPan.empty());
    fs::remove(mixBMsWithoutGt + ".aux.xml", ec);
    fs::remove(mixBPan + ".aux.xml", ec);

    ASSERT_TRUE(rasterHasNoGeoTransform(mixBMsWithoutGt))
        << mixBMsWithoutGt << " 应当没有地理变换，否则混合形态 B 退化成了两侧都有";
    ASSERT_FALSE(rasterHasNoGeoTransform(mixBPan))
        << mixBPan << " 应当**有**地理变换，否则混合形态 B 退化成了两侧都缺";

    const std::string mixBOutput = utf8PathString(getTestDir() / "t13f2r_mixB_output.tif");
    std::map<std::string, gis::framework::ParamValue> mixBParams;
    mixBParams["action"] = std::string("pansharpen");
    mixBParams["input"] = mixBMsWithoutGt;
    mixBParams["pan_file"] = mixBPan;
    mixBParams["output"] = mixBOutput;
    mixBParams["pan_method"] = std::string("brovey");

    const auto mixB = p->execute(mixBParams, progress_);
    EXPECT_TRUE(mixB.success) << "一侧拿不到地理变换不得当成失败：" << mixB.message;
    EXPECT_EQ(mixB.metadata.count("pan_ms_extent_mismatch"), 0u)
        << "PAN 有 / MS 没有 ⇒ 没有可比对象，范围校验应当整个跳过，实际值="
        << (mixB.metadata.count("pan_ms_extent_mismatch")
                ? mixB.metadata.at("pan_ms_extent_mismatch")
                : std::string{"<absent>"});
    EXPECT_EQ(mixB.message.find("extent mismatch"), std::string::npos) << mixB.message;

    // ------------------------------------------------------------------------------------ 现状锁（N51）
    // 本用例锁的是**现状**："缺地理参考 ⇒ 得不到范围校验的保护"，pansharpen 照跑、照出图、
    // 一句提示都不给。同一族的还有顾虑 C-1：一侧有变换、一侧没有时输出会退化成 GDAL 默认参考
    // （MS 那份地理参考直接丢掉，也无人告知）——那部分本批次**不改**（挂 N51）。
    // 层次要分清，别把这条分支读成"整支都有人锁了"：round 2 新加的混合形态两段锁的是
    // "这种输入**不报警**"，而 C-1 那条**信息损失**（MS 的参考被丢掉且无人告知）仍然没有任何护栏。
    // 批次 B 若按 N51 改成"缺地理参考就显式拒绝"，**这条用例要改正向而不是删掉**：
    // 断 result.success == false、断 message 点名缺变换的那一幅（不是含糊的 "extent mismatch"），
    // 上面三条断言逐条换成对应的正向断言即可，**不得整段删除**——
    // 处置模式与 Task 20 的 RejectedFeatureWrite、Task 21 的 N45 现状锁一致。
}

// -------------------------------------------- Task 13 fix round 1 / F-2（Minor M-1）：容差**数值**本身
// 审查实测：把 kPanMsExtentToleranceRatio 从 1e-6 改成 1e-4，8 条用例全绿 ⇒ 只有"相对口径"和
// "低于容差不报警"有牙（Tier 1 的 M3/M4），具体数量级没人管。
// 既有两个样本离得太远：3.33e-3 必报（PansharpenExtentMismatchWarnsWithoutFailing 场景 A）、
// 3.33e-7 必不报（同用例场景 B），中间隔了四个数量级，容差被改成 1e-4/1e-5 都照样绿。
// 本用例补一对**夹住 1e-6** 的样本，推导见下面的算式（照口径实测，不是猜的）：
//
//   MS 10x10 @ 30 m、origin (0,300) ⇒ 包络 X[0,300] Y[0,300]，边长 300；
//   PAN 20x20 @ 15 m ⇒ 20*15 = 300 ⇒ 包络边长同样是 300 ⇒ largerSide = 300；
//   PAN 只沿 X 平移 dx（Y 与 MS 完全对齐）⇒ 四条对应边的差里只有 minX/maxX 两条非零、
//   且都等于 dx ⇒ edgeDelta = dx ⇒ **相对差异 = dx / 300**。
//     · 取 dx = 300 * 5.0e-6 = 0.0015 m（1.5 mm） ⇒ 相对差异 ≈ 5.0e-6，**高于** 1e-6 ⇒ 必须报警；
//     · 取 dx = 300 * 5.0e-7 = 0.00015 m（0.15 mm） ⇒ 相对差异 ≈ 5.0e-7，**低于** 1e-6 ⇒ 必须不报警。
//   两段合起来把容差的可能取值压进 [5e-7, 5e-6)：**新添的牙在上界**——容差被搬大（1e-4/1e-5）
//   只有第一段会红，而这是审查 M-rev-a 实测过"全绿"的那个方向；第二段（下界）与既有的
//   PansharpenExtentMismatchWarnsWithoutFailing 场景 B（3.33e-7）同向、属于刻意保留的双保险，
//   实测见报告：容差改成 1e-7 时**两条一起红**，所以缺了下段也不至于失明。
TEST_F(PluginTest, PansharpenExtentMismatchToleranceAnchorsOneMicroratio) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions msOptions;
    msOptions.crs = gis::tests::FixtureCrs::Epsg3857;
    msOptions.originX = 0.0;
    msOptions.originY = 300.0;
    msOptions.pixelSize = 30.0;
    const std::string ms = gis::tests::writeConstantBandsRaster(
        getTestDir(), "t13f2_ms_30m.tif", 10, 10, {40.0f, 60.0f}, msOptions);
    ASSERT_FALSE(ms.empty());

    const std::vector<float> panData(20 * 20, 100.0f);
    auto writePanShiftedInX = [&](const std::string& fileName, double shiftX) {
        gis::tests::RasterFixtureOptions options;
        options.crs = gis::tests::FixtureCrs::Epsg3857;
        options.originX = shiftX;      // 只动 X ⇒ 边差全部来自 X，Y 两侧精确相等、不引入噪声
        options.originY = 300.0;
        options.pixelSize = 15.0;
        return gis::tests::writeFloatRaster(getTestDir(), fileName, 20, 20, panData, options);
    };
    auto runPansharpen = [&](const std::string& pan, const std::string& output) {
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("pansharpen");
        params["input"] = ms;
        params["pan_file"] = pan;
        params["output"] = output;
        params["pan_method"] = std::string("brovey");
        return p->execute(params, progress_);
    };

    // 段一：相对差异 ≈ 5.0e-6（容差的 5 倍）⇒ 键必须出现，且**仍然不失败**。
    const std::string abovePan = writePanShiftedInX("t13f2_pan_shift_1p5mm.tif", 0.0015);
    ASSERT_FALSE(abovePan.empty());
    const std::string aboveOutput = utf8PathString(getTestDir() / "t13f2_above_output.tif");
    const auto above = runPansharpen(abovePan, aboveOutput);
    EXPECT_TRUE(above.success) << "超容差只告警、不得失败：" << above.message;
    ASSERT_TRUE(above.metadata.count("pan_ms_extent_mismatch") > 0)
        << "1.5 mm / 300 m（相对差异 5e-6）已在 1e-6 容差之外，却没有任何提示：" << above.message;
    // 值断言把"报警阈值"与"报出来的数"一起钉住：容差被搬大时这段先红（键都没有），
    // 而"值写成绝对米数 0.0015"之类的实现躲得过"键存在"、躲不过这里（差 300 倍，
    // 而窗口只有 5e-9）。
    // 容差取 5e-9（相对 0.1%）而不是 Tier 1 那条用的 1e-12：变换式要过一轮 GeoTIFF 的文本往返，
    // 0.0015 与 300.0015 本身又是 double 近似（实算 edgeDelta=0.0015000000000213731，
    // 见报告 §2.1），1e-12 会把真实值误判成错。
    EXPECT_NEAR(std::stod(above.metadata.at("pan_ms_extent_mismatch")), 5.0e-6, 5.0e-9)
        << "值必须是相对差异（边差 dx / 较大边长 300）："
        << above.metadata.at("pan_ms_extent_mismatch");
    EXPECT_NE(above.message.find("extent mismatch"), std::string::npos) << above.message;

    // 段二：相对差异 ≈ 5.0e-7（容差的一半）⇒ 键与提示都不许出现。
    // 这一段的下界方向与既有场景 B（3.33e-7）重叠（容差改成 1e-7 时两条一起红，实测见报告），
    // 保留它是为了让"夹住 1e-6 的一对"落在同一条用例里、读的人不必去拼两条；
    // 它同时把亚毫米级抖动（同传感器正射后的常见量级）不许报警这件事钉在**紧邻容差内侧**的位置，
    // 5e-7 比既有场景 B 的 3.33e-7 更贴近 1e-6，容差被往小搬动时这里先露头。
    const std::string belowPan = writePanShiftedInX("t13f2_pan_shift_0p15mm.tif", 0.00015);
    ASSERT_FALSE(belowPan.empty());
    const std::string belowOutput = utf8PathString(getTestDir() / "t13f2_below_output.tif");
    const auto below = runPansharpen(belowPan, belowOutput);
    ASSERT_TRUE(below.success) << below.message;
    EXPECT_EQ(below.metadata.count("pan_ms_extent_mismatch"), 0u)
        << "0.15 mm / 300 m（相对差异 5e-7）在 1e-6 容差内侧，不该报警，实际值="
        << (below.metadata.count("pan_ms_extent_mismatch")
                ? below.metadata.at("pan_ms_extent_mismatch")
                : std::string{"<absent>"});
    EXPECT_EQ(below.message.find("extent mismatch"), std::string::npos) << below.message;
}

// R-T13-2 及其更正：换参考数据集后 NoData 的**可观察**后果要如实记录，而不是留成巧合，
// 更不能当成新缺陷去修。
// 核心格式事实（src/core/src/gdal_wrapper.cpp:63 的 drv->Create(..., nullptr) 不带创建选项）：
// 产出的多波段 GeoTIFF 是单 IFD、整个数据集只有**一个共享的 GDAL_NODATA 标签** ⇒
// "逐波段 NoData 表达不出来"改前改后都成立，matsToGdalTiff 的逐波段回落（opencv_wrapper.cpp:100）
// 在输出侧几乎没有可观察差异。真正被本任务改掉的只有一件事：共享标签的**值取自哪一幅**。
// 另须记住：pansharpen 本就不做 NoData 掩膜（三个方法分支都不判），NoData 传递从来只是副产物。
TEST_F(PluginTest, PansharpenSharedNoDataTagFollowsPanBand1) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    // PAM 边车按文件名匹配：残留的 .aux.xml 会把别处写过的 NoData 声明带进来，造成假绿/假红
    // （同 Task 7/9 的既有口径，见本文件 proximity/nodata 那批用例）。
    std::error_code ec;
    const auto removeSidecars = [&](const std::string& path) {
        fs::remove(path + ".aux.xml", ec);
    };

    // MS 8x8@0.001 与 PAN 16x16@0.0005 的包络同为 X[116,116.008] Y[39.992,40] ⇒ 不触发范围提示。
    gis::tests::RasterFixtureOptions msOptions;
    msOptions.originX = 116.0;
    msOptions.originY = 40.0;
    msOptions.pixelSize = 0.001;
    msOptions.hasNoData = true;
    msOptions.noData = -9999.0;
    const std::string ms = gis::tests::writeConstantBandsRaster(
        getTestDir(), "t13_nodata_ms.tif", 8, 8, {40.0f, 60.0f}, msOptions);
    ASSERT_FALSE(ms.empty());
    removeSidecars(ms);
    // 健全性：MS 自己必须真的声明了 NoData，否则"输出不再继承"这条断言就是空检查。
    double msDeclared = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(ms, 1, msDeclared)) << "夹具没有给 MS 写出 NoData";
    ASSERT_DOUBLE_EQ(msDeclared, -9999.0);

    const std::vector<float> panData(16 * 16, 100.0f);
    auto writePan = [&](const std::string& fileName, bool withNoData) {
        gis::tests::RasterFixtureOptions options;
        options.originX = 116.0;
        options.originY = 40.0;
        options.pixelSize = 0.0005;
        options.hasNoData = withNoData;
        options.noData = -555.0;
        return gis::tests::writeFloatRaster(getTestDir(), fileName, 16, 16, panData, options);
    };

    auto runPansharpen = [&](const std::string& pan, const std::string& output) {
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("pansharpen");
        params["input"] = ms;
        params["pan_file"] = pan;
        params["output"] = output;
        params["pan_method"] = std::string("brovey");
        return p->execute(params, progress_);
    };

    // 场景 A：PAN 没有声明 NoData ⇒ 改前输出会继承 MS band1 的 -9999，
    // 改后**整幅不声明任何 NoData**（行为改变，按 R-T13-2 如实钉住，不做掩膜也不造 0 值）。
    const std::string panNoTag = writePan("t13_nodata_pan_untagged.tif", false);
    ASSERT_FALSE(panNoTag.empty());
    removeSidecars(panNoTag);
    const std::string outNoTag = utf8PathString(getTestDir() / "t13_nodata_pan_untagged_output.tif");
    removeSidecars(outNoTag);
    const auto untagged = runPansharpen(panNoTag, outNoTag);
    ASSERT_TRUE(untagged.success) << untagged.message;
    // 可观察后果排在 metadata 之前：这样改前的 RED 日志同时暴露"输出仍继承 MS 的 -9999"这件事，
    // 而不是只报"少了个键"（metadata 用的是 ASSERT，会把后面的断言整段截掉）。
    for (int band = 1; band <= 2; ++band) {
        double outNoData = 0.0;
        EXPECT_FALSE(gis::tests::readNoDataValue(outNoTag, band, outNoData))
            << "band " << band << " 不该再继承 MS 的 -9999，实际读到 " << outNoData;
    }
    ASSERT_TRUE(untagged.metadata.count("nodata_source") > 0);
    const std::string untaggedSource = untagged.metadata.at("nodata_source");
    EXPECT_NE(untaggedSource.find("pan_band1"), std::string::npos) << untaggedSource;
    EXPECT_NE(untaggedSource.find("declared=none"), std::string::npos) << untaggedSource;
    EXPECT_NE(untaggedSource.find("shared"), std::string::npos)
        << "必须写明输出是单一共享 NoData 标签：" << untaggedSource;

    // 场景 B：PAN band1 声明 -555 ⇒ 输出的每个波段都报 -555（共享标签，只有一个值）。
    // 改前这里会是 MS 的 -9999，所以这条对"参考数据集"的替换有直接鉴别力。
    const std::string panTagged = writePan("t13_nodata_pan_tagged.tif", true);
    ASSERT_FALSE(panTagged.empty());
    removeSidecars(panTagged);
    const std::string outTagged = utf8PathString(getTestDir() / "t13_nodata_pan_tagged_output.tif");
    removeSidecars(outTagged);
    const auto tagged = runPansharpen(panTagged, outTagged);
    ASSERT_TRUE(tagged.success) << tagged.message;
    for (int band = 1; band <= 2; ++band) {
        double outNoData = 0.0;
        ASSERT_TRUE(gis::tests::readNoDataValue(outTagged, band, outNoData))
            << "band " << band << " 应共享 PAN band1 声明的 NoData";
        EXPECT_DOUBLE_EQ(outNoData, -555.0);
    }
    ASSERT_TRUE(tagged.metadata.count("nodata_source") > 0);
    EXPECT_NE(tagged.metadata.at("nodata_source").find("declared=-555"), std::string::npos)
        << tagged.metadata.at("nodata_source");
}

// Tier 2 / spec A7 的改名部分。`simple_mean` 这个名字是骗人的：它做的是"PAN 与多光谱逐像元求平均"，
// 既不注入全色细节也不保持各波段的相对关系，把它列在"锐化方法"里会误导用户 ⇒ 改名 `mean_blend`。
// R-T13-3：旧值**不进枚举**（不得为了让 GUI 用例继续通过而留在枚举里），只能经由一条显式拒绝
// 路径拿到可操作错误。WHY 单靠下面的 Unknown 分支不够：那句是
// "Unknown pansharpen method: simple_mean"，里面没有新名，用户看完不知道该改成什么。
TEST_F(PluginTest, PansharpenMeanBlendReplacesSimpleMean) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    // 先钉"真值"：枚举本身。GUI 标签表与文档都以它为基准
    // （见 test_coverage_meta.cpp 的 GuiContract.PanMethodLabelTableMatchesPluginEnumValues）。
    const auto specs = p->paramSpecs();
    const gis::framework::ParamSpec* methodSpec = nullptr;
    for (const auto& spec : specs) {
        if (spec.key == "pan_method") methodSpec = &spec;
    }
    ASSERT_NE(methodSpec, nullptr) << "processing 的 paramSpecs 里必须仍有 pan_method";
    const auto& values = methodSpec->enumValues;
    EXPECT_EQ(std::find(values.begin(), values.end(), std::string("simple_mean")), values.end())
        << "旧值不得为了迁就既有用例而留在枚举里";
    EXPECT_EQ(values.size(), 3u) << "pan_method 的取值集合就是 {brovey, ihs, mean_blend}";
    for (const char* expected : {"brovey", "ihs", "mean_blend"}) {
        EXPECT_NE(std::find(values.begin(), values.end(), std::string(expected)), values.end())
            << "枚举里缺 " << expected;
    }
    // 默认值仍是 brovey：改名不许动默认值，否则 GUI/CLI 的缺省提交会悄悄换算法。
    const auto* methodDefault = std::get_if<std::string>(&methodSpec->defaultValue);
    ASSERT_NE(methodDefault, nullptr);
    EXPECT_EQ(*methodDefault, "brovey");

    // mean_blend 的实际语义 = PAN-MS 均值混合（非锐化）。常量夹具让期望值可手算，
    // 这条数值断言挡的是"分支名跟着改了、算式却被改错"。
    const std::string ms = createMultiBandConstantRaster("t13_blend_ms.tif", 8, 8, {40.0f, 60.0f});
    const std::string pan = createConstantRaster("t13_blend_pan.tif", 8, 8, 100.0f);
    const std::string output = utf8PathString(getTestDir() / "t13_blend_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("pansharpen");
    params["input"] = ms;
    params["pan_file"] = pan;
    params["output"] = output;
    params["pan_method"] = std::string("mean_blend");

    const auto blended = p->execute(params, progress_);
    ASSERT_TRUE(blended.success) << blended.message;
    EXPECT_FLOAT_EQ(gis::tests::readPixel(output, 1, 3, 4), 70.0f);   // (40 + 100) / 2
    EXPECT_FLOAT_EQ(gis::tests::readPixel(output, 2, 3, 4), 80.0f);   // (60 + 100) / 2
    EXPECT_EQ(blended.metadata.count("pan_ms_extent_mismatch"), 0u) << blended.message;

    // 旧值必须被显式拒绝，并且错误里同时点名旧值与新值。
    params["pan_method"] = std::string("simple_mean");
    const auto legacy = p->execute(params, progress_);
    EXPECT_FALSE(legacy.success) << "旧枚举必须被显式拒绝并提示改名";
    EXPECT_NE(legacy.message.find("mean_blend"), std::string::npos) << legacy.message;
    EXPECT_NE(legacy.message.find("simple_mean"), std::string::npos)
        << "错误里要点名被拒绝的旧值，否则用户不知道该改哪一个：" << legacy.message;

    // 真正的未知值仍走 Unknown 分支（不许把拒绝写成"兜底接受"，也不许把改名文案糊到所有错误上）。
    params["pan_method"] = std::string("esri_pansharp");
    const auto unknown = p->execute(params, progress_);
    EXPECT_FALSE(unknown.success) << unknown.message;
    EXPECT_NE(unknown.message.find("Unknown pansharpen method"), std::string::npos) << unknown.message;
    EXPECT_EQ(unknown.message.find("renamed"), std::string::npos) << unknown.message;
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

// ── N133：raster_inspect 把常数栅格判成"没有值域" ────────────────────────────────
// 改前现读（Release CLI，同一张 16 个 1.0 的合法 tif = build/tmp/n132/o3.tif）：
//   histogram ⇒ Error: Band has no value range (min >= max)      ← 合法产物被判成坏数据
//   info      ⇒ Min: 1 / Max: 1 / Mean: 1 / StdDev: 0（这条改前就是对的）
//   另一张全 0 且 0=NoData 的 tif（N132 静默烧 0 的产物形状 o.tif）：
//   histogram ⇒ Error: Band has no value range (min >= max)      ← 与上面**同一句**案文
//   info      ⇒ 统计块整块不印、也不说为什么
// ⇒ 两种情况（"算不出统计"和"值域宽度为 0"）共用同一个判据 min >= max，案文也不可区分。
// 夹具算式（期望值先于产物写死）：w×h 全填 value，再把行主序末尾 tailCells 个像元填成
//   0.0 并声明 NoData=0 ⇒ 有效像元数 = w*h - tailCells：
//   (4,4,1.0,0)  ⇒ 16 个有效像元，min == max == 1
//   (4,4,1.0,4)  ⇒ 12 个有效像元，min == max == 1   ← 这一条专门打死"桶数按 w*h 交差"的假修法
//   (4,4,0.0,16) ⇒ 0 个有效像元 ⇒ 统计根本算不出来，这才是唯一该报错的情形
static std::string createConstantRasterWithNoDataTail(const std::string& name, int w, int h,
                                                      float value, int tailCells) {
    const std::string path = (getTestDir() / name).string();
    std::error_code ignored;
    fs::remove(path, ignored);
    fs::remove(path + ".aux.xml", ignored);   // PAM 里缓存的统计/直方图会让"改前红"变成"读到旧值"

    auto ds = gis::core::createRaster(path, w, h, 1, GDT_Float32);
    double adfGT[6] = {116.0, 1.0, 0.0, 40.0, 0.0, -1.0};
    ds->SetGeoTransform(adfGT);

    std::vector<float> data(static_cast<size_t>(w) * static_cast<size_t>(h), value);
    const int totalCells = w * h;
    for (int i = totalCells - tailCells; i < totalCells; ++i) {
        data[static_cast<size_t>(i)] = 0.0f;
    }
    auto* band = ds->GetRasterBand(1);
    band->RasterIO(GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0, 0);
    band->SetNoDataValue(0.0);
    band->FlushCache();
    return path;
}

// 常数栅格必须出值：值域宽度为 0 时给**一个**桶，桶心就是那个常数，数量是有效像元数。
TEST_F(PluginTest, RasterInspectHistogramAcceptsConstantRaster) {
    auto* p = mgr_.find("raster_inspect");
    if (!p) {
        FAIL() << "plugin not loaded: raster_inspect";
    }

    // 臂 1：整幅 1.0、无 NoData ⇒ 16 个有效像元
    const std::string full = createConstantRasterWithNoDataTail(
        "e2e_hist_const_full_input.tif", 4, 4, 1.0f, 0);
    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("histogram");
    params["input"] = full;
    params["bins"] = 8;   // 用户要 8 个桶，常数值域下也只能给 1 个（不能凭空造 8 个区间）

    const auto fullResult = p->execute(params, progress_);
    ASSERT_TRUE(fullResult.success)
        << "N133：常数栅格被 min >= max 判成没有值域，message=" << fullResult.message;
    EXPECT_EQ(fullResult.metadata.at("min"), "1.000000") << fullResult.message;
    EXPECT_EQ(fullResult.metadata.at("max"), "1.000000") << fullResult.message;
    EXPECT_EQ(fullResult.metadata.at("bins"), "1") << "min == max ⇒ 只能给 1 个桶";
    EXPECT_EQ(fullResult.metadata.at("total_pixels"), "16") << "手推：4*4-0 = 16 个有效像元";
    EXPECT_EQ(fullResult.message.find("\"bin\":1"), std::string::npos)
        << "不得出现第二个桶。实际 message=" << fullResult.message;
    EXPECT_NE(fullResult.message.find("Range: [1, 1]"), std::string::npos)
        << "值域要把两个端点都写出来（Min == Max），实际 message=" << fullResult.message;
    EXPECT_NE(fullResult.message.find("\"count\":16"), std::string::npos)
        << "那一个桶要装下全部 16 个像元，实际 message=" << fullResult.message;

    // 臂 2：12 个 1.0 + 4 个 NoData ⇒ 桶里是 12，不是 w*h=16（打死"按尺寸交差"的假修法）
    const std::string partial = createConstantRasterWithNoDataTail(
        "e2e_hist_const_tail_input.tif", 4, 4, 1.0f, 4);
    params["input"] = partial;
    const auto partialResult = p->execute(params, progress_);
    ASSERT_TRUE(partialResult.success) << partialResult.message;
    EXPECT_EQ(partialResult.metadata.at("bins"), "1");
    EXPECT_EQ(partialResult.metadata.at("total_pixels"), "12")
        << "手推：4*4-4 = 12 个有效像元，NoData 不参与计数";
    EXPECT_NE(partialResult.message.find("\"count\":12"), std::string::npos)
        << "实际 message=" << partialResult.message;
}

// info 对常数栅格要照常报出 Min == Max（改前这一条就是绿的，留着是防"顺手把 info 也改成报错"）。
TEST_F(PluginTest, RasterInspectInfoAcceptsConstantRaster) {
    auto* p = mgr_.find("raster_inspect");
    if (!p) {
        FAIL() << "plugin not loaded: raster_inspect";
    }

    const std::string input = createConstantRasterWithNoDataTail(
        "e2e_info_const_input.tif", 4, 4, 1.0f, 4);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("info");
    params["input"] = input;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_NE(result.message.find("Min:    1"), std::string::npos)
        << "常数栅格要报出最小值 1。实际 message=" << result.message;
    EXPECT_NE(result.message.find("Max:    1"), std::string::npos)
        << "常数栅格要报出最大值 1（与最小值相等是合法的）。实际 message=" << result.message;
    EXPECT_EQ(result.message.find("no valid pixels"), std::string::npos)
        << "12 个有效像元不该被说成没有有效像元。实际 message=" << result.message;
}

// 全 NoData（=N132 静默烧 0 产物的形状）才是"算不出统计"，必须报错，而且案文要和
// 改前那句 min >= max 不同 —— 两种情况共用一句话正是本缺陷的后一半。
TEST_F(PluginTest, RasterInspectHistogramRejectsAllNoDataBand) {
    auto* p = mgr_.find("raster_inspect");
    if (!p) {
        FAIL() << "plugin not loaded: raster_inspect";
    }

    const std::string input = createConstantRasterWithNoDataTail(
        "e2e_hist_nodata_input.tif", 4, 4, 0.0f, 16);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("histogram");
    params["input"] = input;
    params["bins"] = 8;

    const auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success) << "全 NoData 的波段算不出统计，必须失败：" << result.message;
    EXPECT_NE(result.message.find("no valid pixels"), std::string::npos)
        << "案文要明说没有有效像元，而不是含糊的没有值域。实际 message=" << result.message;
    EXPECT_EQ(result.message.find("no value range"), std::string::npos)
        << "不得再复用改前那句 min >= max 案文（它同时被常数栅格与全 NoData 共用 ⇒ 诊断是假的）。"
           "实际 message=" << result.message;
}

// info 这一侧：算不出统计时不整块静默消失，要把原因写出来（与 histogram 的案文同口径），
// 但仍不因此让整个只读报告动作失败 —— 见报告里的两条路对账。
TEST_F(PluginTest, RasterInspectInfoDistinguishesAllNoDataBand) {
    auto* p = mgr_.find("raster_inspect");
    if (!p) {
        FAIL() << "plugin not loaded: raster_inspect";
    }

    const std::string input = createConstantRasterWithNoDataTail(
        "e2e_info_nodata_input.tif", 4, 4, 0.0f, 16);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("info");
    params["input"] = input;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    EXPECT_NE(result.message.find("no valid pixels"), std::string::npos)
        << "算不出统计时要说明原因，不能只把统计块整块删掉。实际 message=" << result.message;
    EXPECT_EQ(result.message.find("Min:    "), std::string::npos)
        << "没有有效像元就不该编造 Min。实际 message=" << result.message;
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
        {"evi", "e2e_spindex_evi_output.tif", {{"blue_band", 1}, {"red_band", 3}, {"nir_band", 4}, {"g_value", 2.5}, {"c1", 6.0}, {"c2", 7.5}, {"evi_l", 1.0}}, 100.0f / 176.0f},
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

// Task 9：EVI 的 L 与 SAVI 的 L 必须是两个参数。
// WHY 不能共用 l_value：EVI 标准 L=1、SAVI 标准 L=0.5，一个键无法同时是两个默认值。
// 缺陷机制不是"代码兜底写错"（EVI 分支的兜底本来就是 1.0），而是 paramSpecs 的 l_value 默认值 0.5
// 与该兜底不一致：GUI 的 collectValues() 会把每个 spec 的 defaultValue 无条件提交，
// 于是走 GUI 的用户拿到 L=0.5、走库/CLI 不传 L 的调用方拿到 L=1 —— 同一个 evi 动作两条路径两种结果。
// 夹具 {10,20,30,70} + blue=1/red=3/nir=4 ⇒ 2.5*(70-30)/(70+6*30-7.5*10+L) = 100/(175+L)：
// L=1 ⇒ 100/176，L=0.5 ⇒ 100/175.5。
TEST_F(PluginTest, EviUsesEviLDefaultOfOneNotSaviHalf) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);
    const std::string input = createMultiBandConstantRaster(
        "spi_evi_l_input.tif", 24, 24, {10.0f, 20.0f, 30.0f, 70.0f});
    const std::string output = utf8PathString(getTestDir() / "spi_evi_l_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("evi");
    params["input"] = input;
    params["output"] = output;
    params["blue_band"] = 1;
    params["red_band"] = 3;
    params["nir_band"] = 4;
    // 不传任何 L：必须使用 EVI 标准 L=1 -> 100/176，而非 SAVI 的 0.5

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 5, 5), 100.0f / 176.0f, 1e-5f);
}

TEST_F(PluginTest, EviHonoursExplicitEviL) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);
    const std::string input = createMultiBandConstantRaster(
        "spi_evi_l05_input.tif", 24, 24, {10.0f, 20.0f, 30.0f, 70.0f});
    const std::string output = utf8PathString(getTestDir() / "spi_evi_l05_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("evi");
    params["input"] = input;
    params["output"] = output;
    params["blue_band"] = 1;
    params["red_band"] = 3;
    params["nir_band"] = 4;
    params["evi_l"] = 0.5;   // 必须是 double：getParam 在类型不匹配时静默回落默认值

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 5, 5), 100.0f / 175.5f, 1e-5f);
}

// Tier 2（R-T9-1）：本任务真正的 RED。
// WHY：GUI 会把 paramSpecs() 里所有参数的 defaultValue 原样提交（用户一个参数都没改的场景），
// 所以"遍历 spec 提交默认值"就是 GUI 路径的等价构造；修复前它必然拿到 SAVI 的 0.5。
TEST_F(PluginTest, GuiStyleDefaultSubmissionForEviYieldsStandardL) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);

    const std::string input = createMultiBandConstantRaster(
        "spi_evi_gui_input.tif", 24, 24, {10.0f, 20.0f, 30.0f, 70.0f});
    const std::string output = utf8PathString(getTestDir() / "spi_evi_gui_output.tif");

    // 复刻 GUI：遍历 spec，把默认值全部提交（用户一个参数都没改的场景）
    std::map<std::string, gis::framework::ParamValue> params;
    for (const auto& spec : p->paramSpecs()) {
        if (spec.key == "action") { params["action"] = std::string("evi"); continue; }
        if (spec.key == "input")  { params["input"] = input; continue; }
        if (spec.key == "output") { params["output"] = output; continue; }
        params[spec.key] = spec.defaultValue;
    }

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 5, 5), 100.0f / 176.0f, 1e-5f)
        << "GUI 全默认提交必须得到 EVI 标准 L=1 的结果";
}

// Tier 2（R-T9-2）：把"EVI 读的是哪个键"钉死。
// WHY：若只有上面两条，将来有人把 EVI 分支改回读 l_value（兜底仍写 1.0），前两条照样绿，
// 而 GUI 全默认提交发来的 l_value=0.5 会再次静默胜出 —— 本缺陷原样复活。
// 本用例显式提交 l_value=0.5（GUI 那条路径至今仍在发这个键），断言 EVI 必须是 100/176。
TEST_F(PluginTest, EviRejectsSharedSaviLValueKey) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);
    const std::string input = createMultiBandConstantRaster(
        "spi_evi_reject_lvalue_input.tif", 24, 24, {10.0f, 20.0f, 30.0f, 70.0f});
    const std::string output = utf8PathString(getTestDir() / "spi_evi_reject_lvalue_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("evi");
    params["input"] = input;
    params["output"] = output;
    params["blue_band"] = 1;
    params["red_band"] = 3;
    params["nir_band"] = 4;
    params["l_value"] = 0.5;   // SAVI 的键，EVI 必须无视它

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_NEAR(gis::tests::readPixel(output, 1, 5, 5), 100.0f / 176.0f, 1e-5f)
        << "EVI 不得读取 SAVI 的 l_value 键，否则 GUI 的 0.5 会再次静默胜出";
}

// Fix round 1（F1 / Important I-1）：SAVI 侧的 L 此前完全没有守护。
// WHY 单靠 GOLD 表那条 savi 行封不住：它提交 l_value=0.5，而"SAVI 分支改读 evi_l、兜底仍写 0.5"这种
// 变异下缺键恰好也回落 0.5 ⇒ 期望值 60/100.5 一动不动（审查者实测 542/542 全绿；本轮复跑同一变异，
// 只有本用例红 ⇒ 用户填的 l_value 被静默吞掉这件事从此有了牙）。
// 所以两个方向都要断：l_value 必须驱动 SAVI，evi_l 必须不得驱动 SAVI。
// 期望值按 SAVI 分支的实际公式推导 SAVI = (NIR - Red) * (1 + L) / (NIR + Red + L)，
// 夹具 {10,20,30,70} + red=3/nir=4 ⇒ NIR=70、Red=30：
//   L=0.9 ⇒ 40 * 1.9 / (100 + 0.9) = 76/100.9
//   L=0.5 ⇒ 40 * 1.5 / (100 + 0.5) = 60/100.5
TEST_F(PluginTest, SaviUsesLValueAndIgnoresEviL) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);
    const std::string input = createMultiBandConstantRaster(
        "spi_savi_l_input.tif", 24, 24, {10.0f, 20.0f, 30.0f, 70.0f});

    // 方向一：显式提交 l_value=0.9，SAVI 必须按它算。
    const std::string drivenOutput = utf8PathString(getTestDir() / "spi_savi_l09_output.tif");
    std::map<std::string, gis::framework::ParamValue> drivenParams;
    drivenParams["action"] = std::string("savi");
    drivenParams["input"] = input;
    drivenParams["output"] = drivenOutput;
    drivenParams["red_band"] = 3;
    drivenParams["nir_band"] = 4;
    drivenParams["l_value"] = 0.9;   // 必须是 double：getParam 在类型不匹配时静默回落默认值

    const auto driven = p->execute(drivenParams, progress_);
    ASSERT_TRUE(driven.success) << driven.message;
    EXPECT_NEAR(gis::tests::readPixel(drivenOutput, 1, 5, 5), 76.0f / 100.9f, 1e-5f)
        << "SAVI 必须读取 l_value，否则用户填的 0.9 会被静默换成兜底 0.5";

    // 方向二：只提交 evi_l，SAVI 不得越权读它，必须继续用兜底 0.5。
    const std::string rejectOutput = utf8PathString(getTestDir() / "spi_savi_reject_evil_output.tif");
    std::map<std::string, gis::framework::ParamValue> rejectParams;
    rejectParams["action"] = std::string("savi");
    rejectParams["input"] = input;
    rejectParams["output"] = rejectOutput;
    rejectParams["red_band"] = 3;
    rejectParams["nir_band"] = 4;
    rejectParams["evi_l"] = 0.1;   // EVI 专属键，SAVI 读到它的话结果就变成 44/100.1

    const auto rejected = p->execute(rejectParams, progress_);
    ASSERT_TRUE(rejected.success) << rejected.message;
    EXPECT_NEAR(gis::tests::readPixel(rejectOutput, 1, 5, 5), 60.0f / 100.5f, 1e-5f)
        << "SAVI 不得读取 EVI 的 evi_l 键，否则两个指数的 L 又耦回去了";
}

// Fix round 1（F2 / Important I-2）：四条 EVI 用例锁的都是"evi_l 取到什么值"，没有任何一条锁
// "evi_l 这条 spec 有没有被声明"。WHY 必须单独锁声明：把整条 spec 删掉后 542/542 仍全绿 ——
// 成因是 N35（批次级，本轮不修）：src/cli/main.cpp:76-82 对未知参数键不报错、原样存成 std::string，
// 而 param_spec.h:62 的 std::get_if<double> 失配 ⇒ getParam 静默回落代码兜底，
// 审查者实测删掉 spec 后 CLI `--evi_l=0.5` 返回 L=1 的结果且 exit 0。
// 形状照抄"声明的默认值必须与代码兜底同值"那条先例（本文件里 raster_manage band 的那一段）。
TEST_F(PluginTest, SpindexEviLSpecDeclarationMatchesEviFallback) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);

    const auto specs = p->paramSpecs();
    const gis::framework::ParamSpec* eviLSpec = nullptr;
    for (const auto& spec : specs) {
        if (spec.key == "evi_l") eviLSpec = &spec;
    }
    ASSERT_NE(eviLSpec, nullptr)
        << "EVI 分支的兜底读的就是这个键；spec 被删掉时 GUI 面板不再渲染它、CLI 也不再校验它";
    EXPECT_EQ(eviLSpec->type, gis::framework::ParamType::Double)
        << "类型漂移成 Int/String 后 getParam<double> 会静默回落默认值，取值断言照样绿";

    const auto* declaredDefault = std::get_if<double>(&eviLSpec->defaultValue);
    ASSERT_NE(declaredDefault, nullptr) << "evi_l 的默认值必须是 double 变体";
    EXPECT_EQ(*declaredDefault, 1.0)
        << "evi_l 的 spec 默认值必须与 spindex_plugin.cpp EVI 分支的代码兜底 1.0 同值";

    const auto* declaredMin = std::get_if<double>(&eviLSpec->minValue);
    const auto* declaredMax = std::get_if<double>(&eviLSpec->maxValue);
    ASSERT_NE(declaredMin, nullptr) << "evi_l 的下界必须是 double 变体";
    ASSERT_NE(declaredMax, nullptr) << "evi_l 的上界必须是 double 变体";
    EXPECT_EQ(*declaredMin, 0.0);
    EXPECT_EQ(*declaredMax, 1.0)
        << "上界被放宽（修前只跑本用例的聚焦集是全绿；"
           "round-1 复审在全量上复跑 MF，唯一红的是本用例。K-21：断言消息里不写日志文件名、"
           "不写会被后续任务改动的全量条数，要复算就跑当前全量）"
           "等于把 EVI 的背景因子取值域开了口子";

    // T15-B：required 漂移同样没人守（变异 false→true 时全量仍绿），后果是可观察的：
    // src/cli/main.cpp 会把它打成 [required]、并且不再打印 "Default: 1"，
    // param_card_widget.cpp 会加必填标记 ⇒ "标成必填、隐藏默认值、实际又不必填"。
    EXPECT_FALSE(eviLSpec->required)
        << "evi_l 的 spec 是可选、有默认值的键；标成必填会让 CLI 隐藏默认值、面板加必填标记，"
           "而 EVI 分支不传它照样按兜底 1.0 算（spindex_plugin.cpp 里读 evi_l 的那一行）";
}

// T15-A：SAVI 侧的同一缺陷形状（Task 9 round-1 复审实测：把 spindex_plugin.cpp 里
// l_value 的 spec 默认 double{0.5} 改成 double{0.3} ⇒ 全量仍然全绿，
// 而面板与库从此分叉：gui_regression/nested/spindex_savi/status.json 得到按 0.3 算的
// SAVI computed range [0.518445, …]（= 40*1.3/100.3），库/CLI 不传参时按代码兜底 0.5 得 0.597015）。
// 判据必须与 spindex_plugin.cpp SAVI 分支的代码兜底同值 —— 这条用例守的是"spec 不许自己漂移"，
// GuiContract 抓不到它：那个元测试的 origin 就是插件这条 spec 本身，属同义相对判据，
// 对"插件 spec 自己漂移"结构性免疫（复审者用 M2′/MF2 两个变异实测 GuiContract 恒绿），
// 所以**不得**拿 GuiContract 绿当这条锁存在的证据。
TEST_F(PluginTest, SpindexSaviLSpecDeclarationMatchesSaviFallback) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);
    const auto specs = p->paramSpecs();

    const auto* lSpec = findParamSpec(specs, "l_value");
    ASSERT_NE(lSpec, nullptr)
        << "SAVI 分支的兜底读的就是这个键；spec 被删掉时 GUI 面板不再渲染它、CLI 也不再校验它";
    EXPECT_EQ(lSpec->type, gis::framework::ParamType::Double)
        << "类型漂移成 Int/String 后 getParam<double> 会静默回落默认值，取值断言照样绿";

    const auto* declaredDefault = std::get_if<double>(&lSpec->defaultValue);
    ASSERT_NE(declaredDefault, nullptr) << "l_value 的默认值必须是 double 变体";
    EXPECT_EQ(*declaredDefault, 0.5)
        << "l_value 的 spec 默认值必须与 spindex_plugin.cpp SAVI 分支的代码兜底 0.5 同值";

    const auto* declaredMin = std::get_if<double>(&lSpec->minValue);
    const auto* declaredMax = std::get_if<double>(&lSpec->maxValue);
    ASSERT_NE(declaredMin, nullptr) << "l_value 的下界必须是 double 变体";
    ASSERT_NE(declaredMax, nullptr) << "l_value 的上界必须是 double 变体";
    EXPECT_EQ(*declaredMin, 0.0);
    EXPECT_EQ(*declaredMax, 1.0)
        << "取值域被放宽等于给 SAVI 的背景因子开了口子";

    // required 与 evi_l 同族（T15-B 的后果链对 l_value 同样成立）。
    EXPECT_FALSE(lSpec->required)
        << "l_value 是可选、有默认值的键；标成必填会让 CLI 隐藏默认值、面板加必填标记";
}

// R-T10-9：Task 9 只改了 l_value 的 displayName，真正根治在 spindex.evi 的白名单。
// 这里断两层，缺一不可：
//   1) buildEffectiveGuiParamSpecs 过滤后的有效键集合（R-T10-5 的元断言只管
//      visibleKeys ⊆ paramSpecs，抓不到"键被渲染但本 action 不读"）；
//   2) 真实 widget 树的渲染键 —— 问 ParamCardWidget::hasParam（entries_，即 addParam
//      真建出来的那一行）。WHY 不是 ParamWidget::hasParam：MainWindow::setParamValue
//      （mainwindow.cpp:557-558）用的就是它，而它只扫 specs_（param_widget.cpp:158-163），
//      拿它断"渲染键"等于又抄一遍有效集合。
// 两个 action 方向相反（evi 用 evi_l、savi 用 l_value），一起断才封得住"共用 L 键"这一族。
TEST_F(PluginTest, SpindexEviAndSaviGuiPanelsRenderOnlyTheLKeyThatEachActionReads) {
    auto* p = mgr_.find("spindex");
    ASSERT_NE(p, nullptr);
    const auto specs = p->paramSpecs();

    const struct { const char* action; const char* liveKey; const char* deadKey; } kCases[] = {
        {"evi", "evi_l", "l_value"},
        {"savi", "l_value", "evi_l"},
    };

    for (const auto& kase : kCases) {
        const std::string action = kase.action;
        const auto* config = gis::gui::findActionUiConfig("spindex", action);
        ASSERT_NE(config, nullptr) << "spindex." << action << " 缺少 actionUiConfig 条目";

        const auto effective = gis::gui::buildEffectiveGuiParamSpecs(
            "spindex", action, specs, config->visibleKeys, config->requiredKeys);
        std::set<std::string> effectiveKeys;
        for (const auto& spec : effective) {
            effectiveKeys.insert(spec.key);
        }
        EXPECT_GT(effectiveKeys.count(kase.liveKey), 0u)
            << action << " 必须渲染 " << kase.liveKey;
        EXPECT_EQ(effectiveKeys.count(kase.deadKey), 0u)
            << action << " 不得渲染 " << kase.deadKey << "（该 action 从不读它）";

        ParamWidget panel;
        panel.setUiContext("spindex", action);
        panel.setParamSpecs(effective);

        // 渲染层必须问 ParamCardWidget::hasParam（param_card_widget.cpp:937 读 entries_，
        // 也就是真正建出来的控件行）。WHY 不用 ParamWidget::hasParam：它
        // （param_widget.cpp:158-163）只扫 setParamSpecs 存下来的 specs_，等于把有效集合再抄一遍，
        // 断不出"这一行到底建没建"。
        const auto cards = panel.findChildren<ParamCardWidget*>();
        ASSERT_FALSE(cards.empty()) << action << " 没有生成任何参数卡片";
        std::set<std::string> renderedKeys;
        for (const auto& spec : specs) {
            if (spec.key == "action") {
                continue;
            }
            for (const auto* card : cards) {
                if (card && card->hasParam(spec.key)) {
                    renderedKeys.insert(spec.key);
                }
            }
        }
        // 渲染集合必须与有效集合完全相等：少一个是功能缺失，多一个是本任务要消灭的形态。
        EXPECT_EQ(renderedKeys, effectiveKeys) << action << " 的 widget 树与有效 spec 不一致";
        EXPECT_GT(renderedKeys.count(kase.liveKey), 0u) << action;
        EXPECT_EQ(renderedKeys.count(kase.deadKey), 0u) << action;
    }

    // WHY 这两个键不能进 commonParamTextStorage：那张表按参数键跨插件全局覆写 displayName，
    // 加进去会静默盖掉插件侧的名字（今天没有任何测试能发现）。这里把它钉成断言。
    EXPECT_EQ(gis::gui::findCommonParamText("l_value"), nullptr);
    EXPECT_EQ(gis::gui::findCommonParamText("evi_l"), nullptr);
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

// C3c-A（info 的 A 半边）：整串相等 + 手算 extent 数值。
// 夹具坐标 116.00/116.04 与 40.00/40.04 是下面手写的；info 的 message 是**唯一**产物
// （doInfo 只回文本、不写文件），所以这里的期望值一律按夹具手推，不从产物回填：
//   层数 1、层名 e2e_vector_test（shapefile 的层名 = 文件 stem）、Geometry Point、CRS EPSG:4326、
//   Features 5、三个字段的**名字与顺序**、
//   Extent 四边。默认 ostream 精度 6 位有效数字 ⇒ 116.04 / 40.04 正好是整串里的写法。
// N115 现读：info 只在 GPKG 输入旁写 .aux.xml；本夹具是 shapefile（实测不产边车），
//   且输入本来就在 build/ 的测试目录里 —— 不碰 data/ 的纪律不受影响。
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
        // 字段名与顺序是 info 契约的一部分：三个类型各一，回读时顺序必须与创建顺序一致。
        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);
        OGRFieldDefn codeField("code", OFTInteger);
        ASSERT_EQ(layer->CreateField(&codeField), OGRERR_NONE);
        OGRFieldDefn ratioField("ratio", OFTReal);
        ASSERT_EQ(layer->CreateField(&ratioField), OGRERR_NONE);
        auto* featDefn = layer->GetLayerDefn();
        for (int i = 0; i < 5; ++i) {
            auto* feat = OGRFeature::CreateFeature(featDefn);
            OGRPoint pt(116.0 + i * 0.01, 40.0 + i * 0.01);
            feat->SetGeometry(&pt);
            const std::string name = "p" + std::to_string(i);
            feat->SetField("name", name.c_str());
            feat->SetField("code", i + 1);
            feat->SetField("ratio", 0.25 * (i + 1));
            layer->CreateFeature(feat);
            OGRFeature::DestroyFeature(feat);
        }
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("info");
    params["input"] = shpPath;

    auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << "Vector info failed: " << result.message;

    // 整串相等：层数、层名、几何类型、extent 的打印形态、CRS、要素数、字段名/顺序/类型。
    const std::string expected =
        "Vector Info: " + shpPath + "\n"
        "  Driver: ESRI Shapefile\n"
        "  Layers: 1\n"
        "\n"
        "  Layer [0]: e2e_vector_test\n"
        "    Geometry: Point\n"
        "    Extent:  116, 40 - 116.04, 40.04\n"
        "    CRS:     EPSG:4326\n"
        "    Features: 5\n"
        "    Fields:  3\n"
        "      [0] name (String)\n"
        "      [1] code (Integer)\n"
        "      [2] ratio (Real)\n";
    EXPECT_EQ(result.message, expected);

    // 数值层单独钉一次（GOLD gate 要求块体里有 EXPECT_DOUBLE_EQ/NEAR/FLOAT_EQ；
    // 这里把 extent 行解析成四个 double 再与手算值比，失败时能直接看见是哪条边）。
    const std::string extentMarker = "    Extent:  ";
    const std::size_t at = result.message.find(extentMarker);
    ASSERT_NE(at, std::string::npos) << "info 文本里找不到 extent 行: " << result.message;
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    const int parsed = std::sscanf(result.message.c_str() + at + extentMarker.size(),
                                   "%lf, %lf - %lf, %lf", &minX, &minY, &maxX, &maxY);
    ASSERT_EQ(parsed, 4) << "extent 行解析失败: " << result.message.substr(at);
    EXPECT_DOUBLE_EQ(minX, 116.0);
    EXPECT_DOUBLE_EQ(minY, 40.0);
    EXPECT_DOUBLE_EQ(maxX, 116.04);
    EXPECT_DOUBLE_EQ(maxY, 40.04);
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

// buffer 的**几何侧**判据。WHY 另开一条而不是往 VectorBufferExecutionReportsMetadata 里加：
// 那条夹具是 200 条三点斜线（它是为"两万级要素 + 别触发 gpkg_extensions 报错"服务的），形状不可手算；
// 它今天的判据只到 metadata 与一个属性值，面积/包框一个都没读 ⇒ 登记表一直把它记在 PENDING。
// 本条夹具是一条 (0,0)-(100,0) 的两点线 ⇒ 产物是"运动场"形（中间 2d×L 的矩形 + 两端各一个半圆），
// 四个量都能手推：
//   面积上界 = 2dL + πd²（GEOS 的圆头是**内接**折线，弦在圆弧内侧 ⇒ 不可能超过理想圆）；
//   面积下界 = 2dL + (n/2)·d²·sin(2π/n) 取 n = 32（象限段最小 8 ⇒ 两个半圆合成一整圆、至少 32 段；
//     本仓 vcpkg 现读 ogr_geometry.h:565 `Buffer(double dfDist, int nQuadSegs = 30)` ⇒ 实际是 120 段，
//     落在两条界之间）。下界写 n=32 而不是 n=120，是为了不把判据钉在 GDAL 的缺省段数上 —— 段数是上游
//     可调常量，把它写死等于"上游改个缺省值就红"，而那不该是缺陷。
//   包框四边 = (-d,-d)-(L+d,d)，容差 0.1 也是手推的：极值若落在相邻两段之间，误差上界
//     d·(1−cos(180/n/2))，n=32、d=50 时 = 50·(1−cos(2.8125°)) = 0.060 ⇒ 0.1 有约 1.7× 余量；
//     写 0 会被圆头相位差判红，那不是缺陷。
// 两条容差臂（25 与 50）都要跑：只有一条臂时，"distance 不生效、恒用缺省 100"这类坏法会同时躲过
// 面积与包框（它的产物只是"比期望大"，而一条臂的期望恰好也允许这种比例误差 —— 判别结果见发布台账）。
TEST_F(PluginTest, VectorBufferRoundCapStadiumMatchesHandComputed) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_buffer_stadium_input.gpkg");
    const double lineLength = 100.0;
    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("stadium_line", srs.get(), wkbLineString, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRLineString line;
        line.addPoint(0.0, 0.0);
        line.addPoint(lineLength, 0.0);
        feat->SetGeometry(&line);
        feat->SetField("name", "trunk");
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    auto expectStadium = [&](const std::string& path, double distance) {
        GDALDataset* ds = static_cast<GDALDataset*>(GDALOpenEx(
            path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
        if (!ds) {
            ADD_FAILURE() << "cannot open buffer output: " << path;
            return;
        }
        OGRLayer* layer = ds->GetLayer(0);
        if (!layer) {
            ADD_FAILURE() << "buffer output has no layer: " << path;
            GDALClose(ds);
            return;
        }
        // 层名 = 源层名 + "_buffer"（doBuffer 里 outputLayerName 那条契约）；CRS 与层声明也不许漂。
        EXPECT_STREQ(layer->GetName(), "stadium_line_buffer");
        const OGRSpatialReference* outSrs = layer->GetSpatialRef();
        ASSERT_NE(outSrs, nullptr);
        EXPECT_STREQ(outSrs ? outSrs->GetAuthorityCode(nullptr) : "", "3857");
        EXPECT_EQ(wkbFlatten(layer->GetGeomType()), wkbMultiPolygon);

        OGRFeature* feat = layer->GetNextFeature();
        if (!feat) {
            ADD_FAILURE() << "buffer output has no feature: " << path;
            GDALClose(ds);
            return;
        }
        const OGRGeometry* geom = feat->GetGeometryRef();
        EXPECT_STREQ(feat->GetFieldAsString("name"), "trunk");

        const double rectArea = 2.0 * distance * lineLength;
        constexpr int kMinCircleSegments = 32;
        const double lowerArea = rectArea +
            (kMinCircleSegments / 2.0) * distance * distance * std::sin(2.0 * M_PI / kMinCircleSegments);
        const double upperArea = rectArea + M_PI * distance * distance;
        // get_Area() 不在 OGRGeometry 基类上（同 :4553 那条理由）⇒ 走 OGRGeometryCollection 那一支读。
        const auto* collection = dynamic_cast<const OGRGeometryCollection*>(geom);
        ASSERT_NE(collection, nullptr);
        const double area = collection ? collection->get_Area() : -1.0;
        EXPECT_GE(area, lowerArea) << "distance=" << distance << " area=" << area
                                   << " 低于手推下界 " << lowerArea << " ⇒ 圆头被削/没缓冲";
        EXPECT_LE(area, upperArea) << "distance=" << distance << " area=" << area
                                   << " 超过手推上界 " << upperArea << " ⇒ 距离或单位弄错";

        OGREnvelope env;
        if (geom) {
            geom->getEnvelope(&env);
        }
        EXPECT_NEAR(env.MinX, -distance, 0.1);
        EXPECT_NEAR(env.MaxX, lineLength + distance, 0.1);
        EXPECT_NEAR(env.MinY, -distance, 0.1);
        EXPECT_NEAR(env.MaxY, distance, 0.1);

        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    };

    const std::string nearOutput = utf8PathString(getTestDir() / "e2e_buffer_stadium_d25.gpkg");
    std::map<std::string, gis::framework::ParamValue> nearParams;
    nearParams["action"] = std::string("buffer");
    nearParams["input"] = input;
    nearParams["output"] = nearOutput;
    nearParams["distance"] = 25.0;
    const auto nearResult = p->execute(nearParams, progress_);
    ASSERT_TRUE(nearResult.success) << nearResult.message;
    EXPECT_EQ(nearResult.metadata.at("feature_count"), "1");
    // 记账与实际生效值是两件事：metadata 里那个 distance 只是把入参回显，坏实现照样把它写得漂漂亮亮。
    EXPECT_EQ(nearResult.metadata.at("distance"), "25");
    expectStadium(nearOutput, 25.0);

    const std::string farOutput = utf8PathString(getTestDir() / "e2e_buffer_stadium_d50.gpkg");
    std::map<std::string, gis::framework::ParamValue> farParams;
    farParams["action"] = std::string("buffer");
    farParams["input"] = input;
    farParams["output"] = farOutput;
    farParams["distance"] = 50.0;
    const auto farResult = p->execute(farParams, progress_);
    ASSERT_TRUE(farResult.success) << farResult.message;
    EXPECT_EQ(farResult.metadata.at("feature_count"), "1");
    EXPECT_EQ(farResult.metadata.at("distance"), "50");
    expectStadium(farOutput, 50.0);
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

// ── Task 16 Tier 1：分水岭自动标记路径缺 OpenCV 约定的 `markers += 1` ─────────────
// 为什么主鉴别断言是**关系式**而不是计数（R-T16-1）：
//   改前 sureFg 的两个圆盘就是 connectedComponents 的标签 1 和 2，其余像元（含"确定背景"）全是 0，
//   而 cv::watershed 把 0 一律当"未知区"向外淹没 ⇒ 整幅背景被并进标签 1 的盆地
//   ⇒ 旧的 maxLabel 计数改前改后都得 "2" ⇒ 计划 Step 1 的 EXPECT_EQ(segment_count,"2") **没有鉴别力**。
//   实测改前的输出矩阵（同批自动分支那份产物，32x32 双圆夹具）：
//   标签 1 铺满除圆盘 B 邻域以外的整幅图，标签 2 只剩种子附近——背景根本没独立成区。
// 背景取点：不能用角点 (0,0)。OpenCV watershed 会把"触及影像边框的盆地"沿最外一圈写成 -1
//   （实测改前/改后 (0,0) 都是 -1，用它做背景探针会让断言与缺陷无关），
//   故背景取内部点 (x=16,y=2)：离两个圆心都 > 膨胀半径 8+5，既在确定背景里、又不贴边框。
// 四组关系式（每条到底鉴不鉴别，见下面"逐条鉴别力"一段）：
//   (i) 背景标签 ≠ 任一圆盘种子标签；(ii) 两个种子标签互不相等；
//   (iii) 种子标签 >= 2（OpenCV 约定背景独占 1、区域从 2 起，偏移反了或没偏移都会红）；
//   (iv) 盘内、种子以外的未知带必须被自己的盆地涨满（挡住"先归零、再整体 +1"这种顺序颠倒的实现，
//        它让未知带从一开始就是背景 ⇒ 分水岭不再扩张）。
//   改后应为 背景=1、两区域=2/3（各 80 像元 = 整个圆盘）、边界=-1。
// 逐条鉴别力（一手证据：改前那次红日志里 (i)/(iii) 两条的失败行，实测背景(16,2)=1、A心=1、B心=2）：
//   改前**只有 2 条会红** —— (i) 的 `background != centerA`（1 vs 1）与 (iii) 的 `centerA >= 2`（1 vs 2）。
//   B 盘在改前就独立带着标签 2，所以 (i) 的 `background != centerB`、(ii)、(iii) 的 `centerB >= 2` 改前都是绿的；
//   (i) 处紧跟的 `EXPECT_EQ(background, 1)` 改前改后**同值 = 1**（改前/改后两份落盘矩阵在 (16,2) 都是 1）⇒ **不作鉴别证据**，它是行为锁：钉住"自动分支写出的背景位
//   是标签 1"这一对外可见口径（改前的 1 来自"0 背景被洪水吞进区域 1"、改后的 1 来自"背景独占标签 1"，
//   同一个数字来自两条相反的路径），与下面 EXPECT_EQ(segment_count,"2")、EXPECT_GE(distinctLabels,3)
//   两处"不作鉴别证据"注释同口径 ⇒ 不删也不加强。
//   其余几条并非无用，只是各挡别的假实现：(ii) 挡"两盘并成一盆"（变异 A1 实测会红：两盘合成同一个标签），
//   (iv) 挡"先归零、再整体 +1"的顺序颠倒（变异 A2b 实测会红：未知带从一开始就是标签 1、种子只剩 25 像元的核）。
TEST_F(PluginTest, ProcessingWatershedAutoMarkersKeepBackgroundAsOwnBasin) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    // 32x32 里两个分离的实心圆盘：半径 5，圆心分别在 (x=9,y=16) 与 (x=23,y=16)，盘内 255、盘外 0。
    const int w = 32, h = 32;
    std::vector<float> data(static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float dy = static_cast<float>(y) - 16.0f;
            const bool inA = std::hypot(static_cast<float>(x) - 9.0f, dy) <= 5.0f;
            const bool inB = std::hypot(static_cast<float>(x) - 23.0f, dy) <= 5.0f;
            data[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                (inA || inB) ? 255.0f : 0.0f;
        }
    }
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "ws_t16_auto_input.tif", w, h, data);
    const std::string output = utf8PathString(getTestDir() / "ws_t16_auto_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("watershed");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    const auto countIt = result.metadata.find("segment_count");
    ASSERT_NE(countIt, result.metadata.end());

    // 标签按整数读：分水岭输出本就是整型标签，拿 float 做相等比较会把类型问题混成数值噪声。
    const int background = static_cast<int>(readRasterPixel(output, 16, 2));  // 确定背景（内部取点）
    const int corner = static_cast<int>(readRasterPixel(output, 0, 0));       // 仅作诊断：贴边框一圈恒为 -1
    const int centerA = static_cast<int>(readRasterPixel(output, 9, 16));     // 圆盘 A 的种子
    const int centerB = static_cast<int>(readRasterPixel(output, 23, 16));    // 圆盘 B 的种子
    // 盘内、种子以外的"未知带"取点：距盘心 4 px，在半径约 3.5 的 sureFg 之外、半径 5 的盘之内。
    //   硬约束：**距 -1 谷线须 >= 2 像元**。当前实现下 y=16 行实测
    //   x=13/14 为 2、x=15 已是 -1；x=18/19 为 3、x=17 是 -1 ⇒ (13,16) 与 (19,16) 各只剩 2 像元余量。
    //   2 是下限而不是经验值：谷线厚度由 cv::watershed 按梯度决定、随影像几何变化，取点往盘边挪 1 px
    //   就可能读到 -1 —— 那时红的是取点、不是算法。
    const int diskEdgeA = static_cast<int>(readRasterPixel(output, 13, 16));
    const int diskEdgeB = static_cast<int>(readRasterPixel(output, 19, 16));
    // 每个关系式断言都带这串实测数字：红日志里必须能直接读到改前/改后的标签与计数。
    const std::string observed = " 实测[背景(16,2)=" + std::to_string(background) +
                                 ", 角点(0,0)=" + std::to_string(corner) +
                                 ", A心(9,16)=" + std::to_string(centerA) +
                                 ", B心(23,16)=" + std::to_string(centerB) +
                                 ", A未知带(13,16)=" + std::to_string(diskEdgeA) +
                                 ", B未知带(19,16)=" + std::to_string(diskEdgeB) +
                                 ", segment_count=" + countIt->second + "]";

    // (i) 背景必须独占一个标签：缺 `markers += 1` 时背景被并进标签 1 的盆地，与 A 的种子标签相等。
    //   这一组里改前会红的只有第一条 `background != centerA`（`!= centerB` 改前是 1 vs 2，不红）；
    //   下面的 `EXPECT_EQ(background, 1)` 改前改后同值 = 1 ⇒ 行为锁、不作鉴别证据（口径见函数头注释）。
    EXPECT_NE(background, centerA) << "确定背景与圆盘 A 共用标签 ⇒ 背景被当成未知区淹没" << observed;
    EXPECT_NE(background, centerB) << "确定背景与圆盘 B 共用标签 ⇒ 同上" << observed;
    EXPECT_EQ(background, 1) << "OpenCV 约定：确定背景应为标签 1" << observed;
    // (ii) 两个分离目标不得被并成一个盆地。
    EXPECT_NE(centerA, centerB) << "两个分离目标被并成同一个盆地" << observed;
    // (iii) 区域标签必须从 2 起（背景独占 1）；没做 +1 时种子标签是 1，这里必红。
    EXPECT_GE(centerA, 2) << "区域标签应从 2 起，背景独占 1" << observed;
    EXPECT_GE(centerB, 2) << "区域标签应从 2 起，背景独占 1" << observed;
    // (iv) 未知区必须在整体偏移**之后**才归零：那样它才会被自己的盆地涨满（实测正确实现里两个
    //   种子各占 80 像元 = 整个圆盘）。若把 "+1" 挪到归零循环之后，未知带从一开始就是标签 1，
    //   分水岭不再扩张 ⇒ 盘内取点留在 1、种子只剩 25 像元的核 ⇒ 这两条会红（变异 A2 实测，
    //   变异 A2b 实测红在这两条）。这两个点**距 -1 谷线只有 2 像元**（余量约束及其下限
    //   理由见上面 diskEdgeA/diskEdgeB 声明处的注释），往盘边挪 1 px 就会读到 -1 而变脆。
    EXPECT_EQ(diskEdgeA, centerA) << "圆盘 A 内的未知带没被自己的盆地涨满 ⇒ +1 与归零的顺序倒了" << observed;
    EXPECT_EQ(diskEdgeB, centerB) << "圆盘 B 内的未知带没被自己的盆地涨满 ⇒ 同上" << observed;

    // 计划 Step 1 的计数断言：与上面配对保留，但它**不是**鉴别证据（改前也是 "2"，见函数头注释）。
    EXPECT_EQ(countIt->second, "2")
        << "两个分离目标应计为 2 个区域（确定背景与边界 -1 都不计入）" << observed;

    // 健全性检查，同样不作鉴别证据（改前的标签集 {1,2,-1} 也有 3 个不同值）：
    // 标签栅格至少含 背景 + 两个区域（或边界）这么多种取值。
    auto ds = gis::core::openRaster(output, true);
    ASSERT_NE(ds, nullptr);
    auto* band = ds->GetRasterBand(1);
    ASSERT_NE(band, nullptr);
    std::vector<float> pixels(static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f);
    ASSERT_EQ(band->RasterIO(GF_Read, 0, 0, w, h, pixels.data(), w, h, GDT_Float32, 0, 0), CE_None);
    const std::set<float> distinctLabels(pixels.begin(), pixels.end());
    EXPECT_GE(static_cast<int>(distinctLabels.size()), 3) << "标签栅格退化成单一取值说明分割整体崩了";
}

// ── Task 16 Tier 2：分割计数必须是"唯一区域标签数"，不是 cv::minMaxLoc 的最大标签 ──
// 用**外部标记**分支做这条，因为自动分支的种子标签由 connectedComponents 连续生成（1..N，偏移后 2..N+1），
// 最大标签恰好等于区域数 ⇒ 两种口径在那个几何上相等，测不出缺陷。外部标记的标签由用户给，
// 这里故意给**不连续**的标签 2 与 5（跳过 3、4）：改前 segment_count 报 5，改后报 2。
// 同一幅标记里三个种子的标签值也各自锚住（1/2/5 原样透传到输出），
// 否则"把 segment_count 硬编码成 2"的假实现也能全绿。
TEST_F(PluginTest, ProcessingWatershedExternalMarkersCountDistinctRegionLabels) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    // 32x24 非正方形：顺带让 Tier 3 那条尺寸守卫的"行列写反"变异有东西可挡（见 §4）。
    const int w = 32, h = 24;
    std::vector<float> data(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            data[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                static_cast<float>((x + y) % 16);
        }
    }
    const std::string input = gis::tests::writeFloatRaster(getTestDir(), "ws_t16_ext_input.tif", w, h, data);

    // 标记按 OpenCV 约定书写：1 = 确定背景、>= 2 = 区域、0 = 未知。
    std::vector<float> markers(static_cast<size_t>(w) * static_cast<size_t>(h), 1.0f);
    auto fillRect = [&](int x0, int y0, int x1, int y1, float value) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                markers[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = value;
            }
        }
    };
    fillRect(4, 4, 8, 8, 2.0f);            // 区域 A：标签 2
    fillRect(20, 14, 24, 18, 5.0f);        // 区域 B：标签 5（与 2 之间留标签空洞）
    fillRect(0, 10, w - 1, 12, 0.0f);      // 未知带：0
    const std::string markerPath =
        gis::tests::writeFloatRaster(getTestDir(), "ws_t16_ext_marker.tif", w, h, markers);

    const std::string output = utf8PathString(getTestDir() / "ws_t16_ext_output.tif");
    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("watershed");
    params["input"] = input;
    params["output"] = output;
    params["marker_input"] = markerPath;
    params["band"] = 1;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    const auto countIt = result.metadata.find("segment_count");
    ASSERT_NE(countIt, result.metadata.end());

    const int background = static_cast<int>(readRasterPixel(output, 2, 2));
    const int seedA = static_cast<int>(readRasterPixel(output, 6, 6));
    const int seedB = static_cast<int>(readRasterPixel(output, 22, 16));
    const std::string observed = " 实测[背景(2,2)=" + std::to_string(background) +
                                 ", A心(6,6)=" + std::to_string(seedA) +
                                 ", B心(22,16)=" + std::to_string(seedB) +
                                 ", segment_count=" + countIt->second + "]";

    // 三个种子的标签值原样透传（背景 1 / 区域 2 / 区域 5）：证明 segment_count 是从这幅标记栅格数出来的，
    // 不是硬编码的 2。取点全部避开最外一圈——OpenCV watershed 会把触及边框的盆地沿边框写成 -1。
    EXPECT_EQ(background, 1) << "外部标记里的确定背景标签 1 应原样透传" << observed;
    EXPECT_EQ(seedA, 2) << "外部标记里的区域标签 2 应原样透传" << observed;
    EXPECT_EQ(seedB, 5) << "外部标记里的区域标签 5 应原样透传" << observed;
    EXPECT_EQ(countIt->second, "2")
        << "标记里只有 2 个区域（标签 2 与 5）；最大标签是 5，把 maxLabel 当区域数就少算/多算" << observed;
}

// ── Task 16 Tier 2 补：标记里一个种子都没有 ⇒ 区域数必须是 0 ──────────────────────
// 这条不是"再测一遍计数"，而是计数过滤条件的**唯一可观测边界**：cv::watershed 跑完后所有未知像元
// 都会被某个盆地认领，正常标记里根本不会剩下 0 ⇒ "把 0（未知）当成区域计入"的假实现在上一条用例里
// 测不出来（变异 B4 实测全绿）。只有全 0 标记能让 watershed 真的留下 0：整幅都是未知、没有任何盆地，
// 于是"未知"必须一格都不被算成区域 ⇒ segment_count = 0。
// 注意这条对"改前的 maxLabel 口径"也是绿的（全 0 时最大值同样是 0）⇒ 它是变异杀手，不是红绿证据。
TEST_F(PluginTest, ProcessingWatershedExternalMarkersWithoutSeedsCountsZeroSegments) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    const int w = 16, h = 12;
    std::vector<float> data(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<float>(i % 7);
    }
    const std::string input = gis::tests::writeFloatRaster(getTestDir(), "ws_t16_noseed_input.tif", w, h, data);

    std::vector<float> markers(static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f);  // 全未知、无种子
    const std::string markerPath =
        gis::tests::writeFloatRaster(getTestDir(), "ws_t16_noseed_marker.tif", w, h, markers);

    const std::string output = utf8PathString(getTestDir() / "ws_t16_noseed_output.tif");
    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("watershed");
    params["input"] = input;
    params["output"] = output;
    params["marker_input"] = markerPath;
    params["band"] = 1;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    const auto countIt = result.metadata.find("segment_count");
    ASSERT_NE(countIt, result.metadata.end());
    EXPECT_EQ(countIt->second, "0") << "没有任何种子的标记不该数出区域来，实测 segment_count=" << countIt->second;

    // 输出里不该冒出任何正标签。实测全 0 标记下 watershed 只留下 0（未知）与最外一圈 -1
    // （OpenCV 的边框规则：16x12 的边框正好 2*16 + 2*12 - 4 = 52 个像元）；
    // 这里只断"没有正标签"这个不变量，不把 -1 一圈的个数写进断言（那是 OpenCV 的实现细节）。
    auto ds = gis::core::openRaster(output, true);
    ASSERT_NE(ds, nullptr);
    auto* band = ds->GetRasterBand(1);
    ASSERT_NE(band, nullptr);
    std::vector<float> pixels(static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f);
    ASSERT_EQ(band->RasterIO(GF_Read, 0, 0, w, h, pixels.data(), w, h, GDT_Float32, 0, 0), CE_None);
    int positiveLabelPixels = 0;
    for (const float value : pixels) {
        if (value > 0.0f) ++positiveLabelPixels;
    }
    EXPECT_EQ(positiveLabelPixels, 0) << "无种子的标记里凭空出现了正标签像元，实测正标签像元数="
                                      << positiveLabelPixels;
}

// ── Task 16 Tier 3：外部标记与输入影像尺寸不符时必须返回失败，而不是未捕获异常 ────
// 改前形态（写清楚，别被读成"返回失败"，也别被读成"子进程崩掉"）：processing_plugin.cpp 零 try/catch，
// 尺寸不符撞上 OpenCV 内部的 CV_Assert(src.size() == dst.size())（segmentation.cpp:162）⇒ cv::Exception
// 穿透 execute()。测试进程里 gtest 默认会兜住测试体内的 C++ 异常，所以 ctest 记的是该用例 ***Failed
// 并打印 `unknown file: error: C++ exception with description "OpenCV(4.14.0) …(-215:Assertion failed)
// src.size() == dst.size() in function 'cv::watershed'" thrown in the test body`，
// 子进程并没有 SEH 崩溃（PRE_TEST 模式下每例一个进程，其它用例不受牵连）；
// 生产路径（GUI/CLI）没有 gtest 那层兜获，同一个异常就是真未处理 ⇒ 这才是本条守卫要挡的东西。
// 三个几何各挡一类假守卫（输入固定 32x24）：
//   16x16   = 宽高都不同 ⇒ 任何尺寸守卫都能挡，用来确认"守卫存在且会返回失败"；
//   24x32   = 输入的**转置**尺寸 ⇒ 只有把 cv::Size 的两个操作数按 (cols, rows) 写对才拦得住，专杀行列写反；
//   32x16   = 宽相同、高不同 ⇒ 拦住"只比 cols 一侧"的半守卫。
TEST_F(PluginTest, ProcessingWatershedRejectsMarkerInputWithMismatchedSize) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);

    const int w = 32, h = 24;
    std::vector<float> data(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            data[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                static_cast<float>((x * 2 + y) % 16);
        }
    }
    const std::string input = gis::tests::writeFloatRaster(getTestDir(), "ws_t16_bad_input.tif", w, h, data);

    auto makeMarker = [&](const char* fileName, int mw, int mh) {
        std::vector<float> values(static_cast<size_t>(mw) * static_cast<size_t>(mh), 1.0f);
        // 让标记里有真种子（标签 2），否则尺寸守卫就算被绕过、watershed 也只是拿到全背景。
        values[0] = 2.0f;
        return gis::tests::writeFloatRaster(getTestDir(), fileName, mw, mh, values);
    };

    auto run = [&](const std::string& markerPath, const std::string& outputName, const char* tag) {
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("watershed");
        params["input"] = input;
        params["output"] = utf8PathString(getTestDir() / outputName);
        params["marker_input"] = markerPath;
        params["band"] = 1;
        const auto res = p->execute(params, progress_);
        EXPECT_FALSE(res.success) << tag << " 的 marker_input 必须被拒绝，实际 message=" << res.message;
        return res.message;
    };

    const std::string msgSmall =
        run(makeMarker("ws_t16_bad_marker16.tif", 16, 16), "ws_t16_bad16_output.tif", "16x16");
    EXPECT_NE(msgSmall.find("does not match"), std::string::npos)
        << "尺寸守卫要回一句带两侧实际尺寸的说明，实测 message=" << msgSmall;

    const std::string msgTransposed =
        run(makeMarker("ws_t16_bad_marker24x32.tif", 24, 32), "ws_t16_bad24x32_output.tif", "24x32(转置)");
    EXPECT_NE(msgTransposed.find("does not match"), std::string::npos)
        << "输入 32x24、标记 24x32 只是行列互换，也必须判为尺寸不符" << msgTransposed;
    // 文案里两侧的实际尺寸要按 (宽x高) 各就各位。两个数字互为转置、必然同时出现，
    // 所以只判"包含"分不出两侧顺序 ⇒ 用它们在文案里的**相对位置**来判：marker 侧的数字在
    // "does not match" 之前、input 侧的在后。守卫判据本身的正确性由上面三条几何承担，这里只钉文案。
    const size_t phrasePos = msgTransposed.find("does not match");
    EXPECT_LT(msgTransposed.find("24x32"), phrasePos)
        << "marker_input 侧的 24x32 应出现在 'does not match' 之前，实测 message=" << msgTransposed;
    EXPECT_GT(msgTransposed.find("32x24"), phrasePos)
        << "input 侧的 32x24 应出现在 'does not match' 之后，实测 message=" << msgTransposed;

    // 宽相同、高不同：只比 cols 一侧的"半守卫"会在这里放行。
    const std::string msgSameWidth =
        run(makeMarker("ws_t16_bad_marker32x16.tif", 32, 16), "ws_t16_bad32x16_output.tif", "32x16(同宽)");
    EXPECT_NE(msgSameWidth.find("does not match"), std::string::npos)
        << "输入 32x24、标记 32x16 只有高度不同，也必须判为尺寸不符" << msgSameWidth;
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

// kmeans：不传 kmeans_k ⇒ 兜底必须与 paramSpecs() 的 kmeans_k 默认值同值。
// 改前实现是 getParam("kmeans_k", getParam("k", 5))（提交 54df42c:1400-1401）：
// 既回退到已废弃的键 "k"，又把 5 当默认，而 paramSpecs() 里 kmeans_k 那一条声明的是 4 ⇒ 本用例红。
TEST_F(PluginTest, ProcessingKMeansFallbackKMatchesParamSpec) {
    auto* p = mgr_.find("processing");
    ASSERT_NE(p, nullptr);
    const auto specs = p->paramSpecs();

    const auto* kSpec = findParamSpec(specs, "kmeans_k");
    ASSERT_NE(kSpec, nullptr) << "doKMeans 读的就是这个键，spec 被删掉时面板不再渲染它";
    EXPECT_EQ(kSpec->type, gis::framework::ParamType::Int)
        << "类型漂移成 Double/String 后 getParam<int> 会静默回落兜底值，取值断言照样绿";
    const std::string expectedK = specDefaultAsMetadataString(*kSpec);
    ASSERT_FALSE(expectedK.empty()) << "kmeans_k 的 spec 默认值必须是 int 变体";

    // 用 3 波段栅格（与 ProcessingKMeansExecution 同一夹具形态），且不传 kmeans_k / k。
    const std::string input = createTestRaster("e2e_kmeans_k_lock_input.tif", 30, 30, 3);
    const std::string output = utf8PathString(getTestDir() / "e2e_kmeans_k_lock_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("kmeans");
    params["input"] = input;
    params["output"] = output;
    params["kmeans_attempts"] = 3;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.metadata.at("k"), expectedK)
        << "不传 kmeans_k 时的兜底与 paramSpecs() 的默认值分叉（改前还会回退到已废弃键 k）";
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

// ── N129：dissolve_field 写错 ⇒ 硬失败（旧实现不报错，静默交出一张 0 要素的空图层）────
// WHY 夹具与 VectorDissolveExecution 同形（只建 `type` 字段、两条共边面都写 type='A'）：
// 反向臂要的"写错"必须**只少一个字母**（`typ`），这样红点只能来自字段存在性校验，
// 而不是"整个字段不存在"这种连旧实现都可能碰巧走对的情形。
// 旧实现的实际症状（现读，写下来免得下一个人在这里再猜一遍）：fieldIdx<0 时要素被推进
// allGeoms，但写出段走的却是 `!dissolveField.empty()` 那条分支、遍历的是空的 groups ⇒
// 一个要素也不写，返回消息还是 "Dissolve completed: 0 features"（success=true）。
// WHY 自己清同名一套副本：shapefile 是 .shp/.shx/.dbf/.prj 一套，上一轮的残留会让
// "输出不得存在"这条判据拿到假绿。
static std::string buildDissolveTypeFixture(const std::string& fileName) {
    const std::string shpPath = (getTestDir() / fileName).string();
    const fs::path stem = fs::path(shpPath).stem();
    for (const auto& entry : fs::directory_iterator(getTestDir())) {
        if (entry.path().stem() == stem) {
            std::error_code ignored;
            fs::remove(entry.path(), ignored);
        }
    }

    auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
    EXPECT_NE(driver, nullptr);
    auto* ds = driver->Create(shpPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    EXPECT_NE(ds, nullptr);
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);
    auto* layer = ds->CreateLayer("test", srs.get(), wkbPolygon);
    EXPECT_NE(layer, nullptr);
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
    return shpPath;
}

// 反向臂：字段名少一个字母 ⇒ 必须硬失败，且失败点要能被 message 认出来。
// 三条断言各自钉一件事，缺一条就退化成"任何错误都算过"的空转用例：
//   ① !success（旧实现这条是绿的 ⇒ 这就是改前必红的红点）
//   ② message 点名参数 dissolve_field
//   ③ message 带回写错的值 typ（用户据此知道是自己敲错了什么）
//   ④ 输出文件不得存在（硬失败必须发生在建盘之前，不许留半成品）
TEST_F(PluginTest, VectorDissolveRejectsUnknownDissolveField) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string shpPath = buildDissolveTypeFixture("e2e_dissolve_typo_input.shp");
    const std::string output = (getTestDir() / "e2e_dissolve_typo_output.geojson").string();
    std::error_code ignored;
    fs::remove(output, ignored);
    ASSERT_FALSE(fs::exists(output)) << "本轮开始前就残留了输出，半成品判据会失真";

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("dissolve");
    params["input"] = shpPath;
    params["output"] = output;
    params["dissolve_field"] = std::string("typ");   // 夹具里的真实字段是 type

    const auto result = p->execute(params, progress_);

    EXPECT_FALSE(result.success)
        << "N129：dissolve_field 写成 typ 仍然 success=true ⇒ 旧实现一个要素也不写、"
           "静默交出一张空图层（message=" << result.message << "）";
    EXPECT_NE(result.message.find("dissolve_field not found"), std::string::npos)
        << "失败消息必须点名是哪个参数（与 spatial_join 的 join_field not found 同族口径），"
           "只回 success=false 不算修好。实际 message=" << result.message;
    EXPECT_NE(result.message.find("typ"), std::string::npos)
        << "失败消息必须把写错的那个值带回来。实际 message=" << result.message;
    ASSERT_FALSE(fs::exists(output))
        << "字段校验要在建出输出文件之前完成，失败后不得留半成品：" << output;
}

// 正向对照臂：dissolve_field 留空是 GUI 承诺过的合法用法（"该字段留空时全部合并为一个要素"，
// src/gui/gui_data_support.cpp:527），修 N129 时不得把它一起拒掉。
// 两条共边面 ⇒ 整体融合后恰好 1 个要素。
TEST_F(PluginTest, VectorDissolveWithoutFieldMergesAllFeatures) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string shpPath = buildDissolveTypeFixture("e2e_dissolve_emptyfield_input.shp");
    const std::string output = (getTestDir() / "e2e_dissolve_emptyfield_output.geojson").string();
    std::error_code ignored;
    fs::remove(output, ignored);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("dissolve");
    params["input"] = shpPath;
    params["output"] = output;
    // 不写 params["dissolve_field"]：走"留空 = 全部合并"这条合法路径

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << "留空字段的合法路径被 N129 的校验顺手拒了: " << result.message;

    const auto it = result.metadata.find("feature_count");
    ASSERT_NE(it, result.metadata.end())
        << "dissolve 必须回显写出要素数（本刀同时把这条键补进 dissolve，与同插件其它动作同形）；"
           "实际键数=" << result.metadata.size();
    EXPECT_EQ(it->second, "1") << "两条共边面整体融合后必须是 1 个要素";
    EXPECT_TRUE(fs::exists(output)) << "成功臂必须真交出产物";
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

// ── 以下四条判 union 路径 A 的"真并集 + 整表属性"（销账 N128）──────────────────────────
// 夹具统一 GPKG + 两边 EPSG:3857（areSameCrs 会拒不一致；不设 SRS 今天也能过路径 A，
// 但本刀的夹具必须带 SRS，免得 CRS 闸被顺带绕过）。
// WHY 每条都必须钉包串四个角点：面积、周长在 x↔y 转置下是不变量，只判面积抓不到坐标转置
//（批次 C 已踩过一次），所以"某分块的 (MinX,MaxX,MinY,MaxY) == 手算常量"这条断言不许省。

// union 夹具的一个要素：轴对齐矩形（可选再挖一个轴对齐的洞）+ 三个可选字段。
struct UnionFixtureFeature {
    double minX = 0.0;
    double minY = 0.0;
    double maxX = 0.0;
    double maxY = 0.0;
    std::string name;    // 非空 ⇒ 该图层建 OFTString 字段 name 并写值
    long long pop = -1;  // 非负 ⇒ 该图层建 OFTInteger 字段 pop 并写值
    std::string source;  // 非空 ⇒ 建 OFTString 字段 source：这是**用户数据列**，与标记列同名，专打让位规则
    bool hasHole = false;  // true ⇒ 外环里再挂一条内环（洞）；带洞面与源面相交会切成 MultiPolygon
    double holeMinX = 0.0;
    double holeMinY = 0.0;
    double holeMaxX = 0.0;
    double holeMaxY = 0.0;
};

// 写"矩形面（可带洞）+ name/pop/source 可选字段"的 GPKG 图层当 union 夹具。
// WHY 字段要可选：新语义要判"另一侧字段必须 IS NULL"，两侧各给一半字段才判得住；
// T3 又要求两边都有同名 name 才能钉改名规则 ⇒ 字段名单独可控；
// T6 要求**源侧字段就叫 source** 才判得住"数据列名逐字保留、让位的是标记列"。
// WHY 洞要能用 OGR C++ API 直接挂：带洞面是 T5 唯一能让分块真的出现 MultiPolygon 的夹具；
// 手写 GeoJSON 文本喂驱动会被本机 GDAL 的 reader 报 "too many members in array" 读坏环，夹具数值不可信。
// 没复用 writeTwoPolygonFixture：那份夹具是两条面的固定形状 + 固定单字段，撑不起上面这几种断言。
static bool writeUnionPolygonLayer(const fs::path& path,
                                   const std::vector<UnionFixtureFeature>& features) {
    auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!driver) {
        return false;
    }
    const std::string pathText = utf8PathString(path);
    GDALDataset* ds = driver->Create(pathText.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) {
        return false;
    }
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);
    OGRLayer* layer = ds->CreateLayer("shapes", srs.get(), wkbPolygon, nullptr);
    if (!layer) {
        GDALClose(ds);
        return false;
    }

    bool hasName = false;
    bool hasPop = false;
    bool hasSource = false;
    for (const auto& feature : features) {
        hasName = hasName || !feature.name.empty();
        hasPop = hasPop || feature.pop >= 0;
        hasSource = hasSource || !feature.source.empty();
    }

    bool ok = true;
    if (hasName) {
        OGRFieldDefn nameDefn("name", OFTString);
        ok = layer->CreateField(&nameDefn) == OGRERR_NONE;
    }
    if (hasPop && ok) {
        OGRFieldDefn popDefn("pop", OFTInteger);
        ok = layer->CreateField(&popDefn) == OGRERR_NONE;
    }
    if (hasSource && ok) {
        OGRFieldDefn sourceDefn("source", OFTString);
        ok = layer->CreateField(&sourceDefn) == OGRERR_NONE;
    }

    for (const auto& feature : features) {
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(feature.minX, feature.minY);
        ring.addPoint(feature.maxX, feature.minY);
        ring.addPoint(feature.maxX, feature.maxY);
        ring.addPoint(feature.minX, feature.maxY);
        ring.addPoint(feature.minX, feature.minY);   // 闭合
        poly.addRing(&ring);
        // 内环（洞）按顺时针挂，与外环反向：GDAL/GPKG 不强制方向，但这样喂给 GEOS 的几何
        // 不需要 MakeValid 兜手，夹具面积就是"外环面积 − 内环面积"这个手算值。
        if (feature.hasHole) {
            OGRLinearRing hole;
            hole.addPoint(feature.holeMinX, feature.holeMinY);
            hole.addPoint(feature.holeMinX, feature.holeMaxY);
            hole.addPoint(feature.holeMaxX, feature.holeMaxY);
            hole.addPoint(feature.holeMaxX, feature.holeMinY);
            hole.addPoint(feature.holeMinX, feature.holeMinY);   // 闭合
            poly.addRing(&hole);
        }

        OGRFeature* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetGeometry(&poly);
        if (hasName && !feature.name.empty()) {
            feat->SetField("name", feature.name.c_str());
        }
        if (hasPop && feature.pop >= 0) {
            feat->SetField("pop", static_cast<int>(feature.pop));
        }
        if (hasSource && !feature.source.empty()) {
            feat->SetField("source", feature.source.c_str());
        }
        ok = ok && layer->CreateFeature(feat) == OGRERR_NONE;
        OGRFeature::DestroyFeature(feat);
    }

    GDALClose(ds);
    return ok;
}

// union 路径 B（线 × 面）的源侧夹具：一条水平线 + 可选 OFTString 字段 name，GPKG + EPSG:3857。
// WHY 不复用 writeUnionPolygonLayer：那一份写的是**面**，路径 B 的输入必须是线才会走切分分支；
// WHY 字段仍要可选：这一路要判"源侧整表属性逐条跟着片段走"，name 就是那条判据的载体。
struct UnionFixtureLine {
    double x0 = 0.0;
    double x1 = 0.0;
    double y = 0.0;
    std::string name;   // 非空 ⇒ 建 OFTString 字段 name 并写值
};

static bool writeUnionLineLayer(const fs::path& path, const std::vector<UnionFixtureLine>& lines) {
    auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!driver) {
        return false;
    }
    const std::string pathText = utf8PathString(path);
    GDALDataset* ds = driver->Create(pathText.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) {
        return false;
    }
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);
    OGRLayer* layer = ds->CreateLayer("shapes", srs.get(), wkbLineString, nullptr);
    if (!layer) {
        GDALClose(ds);
        return false;
    }

    bool hasName = false;
    for (const auto& line : lines) {
        hasName = hasName || !line.name.empty();
    }
    bool ok = true;
    if (hasName) {
        OGRFieldDefn nameDefn("name", OFTString);
        ok = layer->CreateField(&nameDefn) == OGRERR_NONE;
    }

    for (const auto& line : lines) {
        OGRLineString geom;
        geom.addPoint(line.x0, line.y);
        geom.addPoint(line.x1, line.y);
        OGRFeature* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetGeometry(&geom);
        if (hasName && !line.name.empty()) {
            feat->SetField("name", line.name.c_str());
        }
        ok = ok && layer->CreateFeature(feat) == OGRERR_NONE;
        OGRFeature::DestroyFeature(feat);
    }

    GDALClose(ds);
    return ok;
}

// 输出分块的一行读数：source 值 + 几何类型 + 面积 + 长度 + 部件/环数 + 包串四角 + 每个字段"有值/NULL"。
struct UnionOutputPiece {
    std::string source;
    OGRwkbGeometryType geomType = wkbUnknown;
    double area = 0.0;
    double length = 0.0;   // 线 × 面路径的"不重不漏"不变量按长度判，面积在那一路没有意义
    int partCount = 0;   // 多面 ⇒ 部件数；单面 ⇒ 环数（外环 + 内环）；其余类族为 0
    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    std::map<std::string, std::string> values;   // 已赋值字段：字段名 → GetFieldAsString
    std::set<std::string> nullFields;            // IsFieldSetAndNotNull()==false 的字段名
};

static std::vector<UnionOutputPiece> readUnionPieces(OGRLayer* layer) {
    std::vector<UnionOutputPiece> pieces;
    layer->ResetReading();
    OGRFeature* feat = nullptr;
    while ((feat = layer->GetNextFeature()) != nullptr) {
        UnionOutputPiece piece;
        OGRGeometry* geom = feat->GetGeometryRef();
        if (geom) {
            piece.geomType = wkbFlatten(geom->getGeometryType());
            // 面积读数：**先判实际类族再读**。get_Area() 只挂在 OGRSurface 与 OGRGeometryCollection 上
            //（OGRGeometry 基类没有这份成员，拿基类指针直接求面积编译不过），
            // 而 OGRPolygon→OGRCurvePolygon→OGRSurface、OGRMultiPolygon→OGRMultiSurface→
            // OGRGeometryCollection 两条链互斥 ⇒ dynamic_cast 二分是干净的。
            // WHY 不用 toSurface()/toMultiSurface()：它们是 cpl::down_cast，**不判类型、永不为 nullptr**，
            // "if (toSurface()) … else if (toMultiSurface()) …" 的 else 分支恒不可达；
            // 对多面调 toSurface() 是非法下转（走错 vtable 槽）。
            if (const OGRSurface* surface = dynamic_cast<const OGRSurface*>(geom)) {
                piece.area = surface->get_Area();
                if (const OGRPolygon* polygon = dynamic_cast<const OGRPolygon*>(geom)) {
                    piece.partCount = (polygon->getExteriorRing() ? 1 : 0) + polygon->getNumInteriorRings();
                }
            } else if (const OGRGeometryCollection* collection =
                           dynamic_cast<const OGRGeometryCollection*>(geom)) {
                piece.area = collection->get_Area();
                piece.partCount = collection->getNumGeometries();
            }
            // 长度读数的类族判据与面积同一条理由：get_Length() 只挂在 OGRCurve（LineString 那一支）
            // 与 OGRGeometryCollection（MultiLineString 那一支，线 × 面路径写出的就是这一支）上，
            // OGRGeometry 基类没有这份成员。
            if (const OGRCurve* curve = dynamic_cast<const OGRCurve*>(geom)) {
                piece.length = curve->get_Length();
            } else if (const OGRGeometryCollection* collection =
                           dynamic_cast<const OGRGeometryCollection*>(geom)) {
                piece.length = collection->get_Length();
            }
            OGREnvelope env;
            geom->getEnvelope(&env);
            piece.minX = env.MinX;
            piece.maxX = env.MaxX;
            piece.minY = env.MinY;
            piece.maxY = env.MaxY;
        }
        const OGRFeatureDefn* defn = feat->GetDefnRef();
        for (int i = 0; i < defn->GetFieldCount(); ++i) {
            const std::string fieldName = defn->GetFieldDefn(i)->GetNameRef();
            if (feat->IsFieldSetAndNotNull(i)) {
                piece.values[fieldName] = feat->GetFieldAsString(i);
            } else {
                piece.nullFields.insert(fieldName);
            }
        }
        const auto sourceIt = piece.values.find("source");
        piece.source = sourceIt == piece.values.end() ? std::string() : sourceIt->second;
        pieces.push_back(piece);
        OGRFeature::DestroyFeature(feat);
    }
    return pieces;
}

// 字段读数：缺列返回 "<missing>"、IS NULL 返回 "<null>"，其余给文本值。
// WHY 不用 .at()：缺列时 .at() 抛异常会连带把同一条用例后面的断言一起跳过。
static std::string unionFieldValue(const UnionOutputPiece& piece, const char* field) {
    const auto valueIt = piece.values.find(field);
    if (valueIt != piece.values.end()) {
        return valueIt->second;
    }
    return piece.nullFields.count(field) != 0 ? "<null>" : "<missing>";
}

// 按"标记列的值 + 包串四角"定位分块，返回下标（找不到给 -1）。
// WHY 标记列名要能换：T6 里数据列自己就叫 source，标记列按让位规则变成 source_2 ⇒
// 用例必须按**列名**去读标记，不许硬写"第 3 列"，否则驱动改了列序时红的是"取错列"而不是"名字不对"。
static int findUnionPieceMarkedBy(const std::vector<UnionOutputPiece>& pieces, const char* markerField,
                                  const std::string& markerValue,
                                  double minX, double maxX, double minY, double maxY) {
    for (size_t i = 0; i < pieces.size(); ++i) {
        const auto& piece = pieces[i];
        if (unionFieldValue(piece, markerField) == markerValue &&
            std::fabs(piece.minX - minX) <= 1e-6 && std::fabs(piece.maxX - maxX) <= 1e-6 &&
            std::fabs(piece.minY - minY) <= 1e-6 && std::fabs(piece.maxY - maxY) <= 1e-6) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// 按"source 值 + 包串四角"定位分块，返回下标（找不到给 -1）。
// WHY 不按下标取：实现写块的顺序不是输出契约，但每块的 source 值与包串是手算常量 ⇒
// 配对既判得住"name↔几何"配错，也判得住 x↔y 转置（面积相同、包串不同）。
static int findUnionPiece(const std::vector<UnionOutputPiece>& pieces, const std::string& sourceValue,
                          double minX, double maxX, double minY, double maxY) {
    return findUnionPieceMarkedBy(pieces, "source", sourceValue, minX, maxX, minY, maxY);
}

static std::string unionMetadataValue(const gis::framework::Result& result, const char* key) {
    const auto it = result.metadata.find(key);
    return it == result.metadata.end() ? std::string("<missing>") : it->second;
}

static std::vector<std::string> readFieldNames(OGRLayer* layer) {
    std::vector<std::string> names;
    const OGRFeatureDefn* defn = layer->GetLayerDefn();
    for (int i = 0; i < defn->GetFieldCount(); ++i) {
        names.push_back(defn->GetFieldDefn(i)->GetNameRef());
    }
    return names;
}

// T1：叠加图层里不与任何源要素相交的那一整块，旧实现整块丢失 ⇒ 本条专打"输出不是并集"。
// 手算：A=[0,10]×[0,4] ⇒ 面积 10×4=40，包串 (0,10,0,4)；
//       B=[20,30]×[2,12] ⇒ 面积 10×10=100，包串 (20,30,2,12)；x 方向 [20,30] 与 [0,10] 不相交 ⇒ 交集 0。
//       期望恰好 2 条：input 40 + overlay 100，和为 140 = 40+100-0 = 并集面积。
TEST_F(PluginTest, VectorUnionEmitsOverlayOnlyPiece) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const fs::path inputPath = getTestDir() / "e2e_union_true_disjoint_input.gpkg";
    const fs::path overlayPath = getTestDir() / "e2e_union_true_disjoint_overlay.gpkg";
    const std::string output = utf8PathString(getTestDir() / "e2e_union_true_disjoint_output.gpkg");

    UnionFixtureFeature srcA;
    srcA.minX = 0.0; srcA.minY = 0.0; srcA.maxX = 10.0; srcA.maxY = 4.0; srcA.name = "src1";
    ASSERT_TRUE(writeUnionPolygonLayer(inputPath, {srcA}));

    UnionFixtureFeature ovlB;
    ovlB.minX = 20.0; ovlB.minY = 2.0; ovlB.maxX = 30.0; ovlB.maxY = 12.0; ovlB.pop = 7;
    ASSERT_TRUE(writeUnionPolygonLayer(overlayPath, {ovlB}));

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("union");
    params["input"] = utf8PathString(inputPath);
    params["overlay_vector"] = utf8PathString(overlayPath);
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.message, "Union completed: 2 features");

    GDALDataset* rawOutDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(rawOutDs, nullptr);
    gis::core::GdalDatasetPtr outDs(rawOutDs);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    const auto pieces = readUnionPieces(outLayer);

    EXPECT_EQ(pieces.size(), 2u) << "真并集必须把叠加独有的一整块也写出来";
    EXPECT_EQ(unionMetadataValue(result, "union_input_pieces"), "1");
    EXPECT_EQ(unionMetadataValue(result, "union_overlap_pieces"), "0");
    EXPECT_EQ(unionMetadataValue(result, "union_overlay_pieces"), "1");
    EXPECT_EQ(unionMetadataValue(result, "union_total_pieces"), "2");

    // input 块：源几何原样 + 叠加侧字段全 NULL。
    const int inputIdx = findUnionPiece(pieces, "input", 0.0, 10.0, 0.0, 4.0);
    ASSERT_GE(inputIdx, 0) << "缺少 source=input 且包串 (0,10,0,4) 的分块";
    EXPECT_NEAR(pieces[inputIdx].area, 40.0, 1e-6);
    EXPECT_EQ(unionFieldValue(pieces[inputIdx], "name"), "src1");
    EXPECT_EQ(unionFieldValue(pieces[inputIdx], "pop"), "<null>");

    // overlay 块：叠加几何原样 + 源侧字段全 NULL（旧实现根本没有这一条）。
    const int overlayIdx = findUnionPiece(pieces, "overlay", 20.0, 30.0, 2.0, 12.0);
    ASSERT_GE(overlayIdx, 0) << "缺少 source=overlay 且包串 (20,30,2,12) 的分块";
    EXPECT_NEAR(pieces[overlayIdx].area, 100.0, 1e-6);
    EXPECT_EQ(unionFieldValue(pieces[overlayIdx], "name"), "<null>");
    EXPECT_EQ(unionFieldValue(pieces[overlayIdx], "pop"), "7");

    double totalArea = 0.0;
    for (const auto& piece : pieces) {
        totalArea += piece.area;
    }
    EXPECT_NEAR(totalArea, 140.0, 1e-6) << "分块面积之和必须等于并集面积 40+100";
}

// T2：统一坐标下的三条分块（源独有 / 相交 / 叠加独有）+ 两侧属性。
// 手算：A=[0,10]×[0,4] 面积 10×4=40；B=[6,16]×[2,12] 面积 10×10=100；
//       交：x 方向 [6,10]=4、y 方向 [2,4]=2 ⇒ A∩B=[6,10]×[2,4] 面积 4×2=8，包串 (6,10,2,4)；
//       input-only = 40-8 = 32（L 形，包串 (0,10,0,4)）；overlay-only = 100-8 = 92（包串 (6,16,2,12)）；
//       校验和 32+8+92 = 132 = 40+100-8 = 并集面积。
// 旧实现只写 1 条"union"合并几何且不带任何属性 ⇒ 条数、面积、属性、计数一起红。
TEST_F(PluginTest, VectorUnionSplitsIntoThreePieceClasses) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const fs::path inputPath = getTestDir() / "e2e_union_true_overlap_input.gpkg";
    const fs::path overlayPath = getTestDir() / "e2e_union_true_overlap_overlay.gpkg";
    const std::string output = utf8PathString(getTestDir() / "e2e_union_true_overlap_output.gpkg");

    UnionFixtureFeature srcA;
    srcA.minX = 0.0; srcA.minY = 0.0; srcA.maxX = 10.0; srcA.maxY = 4.0; srcA.name = "src1";
    ASSERT_TRUE(writeUnionPolygonLayer(inputPath, {srcA}));

    UnionFixtureFeature ovlB;
    ovlB.minX = 6.0; ovlB.minY = 2.0; ovlB.maxX = 16.0; ovlB.maxY = 12.0; ovlB.pop = 7;
    ASSERT_TRUE(writeUnionPolygonLayer(overlayPath, {ovlB}));

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("union");
    params["input"] = utf8PathString(inputPath);
    params["overlay_vector"] = utf8PathString(overlayPath);
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* rawOutDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(rawOutDs, nullptr);
    gis::core::GdalDatasetPtr outDs(rawOutDs);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    const auto pieces = readUnionPieces(outLayer);

    EXPECT_EQ(pieces.size(), 3u) << "三条分块（input/both/overlay）必须各写一条";
    EXPECT_EQ(unionMetadataValue(result, "union_input_pieces"), "1");
    EXPECT_EQ(unionMetadataValue(result, "union_overlap_pieces"), "1");
    EXPECT_EQ(unionMetadataValue(result, "union_overlay_pieces"), "1");
    EXPECT_EQ(unionMetadataValue(result, "union_total_pieces"), "3");

    const int inputIdx = findUnionPiece(pieces, "input", 0.0, 10.0, 0.0, 4.0);
    ASSERT_GE(inputIdx, 0) << "缺少 source=input 且包串 (0,10,0,4) 的分块";
    EXPECT_NEAR(pieces[inputIdx].area, 32.0, 1e-6);
    EXPECT_EQ(unionFieldValue(pieces[inputIdx], "name"), "src1");
    EXPECT_EQ(unionFieldValue(pieces[inputIdx], "pop"), "<null>");

    // both 块：包串必须等于 A∩B 的 (6,10,2,4)（钉坐标，转置类缺陷在这一条上必红）。
    const int bothIdx = findUnionPiece(pieces, "both", 6.0, 10.0, 2.0, 4.0);
    ASSERT_GE(bothIdx, 0) << "缺少 source=both 且包串 (6,10,2,4) 的分块";
    EXPECT_NEAR(pieces[bothIdx].area, 8.0, 1e-6);
    EXPECT_EQ(unionFieldValue(pieces[bothIdx], "name"), "src1");
    EXPECT_EQ(unionFieldValue(pieces[bothIdx], "pop"), "7");

    const int overlayIdx = findUnionPiece(pieces, "overlay", 6.0, 16.0, 2.0, 12.0);
    ASSERT_GE(overlayIdx, 0) << "缺少 source=overlay 且包串 (6,16,2,12) 的分块";
    EXPECT_NEAR(pieces[overlayIdx].area, 92.0, 1e-6);
    EXPECT_EQ(unionFieldValue(pieces[overlayIdx], "name"), "<null>");
    EXPECT_EQ(unionFieldValue(pieces[overlayIdx], "pop"), "7");

    double totalArea = 0.0;
    for (const auto& piece : pieces) {
        totalArea += piece.area;
    }
    EXPECT_NEAR(totalArea, 132.0, 1e-6) << "32+8+92 必须等于并集面积 40+100-8";
}

// T3：两侧同名字段的改名规则钉死（源字段原序在前、叠加字段改名在后、末尾固定 source）。
// 统一坐标同 T2，只是叠加侧字段也叫 name（源 "src1"、叠加 "ovl1"）。
// 期望输出表字段名依次为 name、name_2、source —— 旧实现只建 source 一列 ⇒ 列名逐个红。
TEST_F(PluginTest, VectorUnionRenamesOverlayFieldOnNameCollision) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const fs::path inputPath = getTestDir() / "e2e_union_true_rename_input.gpkg";
    const fs::path overlayPath = getTestDir() / "e2e_union_true_rename_overlay.gpkg";
    const std::string output = utf8PathString(getTestDir() / "e2e_union_true_rename_output.gpkg");

    UnionFixtureFeature srcA;
    srcA.minX = 0.0; srcA.minY = 0.0; srcA.maxX = 10.0; srcA.maxY = 4.0; srcA.name = "src1";
    ASSERT_TRUE(writeUnionPolygonLayer(inputPath, {srcA}));

    UnionFixtureFeature ovlB;
    ovlB.minX = 6.0; ovlB.minY = 2.0; ovlB.maxX = 16.0; ovlB.maxY = 12.0; ovlB.name = "ovl1";
    ASSERT_TRUE(writeUnionPolygonLayer(overlayPath, {ovlB}));

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("union");
    params["input"] = utf8PathString(inputPath);
    params["overlay_vector"] = utf8PathString(overlayPath);
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* rawOutDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(rawOutDs, nullptr);
    gis::core::GdalDatasetPtr outDs(rawOutDs);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    const auto fieldNames = readFieldNames(outLayer);
    const auto pieces = readUnionPieces(outLayer);

    // 改名规则：按下标逐个比名字（只判"存在某列"判不住源在前、叠加在后的顺序）。
    ASSERT_EQ(fieldNames.size(), 3u) << "字段名实际为 " << fieldNames.size() << " 列";
    EXPECT_EQ(fieldNames[0], "name");
    EXPECT_EQ(fieldNames[1], "name_2");
    EXPECT_EQ(fieldNames[2], "source");

    EXPECT_EQ(pieces.size(), 3u);

    // both 块：两侧同名字段必须各归一列（先 SetFrom 源再 SetFrom 叠加的写法会把 name 覆盖成 ovl1）。
    const int bothIdx = findUnionPiece(pieces, "both", 6.0, 10.0, 2.0, 4.0);
    ASSERT_GE(bothIdx, 0) << "缺少 source=both 且包串 (6,10,2,4) 的分块";
    EXPECT_EQ(unionFieldValue(pieces[bothIdx], "name"), "src1");
    EXPECT_EQ(unionFieldValue(pieces[bothIdx], "name_2"), "ovl1");

    const int inputIdx = findUnionPiece(pieces, "input", 0.0, 10.0, 0.0, 4.0);
    ASSERT_GE(inputIdx, 0) << "缺少 source=input 且包串 (0,10,0,4) 的分块";
    EXPECT_EQ(unionFieldValue(pieces[inputIdx], "name"), "src1");
    EXPECT_EQ(unionFieldValue(pieces[inputIdx], "name_2"), "<null>");

    const int overlayIdx = findUnionPiece(pieces, "overlay", 6.0, 16.0, 2.0, 12.0);
    ASSERT_GE(overlayIdx, 0) << "缺少 source=overlay 且包串 (6,16,2,12) 的分块";
    EXPECT_EQ(unionFieldValue(pieces[overlayIdx], "name"), "<null>");
    EXPECT_EQ(unionFieldValue(pieces[overlayIdx], "name_2"), "ovl1");
}

// T4：叠加面互相重叠时的去重（本刀最容易被漏掉的正确性点：不减已写面积）。
// 手算：A=[0,10]×[0,10]=100；B1=[5,15]×[0,10]=100；B2=[8,18]×[0,10]=100（B1∩B2=[8,15]×[0,10]=70）。
//   候选顺序 B1 先、B2 后：
//     A∩B1 = [5,10]×[0,10] = 50 ⇒ 写 both，并进 consumed；
//     A∩B2 = [8,10]×[0,10] = 20 整块落在 consumed 里 ⇒ 减完为空 ⇒ 不产出；
//     rest = A − (B1∪B2) = [0,5]×[0,10] = 50 ⇒ 写 input；
//     B1 − A = [10,15]×[0,10] = 50 ⇒ 写 overlay；
//     B2 − A = [10,18]×[0,10] = 80，再减已写的 B1−A ⇒ [15,18]×[0,10] = 30 ⇒ 写 overlay。
//   面积和 50+50+50+30 = 180 = 全并集 [0,18]×[0,10]。
//   不减 consumed 的话面积和是 50+50+20+50+80 = 250 ≠ 180 ⇒ 本用例把减项和"A∩B2 不产出"钉死。
TEST_F(PluginTest, VectorUnionDeduplicatesOverlappingOverlayPolygons) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const fs::path inputPath = getTestDir() / "e2e_union_true_dedup_input.gpkg";
    const fs::path overlayPath = getTestDir() / "e2e_union_true_dedup_overlay.gpkg";
    const std::string output = utf8PathString(getTestDir() / "e2e_union_true_dedup_output.gpkg");

    UnionFixtureFeature srcA;
    srcA.minX = 0.0; srcA.minY = 0.0; srcA.maxX = 10.0; srcA.maxY = 10.0; srcA.name = "src1";
    ASSERT_TRUE(writeUnionPolygonLayer(inputPath, {srcA}));

    UnionFixtureFeature ovlB1;
    ovlB1.minX = 5.0; ovlB1.minY = 0.0; ovlB1.maxX = 15.0; ovlB1.maxY = 10.0; ovlB1.pop = 1;
    UnionFixtureFeature ovlB2;
    ovlB2.minX = 8.0; ovlB2.minY = 0.0; ovlB2.maxX = 18.0; ovlB2.maxY = 10.0; ovlB2.pop = 2;
    // 写入顺序 B1 先、B2 后：GPKG 按 rowid 顺序读出，候选顺序才是上面算式里的那个顺序。
    ASSERT_TRUE(writeUnionPolygonLayer(overlayPath, {ovlB1, ovlB2}));

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("union");
    params["input"] = utf8PathString(inputPath);
    params["overlay_vector"] = utf8PathString(overlayPath);
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* rawOutDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(rawOutDs, nullptr);
    gis::core::GdalDatasetPtr outDs(rawOutDs);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    const auto pieces = readUnionPieces(outLayer);

    EXPECT_EQ(pieces.size(), 4u) << "A∩B2 应当被 consumed 减干净、一条都不产出";
    EXPECT_EQ(unionMetadataValue(result, "union_input_pieces"), "1");
    EXPECT_EQ(unionMetadataValue(result, "union_overlap_pieces"), "1");
    EXPECT_EQ(unionMetadataValue(result, "union_overlay_pieces"), "2");
    EXPECT_EQ(unionMetadataValue(result, "union_total_pieces"), "4");

    const int inputIdx = findUnionPiece(pieces, "input", 0.0, 5.0, 0.0, 10.0);
    ASSERT_GE(inputIdx, 0) << "缺少 source=input 且包串 (0,5,0,10) 的分块";
    EXPECT_NEAR(pieces[inputIdx].area, 50.0, 1e-6);
    EXPECT_EQ(unionFieldValue(pieces[inputIdx], "name"), "src1");
    EXPECT_EQ(unionFieldValue(pieces[inputIdx], "pop"), "<null>");

    const int bothIdx = findUnionPiece(pieces, "both", 5.0, 10.0, 0.0, 10.0);
    ASSERT_GE(bothIdx, 0) << "缺少 source=both 且包串 (5,10,0,10) 的分块";
    EXPECT_NEAR(pieces[bothIdx].area, 50.0, 1e-6);
    // 相交块带的是"这一条叠加要素"的属性（pop=1 来自 B1，不是 B2 的 2）。
    EXPECT_EQ(unionFieldValue(pieces[bothIdx], "name"), "src1");
    EXPECT_EQ(unionFieldValue(pieces[bothIdx], "pop"), "1");

    const int firstOverlayIdx = findUnionPiece(pieces, "overlay", 10.0, 15.0, 0.0, 10.0);
    ASSERT_GE(firstOverlayIdx, 0) << "缺少 source=overlay 且包串 (10,15,0,10) 的分块";
    EXPECT_NEAR(pieces[firstOverlayIdx].area, 50.0, 1e-6);
    EXPECT_EQ(unionFieldValue(pieces[firstOverlayIdx], "name"), "<null>");
    EXPECT_EQ(unionFieldValue(pieces[firstOverlayIdx], "pop"), "1");

    // 第二条叠加要素只剩没被写过的那 30：包串 (15,18,0,10)、属性来自 B2（pop=2）。
    const int secondOverlayIdx = findUnionPiece(pieces, "overlay", 15.0, 18.0, 0.0, 10.0);
    ASSERT_GE(secondOverlayIdx, 0) << "缺少 source=overlay 且包串 (15,18,0,10) 的分块（不减已写面积就会少了这一块）";
    EXPECT_NEAR(pieces[secondOverlayIdx].area, 30.0, 1e-6);
    EXPECT_EQ(unionFieldValue(pieces[secondOverlayIdx], "pop"), "2");

    double totalArea = 0.0;
    for (const auto& piece : pieces) {
        totalArea += piece.area;
    }
    EXPECT_NEAR(totalArea, 180.0, 1e-6) << "全并集 [0,18]×[0,10] = 180，重复写一次就变 200/250";
}

// T5：叠加面**带洞** ⇒ A∩C 天然被切成两块，写出的是真正的 MultiPolygon。
// 本条是"分块面积先判类族再读"（readUnionPieces 里那处 dynamic_cast）的唯一判据 ——
// T1~T4 的分块全是单面，走不到 OGRGeometryCollection 那一支。
// 夹具（几何一律用 OGR C++ API 现造：外环 + 内环都走 OGRPolygon::addRing，不喂手写 GeoJSON 文本）：
//   源   A = [0,10]×[0,10] ⇒ 面积 10×10 = 100，字段 name="src1"
//   叠加 C = 外环 [-5,15]×[-5,15]（20×20 = 400）挖掉内环（洞）[2,8]×[-1,11]（6×12 = 72）
//          ⇒ 面积 400-72 = 328，字段 pop=9
// 三条分块（类型/面积/包串/环数与控制器本机 ogr2ogr -dialect SQLITE 的 ST_Intersection/ST_Difference 实测一致）：
//   both    = A∩C：洞的 y 范围 [-1,11] 完整盖住 A 的 [0,10] ⇒ A∩洞 = [2,8]×[0,10] = 60 被挖掉，
//             A 剩下 [0,2]×[0,10] 与 [8,10]×[0,10] 两块 ⇒ **MultiPolygon，2 部件**，面积 20+20 = 40，包串 (0,10,0,10)
//   input   = A−C = 洞盖住的那段 [2,8]×[0,10] ⇒ Polygon，1 环（无洞），面积 6×10 = 60，包串 (2,8,0,10)
//   overlay = C−A ⇒ Polygon，**2 环**（外环 [-5,15]² + 被 A 咬出的洞 [2,8]×[0,10]），
//             面积 400-72-40 = 288，包串 (-5,15,-5,15)
//   校验和 40+60+288 = 388 = area(A)+area(C)-area(A∩C) = 100+328-40 ✓
TEST_F(PluginTest, VectorUnionReadsMultiPolygonPieceAreaByClassFamily) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const fs::path inputPath = getTestDir() / "e2e_union_true_holed_input.gpkg";
    const fs::path overlayPath = getTestDir() / "e2e_union_true_holed_overlay.gpkg";
    const std::string output = utf8PathString(getTestDir() / "e2e_union_true_holed_output.gpkg");

    UnionFixtureFeature srcA;
    srcA.minX = 0.0; srcA.minY = 0.0; srcA.maxX = 10.0; srcA.maxY = 10.0; srcA.name = "src1";
    ASSERT_TRUE(writeUnionPolygonLayer(inputPath, {srcA}));

    UnionFixtureFeature ovlC;
    ovlC.minX = -5.0; ovlC.minY = -5.0; ovlC.maxX = 15.0; ovlC.maxY = 15.0; ovlC.pop = 9;
    ovlC.hasHole = true;
    ovlC.holeMinX = 2.0; ovlC.holeMinY = -1.0; ovlC.holeMaxX = 8.0; ovlC.holeMaxY = 11.0;
    ASSERT_TRUE(writeUnionPolygonLayer(overlayPath, {ovlC}));

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("union");
    params["input"] = utf8PathString(inputPath);
    params["overlay_vector"] = utf8PathString(overlayPath);
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.message, "Union completed: 3 features");

    GDALDataset* rawOutDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(rawOutDs, nullptr);
    gis::core::GdalDatasetPtr outDs(rawOutDs);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    const auto pieces = readUnionPieces(outLayer);

    // 条数先钉死：both 若被写成两条独立要素就是 4 条，下面的按包串配对会跟着红。
    EXPECT_EQ(pieces.size(), 3u) << "带洞叠加面下仍然只有三条分块（both 必须是一个 MultiPolygon，不是两条要素）";

    const int bothIdx = findUnionPiece(pieces, "both", 0.0, 10.0, 0.0, 10.0);
    ASSERT_GE(bothIdx, 0) << "缺少 source=both 且包串 (0,10,0,10) 的分块";
    EXPECT_EQ(pieces[bothIdx].geomType, wkbMultiPolygon) << "洞把 A 切成左右两块 ⇒ A∩C 是多面";
    EXPECT_EQ(pieces[bothIdx].partCount, 2) << "部件应为 [0,2]×[0,10] 与 [8,10]×[0,10]";
    // 面积 40 只能从多面那一支读出来（两支各 20）⇒ 这一条就是判类族读数的判据。
    EXPECT_NEAR(pieces[bothIdx].area, 40.0, 1e-6) << "20+20：多面面积必须由 OGRGeometryCollection::get_Area() 求和";
    EXPECT_EQ(unionFieldValue(pieces[bothIdx], "name"), "src1");
    EXPECT_EQ(unionFieldValue(pieces[bothIdx], "pop"), "9");

    const int inputIdx = findUnionPiece(pieces, "input", 2.0, 8.0, 0.0, 10.0);
    ASSERT_GE(inputIdx, 0) << "缺少 source=input 且包串 (2,8,0,10) 的分块";
    EXPECT_EQ(pieces[inputIdx].geomType, wkbPolygon);
    EXPECT_EQ(pieces[inputIdx].partCount, 1) << "input 块是洞盖住的那段，本身没有洞";
    EXPECT_NEAR(pieces[inputIdx].area, 60.0, 1e-6);
    EXPECT_EQ(unionFieldValue(pieces[inputIdx], "name"), "src1");
    EXPECT_EQ(unionFieldValue(pieces[inputIdx], "pop"), "<null>");

    const int overlayIdx = findUnionPiece(pieces, "overlay", -5.0, 15.0, -5.0, 15.0);
    ASSERT_GE(overlayIdx, 0) << "缺少 source=overlay 且包串 (-5,15,-5,15) 的分块";
    EXPECT_EQ(pieces[overlayIdx].geomType, wkbPolygon);
    EXPECT_EQ(pieces[overlayIdx].partCount, 2) << "overlay 块 = 外环 + 被 A 咬出的洞";
    EXPECT_NEAR(pieces[overlayIdx].area, 288.0, 1e-6);
    EXPECT_EQ(unionFieldValue(pieces[overlayIdx], "name"), "<null>");
    EXPECT_EQ(unionFieldValue(pieces[overlayIdx], "pop"), "9");

    double totalArea = 0.0;
    for (const auto& piece : pieces) {
        totalArea += piece.area;
    }
    EXPECT_NEAR(totalArea, 388.0, 1e-6) << "40+60+288 必须等于并集面积 100+328-40";

    EXPECT_EQ(unionMetadataValue(result, "union_input_pieces"), "1");
    EXPECT_EQ(unionMetadataValue(result, "union_overlap_pieces"), "1");
    EXPECT_EQ(unionMetadataValue(result, "union_overlay_pieces"), "1");
    EXPECT_EQ(unionMetadataValue(result, "union_total_pieces"), "3");
}

// T6：用户数据列自己就叫 source 时，**让位的是本工具的标记列**（source → source_2），数据列名逐字保留。
// WHY 这条判据重要：把用户列改名会让下游按原字段名取值的代码静默拿不到值 ——
// 正是"参数被接受但不起作用"那一族缺陷的镜像；标记列是本工具附加的，理应让位。
// 夹具沿用统一坐标：源 A=[0,10]×[0,4]（面积 40，字段 source="keep_me"）、
//                   叠加 B=[6,16]×[2,12]（面积 100，字段 pop=7）⇒ 分块 32/8/92、包串同 T2。
// 期望输出列**依次**为 source、pop、source_2（按下标逐个比，只判"存在"判不住哪一列才是标记）。
TEST_F(PluginTest, VectorUnionKeepsUserFieldNamedSourceAndRenamesMarker) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const fs::path inputPath = getTestDir() / "e2e_union_true_marker_collide_input.gpkg";
    const fs::path overlayPath = getTestDir() / "e2e_union_true_marker_collide_overlay.gpkg";
    const std::string output = utf8PathString(getTestDir() / "e2e_union_true_marker_collide_output.gpkg");

    UnionFixtureFeature srcA;
    srcA.minX = 0.0; srcA.minY = 0.0; srcA.maxX = 10.0; srcA.maxY = 4.0; srcA.source = "keep_me";
    ASSERT_TRUE(writeUnionPolygonLayer(inputPath, {srcA}));

    UnionFixtureFeature ovlB;
    ovlB.minX = 6.0; ovlB.minY = 2.0; ovlB.maxX = 16.0; ovlB.maxY = 12.0; ovlB.pop = 7;
    ASSERT_TRUE(writeUnionPolygonLayer(overlayPath, {ovlB}));

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("union");
    params["input"] = utf8PathString(inputPath);
    params["overlay_vector"] = utf8PathString(overlayPath);
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* rawOutDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(rawOutDs, nullptr);
    gis::core::GdalDatasetPtr outDs(rawOutDs);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    const auto fieldNames = readFieldNames(outLayer);
    const auto pieces = readUnionPieces(outLayer);

    ASSERT_EQ(fieldNames.size(), 3u) << "字段名实际为 " << fieldNames.size() << " 列";
    EXPECT_EQ(fieldNames[0], "source");      // 源数据列逐字保留，一个字都不许改
    EXPECT_EQ(fieldNames[1], "pop");         // 叠加字段没有同名 ⇒ 原样
    EXPECT_EQ(fieldNames[2], "source_2");    // 标记列按同一条改名规则让位

    EXPECT_EQ(pieces.size(), 3u);

    // 数据列 source 里不许出现标记值：撞名时它仍然是用户那份数据。
    for (const auto& piece : pieces) {
        const std::string userValue = unionFieldValue(piece, "source");
        EXPECT_TRUE(userValue == "keep_me" || userValue == "<null>")
            << "数据列 source 被标记值改写了：" << userValue;
    }

    // 标记按列名 source_2 读（实现里它就是 sourceFieldIdx 那一列），不硬写第 3 列。
    const int inputIdx = findUnionPieceMarkedBy(pieces, "source_2", "input", 0.0, 10.0, 0.0, 4.0);
    ASSERT_GE(inputIdx, 0) << "缺少标记为 input 且包串 (0,10,0,4) 的分块";
    EXPECT_EQ(unionFieldValue(pieces[inputIdx], "source_2"), "input");
    EXPECT_EQ(unionFieldValue(pieces[inputIdx], "source"), "keep_me");
    EXPECT_EQ(unionFieldValue(pieces[inputIdx], "pop"), "<null>");

    const int bothIdx = findUnionPieceMarkedBy(pieces, "source_2", "both", 6.0, 10.0, 2.0, 4.0);
    ASSERT_GE(bothIdx, 0) << "缺少标记为 both 且包串 (6,10,2,4) 的分块";
    EXPECT_EQ(unionFieldValue(pieces[bothIdx], "source_2"), "both");
    EXPECT_EQ(unionFieldValue(pieces[bothIdx], "source"), "keep_me");
    EXPECT_EQ(unionFieldValue(pieces[bothIdx], "pop"), "7");

    const int overlayIdx = findUnionPieceMarkedBy(pieces, "source_2", "overlay", 6.0, 16.0, 2.0, 12.0);
    ASSERT_GE(overlayIdx, 0) << "缺少标记为 overlay 且包串 (6,16,2,12) 的分块";
    EXPECT_EQ(unionFieldValue(pieces[overlayIdx], "source_2"), "overlay");
    EXPECT_EQ(unionFieldValue(pieces[overlayIdx], "source"), "<null>");
    EXPECT_EQ(unionFieldValue(pieces[overlayIdx], "pop"), "7");
}

// T7（N128 后半）：线 × 面路径（路径 B）也要两侧整表属性，标记词表收敛为 input/both（split 退役）。
// 手算：源 1 条线 (0,0)→(30,0) 长度 30、name="road1"；叠加**两条互不相交**的面（互不相交才不触发高度重叠闸）
//   P1=[5,10]×[-5,5]（pop=1）、P2=[20,25]×[-5,5]（pop=2）；
//   按 x 区间的切分结果与长度：[0,5]=5 → input、[5,10]=5 → both、[10,20]=10 → input、
//   [20,25]=5 → both、[25,30]=5 → input；
//   长度和 5+5+10+5+5 = 30 == 源线长度 ⇒ 这就是路径 B 的"不重不漏"不变量。
//   面积可加那一条不变量在这一路**不适用**：这一路不产出叠加面 ⇒ union_overlay_pieces 恒为 0。
// 本刀之前的旧实现：输出表只有 source 一列、5 条片段一律写 "split" ⇒ 表结构、两侧属性、
// 三条分桶计数与标记词表一起红（丢掉源侧 name 正是 N128 在这一路的症状）。
// 预研实测（工单 §5）：写出顺序不是 x 单调，落盘是 (0,5)→(10,20)→(25,30)→(20,25)→(5,10)
// ⇒ 用例一律按"包串四角 + 标记值"配对取块，不许按行号取。
TEST_F(PluginTest, VectorUnionMixedDimensionCarriesAttributesPerPiece) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const fs::path inputPath = getTestDir() / "e2e_union_mixed_attr_input.gpkg";
    const fs::path overlayPath = getTestDir() / "e2e_union_mixed_attr_overlay.gpkg";
    const std::string output = utf8PathString(getTestDir() / "e2e_union_mixed_attr_output.gpkg");

    UnionFixtureLine srcLine;
    srcLine.x0 = 0.0; srcLine.x1 = 30.0; srcLine.y = 0.0; srcLine.name = "road1";
    ASSERT_TRUE(writeUnionLineLayer(inputPath, {srcLine}));

    UnionFixtureFeature poly1;
    poly1.minX = 5.0; poly1.minY = -5.0; poly1.maxX = 10.0; poly1.maxY = 5.0; poly1.pop = 1;
    UnionFixtureFeature poly2;
    poly2.minX = 20.0; poly2.minY = -5.0; poly2.maxX = 25.0; poly2.maxY = 5.0; poly2.pop = 2;
    ASSERT_TRUE(writeUnionPolygonLayer(overlayPath, {poly1, poly2}));

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("union");
    params["input"] = utf8PathString(inputPath);
    params["overlay_vector"] = utf8PathString(overlayPath);
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.message, "Union completed: 5 features");

    GDALDataset* rawOutDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(rawOutDs, nullptr);
    gis::core::GdalDatasetPtr outDs(rawOutDs);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    const auto pieces = readUnionPieces(outLayer);

    EXPECT_EQ(pieces.size(), 5u) << "两条叠加面把源线切成 3 条面外片段 + 2 条面内片段";
    EXPECT_EQ(unionMetadataValue(result, "union_input_pieces"), "3");
    EXPECT_EQ(unionMetadataValue(result, "union_overlap_pieces"), "2");
    // 这一路不产出叠加面 ⇒ 恒为 0（是契约，不是缺陷）；不重不漏由下面"长度和 == 30"那条兜住。
    EXPECT_EQ(unionMetadataValue(result, "union_overlay_pieces"), "0");
    EXPECT_EQ(unionMetadataValue(result, "union_total_pieces"), "5");

    // 表结构与路径 A 同一条规则：源字段原序 → 叠加字段 → 标记列（不再有"只建 source 一列"的特例）。
    const auto fieldNames = readFieldNames(outLayer);
    ASSERT_EQ(fieldNames.size(), 3u) << "字段名实际为 " << fieldNames.size() << " 列";
    EXPECT_EQ(fieldNames[0], "name");
    EXPECT_EQ(fieldNames[1], "pop");
    EXPECT_EQ(fieldNames[2], "source");

    struct ExpectedPiece {
        const char* source;
        double minX;
        double maxX;
        double length;
        const char* pop;
    };
    const ExpectedPiece expectations[] = {
        {"input", 0.0, 5.0, 5.0, "<null>"},
        {"both", 5.0, 10.0, 5.0, "1"},
        {"input", 10.0, 20.0, 10.0, "<null>"},
        {"both", 20.0, 25.0, 5.0, "2"},
        {"input", 25.0, 30.0, 5.0, "<null>"},
    };
    double totalLength = 0.0;
    for (const auto& expected : expectations) {
        // 片段都在 y=0 这条水平线上 ⇒ 包串的 MinY/MaxY 都钉成 0：x↔y 转置在这一条上必红。
        const int idx = findUnionPiece(pieces, expected.source, expected.minX, expected.maxX, 0.0, 0.0);
        ASSERT_GE(idx, 0) << "缺少 source=" << expected.source << " 且包串 ("
                          << expected.minX << "," << expected.maxX << ",0,0) 的片段";
        const UnionOutputPiece& piece = pieces[static_cast<size_t>(idx)];
        EXPECT_EQ(piece.geomType, wkbMultiLineString) << "线 × 面的输出仍是线图层（不产出叠加面）";
        EXPECT_EQ(piece.partCount, 1) << "每条片段都是单成员 MultiLineString";
        EXPECT_NEAR(piece.length, expected.length, 1e-6);
        // 整表属性：源侧 name 逐条片段都带着（旧实现整列丢了）。
        EXPECT_EQ(unionFieldValue(piece, "name"), "road1");
        // pop 按 x 区间配对：面内取归属那条面的值，面外全 NULL。
        EXPECT_EQ(unionFieldValue(piece, "pop"), expected.pop);
        totalLength += piece.length;
    }

    // 词表收敛：退役的 "split" 不许再出现在任何片段上（判据不许放宽成"任意非空"）。
    for (const auto& piece : pieces) {
        EXPECT_TRUE(piece.source == "input" || piece.source == "both")
            << "线 × 面路径写出了退役的标记值：" << piece.source;
    }
    EXPECT_NEAR(totalLength, 30.0, 1e-6) << "5+5+10+5+5 必须等于源线长度 30（路径 B 的不重不漏）";
}

// 本刀就地更新：这条用例的条数与几何类型断言逐字没动（线 × 面的**切分结果**没变），
// 只新增"标记词表 + 源侧 name 整表属性"两组判据 —— 旧实现丢了 name 列、三条片段一律写 split，
// 所以这两组在本刀之前必红。夹具里 (0,50)→(100,50) 被正方形 [25,75]×[25,75] 切成 外/内/外
// ⇒ 标记值依次 input/both/input，按 x 区间 (0,25)/(25,75)/(75,100) 配对。
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

    // ↓↓↓ 本刀新增（不改上面条数与几何类型那两条）：线 × 面路径的标记词表与源侧整表属性。
    // 旧实现输出表只有 source 一列、三条片段一律写 "split" ⇒ 下面两组判据在本刀之前必红。
    // 夹具：源线 (0,50)→(100,50)（name="road"）被正方形 [25,75]×[25,75] 切成 外/内/外
    // ⇒ 标记值依次 input/both/input，按 x 区间 (0,25)/(25,75)/(75,100) 配对（写出顺序不是契约，不许按行号取）。
    const auto pieces = readUnionPieces(outLayer);
    ASSERT_EQ(pieces.size(), 3u);

    // 表结构：源字段在前（旧实现整列丢了它），标记列在后。
    const auto fieldNames = readFieldNames(outLayer);
    ASSERT_EQ(fieldNames.size(), 2u) << "字段名实际为 " << fieldNames.size() << " 列";
    EXPECT_EQ(fieldNames[0], "name");
    EXPECT_EQ(fieldNames[1], "source");

    const int leftIdx = findUnionPiece(pieces, "input", 0.0, 25.0, 50.0, 50.0);
    ASSERT_GE(leftIdx, 0) << "缺少 source=input 且包串 (0,25,50,50) 的片段";
    const int middleIdx = findUnionPiece(pieces, "both", 25.0, 75.0, 50.0, 50.0);
    ASSERT_GE(middleIdx, 0) << "缺少 source=both 且包串 (25,75,50,50) 的片段（落进叠加面的那段）";
    const int rightIdx = findUnionPiece(pieces, "input", 75.0, 100.0, 50.0, 50.0);
    ASSERT_GE(rightIdx, 0) << "缺少 source=input 且包串 (75,100,50,50) 的片段";

    // 三条片段都带着源侧那份 name（整表属性跟着每条切分结果走）。
    for (size_t i = 0; i < pieces.size(); ++i) {
        EXPECT_EQ(unionFieldValue(pieces[i], "name"), "road") << "第 " << i << " 条片段丢了源侧 name";
        EXPECT_TRUE(pieces[i].source == "input" || pieces[i].source == "both")
            << "第 " << i << " 条片段写出了退役的标记值：" << pieces[i].source;
    }
    // 长度可加（路径 B 的"不重不漏"）：25 + 50 + 25 == 源线长度 100。
    EXPECT_NEAR(pieces[leftIdx].length + pieces[middleIdx].length + pieces[rightIdx].length,
                100.0, 1e-6);

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
    std::string output = (getTestDir() / "e2e_spatial_join_rejected_output.gpkg").string();

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
    // 21-FixA part2 I-3：同 tile 那两例的口径——光断子串 "CRS" 不够。
    // 这句话由 vector_plugin_common.inc:87 的 failCrsMismatch(action) 生成，而 Clip / Nearest /
    // Overlay 等动作共用同一个函数（实测 5 条用例都在断 message 含 "CRS"）⇒ 子串判据允许
    // "别的动作形状的 CRS 报错"顶替本用例的判据。整句字面 + 反向钉住输出文件名把原因锁死：
    // 若失败其实来自打不开/写不出这份 GPKG（原先它与 VectorSpatialJoinWritesJoinCountAndValue
    // 共用同名输出，一个期望失败一个期望成功 ⇒ 已各自改名），消息里必然出现该文件名 ⇒ 红。
    EXPECT_NE(result.message.find("Spatial join requires input layers to use the same CRS."), std::string::npos)
        << result.message;
    EXPECT_EQ(result.message.find("e2e_spatial_join_rejected_output.gpkg"), std::string::npos)
        << "失败消息点名了本用例的输出文件 ⇒ 原因是这份 GPKG 打不开/写不出，不是 CRS 不匹配："
        << result.message;
}

TEST_F(PluginTest, VectorSpatialJoinWritesJoinCountAndValue) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_spatial_join_input.gpkg");
    const std::string join = utf8PathString(getTestDir() / "e2e_spatial_join_join.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_spatial_join_written_output.gpkg");

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

// ── C3f / N127：--where 白名单必须 UTF-8 感知（中文属性值过滤） ──
// 编码约定（要动这两条用例先读这段，别凭猜改）：
//   tests/CMakeLists.txt:63 给 gis_tests 加了 /utf-8 ⇒ 源文件按 UTF-8 解码，
//   **窄字符串字面量的执行字符集同样是 UTF-8**，与 GDAL 内部用的编码、与本仓 data/ 里
//   GeoJSON/GPKG 属性值的字节序列是同一套 ⇒ 这里可以直接写 "北京"，
//   不需要 u8"" 前缀、也不需要 \x 转义（转义反而会让"源码里的中文"这件事失去可读性）。
//   本文件是无 BOM 的 UTF-8、行尾 LF；有人用 ANSI/GBK 另存出乱码时，
//   先红的是 `ctest -R source_mojibake_guard`（B17 检测器），不是人眼。
// 反向那臂里的坏字节**必须**用 static_cast<char>(0xNN) 逐字节拼：
//   C++ 的十六进制转义会一路吃掉后续十六进制数字（"\xff\xfe" 是一个 \xfffe 而不是两个字节），
//   写成转义串就是在测别的字节序列，判据当场失去意义。
TEST_F(PluginTest, VectorFilterWhereClauseAcceptsChineseAttributeValue) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string input = utf8PathString(getTestDir() / "e2e_filter_where_cn_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_filter_where_cn_output.gpkg");

    // 全部期望值从**输入侧**手推，在本用例自己的构造段一次定死（不从产物回填）：
    //   keptName 是唯一该被 --where 留下的要素，droppedName 必须整条消失；
    //   code 与 ringPoints 是 name↔几何 的配对锚 —— 只断 name 的话，
    //   "留下对的字段但把两条几何写反"这种产物照样绿，所以配对要一起钉。
    //   C3c-A 把 filter 晋升 GOLD：外环逐点坐标与面积都按夹具手算写死（EXPECT_DOUBLE_EQ），
    //   夹具矩形取 900×400（不是正方形）⇒ x↔y 转置不再是不变量，转置产物会逐点红。
    const std::string keptName = "北京";
    const std::string droppedName = "上海";
    const std::string absentName = "广州";  // 输入里不存在 ⇒ 否定臂必须 0 条
    const int keptCode = 1;
    const int droppedCode = 2;
    const int keptRingPoints = 5;     // 四边形闭合 ⇒ 5 个点
    const int droppedRingPoints = 4;  // 三角形闭合 ⇒ 4 个点
    const double keptX[5] = {0.0, 900.0, 900.0, 0.0, 0.0};
    const double keptY[5] = {0.0, 0.0, 400.0, 400.0, 0.0};
    const double keptArea = 360000.0;  // 900 × 400

    {
        std::error_code ec;
        fs::remove(input, ec);  // 夹具对本次运行负责：上一轮的同名文件不许渗进这一轮
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        // 输出层类型由 resultLayerGeometryType() 归一成 MultiPolygon（common.inc:388-402），
        // 输入直接按 MultiPolygon 建 ⇒ 回读时两侧同形，断言不必再猜驱动会怎么升维。
        auto* layer = ds->CreateLayer("provinces", srs.get(), wkbMultiPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);
        OGRFieldDefn codeField("code", OFTInteger);
        ASSERT_EQ(layer->CreateField(&codeField), OGRERR_NONE);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRMultiPolygon polys;
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(0, 0);
        ring.addPoint(900, 0);
        ring.addPoint(900, 400);
        ring.addPoint(0, 400);
        ring.closeRings();
        poly.addRing(&ring);
        polys.addGeometry(&poly);
        feat->SetGeometry(&polys);
        feat->SetField("name", keptName.c_str());
        feat->SetField("code", keptCode);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);

        feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRMultiPolygon polys2;
        OGRPolygon poly2;
        OGRLinearRing ring2;
        ring2.addPoint(5000, 5000);
        ring2.addPoint(6000, 5000);
        ring2.addPoint(5500, 6000);
        ring2.closeRings();
        poly2.addRing(&ring2);
        polys2.addGeometry(&poly2);
        feat->SetGeometry(&polys2);
        feat->SetField("name", droppedName.c_str());
        feat->SetField("code", droppedCode);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("filter");
    params["input"] = input;
    params["output"] = output;
    params["where"] = "name='" + keptName + "'";

    const auto result = p->execute(params, progress_);
    // N127 的正面形态：中文属性值的 --where 必须被接受并且真的留下那一条。
    ASSERT_TRUE(result.success)
        << "N127：中文 --where 被 WHERE 白名单拒了，message = " << result.message;
    ASSERT_TRUE(fs::exists(output));

    // filter **没有** feature_count 元数据（doFilter 只回消息与输出路径，见
    // vector_plugin_info_filter.inc:237-238 与 docs/算法说明/矢量数据处理/空间过滤.md
    // "返回消息中的数量是实际写入输出图层的要素数"）⇒ 自报数只能整串核对消息，
    // 再用落盘侧的 GetFeatureCount 与它对撞（报数与产物不一致必须红）。
    EXPECT_EQ(result.message, "Filter completed: 1 features written");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(FALSE), 1);

    int readBack = 0;
    outLayer->ResetReading();
    OGRFeature* outFeat = nullptr;
    while ((outFeat = outLayer->GetNextFeature()) != nullptr) {
        ++readBack;
        const std::string gotName(outFeat->GetFieldAsString("name"));
        EXPECT_EQ(gotName, keptName) << "第 " << readBack << " 条的 name 不等于输入侧手推的保留值";
        EXPECT_NE(gotName, droppedName) << "被 --where 排除的那条漏进了输出";
        EXPECT_EQ(outFeat->GetFieldAsInteger("code"), keptCode);
        EXPECT_NE(outFeat->GetFieldAsInteger("code"), droppedCode);
        ASSERT_NE(outFeat->GetGeometryRef(), nullptr);
        // 点数走 MultiPolygon → Polygon → 外环这条实路径（getNumPoints() 只在 OGRSurface
        // 及其子类上声明，基类 OGRGeometry 没有 ⇒ 直接对 GetGeometryRef() 调它编不过）。
        OGRMultiPolygon* outPolys = outFeat->GetGeometryRef()->toMultiPolygon();
        ASSERT_NE(outPolys, nullptr);
        ASSERT_EQ(outPolys->getNumGeometries(), 1);
        OGRPolygon* outPoly = outPolys->getGeometryRef(0);
        ASSERT_NE(outPoly, nullptr);
        ASSERT_NE(outPoly->getExteriorRing(), nullptr);
        EXPECT_EQ(outPoly->getExteriorRing()->getNumPoints(), keptRingPoints)
            << "name↔几何 配对断了：留下的字段配上了另一条要素的几何";
        EXPECT_NE(outPoly->getExteriorRing()->getNumPoints(), droppedRingPoints);
        for (int i = 0; i < keptRingPoints; ++i) {
            EXPECT_DOUBLE_EQ(outPoly->getExteriorRing()->getX(i), keptX[i])
                << "第 " << i << " 个顶点的 x 与输入侧手算值不等（x↔y 转置在这里红）";
            EXPECT_DOUBLE_EQ(outPoly->getExteriorRing()->getY(i), keptY[i])
                << "第 " << i << " 个顶点的 y 与输入侧手算值不等";
        }
        EXPECT_DOUBLE_EQ(outPoly->get_Area(), keptArea)
            << "保留要素的面积与 900×400 手算值不等";
        OGRFeature::DestroyFeature(outFeat);
    }
    EXPECT_EQ(readBack, 1) << "输出层逐条数出来的要素数与自报数不一致";
    GDALClose(outDs);

    // 否定臂（C3c-A）：同一条中文谓词换成一个输入里不存在的中文省名 ⇒ 必须 0 条。
    // 与正面臂成对：正面臂证明"中文值真能命中且几何是那一条"，这一臂证明"谓词真的在筛"。
    const std::string emptyOutput =
        utf8PathString(getTestDir() / "e2e_filter_where_cn_empty_output.gpkg");
    std::map<std::string, gis::framework::ParamValue> missParams;
    missParams["action"] = std::string("filter");
    missParams["input"] = input;
    missParams["output"] = emptyOutput;
    missParams["where"] = "name='" + absentName + "'";

    const auto miss = p->execute(missParams, progress_);
    ASSERT_TRUE(miss.success) << "中文否定臂失败: " << miss.message;
    EXPECT_EQ(miss.message, "Filter completed: 0 features written");

    GDALDataset* missDs = static_cast<GDALDataset*>(GDALOpenEx(
        emptyOutput.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(missDs, nullptr);
    OGRLayer* missLayer = missDs->GetLayer(0);
    ASSERT_NE(missLayer, nullptr);
    EXPECT_EQ(missLayer->GetFeatureCount(FALSE), 0);
    GDALClose(missDs);
}

TEST_F(PluginTest, VectorFilterWhereClauseStillRejectsInjectionAndBrokenUtf8) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    // 这一条只验"白名单该拒的还得拒"，不验过滤结果 ⇒ 输入只要**能打开**就够了：
    // doFilter 的顺序是 打开输入 → 定位图层 → 校验 --where（inc:148-159），
    // 输入不存在时先红在 "Cannot open vector file"，那就成了"任何错误都算过"的空转。
    const std::string input = utf8PathString(getTestDir() / "e2e_filter_where_reject_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_filter_where_reject_output.gpkg");
    const std::string expectedError = "WHERE clause contains disallowed characters or keywords";

    {
        std::error_code ec;
        fs::remove(input, ec);
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("provinces", srs.get(), wkbMultiPolygon, nullptr);
        ASSERT_NE(layer, nullptr);
        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);
        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRMultiPolygon polys;
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(0, 0);
        ring.addPoint(10, 0);
        ring.addPoint(10, 10);
        ring.closeRings();
        poly.addRing(&ring);
        polys.addGeometry(&poly);
        feat->SetGeometry(&polys);
        feat->SetField("name", "北京");  // 窄字面量即 UTF-8 字节（见上方编码约定注释块）
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    const char lead = static_cast<char>(0xE4);  // 合法 3 字节 lead（"北" = E5 8C 97 那一类）
    const std::string goodCn = "北京";

    struct Arm {
        const char* label;
        std::string where;
    };
    const std::vector<Arm> arms = {
        // ① 工单点名的注入串：分号（inc:38）+ 危险关键字 DROP（inc:21-24）双重命中
        {"semicolon_and_drop", "1=1; DROP TABLE x"},
        // ①' 双引号从来不在白名单里（inc:6-9 只放行单引号）
        {"double_quote", "name=\"" + goodCn + "\""},
        // ①'' 反引号同样从来不在白名单里
        {"backtick", "name=`" + goodCn + "`"},
        // ② 非法 UTF-8：0xFF / 0xFE 任何位置都不能作 lead
        {"invalid_utf8_ff_fe", std::string("name='") + std::string(1, static_cast<char>(0xFF)) +
                                   std::string(1, static_cast<char>(0xFE)) + "'"},
        // ③ overlong：0xC0/0xC1 领头的 2 字节序列是禁止形态（U+0080 以下必须走单字节）
        {"overlong_c0_80", std::string("name='") + std::string(1, static_cast<char>(0xC0)) +
                              std::string(1, static_cast<char>(0x80)) + "'"},
        // ④ 超出 U+10FFFF：0xF5..0xF7 不是合法的 4 字节 lead
        {"above_u10ffff_f5", std::string("name='") + std::string(1, static_cast<char>(0xF5)) +
                                std::string(1, static_cast<char>(0x80)) +
                                std::string(1, static_cast<char>(0x80)) +
                                std::string(1, static_cast<char>(0x80)) + "'"},
        // ⑤ 悬挂 lead：合法 lead 后面缺续接字节
        {"dangling_lead", std::string("name='") + std::string(1, lead) + "'"},
        // ⑥ 落单的续接字节（前面没有 lead）
        {"lone_continuation", std::string("name='") + std::string(1, static_cast<char>(0x80)) + "'"},
    };
    ASSERT_GE(arms.size(), 4u) << "反向臂少于工单要求的四条，本用例就成了空检查";

    for (const auto& arm : arms) {
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("filter");
        params["input"] = input;
        params["output"] = output;
        params["where"] = arm.where;

        const auto result = p->execute(params, progress_);
        EXPECT_FALSE(result.success) << "反向臂 " << arm.label << " 竟然通过了";
        // 不只断"失败了"：必须失败在**这一条**白名单文案上，
        // 否则任何错误（打不开输入、找不到图层、写盘失败）都会让它"绿"。
        EXPECT_EQ(result.message, expectedError) << "反向臂 " << arm.label << " 的失败文案不是白名单";
    }
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

// ── N122：非 ASCII 输出路径下"先删旧产物"──────────
// 缺陷：窄串构造 fs::path 在 Windows 按 ANSI 解，UTF-8 中文路径下 exists/directory_iterator 全部判"不存在"
// ⇒ 旧产物的边车不会被删；GDAL 自己按 UTF-8 写新 .shp ⇒ 用户拿到"新几何 + 旧索引/旧属性表"。
// 夹具用 .sbn/.sbx/.qix 三个边车：它们与输出同 stem，而 Shapefile 驱动**默认不会重写**这三个
//（.dbf/.shx 会被驱动覆盖，拿它们做判据在旧代码上也会绿 ⇒ 不承重）。
static void writeStaleShapefileSidecars(const fs::path& shpPath) {
    const fs::path stem = shpPath.parent_path() / shpPath.stem();
    for (const char* ext : {".sbn", ".sbx", ".qix"}) {
        fs::path sidecar = stem;
        sidecar += ext;
        std::ofstream ofs(sidecar, std::ios::binary | std::ios::trunc);
        ofs << "STALE";
    }
}

static void expectStaleSidecarsRemoved(const fs::path& shpPath) {
    const fs::path stem = shpPath.parent_path() / shpPath.stem();
    for (const char* ext : {".sbn", ".sbx", ".qix"}) {
        fs::path sidecar = stem;
        sidecar += ext;
        EXPECT_FALSE(fs::exists(sidecar))
            << "N122：旧产物边车没被清掉（窄串 fs::path 在中文路径下失效）：" << sidecar.u8string();
    }
}

TEST_F(PluginTest, VectorRemoveStaleShapefileSidecarsUnderNonAsciiPath) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string input = (getTestDir() / "e2e_n122_lines.shp").string();
    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("lines", srs.get(), wkbLineString);
        ASSERT_NE(layer, nullptr);
        auto* featDefn = layer->GetLayerDefn();
        for (int i = 0; i < 2; ++i) {
            auto* feat = OGRFeature::CreateFeature(featDefn);
            OGRLineString line;
            line.addPoint(i * 100.0, 0.0);
            line.addPoint(i * 100.0 + 50.0, 30.0);
            feat->SetGeometry(&line);
            ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
            OGRFeature::DestroyFeature(feat);
        }
        GDALClose(ds);
    }

    // 中文目录用 fs::u8path 构造：直接写窄串字面量在 Windows 同样会被按 ANSI 解，夹具自己就先坏了。
    const fs::path chineseDir = getTestDir() / fs::u8path(u8"中文目录");
    fs::create_directories(chineseDir);

    struct Case {
        const char* action;
        const char* fileBase;
        const char* paramKey;
        double paramValue;
    };
    // buffer 走共享 helper（removeExistingVectorOutput）；simplify 走它自己的内联复制 ⇒ 两类点位各判一次。
    const Case cases[] = {
        {"buffer", "缓冲结果", "distance", 10.0},
        {"simplify", "简化结果", "tolerance", 1.0},
    };

    for (const auto& c : cases) {
        const fs::path outShp = chineseDir / fs::u8path(std::string(c.fileBase) + ".shp");
        const std::string outUtf8 = utf8PathString(outShp);
        // 每条腿开始前把三个边车重新摆一遍，并确认它们真的存在（否则"删掉了"可能是本来就没有）。
        writeStaleShapefileSidecars(outShp);
        const fs::path stem = outShp.parent_path() / outShp.stem();
        for (const char* ext : {".sbn", ".sbx", ".qix"}) {
            fs::path sidecar = stem;
            sidecar += ext;
            ASSERT_TRUE(fs::exists(sidecar)) << "夹具前置失败，边车没造出来：" << sidecar.u8string();
        }

        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string(c.action);
        params["input"] = input;
        params["output"] = outUtf8;
        params[c.paramKey] = c.paramValue;

        auto result = p->execute(params, progress_);
        EXPECT_TRUE(result.success) << c.action << " failed: " << result.message;
        ASSERT_TRUE(fs::exists(outShp)) << "产物没写出来：" << outUtf8;
        expectStaleSidecarsRemoved(outShp);
    }
}

// 注：曾经试过一条 `ProjectionRemoveStaleShapefileSidecarsUnderNonAsciiPath`（同样的中文目录 + 旧边车），
// 但两臂取证证明它**不承重**：把 projection 的 u8path 退回窄串后该用例仍然绿 ——
// projection 这条腿用 GDAL 覆盖式建数据集，驱动自己会把同 stem 的边车一起删掉，把 helper 的早退缺陷掩盖了。
// 按"能红才算判据"的口径删掉；projection 的那行 u8path 仍保留（对齐同一族写法），缺口登记在 RELEASE 的 N122 条目。

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

// ── N130：投影闸收紧为"必须投影坐标系"（用户 2026-10-22 裁定）────────────────────
// 旧闸只拒 "geographic"，而 detectSrsType 对**没有坐标参考**的图层返回 "unknown" ⇒ 这类输入被放过去：
// buffer 会把 distance 当"度"来算、dissolve 之后的面积/长度单位无从谈起。
// 下面两条与地理坐标系那两条的唯一区别是 CreateLayer 传 nullptr（不挂 SRS）——
// 判据必须落在"缺 CRS 也拒 + 失败不留产物"上，否则收紧前后都绿，等于没测到。
TEST_F(PluginTest, VectorBufferRejectsMissingSrs) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string shpPath = (getTestDir() / "e2e_buffer_nosrs_input.shp").string();
    const std::string output = (getTestDir() / "e2e_buffer_nosrs_output.geojson").string();
    std::error_code ignored;
    fs::remove(output, ignored);
    ASSERT_FALSE(fs::exists(output)) << "本轮开始前就残留了输出，半成品判据会失真";

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(shpPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto* layer = ds->CreateLayer("buffer_nosrs_test", nullptr, wkbPoint);
        ASSERT_NE(layer, nullptr);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPoint pt(12950000.0, 4895000.0);
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
    EXPECT_FALSE(result.success)
        << "N130：没有坐标参考的输入仍然 success=true ⇒ 缓冲距离被按度当米算（message="
        << result.message << "）";
    EXPECT_NE(result.message.find("requires a projected coordinate system"), std::string::npos)
        << "失败消息必须点名需要投影坐标系。实际 message=" << result.message;
    ASSERT_FALSE(fs::exists(output)) << "闸要在建输出之前拦下，失败后不得留半成品：" << output;
}

TEST_F(PluginTest, VectorDissolveRejectsMissingSrs) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string shpPath = (getTestDir() / "e2e_dissolve_nosrs_input.shp").string();
    const std::string output = (getTestDir() / "e2e_dissolve_nosrs_output.gpkg").string();
    std::error_code ignored;
    fs::remove(output, ignored);
    ASSERT_FALSE(fs::exists(output)) << "本轮开始前就残留了输出，半成品判据会失真";

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(shpPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto* layer = ds->CreateLayer("input", nullptr, wkbPolygon);
        ASSERT_NE(layer, nullptr);
        OGRFieldDefn fieldDefn("type", OFTString);
        ASSERT_EQ(layer->CreateField(&fieldDefn), OGRERR_NONE);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(12950000.0, 4895000.0); ring.addPoint(12951000.0, 4895000.0);
        ring.addPoint(12951000.0, 4896000.0); ring.addPoint(12950000.0, 4896000.0);
        ring.addPoint(12950000.0, 4895000.0);
        poly.addRing(&ring);
        feat->SetGeometry(&poly);
        feat->SetField("type", "A");
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("dissolve");
    params["input"] = shpPath;
    params["output"] = output;
    params["dissolve_field"] = std::string("type");

    auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success)
        << "N130：没有坐标参考的输入仍然 success=true ⇒ 融合结果没有可解释的面积/长度口径"
           "（message=" << result.message << "）";
    EXPECT_NE(result.message.find("requires a projected coordinate system"), std::string::npos)
        << "失败消息必须点名需要投影坐标系。实际 message=" << result.message;
    ASSERT_FALSE(fs::exists(output)) << "闸要在建输出之前拦下，失败后不得留半成品：" << output;
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

// ── N138：五个动作写出的 .gpkg 带一个"已登记但 0 行"的 rtree ⇒ 按包框查的读端拿到 0 个要素 ──
// 射程是 5 不是 6：立案时把 buffer 也算进去了，复核现读 `vector_plugin_buffer_clip.inc` 的
// `useTransactions = false` 自 f18164a 起就在那里 ⇒ buffer 既不关索引、那条补建调用也被外层挡死（死代码），
// 它的产物索引一直是回填过的 ⇒ 本块里 buffer 那条用例是**对照**，不是缺陷样本（详见 docs/RELEASE.md 的 N138 条目）。
// 四条判据按读端真实用法写（顺序即工单 §3）：① 动作成功 ② 无过滤要素数 == 夹具期望数
// ③ rtree_<层名>_geom 行数 == **非空几何要素数**（解释性）
//    ④ 夹具包框过滤后要素数 == 夹具期望数（主判据）。
// ③ 的比较对象在 C3m 从"要素数"改成"非空几何数"：GDAL 自己建索引的触发器条件是
//    geom IS NOT NULL AND NOT ST_IsEmpty(geom) ⇒ 含 NULL 或空几何的图层**本来就该**少几行，
//    拿要素数去比会把合规产物判成红（本仓现读：e2e_singlepart_input 3 行含 1 个 NULL ⇒ 索引 2 行
//    是合规状态；t20_empty_point_input 4 行含 2 个空点 ⇒ 索引 2 行同样是合规状态）。
//    非空几何数由本层自己遍历产物数出来（GetGeometryRef() 非空且 !IsEmpty()），不从别处借。
// 层名从产物自己取（这六个动作各写各的层名，共五类：union/difference/intersect/dissolved/源层名），
// 要素数与过滤包框一律由各用例按夹具坐标手算写死，不拿产物比产物（N108 那一族自指期望）。

// 走一遍图层，数出"几何存在且不空"的要素数 —— 与 GDAL 建索引的触发器条件同判据。
// 只读游标，进出各 ResetReading 一次，不留 Reading 状态给下面的包框判据。
static long long countGpkgNonEmptyGeometries(OGRLayer* layer) {
    long long nonEmpty = 0;
    layer->ResetReading();
    while (OGRFeature* feat = layer->GetNextFeature()) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (geom != nullptr && !geom->IsEmpty()) {
            ++nonEmpty;
        }
        OGRFeature::DestroyFeature(feat);
    }
    layer->ResetReading();
    return nonEmpty;
}

static void expectGpkgSpatialIndexUsable(OGRLayer* outLayer,
                                         long long expectedFeatureCount,
                                         double filterMinX,
                                         double filterMinY,
                                         double filterMaxX,
                                         double filterMaxY,
                                         const std::string& output) {
    EXPECT_EQ(outLayer->GetFeatureCount(TRUE), expectedFeatureCount)
        << "无过滤要素数与夹具期望不符，后面的索引判据失去意义：" << output;

    const long long nonEmptyGeoms = countGpkgNonEmptyGeometries(outLayer);

    const std::string rtreeTable = std::string("rtree_") + outLayer->GetName() + "_geom";
    GDALDataset* parentDs = outLayer->GetDataset();
    long long rtreeRows = -1;   // -1 = 索引表根本不存在（ExecuteSQL 的失败形态之一）
    const std::string countSql = "SELECT COUNT(*) FROM \"" + rtreeTable + "\"";
    if (OGRLayer* countLayer = parentDs->ExecuteSQL(countSql.c_str(), nullptr, "SQLITE")) {
        OGRFeature* countFeat = countLayer->GetNextFeature();
        if (countFeat) {
            rtreeRows = countFeat->GetFieldAsInteger(0);
            OGRFeature::DestroyFeature(countFeat);
        }
        parentDs->ReleaseResultSet(countLayer);
    }
    EXPECT_EQ(rtreeRows, nonEmptyGeoms)
        << "空间索引表 " << rtreeTable << " 的行数与非空几何要素数不符（要素数 "
        << expectedFeatureCount << "、非空几何数 " << nonEmptyGeoms << "、索引行数 " << rtreeRows
        << "；NULL 与空几何本来就不进索引，所以这里比的是非空几何数而不是要素数）：" << output;

    outLayer->SetSpatialFilterRect(filterMinX, filterMinY, filterMaxX, filterMaxY);
    outLayer->ResetReading();
    const long long filtered = outLayer->GetFeatureCount(TRUE);
    EXPECT_EQ(filtered, expectedFeatureCount)
        << "按夹具包框 (" << filterMinX << ", " << filterMinY << ", " << filterMaxX << ", "
        << filterMaxY << ") 过滤后只拿到 " << filtered << " 个要素，期望 "
        << expectedFeatureCount << " ⇒ 这就是 N138 的用户可见症状：" << output;
    outLayer->SetSpatialFilter(nullptr);
}

// 夹具复用 VectorBufferExecutionReportsMetadata 那份 200 条线（EPSG:3857）：
// x 从 12940000 起每 20 递增、最右点 12940030+199*20=12944010，y 落在 4852000..4852200，
// distance=25 ⇒ 期望 200 个要素、包框 = 极值 ±25 = (12939975, 4851975, 12944035, 4852225)。
// WHY 输出另起一个 .gpkg 名：本用例自己重建同名夹具输入，与那条元数据用例共用产物名
// 会把两条用例变成"谁后跑谁决定产物"的顺序依赖。
TEST_F(PluginTest, VectorBufferGpkgSpatialIndexIsPopulated) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    std::string shpPath = (getTestDir() / "e2e_buffer_input.shp").string();
    std::string output = (getTestDir() / "e2e_buffer_index_output.gpkg").string();
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

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Buffer failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    expectGpkgSpatialIndexUsable(outLayer, 200, 12939975.0, 4851975.0, 12944035.0, 4852225.0,
                                 output);
    GDALClose(outDs);
}

// 夹具复用 VectorClipKeepsOnlyLinearOverlapSegments：1 条 (0,0)-(200,0) 的线被
// [80,120]×[-20,20] 的裁剪面切出 1 个要素 ⇒ 期望 1；包框直接取裁剪面的包框（结果必在其中）。
TEST_F(PluginTest, VectorClipGpkgSpatialIndexIsPopulated) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    std::string input = (getTestDir() / "e2e_clip_line_input.shp").string();
    std::string clip = (getTestDir() / "e2e_clip_line_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_clip_index_output.gpkg").string();

    auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
    ASSERT_NE(driver, nullptr);
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);

    {
        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto* layer1 = ds1->CreateLayer("input", srs.get(), wkbLineString);
        ASSERT_NE(layer1, nullptr);
        auto* feat1 = OGRFeature::CreateFeature(layer1->GetLayerDefn());
        OGRLineString line;
        line.addPoint(0, 0);
        line.addPoint(200, 0);
        feat1->SetGeometry(&line);
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);
    }
    {
        auto* ds2 = driver->Create(clip.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto* layer2 = ds2->CreateLayer("clip", srs.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);
        auto* feat2 = OGRFeature::CreateFeature(layer2->GetLayerDefn());
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

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Clip failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    expectGpkgSpatialIndexUsable(outLayer, 1, 80.0, -20.0, 120.0, 20.0, output);
    GDALClose(outDs);
}

// 夹具复用 buildDissolveTypeFixture（两个共边正方形 [0,100]²、[100,200]²，type 都是 'A'）：
// 同组 ⇒ 融合成 1 个 MULTIPOLYGON ⇒ 期望 1；包框 = 两个夹具正方形并集的包框 (0,0,200,100)。
TEST_F(PluginTest, VectorDissolveGpkgSpatialIndexIsPopulated) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string shpPath = buildDissolveTypeFixture("e2e_dissolve_index_input.shp");
    const std::string output = (getTestDir() / "e2e_dissolve_index_output.gpkg").string();

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("dissolve");
    params["input"] = shpPath;
    params["output"] = output;
    params["dissolve_field"] = std::string("type");

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Dissolve failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    expectGpkgSpatialIndexUsable(outLayer, 1, 0.0, 0.0, 200.0, 100.0, output);
    GDALClose(outDs);
}

// 夹具复用 T2（VectorUnionSplitsIntoThreePieceClasses）那一对坐标：
// A=[0,10]×[0,4]、B=[6,16]×[2,12] ⇒ input-only / both / overlay-only 三条分块 ⇒ 期望 3；
// 包框 = A 与 B 并集的包框 (0,0,16,12)。
TEST_F(PluginTest, VectorUnionGpkgSpatialIndexIsPopulated) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const fs::path inputPath = getTestDir() / "e2e_union_true_overlap_input.gpkg";
    const fs::path overlayPath = getTestDir() / "e2e_union_true_overlap_overlay.gpkg";
    const std::string output = utf8PathString(getTestDir() / "e2e_union_index_output.gpkg");

    UnionFixtureFeature srcA;
    srcA.minX = 0.0; srcA.minY = 0.0; srcA.maxX = 10.0; srcA.maxY = 4.0; srcA.name = "src1";
    ASSERT_TRUE(writeUnionPolygonLayer(inputPath, {srcA}));

    UnionFixtureFeature ovlB;
    ovlB.minX = 6.0; ovlB.minY = 2.0; ovlB.maxX = 16.0; ovlB.maxY = 12.0; ovlB.pop = 7;
    ASSERT_TRUE(writeUnionPolygonLayer(overlayPath, {ovlB}));

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("union");
    params["input"] = utf8PathString(inputPath);
    params["overlay_vector"] = utf8PathString(overlayPath);
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Union failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    expectGpkgSpatialIndexUsable(outLayer, 3, 0.0, 0.0, 16.0, 12.0, output);
    GDALClose(outDs);
}

// 夹具复用 VectorDifferenceKeepsLinearGeometryOutput：线 (0,0)-(200,0) 减去 [80,120]×[-20,20]
// ⇒ 一条 MULTILINESTRING（两段，仍是一个输入要素的差集）⇒ 期望 1；
// 包框 = 源线的包框在 y 方向按裁剪面半高外扩 (0,-20,200,20)。
TEST_F(PluginTest, VectorDifferenceGpkgSpatialIndexIsPopulated) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    std::string input = (getTestDir() / "e2e_difference_line_input.shp").string();
    std::string overlay = (getTestDir() / "e2e_difference_line_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_difference_index_output.gpkg").string();

    auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
    ASSERT_NE(driver, nullptr);
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);

    {
        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto* layer1 = ds1->CreateLayer("input", srs.get(), wkbLineString);
        ASSERT_NE(layer1, nullptr);
        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer1->CreateField(&nameField), OGRERR_NONE);
        auto* feat1 = OGRFeature::CreateFeature(layer1->GetLayerDefn());
        OGRLineString line;
        line.addPoint(0, 0);
        line.addPoint(200, 0);
        feat1->SetGeometry(&line);
        feat1->SetField("name", "road");
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);
    }
    {
        auto* ds2 = driver->Create(overlay.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto* layer2 = ds2->CreateLayer("overlay", srs.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);
        auto* feat2 = OGRFeature::CreateFeature(layer2->GetLayerDefn());
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

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Difference failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    expectGpkgSpatialIndexUsable(outLayer, 1, 0.0, -20.0, 200.0, 20.0, output);
    GDALClose(outDs);
}

// 夹具复用 VectorIntersectOutputsOverlappedPolygon：[0,100]² ∩ [50,150]²
// ⇒ 1 个要素、结果包框正是两夹具正方形的交 (50,50,100,100)。
TEST_F(PluginTest, VectorIntersectGpkgSpatialIndexIsPopulated) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    std::string input = (getTestDir() / "e2e_intersect_polygon_input.shp").string();
    std::string overlay = (getTestDir() / "e2e_intersect_polygon_overlay.shp").string();
    std::string output = (getTestDir() / "e2e_intersect_index_output.gpkg").string();

    auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
    ASSERT_NE(driver, nullptr);
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);

    {
        auto* ds1 = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds1, nullptr);
        auto* layer1 = ds1->CreateLayer("input", srs.get(), wkbPolygon);
        ASSERT_NE(layer1, nullptr);
        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer1->CreateField(&nameField), OGRERR_NONE);
        auto* feat1 = OGRFeature::CreateFeature(layer1->GetLayerDefn());
        OGRPolygon poly1;
        OGRLinearRing ring1;
        ring1.addPoint(0, 0); ring1.addPoint(100, 0); ring1.addPoint(100, 100);
        ring1.addPoint(0, 100); ring1.addPoint(0, 0);
        poly1.addRing(&ring1);
        feat1->SetGeometry(&poly1);
        feat1->SetField("name", "zone_a");
        ASSERT_EQ(layer1->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);
        GDALClose(ds1);
    }
    {
        auto* ds2 = driver->Create(overlay.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds2, nullptr);
        auto* layer2 = ds2->CreateLayer("overlay", srs.get(), wkbPolygon);
        ASSERT_NE(layer2, nullptr);
        auto* feat2 = OGRFeature::CreateFeature(layer2->GetLayerDefn());
        OGRPolygon poly2;
        OGRLinearRing ring2;
        ring2.addPoint(50, 50); ring2.addPoint(150, 50); ring2.addPoint(150, 150);
        ring2.addPoint(50, 150); ring2.addPoint(50, 50);
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

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Intersect failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    expectGpkgSpatialIndexUsable(outLayer, 1, 50.0, 50.0, 100.0, 100.0, output);
    GDALClose(outDs);
}

// ── C3m 反回潮守卫：src/plugins 里"把 GPKG 空间索引从驱动手里拿走"的两种写法都不许出现 ──────
// 算式：把 ${GIS_SOURCE_DIR}/src/plugins 递归下来的每个 *.inc 与 *.cpp **全文**读进来，
//       在**ASCII 大小写折叠后**的副本上数两个 token 的出现次数，断言合计 == 0；命中时逐条点名
//       "文件:行号 + 命中的是哪个 token"。
// token 一（图层选项形）：spatial_index ⇒ 拦"发 SPATIAL_INDEX=NO 这个图层选项"。
//   为什么要折叠大小写再比：GDAL 取图层选项名走 CSLFetchNameValue（**名字大小写不敏感**）、
//   值走 CPLTestBool（"NO"/"no"/"OFF" 同样判假）⇒ 只数大写形的守卫会被 `spatial_index=no` 一字之差绕过。
// token 二（补建形）：gpkgaddspatialindex ⇒ 拦"写完再调用那个只建表和触发器、不回填既有行的 SQL 函数"
//   （C3l 删掉的就是它；它不带任何图层选项，所以 token 一看不见它，必须单独数）。
// 扫描范围：src/plugins/**/*.inc 与 src/plugins/**/*.cpp（开工现读 42 个文件，其中 vector 目录 30 个），
//       注释行与字符串字面量一起算，**不做任何"排除注释"的行过滤**。
// WHY 连注释也数：这条要防的是"有人又把驱动建索引关掉"，而写回去的人多半会顺手留一句解释为什么
//       关掉的注释 ⇒ 只数代码行的话，那处回潮会被它自己的注释洗成"设计结论的文档"。
// ⚠ 论域就是 src/plugins：`tests/` 与 `docs/` 里出现这两个 token 是**本守卫与台账自己在解释它**，
//   不算回潮 ⇒ 台账口径要写"src/plugins 0 命中"，不许写成"全库 0 命中"。
// ⚠ 0 命中**不是性能结论**：索引开销的量级在 N138 的取证里已经现读过（22 855 要素多约 1 s、
//   文件多约 2.5 MB）。这条是 N138 那条设计结论的守门 —— 产物形态由 expectGpkgSpatialIndexUsable
//   判，本条只判"写法不许回潮"。
// ⚠ 本条拦不住的：不提这两个名字、自己拼 `EXECUTE SQL` 建一张 rtree 表 ⇒ 源码里没有可数的形，
//   这种回潮只有产物侧的 ③④ 两条（expectGpkgSpatialIndexUsable）能看出来。
// 判据自带的三道"不许空跑"闸：扫描根不存在 ⇒ 红；扫到的文件数 < 30 ⇒ 红；
//   没扫到 vector_plugin_common.inc（= 射程没真正够到那 17 个走 builder 的写出点所在目录）⇒ 红。
#ifndef GIS_SOURCE_DIR
#define GIS_SOURCE_DIR "."
#endif

TEST_F(PluginTest, VectorSourceNeverDisablesGpkgSpatialIndex) {
    const std::vector<std::string> needles = {"spatial_index", "gpkgaddspatialindex"};
    const fs::path pluginsDir = fs::u8path(std::string(GIS_SOURCE_DIR) + "/src/plugins");
    ASSERT_TRUE(fs::exists(pluginsDir))
        << "扫描根不存在 ⇒ 这条守卫没在扫任何东西，判红而不是放过：" << pluginsDir.u8string();

    std::size_t scannedFiles = 0;
    bool sawCommonInc = false;
    std::size_t hits = 0;
    std::string offenderList;

    std::error_code ec;
    fs::recursive_directory_iterator it(pluginsDir, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        const fs::directory_entry entry = *it;
        std::error_code secer;
        if (!entry.is_regular_file(secer) || secer) {
            continue;
        }
        const fs::path path = entry.path();
        const std::string ext = path.extension().u8string();
        if (ext != ".inc" && ext != ".cpp") {
            continue;
        }
        ++scannedFiles;
        if (path.filename().u8string() == "vector_plugin_common.inc") {
            sawCommonInc = true;
        }

        std::ifstream in(path, std::ios::binary);
        ASSERT_TRUE(in.is_open()) << "扫描射程内的文件读不开，判据无法覆盖它：" << path.u8string();
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        // ASCII 折叠（只动 A-Z，UTF-8 多字节序列的首字节都 >= 0xC0、不会被改）⇒ 长度与换行位置不变，
        // 行号仍按原文算。
        std::string folded = text;
        for (char& ch : folded) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
        for (const std::string& needle : needles) {
            std::size_t pos = 0;
            while ((pos = folded.find(needle, pos)) != std::string::npos) {
                ++hits;
                const std::size_t line =
                    1 + static_cast<std::size_t>(std::count(text.begin(), text.begin() + pos, '\n'));
                offenderList += "\n  " + path.u8string() + ":" + std::to_string(line) +
                    " （命中 token: " + needle + "）";
                pos += needle.size();
            }
        }
    }
    EXPECT_FALSE(bool(ec)) << "目录递归中断于：" << ec.message();

    ASSERT_GE(scannedFiles, 30u)
        << "只扫到 " << scannedFiles << " 个 *.inc|*.cpp ⇒ 判据在空跑（开工现读是 42 个），"
        << "扫描根是：" << pluginsDir.u8string();
    EXPECT_TRUE(sawCommonInc)
        << "扫描集合里没有 vector_plugin_common.inc ⇒ 射程没够到那 17 个走 builder 的写出点所在的目录，"
        << "这条守卫等于没写；扫描根是：" << pluginsDir.u8string();

    EXPECT_EQ(hits, 0u)
        << "在 src/plugins 全文（含注释与字符串字面量、大小写折叠后）数到 " << hits
            << " 处 forbidden token ⇒ 有人又把 GeoPackage 的空间索引从驱动手里拿走了"
               "（发 `SPATIAL_INDEX=NO` 那一类，或写完再调 GDAL 那个只建表和触发器、不回填既有行的补建函数那一类）。"
               "17 个走 builder 的写出点必须一律发零个图层选项、让 GDAL 在 FlushCache 里延迟建索引并一次性回填"
               "（N138：先关索引批量写、写完再补的那条路径交出的必是『扩展项已登记、rtree 0 行』的产物，"
               "而包框真的盖住数据时读端按包框数出来就是 0 个要素）。"
               "回潮点：" << offenderList;
}

// C3c-A（convert 的 A 半边）：输出格式/层名/要素数/字段保真/几何坐标逐点相等。
// 期望值全部按夹具手推：字段名与顺序 name→code→ratio、code=1..3、ratio=0.25/0.5/0.75、
// 坐标 (116.00,40.00)/(116.01,40.02)/(116.03,40.01)。三个点不关于 y=x 对称
// ⇒ "把 x 与 y 写反"这种转置产物会逐点红（只比面积或包框抓不到）。
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
        OGRFieldDefn nameField("name", OFTString);
        ASSERT_EQ(layer->CreateField(&nameField), OGRERR_NONE);
        OGRFieldDefn codeField("code", OFTInteger);
        ASSERT_EQ(layer->CreateField(&codeField), OGRERR_NONE);
        OGRFieldDefn ratioField("ratio", OFTReal);
        ASSERT_EQ(layer->CreateField(&ratioField), OGRERR_NONE);
        const double fixtureX[3] = {116.0, 116.01, 116.03};
        const double fixtureY[3] = {40.0, 40.02, 40.01};
        auto* featDefn = layer->GetLayerDefn();
        for (int i = 0; i < 3; ++i) {
            auto* feat = OGRFeature::CreateFeature(featDefn);
            OGRPoint pt(fixtureX[i], fixtureY[i]);
            feat->SetGeometry(&pt);
            const std::string name = "p" + std::to_string(i);
            feat->SetField("name", name.c_str());
            feat->SetField("code", i + 1);
            feat->SetField("ratio", 0.25 * (i + 1));
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
    ASSERT_TRUE(result.success) << "Convert failed: " << result.message;
    EXPECT_EQ(result.message, "Convert completed: 3 features converted to GeoJSON");
    ASSERT_EQ(result.metadata.at("feature_count"), "3");
    ASSERT_EQ(result.metadata.at("output_format"), "GeoJSON");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    ASSERT_NE(outDs->GetDriver(), nullptr);
    EXPECT_STREQ(outDs->GetDriver()->GetDescription(), "GeoJSON");
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    // convert 把源层名原样带进输出；本夹具的源是 shapefile，层名 = 输入文件 stem。
    EXPECT_STREQ(outLayer->GetName(), "e2e_convert_input");
    EXPECT_EQ(outLayer->GetFeatureCount(FALSE), 3);

    OGRFeatureDefn* outDefn = outLayer->GetLayerDefn();
    ASSERT_EQ(outDefn->GetFieldCount(), 3);
    EXPECT_STREQ(outDefn->GetFieldDefn(0)->GetNameRef(), "name");
    EXPECT_STREQ(outDefn->GetFieldDefn(1)->GetNameRef(), "code");
    EXPECT_STREQ(outDefn->GetFieldDefn(2)->GetNameRef(), "ratio");

    const char* expectedName[3] = {"p0", "p1", "p2"};
    const double expectedX[3] = {116.0, 116.01, 116.03};
    const double expectedY[3] = {40.0, 40.02, 40.01};
    const double expectedRatio[3] = {0.25, 0.5, 0.75};

    std::set<int> seenCodes;
    outLayer->ResetReading();
    OGRFeature* outFeat = nullptr;
    while ((outFeat = outLayer->GetNextFeature()) != nullptr) {
        const int code = outFeat->GetFieldAsInteger("code");
        ASSERT_GE(code, 1) << "code 越界: " << code;
        ASSERT_LE(code, 3) << "code 越界: " << code;
        EXPECT_TRUE(seenCodes.insert(code).second) << "code 重复: " << code;
        EXPECT_EQ(std::string(outFeat->GetFieldAsString("name")), expectedName[code - 1]);
        EXPECT_DOUBLE_EQ(outFeat->GetFieldAsDouble("ratio"), expectedRatio[code - 1]);
        OGRGeometry* geom = outFeat->GetGeometryRef();
        ASSERT_NE(geom, nullptr);
        ASSERT_EQ(wkbFlatten(geom->getGeometryType()), wkbPoint);
        OGRPoint* pt = geom->toPoint();
        ASSERT_NE(pt, nullptr);
        EXPECT_DOUBLE_EQ(pt->getX(), expectedX[code - 1]) << "code=" << code << " 的 x 被写反/搬移";
        EXPECT_DOUBLE_EQ(pt->getY(), expectedY[code - 1]) << "code=" << code << " 的 y 被写反/搬移";
        OGRFeature::DestroyFeature(outFeat);
    }
    EXPECT_EQ(seenCodes.size(), 3u) << "输出的 code 集合不是 {1,2,3}";

    OGREnvelope outExtent;
    ASSERT_EQ(outLayer->GetExtent(&outExtent, TRUE), OGRERR_NONE);
    EXPECT_DOUBLE_EQ(outExtent.MinX, 116.0);
    EXPECT_DOUBLE_EQ(outExtent.MinY, 40.0);
    EXPECT_DOUBLE_EQ(outExtent.MaxX, 116.03);
    EXPECT_DOUBLE_EQ(outExtent.MaxY, 40.02);

    GDALClose(outDs);
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

// 目标图层拒绝写入时，clip 不得二次释放已移交写者的几何；且写失败必须**硬失败**。
//
// 本用例的前提（必须成立才有覆盖价值）：输出图层被 resultLayerGeometryType(wkbPoint)
// 声明为 MULTIPOINT（vector_plugin_common.inc），clip 对点图层产出的是 POINT，
// 而 promoteGeometryToLayerType（vector_plugin_overlay_support.inc）不提升 Point
// ⇒ Shapefile 驱动拒绝写入，正好命中"移交后不再 delete"的释放分支。
//
// C3n-2c 的现读变化（N139 结构修，用户 2026-10-23 裁定）：改前驱动拒绝被吞成
// "写出 0 条、动作仍 success=true"；改后 doClip 与 doBuffer 同形 —— 写失败 ⇒ 在事务里
// RollbackTransaction() 并返回 Result::fail("Failed to write clip feature")。
// ⇒ 原头注释里"Task 20 修正后把两条前提断言改成正向断言"的出口在这里兑现（不是删断言）：
//   ① 动作必须失败；② 失败消息点名写失败；③ 失败不留半成品（staged 契约；本臂输出原本不存在）；
//   ④ 驱动拒绝写入的日志仍被捕获（证明红/绿来自这条分支，而不是"因为别的原因失败"）；
//   ⑤ 同一动作再跑一遍仍是同一句干净失败（堆与文件句柄都还健康 —— 这才是本用例的原意）。
TEST_F(PluginTest, VectorClipRejectedFeatureWriteDoesNotCorruptHeap) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = a0MakePointLayer("a0_clip_reject_input.gpkg", 100.0, 100.0);
    const std::string clip = a0MakeClipPolygonLayer("a0_clip_reject_overlay.gpkg");
    const std::string output = utf8PathString(getTestDir() / "a0_clip_reject_output.shp");

    // 旧契约时代这条路径会留下一份 0 要素产物（.shp/.shx/.dbf/.prj）⇒ 先按 stem 清干净，
    // 否则"失败不留半成品"会拿到上一轮的假红（与 N129 那批夹具的清理口径一致）。
    {
        const fs::path outputPath = fs::u8path(output);
        std::error_code ec;
        for (fs::directory_iterator it(outputPath.parent_path(),
                 fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            std::error_code secer;
            if (it->is_regular_file(secer) && !secer && it->path().stem() == outputPath.stem()) {
                std::error_code rmec;
                fs::remove(it->path(), rmec);
            }
        }
    }

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

    // ③ 失败不留半成品：本臂的输出原本不存在，写失败后也不得冒出来。
    EXPECT_FALSE(fs::exists(output))
        << "N139：写失败后不许留下半成品（旧契约是交出 0 条要素的产物）";

    // ① + ② 写失败必须硬失败，且消息点名是"写 clip 要素"失败。
    EXPECT_FALSE(result.success)
        << "N139：驱动拒绝写入被吞成了 success=true（消息 = " << result.message << "）";
    EXPECT_NE(result.message.find("Failed to write clip feature"), std::string::npos)
        << "N139：失败消息必须点名写失败（与 doBuffer 的 Failed to write buffer feature 同族），"
           "实际 message = " << result.message;

    // ④ 前提锁：红/绿必须来自"驱动拒绝写入"这条分支。
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
           "本用例不再覆盖写失败分支，须重新找一条能让驱动拒绝写入的夹具（见用例头注释）";

    // ⑤ 堆若已被破坏（例如移交后二次释放），第二次执行通常在这里暴露。
    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        g_gdalErrors.clear();
    }
    CPLPushErrorHandler(captureGdalWarning);
    const auto again = p->execute(params, progress_);
    CPLPopErrorHandler();
    EXPECT_FALSE(again.success) << "Second clip pass unexpectedly succeeded: " << again.message;
    EXPECT_NE(again.message.find("Failed to write clip feature"), std::string::npos)
        << "Second clip pass failed for another reason: " << again.message;
    EXPECT_FALSE(fs::exists(output)) << "第二轮之后也不许留下半成品：" << output;
}

// 同样的所有权移交路径：difference 在写入被拒绝时必须硬失败、不得二次释放。
// C3n-2d 的现读变化（N139 同族修）：改前 doDifference 的三条写支都是
// `if (writeFeatureWithCopiedFields… ) { count++; … }` —— 驱动拒绝被吞成
// "写 0 条、动作仍 success=true"。改后与 C3n-2c 修好的 doClip 同形：写失败 ⇒
// 若在事务里 RollbackTransaction() ⇒ 返回 Result::fail("Failed to write difference feature")。
// 本用例照 VectorClipRejectedFeatureWriteDoesNotCorruptHeap 的五个出口写：
//   ① 动作必须失败；② 消息点名写失败；③ 失败不留半成品（本臂输出原本不存在）；
//   ④ 驱动拒绝写入的日志仍被捕获（证明红/绿来自这条分支）；⑤ 再跑一遍仍是同一句干净失败。
// 前提：输出图层被 resultLayerGeometryType(wkbPoint) 声明为 MULTIPOINT，difference 对点图层
// 产出 POINT，而 promoteGeometryToLayerType 不提升 Point ⇒ Shapefile 驱动拒绝写入。
// 注：输出是 .shp ⇒ useTransactions == false，RollbackTransaction 这一支本用例走不到
//（与 clip 那条同口径；GPKG 的回滚支仍零覆盖，登记在报告缺口）。
TEST_F(PluginTest, VectorDifferenceRejectedFeatureWriteDoesNotCorruptHeap) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = a0MakePointLayer("a0_diff_reject_input.gpkg", 1000.0, 1000.0);
    const std::string overlay = a0MakeClipPolygonLayer("a0_diff_reject_overlay.gpkg");
    const std::string output = utf8PathString(getTestDir() / "a0_diff_reject_output.shp");

    // 旧契约时代这条路径会留下一份 0 要素产物（.shp/.shx/.dbf/.prj）⇒ 先按 stem 清干净，
    // 否则"失败不留半成品"会拿到上一轮的假红（与 N129 那批夹具的清理口径一致）。
    {
        const fs::path outputPath = fs::u8path(output);
        std::error_code ec;
        for (fs::directory_iterator it(outputPath.parent_path(),
                 fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            std::error_code secer;
            if (it->is_regular_file(secer) && !secer && it->path().stem() == outputPath.stem()) {
                std::error_code rmec;
                fs::remove(it->path(), rmec);
            }
        }
    }

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

    // ③ 失败不留半成品：本臂的输出原本不存在，写失败后也不得冒出来。
    EXPECT_FALSE(fs::exists(output))
        << "N139：写失败后不许留下半成品（旧契约是交出 0 条要素的产物）";

    // ① + ② 写失败必须硬失败，且消息点名是"写 difference 要素"失败。
    EXPECT_FALSE(result.success)
        << "N139：驱动拒绝写入被吞成了 success=true（消息 = " << result.message << "）";
    EXPECT_NE(result.message.find("Failed to write difference feature"), std::string::npos)
        << "N139：失败消息必须点名写失败（与 Failed to write clip feature 同族），"
           "实际 message = " << result.message;

    // ④ 前提锁：红/绿必须来自"驱动拒绝写入"这条分支。
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
           "本用例不再覆盖写失败分支，须重新找一条能让驱动拒绝写入的夹具";

    // ⑤ 堆若已被破坏（例如移交后二次释放），第二次执行通常在这里暴露。
    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        g_gdalErrors.clear();
    }
    CPLPushErrorHandler(captureGdalWarning);
    const auto again = p->execute(params, progress_);
    CPLPopErrorHandler();
    EXPECT_FALSE(again.success) << "Second difference pass unexpectedly succeeded: " << again.message;
    EXPECT_NE(again.message.find("Failed to write difference feature"), std::string::npos)
        << "Second difference pass failed for another reason: " << again.message;
    EXPECT_FALSE(fs::exists(output)) << "第二轮之后也不许留下半成品：" << output;
}

// intersect 的同形真载体（C3n-2d）：
// 输入是落在叠加面内部的点 (100,100) ⇒ 交集产 POINT ⇒ MULTIPOINT 图层的 Shapefile 驱动拒绝写入。
// 改前 doIntersect 的两条写支只跳过、动作仍报 "Intersect completed: 0 features" 成功
// ⇒ 本用例必须先红；改成"写失败 ⇒ 回滚（若在事务里）⇒ Result::fail"后转绿。
TEST_F(PluginTest, VectorIntersectRejectedFeatureWriteDoesNotCorruptHeap) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = a0MakePointLayer("a0_intersect_reject_input.gpkg", 100.0, 100.0);
    const std::string overlay = a0MakeClipPolygonLayer("a0_intersect_reject_overlay.gpkg");
    const std::string output = utf8PathString(getTestDir() / "a0_intersect_reject_output.shp");

    {
        const fs::path outputPath = fs::u8path(output);
        std::error_code ec;
        for (fs::directory_iterator it(outputPath.parent_path(),
                 fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            std::error_code secer;
            if (it->is_regular_file(secer) && !secer && it->path().stem() == outputPath.stem()) {
                std::error_code rmec;
                fs::remove(it->path(), rmec);
            }
        }
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("intersect");
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

    EXPECT_FALSE(fs::exists(output))
        << "N139：写失败后不许留下半成品（旧契约是交出 0 条要素的产物）";
    EXPECT_FALSE(result.success)
        << "N139：驱动拒绝写入被吞成了 success=true（消息 = " << result.message << "）";
    EXPECT_NE(result.message.find("Failed to write intersect feature"), std::string::npos)
        << "N139：失败消息必须点名写失败（与 Failed to write clip feature 同族），"
           "实际 message = " << result.message;

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
           "本用例不再覆盖写失败分支，须重新找一条能让驱动拒绝写入的夹具";

    {
        std::lock_guard<std::mutex> lock(g_gdalWarningMutex);
        g_gdalErrors.clear();
    }
    CPLPushErrorHandler(captureGdalWarning);
    const auto again = p->execute(params, progress_);
    CPLPopErrorHandler();
    EXPECT_FALSE(again.success) << "Second intersect pass unexpectedly succeeded: " << again.message;
    EXPECT_NE(again.message.find("Failed to write intersect feature"), std::string::npos)
        << "Second intersect pass failed for another reason: " << again.message;
    EXPECT_FALSE(fs::exists(output)) << "第二轮之后也不许留下半成品：" << output;
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

    // 夹具是 (0,0)-(10,0.5)-(20,-0.5)-(30,0.4)-(40,0) 这条五点折线，三个中间点到弦 (0,0)-(40,0)
    // 的距离就是各自的 |y| ≤ 0.5。tolerance = 2.0 ⇒ 按 Douglas-Peucker 三个中间点全该被去掉，
    // 期望是**手算写死**的两点线 (0,0)-(40,0)、长度 40：原来这里只有 `getNumPoints() < 5`，
    // 那是一条"少一个点就通过"的弱判据 ⇒ 换成整串级的顶点坐标与长度断言。
    auto readOutputLine = [](const std::string& path) {
        GDALDataset* ds = static_cast<GDALDataset*>(GDALOpenEx(
            path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
        EXPECT_NE(ds, nullptr);
        if (!ds) {
            return std::vector<OGRPoint>{};
        }
        OGRLayer* layer = ds->GetLayer(0);
        EXPECT_NE(layer, nullptr);
        std::vector<OGRPoint> pts;
        if (layer) {
            OGRFeature* feat = layer->GetNextFeature();
            if (feat) {
                const OGRGeometry* geom = feat->GetGeometryRef();
                const OGRLineString* line = geom ? geom->toLineString() : nullptr;
                if (line) {
                    pts.reserve(static_cast<size_t>(line->getNumPoints()));
                    for (int i = 0; i < line->getNumPoints(); ++i) {
                        OGRPoint pt;
                        line->getPoint(i, &pt);
                        pts.push_back(pt);
                    }
                }
                OGRFeature::DestroyFeature(feat);
            }
        }
        GDALClose(ds);
        return pts;
    };

    const std::vector<OGRPoint> simplified = readOutputLine(output);
    ASSERT_EQ(simplified.size(), 2u) << "tolerance=2.0 应把三个中间点全部去掉";
    EXPECT_DOUBLE_EQ(simplified[0].getX(), 0.0);
    EXPECT_DOUBLE_EQ(simplified[0].getY(), 0.0);
    EXPECT_DOUBLE_EQ(simplified[1].getX(), 40.0);
    EXPECT_DOUBLE_EQ(simplified[1].getY(), 0.0);

    {
        GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
            output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
        ASSERT_NE(outDs, nullptr);
        OGRLayer* outLayer = outDs->GetLayer(0);
        ASSERT_NE(outLayer, nullptr);
        // 层名与 CRS 是"转换是否保真"这一半：simplify 不改层名、不改 CRS。
        EXPECT_STREQ(outLayer->GetName(), "roads");
        const OGRSpatialReference* outSrs = outLayer->GetSpatialRef();
        ASSERT_NE(outSrs, nullptr);
        const char* authCode = outSrs->GetAuthorityCode(nullptr);
        EXPECT_STREQ(authCode, "3857");
        const double expectedLength = 40.0;
        OGRFeature* outFeat = outLayer->GetNextFeature();
        ASSERT_NE(outFeat, nullptr);
        EXPECT_STREQ(outFeat->GetFieldAsString("name"), "main_road");
        // get_Length() 不挂在 OGRGeometry 基类上（同 :4553 那条理由）⇒ 长度要走判类下转再读。
        const OGRGeometry* outGeom = outFeat->GetGeometryRef();
        const OGRLineString* outLine = outGeom ? outGeom->toLineString() : nullptr;
        ASSERT_NE(outLine, nullptr) << "simplify 的产物必须是 LineString，否则长度判据无从读起";
        EXPECT_NEAR(outLine->get_Length(), expectedLength, 1e-9);
        OGRFeature::DestroyFeature(outFeat);
        GDALClose(outDs);
    }

    // 第二条容差臂：tolerance = 0.1 比三个中间点的偏移（0.5/0.5/0.4）都小 ⇒ 输出必是**原样的五点线**。
    // WHY 要这条：只有一条"容差很大"的臂时，"根本不做简化（原样 clone）"这种坏实现会**恰好**被上面那条
    // 抓住，但"把 tolerance 当成常数放大/缩小"这类坏实现不会改变 2.0 那臂的结果；两点合起来才同时
    // 抓住"没简化"和"容差不生效"两种坏法（判别结果见本刀报告）。
    const std::string tinyOutput = utf8PathString(getTestDir() / "e2e_simplify_tinytol_output.gpkg");
    std::map<std::string, gis::framework::ParamValue> tinyParams;
    tinyParams["action"] = std::string("simplify");
    tinyParams["input"] = input;
    tinyParams["output"] = tinyOutput;
    tinyParams["tolerance"] = 0.1;
    const auto tinyResult = p->execute(tinyParams, progress_);
    EXPECT_TRUE(tinyResult.success) << tinyResult.message;
    EXPECT_EQ(tinyResult.metadata.at("feature_count"), "1");
    const std::vector<OGRPoint> kept = readOutputLine(tinyOutput);
    ASSERT_EQ(kept.size(), 5u) << "tolerance=0.1 小于所有中间点偏移 ⇒ 五点必须全留";
    const double expectKeptX[5] = {0.0, 10.0, 20.0, 30.0, 40.0};
    const double expectKeptY[5] = {0.0, 0.5, -0.5, 0.4, 0.0};
    for (int i = 0; i < 5; ++i) {
        EXPECT_DOUBLE_EQ(kept[static_cast<size_t>(i)].getX(), expectKeptX[i]);
        EXPECT_DOUBLE_EQ(kept[static_cast<size_t>(i)].getY(), expectKeptY[i]);
    }
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
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.message, "Repair completed: 1 features");
    EXPECT_EQ(result.metadata.at("repaired_count"), "1");
    EXPECT_EQ(result.metadata.at("skipped_count"), "0");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(FALSE), 1);
    OGRFeature* outFeat = outLayer->GetNextFeature();
    ASSERT_NE(outFeat, nullptr);
    EXPECT_STREQ(outFeat->GetFieldAsString("name"), "invalid");
    OGRGeometry* outGeom = outFeat->GetGeometryRef();
    ASSERT_NE(outGeom, nullptr);
    // IsValid 单独一条：它是"坏几何已修"的必要条件，不是修复后真值。
    EXPECT_TRUE(outGeom->IsValid());
    EXPECT_FALSE(outGeom->IsEmpty());

    // C3c-A：修复后的真值按夹具手算。
    //   输入蝴蝶结 (0,0)→(50,50)→(50,0)→(0,50)→(0,0) 自交于 (25,25)，鞋带面积为 0；
    //   MakeValid 把它拆成两个三角形 (0,0)/(25,25)/(0,50) 与 (25,25)/(50,0)/(50,50)，
    //   每个面积 625、合计 1250；每个外环 4 个点（三角形闭合）。
    //   顶点集合按**无序去重**比：MakeValid 的起始点/环方向不是契约，坐标与面积才是。
    ASSERT_EQ(wkbFlatten(outGeom->getGeometryType()), wkbMultiPolygon)
        << "修复产物不是 MultiPolygon: " << OGRGeometryTypeToName(outGeom->getGeometryType());
    OGRMultiPolygon* repaired = outGeom->toMultiPolygon();
    ASSERT_NE(repaired, nullptr);
    EXPECT_EQ(repaired->getNumGeometries(), 2);
    EXPECT_DOUBLE_EQ(repaired->get_Area(), 1250.0);

    std::set<std::pair<double, double>> vertices;
    for (int i = 0; i < repaired->getNumGeometries(); ++i) {
        OGRPolygon* part = repaired->getGeometryRef(i)->toPolygon();
        ASSERT_NE(part, nullptr) << "第 " << i << " 个部件不是 Polygon";
        ASSERT_NE(part->getExteriorRing(), nullptr);
        EXPECT_EQ(part->getExteriorRing()->getNumPoints(), 4)
            << "第 " << i << " 个部件的外环不是闭合三角形";
        EXPECT_DOUBLE_EQ(part->get_Area(), 625.0);
        for (int j = 0; j < part->getExteriorRing()->getNumPoints(); ++j) {
            vertices.emplace(part->getExteriorRing()->getX(j), part->getExteriorRing()->getY(j));
        }
    }
    const std::vector<std::pair<double, double>> expectedVertices = {
        {0.0, 0.0}, {0.0, 50.0}, {25.0, 25.0}, {50.0, 0.0}, {50.0, 50.0}};
    ASSERT_EQ(vertices.size(), expectedVertices.size()) << "修复后顶点集合大小不是 5";
    for (const auto& expected : expectedVertices) {
        EXPECT_TRUE(vertices.count(expected) == 1)
            << "缺少手算顶点 (" << expected.first << ", " << expected.second << ")";
    }

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

// ============== C3b-B1：拓扑/检查四条的 CSV 拆列判据公共件 ==============
// 工单（.superpowers/sdd/2026-10-21-批次C全功能验证与文档同步/task-C3b-brief.md §C3b-B1）把这四条
// （overlap_check / topology_check / adjacency / dangling_endpoint_check）的判据从"子串包含"
// 升级成"表头整串相等 + 数据行先按 ',' 拆列再逐列比较 + 数值列解析后比"。
// WHY 不许再用 find(",50") 这类子串：子串对**列位**、**基数**和**邻近值**三件事都不敏感 ——
//   "50" 写在 overlap_area 还是写在 target_fid 里、行里多一列还是少一列、"150" 或 "500"
//   这类值全都命中同一个子串；于是"面积列写死 50"这种变异测不出来（工单点名的正是这个病）。
//   拆列以后每一列有自己的位置与名字，红了当场说得出是哪一列歪了。
// WHY 解析失败不许"以崩代红"：std::stod / std::stoll 抛异常在 gtest 里算 crash（用例被中断、
//   消息不可读、退出码非 0），那不是干净的红。下面一律走 std::from_chars：它不抛异常，
//   并且**要求整列被消费完**（res.ptr == last），所以 "50x" 这种部分匹配同样判失败。
//   失败时 value 填 quiet_NaN / int64 的最小值：EXPECT_NEAR 遇 NaN 恒为假、EXPECT_EQ 遇哨兵恒为假，
//   于是即便调用点忘了先看 ok 标志，也绝不会因为"失败值恰好等于期望 0.0"而假绿。

struct CsvDoubleColumn {
    bool ok = false;
    double value = std::numeric_limits<double>::quiet_NaN();
    std::string text;  // 列原文；列不存在时是一个点名用的占位串
};

struct CsvInt64Column {
    bool ok = false;
    long long value = std::numeric_limits<long long>::min();
    std::string text;
};

// 按 ',' 把一行 CSV 拆成列：不修剪空白、不吞空列。
// WHY 不吞空列：topology_check 的 empty/invalid 行形如 `fid,,empty_geometry,0`，第二列就是空的；
//   吃掉空串会让后面每一列整体左移，"列没写歪"这件事反而被判据自己破坏了。
static std::vector<std::string> splitCsvColumns(const std::string& line) {
    std::vector<std::string> columns;
    std::string current;
    for (const char ch : line) {
        if (ch == ',') {
            columns.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    columns.push_back(current);  // 末列没有尾随逗号，也要照样产出来
    return columns;
}

// 列不存在（拆出来没那么多列）时的点名文本：红了要能一眼看出"少了一列"而不是"值不对"。
static std::string missingCsvColumnText(std::size_t index) {
    return "<第 " + std::to_string(index) + " 列不存在>";
}

static CsvDoubleColumn csvColumnAsDouble(const std::vector<std::string>& columns,
                                         std::size_t index) {
    CsvDoubleColumn out;
    if (index >= columns.size()) {
        out.text = missingCsvColumnText(index);
        return out;
    }
    out.text = columns[index];
    const char* first = out.text.data();
    const char* last = first + out.text.size();
    double parsed = 0.0;
    const std::from_chars_result result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc() || result.ptr != last) {
        return out;  // 空列 / 非数字 / 部分匹配 / 越界：ok 保持 false，哨兵值保持 NaN
    }
    out.value = parsed;
    out.ok = true;
    return out;
}

static CsvInt64Column csvColumnAsInt64(const std::vector<std::string>& columns,
                                       std::size_t index) {
    CsvInt64Column out;
    if (index >= columns.size()) {
        out.text = missingCsvColumnText(index);
        return out;
    }
    out.text = columns[index];
    const char* first = out.text.data();
    const char* last = first + out.text.size();
    long long parsed = 0;
    const std::from_chars_result result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc() || result.ptr != last) {
        return out;
    }
    out.value = parsed;
    out.ok = true;
    return out;
}

// 把拆好的列 + 解析结果打包成一条可读的失败上下文（"第几列叫什么、原文行长什么样"）。
static std::string csvColumnContext(const std::vector<std::string>& columns, std::size_t index,
                                    const std::string& rawLine) {
    std::string note = "列数=" + std::to_string(columns.size()) +
        " 列下标=" + std::to_string(index) + " 原文行=[" + rawLine + "]";
    if (index < columns.size()) {
        note += " 该列原文=[" + columns[index] + "]";
    }
    return note;
}

// 写完输入 GPKG 之后**重新打开输入层**，按 GetNextFeature 的顺序现读每条要素的 GetFID()。
// WHY 必须从输入侧导出（控制器的裁定 R-B1-fid）：期望值若从产出的 CSV 回填，等于"实现写什么
//   就认什么"，列写歪、配对写反、甚至把同一对写两遍都可能被自洽地绿过去。
// WHY 不照抄"GeoJSON 的 FID 从 1 起"那句旧注释：那条证的是 GeoJSON 驱动，**不是 GPKG**。
//   本函数只把驱动实际给的 FID 读出来用，不对"从 0 还是从 1 起"做任何假设。
static std::vector<long long> readLayerFidsInCreationOrder(const std::string& path) {
    std::vector<long long> fids;
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpenEx(
        path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    if (!ds) {
        return fids;
    }
    OGRLayer* layer = ds->GetLayer(0);
    if (layer) {
        layer->ResetReading();
        OGRFeature* feat = nullptr;
        while ((feat = layer->GetNextFeature()) != nullptr) {
            fids.push_back(static_cast<long long>(feat->GetFID()));
            OGRFeature::DestroyFeature(feat);
        }
    }
    GDALClose(ds);
    return fids;
}

// 逐行读 CSV（保留行数与每行原文；拆列由调用点负责）。
static std::vector<std::string> readCsvLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream ifs(fs::u8path(path));
    if (!ifs.is_open()) {
        return lines;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();  // 行尾归一化：只在**行尾**，绝不用 replace("",",")
        }
        lines.push_back(line);
    }
    return lines;
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

        // 三个矩形**全部写成闭合环**（makeRect 的第 5 个点回到起点），EPSG:3857 投影 ⇒ 不撞投影闸。
        // 逐对关系（工单要求"矩形两两关系必须逐对算，不能只看想测的那一对"）：
        //   P1 [0,10]×[0,10] ∩ P2 [10,20]×[0,10]：共竖边 x=10, y∈[0,10] ⇒ Touches 真、
        //     交集是线 ⇒ shared_length = 10.0；
        //   P1 ∩ P3 [-10,0]×[10,20]：只在点 (0,10) 相遇（P1 上边 y=10,x∈[0,10] 与
        //     P3 下边 y=10,x∈[-10,0] 的交集是单点）⇒ Touches 真、交集不是线 ⇒ **0.0 但行照写**；
        //   P2 ∩ P3：P2 的 x 从 10 起、P3 到 0 止 ⇒ 不相邻 ⇒ 无行。
        // ⇒ relation_count = 2、CSV = 1 表头 + 2 数据行，两行长度分别是 10.0 与 0.0。
        // ⚠ P3 不能写成 [10,20]×[10,20]：那样它会与 P2 共横边 y=10,x∈[10,20]（又一段 10.0），
        //   关系数变 3，"点接触 ⇒ 0.0"这条新增语义覆盖就被稀释掉了。
        auto* feat1 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly1 = makeRect(0, 0, 10, 10);   // P1
        feat1->SetGeometry(&poly1);
        ASSERT_EQ(layer->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);

        auto* feat2 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly2 = makeRect(10, 0, 20, 10);  // P2
        feat2->SetGeometry(&poly2);
        ASSERT_EQ(layer->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);

        auto* feat3 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly3 = makeRect(-10, 10, 0, 20);  // P3（工单定稿；旧夹具的 [30,40]×[0,10] 谁都不挨）
        feat3->SetGeometry(&poly3);
        ASSERT_EQ(layer->CreateFeature(feat3), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat3);
        GDALClose(ds);
    }

    // R-B1-fid：期望的 FID 一律从**输入侧**现读（重新打开刚落盘的输入层，按创建顺序取 GetFID()），
    // 不许从产出的 CSV 回填，也不假设"GPKG 的 FID 从 1 起"。
    const std::vector<long long> fids = readLayerFidsInCreationOrder(input);
    ASSERT_EQ(fids.size(), 3u)
        << "adjacency 夹具创建了 3 个矩形，重新打开输入层却没读回 3 个 FID";

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("adjacency");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("relation_count"), "2");

    const std::vector<std::string> lines = readCsvLines(output);
    ASSERT_EQ(lines.size(), 3u) << "adjacency CSV 应为 1 表头 + 2 数据行";
    EXPECT_EQ(lines[0], "source_fid,target_fid,shared_length");

    // 实现按 (i,j) 上三角、按读入顺序写行 ⇒ 第 1 行是 (P1,P2)、第 2 行是 (P1,P3)。
    // 旧判据是 `lines[1].find(",10")`：那一个子串既钉不住列位（target_fid=10 也命中）、
    // 也钉不住基数（多写一行也命中），现在整段删掉，改成拆列逐列比。
    const std::vector<std::string> row1 = splitCsvColumns(lines[1]);
    ASSERT_EQ(row1.size(), 3u) << "adjacency 第 1 数据行列数不是 3: " << csvColumnContext(row1, 0, lines[1]);
    const CsvInt64Column row1Source = csvColumnAsInt64(row1, 0);
    EXPECT_TRUE(row1Source.ok) << "source_fid 列不是整数: " << csvColumnContext(row1, 0, lines[1]);
    EXPECT_EQ(row1Source.value, fids[0]) << "source_fid 不是 P1: " << csvColumnContext(row1, 0, lines[1]);
    const CsvInt64Column row1Target = csvColumnAsInt64(row1, 1);
    EXPECT_TRUE(row1Target.ok) << "target_fid 列不是整数: " << csvColumnContext(row1, 1, lines[1]);
    EXPECT_EQ(row1Target.value, fids[1]) << "target_fid 不是 P2: " << csvColumnContext(row1, 1, lines[1]);
    const CsvDoubleColumn row1Length = csvColumnAsDouble(row1, 2);
    EXPECT_TRUE(row1Length.ok) << "shared_length 列不是数字: " << csvColumnContext(row1, 2, lines[1]);
    // 共竖边 x=10、y 从 0 到 10 ⇒ 长度 = 10 - 0 = 10.0。
    EXPECT_NEAR(10.0, row1Length.value, 1e-6)
        << "P1-P2 共边长应为 10.0: " << csvColumnContext(row1, 2, lines[1]);

    const std::vector<std::string> row2 = splitCsvColumns(lines[2]);
    ASSERT_EQ(row2.size(), 3u) << "adjacency 第 2 数据行列数不是 3: " << csvColumnContext(row2, 0, lines[2]);
    const CsvInt64Column row2Source = csvColumnAsInt64(row2, 0);
    EXPECT_TRUE(row2Source.ok) << "source_fid 列不是整数: " << csvColumnContext(row2, 0, lines[2]);
    EXPECT_EQ(row2Source.value, fids[0]) << "source_fid 不是 P1: " << csvColumnContext(row2, 0, lines[2]);
    const CsvInt64Column row2Target = csvColumnAsInt64(row2, 1);
    EXPECT_TRUE(row2Target.ok) << "target_fid 列不是整数: " << csvColumnContext(row2, 1, lines[2]);
    EXPECT_EQ(row2Target.value, fids[2]) << "target_fid 不是 P3: " << csvColumnContext(row2, 1, lines[2]);
    const CsvDoubleColumn row2Length = csvColumnAsDouble(row2, 2);
    EXPECT_TRUE(row2Length.ok) << "shared_length 列不是数字: " << csvColumnContext(row2, 2, lines[2]);
    // 点接触 (0,10)：交集不是线 ⇒ sharedBoundaryLength 落到 total = 0.0，**但行照写**。
    // WHY 这条 0.0 是新语义覆盖：全仓此前没有点接触用例，而"只在长度非零时才写行"这种
    // 优化会把这一行整条吞掉 ⇒ 关系数与基数断言一起红。
    EXPECT_NEAR(0.0, row2Length.value, 1e-6)
        << "P1-P3 点接触应写成 0.0 而不是被跳过: " << csvColumnContext(row2, 2, lines[2]);
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

        // F1 = (0,0)-(10,10)、F2 = (5,0)-(15,10)、F3 = (0,0)-(4,4)，全部 EPSG:3857 ⇒ 不撞投影闸。
        // 三对逐算（工单定稿）：
        //   F1∩F2：x 重叠 [5,10]（宽 5）、y 重叠 [0,10]（高 10）⇒ 交集 5×10 = 50；
        //   F1∩F3：F3 整个在 F1 内部 ⇒ 交集就是 F3 自己 4×4 = 16；
        //   F2∩F3：F2 的 x 从 5 起、F3 到 4 止 ⇒ 不相交 ⇒ 无行。
        // ⇒ relation_count = 2、CSV = 1 表头 + 2 数据行，两行面积分别是 50.0 与 16.0。
        // ⚠ **必须有两对不同面积**：只有一对（旧夹具就是只有一对，第三个矩形 [30,40]×[0,10]
        //   谁都不挨）的话，"面积列写死 50"这类变异测不出 —— 旧判据 find(",50") 正是这个病。
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
        OGRPolygon poly3 = makeRect(0, 0, 4, 4);
        feat3->SetGeometry(&poly3);
        ASSERT_EQ(layer->CreateFeature(feat3), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat3);
        GDALClose(ds);
    }

    // R-B1-fid：期望 FID 从输入侧现读，不从产出的 CSV 回填。
    const std::vector<long long> fids = readLayerFidsInCreationOrder(input);
    ASSERT_EQ(fids.size(), 3u)
        << "overlap_check 夹具创建了 3 个矩形，重新打开输入层却没读回 3 个 FID";

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("overlap_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("relation_count"), "2");

    const std::vector<std::string> lines = readCsvLines(output);
    ASSERT_EQ(lines.size(), 3u) << "overlap_check CSV 应为 1 表头 + 2 数据行";
    EXPECT_EQ(lines[0], "source_fid,target_fid,overlap_area");

    // 实现按 (i,j) 上三角、按读入顺序写行，且 `overlapArea > 0.0` 才写 ⇒
    // 第 1 行 (F1,F2)、第 2 行 (F1,F3)。旧判据是 `lines[1].find(",50")`，整段删掉换拆列断言。
    const std::vector<std::string> row1 = splitCsvColumns(lines[1]);
    ASSERT_EQ(row1.size(), 3u) << "overlap_check 第 1 数据行列数不是 3: " << csvColumnContext(row1, 0, lines[1]);
    const CsvInt64Column row1Source = csvColumnAsInt64(row1, 0);
    EXPECT_TRUE(row1Source.ok) << "source_fid 列不是整数: " << csvColumnContext(row1, 0, lines[1]);
    EXPECT_EQ(row1Source.value, fids[0]) << "source_fid 不是 F1: " << csvColumnContext(row1, 0, lines[1]);
    const CsvInt64Column row1Target = csvColumnAsInt64(row1, 1);
    EXPECT_TRUE(row1Target.ok) << "target_fid 列不是整数: " << csvColumnContext(row1, 1, lines[1]);
    EXPECT_EQ(row1Target.value, fids[1]) << "target_fid 不是 F2: " << csvColumnContext(row1, 1, lines[1]);
    const CsvDoubleColumn row1Area = csvColumnAsDouble(row1, 2);
    EXPECT_TRUE(row1Area.ok) << "overlap_area 列不是数字: " << csvColumnContext(row1, 2, lines[1]);
    // 算式：交集宽 10-5 = 5、高 10-0 = 10 ⇒ 5×10 = 50。
    EXPECT_NEAR(50.0, row1Area.value, 1e-6)
        << "F1∩F2 面积应为 5×10=50: " << csvColumnContext(row1, 2, lines[1]);

    const std::vector<std::string> row2 = splitCsvColumns(lines[2]);
    ASSERT_EQ(row2.size(), 3u) << "overlap_check 第 2 数据行列数不是 3: " << csvColumnContext(row2, 0, lines[2]);
    const CsvInt64Column row2Source = csvColumnAsInt64(row2, 0);
    EXPECT_TRUE(row2Source.ok) << "source_fid 列不是整数: " << csvColumnContext(row2, 0, lines[2]);
    EXPECT_EQ(row2Source.value, fids[0]) << "source_fid 不是 F1: " << csvColumnContext(row2, 0, lines[2]);
    const CsvInt64Column row2Target = csvColumnAsInt64(row2, 1);
    EXPECT_TRUE(row2Target.ok) << "target_fid 列不是整数: " << csvColumnContext(row2, 1, lines[2]);
    EXPECT_EQ(row2Target.value, fids[2]) << "target_fid 不是 F3: " << csvColumnContext(row2, 1, lines[2]);
    const CsvDoubleColumn row2Area = csvColumnAsDouble(row2, 2);
    EXPECT_TRUE(row2Area.ok) << "overlap_area 列不是数字: " << csvColumnContext(row2, 2, lines[2]);
    // 算式：F3 被 F1 完全包含 ⇒ 交集 = F3 自己 = 4×4 = 16。
    // WHY 两行面积必须不同（50 与 16）：同值时"把面积写成第一对的面积"这种变异没有着力点。
    EXPECT_NEAR(16.0, row2Area.value, 1e-6)
        << "F1∩F3 面积应为 4×4=16: " << csvColumnContext(row2, 2, lines[2]);
}

// ============== C3d（N125 销账）：混合维度交集不得被 overlap_check 漏报 ==============
// 工单：.superpowers/sdd/2026-10-21-批次C全功能验证与文档同步/task-C3d-brief.md
// 缺陷：overlap_check 曾在本地写一份"只认 Polygon / MultiPolygon"的交面积 switch，交集是
//   GEOMETRYCOLLECTION 时落 default 得 0.0 ⇒ `overlapArea > 0.0` 不成立 ⇒ 整行不写；
//   同一对要素在 topology_check 那侧走 common.inc 的 polygonIntersectionArea()（递归累加集合里的
//   子面）⇒ 报得出面积。两个动作对"交集"的算法口径不一致，真实省界上实测 overlap 报 0 条、
//   topology 报 1 条（12,14,overlap,401282）。本用例把这条分支单独钉住。
// 夹具（EPSG:3857，两个要素都必须是 MultiPolygon 成员 —— 两个单 Polygon 的交集不会跨维度，
//   构造不出集合，也就测不到这条分支）：
//   A = MultiPolygon{ A1 = [0,10]×[0,10], A2 = [20,25]×[20,26] }
//   B = MultiPolygon{ B1 = [5,15]×[5,15], B2 = [20,25]×[26,30] }
// 交集逐成员算（A 的两个成员 × B 的两个成员共 4 对）：
//   A1∩B1：x 重叠 [5,10]（宽 10-5 = 5）、y 重叠 [5,10]（高 10-5 = 5）⇒ POLYGON，面积 5×5 = 25；
//   A1∩B2：A1 的 y 到 10 止、B2 的 y 从 26 起 ⇒ EMPTY，不进集合；
//   A2∩B1：A2 的 x 从 20 起、B1 的 x 到 15 止 ⇒ EMPTY，不进集合；
//   A2∩B2：A2 的 y 到 26 止、B2 的 y 从 26 起 ⇒ 只在直线 y=26 上共边，x∈[20,25]
//     ⇒ LINESTRING（长 25-20 = 5，**面积 0**）。
// ⇒ A∩B 同时含面与线两种维度 ⇒ GEOS 返回 GEOMETRYCOLLECTION(POLYGON, LINESTRING)。
// 期望面积算式（线部分贡献 0，必须写清，否则"25"看着像抄来的）：
//   overlap_area = area(POLYGON) + area(LINESTRING) = 5×5 + 0 = 25。
// WHY 判据不许降级成"只要非 0"：那样"把面积写成常量 50""把集合里的线当成面算"这一族变异全绿；
//   本用例同时钉基数（relation_count 与行数）、FID 对号（拆列 + 从输入侧现读的 FID）与面积（25.0）。
TEST_F(PluginTest, VectorOverlapCheckMixedGeometryCollectionIntersection) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_overlap_check_mixed_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_overlap_check_mixed_output.csv");

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

    auto makeMultiPolygon = [&](const OGRPolygon& first, const OGRPolygon& second) {
        OGRMultiPolygon multi;
        multi.addGeometry(&first);
        multi.addGeometry(&second);
        return multi;
    };

    const OGRPolygon polyA1 = makeRect(0, 0, 10, 10);
    const OGRPolygon polyA2 = makeRect(20, 20, 25, 26);
    const OGRPolygon polyB1 = makeRect(5, 5, 15, 15);
    const OGRPolygon polyB2 = makeRect(20, 26, 25, 30);
    const OGRMultiPolygon multiA = makeMultiPolygon(polyA1, polyA2);
    const OGRMultiPolygon multiB = makeMultiPolygon(polyB1, polyB2);

    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        // 图层类型必须是 wkbMultiPolygon：overlap_check:25 的类型闸只放行 Polygon / MultiPolygon，
        // 而"两个 MultiPolygon 的交集跨维度"正是本用例要走的分支。
        auto* layer = ds->CreateLayer("parcels", srs.get(), wkbMultiPolygon, nullptr);
        ASSERT_NE(layer, nullptr);

        auto* featA = OGRFeature::CreateFeature(layer->GetLayerDefn());
        featA->SetGeometry(&multiA);
        ASSERT_EQ(layer->CreateFeature(featA), OGRERR_NONE);
        OGRFeature::DestroyFeature(featA);

        auto* featB = OGRFeature::CreateFeature(layer->GetLayerDefn());
        featB->SetGeometry(&multiB);
        ASSERT_EQ(layer->CreateFeature(featB), OGRERR_NONE);
        OGRFeature::DestroyFeature(featB);
        GDALClose(ds);
    }

    // R-B1-fid：期望 FID 从输入侧现读，不从产出的 CSV 回填。
    const std::vector<long long> fids = readLayerFidsInCreationOrder(input);
    ASSERT_EQ(fids.size(), 2u)
        << "overlap_check 混合交集夹具创建了 2 个 MultiPolygon 要素，重新打开输入层却没读回 2 个 FID";

    // ---- 夹具前提：从**文件里读回**的两个几何现算交集，必须是 GeometryCollection。----
    // WHY 从文件读回而不是用内存里那份 multiA/multiB：产品看到的就是磁盘上的几何，
    //   前提必须由同一份几何证明；GPKG 往返若改了维度（例如塌成 MultiPolygon），这条会当场红，
    //   而"产品断言"跟着红就分不清是配方没构造出来还是实现有问题。
    // WHY 把实际类型写进消息：工单第 3 条要求"实测类型不是 wkbGeometryCollection 就停下报实际类型"，
    //   红了要能直接读出那个类型，不必再跑第二遍探针。
    {
        GDALDataset* probeDs = static_cast<GDALDataset*>(GDALOpenEx(
            input.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
        ASSERT_NE(probeDs, nullptr);
        OGRLayer* probeLayer = probeDs->GetLayer(0);
        ASSERT_NE(probeLayer, nullptr);
        probeLayer->ResetReading();
        OGRFeature* first = probeLayer->GetNextFeature();
        OGRFeature* second = probeLayer->GetNextFeature();
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        std::unique_ptr<OGRGeometry> intersection(
            first->GetGeometryRef()->Intersection(second->GetGeometryRef()));
        OGRFeature::DestroyFeature(first);
        OGRFeature::DestroyFeature(second);
        ASSERT_NE(intersection, nullptr);
        const OGRwkbGeometryType flatType = wkbFlatten(intersection->getGeometryType());
        EXPECT_EQ(flatType, wkbGeometryCollection)
            << "夹具的 A∩B 实测几何类型是 " << intersection->getGeometryName()
            << "（" << static_cast<int>(flatType) << "），不是 wkbGeometryCollection ⇒ 配方没构造出混合维度";
        // 混合维度还得**真的**同时含有面成员与非面成员：只把集合里全换成面，default 分支同样测不到。
        if (flatType == wkbGeometryCollection) {
            OGRGeometryCollection* collection = intersection->toGeometryCollection();
            int polygonParts = 0;
            int nonPolygonParts = 0;
            for (int i = 0; i < collection->getNumGeometries(); ++i) {
                const OGRwkbGeometryType subType =
                    wkbFlatten(collection->getGeometryRef(i)->getGeometryType());
                if (subType == wkbPolygon || subType == wkbMultiPolygon) ++polygonParts;
                else ++nonPolygonParts;
            }
            EXPECT_GE(polygonParts, 1) << "集合里没有面子 ⇒ 修复后的面积无从计算";
            EXPECT_GE(nonPolygonParts, 1) << "集合里没有非面子 ⇒ 不是混合维度，测不到那条分支";
        }
        GDALClose(probeDs);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("overlap_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    // 修复前红在这里：本地 switch 落 default ⇒ 面积 0.0 ⇒ 整行不写 ⇒ relation_count 报 "0"。
    EXPECT_EQ(result.metadata.at("relation_count"), "1")
        << "混合维度交集必须算出面积并写出这一行（N125）";

    const std::vector<std::string> lines = readCsvLines(output);
    // 修复前 CSV 只有表头 ⇒ 这一条 1u vs 2u 是缺陷证据本身；修复后是 1 表头 + 1 数据行。
    ASSERT_EQ(lines.size(), 2u) << "overlap_check 混合交集 CSV 应为 1 表头 + 1 数据行";
    EXPECT_EQ(lines[0], "source_fid,target_fid,overlap_area");

    const std::vector<std::string> row1 = splitCsvColumns(lines[1]);
    ASSERT_EQ(row1.size(), 3u) << "overlap_check 第 1 数据行列数不是 3: " << csvColumnContext(row1, 0, lines[1]);
    const CsvInt64Column row1Source = csvColumnAsInt64(row1, 0);
    EXPECT_TRUE(row1Source.ok) << "source_fid 列不是整数: " << csvColumnContext(row1, 0, lines[1]);
    EXPECT_EQ(row1Source.value, fids[0]) << "source_fid 不是 A: " << csvColumnContext(row1, 0, lines[1]);
    const CsvInt64Column row1Target = csvColumnAsInt64(row1, 1);
    EXPECT_TRUE(row1Target.ok) << "target_fid 列不是整数: " << csvColumnContext(row1, 1, lines[1]);
    EXPECT_EQ(row1Target.value, fids[1]) << "target_fid 不是 B: " << csvColumnContext(row1, 1, lines[1]);
    const CsvDoubleColumn row1Area = csvColumnAsDouble(row1, 2);
    EXPECT_TRUE(row1Area.ok) << "overlap_area 列不是数字: " << csvColumnContext(row1, 2, lines[1]);
    // 算式：面部分 5×5 = 25、线部分贡献 0 ⇒ overlap_area = 25。
    EXPECT_NEAR(25.0, row1Area.value, 1e-6)
        << "GEOMETRYCOLLECTION(POLYGON, LINESTRING) 的面积应为 5×5+0=25: "
        << csvColumnContext(row1, 2, lines[1]);
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

        // 夹具（C3b-3 定稿）：F1 = (0,0)-(10,10)、F2 与 F1 **逐字节相同**、F3 = (5,0)-(15,10)、
        // 本刀新增 F4 = (0,0)-(2,4)，即矩形 [0,2]×[0,4]（面积 8）。
        // 四要素逐对算（实现按 i<j 的 fid 顺序遍历上三角 ⇒ 行序确定，见 topology_check 的双层循环）：
        //   (F1,F2) Equals 真 ⇒ 1 行 duplicate_geometry；实现是 `if / else if` **互斥**，
        //     所以这一对不会再写一条 overlap（把互斥拆成两个独立 if 就会多出一行，基数当场红）；
        //   (F1,F3) x 重叠 [5,10]（宽 5）、y 重叠 [0,10]（高 10）⇒ 5×10 = 50 ⇒ 1 行 overlap,50；
        //   (F1,F4) F4 整块落在 F1 内部 ⇒ 交集 = F4 自己 = 2×4 = 8 ⇒ 1 行 overlap,8；
        //   (F2,F3) F2 与 F1 逐字节相同 ⇒ 算式同 (F1,F3) ⇒ **又 1 行** overlap,50；
        //   (F2,F4) 同理 ⇒ **又 1 行** overlap,8；
        //   (F3,F4) F3 的 x 从 5 起、F4 的 x 到 2 止 ⇒ 两块不相交 ⇒ Intersects 假 ⇒ 无行。
        // ⇒ issue_count = 5、CSV = 1 表头 + 5 数据行，其中 duplicate_geometry 恰好 1 条、
        //   overlap 恰好 4 条，且 issue_value 的面积集合是 {50, 8} —— **两种值都在**。
        // WHY 非加 F4 不可（这一刀的存在理由，不是锦上添花）：改前只有 F1/F2/F3，两条 overlap 行的
        //   期望值都是 50.0 ⇒ "把面积写成常量 50.0"这类变异在**新臂也不会红**，等于把常数当期望值钉进库。
        //   加 F4 后同一个 issue_value 列同时承载 50.0 与 8.0：写死任一个常量都会红在另一行的拆列断言上，
        //   "把 overlap 行少写一条 / 多写一条"也会红在行数与计数上。
        // ⚠ empty_geometry / invalid_geometry 两类行**本切片不新增**：实现里 empty 与 invalid 是
        //   两个独立 `if`（可同写两行），而"自交面是否同时被判 invalid"没有算式依据 ⇒ 已挂 N123，
        //   这里不猜期望值。
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

        auto* feat4 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly4 = makeRect(0, 0, 2, 4);   // F4 = [0,2]×[0,4]，面积 8
        feat4->SetGeometry(&poly4);
        ASSERT_EQ(layer->CreateFeature(feat4), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat4);
        GDALClose(ds);
    }

    // R-B1-fid：期望 FID 从输入侧现读，不从产出的 CSV 回填。
    const std::vector<long long> fids = readLayerFidsInCreationOrder(input);
    ASSERT_EQ(fids.size(), 4u)
        << "topology_check 夹具创建了 4 个矩形，重新打开输入层却没读回 4 个 FID";

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("topology_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("issue_count"), "5");

    const std::vector<std::string> lines = readCsvLines(output);
    ASSERT_EQ(lines.size(), 6u) << "topology_check CSV 应为 1 表头 + 5 数据行";
    EXPECT_EQ(lines[0], "source_fid,target_fid,issue_type,issue_value");

    // 实现先写逐要素的 empty/invalid 行（本夹具一条都没有），再按 (i,j) 上三角写行
    // ⇒ 数据行顺序是 (F1,F2) duplicate、(F1,F3) overlap 50、(F1,F4) overlap 8、
    //   (F2,F3) overlap 50、(F2,F4) overlap 8；(F3,F4) 两块不相交 ⇒ 没有第 6 条数据行。
    // 旧判据是"对整行 find(\"duplicate_geometry\") / find(\"overlap,50\") 计数"：它既钉不住
    // FID 对（哪一对重复、哪一对重叠根本没记名），也钉不住列位（issue_type 与 issue_value 串味
    // 也能命中同一个子串），现在整段换成拆列逐列比。
    const std::vector<std::string> row1 = splitCsvColumns(lines[1]);
    ASSERT_EQ(row1.size(), 4u) << "topology_check 第 1 数据行列数不是 4: " << csvColumnContext(row1, 0, lines[1]);
    const CsvInt64Column row1Source = csvColumnAsInt64(row1, 0);
    EXPECT_TRUE(row1Source.ok) << "source_fid 列不是整数: " << csvColumnContext(row1, 0, lines[1]);
    EXPECT_EQ(row1Source.value, fids[0]) << "source_fid 不是 F1: " << csvColumnContext(row1, 0, lines[1]);
    const CsvInt64Column row1Target = csvColumnAsInt64(row1, 1);
    EXPECT_TRUE(row1Target.ok) << "target_fid 列不是整数: " << csvColumnContext(row1, 1, lines[1]);
    EXPECT_EQ(row1Target.value, fids[1]) << "target_fid 不是 F2（重复的那一对）: "
                                        << csvColumnContext(row1, 1, lines[1]);
    EXPECT_EQ(row1[2], "duplicate_geometry")
        << "issue_type 列整串不相等: " << csvColumnContext(row1, 2, lines[1]);
    const CsvDoubleColumn row1Value = csvColumnAsDouble(row1, 3);
    EXPECT_TRUE(row1Value.ok) << "issue_value 列不是数字: " << csvColumnContext(row1, 3, lines[1]);
    // duplicate_geometry 行的 issue_value 由实现写成常量 0（算式：面积类问题才带值，重复不带）。
    EXPECT_NEAR(0.0, row1Value.value, 1e-6)
        << "duplicate_geometry 的 issue_value 应为 0.0: " << csvColumnContext(row1, 3, lines[1]);

    const std::vector<std::string> row2 = splitCsvColumns(lines[2]);
    ASSERT_EQ(row2.size(), 4u) << "topology_check 第 2 数据行列数不是 4: " << csvColumnContext(row2, 0, lines[2]);
    const CsvInt64Column row2Source = csvColumnAsInt64(row2, 0);
    EXPECT_TRUE(row2Source.ok) << "source_fid 列不是整数: " << csvColumnContext(row2, 0, lines[2]);
    EXPECT_EQ(row2Source.value, fids[0]) << "source_fid 不是 F1: " << csvColumnContext(row2, 0, lines[2]);
    const CsvInt64Column row2Target = csvColumnAsInt64(row2, 1);
    EXPECT_TRUE(row2Target.ok) << "target_fid 列不是整数: " << csvColumnContext(row2, 1, lines[2]);
    EXPECT_EQ(row2Target.value, fids[2]) << "target_fid 不是 F3: " << csvColumnContext(row2, 1, lines[2]);
    EXPECT_EQ(row2[2], "overlap") << "issue_type 列整串不相等: " << csvColumnContext(row2, 2, lines[2]);
    const CsvDoubleColumn row2Value = csvColumnAsDouble(row2, 3);
    EXPECT_TRUE(row2Value.ok) << "issue_value 列不是数字: " << csvColumnContext(row2, 3, lines[2]);
    // 算式：F1 与 F3 的 x 重叠 [5,10]（宽 5）、y 重叠 [0,10]（高 10）⇒ 5×10 = 50。
    EXPECT_NEAR(50.0, row2Value.value, 1e-6)
        << "F1-F3 重叠面积应为 5×10=50: " << csvColumnContext(row2, 3, lines[2]);

    const std::vector<std::string> row3 = splitCsvColumns(lines[3]);
    ASSERT_EQ(row3.size(), 4u) << "topology_check 第 3 数据行列数不是 4: " << csvColumnContext(row3, 0, lines[3]);
    const CsvInt64Column row3Source = csvColumnAsInt64(row3, 0);
    EXPECT_TRUE(row3Source.ok) << "source_fid 列不是整数: " << csvColumnContext(row3, 0, lines[3]);
    EXPECT_EQ(row3Source.value, fids[0]) << "source_fid 不是 F1: " << csvColumnContext(row3, 0, lines[3]);
    const CsvInt64Column row3Target = csvColumnAsInt64(row3, 1);
    EXPECT_TRUE(row3Target.ok) << "target_fid 列不是整数: " << csvColumnContext(row3, 1, lines[3]);
    EXPECT_EQ(row3Target.value, fids[3]) << "target_fid 不是 F4: " << csvColumnContext(row3, 1, lines[3]);
    EXPECT_EQ(row3[2], "overlap") << "issue_type 列整串不相等: " << csvColumnContext(row3, 2, lines[3]);
    const CsvDoubleColumn row3Value = csvColumnAsDouble(row3, 3);
    EXPECT_TRUE(row3Value.ok) << "issue_value 列不是数字: " << csvColumnContext(row3, 3, lines[3]);
    // 算式：F4 = [0,2]×[0,4] 整块落在 F1 = [0,10]×[0,10] 内部 ⇒ 交集就是 F4 自己 = 2×4 = 8。
    // ⚠ 这一行是本刀的全部意义：改前 issue_value 列只有 50.0 这一个值 ⇒ "面积写死成常量 50.0"
    //   在新臂也不会红。现在同一列同时承载 50.0 与 8.0，写死任何一个都红在另一行上。
    EXPECT_NEAR(8.0, row3Value.value, 1e-6)
        << "F1-F4 重叠面积应为 2×4=8: " << csvColumnContext(row3, 3, lines[3]);

    const std::vector<std::string> row4 = splitCsvColumns(lines[4]);
    ASSERT_EQ(row4.size(), 4u) << "topology_check 第 4 数据行列数不是 4: " << csvColumnContext(row4, 0, lines[4]);
    const CsvInt64Column row4Source = csvColumnAsInt64(row4, 0);
    EXPECT_TRUE(row4Source.ok) << "source_fid 列不是整数: " << csvColumnContext(row4, 0, lines[4]);
    EXPECT_EQ(row4Source.value, fids[1]) << "source_fid 不是 F2: " << csvColumnContext(row4, 0, lines[4]);
    const CsvInt64Column row4Target = csvColumnAsInt64(row4, 1);
    EXPECT_TRUE(row4Target.ok) << "target_fid 列不是整数: " << csvColumnContext(row4, 1, lines[4]);
    EXPECT_EQ(row4Target.value, fids[2]) << "target_fid 不是 F3: " << csvColumnContext(row4, 1, lines[4]);
    EXPECT_EQ(row4[2], "overlap") << "issue_type 列整串不相等: " << csvColumnContext(row4, 2, lines[4]);
    const CsvDoubleColumn row4Value = csvColumnAsDouble(row4, 3);
    EXPECT_TRUE(row4Value.ok) << "issue_value 列不是数字: " << csvColumnContext(row4, 3, lines[4]);
    // 算式同 (F1,F3)（F2 与 F1 逐字节相同）⇒ 又是 5×10 = 50。这一行同时钉住"(F1,F2) 那一对
    //   走 Equals 分支之后，F2 仍然照常参与后面每一对的 overlap 判定"。
    EXPECT_NEAR(50.0, row4Value.value, 1e-6)
        << "F2-F3 重叠面积应为 5×10=50: " << csvColumnContext(row4, 3, lines[4]);

    const std::vector<std::string> row5 = splitCsvColumns(lines[5]);
    ASSERT_EQ(row5.size(), 4u) << "topology_check 第 5 数据行列数不是 4: " << csvColumnContext(row5, 0, lines[5]);
    const CsvInt64Column row5Source = csvColumnAsInt64(row5, 0);
    EXPECT_TRUE(row5Source.ok) << "source_fid 列不是整数: " << csvColumnContext(row5, 0, lines[5]);
    EXPECT_EQ(row5Source.value, fids[1]) << "source_fid 不是 F2: " << csvColumnContext(row5, 0, lines[5]);
    const CsvInt64Column row5Target = csvColumnAsInt64(row5, 1);
    EXPECT_TRUE(row5Target.ok) << "target_fid 列不是整数: " << csvColumnContext(row5, 1, lines[5]);
    EXPECT_EQ(row5Target.value, fids[3]) << "target_fid 不是 F4: " << csvColumnContext(row5, 1, lines[5]);
    EXPECT_EQ(row5[2], "overlap") << "issue_type 列整串不相等: " << csvColumnContext(row5, 2, lines[5]);
    const CsvDoubleColumn row5Value = csvColumnAsDouble(row5, 3);
    EXPECT_TRUE(row5Value.ok) << "issue_value 列不是数字: " << csvColumnContext(row5, 3, lines[5]);
    // 算式同 (F1,F4)（F2 与 F1 逐字节相同）⇒ 2×4 = 8。
    EXPECT_NEAR(8.0, row5Value.value, 1e-6)
        << "F2-F4 重叠面积应为 2×4=8: " << csvColumnContext(row5, 3, lines[5]);

    // 类别与面积集合的**计数**口径：从拆好的列统计，不用旧的子串 find。
    // WHY 逐行拆列之后还要再数一遍：逐行断言钉的是"第 k 行长什么样"，而这里钉的是
    //   "全表只允许这两类、条数正好 1 + 4，且面积集合 {50, 8} 两种都在、各 2 条"——
    //   两个口径互为对照：行序被改动时逐行断言红，同类行被多写/少写时这里红。
    int duplicateRowCount = 0;
    int overlapRowCount = 0;
    int otherTypeRowCount = 0;
    int overlapArea50Count = 0;
    int overlapArea8Count = 0;
    int overlapAreaOtherCount = 0;
    for (std::size_t lineIndex = 1; lineIndex < lines.size(); ++lineIndex) {
        const std::vector<std::string> columns = splitCsvColumns(lines[lineIndex]);
        if (columns.size() != 4u) {
            ADD_FAILURE() << "topology_check 数据行列数不是 4: "
                          << csvColumnContext(columns, 0, lines[lineIndex]);
            continue;
        }
        if (columns[2] == "duplicate_geometry") {
            ++duplicateRowCount;
        } else if (columns[2] == "overlap") {
            ++overlapRowCount;
            const CsvDoubleColumn value = csvColumnAsDouble(columns, 3);
            if (value.ok && std::fabs(value.value - 50.0) <= 1e-6) {
                ++overlapArea50Count;   // (F1,F3) 与 (F2,F3)
            } else if (value.ok && std::fabs(value.value - 8.0) <= 1e-6) {
                ++overlapArea8Count;    // (F1,F4) 与 (F2,F4)
            } else {
                ++overlapAreaOtherCount;
            }
        } else {
            ++otherTypeRowCount;
        }
    }
    EXPECT_EQ(duplicateRowCount, 1) << "duplicate_geometry 行应恰好 1 条（只有 (F1,F2) 这一对）";
    EXPECT_EQ(overlapRowCount, 4) << "overlap 行应恰好 4 条：(F1,F3) (F1,F4) (F2,F3) (F2,F4)";
    EXPECT_EQ(otherTypeRowCount, 0)
        << "出现了预期之外的 issue_type（empty/invalid 本夹具一条都不该有，N123 仍未覆盖）";
    // 两种面积都必须在场，各 2 条 ⇒ 这才是"面积列真的按每对算出来"的证据，而不是一个常量。
    EXPECT_EQ(overlapArea50Count, 2) << "overlap 面积 5×10=50 的行应恰好 2 条";
    EXPECT_EQ(overlapArea8Count, 2) << "overlap 面积 2×4=8 的行应恰好 2 条";
    EXPECT_EQ(overlapAreaOtherCount, 0) << "出现了 50 与 8 之外的 overlap 面积";
}

// ============== C3a-4：几何派生四条（hull/envelope/boundary/midpoints）公共件 ==============
// 这批用例的判据形状由工单钉死（.superpowers/sdd/2026-10-21-批次C全功能验证与文档同步/
// task-C3a4-brief.md）：夹具从单要素改成两要素非对称 L，断言改成"先基数、后成员"的顶点集合比较
// + 包框四个极值 + name↔几何配对。下面几件公共件只为这个目的存在，不做通用化。

// 顶点规范化键：去重这一步先把坐标量化到 1e-6 网格，再拿整数对当键。
// WHY 不用字符串拼接相等当键：拼串键对末位抖动敏感（同一个 6 可能拼成 "6"，也可能拼成
// "6.000000000000001"），基数断言会随格式化规则漂移，红了也说不清是几何变了还是字符串变了。
using DerivedVertexKey = std::pair<long long, long long>;

// 顶点比较的容差：与工单规定的 EXPECT_NEAR(.., 1e-6) 同值。
// WHY 成员判断用容差而不是 ==：几何派生回来的坐标是 double，末位抖动是常态；整数网格键只
// 负责"把同一个点认作一个"，判"这个点在不在集合里"仍然按 1e-6 容差比。
constexpr double kDerivedVertexTolerance = 1e-6;

static DerivedVertexKey makeDerivedVertexKey(double x, double y) {
    return {std::llround(x * 1e6), std::llround(y * 1e6)};
}

// 取一条线/环上的**去重**顶点集合（保留首次出现的那个原值，供容差比较用）。
// 闭合环首尾重复的那个点在集合里只算一次 ⇒ 5 个顶点的凸包环（存 6 个点）数出来是 5，
// 6 个顶点的输入环（存 7 个点）数出来是 6。
static std::vector<std::pair<double, double>> collectDerivedVertices(const OGRSimpleCurve* curve) {
    std::vector<std::pair<double, double>> verts;
    std::set<DerivedVertexKey> seen;
    if (!curve) {
        return verts;
    }
    OGRPoint pt;
    for (int i = 0; i < curve->getNumPoints(); ++i) {
        curve->getPoint(i, &pt);
        if (seen.insert(makeDerivedVertexKey(pt.getX(), pt.getY())).second) {
            verts.emplace_back(pt.getX(), pt.getY());
        }
    }
    return verts;
}

// 先断基数、再断成员（工单要求的 fail-closed 形状），基数不等时**不**进成员那一步。
// WHY 基数必须先行：只写"每个期望点都能在集合里找到"，空集合与错集合都可能是恒真的假绿
//（本仓 N108 那一族）。基数相等 + 每个期望点都在场 ⇒ 两个集合相等，这才是完整判据。
// WHY 按集合比、不钉顺序：GEOS 给的环起点与绕向是实现细节（本批实测凸包 A 的环是
// (0,0)→(0,8)→(4,8)→(12,3)→(12,0)，顺时针、起点不定），而本仓已因 GEOS 升级出过一次堆损坏
//（Task A0）⇒ 钉顺序等于把实现细节当规格埋进库，换版本就红一片。
static ::testing::AssertionResult expectDerivedVertexSet(
    const std::vector<std::pair<double, double>>& actual,
    const std::vector<std::pair<double, double>>& expected,
    const char* what) {
    if (actual.size() != expected.size()) {
        std::string dump;
        for (const auto& v : actual) {
            dump += "(" + std::to_string(v.first) + ", " + std::to_string(v.second) + ")";
        }
        return ::testing::AssertionFailure()
            << what << " 顶点集合基数 = " << actual.size() << "（期望 " << expected.size()
            << "：先去重再比基数，空集合也算不等），实际集合 = {" << dump << "}";
    }
    for (const auto& point : expected) {
        bool found = false;
        for (const auto& v : actual) {
            if (std::fabs(v.first - point.first) <= kDerivedVertexTolerance &&
                std::fabs(v.second - point.second) <= kDerivedVertexTolerance) {
                found = true;
                break;
            }
        }
        if (!found) {
            return ::testing::AssertionFailure()
                << what << " 基数 " << expected.size() << " 对上了，但在 1e-6 容差内找不到期望顶点 ("
                << point.first << ", " << point.second << ')';
        }
    }
    return ::testing::AssertionSuccess();
}

// 面类三条（convex_hull / envelope / boundary）共用的两要素夹具。
// A = 非对称 L：(0,0)-(12,0)-(12,3)-(4,3)-(4,8)-(0,8) 闭合，与 centroid 用例同一只 L。
//     ① 点集关于 y=x 转置后是 {(0,0),(0,12),(3,12),(3,4),(8,4),(8,0)} ≠ 自身 ⇒ "坐标轴写反"
//        这类镜像变异在钉坐标的断言上必红（旧夹具 (0,0),(10,0),(10,2),(2,2),(2,10),(0,10)
//        自己关于 y=x 对称，转置后逐点仍在集合内，连"形状不同"都造不出来）；
//     ② 面积 = 底条 12×3=36 + 左条 4×5=20 = 56；周长 = 12+3+8+5+4+8 = 40。
// B = 小矩形：(20,0)-(24,0)-(24,2)-(20,2) 闭合，包框 [20,24]×[0,2] 与 A 的 [0,12]×[0,8] 完全
//     不相交，自身转置后也不等于自己；面积 4×2=8，周长 4+2+4+2=12。
// WHY 非要从 1 要素改成 2 要素：单要素夹具上"只处理第一个要素"这一族缺陷结构性不可见
//（C3a-3a/3b 实测），只有"基数 == 2"+"第 2 要素 name 是 B 且几何也是 B 的"才打得住。
static bool writeTwoPolygonFixture(const std::string& path) {
    auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!driver) {
        return false;
    }
    auto* ds = driver->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) {
        return false;
    }
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);
    auto* layer = ds->CreateLayer("shapes", srs.get(), wkbPolygon, nullptr);
    if (!layer) {
        GDALClose(ds);
        return false;
    }
    OGRFieldDefn nameField("name", OFTString);
    if (layer->CreateField(&nameField) != OGRERR_NONE) {
        GDALClose(ds);
        return false;
    }

    auto addPolygon = [&](const char* name,
                          const std::vector<std::pair<double, double>>& ring) {
        OGRPolygon poly;
        OGRLinearRing linearRing;
        for (const auto& point : ring) {
            linearRing.addPoint(point.first, point.second);
        }
        linearRing.addPoint(ring.front().first, ring.front().second);  // 闭合
        poly.addRing(&linearRing);
        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", name);
        feat->SetGeometry(&poly);
        const bool ok = layer->CreateFeature(feat) == OGRERR_NONE;
        OGRFeature::DestroyFeature(feat);
        return ok;
    };

    bool ok = addPolygon("A", {{0.0, 0.0}, {12.0, 0.0}, {12.0, 3.0},
                               {4.0, 3.0}, {4.0, 8.0}, {0.0, 8.0}});
    ok = addPolygon("B", {{20.0, 0.0}, {24.0, 0.0}, {24.0, 2.0}, {20.0, 2.0}}) && ok;
    GDALClose(ds);
    return ok;
}

TEST_F(PluginTest, VectorConvexHullExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_convex_hull_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_convex_hull_output.gpkg");

    ASSERT_TRUE(writeTwoPolygonFixture(input));

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("convex_hull");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    // 派生不增删要素：两个输入 ⇒ 两个输出。旧夹具只有 1 个要素，"只处理第一个要素"看不见。
    EXPECT_EQ(result.metadata.at("feature_count"), "2");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    // 元数据说 2 条，落盘的也必须是 2 条：只看 metadata 等于信实现自己的记账。
    EXPECT_EQ(outLayer->GetFeatureCount(), 2);

    // 要素 A（非对称 L，面积 56）：凸包 = L 补上缺口三角形，即去掉凹角 (4,3)。
    //   缺口三角形 = ½·8·5 = 20（水平边 12-4=8，竖直边 8-3=5）⇒ 凸包面积 = 56 + 20 = 76。
    //   顶点集合 = 输入环去掉凹角 = {(0,0),(12,0),(12,3),(4,8),(0,8)} 共 5 个。
    //   x 与 y 不同尺（[0,12] 与 [0,8]）⇒ 转置类变异在下面这套点面前必红。
    OGRFeature* featA = outLayer->GetNextFeature();
    ASSERT_NE(featA, nullptr);
    EXPECT_STREQ(featA->GetFieldAsString("name"), "A");
    ASSERT_NE(featA->GetGeometryRef(), nullptr);
    const auto* hullA = featA->GetGeometryRef()->toPolygon();
    ASSERT_NE(hullA, nullptr);
    const auto hullAVerts = collectDerivedVertices(hullA->getExteriorRing());
    ASSERT_TRUE(expectDerivedVertexSet(hullAVerts, {{0.0, 0.0}, {12.0, 0.0}, {12.0, 3.0},
                                                    {4.0, 8.0}, {0.0, 8.0}}, "convex_hull A"));
    EXPECT_NEAR(hullA->get_Area(), 76.0, 1e-6);
    OGRFeature::DestroyFeature(featA);

    // 要素 B（4×2 矩形）：凸包就是它自己 ⇒ 顶点集合 {(20,0),(24,0),(24,2),(20,2)}，面积 4×2=8。
    //   name↔几何配对：两个输出若都写 A 的几何，这里 4 个顶点与面积 8 一起红。
    OGRFeature* featB = outLayer->GetNextFeature();
    ASSERT_NE(featB, nullptr);
    EXPECT_STREQ(featB->GetFieldAsString("name"), "B");
    ASSERT_NE(featB->GetGeometryRef(), nullptr);
    const auto* hullB = featB->GetGeometryRef()->toPolygon();
    ASSERT_NE(hullB, nullptr);
    const auto hullBVerts = collectDerivedVertices(hullB->getExteriorRing());
    ASSERT_TRUE(expectDerivedVertexSet(hullBVerts, {{20.0, 0.0}, {24.0, 0.0}, {24.0, 2.0},
                                                   {20.0, 2.0}}, "convex_hull B"));
    EXPECT_NEAR(hullB->get_Area(), 8.0, 1e-6);
    OGRFeature::DestroyFeature(featB);
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

        // 夹具是不对称 L 形 (0,0)-(12,0)-(12,3)-(4,3)-(4,8)-(0,8)：
        //   底条 12x3 = 36，形心 (6, 1.5)；左条 4x5 = 20，形心 (2, 5.5)；总面积 56
        //   cx = (36*6 + 20*2)/56 = 256/56 = 32/7
        //   cy = (36*1.5 + 20*5.5)/56 = 164/56 = 41/14
        // 三个要点：① cx != cy ⇒ "坐标轴写反"会红（正方形夹具上这条永远测不出）；
        // ② 与包框中心 (6,4) 不同 ⇒ "面积重心写成包框中心"会红；
        // ③ 期望值由夹具自己的形状算出来，不从产物回填。
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(0, 0);
        ring.addPoint(12, 0);
        ring.addPoint(12, 3);
        ring.addPoint(4, 3);
        ring.addPoint(4, 8);
        ring.addPoint(0, 8);
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
    EXPECT_NEAR(centroid->getX(), 32.0 / 7.0, 1e-6);
    EXPECT_NEAR(centroid->getY(), 41.0 / 14.0, 1e-6);
    // 这里不核"重心在形内"：centroid 的定义性质允许落在凹形之外，
    // 与 interior_point 的区别正在这里（那条用例核形内）。
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorEnvelopeExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_envelope_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_envelope_output.gpkg");

    ASSERT_TRUE(writeTwoPolygonFixture(input));

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("envelope");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    // 派生不增删要素：两个输入 ⇒ 两个输出（旧夹具只有 1 个要素）。
    EXPECT_EQ(result.metadata.at("feature_count"), "2");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(), 2);

    // 期望值来自夹具点集逐维取极值（不是从产物回填）：
    //   A 的非对称 L：x∈{0,12,12,4,4,0} → [0,12]，y∈{0,0,3,3,8,8} → [0,8] ⇒ 0/0/12/8
    //   B 的小矩形  ：x∈{20,24,24,20}   → [20,24]，y∈{0,0,2,2}   → [0,2] ⇒ 20/0/24/2
    // WHY 四个极值而不是只断面积：包框面积（12×8=96、4×2=8）在 x↔y 转置下数值不变，
    // 是镜像不变量 ⇒ 承重的是这四个带方向的极值；面积两条留着只是顺手核对乘出来的矩形没变形。
    OGRFeature* featA = outLayer->GetNextFeature();
    ASSERT_NE(featA, nullptr);
    EXPECT_STREQ(featA->GetFieldAsString("name"), "A");
    ASSERT_NE(featA->GetGeometryRef(), nullptr);
    OGREnvelope envA;
    featA->GetGeometryRef()->getEnvelope(&envA);
    EXPECT_NEAR(envA.MinX, 0.0, 1e-6);
    EXPECT_NEAR(envA.MinY, 0.0, 1e-6);
    EXPECT_NEAR(envA.MaxX, 12.0, 1e-6);
    EXPECT_NEAR(envA.MaxY, 8.0, 1e-6);
    const auto* envPolyA = featA->GetGeometryRef()->toPolygon();
    ASSERT_NE(envPolyA, nullptr);
    EXPECT_NEAR(envPolyA->get_Area(), 96.0, 1e-6);
    OGRFeature::DestroyFeature(featA);

    // 要素 B：若实现"只处理第一个要素"，这里四个极值会全部读成 A 的 0/0/12/8 ⇒ 必红。
    OGRFeature* featB = outLayer->GetNextFeature();
    ASSERT_NE(featB, nullptr);
    EXPECT_STREQ(featB->GetFieldAsString("name"), "B");
    ASSERT_NE(featB->GetGeometryRef(), nullptr);
    OGREnvelope envB;
    featB->GetGeometryRef()->getEnvelope(&envB);
    EXPECT_NEAR(envB.MinX, 20.0, 1e-6);
    EXPECT_NEAR(envB.MinY, 0.0, 1e-6);
    EXPECT_NEAR(envB.MaxX, 24.0, 1e-6);
    EXPECT_NEAR(envB.MaxY, 2.0, 1e-6);
    const auto* envPolyB = featB->GetGeometryRef()->toPolygon();
    ASSERT_NE(envPolyB, nullptr);
    EXPECT_NEAR(envPolyB->get_Area(), 8.0, 1e-6);
    OGRFeature::DestroyFeature(featB);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorBoundaryExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_boundary_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_boundary_output.gpkg");

    ASSERT_TRUE(writeTwoPolygonFixture(input));

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("boundary");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    // 派生不增删要素：两个面 ⇒ 两条边界（旧夹具只有 1 个要素）。
    EXPECT_EQ(result.metadata.at("feature_count"), "2");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(), 2);

    // 简单面的边界 = 它自己的环 ⇒ 顶点集合就是输入环的 6 个点（顺序无关，见 expectDerivedVertexSet）。
    // 周长算式：A 的 L 形 12+3+8+5+4+8 = 40。
    // ⚠ 这个 40.0 与旧夹具（10×10 正方形，周长恒 40）撞值，是改夹具之后**重算**出来的同一个数，
    //   不是忘了更新期望值；单看这条数值没有判别力，承重的是新加的点集合、闭合断言和 B 的 12.0。
    const auto assertBoundaryOf = [&](OGRFeature* feat, const char* name,
                                      const std::vector<std::pair<double, double>>& verts,
                                      double perimeter) {
        ASSERT_NE(feat, nullptr);
        EXPECT_STREQ(feat->GetFieldAsString("name"), name);
        ASSERT_NE(feat->GetGeometryRef(), nullptr);
        const auto* outGeom = feat->GetGeometryRef();
        const auto flatType = wkbFlatten(outGeom->getGeometryType());
        // 宽松的"两者之一"保留（不把语义争议钉在类型上）；结构事实由下面那条成员数断言补上。
        EXPECT_TRUE(flatType == wkbLineString || flatType == wkbMultiLineString)
            << static_cast<int>(flatType);
        const OGRLineString* member = nullptr;
        if (flatType == wkbLineString) {
            member = outGeom->toLineString();
        } else {
            const auto* mls = outGeom->toMultiLineString();
            ASSERT_NE(mls, nullptr);
            // 本批实测结构事实：boundary 对简单面的输出是 wkbMultiLineString 且**只含 1 条成员线**
            //（不是 LineString）。多写一条成员就是"把每个环拆开"，少写就是丢边界。
            ASSERT_EQ(mls->getNumGeometries(), 1);
            member = mls->getGeometryRef(0)->toLineString();
        }
        ASSERT_NE(member, nullptr);
        // 环闭合：面的边界不该是开口的折线。
        EXPECT_TRUE(member->get_IsClosed());
        const auto actual = collectDerivedVertices(member);
        EXPECT_TRUE(expectDerivedVertexSet(actual, verts, name)) << "（boundary 的顶点集合）";
        EXPECT_NEAR(member->get_Length(), perimeter, 1e-6);
    };

    OGRFeature* featA = outLayer->GetNextFeature();
    assertBoundaryOf(featA, "A", {{0.0, 0.0}, {12.0, 0.0}, {12.0, 3.0},
                                  {4.0, 3.0}, {4.0, 8.0}, {0.0, 8.0}}, 40.0);
    OGRFeature::DestroyFeature(featA);

    // 要素 B：矩形环 4 个点、周长 4+2+4+2 = 12。
    // name↔几何配对：两个输出若都写第一个要素的环，这里 4 个点与 12.0 一起红。
    OGRFeature* featB = outLayer->GetNextFeature();
    assertBoundaryOf(featB, "B", {{20.0, 0.0}, {24.0, 0.0}, {24.0, 2.0}, {20.0, 2.0}}, 12.0);
    OGRFeature::DestroyFeature(featB);
    GDALClose(outDs);
}

// ── C3m：三条"此前根本不建索引"的动作各一条，把三种几何类别补齐 ──────────────────────
// centroid 出**点**、boundary 出**线**、convex_hull 出**面**；三条都只加"索引可用性"这一层断言，
// 夹具一律复用上面那条同名执行用例的（不新造数据），期望数与包框按夹具坐标手算写死。
// WHY 输出另起一个 .gpkg 名：与 buffer/clip 那六条同口径 —— 与本刀的执行用例共用产物名会把两条
//   用例变成"谁后跑谁决定产物"（VectorBufferGpkgSpatialIndexIsPopulated 上面那段 WHY 定过的判断）。
// WHY 三条而不是抽查一条：驱动建索引在点/线/面三类几何上的回填路径不同（触发器只有一条，但
//   rtree 的 MBR 由几何自己算），只验一类的话另外两类回潮无人拦。

TEST_F(PluginTest, VectorCentroidGpkgSpatialIndexIsPopulated) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string input = utf8PathString(getTestDir() / "e2e_centroid_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_centroid_index_output.gpkg");

    // 夹具与 VectorCentroidExecution 逐字同形：一只不对称 L 形面
    //（(0,0)-(12,0)-(12,3)-(4,3)-(4,8)-(0,8)，EPSG:3857，层名 shapes，字段 name=A）。
    // 同一只 L 也是 writeTwoPolygonFixture 的要素 A ⇒ 这里不引入任何新几何。
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
        ring.addPoint(12, 0);
        ring.addPoint(12, 3);
        ring.addPoint(4, 3);
        ring.addPoint(4, 8);
        ring.addPoint(0, 8);
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
    EXPECT_TRUE(result.success) << "Centroid failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    // 期望 1 个要素；包框取输入面的包框 (0,0,12,8) —— 形心 (32/7, 41/14) 必在其中，
    // 这条算式来自上面夹具的顶点集合，不从产物回填。
    expectGpkgSpatialIndexUsable(outLayer, 1, 0.0, 0.0, 12.0, 8.0, output);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorBoundaryGpkgSpatialIndexIsPopulated) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string input = utf8PathString(getTestDir() / "e2e_boundary_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_boundary_index_output.gpkg");

    // 复用 VectorBoundaryExecution 的夹具（面类三条共用的两要素夹具）。
    ASSERT_TRUE(writeTwoPolygonFixture(input));

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("boundary");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Boundary failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    // 期望 2 条边界线；包框 = 夹具两只面的极值并集：A 的 x∈[0,12] y∈[0,8] ∪ B 的 x∈[20,24] y∈[0,2]
    //   ⇒ (0, 0, 24, 8)。边界是输入环自身的顶点，不会跑到输入包框之外。
    expectGpkgSpatialIndexUsable(outLayer, 2, 0.0, 0.0, 24.0, 8.0, output);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorConvexHullGpkgSpatialIndexIsPopulated) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string input = utf8PathString(getTestDir() / "e2e_convex_hull_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_convex_hull_index_output.gpkg");

    // 复用 VectorConvexHullExecution 的夹具（与 boundary 同一份两要素夹具）。
    ASSERT_TRUE(writeTwoPolygonFixture(input));

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("convex_hull");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << "Convex hull failed: " << result.message;
    ASSERT_TRUE(fs::exists(output));

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    // 期望 2 个面（派生不增删要素）；凸包不超出输入包框 ⇒ 包框同 boundary 那条 (0, 0, 24, 8)。
    expectGpkgSpatialIndexUsable(outLayer, 2, 0.0, 0.0, 24.0, 8.0, output);
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

        // 夹具三个要素（工单 §C3b-B2 定稿；本动作既无投影闸也无几何类型闸）：
        //   F1 = MultiPolygon，**2 个部件**；
        //   F2 = MultiPolygon，**1 个部件**（⚠ 必须在场，见下一条）；
        //   F3 = 普通 Polygon，1 个部件（本刀新增的"两个半边都不成立"那一格）。
        // 实现的判定是 `partCount > 1 || isMultipartGeometryType(...)`（vector_plugin_multipart_check.inc:44），
        // 两个半边各证一头：F1 靠左半边（部件数 2），F2 靠**右半边**（类型是 Multi 但部件数 1），
        // F3 两边都不成立 ⇒ 不许报出。"单部件 MultiPolygon 也算多部件"是本动作最容易写错的口径
        // （控制器 2026-10-22 实测真实省层 42/42 全报多部件，走的就是右半边）⇒ 用断言钉，不靠注释。
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

        // F3：普通 Polygon（部件数 1、类型也不是 Multi）⇒ 判定的两个半边都不成立 ⇒ 不许进 CSV，
        // 但它照样要被扫描 ⇒ 正是它把 scanned_count=3 与 issue_count=2 的差额撑出来。
        // ⚠ 它写成 Polygon 落进**声明为 wkbMultiPolygon 的层**是有意的：GDAL 的
        //   OGRGeoPackageTableLayer::CheckGeometryType 对这种组合只发一条 CE_Warning
        //   （原文 "…which is not normally allowed by the GeoPackage specification, but the driver
        //   will however do it"）便照原样落盘，blob 头与 WKB 都带自己的类型，读回来仍是 wkbPolygon。
        //   若图省事把它也写成 MultiPolygon，F3 就退化成第二个 F2 ⇒ "非多部件不许报"这一格覆盖归零。
        OGRPolygon plainPoly;
        OGRLinearRing plainRing;
        plainRing.addPoint(30, 0);
        plainRing.addPoint(35, 0);
        plainRing.addPoint(35, 5);
        plainRing.addPoint(30, 5);
        plainRing.addPoint(30, 0);
        plainPoly.addRing(&plainRing);

        auto* feat3 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat3->SetGeometry(&plainPoly);
        ASSERT_EQ(layer->CreateFeature(feat3), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat3);
        GDALClose(ds);
    }

    // R-B1-fid：期望的 FID 一律从**输入侧**现读（重新打开刚落盘的输入层，按创建顺序取 GetFID()），
    // 不许从产出的 CSV 回填，也不假设"GPKG 的 FID 从 1 起"。
    const std::vector<long long> fids = readLayerFidsInCreationOrder(input);
    ASSERT_EQ(fids.size(), 3u)
        << "multipart_check 夹具创建了 3 个要素，重新打开输入层却没读回 3 个 FID";

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("multipart_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("issue_count"), "2");
    EXPECT_EQ(result.metadata.at("scanned_count"), "3");

    const std::vector<std::string> lines = readCsvLines(output);
    ASSERT_EQ(lines.size(), 3u)
        << "multipart_check CSV 应为 1 表头 + 2 数据行（F3 普通 Polygon 不许报出）";
    EXPECT_EQ(lines[0], "source_fid,geom_type,part_count");

    // 实现按读入顺序写行 ⇒ 第 1 数据行是 F1、第 2 是 F2。
    // 旧判据 `lines[1].find(",2")` / `lines[2].find(",1")`（工单点名的那两条）已整段删除，
    // 不留新旧并存：子串对**列位**（"2" 写在 source_fid 里也命中）、**基数**（多写一行也命中）
    // 和**邻近值**（",12"、",21" 都命中同一条）三件事都不敏感。
    const std::vector<std::string> row1 = splitCsvColumns(lines[1]);
    ASSERT_EQ(row1.size(), 3u)
        << "multipart_check 第 1 数据行列数不是 3: " << csvColumnContext(row1, 0, lines[1]);
    const CsvInt64Column row1Fid = csvColumnAsInt64(row1, 0);
    EXPECT_TRUE(row1Fid.ok) << "source_fid 列不是整数: " << csvColumnContext(row1, 0, lines[1]);
    EXPECT_EQ(row1Fid.value, fids[0]) << "source_fid 不是 F1: " << csvColumnContext(row1, 0, lines[1]);
    EXPECT_EQ(row1[1], "MultiPolygon")
        << "geom_type 列应为 MultiPolygon: " << csvColumnContext(row1, 1, lines[1]);
    const CsvDoubleColumn row1Parts = csvColumnAsDouble(row1, 2);
    EXPECT_TRUE(row1Parts.ok) << "part_count 列不是数字: " << csvColumnContext(row1, 2, lines[1]);
    // 算式：F1 由两个闭合环面拼成 ⇒ geometryPartCount 走 wkbMultiPolygon 分支取
    //   getNumGeometries() = 2（vector_plugin_common.inc:521）。
    // ⚠ R-C3b-1 选 ②：part_count 是**部件计数**、不是量测，写成浮比只为了让机器级判据
    //   CoverageMeta.GoldEvidenceIsAuditable 认它作数值断言；1e-9 远小于任何整数偏差，语义损失为零。
    EXPECT_NEAR(2.0, row1Parts.value, 1e-9)
        << "F1 的部件计数应为 2: " << csvColumnContext(row1, 2, lines[1]);

    const std::vector<std::string> row2 = splitCsvColumns(lines[2]);
    ASSERT_EQ(row2.size(), 3u)
        << "multipart_check 第 2 数据行列数不是 3: " << csvColumnContext(row2, 0, lines[2]);
    const CsvInt64Column row2Fid = csvColumnAsInt64(row2, 0);
    EXPECT_TRUE(row2Fid.ok) << "source_fid 列不是整数: " << csvColumnContext(row2, 0, lines[2]);
    EXPECT_EQ(row2Fid.value, fids[1]) << "source_fid 不是 F2: " << csvColumnContext(row2, 0, lines[2]);
    EXPECT_EQ(row2[1], "MultiPolygon")
        << "geom_type 列应为 MultiPolygon: " << csvColumnContext(row2, 1, lines[2]);
    const CsvDoubleColumn row2Parts = csvColumnAsDouble(row2, 2);
    EXPECT_TRUE(row2Parts.ok) << "part_count 列不是数字: " << csvColumnContext(row2, 2, lines[2]);
    // 这一行钉的是右半边：F2 只有一个部件 ⇒ 计数为 1，行却**必须存在**（靠 isMultipartGeometryType 报出）。
    // "计数 = 1" 与 "行存在" 两条合起来才排掉"把 partCount>1 写成 partCount>=1"这类变异
    // （那样 F3 也会被报出，行数与 issue_count 一起红）。
    EXPECT_NEAR(1.0, row2Parts.value, 1e-9)
        << "F2 的部件计数应为 1（单部件 MultiPolygon 也算多部件）: "
        << csvColumnContext(row2, 2, lines[2]);
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

        // 夹具（工单 §C3b-B2 定稿 + §C3b-4 追加 F3，EPSG:3857）：
        //   F1 = MultiPolygon，两个部件 **4×4 = 16** 与 **2×2 = 4**；
        //   F2 = 普通 Polygon **3×3 = 9**（B2 新增的第二要素 ⇒ 单部件要素也走一遍展开路径，
        //        这是 N120 想要的"多要素"覆盖在 singlepart 上做实）。
        //   F3 = **NULL 几何要素**（本刀 C3b-4 新增，name = "C"）⇒ 让实现的跳过分支真正被走到，
        //        使 `skipped_count` 从恒 0（不承重）变成 1（承重）。
        // 三个面积故意互不相同：若全等，"把每个部件都写成同一个"这类变异测不出来。
        // ⚠ F2 是 Polygon 写进声明为 wkbMultiPolygon 的层：GDAL 只发一条 CE_Warning 便照原样落盘，
        //   blob 自带类型，读回来仍是 wkbPolygon（与 multipart_check 夹具同一处理，理由见那里的注释）。
        //   层声明类型仍是 MultiPolygon ⇒ singlepartLayerGeometryType 给出 wkbPolygon，
        //   这正是本用例要走的分支（换成未知类型会直接 fail 在类型闸上）。
        OGRMultiPolygon multiPoly;
        OGRPolygon poly1;
        OGRLinearRing ring1;
        ring1.addPoint(0, 0);
        ring1.addPoint(4, 0);
        ring1.addPoint(4, 4);
        ring1.addPoint(0, 4);
        ring1.addPoint(0, 0);
        poly1.addRing(&ring1);  // 4*4 = 16
        multiPoly.addGeometry(&poly1);

        OGRPolygon poly2;
        OGRLinearRing ring2;
        ring2.addPoint(10, 10);
        ring2.addPoint(12, 10);
        ring2.addPoint(12, 12);
        ring2.addPoint(10, 12);
        ring2.addPoint(10, 10);
        poly2.addRing(&ring2);  // 2*2 = 4
        multiPoly.addGeometry(&poly2);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", "A");
        feat->SetGeometry(&multiPoly);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);

        OGRPolygon soloPoly;
        OGRLinearRing ring3;
        ring3.addPoint(20, 0);
        ring3.addPoint(23, 0);
        ring3.addPoint(23, 3);
        ring3.addPoint(20, 3);
        ring3.addPoint(20, 0);
        soloPoly.addRing(&ring3);  // 3*3 = 9

        auto* featB = OGRFeature::CreateFeature(layer->GetLayerDefn());
        featB->SetField("name", "B");
        featB->SetGeometry(&soloPoly);
        ASSERT_EQ(layer->CreateFeature(featB), OGRERR_NONE);
        OGRFeature::DestroyFeature(featB);

        // F3：一条**没有几何**的要素。走 GPKG + OGR API 的原路（`SetGeometry(nullptr)` 后 `CreateFeature`），
        // 因为本用例的载体就是 GPKG ⇒ 换驱动会连读取路径一起换掉，判据就不再是 singlepart 的跳过分支。
        // ⚠ 不要照抄 `DuplicatePointCheckToleratesMissingGeometry` 的写法——那条是在**手写 GeoJSON 字符串**里
        //   放 `"geometry": null`，载体不同（GeoJSON 读取路径），不能拿来当 GPKG 写入的先例。
        //   口径（谁进哪个键）参考的是 `DuplicatePointCheckSkipsNonPointFeatureGeometry`，见下面期望值处的注释。
        auto* featC = OGRFeature::CreateFeature(layer->GetLayerDefn());
        featC->SetField("name", "C");
        featC->SetGeometry(nullptr);
        ASSERT_EQ(layer->CreateFeature(featC), OGRERR_NONE)
            << "GPKG 拒写 NULL 几何 ⇒ 本刀前提不成立（按工单停下报告，不改用 GeoJSON 绕过）";
        OGRFeature::DestroyFeature(featC);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("singlepart");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    // 三个计数键一个不落，每个都带算式（对着 vector_plugin_singlepart.inc:85-113 的循环推）：
    //   feature_count = 展开后写出的部件数 = F1 的 2 + F2 的 1 + F3 的 0 = **3**（空几何不产出任何东西）；
    //   source_count  = 被遍历到的源要素数 = F1 + F2 + F3 = **3**（跳过分支自己也要 ++sourceCount，:88-90
    //                   两条出口都 ++ ⇒ 进度/审计口径不会因跳过而变小）；
    //   skipped_count = 空/缺几何被跳过的源要素数 = F3 那一条 = **1**。
    // ⚠ 这一刀之前钉的是 `skipped_count == "0"`，而当时夹具的 F1/F2 两条要素都合法 ⇒ 把实现里的
    //   `++skippedCount`（:88）整行删掉，该键**仍然是 0**，断言一样绿 ⇒ 那条断言**不承重**，
    //   变异 M-8a 在当前夹具上是等价变异。这与 topology 的"两行同值"、duplicate_point 的"零向量"
    //   是同一族病：**期望值本身没有区分度**，不是断言写法的错。加了 F3 之后该键=1，
    //   删掉 `++skippedCount` 会立刻红，M-8a 才有资格进取证轮。
    // ⚠ 口径提醒（两个键同名近义、别互相引用）：
    //   · `singlepart` 的 `skipped_count` 数的是"空/缺几何被跳过"⇒ NULL 几何**应当计入**（本用例钉的就是它）；
    //   · `duplicate_point_check` 的 `skipped_geometry_count` 数的是"有类型但不是点类"⇒ NULL 几何**不计入**
    //     （`DuplicatePointCheckToleratesMissingGeometry` 钉该键为 0，
    //      `DuplicatePointCheckSkipsNonPointFeatureGeometry` 钉非点几何那条为 1）。
    //   两条不矛盾，各自钉的是各自动作的实现分支。
    // ⚠ 这不是全仓第一条 skipped_count 断言 —— `repair` 链那条用例早就钉过它自己的该键；
    //   准确说法是"对 singlepart 这个动作，这是第一条"。
    EXPECT_EQ(result.metadata.at("feature_count"), "3");
    EXPECT_EQ(result.metadata.at("source_count"), "3");
    EXPECT_EQ(result.metadata.at("skipped_count"), "1");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(), 3);   // 2 + 1 + 0：F3 是空几何，一个部件都不产出
    outLayer->ResetReading();

    std::vector<double> areas;
    std::map<std::string, int> nameCounts;
    int nonPolygonCount = 0;
    OGRFeature* outFeat = nullptr;
    while ((outFeat = outLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = outFeat->GetGeometryRef();
        if (!geom || wkbFlatten(geom->getGeometryType()) != wkbPolygon) {
            ++nonPolygonCount;   // 输出几何类型是契约：dstType = singlepartLayerGeometryType(wkbMultiPolygon)
        } else {
            // get_Area() 声明在 OGRCurvePolygon / OGRPolygon / OGRMultiPolygon 上，OGRGeometry 基类没有
            // ⇒ 上面已核过扁平类型是 wkbPolygon，这里下推成 OGRPolygon 再取面积（本文件其它用例同一写法）。
            areas.push_back(geom->toPolygon()->get_Area());
        }
        ++nameCounts[outFeat->GetFieldAsString("name")];
        OGRFeature::DestroyFeature(outFeat);
    }
    GDALClose(outDs);

    EXPECT_EQ(nonPolygonCount, 0) << "singlepart 的输出要素必须全是 Polygon，不该有非 Polygon 几何";
    // 三条面积的**多重集**仍是 {4.0, 9.0, 16.0}，加 F3 前后不变：F3 没有几何，
    // 既不进 areas、也不进 nonPolygonCount（它在实现里就被 continue 掉了，压根没写进输出层）。
    ASSERT_EQ(areas.size(), 3u) << "singlepart 应回读到 3 个 Polygon 部件的面积";
    // **按多重集比，不按位置比**（把三条实读面积排序后逐个比）。
    // WHY：singlepart 写每条部件走 writeFeatureWithCopiedFieldsOwned，那里 feature->SetFID(OGRNullFID)
    //   （vector_plugin_overlay_support.inc:134）⇒ 输出 FID 完全由驱动重排，**顺序不是契约**；
    //   钉"第 1 条必须是 16"等于把实现细节当规格埋进库里——本仓 C3a-4 刚为顶点顺序做过同样的裁定，
    //   同一族病。排序以后仍然钉得住"三个面积分别是 16/4/9"：少写一个部件、把面积算错、
    //   把两个部件写成同一个，都会让某个位置红。
    std::sort(areas.begin(), areas.end());
    EXPECT_NEAR(4.0, areas[0], 1e-6) << "最小的部件面积应为 2*2 = 4，实读排序后 " << areas[0];
    EXPECT_NEAR(9.0, areas[1], 1e-6) << "中间的部件面积应为 3*3 = 9，实读排序后 " << areas[1];
    EXPECT_NEAR(16.0, areas[2], 1e-6) << "最大的部件面积应为 4*4 = 16，实读排序后 " << areas[2];
    // 字段随部件复制（SetFrom(sourceFeature, TRUE)）：两个 F1 部件带 A、F2 部件带 B。
    // 同样按**计数**比而不是按位置比，理由与上面那条一致。
    // F3（name = "C"）那一行不用另加断言：输出总数已被上面的 `GetFeatureCount() == 3` 钉死，
    // 而 A=2、B=1 占满这 3 行 ⇒ "C 不产出行"是这三条的推论，再加一条只是同一个事实写两遍。
    EXPECT_EQ(nameCounts["A"], 2) << "F1 的两个部件都应带上 name = A";
    EXPECT_EQ(nameCounts["B"], 1) << "F2 展开出的那个部件应带上 name = B";
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

    // 只数要素数抓不到"顶点顺序写反""丢掉收尾点""坐标轴互换"这三类错，
    // 所以按 vertex_idx 的顺序把三个点的坐标逐个钉死（夹具折线 (0,0)-(5,5)-(10,0)）。
    // 数值直接由夹具自己的顶点表推出，不从产物回填。
    static const double kExpectedVertices[3][2] = { {0.0, 0.0}, {5.0, 5.0}, {10.0, 0.0} };
    outLayer->ResetReading();
    int vertexSeen = 0;
    for (int i = 0; i < 3; ++i) {
        OGRFeature* vertexFeat = outLayer->GetNextFeature();
        ASSERT_NE(vertexFeat, nullptr) << "vertex feature " << i << " missing";
        EXPECT_EQ(vertexFeat->GetFieldAsInteger("vertex_idx"), i);
        ASSERT_NE(vertexFeat->GetGeometryRef(), nullptr);
        EXPECT_EQ(wkbFlatten(vertexFeat->GetGeometryRef()->getGeometryType()), wkbPoint);
        const auto* vertexPoint = vertexFeat->GetGeometryRef()->toPoint();
        EXPECT_NEAR(vertexPoint->getX(), kExpectedVertices[i][0], 1e-9);
        EXPECT_NEAR(vertexPoint->getY(), kExpectedVertices[i][1], 1e-9);
        OGRFeature::DestroyFeature(vertexFeat);
        ++vertexSeen;
    }
    EXPECT_EQ(vertexSeen, 3);
    EXPECT_EQ(outLayer->GetNextFeature(), nullptr);
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

    // 两个端点必须分别是折线的 StartPoint 与 EndPoint：夹具 (0,0)-(5,5)-(10,0) 的首尾是
    // (0,0) 与 (10,0)，两点不重合，所以"只吐起点""首尾都取起点""端点写成重心"
    // 这三种变异在这里都会红（只数 feature 数则三种全绿）。
    outLayer->ResetReading();
    OGRFeature* startFeat = outLayer->GetNextFeature();
    ASSERT_NE(startFeat, nullptr);
    EXPECT_EQ(startFeat->GetFieldAsInteger("endpoint_type"), 0);
    const auto* startPoint = startFeat->GetGeometryRef()->toPoint();
    ASSERT_NE(startPoint, nullptr);
    EXPECT_NEAR(startPoint->getX(), 0.0, 1e-9);
    EXPECT_NEAR(startPoint->getY(), 0.0, 1e-9);
    OGRFeature::DestroyFeature(startFeat);

    OGRFeature* endFeat = outLayer->GetNextFeature();
    ASSERT_NE(endFeat, nullptr);
    EXPECT_EQ(endFeat->GetFieldAsInteger("endpoint_type"), 1);
    const auto* endPoint = endFeat->GetGeometryRef()->toPoint();
    ASSERT_NE(endPoint, nullptr);
    EXPECT_NEAR(endPoint->getX(), 10.0, 1e-9);
    EXPECT_NEAR(endPoint->getY(), 0.0, 1e-9);
    OGRFeature::DestroyFeature(endFeat);

    EXPECT_EQ(outLayer->GetNextFeature(), nullptr);
    GDALClose(outDs);
}

TEST_F(PluginTest, VectorMidpointsExtractExecution) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "e2e_midpoints_extract_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "e2e_midpoints_extract_output.gpkg");

    {
        // 两要素折线夹具。旧夹具是单条 2 顶点水平线 (0,0)-(10,0) ⇒ 只有 x 承重：
        // "端点平均""每段各取中点""0.25 分位"这些变异在水平线上都与真值重合（C3a-3b 实测）。
        // 换成每条 2 段、且 y 非零的折线后：
        //   A = (0,0)-(6,0)-(6,8)：总长 6+8=14 ⇒ 半分位 7 ⇒ 落在第二段、距起点 7-6=1 ⇒ (6, 1)
        //       （端点平均给 (3,4)、每段中点给 {(3,0),(6,4)}、起点给 (0,0)、0.25 分位给 (3.5,0) ⇒ 五个候选两两不同）
        //   B = (0,10)-(5,10)-(5,16)：总长 5+6=11 ⇒ 半分位 5.5 ⇒ 第二段上移 5.5-5=0.5 ⇒ (5, 10.5)
        // 两条线的 y 期望都是非零值 ⇒ y 这一维真的承重；x↔y 转置类变异在下面四条 EXPECT_NEAR 上必红。
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

        auto addLine = [&](const char* name,
                           const std::vector<std::pair<double, double>>& pts) {
            OGRLineString line;
            for (const auto& pt : pts) {
                line.addPoint(pt.first, pt.second);
            }
            auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
            feat->SetField("name", name);
            feat->SetGeometry(&line);
            const bool ok = layer->CreateFeature(feat) == OGRERR_NONE;
            OGRFeature::DestroyFeature(feat);
            return ok;
        };
        ASSERT_TRUE(addLine("A", {{0.0, 0.0}, {6.0, 0.0}, {6.0, 8.0}}));
        ASSERT_TRUE(addLine("B", {{0.0, 10.0}, {5.0, 10.0}, {5.0, 16.0}}));
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("midpoints_extract");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    // 每条线一个中点 ⇒ 两个输入要素给两个输出（派生不增删要素）。
    EXPECT_EQ(result.metadata.at("feature_count"), "2");

    GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
        output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(outDs, nullptr);
    OGRLayer* outLayer = outDs->GetLayer(0);
    ASSERT_NE(outLayer, nullptr);
    EXPECT_EQ(outLayer->GetFeatureCount(), 2);

    OGRFeature* featA = outLayer->GetNextFeature();
    ASSERT_NE(featA, nullptr);
    EXPECT_STREQ(featA->GetFieldAsString("name"), "A");
    ASSERT_NE(featA->GetGeometryRef(), nullptr);
    const auto* midA = featA->GetGeometryRef()->toPoint();
    ASSERT_NE(midA, nullptr);
    EXPECT_NEAR(midA->getX(), 6.0, 1e-6);
    EXPECT_NEAR(midA->getY(), 1.0, 1e-6);
    OGRFeature::DestroyFeature(featA);

    // name↔几何配对：若两条输出都写第一个要素的中点，B 的 (5,10.5) 会读成 (6,1) ⇒ x、y 一起红。
    OGRFeature* featB = outLayer->GetNextFeature();
    ASSERT_NE(featB, nullptr);
    EXPECT_STREQ(featB->GetFieldAsString("name"), "B");
    ASSERT_NE(featB->GetGeometryRef(), nullptr);
    const auto* midB = featB->GetGeometryRef()->toPoint();
    ASSERT_NE(midB, nullptr);
    EXPECT_NEAR(midB->getX(), 5.0, 1e-6);
    EXPECT_NEAR(midB->getY(), 10.5, 1e-6);
    OGRFeature::DestroyFeature(featB);
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
        // 夹具是 10x4 的矩形，不是正方形：x 与 y 的中心坐标不相等（5 与 2），
        // 于是"坐标轴写反"在这里必红；而矩形上"面积重心""包框中心""面内代表点"
        // 三种定义给出同一个点 (5,2)，所以这条期望值不与"interior_point 该用哪种算法"绑定，
        // 不会把有争议的语义当成真值钉进库（本批实测 interior_point 目前与 centroid 逐点同值，
        // 凹面上会把点放到形外，那条差异只登记进工单，不在这里固定）。
        ring.addPoint(0, 0);
        ring.addPoint(10, 0);
        ring.addPoint(10, 4);
        ring.addPoint(0, 4);
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
    EXPECT_NEAR(point->getX(), 5.0, 1e-6);
    EXPECT_NEAR(point->getY(), 2.0, 1e-6);
    // 代表点必须真的落在面上：这是 interior_point 区别于"随便一个中心"的定义级下界。
    OGRPolygon expectedPolygon;
    OGRLinearRing expectedRing;
    expectedRing.addPoint(0, 0);
    expectedRing.addPoint(10, 0);
    expectedRing.addPoint(10, 4);
    expectedRing.addPoint(0, 4);
    expectedRing.addPoint(0, 0);
    expectedPolygon.addRing(&expectedRing);
    EXPECT_TRUE(expectedPolygon.Contains(point));
    EXPECT_GE(point->getX(), 0.0);
    EXPECT_LE(point->getX(), 10.0);
    EXPECT_GE(point->getY(), 0.0);
    EXPECT_LE(point->getY(), 4.0);
    OGRFeature::DestroyFeature(outFeat);
    GDALClose(outDs);

    // N117：矩形夹具上"面积重心 / 包框中心 / 面内代表点"三种定义同值，抓不到
    // interior_point 用错算法。这一段专门用**凹面**把"代表点必须落在形内"这条定义级
    // 性质钉住。夹具是两条宽 1 的臂组成的 L：(0,0)-(10,0)-(10,1)-(1,1)-(1,10)-(0,10)。
    // 面积 = 横臂 10x1=10（形心 (5,0.5)）+ 竖臂 1x9=9（形心 (0.5,5.5)）= 19
    //   cx = (10*5 + 9*0.5)/19 = 54.5/19 = 2.8684
    //   cy = (10*0.5 + 9*5.5)/19 = 54.5/19 = 2.8684
    // 而该 L 形只在 y<=1 或 x<=1 的条带上有点，(2.8684, 2.8684) **不在形内**
    // ⇒ 面积重心（=现在的 Centroid()）在这里必红，面内代表点（PointOnSurface）必绿。
    // 不钉具体坐标（代表点本来不唯一），只钉性质 —— 避免把某个 GEOS 版本的选择当期望值写死。
    const std::string lInput = utf8PathString(getTestDir() / "e2e_interior_point_l_input.gpkg");
    const std::string lOutput = utf8PathString(getTestDir() / "e2e_interior_point_l_output.gpkg");
    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(lInput.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
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
        ring.addPoint(10, 1);
        ring.addPoint(1, 1);
        ring.addPoint(1, 10);
        ring.addPoint(0, 10);
        ring.addPoint(0, 0);
        poly.addRing(&ring);

        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", "L");
        feat->SetGeometry(&poly);
        ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> lParams;
    lParams["action"] = std::string("interior_point");
    lParams["input"] = lInput;
    lParams["output"] = lOutput;

    const auto lResult = p->execute(lParams, progress_);
    EXPECT_TRUE(lResult.success) << lResult.message;
    ASSERT_TRUE(fs::exists(lOutput));
    EXPECT_EQ(lResult.metadata.at("feature_count"), "1");

    OGRLinearRing lRing;
    lRing.addPoint(0, 0);
    lRing.addPoint(10, 0);
    lRing.addPoint(10, 1);
    lRing.addPoint(1, 1);
    lRing.addPoint(1, 10);
    lRing.addPoint(0, 10);
    lRing.addPoint(0, 0);
    OGRPolygon lShape;
    lShape.addRing(&lRing);

    GDALDataset* lDs = static_cast<GDALDataset*>(GDALOpenEx(
        lOutput.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(lDs, nullptr);
    OGRLayer* lLayer = lDs->GetLayer(0);
    ASSERT_NE(lLayer, nullptr);
    OGRFeature* lFeat = lLayer->GetNextFeature();
    ASSERT_NE(lFeat, nullptr);
    const auto* lPoint = lFeat->GetGeometryRef()->toPoint();
    ASSERT_NE(lPoint, nullptr);
    EXPECT_TRUE(lShape.Contains(lPoint))
        << "interior_point 给出的代表点不在凹面内部，坐标=("
        << lPoint->getX() << ", " << lPoint->getY() << ")";
    EXPECT_TRUE(lShape.IsPointOnSurface(lPoint))
        << "IsPointOnSurface 判假，坐标=(" << lPoint->getX() << ", " << lPoint->getY() << ")";
    OGRFeature::DestroyFeature(lFeat);
    GDALClose(lDs);
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

        // 夹具四个点（C3b-3 定稿，EPSG:3857）：(3,5)、(1,1)、(3,5)、(2,2)。
        // 键是 pointKey 把 x,y 定标到 10 位小数（vector_plugin_common.inc:626），**先到先得**记进
        // firstPointByKey ⇒ 只有第 3 个点 (3,5) 命中已有键 ⇒ 恰好 1 行、issue_count = 1；
        // (1,1) 与 (2,2) 各只出现一次 ⇒ 谁也不成对。算式就是这一条：重复对 = {(3,5) 的第 1 次与第 3 次}。
        // (1,1) 与 (2,2) 是两个谁也不挨的干扰点：它们撑出 scanned_count = 4 与 issue_count = 1 的差额，
        // 于是"把每个扫到的点都写一行"这类变异当场红。
        // WHY 把重复点从原点挪走（这一刀的存在理由）：B2 定稿的重复对是 (0,0) ⇒ 那一行是 `…,0,0`，
        //   **x 与 y 两维同时为 0** ⇒ "把坐标两列写成常量 0"这类变异在新臂也不会红，与 topology 那处、
        //   C3a 的 midpoints、C3b-B1 给 dangling_endpoint 换 L3 的 y=3 全是同族病（期望值等于零向量就不承重）。
        //   改成 (3,5) 后两维都非零 ⇒ x 列与 y 列各自成为能单独判别的断言。
        addPoint(3, 5);
        addPoint(1, 1);
        addPoint(3, 5);
        addPoint(2, 2);
        GDALClose(ds);
    }

    // R-B1-fid：两个 FID 列的期望值都从**输入侧**现读（创建顺序的第 1 个与第 3 个点）。
    const std::vector<long long> fids = readLayerFidsInCreationOrder(input);
    ASSERT_EQ(fids.size(), 4u)
        << "duplicate_point_check 夹具创建了 4 个点，重新打开输入层却没读回 4 个 FID";

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("duplicate_point_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    // 三个计数键一个不落。skipped_geometry_count 本夹具应为 0：四个几何都是 wkbPoint，
    // 既不会走 :87 的"非点几何"分支，也不会走 :83 的 MultiPoint 非点子几何防御臂。
    EXPECT_EQ(result.metadata.at("issue_count"), "1");
    EXPECT_EQ(result.metadata.at("scanned_count"), "4");
    EXPECT_EQ(result.metadata.at("skipped_geometry_count"), "0");

    const std::vector<std::string> lines = readCsvLines(output);
    ASSERT_EQ(lines.size(), 2u) << "duplicate_point_check CSV 应为 1 表头 + 1 数据行";
    EXPECT_EQ(lines[0], "source_fid,duplicate_fid,x,y");

    // 旧写法是手写 stringstream 按 ',' 切一遍、只比 fields[2]/fields[3] 两个字符串，整段删除：
    // **两个 FID 列一个都没钉** ⇒ "配对写反"和"把 duplicate_fid 也写成 source 的 fid"（Task 20
    // 修的就是同一 fid 那一族语义）都测不出；列数也只核到"切出 4 段"，没核每段是什么。
    const std::vector<std::string> row1 = splitCsvColumns(lines[1]);
    ASSERT_EQ(row1.size(), 4u)
        << "duplicate_point_check 数据行列数不是 4: " << csvColumnContext(row1, 0, lines[1]);
    const CsvInt64Column row1Source = csvColumnAsInt64(row1, 0);
    EXPECT_TRUE(row1Source.ok) << "source_fid 列不是整数: " << csvColumnContext(row1, 0, lines[1]);
    EXPECT_EQ(row1Source.value, fids[0])
        << "source_fid 应是**先到**的那个 (3,5)（创建顺序第 1 个点）: "
        << csvColumnContext(row1, 0, lines[1]);
    const CsvInt64Column row1Dup = csvColumnAsInt64(row1, 1);
    EXPECT_TRUE(row1Dup.ok) << "duplicate_fid 列不是整数: " << csvColumnContext(row1, 1, lines[1]);
    EXPECT_EQ(row1Dup.value, fids[2])
        << "duplicate_fid 应是**后到**的那个 (3,5)（创建顺序第 3 个点），两列不许写成同一个: "
        << csvColumnContext(row1, 1, lines[1]);
    const CsvDoubleColumn row1X = csvColumnAsDouble(row1, 2);
    EXPECT_TRUE(row1X.ok) << "x 列不是数字: " << csvColumnContext(row1, 2, lines[1]);
    // 算式：被写进 CSV 的是**后到**那个点的坐标，即夹具里第 3 个点 (3,5) ⇒ x = 3。
    // ⚠ 两维都必须非零正是本刀要买的东西：x=3 ⇒ "把 x 列写成常量 0"当场红在这里。
    EXPECT_NEAR(3.0, row1X.value, 1e-6)
        << "重复点的 x 应为 3.0: " << csvColumnContext(row1, 2, lines[1]);
    const CsvDoubleColumn row1Y = csvColumnAsDouble(row1, 3);
    EXPECT_TRUE(row1Y.ok) << "y 列不是数字: " << csvColumnContext(row1, 3, lines[1]);
    // 算式同上 ⇒ y = 5。（B2 当时如实上报"这一维不承重、按工单逐字采用没擅自改夹具"是对的；
    //   本刀把夹具换成 (3,5) 后这条欠账清掉，那一维现在承重了。）
    EXPECT_NEAR(5.0, row1Y.value, 1e-6)
        << "重复点的 y 应为 5.0: " << csvColumnContext(row1, 3, lines[1]);
}

// --- Task 20：duplicate_point_check 的 MultiPoint / 非点几何处置 ---
//
// 修复前 vector_plugin_duplicate_point_check.inc:49 对每个要素无条件写
//     normalized ? normalized->toPoint() : nullptr
// 而 OGRGeometry::toPoint() 的原文是（ogr_geometry.h:649-655）
//     /** Down-cast to OGRPoint*.  Implies prior checking that
//      *  wkbFlatten(getGeometryType()) == wkbPoint. */
//     inline OGRPoint *toPoint() { return cpl::down_cast<OGRPoint *>(this); }
// ⇒ 它既不校验类型、也**永不返回 nullptr**，:50 的 if (point && ...) 因此挡不住任何东西。
// 图层类型闸（同文件里 `if (geomType != wkbPoint && geomType != wkbMultiPoint)` 那道放行判据）
// 对 wkbPoint 与 wkbMultiPoint 都放行，所以 MultiPoint 图层会一路走到这次降型。
// 下面三条用例把"按类型分派后必须得到什么"钉死；坐标断言用整行相等，
// 以免修复前那种"把 OGRMultiPoint 当 OGRPoint 读出来的垃圾坐标"被宽松断言放过。

TEST_F(PluginTest, DuplicatePointCheckHandlesMultiPointLayer) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "t20_multipoint_dupes_input.geojson");
    const std::string output = utf8PathString(getTestDir() / "t20_multipoint_dupes_output.csv");
    {
        std::ofstream ofs(input);
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"type":"FeatureCollection","features":[)"
            << R"({"type":"Feature","properties":{"id":1},"geometry":{"type":"MultiPoint","coordinates":[[0,0],[1,1],[0,0]]}},)"
            << R"({"type":"Feature","properties":{"id":2},"geometry":{"type":"MultiPoint","coordinates":[[5,5]]}},)"
            << R"({"type":"Feature","properties":{"id":3},"geometry":{"type":"MultiPoint","coordinates":[[1,1],[9,9]]}})"
            << "]}";
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("duplicate_point_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    std::ifstream ifs(fs::u8path(output));
    ASSERT_TRUE(ifs.is_open());
    std::vector<std::string> lines;
    for (std::string line; std::getline(ifs, line);) {
        lines.push_back(line);
    }
    ASSERT_EQ(lines.size(), 3u) << "表头 + 两条重复记录，实际内容：" << [&] {
        std::string all;
        for (const auto& l : lines) { all += "[" + l + "]"; }
        return all;
    }();
    EXPECT_EQ(lines[0], "source_fid,duplicate_fid,x,y");
    // GeoJSON 驱动的 FID 从 1 起（修复前的实测行 "1,3,1,1" 里出现了 fid 3，而图层只有三个要素）。
    // 同一 MultiPoint 内部重复（R-T20-2 要求覆盖的那一档）：source 与 duplicate 同为 fid 1。
    EXPECT_EQ(lines[1], "1,1,0,0");
    // 跨要素重复：(1,1) 先到先得记在 fid 1，fid 3 的 (1,1) 被判为重复。
    EXPECT_EQ(lines[2], "1,3,1,1");

    EXPECT_EQ(result.metadata.at("issue_count"), "2") << "每对重复计一次，两档各一次";
    EXPECT_EQ(result.metadata.at("scanned_count"), "3") << "scanned_count 仍是要素数，不是点数";
    EXPECT_EQ(result.metadata.count("skipped_geometry_count"), 1u)
        << "非点几何要有显式跳过计数键（纯 MultiPoint 图层应为 0）";
    if (result.metadata.count("skipped_geometry_count")) {
        EXPECT_EQ(result.metadata.at("skipped_geometry_count"), "0");
    }
}

TEST_F(PluginTest, DuplicatePointCheckSkipsNonPointFeatureGeometry) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    // 图层声明为混合类型时 effectiveLayerGeometryType 取首个非空要素的类型（common.inc:425-446），
    // 这里首要素是 Point ⇒ 类型闸放行，后续 LineString 要素必须走"显式跳过并计数"分支。
    const std::string input = utf8PathString(getTestDir() / "t20_mixed_geom_dupes_input.geojson");
    const std::string output = utf8PathString(getTestDir() / "t20_mixed_geom_dupes_output.csv");
    {
        std::ofstream ofs(input);
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"type":"FeatureCollection","features":[)"
            << R"({"type":"Feature","properties":{"id":1},"geometry":{"type":"Point","coordinates":[1,1]}},)"
            << R"({"type":"Feature","properties":{"id":2},"geometry":{"type":"LineString","coordinates":[[1,1],[2,2],[3,3]]}},)"
            << R"({"type":"Feature","properties":{"id":3},"geometry":{"type":"Point","coordinates":[1,1]}})"
            << "]}";
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("duplicate_point_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    std::ifstream ifs(fs::u8path(output));
    ASSERT_TRUE(ifs.is_open());
    std::vector<std::string> lines;
    for (std::string line; std::getline(ifs, line);) {
        lines.push_back(line);
    }
    // LineString 的三个顶点（含 (1,1)）都不得参与重复点判定，否则这里会多出一行。
    ASSERT_EQ(lines.size(), 2u) << "只应有 Point 要素之间的一对重复，实际内容：" << [&] {
        std::string all;
        for (const auto& l : lines) { all += "[" + l + "]"; }
        return all;
    }();
    EXPECT_EQ(lines[0], "source_fid,duplicate_fid,x,y");
    // 三个要素的 fid 依次为 1/2/3（GeoJSON 驱动 FID 从 1 起）：
    // 重复对只能落在两个 Point 要素（fid 1 与 fid 3）之间，LineString 要素（fid 2）不得成为任一侧。
    EXPECT_EQ(lines[1], "1,3,1,1");

    EXPECT_EQ(result.metadata.at("issue_count"), "1");
    EXPECT_EQ(result.metadata.at("scanned_count"), "3") << "跳过的要素仍计入扫描数，否则进度/审计口径会变小";
    EXPECT_EQ(result.metadata.count("skipped_geometry_count"), 1u)
        << "非点几何不得静默 else 跳过，必须计数上报";
    if (result.metadata.count("skipped_geometry_count")) {
        EXPECT_EQ(result.metadata.at("skipped_geometry_count"), "1");
    }
}

TEST_F(PluginTest, DuplicatePointCheckMultiPointKeepsZOutOfDedupKey) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    // 既有去重键 pointKey（common.inc:626-631）是 setprecision(10) 的 "x,y" ⇒ 纯 2D、Z 被忽略。
    // 这是既有语义，本用例只负责把它钉住，防止后来人"顺手把 Z 也纳入键"而悄悄改掉判定口径。
    const std::string input = utf8PathString(getTestDir() / "t20_multipoint_z_input.geojson");
    const std::string output = utf8PathString(getTestDir() / "t20_multipoint_z_output.csv");
    {
        std::ofstream ofs(input);
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"type":"FeatureCollection","features":[)"
            << R"({"type":"Feature","properties":{"id":1},"geometry":{"type":"MultiPoint","coordinates":[[0,0,3],[0,0,7]]}})"
            << "]}";
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("duplicate_point_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    std::ifstream ifs(fs::u8path(output));
    ASSERT_TRUE(ifs.is_open());
    std::vector<std::string> lines;
    for (std::string line; std::getline(ifs, line);) {
        lines.push_back(line);
    }
    ASSERT_EQ(lines.size(), 2u) << "同 xy 不同 z 仍判为重复（键不含 z）";
    EXPECT_EQ(lines[1], "1,1,0,0") << "单个 MultiPoint 要素（fid 1）内部两点同 xy 不同 z";
    EXPECT_EQ(result.metadata.at("issue_count"), "1");
    EXPECT_EQ(result.metadata.at("scanned_count"), "1") << "scanned_count 是要素数：一个要素内的一对重复";
}

// 行为锁（修复前后都应为绿）：几何缺失（"geometry":null）必须安全跳过，既不能崩，
// 也不进 skipped_geometry_count —— 该键的口径是"有类型但不是点类的几何"，NULL 几何不属于这一类，
// 保持修复前 `normalized ? …` 的既有口径不变。这条同时让 if (normalized) 这道守卫可被变异杀掉。
TEST_F(PluginTest, DuplicatePointCheckToleratesMissingGeometry) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "t20_missing_geom_input.geojson");
    const std::string output = utf8PathString(getTestDir() / "t20_missing_geom_output.csv");
    {
        std::ofstream ofs(input);
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"type":"FeatureCollection","features":[)"
            << R"({"type":"Feature","properties":{"id":1},"geometry":{"type":"Point","coordinates":[2,2]}},)"
            << R"({"type":"Feature","properties":{"id":2},"geometry":null},)"
            << R"({"type":"Feature","properties":{"id":3},"geometry":{"type":"Point","coordinates":[2,2]}})"
            << "]}";
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("duplicate_point_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.metadata.at("issue_count"), "1");
    EXPECT_EQ(result.metadata.at("scanned_count"), "3") << "缺几何的要素仍计入扫描数";
    EXPECT_EQ(result.metadata.at("skipped_geometry_count"), "0")
        << "NULL 几何不是\"有类型但非点\"，沿用修复前口径不计入";

    std::ifstream ifs(fs::u8path(output));
    ASSERT_TRUE(ifs.is_open());
    std::vector<std::string> lines;
    for (std::string line; std::getline(ifs, line);) {
        lines.push_back(line);
    }
    ASSERT_EQ(lines.size(), 2u) << "缺几何要素不得被当成任何一侧";
    EXPECT_EQ(lines[0], "source_fid,duplicate_fid,x,y");
    EXPECT_EQ(lines[1], "1,3,2,2");
}

// 行为锁：EMPTY 点既不判重、也不计入 skipped_geometry_count（修复前 !point->IsEmpty() 的既有语义）。
// 两个 EMPTY 点若去掉这层过滤就会因键同为 nan 而被互判重复，故这条能杀掉"删除 IsEmpty 过滤"的变异。
TEST_F(PluginTest, DuplicatePointCheckIgnoresEmptyPoints) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr);

    const std::string input = utf8PathString(getTestDir() / "t20_empty_point_input.gpkg");
    const std::string output = utf8PathString(getTestDir() / "t20_empty_point_output.csv");
    {
        auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        ASSERT_NE(driver, nullptr);
        auto* ds = driver->Create(input.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
        ASSERT_NE(ds, nullptr);
        auto srs = std::make_unique<OGRSpatialReference>();
        srs->importFromEPSG(3857);
        auto* layer = ds->CreateLayer("t20_empty", srs.get(), wkbPoint, nullptr);
        ASSERT_NE(layer, nullptr);

        auto addGeometry = [&](OGRGeometry* geom) {
            auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
            feat->SetGeometry(geom);
            ASSERT_EQ(layer->CreateFeature(feat), OGRERR_NONE);
            OGRFeature::DestroyFeature(feat);
        };
        OGRPoint first(2.0, 2.0);
        addGeometry(&first);
        OGRPoint emptyPoint;   // 默认构造即 EMPTY（坐标 NaN），非 NULL 几何
        ASSERT_TRUE(emptyPoint.IsEmpty());
        addGeometry(&emptyPoint);
        addGeometry(&emptyPoint);
        OGRPoint last(2.0, 2.0);
        addGeometry(&last);
        GDALClose(ds);
    }

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("duplicate_point_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.metadata.at("issue_count"), "1") << "只有两个 (2,2) 构成重复，两个 EMPTY 点互不判重";
    EXPECT_EQ(result.metadata.at("scanned_count"), "4");
    EXPECT_EQ(result.metadata.at("skipped_geometry_count"), "0");

    std::ifstream ifs(fs::u8path(output));
    ASSERT_TRUE(ifs.is_open());
    std::vector<std::string> lines;
    for (std::string line; std::getline(ifs, line);) {
        lines.push_back(line);
    }
    ASSERT_EQ(lines.size(), 2u) << "EMPTY 点若被当普通点判重会多出一行 nan 坐标";
    EXPECT_EQ(lines[0], "source_fid,duplicate_fid,x,y");
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

        // 夹具三个要素（工单 §C3b-B2 定稿，EPSG:3857）：
        //   F1 = Polygon 带 **2 个内环** ⇒ hole_count = 2；
        //   F2 = Polygon **无洞** ⇒ 不报出（正是它撑出 scanned_count=3 与 issue_count=2 的差额）；
        //   F3 = MultiPolygon，两个子面分别带 **1 个**与 **3 个**内环 ⇒ hole_count = 1 + 3 = **4**。
        // 洞数做成 2 / 0 / 4 三个不同档：只有一档的话"hole_count 写死成常数"这类变异测不出来。
        // ⚠ F3 是 MultiPolygon 写进**声明为 wkbPolygon 的层**：GDAL 的 CheckGeometryType 只发一条
        //   CE_Warning 便照原样落盘，blob 自带类型 ⇒ 读回来仍是 wkbMultiPolygon，geometryHoleCount
        //   才会走 MultiPolygon 那一支把子面内环**加总**。层级闸看的是层声明类型
        //   （effectiveLayerGeometryType = wkbPolygon ∈ {Polygon, MultiPolygon}）⇒ 闸照过。
        auto makeRing = [](double minX, double minY, double maxX, double maxY) {
            OGRLinearRing ring;
            ring.addPoint(minX, minY);
            ring.addPoint(maxX, minY);
            ring.addPoint(maxX, maxY);
            ring.addPoint(minX, maxY);
            ring.addPoint(minX, minY);
            return ring;
        };

        // F1：外环 (0,0)-(10,10) + 两个互不重叠、都在外环内的内环。
        OGRPolygon withHole;
        OGRLinearRing f1Outer = makeRing(0, 0, 10, 10);
        withHole.addRing(&f1Outer);
        OGRLinearRing f1InnerA = makeRing(2, 2, 4, 4);
        OGRLinearRing f1InnerB = makeRing(6, 2, 8, 4);
        withHole.addRing(&f1InnerA);
        withHole.addRing(&f1InnerB);

        auto* feat1 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat1->SetGeometry(&withHole);
        ASSERT_EQ(layer->CreateFeature(feat1), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat1);

        // F2：只有一个外环 ⇒ getNumInteriorRings() = 0 ⇒ 不许报出，但要计入 scanned_count。
        OGRPolygon noHole;
        OGRLinearRing ring = makeRing(20, 0, 30, 10);
        noHole.addRing(&ring);

        auto* feat2 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat2->SetGeometry(&noHole);
        ASSERT_EQ(layer->CreateFeature(feat2), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat2);

        // F3：子面 A 带 1 个内环、子面 B 带 3 个内环 ⇒ 该要素的 hole_count = 1 + 3 = 4。
        OGRPolygon f3PartA;
        OGRLinearRing f3OuterA = makeRing(40, 0, 50, 10);
        f3PartA.addRing(&f3OuterA);
        OGRLinearRing f3InnerA = makeRing(42, 2, 44, 4);
        f3PartA.addRing(&f3InnerA);

        OGRPolygon f3PartB;
        OGRLinearRing f3OuterB = makeRing(60, 0, 70, 10);
        f3PartB.addRing(&f3OuterB);
        OGRLinearRing f3InnerB1 = makeRing(61, 1, 62, 2);
        OGRLinearRing f3InnerB2 = makeRing(64, 4, 65, 5);
        OGRLinearRing f3InnerB3 = makeRing(67, 7, 68, 8);
        f3PartB.addRing(&f3InnerB1);
        f3PartB.addRing(&f3InnerB2);
        f3PartB.addRing(&f3InnerB3);

        OGRMultiPolygon multiWithHoles;
        multiWithHoles.addGeometry(&f3PartA);
        multiWithHoles.addGeometry(&f3PartB);

        auto* feat3 = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat3->SetGeometry(&multiWithHoles);
        ASSERT_EQ(layer->CreateFeature(feat3), OGRERR_NONE);
        OGRFeature::DestroyFeature(feat3);
        GDALClose(ds);
    }

    // R-B1-fid：期望 FID 从**输入侧**现读；F3 是创建顺序的第 3 个要素 ⇒ fids[2]。
    const std::vector<long long> fids = readLayerFidsInCreationOrder(input);
    ASSERT_EQ(fids.size(), 3u)
        << "hole_check 夹具创建了 3 个要素，重新打开输入层却没读回 3 个 FID";

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("hole_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    // issue_count 的口径是**有洞的要素数**（实现每要素最多 ++issueCount，
    // vector_plugin_hole_check.inc:49-53），不是洞的总数。本夹具洞的总数 = 2 + 4 = 6，
    // 有洞的要素数 = 2 ⇒ 两个数不等，"2" 才算把这个口径钉住（写成 6 就是照抄误解）。
    EXPECT_EQ(result.metadata.at("issue_count"), "2");
    EXPECT_EQ(result.metadata.at("scanned_count"), "3");

    const std::vector<std::string> lines = readCsvLines(output);
    ASSERT_EQ(lines.size(), 3u) << "hole_check CSV 应为 1 表头 + 2 数据行（F2 无洞不许报出）";
    EXPECT_EQ(lines[0], "source_fid,geom_type,hole_count");

    // 实现按读入顺序写行 ⇒ 第 1 数据行是 F1、第 2 是 F3（F2 被跳过）。
    // 旧判据 `lines[1].find("Polygon")` / `lines[1].find(",1")` 整段删除，不留新旧并存：
    // ",1" 对列位（fid 里就有 1）、基数（F2 若被误报成 hole_count=1 也命中）都不敏感。
    const std::vector<std::string> row1 = splitCsvColumns(lines[1]);
    ASSERT_EQ(row1.size(), 3u)
        << "hole_check 第 1 数据行列数不是 3: " << csvColumnContext(row1, 0, lines[1]);
    const CsvInt64Column row1Fid = csvColumnAsInt64(row1, 0);
    EXPECT_TRUE(row1Fid.ok) << "source_fid 列不是整数: " << csvColumnContext(row1, 0, lines[1]);
    EXPECT_EQ(row1Fid.value, fids[0]) << "source_fid 不是 F1: " << csvColumnContext(row1, 0, lines[1]);
    EXPECT_EQ(row1[1], "Polygon")
        << "geom_type 列应为 Polygon（blob 自带类型，不受层声明类型影响）: "
        << csvColumnContext(row1, 1, lines[1]);
    const CsvDoubleColumn row1Holes = csvColumnAsDouble(row1, 2);
    EXPECT_TRUE(row1Holes.ok) << "hole_count 列不是数字: " << csvColumnContext(row1, 2, lines[1]);
    // 算式：F1 = 1 个外环 + 2 个内环 ⇒ getNumInteriorRings() = 2（Polygon 分支，common.inc:638）。
    // ⚠ R-C3b-1 选 ②：hole_count 是**内环计数**、不是量测，写成浮比只为了让机器级判据
    //   CoverageMeta.GoldEvidenceIsAuditable 认它作数值断言；1e-9 远小于任何整数偏差，语义损失为零。
    EXPECT_NEAR(2.0, row1Holes.value, 1e-9)
        << "F1 的内环计数应为 2: " << csvColumnContext(row1, 2, lines[1]);

    const std::vector<std::string> row2 = splitCsvColumns(lines[2]);
    ASSERT_EQ(row2.size(), 3u)
        << "hole_check 第 2 数据行列数不是 3: " << csvColumnContext(row2, 0, lines[2]);
    const CsvInt64Column row2Fid = csvColumnAsInt64(row2, 0);
    EXPECT_TRUE(row2Fid.ok) << "source_fid 列不是整数: " << csvColumnContext(row2, 0, lines[2]);
    EXPECT_EQ(row2Fid.value, fids[2])
        << "source_fid 不是 F3（F2 无洞，不许占这一行）: " << csvColumnContext(row2, 0, lines[2]);
    EXPECT_EQ(row2[1], "MultiPolygon")
        << "geom_type 列应为 MultiPolygon: " << csvColumnContext(row2, 1, lines[2]);
    const CsvDoubleColumn row2Holes = csvColumnAsDouble(row2, 2);
    EXPECT_TRUE(row2Holes.ok) << "hole_count 列不是数字: " << csvColumnContext(row2, 2, lines[2]);
    // 算式：F3 = 子面 A(1 内环) + 子面 B(3 内环) ⇒ MultiPolygon 分支逐子面累加 = 1 + 3 = 4
    //   （common.inc:639-645）。这条同时钉住"只数第一个子面"（会给 1）与"数外环"（会给 2+…）两类变异。
    EXPECT_NEAR(4.0, row2Holes.value, 1e-9)
        << "F3 的内环计数应为 1+3=4: " << csvColumnContext(row2, 2, lines[2]);
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

        // 夹具定稿四条线（工单："一次定稿，不许派发后再改形状"），EPSG:3857：
        //   L1 (0,0)-(1,0)、L2 (1,0)-(2,0)、L3 (10,3)-(11,3)、L4 闭合环 (20,20)-(21,20)-(21,21)-(20,20)。
        // 端点出现次数手推（实现把每条线只取**首末两个点**，中间顶点根本不参与判定）：
        //   (0,0) 1 次 ⇒ 悬空；(1,0) 2 次（L1 末点 + L2 起点）⇒ 非悬空；(2,0) 1 次 ⇒ 悬空；
        //   (10,3) 1 次 ⇒ 悬空；(11,3) 1 次 ⇒ 悬空；
        //   (20,20) 2 次（L4 的首点与末点是同一个坐标）⇒ 非悬空，
        //   而 L4 的 (21,20) 与 (21,21) 是中间顶点，压根不入端点表 ⇒ 闭合环没有悬空端点。
        // ⇒ 悬空 4 条 ⇒ issue_count = 4、CSV = 1 表头 + 4 数据行。
        // WHY L3 的 y 要写成 3 而不是 0：旧夹具所有端点 y 全 0 ⇒ "y 列写成 0"这种变异测不出
        //   （与 C3a 的 midpoints 同一族病）；y=3 让 y 这一维承重。
        // WHY 要 L4：全仓此前没有"闭合环"这种输入，加进来才钉得住"首末同点 ⇒ 出现 2 次 ⇒ 非悬空"。
        addLine({{0, 0}, {1, 0}});                                    // L1
        addLine({{1, 0}, {2, 0}});                                    // L2（与 L1 在 (1,0) 相接）
        addLine({{10, 3}, {11, 3}});                                  // L3（y=3，让 y 维承重）
        addLine({{20, 20}, {21, 20}, {21, 21}, {20, 20}});            // L4（闭合环）
        GDALClose(ds);
    }

    // R-B1-fid：期望 FID 从输入侧现读，不从产出的 CSV 回填。
    const std::vector<long long> fids = readLayerFidsInCreationOrder(input);
    ASSERT_EQ(fids.size(), 4u)
        << "dangling_endpoint_check 夹具创建了 4 条线，重新打开输入层却没读回 4 个 FID";

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("dangling_endpoint_check");
    params["input"] = input;
    params["output"] = output;

    const auto result = p->execute(params, progress_);
    EXPECT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output));
    EXPECT_EQ(result.metadata.at("issue_count"), "4");
    // 事实表口径：scanned_count = 扫描的**源要素数**（不是端点数）。夹具创建了 4 条线 ⇒ 4。
    EXPECT_EQ(result.metadata.at("scanned_count"), "4");

    const std::vector<std::string> lines = readCsvLines(output);
    ASSERT_EQ(lines.size(), 5u) << "dangling_endpoint_check CSV 应为 1 表头 + 4 数据行";
    EXPECT_EQ(lines[0], "source_fid,endpoint_type,x,y");

    // 实现按端点表的采集顺序（要素读入顺序 × 先 start 后 end）写行，只写出现 1 次的那些 ⇒
    //   第 1 行 (L1, start, 0, 0)、第 2 行 (L2, end, 2, 0)、第 3 行 (L3, start, 10, 3)、
    //   第 4 行 (L3, end, 11, 3)。
    // 旧判据是四条 `find(",0,0,0")` 之类的子串：子串对列位与基数都不敏感
    //（endpoint_type 与 x 串味、多写一行少写一行都可能命中），整段删掉换成拆列逐列比。
    // endpoint_type 的口径：**列名像文本、实为数字码** 0 = start / 1 = end（CHANGELOG 已记，
    // 不是这里靠猜）；钉数字码而不是钉 "start"/"end" 字样。
    struct ExpectedRow {
        std::size_t fidIndex;
        long long endpointType;
        double x;
        double y;
    };
    const std::vector<ExpectedRow> expected = {
        {0, 0, 0.0, 0.0},    // L1 起点 (0,0)：只出现 1 次 ⇒ 悬空，type = 0 = start
        {1, 1, 2.0, 0.0},    // L2 末点 (2,0)：只出现 1 次 ⇒ 悬空，type = 1 = end
        {2, 0, 10.0, 3.0},   // L3 起点 (10,3)
        {2, 1, 11.0, 3.0},   // L3 末点 (11,3)
    };
    ASSERT_EQ(expected.size() + 1, lines.size()) << "期望行数与 CSV 行数不一致";
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const std::string& rawLine = lines[i + 1];
        const std::vector<std::string> columns = splitCsvColumns(rawLine);
        ASSERT_EQ(columns.size(), 4u) << "第 " << i + 1 << " 数据行列数不是 4: "
                                     << csvColumnContext(columns, 0, rawLine);
        const CsvInt64Column source = csvColumnAsInt64(columns, 0);
        EXPECT_TRUE(source.ok) << "source_fid 列不是整数: " << csvColumnContext(columns, 0, rawLine);
        EXPECT_EQ(source.value, fids[expected[i].fidIndex])
            << "source_fid 不是第 " << expected[i].fidIndex << " 条线: "
            << csvColumnContext(columns, 0, rawLine);
        const CsvInt64Column type = csvColumnAsInt64(columns, 1);
        EXPECT_TRUE(type.ok) << "endpoint_type 列不是整数码: " << csvColumnContext(columns, 1, rawLine);
        EXPECT_EQ(type.value, expected[i].endpointType)
            << "endpoint_type 码不符（0=start/1=end）: " << csvColumnContext(columns, 1, rawLine);
        const CsvDoubleColumn x = csvColumnAsDouble(columns, 2);
        EXPECT_TRUE(x.ok) << "x 列不是数字: " << csvColumnContext(columns, 2, rawLine);
        EXPECT_NEAR(expected[i].x, x.value, 1e-6)
            << "x 列不符: " << csvColumnContext(columns, 2, rawLine);
        const CsvDoubleColumn y = csvColumnAsDouble(columns, 3);
        EXPECT_TRUE(y.ok) << "y 列不是数字: " << csvColumnContext(columns, 3, rawLine);
        // y 这一维靠 L3 的 3.0 承重：全 0 的旧夹具下"y 写死 0"的变异测不出。
        EXPECT_NEAR(expected[i].y, y.value, 1e-6)
            << "y 列不符: " << csvColumnContext(columns, 3, rawLine);
    }
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

// ── Task 11：fill_sinks 的 priority-flood 行为锁 ──
namespace {

struct FillRunOutput {
    gis::framework::Result result;
    std::string output;
};

// 统一的 fill_sinks 调用（与本文件的 TerrainFillSinksAndFlowDirectionExecution 同构）。
// extra 用来叠加 epsilon 这类可选键，让同一份夹具能被多条用例复用。
FillRunOutput runFillSinks(gis::framework::IGisPlugin* plugin,
                           gis::core::ProgressReporter& progress,
                           const std::string& input,
                           const std::string& outputName,
                           const std::map<std::string, gis::framework::ParamValue>& extra = {}) {
    FillRunOutput run;
    run.output = utf8PathString(getTestDir() / outputName);
    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("fill_sinks");
    params["input"] = input;
    params["output"] = run.output;
    params["band"] = 1;
    params["z_factor"] = 1.0;
    for (const auto& [key, value] : extra) {
        params[key] = value;
    }
    run.result = plugin->execute(params, progress);
    return run;
}

// 两像元平底坑：四周均匀 10.0，坑由 (4,4) 与 (4,5) 两个 2.0 像元组成。
// WHY 两个像元而不是一个：单像元坑在"邻域极值 + 不动点"那版里也会被一次填满，
// 平底（坑内多个像元等高的水平面）才是那版的结构性死角。
std::string makeFlatPitRaster(const std::string& name) {
    const int w = 9, h = 9;
    std::vector<float> data(static_cast<size_t>(w * h), 10.0f);
    data[4 * w + 4] = 2.0f;
    data[4 * w + 5] = 2.0f;
    return gis::tests::writeFloatRaster(getTestDir(), name, w, h, data);
}

// 嵌套洼地：外圈 4.0，3x3 平底 1.0，最中心再挖一个 0.5。
// WHY：洼地里有洼地 ⇒ 出水目标必须逐层传递（内坑要抬到外平底的水面，外平底要抬到 4.0），
// 这正是有限轮次不动点会"泄漏"（填不满或只填一层）的形态。
std::string makeNestedPitRaster(const std::string& name) {
    const int w = 9, h = 9;
    std::vector<float> data(static_cast<size_t>(w * h), 4.0f);
    for (int y = 3; y <= 5; ++y) {
        for (int x = 3; x <= 5; ++x) {
            data[y * w + x] = 1.0f;
        }
    }
    data[4 * w + 4] = 0.5f;
    return gis::tests::writeFloatRaster(getTestDir(), name, w, h, data);
}

// 低沟坑：9x9 均匀 10.0，但 y=4 一行从数据集左沿到中心开出高程 5.0 的沟（x=0..3），
// 中心 (4,4) 是 2.0 的坑 ⇒ 这个坑有两条出水路径：经沟（水面 5.0）和经高地（水面 10.0）。
std::string makeTrenchPitRaster(const std::string& name) {
    const int w = 9, h = 9;
    std::vector<float> data(static_cast<size_t>(w * h), 10.0f);
    for (int x = 0; x <= 3; ++x) {
        data[4 * w + x] = 5.0f;
    }
    data[4 * w + 4] = 2.0f;
    return gis::tests::writeFloatRaster(getTestDir(), name, w, h, data);
}

std::vector<float> readWholeBand(const std::string& path, int width, int height) {
    auto ds = gis::core::openRaster(path, true);
    std::vector<float> data(static_cast<size_t>(width * height), 0.0f);
    ds->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, width, height, data.data(),
                                   width, height, GDT_Float32, 0, 0);
    return data;
}

// 斜坡上的两像元平底坑：背景沿 y 每行升高 0.2（y=0 为 10.0，y=8 为 11.6），
// 把 (x=4,y=3) 与 (x=4,y=4) 挖成 2.0 —— 前者是离出口近的平底，后者是坑的最里一格。
// WHY 用斜坡而不用平地当 epsilon 的夹具：ε 的施与判据是"value <= 出水面"，而与出水面
// **等高**的像元同样算洼地的一部分（平底就是靠这条爬出来的）。平地背景（四周都 10.0）会让
// 整幅图逐圈拿到 ε，"洼地外像元必须不变"就没法逐像元写成断言。斜坡上每个像元的出水面都
// 严格低于它自己（最低出水面的父像元是它上面那一行 10.0+0.2(y-1)），于是"除坑以外
// 一个像元都不许动"是硬事实；而相邻行只差 0.2 < ε=0.5 又正好让"给真实地形加高"那种
// 写反的条件无处遁形。
std::string makeSlopePitRaster(const std::string& name) {
    const int w = 9, h = 9;
    std::vector<float> data(static_cast<size_t>(w * h), 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            data[y * w + x] = 10.0f + 0.2f * static_cast<float>(y);
        }
    }
    data[3 * w + 4] = 2.0f;
    data[4 * w + 4] = 2.0f;
    return gis::tests::writeFloatRaster(getTestDir(), name, w, h, data);
}

// 本夹具里坑的出水面：坑的父像元是 y=2 那一行（10.0 + 0.2*2）。
constexpr float kSlopePourAtRow2 = 10.0f + 0.2f * 2.0f;   // 10.4：两格平底填平后的水面
constexpr float kSlopePourAtRow3 = 10.0f + 0.2f * 3.0f;   // 10.6：最里一格的另一条出水路径

// 整片等高的平地（side x side），可选在正中央挖一个单像元坑。
// WHY 用"全图等高"的夹具来锁 epsilon 的作用域：等高背景里每个像元都恰好与把它入队的水面同高，
// 填洼对它们是原地不动（没有被抬过），于是"ε 只许落在真被填洼抬过的像元上"这条判据能被逐像元
// 写成断言。斜坡背景做不到这件事——斜坡上没有任何像元与出水面等高，作用域写错也看不出来。
std::string makeUniformPitRaster(const std::string& name, int side, float background,
                                 bool digCentrePit, float pitValue) {
    std::vector<float> data(static_cast<size_t>(side) * side, background);
    if (digCentrePit) {
        data[static_cast<size_t>(side / 2) * side + side / 2] = pitValue;
    }
    return gis::tests::writeFloatRaster(getTestDir(), name, side, side, data);
}

// 从数据集边缘出发、沿"非降"路径反向淹没，数没有被到达的像元 = 输出里仍存在的封闭洼地。
// WHY 必须是 8 邻域：填洼与下游 D8 流向共用同一套 8 邻域，按 4 邻域会把"只能经角点排水"的
// 像元误判成封闭汇（对角门那个夹具就是这么暴露的），所以这里不得改成 4 邻域。
// WHY 需要这条通式：ε 会把水面逐层抬高，"输出不留封闭洼地"这条承诺在 ε>0 时才真正受考验——
// 只在 ε=0 上验过等于没验（抬升累积有可能反过来把洼地自己的出海口顶高）。
size_t countClosedCells(const std::vector<float>& data, int width, int height,
                        const std::vector<uint8_t>& valid) {
    std::vector<uint8_t> seen(data.size(), 0);
    std::vector<size_t> order;
    auto enqueue = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        const size_t i = static_cast<size_t>(y) * width + x;
        if (!valid[i] || seen[i]) {
            return;
        }
        seen[i] = 1;
        order.push_back(i);
    };
    for (int x = 0; x < width; ++x) {
        enqueue(x, 0);
        enqueue(x, height - 1);
    }
    for (int y = 0; y < height; ++y) {
        enqueue(0, y);
        enqueue(width - 1, y);
    }
    for (size_t head = 0; head < order.size(); ++head) {
        const size_t i = order[head];
        const int y = static_cast<int>(i / static_cast<size_t>(width));
        const int x = static_cast<int>(i % static_cast<size_t>(width));
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const int ny = y + dy;
                const int nx = x + dx;
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                    continue;
                }
                const size_t j = static_cast<size_t>(ny) * width + nx;
                if (!valid[j] || seen[j] || data[j] < data[i]) {
                    continue;
                }
                seen[j] = 1;
                order.push_back(j);
            }
        }
    }
    size_t closed = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        if (valid[i] && !seen[i]) {
            ++closed;
        }
    }
    return closed;
}

} // namespace

TEST_F(PluginTest, FillSinksLevelsMultiCellFlatBottomPit) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    const std::string input = makeFlatPitRaster("fill_flat_input.tif");
    const auto run = runFillSinks(p, progress_, input, "fill_flat_output.tif");
    ASSERT_TRUE(run.result.success) << run.result.message;
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 4, 4), 10.0f, 1e-4f);
    // WHY 是 (5,4) 不是 (4,5)：坑的第二像元写的是 data[4*w+5] ⇒ 行 4 列 5。
    // 计划片段按 (x=4,y=5) 读，读到的是坑外的 10.0 像元，那条断言在旧实现下也会绿，
    // 平底就只剩一个像元被断住。
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 5, 4), 10.0f, 1e-4f);
    // 坑外不得被顺手抬高：平底之外的 10.0 必须逐像元保持 10.0。
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 9; ++x) {
            if ((x == 4 && y == 4) || (x == 5 && y == 4)) {
                continue;
            }
            EXPECT_NEAR(gis::tests::readPixel(run.output, 1, x, y), 10.0f, 1e-4f)
                << "x=" << x << " y=" << y;
        }
    }
}

TEST_F(PluginTest, FillSinksLevelsNestedDepressions) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    const std::string input = makeNestedPitRaster("fill_nested_input.tif");
    const auto run = runFillSinks(p, progress_, input, "fill_nested_output.tif");
    ASSERT_TRUE(run.result.success) << run.result.message;
    for (int y = 3; y <= 5; ++y) {
        for (int x = 3; x <= 5; ++x) {
            EXPECT_NEAR(gis::tests::readPixel(run.output, 1, x, y), 4.0f, 1e-4f)
                << "x=" << x << " y=" << y;
        }
    }
}

// 性质断言：填洼是"抬升"运算，任何像元都不许被压低（水文上填洼只会增加体积）。
// WHY 三个夹具都要跑（F4）：这条性质得先能表达"降低"才谈得上守卫它。上一版只跑嵌套洼地那幅图，
// 而它的背景值 4.0 恰好等于平底的水面 4.0 ⇒ 任何"把像元压到出水面"的实现（无条件赋值那一类）
// 在这幅图上不会低于原值，于是这条用例在它唯一的夹具上无法失败——审查者仿真 d6 证实 #206 不红。
// 斜坡图（每行 +0.2）里每个非坑像元都严格高于把它入队的水面 ⇒ 压到出水面就是降低，抓得住；
// 低沟图（背景 10.0 / 沟 5.0 / 坑 2.0）补一个"出水目标来自更低的另一条路径"的形态。
// 嵌套那幅仍然留着：它是上一轮的锚，只是不再独自承担这条性质。
TEST_F(PluginTest, FillSinksIsMonotonicAndNeverLowersTerrain) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    const std::vector<std::pair<std::string, std::string>> fixtures = {
        {"嵌套洼地（背景恰等于出水面，降低在这幅图上不可表示，留作历史锚）",
         makeNestedPitRaster("fill_mono_input.tif")},
        {"斜坡平底坑（每个非坑像元都严格高于出水面，降低在这里可表示）",
         makeSlopePitRaster("fill_mono_slope_input.tif")},
        {"低沟坑（出水路径低于背景）",
         makeTrenchPitRaster("fill_mono_trench_input.tif")},
    };
    for (size_t f = 0; f < fixtures.size(); ++f) {
        const std::string& tag = fixtures[f].first;
        const std::string input = fixtures[f].second;
        const auto run = runFillSinks(p, progress_, input,
                                      "fill_mono_" + std::to_string(f) + "_output.tif");
        ASSERT_TRUE(run.result.success) << tag << " " << run.result.message;
        const auto before = readWholeBand(input, 9, 9);
        const auto after = readWholeBand(run.output, 9, 9);
        ASSERT_EQ(before.size(), after.size());
        size_t lowered = 0;
        size_t firstIndex = 0;
        for (size_t i = 0; i < before.size(); ++i) {
            if (after[i] < before[i] - 1e-4f) {
                if (!lowered) {
                    firstIndex = i;
                }
                ++lowered;
            }
        }
        EXPECT_EQ(lowered, static_cast<size_t>(0))
            << tag << "：填洼不得降低地形，实际有 " << lowered << " 个像元被压低，第一个 index="
            << firstIndex << " 从 " << before[firstIndex] << " 变成 " << after[firstIndex];
    }
}

// R-T11-1 的判别器：洼地到出口存在两条不同高程的路径时，priority-flood 必须给
// "沿非降路径可达的**最低**出水面"，即中心坑只能抬到沟的 5.0。
// WHY 必须有这条：其余洼地用例的洼地外圈是**均匀**高程（10.0 / 4.0），无论堆是从低到高
// 还是从高到低弹出，抬升目标都恰好等于那唯一的外圈值 ⇒ 最大堆版本的过度抬升在这些
// 用例上照样全绿。只有"多条出水路径"才把弹出方向暴露出来。
TEST_F(PluginTest, FillSinksFillsToLowestPourPathNotHighest) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    const std::string input = makeTrenchPitRaster("fill_trench_input.tif");
    const auto run = runFillSinks(p, progress_, input, "fill_trench_output.tif");
    ASSERT_TRUE(run.result.success) << run.result.message;

    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 4, 4), 5.0f, 1e-4f)
        << "中心坑必须抬到经沟这条**更低**的出水路径（5.0）；得到 10.0 说明堆是从高到低弹出的";
    for (int x = 0; x <= 4; ++x) {
        EXPECT_NEAR(gis::tests::readPixel(run.output, 1, x, 4), 5.0f, 1e-4f) << "x=" << x;
    }
    // 高地一侧的真实地形（含 10.0 的平坦背景）一个都不许被抬：全幅最大值仍必须是 10.0。
    const auto after = readWholeBand(run.output, 9, 9);
    const float maxValue = *std::max_element(after.begin(), after.end());
    EXPECT_NEAR(maxValue, 10.0f, 1e-4f) << "priority-flood 不得把洼地以外的真实地形抬高";
}

// R-T11-2 的默认值锁：ε=0.0（GUI 全默认提交发来的就是它）与完全不提交 epsilon 必须逐像元一致。
// WHY 单独一条：ε 一旦被写成"无条件叠加"或默认值被改成非 0，所有洼地都会被顺手抬高，
// 而其余用例只断"填到水面"，看不出这 0.x 的漂移。
TEST_F(PluginTest, FillSinksEpsilonZeroEqualsUnconfiguredRun) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    const std::string input = makeSlopePitRaster("fill_eps0_input.tif");

    const auto plain = runFillSinks(p, progress_, input, "fill_eps0_plain_output.tif");
    ASSERT_TRUE(plain.result.success) << plain.result.message;
    const auto explicitZero = runFillSinks(p, progress_, input, "fill_eps0_explicit_output.tif",
                                           {{"epsilon", 0.0}});
    ASSERT_TRUE(explicitZero.result.success) << explicitZero.result.message;

    const auto before = readWholeBand(input, 9, 9);
    const auto a = readWholeBand(plain.output, 9, 9);
    const auto b = readWholeBand(explicitZero.output, 9, 9);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]) << "index " << i << "：epsilon=0.0 与未启用必须完全一致";
        EXPECT_GE(a[i], before[i] - 1e-4f) << "index " << i << " 填洼不得降低地形";
    }
    // 平底两格都填到同一个水面（这才是"洼地内不残存封闭水面"的判据本身）。
    EXPECT_NEAR(gis::tests::readPixel(plain.output, 1, 4, 3), kSlopePourAtRow2, 1e-4f);
    EXPECT_NEAR(gis::tests::readPixel(plain.output, 1, 4, 4), kSlopePourAtRow2, 1e-4f);
}

// R-T11-2 的语义锁：ε>0 只在洼地内形成离出口单调递增的微梯度，洼地外一个像元都不许动。
// 计划片段写的是 `epsilon > 0.0 && value > top.first`：像元刚被抬到 top.first 时该条件为假
// ⇒ 洼地一个 ε 都拿不到，反倒是本来就高于出水面的真实地形被抬到 top.first+ε（语义与 Produces
// 相反）。本用例两个方向都断，后者正是那条条件会打红的位置。
TEST_F(PluginTest, FillSinksEpsilonGradientsDepressionAndKeepsHigherTerrain) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    const std::string input = makeSlopePitRaster("fill_eps_input.tif");
    const double epsilon = 0.5;
    const auto run = runFillSinks(p, progress_, input, "fill_eps_output.tif",
                                  {{"epsilon", epsilon}});
    ASSERT_TRUE(run.result.success) << run.result.message;

    const float nearCell = gis::tests::readPixel(run.output, 1, 4, 3);   // 离出口近的平底格
    const float deepCell = gis::tests::readPixel(run.output, 1, 4, 4);   // 最里一格
    // (a) 洼地内的像元必须拿到 ε，且沿入队（淹没）方向严格递增：父出水面 + ε ⇒ 水仍能往外排。
    EXPECT_NEAR(nearCell, kSlopePourAtRow2 + static_cast<float>(epsilon), 1e-4f)
        << "被填平的像元必须抬到 出水面+ε；只抬到出水面说明 ε 的条件写反了";
    EXPECT_GT(nearCell, kSlopePourAtRow2) << "洼地内必须拿到 ε";
    EXPECT_GT(deepCell, nearCell) << "洼地内沿入队顺序必须严格递增（离出口越远水面越高）";

    // (b2) 最里一格的水面必须是"对角方向那一行"的 10.6 + ε。
    // WHY 单独一条并且带数值（F3）：这一格只经 8 邻域才够得着 y=2 那行的出水面；邻居扩展若退化成
    // 4 邻域，它就只能从正上方的 10.8 那一行爬 ⇒ 得到 11.3 而不是 11.1。以前这个错误只被一句
    // "洼地外的真实地形必须逐像元不变"抓到，文案不指向邻域，会把改错方向的人带走。
    EXPECT_NEAR(deepCell, kSlopePourAtRow3 + static_cast<float>(epsilon), 1e-4f)
        << "洼地内的出水目标由 8 邻域决定（10.6 那一行）；得到 10.8+ε 说明邻居扩展只走了 4 邻域";

    // (b1) ε 的作用域：变化只能发生在两个坑像元上，其余像元（没被填洼抬过）逐像元原值。
    const auto before = readWholeBand(input, 9, 9);
    const auto after = readWholeBand(run.output, 9, 9);
    ASSERT_EQ(before.size(), after.size());
    std::vector<size_t> changed;
    for (size_t i = 0; i < before.size(); ++i) {
        const bool isPit = (i == static_cast<size_t>(3 * 9 + 4)) ||
                           (i == static_cast<size_t>(4 * 9 + 4));
        if (isPit) {
            EXPECT_GT(after[i], before[i]) << "index " << i << " 坑内像元必须被抬升";
            continue;
        }
        if (after[i] != before[i]) {
            changed.push_back(i);
        }
    }
    EXPECT_TRUE(changed.empty())
        << "ε 落到了没有被填洼抬过的像元上（参数描述说的是洼地内），实际改动 " << changed.size()
        << " 个，第一个 x=" << (changed.empty() ? 0u : static_cast<unsigned>(changed.front() % 9))
        << " y=" << (changed.empty() ? 0u : static_cast<unsigned>(changed.front() / 9))
        << "，其值从 " << (changed.empty() ? 0.0f : before[changed.front()]) << " 变成 "
        << (changed.empty() ? 0.0f : after[changed.front()]);
}

// F1（epsilon 的作用域）：ε 只许落在"被填洼抬过"的像元上，纯平地一格都不许动。
// WHY 这条是判别用例：把实现里的判据从 value < top.first 退回上一版的 value <= top.first，
// 与出水面等高的背景像元也会被判成洼地、并把抬后值推进堆 ⇒ 平地整幅逐圈累积成锥体，锥体再反过来
// 把坑自己的出水面顶高。<= 版实测：21x21 纯平地 ε=10 改动 361 个像元；平地+中心单坑 ε=10 时中心
// 是 105.0 而不是 15.0。参数描述写的是 inside filled depressions ⇒ 这是产品高程错 + 描述不符，
// 不是风格问题。
TEST_F(PluginTest, FillSinksEpsilonOnlyTouchesCellsTheFillRaised) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    const int side = 21;
    const double epsilon = 10.0;
    const float background = 5.0f;
    const size_t centre = static_cast<size_t>(side / 2) * side + side / 2;
    auto coord = [side](size_t i) {
        return "x=" + std::to_string(i % static_cast<size_t>(side)) +
               " y=" + std::to_string(i / static_cast<size_t>(side));
    };

    // (1) 纯平地：没有任何像元被填洼抬过 ⇒ ε 再大也必须逐像元保持原值。
    const std::string flatInput = makeUniformPitRaster("fill_eps_plain_input.tif", side, background,
                                                       false, 0.0f);
    const auto flat = runFillSinks(p, progress_, flatInput, "fill_eps_plain_output.tif",
                                   {{"epsilon", epsilon}});
    ASSERT_TRUE(flat.result.success) << flat.result.message;
    const auto flatBefore = readWholeBand(flatInput, side, side);
    const auto flatAfter = readWholeBand(flat.output, side, side);
    ASSERT_EQ(flatBefore.size(), flatAfter.size());
    std::vector<size_t> flatOffenders;
    for (size_t i = 0; i < flatBefore.size(); ++i) {
        if (flatAfter[i] != flatBefore[i]) {
            flatOffenders.push_back(i);
        }
    }
    EXPECT_TRUE(flatOffenders.empty())
        << "纯平地上没有被填洼抬过的像元，ε>0 却改动了 " << flatOffenders.size()
        << " 个，第一个 " << (flatOffenders.empty() ? std::string("无") : coord(flatOffenders.front()))
        << "（判据退回 <= 时整片平地会变成锥体）";

    // (2) 平地 + 中心单坑：只有被填的那一格允许带 ε，其余像元逐像元原值。
    const std::string pitInput = makeUniformPitRaster("fill_eps_pit_input.tif", side, background,
                                                      true, 1.0f);
    const auto run = runFillSinks(p, progress_, pitInput, "fill_eps_pit_output.tif",
                                  {{"epsilon", epsilon}});
    ASSERT_TRUE(run.result.success) << run.result.message;
    const auto before = readWholeBand(pitInput, side, side);
    const auto after = readWholeBand(run.output, side, side);
    ASSERT_EQ(before.size(), after.size());
    EXPECT_NEAR(after[centre], background + static_cast<float>(epsilon), 1e-4f)
        << "被填洼抬过的像元 = 出水面 + ε；停在 5.0 说明 ε 没进洼地，远高于 15.0 说明背景被卷进来了";
    std::vector<size_t> offenders;
    for (size_t i = 0; i < before.size(); ++i) {
        if (i != centre && after[i] != before[i]) {
            offenders.push_back(i);
        }
    }
    EXPECT_TRUE(offenders.empty())
        << "ε 落到了没被填洼抬过的像元上（参数描述说的是洼地内），实际 " << offenders.size()
        << " 个，第一个 " << (offenders.empty() ? std::string("无") : coord(offenders.front()))
        << "，其值从 " << (offenders.empty() ? 0.0f : before[offenders.front()]) << " 变成 "
        << (offenders.empty() ? 0.0f : after[offenders.front()]);

    // (3) 抬完 ε 之后仍然不留封闭洼地（Produces 那句承诺在 ε>0 下重新证一次）。
    std::vector<uint8_t> valid(after.size(), 1);
    EXPECT_EQ(countClosedCells(after, side, side, valid), static_cast<size_t>(0))
        << "ε 逐层抬高水面后不得把洼地自己的出海口顶高";
    const auto zero = runFillSinks(p, progress_, pitInput, "fill_eps_pit_zero_output.tif",
                                   {{"epsilon", 0.0}});
    ASSERT_TRUE(zero.result.success) << zero.result.message;
    const auto zeroAfter = readWholeBand(zero.output, side, side);
    EXPECT_NEAR(zeroAfter[centre], background, 1e-4f) << "ε=0 时坑只抬到出水面";
    EXPECT_EQ(countClosedCells(zeroAfter, side, side, valid), static_cast<size_t>(0));
}

// 全局闸门（Task 10 审查后新增，"谁加参数谁补可见性"的配套）：epsilon 进 paramSpecs() 的同批
// 必须进 terrain.fill_sinks 自己的 visibleKeys，并且本 action 的面板锁把两侧钉成完全相等。
// WHY 形状照抄 SpindexEviAndSaviGuiPanelsRenderOnlyTheLKeyThatEachActionReads：
// 只断"表里有这个键"判不出渲染，所以渲染层问 ParamCardWidget::hasParam（param_card_widget.cpp:937
// 读 entries_，即真正建出来的控件行）；ParamWidget::hasParam（param_widget.cpp:158-163）只扫
// setParamSpecs 存下来的 specs_，等于把有效集合再抄一遍，断不出"这一行到底建没建"。
TEST_F(PluginTest, TerrainFillSinksGuiPanelRendersEpsilonAndHidesKeysTheActionNeverReads) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    const auto specs = p->paramSpecs();
    const auto* config = gis::gui::findActionUiConfig("terrain", "fill_sinks");
    ASSERT_NE(config, nullptr) << "terrain.fill_sinks 缺少 actionUiConfig 条目";

    const auto effective = gis::gui::buildEffectiveGuiParamSpecs(
        "terrain", "fill_sinks", specs, config->visibleKeys, config->requiredKeys);
    std::set<std::string> effectiveKeys;
    for (const auto& spec : effective) {
        effectiveKeys.insert(spec.key);
    }

    // 有效集合必须与白名单**完全相等**：少一个键说明白名单写的是插件没声明的键（错键名会同时
    // 隐藏真参数），多一个键说明 visibleKeys 是空集（空集在 buildEffectiveGuiParamSpecs 里
    // 等于不过滤，面板会把该 action 不读的键一起铺出来）。
    EXPECT_EQ(effectiveKeys, config->visibleKeys) << "terrain.fill_sinks 的有效键集与白名单不一致";
    EXPECT_GT(effectiveKeys.count("epsilon"), 0u) << "epsilon 必须被渲染（插件真的读它）";
    EXPECT_EQ(effectiveKeys.count("accum_threshold"), 0u)
        << "accum_threshold 只有 stream_extract 读，不得出现在 fill_sinks 面板上";

    // epsilon 是可选键：requiredKeys 只能要求插件本来就会拒绝的东西，而 doFillSinks 对缺省
    // epsilon 会兜底 0.0，把它列进 requiredKeys 就是凭空加严、GUI 用户无法留空提交。
    EXPECT_EQ(config->requiredKeys.count("epsilon"), 0u);
    EXPECT_EQ(config->visibleKeys.count("action"), 0u)
        << "\"action\" 被 buildEffectiveGuiParamSpecs 无条件跳过，写进白名单无效还与既有约定分叉";
    const gis::framework::ParamSpec* epsilonSpec = nullptr;
    for (const auto& spec : specs) {
        if (spec.key == "epsilon") {
            epsilonSpec = &spec;
        }
    }
    ASSERT_NE(epsilonSpec, nullptr) << "paramSpecs() 没有 epsilon，面板渲染的是空气";
    EXPECT_FALSE(epsilonSpec->required);

    ParamWidget panel;
    panel.setUiContext("terrain", "fill_sinks");
    panel.setParamSpecs(effective);
    const auto cards = panel.findChildren<ParamCardWidget*>();
    ASSERT_FALSE(cards.empty()) << "terrain.fill_sinks 没有生成任何参数卡片";
    std::set<std::string> renderedKeys;
    for (const auto& spec : specs) {
        if (spec.key == "action") {
            continue;
        }
        for (const auto* card : cards) {
            if (card && card->hasParam(spec.key)) {
                renderedKeys.insert(spec.key);
            }
        }
    }
    EXPECT_EQ(renderedKeys, effectiveKeys) << "terrain.fill_sinks 的 widget 树与有效 spec 不一致";
    EXPECT_GT(renderedKeys.count("epsilon"), 0u);
    EXPECT_EQ(renderedKeys.count("accum_threshold"), 0u);
}

// 9x9 常量背景 + 若干 NoData / 低值像元的 fill_sinks 夹具。cells 里 (x,y)→值，
// ndCells 里的像元同时写进栅格NoData 哨兵（写入器只会 SetNoDataValue 整带，
// 所以值本身也要写成 noData）。
namespace {

std::string makeNoDataPitRaster(const std::string& name, float background,
                                const std::map<std::pair<int, int>, float>& cells,
                                const std::vector<std::pair<int, int>>& ndCells) {
    const int w = 9, h = 9;
    std::vector<float> data(static_cast<size_t>(w * h), background);
    for (const auto& [pos, value] : cells) {
        data[pos.second * w + pos.first] = value;
    }
    for (const auto& pos : ndCells) {
        data[pos.second * w + pos.first] = -9999.0f;
    }
    gis::tests::RasterFixtureOptions options;
    options.hasNoData = true;
    options.noData = -9999.0;
    return gis::tests::writeFloatRaster(getTestDir(), name, w, h, data, options);
}

} // namespace

// 计划 Step 1 的第 4 条：NoData 不参与填洼，输出仍带同一个 NoData 值。
// WHY 仍然留着：它锁的是"NoData 像元不被写成高程、哨兵值不漂移"这一半；
// 但它锁不住障壁/出海口那个方向（NoData 在角上、不邻接坑，两种处置结果相同），
// 所以另有 FillSinksDoesNotSeedNoDataEdgeCellsAsPourPoints 与
// FillSinksTreatsNoDataAsBarrierNotOutlet 两条判别用例（R-T11-3）。
TEST_F(PluginTest, FillSinksSkipsNoDataCells) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    const std::string input = makeNoDataPitRaster("fill_nd_input.tif", 10.0f,
                                                  {{std::make_pair(4, 4), 2.0f}},
                                                  {std::make_pair(0, 0)});
    const auto run = runFillSinks(p, progress_, input, "fill_nd_output.tif");
    ASSERT_TRUE(run.result.success) << run.result.message;

    double noData = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(run.output, 1, noData));
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 0, 0), static_cast<float>(noData), 1e-4f);
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 4, 4), 10.0f, 1e-4f);
}

// R-T11-3 判别用例之一：NoData 像元落在数据集边缘、且紧贴坑。
// WHY 这个形态才有鉴别力：边缘像元是**原值直接**入队的出海口，-9999 因此会第一个被弹出，
// 成为一条假出水路径；坑与它相邻，于是坑被"排水"而完全不抬。
// 反向证明（内部 NoData 像元没有这个效果）：处在坑与边缘之间但**不在边缘上**的 NoData
// 像元是被弹出时按 parent 水面抬起来的，-9999 拿不到出水面 ⇒ 两种处置都得到 10.0，测不出差异
// （这条反例当时用临时探针夹具模拟过、未留成用例；结论如上，别把它当既有用例引用）。
// 期望：障壁处置 ⇒ 坑只能抬到周边 10.0；出海口处置 ⇒ 坑停在 2.0。
TEST_F(PluginTest, FillSinksDoesNotSeedNoDataEdgeCellsAsPourPoints) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    // (x=0,y=4) 在左边缘上是 NoData；坑 (x=1,y=4) 与它相邻。
    const std::string input = makeNoDataPitRaster("fill_nd_edge_input.tif", 10.0f,
                                                  {{std::make_pair(1, 4), 2.0f}},
                                                  {std::make_pair(0, 4)});
    const auto run = runFillSinks(p, progress_, input, "fill_nd_edge_output.tif");
    ASSERT_TRUE(run.result.success) << run.result.message;

    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 1, 4), 10.0f, 1e-4f)
        << "NoData 当障壁时坑只能抬到周边 10.0；停在 2.0 说明 -9999 被当成了出海口";
    double noData = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(run.output, 1, noData));
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 0, 4), static_cast<float>(noData), 1e-4f);
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 0, 3), 10.0f, 1e-4f);
}

// R-T11-3 判别用例之二（水文学上更实质的那个方向）：一条 5.0 的低沟本来能把坑排走，
// 但被一片 NoData 隔断 —— 障壁语义下这条排水路径不存在，坑只能翻过 10.0 的高地出水 ⇒ 抬到 10.0。
// 若把 NoData 当成可穿越的出海口，坑会从 -9999 处排进低沟、只抬到 5.0（用例即红）。
// WHY 还要单独一条：上一条靠"边缘种子原值入队"，这一条靠"邻居扩展是否越过 NoData"，
// 两个方向可以各自被写错。
TEST_F(PluginTest, FillSinksTreatsNoDataAsBarrierNotOutlet) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    std::map<std::pair<int, int>, float> cells;
    cells[std::make_pair(0, 4)] = 5.0f;
    cells[std::make_pair(1, 4)] = 5.0f;
    cells[std::make_pair(2, 4)] = 5.0f;   // 低沟：从左边缘通向 NoData
    cells[std::make_pair(4, 4)] = 2.0f;   // 坑：沟被 NoData 隔断后只能翻越 10.0 的高地
    // (x=3,y=4) 正好卡在低沟与坑之间。
    const std::string input = makeNoDataPitRaster("fill_nd_barrier_input.tif", 10.0f, cells,
                                                  {std::make_pair(3, 4)});
    const auto run = runFillSinks(p, progress_, input, "fill_nd_barrier_output.tif");
    ASSERT_TRUE(run.result.success) << run.result.message;

    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 4, 4), 10.0f, 1e-4f)
        << "NoData 是障壁 ⇒ 低沟那条 5.0 的路径不可用，坑必须抬到 10.0；得到 5.0 说明穿过了 NoData";
    for (int x = 0; x <= 2; ++x) {
        EXPECT_NEAR(gis::tests::readPixel(run.output, 1, x, 4), 5.0f, 1e-4f) << "x=" << x;
    }
    double noData = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(run.output, 1, noData));
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 3, 4), static_cast<float>(noData), 1e-4f);
}

// F3（8 邻域语义的具名用例，ε=0）：坑的四个正交邻居全是 10.0 的墙，只有一个**对角**方向接着
// 能排到边缘的 5.0 背景 ⇒ 只有按 8 邻域扩展，坑才会被填到 5.0；按 4 邻域它会被填到 10.0。
// WHY 必须 8 邻域（与 D8 一致）：填洼的输出直接喂给 flow_direction，而后者是 D8（8 个方向，
// 含对角）。填洼按 4 邻域填平、流向按 8 邻域找下坡，两套邻域不一致 ⇒ 填洼多抬一堆像元，
// 还会在"只能经角点排水"的地形上造出本不存在的封闭汇。量化随机 DEM 上 4/8 邻域的封闭像元数
// 差异是 300+ 量级（审查者 drive.py 的 closed4 列），不是边角情形。
TEST_F(PluginTest, FillSinksDrainsThroughDiagonalCornerWithEightNeighbours) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    const int w = 9, h = 9;
    std::vector<float> data(static_cast<size_t>(w * h), 5.0f);
    const std::pair<int, int> wall[] = {{3, 2}, {3, 4}, {2, 3}, {4, 3}, {2, 2}, {4, 2}, {2, 4}};
    for (const auto& pos : wall) {
        data[pos.second * w + pos.first] = 10.0f;
    }
    const int pitX = 3;
    const int pitY = 3;
    data[pitY * w + pitX] = 1.0f;      // 四个正交全是 10.0 的墙，只有对角 (4,4) 是 5.0 背景
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "fill_diag_input.tif", w, h, data);

    const auto run = runFillSinks(p, progress_, input, "fill_diag_output.tif");
    ASSERT_TRUE(run.result.success) << run.result.message;
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, pitX, pitY), 5.0f, 1e-4f)
        << "坑只能经角点 (4,4) 排水 ⇒ 5.0；得到 10.0 说明邻居扩展退化成了 4 邻域，"
           "与 D8 流向的 8 个方向不一致";
    for (const auto& pos : wall) {
        EXPECT_NEAR(gis::tests::readPixel(run.output, 1, pos.first, pos.second), 10.0f, 1e-4f)
            << "墙 (x=" << pos.first << ",y=" << pos.second << ") 本来就高于水面，不得被改动";
    }
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 4, 4), 5.0f, 1e-4f) << "排水口那个对角像元";
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 8, 8), 5.0f, 1e-4f) << "边缘背景";
    const auto after = readWholeBand(run.output, w, h);
    std::vector<uint8_t> valid(after.size(), 1);
    EXPECT_EQ(countClosedCells(after, w, h, valid), static_cast<size_t>(0))
        << "经角点填完之后仍不得留下封闭洼地";
}

// F2（NoData 哨兵恢复的覆盖）：fill_sinks 分支末尾那句 result.setTo(noDataValue, noDataMask)
// 只在 z_factor≠1 时才第一次真的被考验到。
// WHY 必须 z≠1：elevation 先乘 z_factor，NoData 掩膜是按 noDataValue * zFactor 比出来的，于是中间态
// 是 -9999*z；那步 setTo 把它写回**声明的**哨兵。z_factor=1.0 时两者同值 ⇒ 既有三条 NoData 用例的
// 哨兵断言恒真：把 setTo 整块删掉，上一轮的全量仍然全绿（审查者实测，本轮 §判别取证复现），
// 这条守卫实际零覆盖。真实后果不是风格问题：z≠1 时哨兵像元会留在 -19998/-99990/-4999.5，
// 而输出文件仍声明 NODATA=-9999 ⇒ 这些像元成了伪低地，反过来把下游 D8 全灌进这些假坑。
TEST_F(PluginTest, FillSinksRestoresNoDataSentinelWhenZFactorIsNotOne) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    // 低沟 5.0 从左边缘通向 NoData (3,4)，坑在 (4,4)；再把左上角 (0,0) 也设成 NoData。
    const std::string input = makeNoDataPitRaster(
        "fill_ndz_input.tif", 10.0f,
        {{std::make_pair(0, 4), 5.0f}, {std::make_pair(1, 4), 5.0f},
         {std::make_pair(2, 4), 5.0f}, {std::make_pair(4, 4), 2.0f}},
        {std::make_pair(3, 4), std::make_pair(0, 0)});
    const double zFactor = 2.0;
    const auto run = runFillSinks(p, progress_, input, "fill_ndz_output.tif",
                                  {{"z_factor", zFactor}});
    ASSERT_TRUE(run.result.success) << run.result.message;

    double declared = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(run.output, 1, declared));
    EXPECT_NEAR(declared, -9999.0, 1e-4) << "输出文件声明的 NODATA 仍是输入那个哨兵";

    // ①哨兵像元读回的是**声明的** noDataValue，不是 nodata*zFactor 的中间值。
    const float sentinel = gis::tests::readPixel(run.output, 1, 3, 4);
    EXPECT_NEAR(sentinel, static_cast<float>(declared), 1e-4f)
        << "(x=3,y=4) 的哨兵必须被写回声明值 -9999；读到 -19998 说明 setTo(noDataValue, noDataMask) "
           "被删掉了，而文件仍声明 NODATA=-9999";
    EXPECT_NE(sentinel, static_cast<float>(declared * zFactor))
        << "不得留下 noDataValue * zFactor = -19998 这种伪低地像元";
    const float corner = gis::tests::readPixel(run.output, 1, 0, 0);
    EXPECT_NEAR(corner, static_cast<float>(declared), 1e-4f)
        << "边缘上的 NoData 像元同样要走那步恢复（它是种子，不经抬升判据）";

    // ②被声明哨兵覆盖的像元之外，缩放高程照常输出（否则这条用例会因为整幅都错而假过）。
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 4, 4), 20.0f, 1e-4f)
        << "z_factor=2 ⇒ 坑抬到 10.0*2=20.0，缩放与哨兵恢复互不干扰";
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 0, 4), 10.0f, 1e-4f) << "低沟 5.0*2";
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 8, 8), 20.0f, 1e-4f) << "背景 10.0*2";
}

// F5（NaN 当障壁）：一条只留一个真实出口的坑，把 std::isfinite 的效力逼出来。
// WHY 这个形态才有鉴别力：坑 (4,4) 的 8 个邻居里 7 个是 NaN，只剩对角 (5,5) 这一个 10.0 的真出口。
// 加了 std::isfinite ⇒ NaN 与 NoData 同语义（不作种子也不被扩展）⇒ 坑经 (5,5) 抬到 10.0。
// 没加 ⇒ NaN 像元照常入队，被弹出时 top.first 是 NaN，"value < 出水面"对邻居恒假 ⇒ 邻居只被
// 标记成已处理却一点没抬 ⇒ 坑从 NaN 那里"漏"出去、静默停在 2.0（实测连续 3 次都是 2.0）。
// 顺带说清为什么这里必须写注释而不是靠"结果看起来一样"：单个 NaN 摆在坑边上时两种实现都给
// 10.0（堆里 NaN 的先后不受保证），所以判据要落在"坑只有一个真实出口"这种几何上才稳。
TEST_F(PluginTest, FillSinksTreatsNaNCellsAsBarrierLikeNoData) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    const int w = 9, h = 9;
    std::vector<float> data(static_cast<size_t>(w * h), 10.0f);
    const float nanValue = std::numeric_limits<float>::quiet_NaN();
    std::vector<std::pair<int, int>> nanRing;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0 || (dx == 1 && dy == 1)) {
                continue;            // 留出对角 (5,5) 这个真实出口
            }
            nanRing.push_back(std::make_pair(4 + dx, 4 + dy));
            data[(4 + dy) * w + (4 + dx)] = nanValue;
        }
    }
    data[4 * w + 4] = 2.0f;          // 坑：四周只有 (5,5) 一个合法邻居
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "fill_nan_input.tif", w, h, data);

    const auto run = runFillSinks(p, progress_, input, "fill_nan_output.tif");
    ASSERT_TRUE(run.result.success) << run.result.message;

    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 4, 4), 10.0f, 1e-4f)
        << "NaN 是障壁 ⇒ 坑只能经唯一的真出口 (5,5) 抬到 10.0；停在 2.0 说明坑被当成从 NaN 那里排走了"
           "（NaN 被弹出后 value < 出水面 恒假，邻居标记已处理却一点没抬）";
    for (const auto& pos : nanRing) {
        EXPECT_TRUE(std::isnan(gis::tests::readPixel(run.output, 1, pos.first, pos.second)))
            << "障壁像元 (x=" << pos.first << ",y=" << pos.second << ") 既不被填也不被赋哨兵值："
               "输出保留 NaN，下游按无效像元处理，我们不为它编一个高程";
    }
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 5, 5), 10.0f, 1e-4f) << "唯一的真出口本身不被改动";
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 0, 0), 10.0f, 1e-4f) << "NaN 环之外的背景";
    const auto after = readWholeBand(run.output, w, h);
    std::vector<uint8_t> valid(after.size(), 1);
    for (const auto& pos : nanRing) {
        valid[static_cast<size_t>(pos.second) * w + pos.first] = 0;   // NaN 与 NoData 一样不参与可达性
    }
    EXPECT_EQ(countClosedCells(after, w, h, valid), static_cast<size_t>(0))
        << "把 NaN 当障壁之后仍不得留下封闭洼地";
}

// F6（Produces 那句"输出无任何封闭洼地"的既定例外）：被 NoData 完整围死的坑永不入队 ⇒ 不填。
// WHY 钉成显式用例而不是留在审查日志里：priority-flood 只从数据集边缘的出海口往外淹没，
// NoData 是不可穿越的障壁（R-T11-3），于是这个坑没有任何合法出水路径——填它等于凭空造一个水面，
// 而"抬到多少"没有任何依据。所以 Produces 的无封闭洼地承诺在这里字面上不成立，是**有意的语义**，
// 口径由控制器写进 spec（K-15/K-16）。参考实现同样把它标成 never_drained 并保留原值。
TEST_F(PluginTest, FillSinksLeavesPitEnclosedByNoDataUnfilled) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    std::map<std::pair<int, int>, float> cells;
    cells[std::make_pair(4, 4)] = 2.0f;
    std::vector<std::pair<int, int>> ring;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            ring.push_back(std::make_pair(4 + dx, 4 + dy));   // 坑的 8 个邻居全是 NoData
        }
    }
    const std::string input = makeNoDataPitRaster("fill_ndclosed_input.tif", 10.0f, cells, ring);
    const auto run = runFillSinks(p, progress_, input, "fill_ndclosed_output.tif");
    ASSERT_TRUE(run.result.success) << run.result.message;

    double declared = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(run.output, 1, declared));
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 4, 4), 2.0f, 1e-4f)
        << "被 NoData 围死的洼地没有合法出水路径 ⇒ 既不填也不凭空造水面，必须保持 2.0";
    for (const auto& pos : ring) {
        EXPECT_NEAR(gis::tests::readPixel(run.output, 1, pos.first, pos.second),
                    static_cast<float>(declared), 1e-4f)
            << "围死它的那圈障壁 (x=" << pos.first << ",y=" << pos.second << ") 保持声明哨兵";
    }
    EXPECT_NEAR(gis::tests::readPixel(run.output, 1, 0, 0), 10.0f, 1e-4f) << "障壁之外的背景不受影响";
}

// ── N131：rasterize 的 attribute 点名就必须存在，且产物要能自证烧了什么 ────────────────
// 夹具算式写死在这里，下面三条臂的期望值全部由它手推（不是从产物反灌回来当"实测"）：
//   两条面：A = [0,1]×[0,1] value=3；B = [2,4]×[3,4] value=5；resolution=1.0
//   图层外包 = [0,4]×[0,4] ⇒ xSize = ceil((4-0)/1) = 4、ySize = 4 ⇒ 输出 4×4 = 16 个像元
//   地理变换左上 (0,4)、像元 (1,-1) ⇒ 像元 (i,j) 中心 = (i+0.5, 3.5-j)
//   GDAL 栅格化默认只烧"中心落在几何内"的像元（未开 ALL_TOUCHED）⇒
//     A：i=0 → x=0.5∈[0,1]；j=3 → y=0.5∈[0,1] ⇒ 唯一像元 (i=0,j=3) = 3
//     B：i=2 → 2.5、i=3 → 3.5 都 ∈[2,4]；j=0 → y=3.5∈[3,4] ⇒ (2,0)=5、(3,0)=5
//   ⇒ 非零像元恰好 3 个（1 个 3、2 个 5），像元总和 = 3+5+5 = 13，其余 13 个像元 = 0
//   留空 attribute 走 -burn 1 ⇒ 同样这 3 个像元 = 1，总和 = 3
// WHY 用 shapefile + OFTInteger：OGR 把整数域字段以 float 读出时 3.0/5.0 是精确值，
// 逐像元 == 比较站得住；换成实数字段就得改成 EXPECT_NEAR 并把容差写进判据说明。
static std::string buildRasterizeValueFixture(const std::string& fileName) {
    const std::string shpPath = (getTestDir() / fileName).string();
    const fs::path stem = fs::path(shpPath).stem();
    for (const auto& entry : fs::directory_iterator(getTestDir())) {
        if (entry.path().stem() == stem) {
            std::error_code ignored;
            fs::remove(entry.path(), ignored);
        }
    }

    auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
    EXPECT_NE(driver, nullptr);
    auto* ds = driver->Create(shpPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    EXPECT_NE(ds, nullptr);
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);
    auto* layer = ds->CreateLayer("test", srs.get(), wkbPolygon);
    EXPECT_NE(layer, nullptr);
    OGRFieldDefn valueDefn("value", OFTInteger);
    layer->CreateField(&valueDefn);

    auto addRect = [&](double minX, double minY, double maxX, double maxY, int value) {
        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(minX, minY); ring.addPoint(maxX, minY);
        ring.addPoint(maxX, maxY); ring.addPoint(minX, maxY);
        ring.addPoint(minX, minY);
        poly.addRing(&ring);
        feat->SetGeometry(&poly);
        feat->SetField("value", value);
        layer->CreateFeature(feat);
        OGRFeature::DestroyFeature(feat);
    };
    addRect(0.0, 0.0, 1.0, 1.0, 3);
    addRect(2.0, 3.0, 4.0, 4.0, 5);

    GDALClose(ds);
    return shpPath;
}

// ── N132 夹具：与 buildRasterizeValueFixture 同一批几何、同一套手推算式，只换字段类型 ──
// 复用 N131 块里已经写死的推导（图层外包 [0,4]×[0,4]、resolution=1.0 ⇒ 4×4 = 16 个像元；
// 地理变换左上 (0,4)、像元 (1,-1) ⇒ 像元 (i,j) 中心 = (i+0.5, 3.5-j)；未开 ALL_TOUCHED
// ⇒ 只烧"中心落在几何内"的像元 ⇒ 非零像元恰好 3 个：(i=0,j=3)、(i=2,j=0)、(i=3,j=0)）。
// 两条臂各钉一类缺陷：
//   label = OFTString，值写作 "road_a"/"road_b"（真正非数值的文本）⇒ 必须被拒。
//     WHY 不用 "3"/"5" 这种"看着像数值"的文本：实跑发现旧实现会把它烧成 3.0/5.0 ——
//     GDAL 的 -a 走 GetFieldAsDouble，对文本域做 CPLAtof 静默转换，所以那种夹具在改前
//     烧出来的是"碰巧对"的值，抓不到本缺陷；只有非数值文本才复现控制器的字节级症状
//     （build/tmp/n132/o.tif 尾 64 字节 = 16 个 00 00 00 00 且 Success）。
//     新校验按类型一律拒绝，两种文本形态（可转换 / 不可转换）都进不了烧写阶段。
//   h     = OFTReal（值 2.5，withNumericField=true 才建）⇒ 数值型必须照常可烧（正对照）。
// 正对照的期望值算式（写在产物之前，不许从产物回填）：
//   烧 h ⇒ 3 个像元 = 2.5，像元和 = 3 × 2.5 = 7.5，其余 16 - 3 = 13 个像元 = 0。
//   2.5 = 5/2 在二进制下是精确值，Float32 存下再读回 double 仍精确 ⇒ 逐像元 == 与总和 ==
//   都站得住，这条臂不需要浮点容差。
static std::string buildRasterizeFieldFixture(const std::string& fileName, bool withNumericField) {
    const std::string shpPath = (getTestDir() / fileName).string();
    const fs::path stem = fs::path(shpPath).stem();
    for (const auto& entry : fs::directory_iterator(getTestDir())) {
        if (entry.path().stem() == stem) {
            std::error_code ignored;
            fs::remove(entry.path(), ignored);
        }
    }

    auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
    EXPECT_NE(driver, nullptr);
    auto* ds = driver->Create(shpPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    EXPECT_NE(ds, nullptr);
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);
    auto* layer = ds->CreateLayer("test", srs.get(), wkbPolygon);
    EXPECT_NE(layer, nullptr);

    OGRFieldDefn labelDefn("label", OFTString);
    layer->CreateField(&labelDefn);
    if (withNumericField) {
        OGRFieldDefn heightDefn("h", OFTReal);
        layer->CreateField(&heightDefn);
    }

    auto addRect = [&](double minX, double minY, double maxX, double maxY, const char* label) {
        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        OGRPolygon poly;
        OGRLinearRing ring;
        ring.addPoint(minX, minY); ring.addPoint(maxX, minY);
        ring.addPoint(maxX, maxY); ring.addPoint(minX, maxY);
        ring.addPoint(minX, minY);
        poly.addRing(&ring);
        feat->SetGeometry(&poly);
        feat->SetField("label", label);
        if (withNumericField) {
            feat->SetField("h", 2.5);
        }
        layer->CreateFeature(feat);
        OGRFeature::DestroyFeature(feat);
    };
    addRect(0.0, 0.0, 1.0, 1.0, "road_a");
    addRect(2.0, 3.0, 4.0, 4.0, "road_b");

    GDALClose(ds);
    return shpPath;
}

static void removeRasterizeOutput(const std::string& output) {
    std::error_code ignored;
    fs::remove(output, ignored);
    fs::remove(output + ".aux.xml", ignored);
}

// 反向臂：字段名写反两个字母（valeu）⇒ 硬失败 + 点名参数 + 带回写错的值 + 不建输出文件。
// 旧实现的真实症状（现读，改前红臂逐条对得上）：GDAL 的 `-a valeu` 会发
// `ERROR 1: Failed to find field valeu on layer …` ⇒ 插件走 errCode 分支返回
// "Rasterize failed: Failed to find field valeu …"，**success 已经是 false**；
// 真正没被守住的是两件事：① 输出栅格在调用 GDAL 之前就被 drv->Create 建出来并 Fill(0)
// 过 ⇒ 失败后磁盘上留一张全 0 的半成品 tif（0 又恰好是本动作的 NoData，肉眼看不出差别）；
// ② 消息是 GDAL 内部口径（点名的是 layer 与文件名），没点出用户填的参数叫 attribute。
// ⇒ 本臂的判别点是 message 口径与"输出不存在"这两条，不是 success 那条
//   （success 断言是对新实现的契约要求，旧实现也满足，别把它当成红臂的证据）。
TEST_F(PluginTest, VectorRasterizeRejectsUnknownAttributeField) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string shpPath = buildRasterizeValueFixture("e2e_rasterize_typo_input.shp");
    const std::string output = (getTestDir() / "e2e_rasterize_typo_output.tif").string();
    removeRasterizeOutput(output);
    ASSERT_FALSE(fs::exists(output)) << "本轮开始前就残留了输出，半成品判据会失真";

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("rasterize");
    params["input"] = shpPath;
    params["output"] = output;
    params["resolution"] = 1.0;
    params["attribute"] = std::string("valeu");   // 夹具里的真实字段是 value

    const auto result = p->execute(params, progress_);

    EXPECT_FALSE(result.success)
        << "N131：attribute 写成 valeu 仍然 success=true ⇒ 烧出整幅 0 而 0 又是 NoData，"
           "产物静默变成了另一种东西（message=" << result.message << "）";
    EXPECT_NE(result.message.find("attribute not found"), std::string::npos)
        << "失败消息必须点名是哪个参数（与 dissolve_field/join_field not found 同族口径）。实际 message="
        << result.message;
    EXPECT_NE(result.message.find("valeu"), std::string::npos)
        << "失败消息必须把写错的那个值带回来。实际 message=" << result.message;
    ASSERT_FALSE(fs::exists(output))
        << "字段校验要在建出输出栅格之前完成，失败后不得留半成品：" << output;
}

// 正向臂：烧对字段 + 元数据自证（宽/高/被烧要素数/烧的是哪个字段）。
// 三条臂各钉一类缺陷：像元值（烧错字段/烧错位置）、尺寸（行列数算反）、键（产物无法自证）。
TEST_F(PluginTest, VectorRasterizeBurnsAttributeFieldAndReportsMetadata) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string shpPath = buildRasterizeValueFixture("e2e_rasterize_value_input.shp");
    const std::string output = (getTestDir() / "e2e_rasterize_value_output.tif").string();
    removeRasterizeOutput(output);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("rasterize");
    params["input"] = shpPath;
    params["output"] = output;
    params["resolution"] = 1.0;
    params["attribute"] = std::string("value");

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(fs::exists(output)) << output;

    // 元数据：改前一条键都不发（vector_plugin_raster_convert.inc 的 Result::ok 不带 metadata）
    for (const auto& key : std::map<std::string, std::string>{
             {"raster_width", "4"}, {"raster_height", "4"},
             {"source_feature_count", "2"}, {"burn_field", "value"}}) {
        const auto it = result.metadata.find(key.first);
        ASSERT_NE(it, result.metadata.end())
            << "rasterize 必须回显 " << key.first << "；实际键数=" << result.metadata.size();
        EXPECT_EQ(it->second, key.second) << "元数据键 " << key.first;
    }
    EXPECT_EQ(result.metadata.count("burn_value"), 0u)
        << "点了字段的臂不得同时出现 burn_value：一个产物只能说明一种烧值来源";

    const auto pixels = readWholeBand(output, 4, 4);
    ASSERT_EQ(pixels.size(), 16u) << "夹具算式的前提是 4×4";

    int count3 = 0;
    int count5 = 0;
    int countOther = 0;
    double sum = 0.0;
    for (const float v : pixels) {
        sum += static_cast<double>(v);
        if (v == 3.0f) {
            ++count3;
        } else if (v == 5.0f) {
            ++count5;
        } else if (v != 0.0f) {
            ++countOther;
        }
    }
    EXPECT_EQ(countOther, 0) << "值集合必须恰好 ⊆ {0,3,5}：出现别的值就是烧错了字段或烧错了位置";
    EXPECT_EQ(count3, 1) << "手推：A 只覆盖 (i=0,j=3) 一个像元";
    EXPECT_EQ(count5, 2) << "手推：B 覆盖 (2,0)(3,0) 两个像元";
    EXPECT_NEAR(sum, 13.0, 1e-4) << "手推：3+5+5=13，其余 13 个像元为 0";

    // 逐位置钉死，防"总和凑对了但像元摆错"（3 个非零像元若被摆成一行，上面三条全过）
    EXPECT_EQ(pixels[3 * 4 + 0], 3.0f) << "(i=0,j=3) 必须是 3";
    EXPECT_EQ(pixels[0 * 4 + 2], 5.0f) << "(i=2,j=0) 必须是 5";
    EXPECT_EQ(pixels[0 * 4 + 3], 5.0f) << "(i=3,j=0) 必须是 5";
    EXPECT_EQ(pixels[0], 0.0f) << "(i=0,j=0) 不在任何几何里";
}

// 留空臂：不点 attribute 是合法用法（GUI/spec 默认就是空串 ⇒ 走 -burn 1）。
// 作用是证明这条修复没有顺手把默认路径也拒了，且烧值来源在两种臂下各自说得清。
TEST_F(PluginTest, VectorRasterizeWithoutAttributeBurnsConstantOne) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string shpPath = buildRasterizeValueFixture("e2e_rasterize_burn_input.shp");
    const std::string output = (getTestDir() / "e2e_rasterize_burn_output.tif").string();
    removeRasterizeOutput(output);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("rasterize");
    params["input"] = shpPath;
    params["output"] = output;
    params["resolution"] = 1.0;
    // 不写 params["attribute"]：走 -burn 1 这条合法路径

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << "留空 attribute 的合法路径被 N131 的校验顺手拒了: " << result.message;

    const auto width = result.metadata.find("raster_width");
    ASSERT_NE(width, result.metadata.end()) << "实际键数=" << result.metadata.size();
    EXPECT_EQ(width->second, "4");
    const auto burnValue = result.metadata.find("burn_value");
    ASSERT_NE(burnValue, result.metadata.end()) << "留空臂必须说明烧的是常数：burn_value";
    EXPECT_EQ(burnValue->second, "1");
    EXPECT_EQ(result.metadata.count("burn_field"), 0u)
        << "没点字段的臂不得出现 burn_field，否则元数据会把常数烧写说成字段烧写";

    const auto pixels = readWholeBand(output, 4, 4);
    ASSERT_EQ(pixels.size(), 16u);
    int countOne = 0;
    int countOther = 0;
    double sum = 0.0;
    for (const float v : pixels) {
        sum += static_cast<double>(v);
        if (v == 1.0f) {
            ++countOne;
        } else if (v != 0.0f) {
            ++countOther;
        }
    }
    EXPECT_EQ(countOther, 0) << "值集合必须恰好 ⊆ {0,1}";
    EXPECT_EQ(countOne, 3) << "手推：与正向臂同一批几何 ⇒ 同样 3 个像元，只是值换成 1";
    EXPECT_NEAR(sum, 3.0, 1e-4) << "手推：1+1+1=3";
}

// N132 反向臂：attribute 点名了一个**存在**但类型是 OFTString 的字段 ⇒ 必须硬失败。
// 旧实现的真实症状（控制器双臂字节级实测，build/tmp/n132/，4×4 Float32 尾 64 字节）：
//   --attribute=label ⇒ 16 个 00000000 且 Success；
//   同输入不传 attribute（burn=1）⇒ 16 个 0000803f（1.0）。
// 存在性校验（N131）在这里拦不住，因为字段确实存在；GDAL 的 -a 取不到数值 ⇒ 每个像元烧成 0，
// 而 0 恰是本动作的 NoData ⇒ 产物"打开看不出异常"、动作还报 Success，
// raster_inspect histogram 对它报 no valid pixels found in sampling（整幅都是 NoData）。
// ⇒ 判据三条：success 为假 + 案文点名"不是数值型"并把字段名带回来 + 磁盘上不留半成品。
TEST_F(PluginTest, VectorRasterizeRejectsNonNumericAttributeField) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string shpPath = buildRasterizeFieldFixture("e2e_rasterize_str_input.shp", false);
    const std::string output = (getTestDir() / "e2e_rasterize_str_output.tif").string();
    removeRasterizeOutput(output);
    ASSERT_FALSE(fs::exists(output)) << "本轮开始前就残留了输出，半成品判据会失真";

    // 夹具里 label 是真实存在的字段 ⇒ 这条臂走的不是"字段不存在"分支，而是类型分支
    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("rasterize");
    params["input"] = shpPath;
    params["output"] = output;
    params["resolution"] = 1.0;
    params["attribute"] = std::string("label");

    const auto result = p->execute(params, progress_);

    EXPECT_FALSE(result.success)
        << "N132：attribute 指向 OFTString 字段仍然 success=true ⇒ 整幅静默烧成 0（而 0 是 NoData）。"
           "实际 message=" << result.message;
    EXPECT_NE(result.message.find("not numeric"), std::string::npos)
        << "失败案文要说明的是类型不对，不是字段不存在。实际 message=" << result.message;
    EXPECT_NE(result.message.find("label"), std::string::npos)
        << "失败案文必须把点名的字段名带回来。实际 message=" << result.message;
    EXPECT_EQ(result.message, "attribute field is not numeric: label")
        << "案文形态要与既有两条失败文案同族（点名参数 + 带回写错的值）";
    ASSERT_FALSE(fs::exists(output))
        << "类型校验要在建出输出栅格之前完成，失败后不得留全 0 半成品：" << output;
}

// N132 正对照：同一夹具加一个 OFTReal 字段 h=2.5 ⇒ 数值型必须照常可烧，
// 用来证明反向臂不是一条"把所有字段都拒掉"的窄规则（否则反向臂与正对照会同时绿在"全拒"上）。
// 期望值全部来自夹具上方算式：3 个像元 = 2.5、和 = 3 × 2.5 = 7.5、其余 13 个像元 = 0。
TEST_F(PluginTest, VectorRasterizeBurnsRealFieldValues) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string shpPath = buildRasterizeFieldFixture("e2e_rasterize_real_input.shp", true);
    const std::string output = (getTestDir() / "e2e_rasterize_real_output.tif").string();
    removeRasterizeOutput(output);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("rasterize");
    params["input"] = shpPath;
    params["output"] = output;
    params["resolution"] = 1.0;
    params["attribute"] = std::string("h");   // OFTReal，夹具里两条要素都取 2.5

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success)
        << "数值型字段被 N132 的校验顺手拒了（正对照失败）: " << result.message;
    ASSERT_TRUE(fs::exists(output)) << output;

    const auto burnField = result.metadata.find("burn_field");
    ASSERT_NE(burnField, result.metadata.end()) << "实际键数=" << result.metadata.size();
    EXPECT_EQ(burnField->second, "h") << "元数据要回显点名的那个数值字段";

    const auto pixels = readWholeBand(output, 4, 4);
    ASSERT_EQ(pixels.size(), 16u) << "夹具算式的前提是 4×4";

    int countReal = 0;
    int countOther = 0;
    double sum = 0.0;
    for (const float v : pixels) {
        sum += static_cast<double>(v);
        if (v == 2.5f) {
            ++countReal;
        } else if (v != 0.0f) {
            ++countOther;
        }
    }
    EXPECT_EQ(countOther, 0) << "值集合必须恰好 ⊆ {0, 2.5}：出现别的值就是烧错了字段";
    EXPECT_EQ(countReal, 3) << "手推：3 个像元中心落在两条面里";
    EXPECT_EQ(sum, 7.5) << "手推：3 × 2.5 = 7.5（2.5 在二进制下精确 ⇒ 不用容差）";

    // 逐位置钉死，防"总数与和都对了但像元摆错"
    EXPECT_EQ(pixels[3 * 4 + 0], 2.5f) << "(i=0,j=3) 必须是 2.5";
    EXPECT_EQ(pixels[0 * 4 + 2], 2.5f) << "(i=2,j=0) 必须是 2.5";
    EXPECT_EQ(pixels[0 * 4 + 3], 2.5f) << "(i=3,j=0) 必须是 2.5";
    EXPECT_EQ(pixels[0], 0.0f) << "(i=0,j=0) 不在任何几何里";
}

// ── N134 夹具：双图层 GPKG，用来证明 layer 参数真的限制烧写范围 ───────────────────
// 两层各**一个**多边形、字段 value 为 OFTInteger、EPSG:3857：
//   poly_a（先建 ⇒ 第一层）：环形，外环 [0,4]×[0,4] + 内环 [1,3]×[1,3]，value=3
//   poly_b（第二层）       ：矩形 [1,3]×[1,3]（正好填住那个洞），value=7
// 两层的几何**互不重叠**（洞里的面积不属于 poly_a），但 poly_b 完全落在 poly_a 的外包内，
// 而 poly_a 的几何又有一部分落在 poly_b 外包向外的取整余量里 ⇒ 三条臂都能被"全层烧写"打红。
//
// 期望像元数与和全部由下面的算式手推（resolution=1.5，先写死再实现，不许从产物回填）：
//   像元 (i,j) 中心 = (MinX + (i+0.5)*1.5, MaxY - (j+0.5)*1.5)，未开 ALL_TOUCHED ⇒ 只看中心
//   臂 A/B（layer=poly_a / 不传 layer）：范围来自 poly_a = [0,4]×[0,4]
//     xSize = ceil((4-0)/1.5) = ceil(2.667) = 3 = ySize ⇒ 3×3 = 9 个像元
//     中心 x ∈ {0.75, 2.25, 3.75}、y ∈ {3.25, 1.75, 0.25}
//     洞 [1,3]×[1,3] 只吃得住中心 (2.25,1.75)（i=1,j=1）这一个 ⇒ poly_a 烧到 9-1 = 8 个像元
//     ⇒ 修后 count3 = 8、count7 = 0、和 = 8×3 = 24、(i=1,j=1) = 0
//     ⇒ 修前 poly_b 把 (2.25,1.75) 烧成 7 ⇒ count7 = 1 ⇒ 红
//   臂 C（layer=poly_b）：范围来自 poly_b = [1,3]×[1,3]
//     xSize = ceil((3-1)/1.5) = ceil(1.333) = 2 = ySize ⇒ 2×2 = 4 个像元
//     中心 = (1 + (i+0.5)*1.5, 3 - (j+0.5)*1.5) ⇒ x ∈ {1.75, 3.25}、y ∈ {2.25, 0.75}
//     poly_b = [1,3]×[1,3] ⇒ x 只有 1.75 在里面（3.25 > 3）、y 只有 2.25 在里面（0.75 < 1）
//     ⇒ 只有 (i=0,j=0) 一个中心落在 poly_b 内 ⇒ 修后 count7 = 1、count3 = 0、和 = 1×7 = 7
//     ⇒ 修前另外 3 个中心 (3.25,2.25)(1.75,0.75)(3.25,0.75) 全都在环形的环带上（洞外、外环内）
//       ⇒ 被 poly_a 烧成 3 ⇒ count3 = 3 ⇒ 红
static std::string buildRasterizeTwoLayerFixture(const std::string& fileName) {
    const std::string gpkgPath = (getTestDir() / fileName).string();
    std::error_code ignored;
    fs::remove(gpkgPath, ignored);

    auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    EXPECT_NE(driver, nullptr);
    auto* ds = driver->Create(gpkgPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    EXPECT_NE(ds, nullptr);
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);

    auto addLayer = [&](const char* layerName, OGRGeometry* geom, int value) {
        auto* layer = ds->CreateLayer(layerName, srs.get(), wkbPolygon);
        EXPECT_NE(layer, nullptr);
        OGRFieldDefn valueDefn("value", OFTInteger);
        layer->CreateField(&valueDefn);
        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetGeometry(geom);
        feat->SetField("value", value);
        layer->CreateFeature(feat);
        OGRFeature::DestroyFeature(feat);
        delete geom;
    };

    auto ring = [](double minX, double minY, double maxX, double maxY) {
        auto* r = new OGRLinearRing();
        r->addPoint(minX, minY);
        r->addPoint(maxX, minY);
        r->addPoint(maxX, maxY);
        r->addPoint(minX, maxY);
        r->addPoint(minX, minY);
        return r;
    };

    // 第一层 poly_a：外环套内环的一个多边形（洞 = [1,3]×[1,3]）
    auto* donut = new OGRPolygon();
    donut->addRing(ring(0.0, 0.0, 4.0, 4.0));
    donut->addRing(ring(1.0, 1.0, 3.0, 3.0));
    addLayer("poly_a", donut, 3);

    // 第二层 poly_b：正好填住洞的另一个多边形，与 poly_a 的几何互不重叠
    auto* plug = new OGRPolygon();
    plug->addRing(ring(1.0, 1.0, 3.0, 3.0));
    addLayer("poly_b", plug, 7);

    GDALClose(ds);
    return gpkgPath;
}

// 把一次 rasterize 的产物读回来，按值分类计数并求和（三条臂共用同一套数数口径）。
struct RasterizeBurnTally {
    std::vector<float> pixels;
    int count3 = 0;
    int count7 = 0;
    int countOther = 0;   // 既不是 3 也不是 7 又不是 0 的像元
    double sum = 0.0;
};

static RasterizeBurnTally tallyBurnedPixels(const std::string& output, int width, int height) {
    RasterizeBurnTally tally;
    tally.pixels = readWholeBand(output, width, height);
    for (const float v : tally.pixels) {
        tally.sum += static_cast<double>(v);
        if (v == 3.0f) {
            ++tally.count3;
        } else if (v == 7.0f) {
            ++tally.count7;
        } else if (v != 0.0f) {
            ++tally.countOther;
        }
    }
    return tally;
}

// 臂 A + 臂 B：点名 poly_a 与不传 layer 必须给出**同一个**产物，而且里面不得出现 7。
// 这是本缺陷最硬的一条证据：改前 GDALRasterize 把整个 srcDS 交给烧写 ⇒ 未点名的 poly_b 一起烧。
TEST_F(PluginTest, VectorRasterizeLayerParameterLimitsBurnedLayer) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string gpkgPath = buildRasterizeTwoLayerFixture("e2e_rasterize_2layer_input.gpkg");

    struct Arm {
        const char* tag;     // 进文件名 ⇒ 只用 ASCII
        const char* name;    // 只进断言文案
        const char* layer;   // nullptr ⇒ 不传该参数
    };
    const std::vector<Arm> arms = {
        {"named", "点名 layer=poly_a", "poly_a"},
        {"blank", "留空 layer（应当烧第一层）", nullptr},
    };

    for (const Arm& arm : arms) {
        const std::string outputName = std::string("e2e_rasterize_2layer_a_") + arm.tag + ".tif";
        const std::string output = utf8PathString(getTestDir() / outputName);
        removeRasterizeOutput(output);

        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("rasterize");
        params["input"] = gpkgPath;
        params["output"] = output;
        params["resolution"] = 1.5;
        params["attribute"] = std::string("value");
        if (arm.layer != nullptr) {
            params["layer"] = std::string(arm.layer);
        }

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << arm.name << ": " << result.message;

        // 尺寸来自**点名层**的外包（这一条改前就是对的，留着钉住"extent 只看点名层"）
        ASSERT_NE(result.metadata.find("raster_width"), result.metadata.end()) << arm.name;
        EXPECT_EQ(result.metadata.at("raster_width"), "3") << arm.name << "：ceil(4/1.5)=3";
        EXPECT_EQ(result.metadata.at("raster_height"), "3") << arm.name << "：ceil(4/1.5)=3";
        // N134 的记账面：source_feature_count 只数**被烧的那一层**。算式：被烧的 poly_a 有 1 个要素 ⇒ "1"；
        // 旧的全层求和口径给 1+1 = 2，等于把没递交给烧写的 poly_b 也记进产物的自证里。
        EXPECT_EQ(result.metadata.at("source_feature_count"), "1")
            << arm.name << "：poly_a = 1 个要素（留空臂烧的也是第一层 = poly_a，同一答案）";

        const auto tally = tallyBurnedPixels(output, 3, 3);
        ASSERT_EQ(tally.pixels.size(), 9u) << arm.name << "：算式前提是 3×3=9 个像元";
        EXPECT_EQ(tally.count7, 0)
            << arm.name << "：N134 —— layer=poly_b 的像元值 7 出现在产物里，说明未点名的图层也被烧了";
        EXPECT_EQ(tally.countOther, 0) << arm.name << "：值集合必须恰好 ⊆ {0,3}";
        EXPECT_EQ(tally.count3, 8) << arm.name << "：手推 9 个中心里只有 (2.25,1.75) 落在洞里";
        EXPECT_EQ(tally.sum, 24.0) << arm.name << "：手推 8×3 = 24（3 在二进制下精确）";
        EXPECT_EQ(tally.pixels[1 * 3 + 1], 0.0f) << arm.name << "：洞心 (i=1,j=1) 不该被烧";
        EXPECT_EQ(tally.pixels[0], 3.0f) << arm.name << "：中心 (0.75,3.25) 在环带上";
    }
}

// 臂 C：点名第二层 poly_b ⇒ 烧写范围跟着换成 poly_b 的外包，且 3 不得出现。
// 这条臂同样"改前必红"：2×2 栅格里有 3 个像元中心落在 poly_a 的环带上（见夹具算式），
// 改前那 3 个会被一起烧成 3；而改前更早一步是整条动作直接失败（多图层源没有 -l/-sql）。
TEST_F(PluginTest, VectorRasterizeLayerParameterCanSelectSecondLayer) {
    auto* p = mgr_.find("vector");
    if (!p) {
        FAIL() << "plugin not loaded: vector";
    }

    const std::string gpkgPath = buildRasterizeTwoLayerFixture("e2e_rasterize_2layer_c_input.gpkg");
    const std::string output =
        (getTestDir() / "e2e_rasterize_2layer_c_output.tif").string();
    removeRasterizeOutput(output);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("rasterize");
    params["input"] = gpkgPath;
    params["output"] = output;
    params["resolution"] = 1.5;
    params["attribute"] = std::string("value");
    params["layer"] = std::string("poly_b");

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    // 范围来自点名层 poly_b = [1,3]×[1,3] ⇒ ceil(2/1.5) = 2
    EXPECT_EQ(result.metadata.at("raster_width"), "2") << "手推：ceil((3-1)/1.5)=2";
    EXPECT_EQ(result.metadata.at("raster_height"), "2");
    EXPECT_EQ(result.metadata.at("burn_field"), "value");
    // N134 的记账面（控制器裁定随本刀一起收紧）：source_feature_count 只数**被烧的那一层**，
    // 不再按输入数据集全部图层求和。算式：点名层 poly_b 有 1 个要素 ⇒ 期望 "1"；
    // 旧的"全层求和"口径会给 1+1 = 2，那等于宣称烧了没被递交给烧写的 poly_a。
    EXPECT_EQ(result.metadata.at("source_feature_count"), "1")
        << "只数被烧的那一层：poly_b = 1 个要素（全层求和的旧口径 2 会把未点名的 poly_a 也记进产物）";

    const auto tally = tallyBurnedPixels(output, 2, 2);
    ASSERT_EQ(tally.pixels.size(), 4u) << "算式前提是 2×2=4 个像元";
    EXPECT_EQ(tally.count3, 0)
        << "N134：点名 poly_b 却烧进了未点名的 poly_a（3 出现在产物里）";
    EXPECT_EQ(tally.countOther, 0) << "值集合必须恰好 ⊆ {0,7}";
    EXPECT_EQ(tally.count7, 1) << "手推：4 个中心里只有 (1.75,2.25) 落在 poly_b 内";
    EXPECT_EQ(tally.sum, 7.0) << "手推：1×7 = 7";
    EXPECT_EQ(tally.pixels[0 * 2 + 0], 7.0f) << "(i=0,j=0) 中心 (1.75,2.25) 在 poly_b 内";
    EXPECT_EQ(tally.pixels[1 * 2 + 0], 0.0f) << "(i=1,j=0) 中心 (3.25,2.25) 的 x 超出 poly_b 的 3";
    EXPECT_EQ(tally.pixels[0 * 2 + 1], 0.0f) << "(i=0,j=1) 中心 (1.75,0.75) 的 y 低于 poly_b 的 1";
    EXPECT_EQ(tally.pixels[1 * 2 + 1], 0.0f) << "(i=1,j=1) 中心 (3.25,0.75) 两个方向都出界";
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
    // 期望值是**夹具的函数**，不从被测输出的任何像元读取。
    // 这里原先写的是 `EXPECT_NEAR(readRasterPixel(output, 1, 4), readRasterPixel(output, 7, 4), 1e-4f)`：
    // 第二个操作数取自被测输出本身，于是这一句只断"这两个像元相等"，完全不断"流域标签等于几"
    // —— 把整张流域图重新编号（例如出口发号起点从 1 挪到 0，第一条流域就此与背景值 0 撞车），
    // 这两句照样绿。下面的常量把标签值本身钉住了。
    //
    // 算式（地形来自 createEastDownhillRaster：9x9，dem(x,y) = 9 - x，逐行完全相同、自西向东单调下降）：
    //   1) D8 流向：内部像元 x,y 属于 [1,7]。中心比正东邻居高 1，坡度 = 1/1.0 = 1.0；
    //      东南、东北对角邻居同样低 1 但距离 1.41421356，坡度 = 0.707 较小；南北方向不降。
    //      最大正坡度方向唯一 ⇒ 每个内部像元的流向码都是 1（正东）。
    //   2) 出口：只有 x = 7 的内部像元，其东邻 x+1 = 8 触到 cols-1 = 8 ⇒ 判为出口。
    //      出口按 (y 升序, x 升序) 依次发号，号从 1 起 ⇒ 第 y 行的出口拿到标签 y，
    //      全图共 7 条流域，标签 1..7，每条流域恰好是"同一行的内部像元"。
    //   3) 上游回填：按拓扑逆序把出口标签沿流向反向往西铺 ⇒ 行 y 的 x = 1..7 全部拿到标签 y。
    //   ⇒ 结论：内部像元 (x,y) 的标签 = 行号 y，与列号 x 无关（横向同盆地不变量的来源）。
    //   最外圈 x = 0/8、y = 0/8 不在上面两个循环的扫描范围内 ⇒ 标签 0；本用例只钉内部，
    //   不拿"外圈为 0"当契约（那是当前实现的扫描边界，不是流域语义）。
    EXPECT_NEAR(readRasterPixel(output, 1, 1), 1.0f, 1e-4f);  // 第 1 行流域：标签 1（发号起点是 1，不是 0）
    EXPECT_NEAR(readRasterPixel(output, 4, 3), 3.0f, 1e-4f);  // 第 3 行：相邻两行分属两条不同流域
    EXPECT_NEAR(readRasterPixel(output, 4, 4), 4.0f, 1e-4f);  // 第 4 行：标签 4
    EXPECT_NEAR(readRasterPixel(output, 1, 4), 4.0f, 1e-4f);  // 同流域不变量：行 4 的西端也是 4
    EXPECT_NEAR(readRasterPixel(output, 7, 4), 4.0f, 1e-4f);  // 同流域不变量：行 4 的出口像元也是 4
    EXPECT_NEAR(readRasterPixel(output, 7, 7), 7.0f, 1e-4f);  // 第 7 行：标签 7（流域总数 7 的上界）
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

// ============================================================================
// Task 12：viewshed 的曲率/折射系数参数化 + 非投影 CRS 告警。
//
// R-T12-1 的"先量再断"要求本组用例的几何必须落在修正量真正起作用的尺度上：
// Δz ≈ curvCoeff·d²/(2R)（R=6371 km，curvCoeff=0.85714）⇒ 4 km 处只有约 1 m，
// 而 30 km 处约 180 m。所以这里用 60×60、像元 1000 m 的 EPSG:3857 **平坦** DEM
// （简报原稿的 40×40@100 m + 丘被裁定作废：那个尺度上丘根本不遮挡任何像元，
//  两种系数会得到相同的可见像元数，EXPECT_LT 直接失效）。
// 平坦 DEM 上没有地形遮挡，可见性完全由"最大距离"与"曲率下沉"决定，
// 于是 visibleWith < visibleNo 由几何与量级共同保证，而不是靠碰运气。
// ============================================================================
namespace {

constexpr int kViewCurvSide = 60;              // 60×60 像元
constexpr double kViewCurvPixel = 1000.0;      // 1 像元 = 1 km ⇒ 图幅 60 km 见方
constexpr double kViewCurvMaxDistance = 30000.0;   // 30 km：把"图幅外缘"从两个计数里都切掉
constexpr double kViewCurvObserverHeight = 10.0;
constexpr double kViewCurvCoeffGdalDefault = 0.85714;
constexpr double kViewCurvObserverX = 30500.0;   // 中心像元 (30,29) 的像元中心
constexpr double kViewCurvObserverY = 30500.0;

// 平坦、带投影 CRS 的 DEM 夹具（EPSG:3857，走 raster_fixture 写入器，dtype 只有 Float32）。
std::string makeFlatProjectedDem(const char* name, int side, double pixelSize, float elevation) {
    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0;
    options.originY = pixelSize * side;
    options.pixelSize = pixelSize;
    std::vector<float> data(static_cast<size_t>(side) * static_cast<size_t>(side), elevation);
    return gis::tests::writeFloatRaster(getTestDir(), name, side, side, data, options);
}

// 数"可见"像元（= GDALViewshedGenerate 的 dfVisibleVal 255）。这里按 GDT_Byte 读回，
// 输出类型本身是不是 Byte 由 Tier 3 的 TerrainViewshedOutputsAreBothByteMask 单独把守，
// 两条用例各管一层，免得"类型漂移成 Float32"和"曲率没生效"混成同一条红。
int countVisibleViewshedCells(const std::string& path, int width, int height) {
    auto ds = gis::tests::openFixtureRaster(path, "countVisibleViewshedCells");
    if (!ds) {
        return -1;   // 夹具已经把失败报在 raster_fixture.h 那一行
    }
    auto* band = ds->GetRasterBand(1);
    if (!band) {
        ADD_FAILURE() << "countVisibleViewshedCells: band 1 is null in " << path;
        return -1;
    }
    std::vector<unsigned char> data(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    if (band->RasterIO(GF_Read, 0, 0, width, height, data.data(), width, height, GDT_Byte, 0, 0)
        != CE_None) {
        ADD_FAILURE() << "countVisibleViewshedCells: RasterIO failed for " << path;
        return -1;
    }
    int visible = 0;
    for (const unsigned char value : data) {
        visible += (value == 255) ? 1 : 0;
    }
    return visible;
}

// 跑一次 terrain.viewshed。hasCurvatureCoeff=false 时**完全不传**该键，
// 用来把"spec/代码兜底的默认值就是 0.85714"与"显式传 0.85714"钉成同一件事。
gis::framework::Result runCurvViewshed(gis::framework::IGisPlugin* plugin,
                                       gis::core::ProgressReporter& progress,
                                       const std::string& input,
                                       const std::string& output,
                                       bool hasCurvatureCoeff,
                                       double curvatureCoeff) {
    std::map<std::string, gis::framework::ParamValue> params = {
        {"action", std::string("viewshed")},
        {"input", input},
        {"output", output},
        {"band", 1},
        {"observer_x", kViewCurvObserverX},
        {"observer_y", kViewCurvObserverY},
        {"observer_height", kViewCurvObserverHeight},
        {"target_height", 0.0},
        {"max_distance", kViewCurvMaxDistance},
    };
    if (hasCurvatureCoeff) {
        params["curvature_coeff"] = curvatureCoeff;
    }
    return plugin->execute(params, progress);
}

} // namespace

// R-T12-1：曲率/折射修正必须真的进入 GDALViewshedGenerate 的第 13 个实参（dfCurvCoeff）。
// 改前该实参被硬编码成 0.0（=禁用），三次运行的可见像元数完全相同 ⇒ EXPECT_LT 红。
TEST_F(PluginTest, ViewshedUsesEarthCurvatureCoefficientByDefault) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    const std::string input = makeFlatProjectedDem(
        "view_curv_input.tif", kViewCurvSide, kViewCurvPixel, 100.0f);
    ASSERT_FALSE(input.empty());

    const std::string defaultPath = utf8PathString(getTestDir() / "view_curv_default_output.tif");
    const std::string withPath = utf8PathString(getTestDir() / "view_curv_on_output.tif");
    const std::string noPath = utf8PathString(getTestDir() / "view_curv_off_output.tif");

    const auto defaultRun = runCurvViewshed(p, progress_, input, defaultPath, false, 0.0);
    ASSERT_TRUE(defaultRun.success) << defaultRun.message;
    const auto withRun = runCurvViewshed(p, progress_, input, withPath, true,
                                         kViewCurvCoeffGdalDefault);
    ASSERT_TRUE(withRun.success) << withRun.message;
    const auto noRun = runCurvViewshed(p, progress_, input, noPath, true, 0.0);
    ASSERT_TRUE(noRun.success) << noRun.message;

    const int totalCells = kViewCurvSide * kViewCurvSide;
    const int visibleDefault = countVisibleViewshedCells(defaultPath, kViewCurvSide, kViewCurvSide);
    const int visibleWith = countVisibleViewshedCells(withPath, kViewCurvSide, kViewCurvSide);
    const int visibleNo = countVisibleViewshedCells(noPath, kViewCurvSide, kViewCurvSide);

    // R-T12-1 第 1 步"先量再断"的两个数不再用 std::cout 常驻打印（复审 F-4）：
    // ctest 通过态不收集 stdout，长期留在测试码里只是低价值噪声；同样的两个数已经流进
    // 下面 EXPECT_LT / EXPECT_EQ 的失败消息，而取证时的实测值（启用曲率 2819 个 / coeff=0.0 517 个）
    // 已经抄在这里，红态时还会由失败消息把双方的实际值再打一遍。
    EXPECT_LT(visibleWith, visibleNo)
        << "启用曲率/折射后远距离可见性应更保守；实测 coeff=0.85714 为 " << visibleWith
        << " 个、coeff=0.0 为 " << visibleNo << " 个（相等即说明 dfCurvCoeff 没进算法）";
    EXPECT_EQ(visibleDefault, visibleWith)
        << "不传 curvature_coeff 时必须与显式传 0.85714 同解（默认值就是 GDAL 的 0.85714），"
        << "实测默认 " << visibleDefault << " 个 / 显式 0.85714 " << visibleWith << " 个";

    // R-T12-1 第 3 步：两个数都得是正数且小于总像元数，防"全不可见"伪装成通过。
    EXPECT_GT(visibleWith, 0) << "曲率修正不应把整幅图判成不可见";
    EXPECT_LT(visibleWith, totalCells) << "启用曲率后仍有远处可见像元，说明几何没退化";
    EXPECT_GT(visibleNo, 0);
    EXPECT_LT(visibleNo, totalCells)
        << "coeff=0.0 时也应受 max_distance=" << kViewCurvMaxDistance
        << " 约束（否则可见数=总像元数，健全性检查失去意义），实测 " << visibleNo << "/" << totalCells;

    // F-3（fix round 1）：两行 curvature_coeff 回显是简报之外新增的 metadata 面，上一轮零断言，
    // 批次 A 的纪律是"加了就得有牙"。断的是**值**不是存在性 ⇒ "键塞了但塞错东西"也红。
    // 三条 run 各自钉一段：
    //   withRun    = 显式传 0.85714 ⇒ 回显必须等于实际入参；
    //   defaultRun = 根本不传该键   ⇒ 回显必须是代码兜底值（与 visibleDefault==visibleWith 那条同源，
    //                但那条断的是算法结果，这条断的是"回显给 GUI/CLI 看的默认值"本身）；
    //   noRun      = 显式传 0.0     ⇒ 回显必须是 0.000000，杀掉"把回显写成常量 0.857140"的假实现。
    // 字符串形态是 std::to_string(double) 的结果（固定 6 位小数），照源码字面量写成 "0.85714" 会假红；
    // 下面的字面量取自改前实测打出的 std::to_string 结果（一次性取证日志不入库，故数字抄在这里）。
    ASSERT_TRUE(withRun.metadata.count("curvature_coeff") > 0)
        << "result.metadata 没回显 curvature_coeff（terrain_plugin.cpp 的 "
           "result.metadata[\"curvature_coeff\"] = std::to_string(curvatureCoeff) 那行没了）";
    EXPECT_EQ(withRun.metadata.at("curvature_coeff"), "0.857140")
        << "显式传 curvature_coeff=0.85714 时回显的实际字符串";
    ASSERT_TRUE(defaultRun.metadata.count("curvature_coeff") > 0);
    EXPECT_EQ(defaultRun.metadata.at("curvature_coeff"), "0.857140")
        << "不传该键时回显的必须是代码兜底 0.85714，否则面板/CLI 读到的默认值不可信";
    ASSERT_TRUE(noRun.metadata.count("curvature_coeff") > 0);
    EXPECT_EQ(noRun.metadata.at("curvature_coeff"), "0.000000")
        << "显式传 0.0（关闭修正）时回显必须跟着变成 0.000000，钉住回显是实际入参不是常量";

    // 另一行回显 meta.algorithmParams["curvature_coeff"] 走的是输出栅格的 GIS_PROCESSING 元数据域，
    // 与 result.metadata 是两条独立通路 ⇒ 单独钉，删任一行都要红。
    {
        auto ds = gis::tests::openFixtureRaster(withPath, "curvature_coeff algorithmParams echo");
        ASSERT_TRUE(ds != nullptr);
        const auto persisted = gis::core::readProcessingMetadata(ds.get());
        const auto it = persisted.algorithmParams.find("curvature_coeff");
        ASSERT_TRUE(it != persisted.algorithmParams.end())
            << "输出栅格的 GIS_PROCESSING 域里没有 GIS_PARAM_curvature_coeff"
               "（meta.algorithmParams[\"curvature_coeff\"] 那行回显没了）";
        EXPECT_EQ(it->second, "0.857140")
            << "写进输出栅格元数据的 curvature_coeff 实际字符串";
    }
}

// R-T12-2：非投影 CRS ⇒ metadata["crs_warning"] 的**值**必须是 Produces 规定的那串，
// 且 result.message 真的追加了同一句提示（Produces 有这条，简报原文没测）。
TEST_F(PluginTest, ViewshedWarnsOnNonProjectedCrs) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg4326;
    // 观察点 (116.002, 40.002) 必须落在图幅内：默认 originY=40.0 时像元 0 的北边就是 40.0，
    // 简报原稿的写法会把观察点甩到栅格外面（改前红在这条执行失败上，与缺陷无关）。
    options.originX = 116.0;
    options.originY = 40.005;
    options.pixelSize = 0.001;
    const std::string input = gis::tests::writeFloatRaster(
        getTestDir(), "view_geographic_input.tif", 9, 9, std::vector<float>(81, 5.0f), options);
    ASSERT_FALSE(input.empty());
    const std::string output = utf8PathString(getTestDir() / "view_geographic_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("viewshed");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["observer_x"] = 116.002;
    params["observer_y"] = 40.002;
    params["observer_height"] = 10.0;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.metadata.count("crs_warning") > 0)
        << "EPSG:4326 输入必须报 crs_warning，实际 metadata 只有 action/band 等键";
    // 值断言：只断"键存在"的假实现（塞空串、塞时间戳）在这里就会红。
    EXPECT_EQ(result.metadata.at("crs_warning"), "observer analysis expects projected CRS");
    // Produces 的第二句承诺：message 末尾追加同样提示（GUI 结果区只看得到 message）。
    EXPECT_NE(result.message.find("observer analysis expects projected CRS"), std::string::npos)
        << "result.message 没拼接 CRS 提示：" << result.message;
}

// R-T12-2 的正例（关键的一条）：投影 CRS 输入时**不得**出现 crs_warning。
// 没有这条，"无条件塞警告"的假实现也能全绿 ⇒ 它的鉴别证据是判别变异 M3，
// 改前状态本身是绿的（缺省就不存在该键），报告里按这条说明。
TEST_F(PluginTest, ViewshedDoesNotWarnOnProjectedCrs) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    const std::string input = makeFlatProjectedDem(
        "view_projected_no_warning_input.tif", 12, 1000.0, 30.0f);
    ASSERT_FALSE(input.empty());
    const std::string output = utf8PathString(getTestDir() / "view_projected_no_warning_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("viewshed");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["observer_x"] = 6500.0;
    params["observer_y"] = 5500.0;
    params["observer_height"] = 10.0;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.metadata.count("crs_warning"), 0u)
        << "EPSG:3857 是投影坐标系，不该有 CRS 告警，实际值="
        << (result.metadata.count("crs_warning") ? result.metadata.at("crs_warning") : std::string{});
    EXPECT_EQ(result.message.find("observer analysis expects projected CRS"), std::string::npos)
        << "投影 CRS 的 message 里混进了非投影提示：" << result.message;
}

// R-T12-2 守卫的**另一半**（批次 A 复审 F-1）：栅格**完全没有 CRS** 时也必须告警。
// WHY 本条与 ViewshedWarnsOnNonProjectedCrs **不可互相替代**——terrain_plugin.cpp 的判据是
//   `if (auto* srs = srcDs->GetSpatialRef(); !srs || !srs->IsProjected())`，两个 half 吃的是
//   两条互斥的输入形态：
//   * ViewshedWarnsOnNonProjectedCrs：EPSG:4326 ⇒ GetSpatialRef() **非空**、IsProjected() 为假
//     ⇒ 只命中 `!srs->IsProjected()` 右半（本条对它毫无覆盖，删掉右半它才红、本条仍绿）；
//   * 本条：夹具默认 crs = FixtureCrs::None ⇒ raster_fixture.h 的 epsgFor(None) 返回 0、
//     applyFixtureHeader 只在 epsg>0 时才 SetProjection ⇒ 产出的栅格**没有 SRS**，
//     GetSpatialRef()==nullptr ⇒ 只命中 `!srs` 左半（删掉左半 `!srs ||` 只有本条会红）。
// ⇒ 两条各钉一个 half，合起来才把整条守卫钉住；缺任何一条，另一半都是"绿色掩盖的缺陷"。
//   两个方向都做过判别变异（报告 fix round 1 的 F-1 段）：删左半 ⇒ 只有本条红、4326 那条绿；
//   删右半 ⇒ 只有 4326 那条红、本条绿。所以二者**不可互相替代**。
// WHY 旧用例 TerrainViewshedExecution 不算覆盖：它用 createConstantRaster（从不 SetProjection，
// 于是同样走 `!srs` 左半），但它只断 success/action/像元值，对 crs_warning 一条断言都没有 ⇒
// 分支被执行、却零断言，删掉左半它照样绿（这正是复审 F-1 实测到的现象）。
// 而"无 CRS 的 DEM"在 GIS 里是极常见的输入（现场切片、老数据、手工导出），比地理坐标系更需要这条提示。
TEST_F(PluginTest, ViewshedWarnsWhenRasterHasNoCrsAtAll) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    // 几何与 ViewshedDoesNotWarnOnProjectedCrs 逐项对齐（12×12、像元 1000 m、平坦 30 m、
    // 观察点 (6500,5500)），唯一差别是**不写 CRS**：于是两条用例之间只有
    // "GetSpatialRef() 是不是 nullptr"这一个变量，红点不会被几何差异污染。
    gis::tests::RasterFixtureOptions options;   // crs 保持默认 FixtureCrs::None：本条是第一个把"无 SRS"当被测前提的用例
    options.originX = 0.0;
    options.originY = 12000.0;
    options.pixelSize = 1000.0;
    const std::string input = gis::tests::writeFloatRaster(
        getTestDir(), "view_no_crs_input.tif", 12, 12, std::vector<float>(144, 30.0f), options);
    ASSERT_FALSE(input.empty());

    // 前提检查（不是被测行为）：夹具产出的栅格必须**真的**没有 SRS。
    // 若哪天 raster_fixture 给 None 也写了投影，本条会静默退化成 4326 那条的复制品、
    // 于是左半分支重新变成零覆盖 ⇒ 这里当场报红，而不是"看起来还在测"。
    {
        auto ds = gis::tests::openFixtureRaster(input, "ViewshedWarnsWhenRasterHasNoCrsAtAll");
        ASSERT_TRUE(ds != nullptr);
        ASSERT_TRUE(ds->GetSpatialRef() == nullptr)
            << "夹具产出的栅格带了 SRS，本用例不再钉 `!srs` 半分支（epsgFor(None)=0 / "
               "applyFixtureHeader 只在 epsg>0 时 SetProjection 这两条前提被改动了）";
    }

    const std::string output = utf8PathString(getTestDir() / "view_no_crs_output.tif");
    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("viewshed");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["observer_x"] = 6500.0;
    params["observer_y"] = 5500.0;
    params["observer_height"] = 10.0;

    const auto result = p->execute(params, progress_);
    // 无 CRS 只是"结果不可信"，不是执行失败：断 success 钉住"告警不升级为失败"这条口径
    // （与前两条 CRS 用例同形，不许退化成只断存在性）。
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_TRUE(result.metadata.count("crs_warning") > 0)
        << "完全没有 CRS 的 DEM 必须报 crs_warning：连线性单位都无法判定时曲率/折射修正必然失效，"
           "实际 metadata 只有 action/band 等键";
    // 值断言：塞空串、塞别的文案在这里就会红（与前两条同一份字面量）。
    EXPECT_EQ(result.metadata.at("crs_warning"), "observer analysis expects projected CRS");
    // Produces 的第二句承诺：message 里拼接了同一串（GUI 结果区只看得到 message）。
    EXPECT_NE(result.message.find("observer analysis expects projected CRS"), std::string::npos)
        << "result.message 没拼接 CRS 提示：" << result.message;
}

// 全局闸门（Task 10 审查后新增，"谁加参数谁补可见性"的配套）：curvature_coeff 进 paramSpecs()
// 的同批必须进 terrain.viewshed 自己的 visibleKeys，并且本 action 的面板锁把两侧钉成完全相等。
// 形状照抄 TerrainFillSinksGuiPanelRendersEpsilonAndHidesKeysTheActionNeverReads：
// 渲染层问 ParamCardWidget::hasParam（param_card_widget.cpp:937 读 entries_，即真正建出来的控件行），
// **不用** ParamWidget::hasParam（param_widget.cpp:158-163 只扫 setParamSpecs 存下的 specs_，
// 等于把有效集合再抄一遍，断不出"这一行到底建没建"）。
// spec 的默认值/区间是**值断言**：只做 count(key)>0 的存在性断言在这一层不算数，
// 而 N35（src/cli/main.cpp 对未知键不报错、原样存成 std::string ⇒ getParam<double> 静默回落）
// 决定"spec 有没有被声明"本身也必须被锁住。
TEST_F(PluginTest, TerrainViewshedGuiPanelRendersCurvatureCoeffAndHidesKeysTheActionNeverReads) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);
    const auto specs = p->paramSpecs();

    const auto* config = gis::gui::findActionUiConfig("terrain", "viewshed");
    ASSERT_NE(config, nullptr) << "terrain.viewshed 缺少 actionUiConfig 条目";

    const auto effective = gis::gui::buildEffectiveGuiParamSpecs(
        "terrain", "viewshed", specs, config->visibleKeys, config->requiredKeys);
    std::set<std::string> effectiveKeys;
    for (const auto& spec : effective) {
        effectiveKeys.insert(spec.key);
    }

    // 有效集合必须与白名单**完全相等**：少一个是错键名（真参数被隐藏），多一个是空集不过滤。
    EXPECT_EQ(effectiveKeys, config->visibleKeys) << "terrain.viewshed 的有效键集与白名单不一致";
    EXPECT_GT(effectiveKeys.count("curvature_coeff"), 0u)
        << "curvature_coeff 必须被渲染：doViewshed 真的读它（getParam<double>）";
    // 本 action 不读的键一律不得出现在面板上（放一个填了没用的键就是本批次要消灭的形态）。
    for (const char* deadKey : {"accum_threshold", "epsilon", "observer_points", "z_factor",
                                "water_level", "reference"}) {
        EXPECT_EQ(effectiveKeys.count(deadKey), 0u)
            << deadKey << " 从未被 doViewshed 读取，不得出现在 terrain.viewshed 面板上";
    }

    const gis::framework::ParamSpec* curvSpec = nullptr;
    for (const auto& spec : specs) {
        if (spec.key == "curvature_coeff") {
            curvSpec = &spec;
        }
    }
    ASSERT_NE(curvSpec, nullptr) << "paramSpecs() 没有 curvature_coeff，面板渲染的是空气";
    EXPECT_EQ(curvSpec->type, gis::framework::ParamType::Double)
        << "类型漂移成 Int/String 后 getParam<double> 会静默回落代码兜底，取值断言照样绿";
    EXPECT_FALSE(curvSpec->required) << "curvature_coeff 是可选键（有 GDAL 默认值可兜底）";

    const auto* declaredDefault = std::get_if<double>(&curvSpec->defaultValue);
    ASSERT_NE(declaredDefault, nullptr) << "curvature_coeff 的默认值必须是 double 变体";
    EXPECT_EQ(*declaredDefault, 0.85714)
        << "spec 默认值必须与 terrain_plugin.cpp doViewshed 的代码兜底 0.85714 同值，"
           "否则 GUI 提交 0.85714 而 CLI/直调走另一个数";
    const auto* declaredMin = std::get_if<double>(&curvSpec->minValue);
    const auto* declaredMax = std::get_if<double>(&curvSpec->maxValue);
    ASSERT_NE(declaredMin, nullptr) << "curvature_coeff 的下界必须是 double 变体";
    ASSERT_NE(declaredMax, nullptr) << "curvature_coeff 的上界必须是 double 变体";
    EXPECT_EQ(*declaredMin, 0.0) << "0 的语义是「关闭修正」，下界不能高于它";
    EXPECT_EQ(*declaredMax, 2.0)
        << "上界放宽到 2.0 之外等于把曲率/折射系数开到无物理意义的取值（大气折射典型值 6/7）";

    // 非必填：requiredKeys 只能要求插件本来就会拒绝的东西，doViewshed 对缺省会兜底 0.85714。
    EXPECT_EQ(config->requiredKeys.count("curvature_coeff"), 0u);
    EXPECT_EQ(config->visibleKeys.count("action"), 0u)
        << "\"action\" 被 buildEffectiveGuiParamSpecs 无条件跳过，写进白名单无效还与既有约定分叉";
    // 默认值永不被展示层覆写：commonParamText 那张表按参数键跨插件全局生效。
    EXPECT_EQ(gis::gui::findCommonParamText("curvature_coeff"), nullptr);

    // R-T12-4：只追加到 viewshed 这一条。multi 今天不读该键，加进它的白名单
    // 就是"面板上有个改了没用的参数"，而 Task 10 的元断言（visibleKeys ⊆ paramSpecs）抓不到这种。
    const auto* multiConfig = gis::gui::findActionUiConfig("terrain", "viewshed_multi");
    ASSERT_NE(multiConfig, nullptr) << "terrain.viewshed_multi 缺少 actionUiConfig 条目";
    EXPECT_EQ(multiConfig->visibleKeys.count("curvature_coeff"), 0u)
        << "runViewshedMultiProcess 的第 13 实参仍是硬编码 0.0，白名单不能有这个键";
    const auto multiEffective = gis::gui::buildEffectiveGuiParamSpecs(
        "terrain", "viewshed_multi", specs, multiConfig->visibleKeys, multiConfig->requiredKeys);
    std::set<std::string> multiEffectiveKeys;
    for (const auto& spec : multiEffective) {
        multiEffectiveKeys.insert(spec.key);
    }
    EXPECT_EQ(multiEffectiveKeys, multiConfig->visibleKeys)
        << "terrain.viewshed_multi 的有效键集与白名单不一致";

    // 渲染层：只问 ParamCardWidget::hasParam（entries_）。
    ParamWidget panel;
    panel.setUiContext("terrain", "viewshed");
    panel.setParamSpecs(effective);
    const auto cards = panel.findChildren<ParamCardWidget*>();
    ASSERT_FALSE(cards.empty()) << "terrain.viewshed 没有生成任何参数卡片";
    std::set<std::string> renderedKeys;
    for (const auto& spec : specs) {
        if (spec.key == "action") {
            continue;
        }
        for (const auto* card : cards) {
            if (card && card->hasParam(spec.key)) {
                renderedKeys.insert(spec.key);
            }
        }
    }
    EXPECT_EQ(renderedKeys, effectiveKeys) << "terrain.viewshed 的 widget 树与有效 spec 不一致";
    EXPECT_GT(renderedKeys.count("curvature_coeff"), 0u)
        << "curvature_coeff 的控件行没建出来：白名单里有、面板上没有";
}

// R-T12-3：两个视域动作的**输出契约**必须一致。改前形态：单点走 GDALViewshedGenerate("GTiff")
// 直接写 GDT_Byte 掩膜，multi 走 gis::core::matToGdalTiff —— 那个函数把输出栅格硬编码成
// GDT_Float32（src/core/src/opencv_wrapper.cpp:43），于是同一个语义的产品有两种类型；
// 同一段 :51-57 还会把源波段声明的 NoData **无条件抄到输出波段**，而 0/255 掩膜里根本没有
// 无效像元（dfOutOfRangeVal 与 dfNoDataVal 都是 0.0，即"不可见"），抄过去等于给产品打一个
// 会把某些 0 当 nodata 渲染的声明。这两条都已读码核实，不是转述。
// WHY 夹具的源 DEM 必须声明 NoData：那是上面第二条的唯一触发条件。
TEST_F(PluginTest, TerrainViewshedOutputsAreBothByteMask) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.hasNoData = true;
    options.noData = -9999.0;
    options.originX = 0.0;
    options.originY = 20000.0;
    options.pixelSize = 1000.0;
    const int side = 20;
    std::vector<float> data(static_cast<size_t>(side) * static_cast<size_t>(side), 30.0f);
    const std::string input = gis::tests::writeFloatRaster(
        getTestDir(), "view_byte_input.tif", side, side, data, options);
    ASSERT_FALSE(input.empty());

    const std::string singleOutput = utf8PathString(getTestDir() / "view_byte_single_output.tif");
    std::map<std::string, gis::framework::ParamValue> singleParams;
    singleParams["action"] = std::string("viewshed");
    singleParams["input"] = input;
    singleParams["output"] = singleOutput;
    singleParams["band"] = 1;
    singleParams["observer_x"] = 5500.0;
    singleParams["observer_y"] = 14500.0;
    singleParams["observer_height"] = 10.0;
    const auto singleRun = p->execute(singleParams, progress_);
    ASSERT_TRUE(singleRun.success) << singleRun.message;

    const std::string multiOutput = utf8PathString(getTestDir() / "view_byte_multi_output.tif");
    std::map<std::string, gis::framework::ParamValue> multiParams;
    multiParams["action"] = std::string("viewshed_multi");
    multiParams["input"] = input;
    multiParams["output"] = multiOutput;
    multiParams["band"] = 1;
    multiParams["observer_points"] = std::string("5500,14500;15500,4500");
    multiParams["observer_height"] = 10.0;
    const auto multiRun = p->execute(multiParams, progress_);
    ASSERT_TRUE(multiRun.success) << multiRun.message;

    // 逐幅量：波段数、声明类型、有没有被声明 NoData、像元取值集合。
    struct MaskMeasure {
        int bands = -1;
        int dtype = -1;
        bool noDataDeclared = false;
        double noData = 0.0;
        int visible = 0;
        int other = 0;
    };
    auto measure = [&](const std::string& path, MaskMeasure& out) {
        auto ds = gis::tests::openFixtureRaster(path, "TerrainViewshedOutputsAreBothByteMask");
        ASSERT_TRUE(ds != nullptr);
        out.bands = ds->GetRasterCount();
        auto* band = ds->GetRasterBand(1);
        ASSERT_TRUE(band != nullptr);
        out.dtype = static_cast<int>(band->GetRasterDataType());
        out.noDataDeclared = gis::tests::readNoDataValue(path, 1, out.noData);
        std::vector<unsigned char> cells(static_cast<size_t>(side) * static_cast<size_t>(side), 0);
        // 按 Byte 读：改前 multi 的产品是 Float32=255.0，GDAL 会转成 255 ⇒ 这里只用于数取值集合，
        // "类型本身是不是 Byte"由上面的 dtype 单独把守。
        ASSERT_EQ(band->RasterIO(GF_Read, 0, 0, side, side, cells.data(), side, side, GDT_Byte, 0, 0),
                  CE_None);
        for (const unsigned char value : cells) {
            out.visible += (value == 255) ? 1 : 0;
            out.other += (value == 0 || value == 255) ? 0 : 1;
        }
    };
    MaskMeasure single;
    MaskMeasure multi;
    measure(singleOutput, single);
    measure(multiOutput, multi);

    // 这两幅的实测数（bands/dtype/noDataDeclared/visible255/otherValues）不再用 std::cout 常驻打印
    // （复审 F-4）：改前/改后的原始实测行只存在于当时的一次性取证日志里（build/ 不入库，不再指路），
    // 而断言失败时 gtest 会自动把比较双方的实际值打出来，
    // 红态信息量没有损失，通过态本来也不收集 stdout。
    const int totalCells = side * side;
    // (1) 两个动作都写单波段 0/255 掩膜，类型必须都是 GDT_Byte（GDT_Float32 是 6、GDT_Byte 是 1）。
    EXPECT_EQ(single.bands, 1);
    EXPECT_EQ(multi.bands, 1) << "viewshed_multi 是「多个观察点并成一幅掩膜」，不是逐点分层";
    EXPECT_EQ(single.dtype, static_cast<int>(GDT_Byte)) << "单点视域输出类型漂移";
    EXPECT_EQ(multi.dtype, static_cast<int>(GDT_Byte))
        << "viewshed_multi 必须与单点视域同为 GDT_Byte；改前它经 matToGdalTiff 写成 Float32"
           "（opencv_wrapper.cpp:43 把输出类型硬编码成 GDT_Float32）";

    // (2) 源 DEM 声明了 -9999，multi 这一侧改前正是把它抄进 0/255 掩膜的那条路径。
    double sourceNoData = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(input, 1, sourceNoData)) << "夹具没写出 NoData，本条判据不成立";
    EXPECT_EQ(sourceNoData, -9999.0);
    EXPECT_FALSE(multi.noDataDeclared)
        << "0/255 掩膜里没有无效像元，把源 DEM 的 NoData 抄到输出波段（opencv_wrapper.cpp:51-57）"
           "会让不可见像元被当 nodata 渲染；实测声明值=" << multi.noData;
    // 单点这一侧今天由 GDAL 自己打声明（实测 noDataDeclared=1、值 0.0，来自第 12 实参 dfNoDataVal=0.0），
    // 那是另一个缺陷（报告顾虑 C-3），本任务不动它；这里只锁"没把源 DEM 的 -9999 继承过来"。
    EXPECT_NE(single.noData, sourceNoData)
        << "单点视域把源 DEM 的 NoData 抄进了掩膜，实测值=" << single.noData;

    // (3) 值集合只有 {0,255}，且第二个观察点真的参与了并集。
    EXPECT_EQ(single.other, 0) << "掩膜里出现 0/255 之外的取值，说明可见/不可见值被改写过";
    EXPECT_EQ(multi.other, 0) << "掩膜里出现 0/255 之外的取值，说明可见/不可见值被改写过";
    EXPECT_GT(single.visible, 0) << "单点视域整幅不可见（退化）";
    EXPECT_LT(single.visible, totalCells) << "单点视域整幅可见，说明观察点没起作用";
    EXPECT_GT(multi.visible, single.visible)
        << "两个观察点的并集必须严格大于单点结果，否则第二个点等于没算："
        << "multi=" << multi.visible << " single=" << single.visible;
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

// ===================== Task 19：cut_fill / reservoir_volume 的 NoData 与地理参考 =====================
//
// 靶点（src/plugins/terrain/terrain_plugin.cpp 的改前形态 = 提交 ad69780^ ⇒ 下面点名 terrain_plugin.cpp
// 的裸行号一律按这一版读，函数本身按 runXxxProcess 定位，
// 不是 ad69780^:1620/:1631 那两个五行转发包装）：
//   :1180-1189  GetGeoTransform 只在**两边都成功**时才做一致性校验，任一边失败既不报错也不校验；
//               :1199 的 pixelArea 于是取 GDAL 在失败时填进数组的默认单位变换（本仓测试在
//               :1903-1911 已经确认过"失败也会填默认值"这一形态），库容侧 :1269 更是把返回值
//               整个丢弃 ⇒ 面积/体积按凭空单位出数，仍然报 success。
//   :1202-1212 / :1273-1282 体积累加**完全不看 NoData**：GetNoDataValue 全文件只出现在
//               :287（runLocalTerrainProcess）与 :355（runHydrologyTerrainProcess），这两个函数
//               一次都没读 ⇒ -9999 哨兵被当成真实高程参与 inputMat-referenceMat 与
//               waterLevel-elevation 的差值与累加。
//   :658        topoOrder.reserve((rows-2)*(cols-2)) 是 int 乘积：rows<2 而 cols>2（或反之）
//               时乘积为负 ⇒ 隐式转成巨大 size_t ⇒ std::vector::reserve 抛 length_error，
//               异常一路穿出 execute（runHydrologyTerrainProcess 到 :658 之前没有任何尺寸闸门）。
//
// 期望值算式（夹具 pixelSize=1.0 ⇒ gt={116,1,0,40,0,-1} ⇒ pixelArea=|1×(-1)-0×0|=1.0，
// 所以体积在数值上就等于"像元当量 × 深度"）：见各用例内的分区说明与括号里的未修值。
// metadata 一律走 std::to_string(double)（ad69780^:1232-1234 / ad69780^:1302-1304），所以既做 stod 数值比对，
// 也用**同一个 std::to_string** 生成期望串钉住格式通道（Task 15 立的口径，不手拼 "30.000000"）。

// 把结果里的一个 metadata 值取出来打进断言消息：本任务的静默缺陷在**改前**是带着数值报成功的，
// 取证时必须能看到那个数才知道静默的具体形态（恒 0？GDAL 默认单位变换？还是别的）。
// WHY 用 find 而不是 metadata[key]：结果在本类用例里是 const（operator[] 编不过），
// 而且缺键时不该凭空造出一个空串，把"键不存在"与"键存在但为空"混成同一种红。
static std::string t19MetadataValue(const gis::framework::Result& result, const char* key) {
    const auto it = result.metadata.find(key);
    return it == result.metadata.end() ? std::string("<absent>") : it->second;
}

TEST_F(PluginTest, CutFillExcludesNoDataOnBothSides) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.hasNoData = true;
    options.noData = -9999.0;
    options.pixelSize = 1.0;
    // A 区的参考栅格**故意声明另一个哨兵值**：实现里"无效像元写哪一份 NoData"是一个真正的决策
    // （取输入侧声明值，因为 matToGdalTiff 把输入侧的 NoData 抄到输出波段）。两侧哨兵相同的话，
    // 把这条决策改成"写参考侧的值"不会有任何 observable ⇒ 本条断言就杀不掉那个变异（见 R18）。
    gis::tests::RasterFixtureOptions referenceOptions;
    referenceOptions.hasNoData = true;
    referenceOptions.noData = -7777.0;
    referenceOptions.pixelSize = 1.0;
    const int w = 10, h = 10;

    // 四个分区（x 为列、y 为行，各区的像元数与差值都写在后面）：
    //   A 区 x<5            （5×10=50 像元）：只有**参考侧**是 NoData（-7777），design=10 / existing=NoData
    //   B 区 5<=x<7         （2×10=20 像元）：只有**输入侧**是 NoData（-9999），design=NoData / existing=8
    //   C 区 x>=7 且 y<5    （3×5=15 像元）：两侧有效，diff=+2 ⇒ 填方 15×2×1=30
    //   D 区 x>=7 且 y>=5   （3×5=15 像元）：两侧有效，diff=-2 ⇒ 挖方 15×2×1=30
    // A、B 分侧是**必需的**：双侧掩膜是两条独立守卫，只把 NoData 放一侧的用例杀不掉另一侧的变异。
    // 未修时（两侧哨兵同为 -9999 的那次 RED 实测）：A 区 diff=10-(-9999)=+10009 ⇒ fill=30+50×10009=500480；
    //   B 区 diff=-9999-8=-10007 ⇒ cut=30+20×10007=200170。改成 -7777 后 A 区的未修值只是
    //   按同一公式换成 30+50×7787=389380，"红不红"与"哪条守卫被杀"都不依赖哨兵的具体数值。
    std::vector<float> design(static_cast<size_t>(w) * static_cast<size_t>(h), 10.0f);
    std::vector<float> existing(static_cast<size_t>(w) * static_cast<size_t>(h), 8.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = static_cast<size_t>(y * w + x);
            if (x < 5) {
                existing[i] = -7777.0f;      // A 区：参考侧哨兵
            } else if (x < 7) {
                design[i] = -9999.0f;        // B 区：输入侧哨兵
            } else if (y >= 5) {
                design[i] = 8.0f;
                existing[i] = 10.0f;
            }
        }
    }

    const std::string designPath =
        gis::tests::writeFloatRaster(getTestDir(), "t19_cf_design.tif", w, h, design, options);
    const std::string existingPath =
        gis::tests::writeFloatRaster(getTestDir(), "t19_cf_existing.tif", w, h, existing, referenceOptions);
    ASSERT_FALSE(designPath.empty());
    ASSERT_FALSE(existingPath.empty());
    const std::string output = utf8PathString(getTestDir() / "t19_cf_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("cut_fill");
    params["input"] = designPath;
    params["reference"] = existingPath;
    params["output"] = output;
    params["band"] = 1;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    EXPECT_NEAR(std::stod(result.metadata.at("fill_volume")), 30.0, 1e-6)
        << "NoData 像元不得计入体积；A 区（参考侧哨兵）漏进来会让 fill 变成 500480";
    EXPECT_NEAR(std::stod(result.metadata.at("cut_volume")), 30.0, 1e-6)
        << "NoData 像元不得计入体积；B 区（输入侧哨兵）漏进来会让 cut 变成 200170";
    EXPECT_NEAR(std::stod(result.metadata.at("net_volume")), 0.0, 1e-6);
    EXPECT_EQ(result.metadata.at("fill_volume"), std::to_string(30.0))
        << "metadata 走 std::to_string(double)，期望串必须用同一个调用生成";
    EXPECT_NEAR(std::stod(result.metadata.at("invalid_pixels")), 70.0, 1e-6)
        << "无效像元计数 = A 区 50 + B 区 20";

    // 差值栅格：无效像元写 NoData（输入侧有定义时），有效像元保留真实差值。
    EXPECT_NEAR(readRasterPixel(output, 0, 2), -9999.0f, 1e-4f)
        << "A 区（参考侧 -7777 哨兵）要写的是**输入侧声明的** -9999，不是 7787 也不是 -7777";
    EXPECT_NEAR(readRasterPixel(output, 5, 5), -9999.0f, 1e-4f) << "B 区应写哨兵而不是 -10007";
    EXPECT_NEAR(readRasterPixel(output, 8, 2), 2.0f, 1e-4f);
    EXPECT_NEAR(readRasterPixel(output, 8, 7), -2.0f, 1e-4f);
    double outNoData = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 1, outNoData))
        << "输出栅格必须声明 NoData，否则上面那格 -9999 只是普通像元值";
    EXPECT_DOUBLE_EQ(outNoData, -9999.0);
}

TEST_F(PluginTest, ReservoirVolumeSkipsNoDataCells) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.hasNoData = true;
    options.noData = -9999.0;
    options.pixelSize = 1.0;
    const int w = 10, h = 10;

    // 左半（x<5，5×10=50 像元）NoData，右半高程 10.0、water_level=13 ⇒ depth=3：
    //   area = 50×1 = 50，volume = 50×3×1 = 150，invalid_pixels = 50。
    // 未修时左半 depth=13-(-9999)=10012 ⇒ area=100、volume=150+50×10012=500750。
    std::vector<float> dem(static_cast<size_t>(w) * static_cast<size_t>(h), 10.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < 5; ++x) {
            dem[static_cast<size_t>(y * w + x)] = -9999.0f;
        }
    }
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t19_rv_input.tif", w, h, dem, options);
    ASSERT_FALSE(input.empty());
    const std::string output = utf8PathString(getTestDir() / "t19_rv_output.tif");

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("reservoir_volume");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["water_level"] = 13.0;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    EXPECT_NEAR(std::stod(result.metadata.at("reservoir_area")), 50.0, 1e-6);
    EXPECT_NEAR(std::stod(result.metadata.at("reservoir_volume")), 150.0, 1e-6)
        << "NoData 像元的假水深 10012 漏进来会变成 500750";
    EXPECT_EQ(result.metadata.at("reservoir_volume"), std::to_string(150.0));
    EXPECT_NEAR(std::stod(result.metadata.at("invalid_pixels")), 50.0, 1e-6);

    EXPECT_NEAR(readRasterPixel(output, 0, 5), -9999.0f, 1e-4f) << "NoData 区应写哨兵而不是 10012";
    EXPECT_NEAR(readRasterPixel(output, 5, 5), 3.0f, 1e-4f);
    double outNoData = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(output, 1, outNoData));
    EXPECT_DOUBLE_EQ(outNoData, -9999.0);
}

TEST_F(PluginTest, CutFillFailsWhenGeoTransformMissing) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    // 形状 (a)：两侧都缺地理参考。改前 GetGeoTransform 的双成功条件整块跳过 ⇒ 一致性校验没做、
    // pixelArea 取 GDAL 失败时填入的默认单位变换 ⇒ 体积按凭空单位出数还报 success。
    // 形状 (b)：输入侧有、参考侧缺。改前的 `&&` 短路与 (a) 走的是同一条静默臂，
    //           但 pixelArea 来自输入的**有效**变换 ⇒ 数值看着"很合理"，更该显式失败。
    // 两种形状分别钉住两条守卫的**顺序与归属**：先输入后参考，消息各说各的。
    const std::string noGeoInput = createRasterWithoutGeoTransform("t19_cf_nogt_input.tif", 10, 10, 1, 12.0f);
    const std::string noGeoRef = createRasterWithoutGeoTransform("t19_cf_nogt_reference.tif", 10, 10, 1, 10.0f);
    const std::string withGeoInput = createConstantRaster("t19_cf_withgt_input.tif", 10, 10, 12.0f, 116.0, 40.0, 1.0);
    ASSERT_FALSE(noGeoInput.empty());
    ASSERT_FALSE(noGeoRef.empty());
    ASSERT_TRUE(rasterHasNoGeoTransform(noGeoInput));   // 前置条件：夹具造出的确实是"取不到变换"
    ASSERT_TRUE(rasterHasNoGeoTransform(noGeoRef));

    const std::string outputA = utf8PathString(getTestDir() / "t19_cf_nogt_output_a.tif");
    // 先清掉同名产物：本用例断言的是"失败路径不得留产物"，而上一轮（改前的静默成功臂）
    // 或上一次运行留下的同名文件会让这条断言假红——产物存在性只对**本次运行**有意义。
    // 用 error_code 重载而不是抛异常版本：文件本来就不存在是正常路径，不该为此炸掉用例。
    std::error_code removeEc;
    fs::remove(outputA, removeEc);
    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("cut_fill");
    params["input"] = noGeoInput;
    params["reference"] = noGeoRef;
    params["output"] = outputA;
    params["band"] = 1;
    const auto bothMissing = p->execute(params, progress_);
    EXPECT_FALSE(bothMissing.success)
        << "缺地理参考必须显式失败（改前会静默成功，fill_volume=" << t19MetadataValue(bothMissing, "fill_volume") << "）";
    EXPECT_NE(bothMissing.message.find("input raster geotransform is missing"), std::string::npos)
        << bothMissing.message;
    EXPECT_FALSE(fs::exists(outputA)) << "失败路径不得留下产物";

    const std::string outputB = utf8PathString(getTestDir() / "t19_cf_nogt_output_b.tif");
    fs::remove(outputB, removeEc);   // 同上：产物存在性只对本次运行负责
    params["input"] = withGeoInput;
    params["output"] = outputB;
    const auto referenceMissing = p->execute(params, progress_);
    EXPECT_FALSE(referenceMissing.success)
        << "参考侧缺地理参考同样不得静默（改前 pixelArea 来自输入的有效变换，看着完全正常）";
    EXPECT_NE(referenceMissing.message.find("reference raster geotransform is missing"), std::string::npos)
        << referenceMissing.message;
    EXPECT_EQ(referenceMissing.message.find("input raster geotransform"), std::string::npos)
        << "守卫归属不能搞反：" << referenceMissing.message;
    EXPECT_FALSE(fs::exists(outputB));

    // 形状 (c)：两边**都取得到**变换但不一致。这条一致性判据改前就存在，可是被关在
    // `if (两边都成功)` 里、和本次新加的两条守卫挤在同一个块中；本次把它搬到了无条件执行的位置，
    // 所以必须重新钉一次——否则"搬家时把判据写漏/写松"没有任何用例会红。
    // 判别量：输入 pixelSize=1.0、参考 pixelSize=2.0 ⇒ gt[1] 与 gt[5] 各差 1.0，远超 1e-9。
    const std::string mismatchRef =
        createConstantRaster("t19_cf_mismatch_reference.tif", 10, 10, 10.0f, 116.0, 40.0, 2.0);
    ASSERT_FALSE(mismatchRef.empty());
    const std::string outputC = utf8PathString(getTestDir() / "t19_cf_mismatch_output_c.tif");
    fs::remove(outputC, removeEc);
    params["input"] = withGeoInput;
    params["reference"] = mismatchRef;
    params["output"] = outputC;
    const auto mismatched = p->execute(params, progress_);
    EXPECT_FALSE(mismatched.success) << "两处变换都存在但不一致时必须失败";
    EXPECT_NE(mismatched.message.find("input and reference geotransform must match"), std::string::npos)
        << mismatched.message;
    EXPECT_FALSE(fs::exists(outputC)) << "失败路径不得留下产物";
}

TEST_F(PluginTest, ReservoirVolumeFailsWhenGeoTransformMissing) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    // 改前 :1269 写成 `srcDs->GetGeoTransform(gt);`——返回值整个丢弃，
    // 于是缺参考的 DEM 也照样按 GDAL 默认单位变换出一个面积/体积并报 success。
    const std::string input = createRasterWithoutGeoTransform("t19_rv_nogt_input.tif", 10, 10, 1, 10.0f);
    ASSERT_FALSE(input.empty());
    ASSERT_TRUE(rasterHasNoGeoTransform(input));
    const std::string output = utf8PathString(getTestDir() / "t19_rv_nogt_output.tif");
    // 同上：清掉上一轮（或改前静默成功那一次）留下的同名产物，否则"不得留产物"这条会假红。
    std::error_code removeEc;
    fs::remove(output, removeEc);

    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("reservoir_volume");
    params["input"] = input;
    params["output"] = output;
    params["band"] = 1;
    params["water_level"] = 13.0;

    const auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success)
        << "缺地理参考必须显式失败（改前会静默成功，reservoir_volume=" << t19MetadataValue(result, "reservoir_volume") << "）";
    EXPECT_NE(result.message.find("input raster geotransform is missing"), std::string::npos)
        << result.message;
    EXPECT_FALSE(fs::exists(output)) << "失败路径不得留下产物";
}

TEST_F(PluginTest, WatershedOnSingleRowOrColumnRasterDoesNotThrow) {
    auto* p = mgr_.find("terrain");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.pixelSize = 1.0;

    // :658 的 (rows-2)*(cols-2) 只在**一边小于 2 而另一边大于 2**时为负：
    //   1 行 × 6 列 ⇒ (1-2)*(6-2) = -4；6 行 × 1 列 ⇒ (6-2)*(1-2) = -4。
    // 2×N / N×2 的乘积是 **0**，挡不住任何东西 ⇒ 按工单口径**不拿它当用例**。
    // 改前这里负 int 隐式转成巨大 size_t，std::vector::reserve 抛 length_error，
    // 而 runHydrologyTerrainProcess 到 :658 之前只有 input/output/band/z_factor 四项校验，
    // 没有任何尺寸闸门 ⇒ 异常穿出 execute（用 ASSERT_NO_THROW 接住，不让它把整个 gis_tests 进程带走）。
    std::vector<float> oneRow(6);
    for (int x = 0; x < 6; ++x) oneRow[static_cast<size_t>(x)] = static_cast<float>(x);
    const std::string oneRowInput =
        gis::tests::writeFloatRaster(getTestDir(), "t19_ws_1x6.tif", 6, 1, oneRow, options);
    ASSERT_FALSE(oneRowInput.empty());

    const std::string oneRowOutput = utf8PathString(getTestDir() / "t19_ws_1x6_output.tif");
    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("watershed");
    params["input"] = oneRowInput;
    params["output"] = oneRowOutput;
    params["band"] = 1;
    params["z_factor"] = 1.0;

    gis::framework::Result rowResult{};
    ASSERT_NO_THROW(rowResult = p->execute(params, progress_))
        << "1×N 几何让 topoOrder.reserve 收到负乘积 ⇒ reserve 抛 length_error";
    EXPECT_TRUE(rowResult.success) << rowResult.message;
    EXPECT_NEAR(readRasterPixel(oneRowOutput, 3, 0), 0.0f, 1e-4f)
        << "单行栅格的内圈循环一次都不执行，流域网格只能是全 0";

    std::vector<float> oneColumn(6);
    for (int y = 0; y < 6; ++y) oneColumn[static_cast<size_t>(y)] = static_cast<float>(y);
    const std::string oneColumnInput =
        gis::tests::writeFloatRaster(getTestDir(), "t19_ws_6x1.tif", 1, 6, oneColumn, options);
    ASSERT_FALSE(oneColumnInput.empty());
    const std::string oneColumnOutput = utf8PathString(getTestDir() / "t19_ws_6x1_output.tif");
    params["input"] = oneColumnInput;
    params["output"] = oneColumnOutput;

    gis::framework::Result columnResult{};
    ASSERT_NO_THROW(columnResult = p->execute(params, progress_))
        << "N×1 几何是同一个负乘积的另一半";
    EXPECT_TRUE(columnResult.success) << columnResult.message;
    EXPECT_NEAR(readRasterPixel(oneColumnOutput, 0, 3), 0.0f, 1e-4f);
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
// 只修方向、仍保留 convertTo 的错误实现下，本用例照样红（变异实测：#221 的两处读数都变 65536，
// 期望分别是 0 与 3——-3.0 被饱和成 0 ⇒ 整幅无目标像元 ⇒ 落进下面 R-T7-7 那段"全背景"分支）。
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
    // 65536（三处读数一致这一事实本身就是本用例在钉的东西，65536 是按盘上实测取的值）。本用例按
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
// 变异实测（把 gdalBandToMat(srcDS.get(), b) 写成固定取第 1 波段）：该轮 6 条里 2 条红，本用例红在
// band2 的两处读数互换（(4,2) 实测 4 期望 0、(0,2) 实测 0 期望 4），附带多波段那条 band2 实测 0 期望 65536。
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
// （变异轮 A 的实测结论，日志不入库故只留数）。这一条**只**为那一行存在：pixelSize = 0.5 时
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
// TIFFTAG_GDAL_NODATA，逐波段 SetNoDataValue 只有最后一个留得住（PAM 取证的另一半结论：**我们的写出路径
// 不产生 .aux.xml**，所以"边车把别处声明带进来"只能是显式写过边车的那些用例）。
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
    // CLI/GUI 摘要都会遍历 Result::metadata（cli/main.cpp:240-245、gui_data_support.cpp:1930-1938）。
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
// 变异取证（M1：把分歧判定改成"多波段就告警"）：该轮 9 条里**只有本用例红**
// （`89% tests passed, 1 tests failed out of 9`），红在 metadata 告警键数实测 1（期望 0）与告警文本命中。
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
    // 边车会把别处写过的声明静默带进来（PAM 取证实测：本项目自己的写出路径不产生 .aux.xml，
    // 边车只能来自显式写过它的那些用例），而本用例断的正是
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

// R-T7-9：`doProximity` 只读 input/output/max_distance（取证按函数名取：band/nodata_value 归
// raster_manage_plugin.cpp 的 doSetNoData、levels 归 doBuildOverviews、vector/feature_id_field
// 归 doZonalStats。当年写的是 e7cb1e0:207/:208、e7cb1e0:81、e7cb1e0:258/:260 —— 那组数在当时
// 逐条对得上，957643a 往文件头加了 #include <cmath> 之后整段 +1，顺着旧行号会读到 input），但修前
// gui_data_support.cpp 的 raster_manage 配置里没有 proximity 条目
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

// R-T10-8：zonal_stats 原先没有 actionUiConfig 条目 ⇒ 面板渲染插件级全部 9 个参数。
// 读取点取证：doZonalStats 只取 input/vector/output/feature_id_field
// （raster_manage_plugin.cpp:258-261），:264 对空 vector 直接 fail，所以 vector 进 requiredKeys。
// WHY 白名单里没有 band：派发时给的键表含 band，但 raster_manage_plugin.cpp 的 getParam 调用
// 只有上述四处（band 只在 raster_manage_plugin.cpp 的 doSetNoData 里读，与上面 R-T7-9 同一取证口径），
// 列进来就是放一个填了不生效的键。
TEST_F(PluginTest, ZonalStatsGuiPanelHidesParametersTheActionNeverReads) {
    auto* p = mgr_.find("raster_manage");
    ASSERT_NE(p, nullptr);

    const auto* config = gis::gui::findActionUiConfig("raster_manage", "zonal_stats");
    ASSERT_NE(config, nullptr)
        << "zonal_stats 没有 ActionUiConfig 条目：展示层没有白名单可用，面板会渲染全部插件参数";
    EXPECT_EQ(config->visibleKeys,
              (std::set<std::string>{"input", "output", "vector", "feature_id_field"}));
    EXPECT_EQ(config->requiredKeys, (std::set<std::string>{"input", "output", "vector"}));
    EXPECT_EQ(config->visibleKeys.count("action"), 0u);

    const auto effective = gis::gui::buildEffectiveGuiParamSpecs(
        "raster_manage", "zonal_stats", p->paramSpecs(), config->visibleKeys, config->requiredKeys);
    std::set<std::string> effectiveKeys;
    for (const auto& spec : effective) {
        effectiveKeys.insert(spec.key);
    }
    EXPECT_EQ(effectiveKeys, config->visibleKeys);
    for (const char* deadKey : {"band", "levels", "resample", "nodata_value", "max_distance"}) {
        EXPECT_EQ(effectiveKeys.count(deadKey), 0u) << deadKey << " 从未被 doZonalStats 读取";
    }

    for (const auto& spec : effective) {
        if (spec.key == "input" || spec.key == "output" || spec.key == "vector") {
            EXPECT_TRUE(spec.required) << spec.key;
        } else {
            EXPECT_FALSE(spec.required) << spec.key;
        }
    }

    EXPECT_FALSE(gis::gui::actionDescription("raster_manage", "zonal_stats").isEmpty());
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
#ifdef _DEBUG
    // Debug 半边（N99）：matching 插件在 Debug 带 GIS_MATCHING_LIGHTWEIGHT_DEBUG
    // （src/plugins/matching/CMakeLists.txt 是 PRIVATE $<$<CONFIG:Debug>:…> 且插件为 SHARED，
    //  宏只作用于插件自己的 TU，测试 TU 看不见）⇒ doStitch 必返 fail。
    // 这里钉的是"Debug 必须失败 + 失败消息必须是轻量闸门且点名 stitch"，不是"任何失败都放过"。
    ASSERT_FALSE(result.success)
        << "Debug 轻量闸门漂移：stitch 在 Debug 里竟然成功了，本用例的 Debug 半边判据已失效";
    EXPECT_NE(result.message.find("Debug lightweight mode"), std::string::npos)
        << "失败原因不是轻量闸门，而是别的错误：" << result.message;
    EXPECT_NE(result.message.find("stitch"), std::string::npos)
        << "轻量闸门消息没点名 stitch（报成别的动作也算失败）：" << result.message;
    return;
#else
    // Release 半边：真实路径必须成功，之后一条真实契约都不许被软分支吞掉。
    ASSERT_TRUE(result.success)
        << "Release 里 stitch 必须成功；旧软分支会把任何失败静默降级成 return（N99）：" << result.message;
#endif
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
#ifdef _DEBUG
    // Debug 半边（N99）：同 stitch，Debug 里 doEccRegister 必返 fail。
    // 键别断言用 ecc_register：若闸门把动作名报错（换成 stitch 的消息）这里必须能抓到。
    ASSERT_FALSE(result.success)
        << "Debug 轻量闸门漂移：ecc_register 在 Debug 里竟然成功了，本用例的 Debug 半边判据已失效";
    EXPECT_NE(result.message.find("Debug lightweight mode"), std::string::npos)
        << "失败原因不是轻量闸门，而是别的错误：" << result.message;
    EXPECT_NE(result.message.find("ecc_register"), std::string::npos)
        << "轻量闸门消息没点名 ecc_register（报成别的动作也算失败）：" << result.message;
    return;
#else
    // Release 半边：真实配准路径必须成功。
    ASSERT_TRUE(result.success)
        << "Release 里 ecc_register 必须成功；旧软分支会把任何失败静默降级成 return（N99）：" << result.message;
#endif
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

// Task 8：混淆矩阵按 confusionMatrix[参考值][分类值] 构建（doAccuracyAssessment 的像元循环），
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

    // 前提自证（F4）：本用例的红只可能来自 PA/UA 互换，所以两个栅格都必须**没有**声明 NoData；
    // 声明状态一旦漂移，"红=互换"这条归因就不成立了。PAM 边车按文件名匹配，残留的 .aux.xml
    // 会把声明值带进来（N30 口径保留的防御）。
    std::error_code ec;
    fs::remove(reference + ".aux.xml", ec);
    fs::remove(classified + ".aux.xml", ec);
    double declared = 0.0;
    ASSERT_FALSE(gis::tests::readNoDataValue(reference, 1, declared)) << reference;
    ASSERT_FALSE(gis::tests::readNoDataValue(classified, 1, declared)) << classified;

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
    // 前提自证必须用 ASSERT_*（F3）：readNoDataValue 的失败路径（打不开文件/波段为空）也返回
    // false，此时 declared 是残值，后续断言会在已经塌掉的地基上继续跑——正是"前提没了用例还绿"。
    ASSERT_FALSE(gis::tests::readNoDataValue(reference, 1, declared)) << reference;
    ASSERT_FALSE(gis::tests::readNoDataValue(classified, 1, declared)) << classified;

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

// Task 8（R-T8-2）：pbSuccess 修复的反向守护——把判据改成"永不跳过"照样能过上一条用例，却会毁掉
// 真正的 NoData 处理。本用例断的是计数与矩阵结构（NoData 像元不进分母、不当成一个类别），
// PA/UA 的归属由上一条用例负责，这里刻意不再断数值，免得两条用例红在同一维度上分不出原因。
TEST_F(PluginTest, AccuracyAssessmentExcludesDeclaredNoDataPixel) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    // 参考 {-9999, 0, 1, 1} 声明 NoData=-9999；分类 {0, 0, 1, 1} 不声明 NoData
    // 只有像元 0 该被剔除 → total_samples=3、correct_samples=3、overall=1.0、类别只剩 {0,1}
    // "永不跳过"的实现会把 -9999 当成合法类别塞进矩阵 → total_samples=4、overall=0.75、class_count=3
    gis::tests::RasterFixtureOptions refOptions;
    refOptions.hasNoData = true;
    refOptions.noData = -9999.0;
    const std::string reference = gis::tests::writeFloatRaster(
        getTestDir(), "acc_nodata_ref.tif", 2, 2, {-9999.0f, 0.0f, 1.0f, 1.0f}, refOptions);
    const std::string classified = gis::tests::writeFloatRaster(
        getTestDir(), "acc_nodata_cls.tif", 2, 2, {0.0f, 0.0f, 1.0f, 1.0f});
    ASSERT_FALSE(reference.empty());
    ASSERT_FALSE(classified.empty());

    // PAM 边车按文件名匹配：残留声明会让"分类栅格未声明 NoData"这条前提失效。
    std::error_code ec;
    fs::remove(reference + ".aux.xml", ec);
    fs::remove(classified + ".aux.xml", ec);
    double declared = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(reference, 1, declared)) << reference;
    EXPECT_DOUBLE_EQ(declared, -9999.0);
    ASSERT_FALSE(gis::tests::readNoDataValue(classified, 1, declared)) << classified;

    const std::string output = utf8PathString(getTestDir() / "acc_nodata_output.json");

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

    // NoData 值不得成为一个类别：矩阵与 PA/UA 都不该出现 "-9999" 这个键。
    const QJsonObject matrix = root.value("confusion_matrix").toObject();
    ASSERT_TRUE(matrix.contains("0")) << content;
    ASSERT_TRUE(matrix.contains("1")) << content;
    EXPECT_FALSE(matrix.contains("-9999")) << content;
    EXPECT_FALSE(root.value("producers_accuracy").toObject().contains("-9999")) << content;
    EXPECT_FALSE(root.value("users_accuracy").toObject().contains("-9999")) << content;

    // 分母只算存活像元：3 个有效像元、3 个判对（0 类像元因分类栅格未声明 NoData 而保留）。
    EXPECT_EQ(result.metadata.at("total_samples"), "3") << content;
    EXPECT_EQ(result.metadata.at("correct_samples"), "3") << content;
    EXPECT_EQ(result.metadata.at("class_count"), "2") << content;
    EXPECT_NEAR(root.value("overall_accuracy").toDouble(), 1.0, 1e-6) << content;
    EXPECT_EQ(root.value("total_samples").toDouble(), 3.0) << content;
}

// Task 8（fix round 1，I-1 + I-2）：上面三条用例合起来仍然漏掉两个方向——
// 没有任何一条让**分类侧**声明 NoData（于是 classifiedHasNoData 恒 0、跳过条件的分类侧析取项恒 false），
// 也没有任何一条让某个类别只出现在一侧（于是 rowSum/colSum 恒 > 0，守卫的 false 分支与
// "守卫配哪个和"从未被检验）。本用例同时踩中这两处，一个夹具封两个缺口。
TEST_F(PluginTest, AccuracyAssessmentHandlesClassPresentOnlyOnOneSide) {
    auto* p = mgr_.find("classification");
    ASSERT_NE(p, nullptr);

    // 参考 {1,1,1, 2,2,2} 不声明 NoData；分类 {1,1,-9999, 2,2,3} 声明 NoData=-9999
    // 被跳过的只有像元 2 那个 -9999 ⇒ total_samples=5、correct_samples=4、类别 {1,2,3}
    // 混淆(行=参考)：[1][1]=2；[2][2]=2、[2][3]=1
    //   rowSum(1)=2 colSum(1)=2 ⇒ PA(1)=1.0        UA(1)=1.0
    //   rowSum(2)=3 colSum(2)=2 ⇒ PA(2)=2/3        UA(2)=1.0
    //   rowSum(3)=0 colSum(3)=1 ⇒ PA(3)=守卫 0.0   UA(3)=0/1=0.0
    // 类别 3 只出现在分类图一侧，所以它的 rowSum 为 0：PA 必须由守卫给 0.0。守卫与分母一旦错配，
    // PA(3) 就变成 0.0/0.0 ⇒ 写出 -nan(ind) ⇒ 整个 JSON 不可解析，红点落在 isObject()。
    gis::tests::RasterFixtureOptions clsOptions;
    clsOptions.hasNoData = true;
    clsOptions.noData = -9999.0;
    const std::string reference = gis::tests::writeFloatRaster(
        getTestDir(), "acc_oneside_ref.tif", 2, 3,
        {1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f});
    const std::string classified = gis::tests::writeFloatRaster(
        getTestDir(), "acc_oneside_cls.tif", 2, 3,
        {1.0f, 1.0f, -9999.0f, 2.0f, 2.0f, 3.0f}, clsOptions);
    ASSERT_FALSE(reference.empty());
    ASSERT_FALSE(classified.empty());

    // 前提自证：NoData 声明只在分类侧存活——这正是三条既有用例都没覆盖到的那一侧。
    // 用 ASSERT_*：readNoDataValue 的失败路径同样返回 false，前提塌了不能让用例继续跑。
    std::error_code ec;
    fs::remove(reference + ".aux.xml", ec);
    fs::remove(classified + ".aux.xml", ec);
    double declared = 0.0;
    ASSERT_FALSE(gis::tests::readNoDataValue(reference, 1, declared)) << reference;
    ASSERT_TRUE(gis::tests::readNoDataValue(classified, 1, declared)) << classified;
    EXPECT_DOUBLE_EQ(declared, -9999.0);

    const std::string output = utf8PathString(getTestDir() / "acc_oneside_output.json");

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

    // I-1：分类侧的 NoData 像元不进分母。删掉分类侧那条析取项，这里就变成 6。
    EXPECT_EQ(root.value("total_samples").toDouble(), 5.0) << content;
    EXPECT_EQ(result.metadata.at("total_samples"), "5") << content;

    // 结构维度再钉一次：-9999 不能成为一个类别。
    const QJsonObject matrix = root.value("confusion_matrix").toObject();
    EXPECT_FALSE(matrix.contains("-9999")) << content;
    EXPECT_FALSE(root.value("producers_accuracy").toObject().contains("-9999")) << content;
    EXPECT_FALSE(root.value("users_accuracy").toObject().contains("-9999")) << content;

    // I-2：分母为 0 的那一侧只能由守卫给 0.0；另一侧 colSum=1 走的是真除法。
    const QJsonObject pa = root.value("producers_accuracy").toObject();
    const QJsonObject ua = root.value("users_accuracy").toObject();
    ASSERT_TRUE(pa.contains("3")) << content;
    ASSERT_TRUE(ua.contains("3")) << content;
    EXPECT_NEAR(pa.value("3").toDouble(), 0.0, 1e-6) << content;
    EXPECT_NEAR(ua.value("3").toDouble(), 0.0, 1e-6) << content;

    // 两侧都出现的类别必须走真除法：这条挡住"把守卫写成无条件返回 0.0"的实现，
    // 否则 PA(3)/UA(3) 的 0.0 可能来自退化而不是来自分母为 0 的守卫。
    EXPECT_NEAR(pa.value("2").toDouble(), 2.0 / 3.0, 1e-6) << content;
    EXPECT_NEAR(ua.value("2").toDouble(), 1.0, 1e-6) << content;

    EXPECT_EQ(result.metadata.at("correct_samples"), "4") << content;
    EXPECT_EQ(root.value("correct_samples").toDouble(), 4.0) << content;
    EXPECT_EQ(result.metadata.at("class_count"), "3") << content;
}

// =====================================================================
// Task 17：cutting.tile 层级几何与参数面（批次 A 内该 action 的首批用例）。
//
// 为什么这里的用例几乎全部钉"文件集合/字节/消息点名"而不是钉进度行：
// Tier-0 一次性探针实测（探针产物落 build/ 不入库，数字与符号事实抄在下面）复现了控制器的三条事实——
//   F1: 修正前任何 profile 都 Success 但一张瓦片都不写（srcWin[3] 恒为负，
//       cutting_plugin.cpp:919 符号错误 ⇒ 每瓦片 continue）；
//   F2: profile=mercator 时 metadata.json 的 bounds 经纬度整体调换
//       （GDAL3 轴序，:801-813 未设 OAMS_TRADITIONAL_GIS_ORDER）；
//   F3: z<maxZoom 的 -outsize 全球网格真实物化（实测 z=6 5.72s、z=7 21.26s，
//       层级×4 外推 z=10 ≈ 21min），按字面计划不可行 ⇒ R-T17-7 改为
//       "单一 warped 数据集 + tileWorldRect 反算像元窗口"。
// 断言形态按 R-T17-9：路径集合写死字面量（z=10 为 {512,513}×{512,513} 4 张、
// z=11 为 {1024..1026}×{1024..1026} 9 张——列 1027 与行 1027 整体落东/南 NoData 带，
// 16−4−4+1=9；算术与出处见用例 TileLevelGeometryUsesWebMercatorGrid 内"WHY 9 张"那段注释），
// 亮度/对比度留给 Task 18。
// =====================================================================

namespace {

// 临时文件侦察用：tile 的 warped 临时栅格名带 pid（R-T17-4），测试与插件同进程，
// CPLGetPID() 与插件拼出的名字一致。
// 修复轮 F3：扫描口径从"整个系统临时目录里所有 gis_tile_warped_*.tif"**收窄到本进程自己那一份**。
// WHY：本用例证明的是"本进程的动作不会把临时栅格拉在临时目录里"，而改前按前缀扫全目录 ⇒
//   **别的进程**（被 timeout 杀掉的测试、gis-cli 取证、上一轮崩溃的残留）留下的同名文件
//   也会让 #305/#306 判红，是假红而不是缺陷：本批真红过两轮（一轮是 Release 全量的 305/306，
//   一轮是 Debug 里手工放一个 `gis_tile_warped_987654.tif` 冒充他进程残留）。
//   产品侧文件名本来就带 CPLGetPID()，所以按 pid 归属匹配不损失"漏清理必红"的原意：
//   本进程写出的文件必然带本进程 pid 这一段。
//   放弃的那一半能力（顺带发现别的进程的历史残留）不属于本用例的契约，交脚本/人工巡检。
// Task B6 / m-4：产品侧在 pid 之后又加了一段"每次调用唯一"的序号
//   （gis_tile_warped_<pid>_<seq>.tif），所以这里必须跟着改成**两种形态都认**：
//   `<pid>` 与 `<pid>_<数字>`。写死旧名会让这条扫描在新命名下永远扫不到东西 ——
//   那等于把 R-T17-4 的两条清理用例（#315/#316）悄悄改成恒真。
//   分隔符必须是 `_`：pid 1234 不能把别的进程的 `gis_tile_warped_12345_1.tif` 认成自己的。
fs::path t17TempDir() {
    return fs::temp_directory_path();
}

// name 属于本进程的 warped 临时栅格？（旧 pid-only 形态与新 pid+seq 形态都算）
bool t17IsOwnWarpedName(const std::string& name, const std::string& ignoreName = std::string()) {
    if (!ignoreName.empty() && name == ignoreName) return false;   // 用例自己种的哨兵不算泄漏
    const std::string prefix = "gis_tile_warped_";
    const std::string suffix = ".tif";
    if (name.size() <= prefix.size() + suffix.size()) return false;
    if (name.compare(0, prefix.size(), prefix) != 0) return false;
    if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    const std::string middle = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
    const std::string ownPid = std::to_string(CPLGetPID());
    if (middle == ownPid) return true;                                     // 旧形态（改前）
    if (middle.size() <= ownPid.size() + 1) return false;
    if (middle.compare(0, ownPid.size(), ownPid) != 0) return false;
    if (middle[ownPid.size()] != '_') return false;
    const std::string seq = middle.substr(ownPid.size() + 1);              // 新形态：<pid>_<seq>
    return seq.find_first_not_of("0123456789") == std::string::npos;
}

std::vector<fs::path> t17ScanWarpedLeftovers(const fs::path& ignore = fs::path()) {
    const std::string ignoreName = ignore.empty() ? std::string{} : ignore.filename().string();
    std::vector<fs::path> found;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(t17TempDir(), ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        if (t17IsOwnWarpedName(e.path().filename().string(), ignoreName)) found.push_back(e.path());
    }
    return found;
}

void t17ExpectNoWarpedLeftovers(const fs::path& ignore = fs::path()) {
    const auto leftovers = t17ScanWarpedLeftovers(ignore);
    std::string detail;
    for (const auto& p : leftovers) detail += utf8PathString(p) + "; ";
    EXPECT_TRUE(leftovers.empty()) << "临时 warped 栅格未被清理: " << detail;
}

// 收集 output 下除 metadata.json 之外的全部产物（相对路径，'/' 分隔）。
std::set<std::string> t17CollectTileFiles(const fs::path& outputDir) {
    std::set<std::string> files;
    if (!fs::exists(outputDir)) return files;
    for (auto it = fs::recursive_directory_iterator(outputDir);
         it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file()) continue;
        const std::string name = it->path().filename().string();
        if (name == "metadata.json") continue;
        files.insert(fs::relative(it->path(), outputDir).generic_string());
    }
    return files;
}

std::string t17ReadWholeFile(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

// 可编排取消的进度器：第 cancelAfter 次 onProgress 之后 isCancelled() 置真，
// 下一次 throwIfCancelled() 抛出。pre/post 实现的早期进度调用次序一致，
// 取 3 保证 warped 数据集已经建立（清理义务才有针对性）。
class T17CancellableProgress : public gis::core::ProgressReporter {
public:
    explicit T17CancellableProgress(int cancelAfter) : cancelAfter_(cancelAfter) {}
    void onProgress(double) override {
        ++calls_;
        if (cancelAfter_ >= 0 && calls_ >= cancelAfter_) cancelled_ = true;
    }
    void onMessage(const std::string&) override {}
    bool isCancelled() const override { return cancelled_; }
private:
    int calls_ = 0;
    int cancelAfter_ = -1;
    bool cancelled_ = false;
};

std::map<std::string, gis::framework::ParamValue> t17BaseTileParams(
    const std::string& actionInput, const std::string& outputDir) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["action"] = std::string("tile");
    params["input"] = actionInput;
    params["output"] = outputDir;
    params["profile"] = std::string("mercator");
    params["skip_blank"] = false;
    params["add_alpha"] = false;
    return params;
}

// B16（N36）夹具：EPSG:4326 的 0/250 棋盘格。
// WHY 不是 3857：3857→3857 的 warp 输出网格与源网格重合，`-r` 取什么都不改变像素值
//   （仓库里那些 3857 夹具对 `resampling` 赋值至今"看似生效"就是这个缘故）；
//   4326→3857 时 y 向被墨卡托压缩 1/cos(40°)≈1.3 倍 ⇒ 是真实重采样，两种插值取值才会不同。
// WHY 不是纯色：纯色下 nearest 与 average 结果必然相同，用例会假绿（任务书点名）。
std::string t36ResampleFixtureInput(const std::string& name) {
    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg4326;
    options.originX = 116.0; options.originY = 40.0; options.pixelSize = 0.001;
    std::vector<float> data(64 * 64, 0.0f);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            data[static_cast<size_t>(y) * 64 + x] = ((x + y) & 1) ? 250.0f : 0.0f;
    return gis::tests::writeFloatRaster(getTestDir(), name, 64, 64, data, options);
}

// 跑一次 cutting.tile（z=8 单瓦片）并返回"全部瓦片文件的相对路径 + 字节"的拼接。
// resample 为空串 ⇒ **不传**该键（用来钉缺省值）。故意把"有产物"也当判据：
// 两个空串相等会把"缺省值没变"这条断言变成恒真。
std::string t36RunTileCollectBytes(gis::framework::IGisPlugin* p, const std::string& input,
                                   const std::string& outName, const std::string& resample,
                                   gis::core::ProgressReporter& progress) {
    const fs::path output = getTestDir() / outName;
    fs::remove_all(output);
    auto params = t17BaseTileParams(input, utf8PathString(output));
    params["min_zoom"] = 8;
    params["max_zoom"] = 8;
    if (!resample.empty()) params["resample"] = resample;
    const auto result = p->execute(params, progress);
    EXPECT_TRUE(result.success) << outName << ": " << result.message;
    const auto files = t17CollectTileFiles(output);
    EXPECT_FALSE(files.empty()) << outName << " 一张瓦片都没产出";
    std::string bytes;
    for (const auto& f : files) {
        bytes += f;
        bytes += t17ReadWholeFile(output / fs::path(f));
    }
    EXPECT_FALSE(bytes.empty()) << outName << " 瓦片字节为空";
    return bytes;
}

// metadata.json 的 "bounds" 解析（修复轮 1 起新用例共用）。
// WHY 不复用 #297 里的那段 sscanf：它是既有锚（GOLD 冻结），把它的解析段抽走等于改动锚；
// 这里按同一手法（跳过 pos 处的起始引号后从 'b' 起匹配，格式里的空白符吸收冒号与逗号后的
// 空格）另写一份，只服务新增用例。
std::array<double, 4> t17MetadataBounds(const fs::path& outputDir) {
    const std::string json = t17ReadWholeFile(outputDir / "metadata.json");
    const auto pos = json.find("\"bounds\"");
    if (pos == std::string::npos) {
        ADD_FAILURE() << "metadata.json 里没有 bounds 字段: " << json;
        return {0.0, 0.0, 0.0, 0.0};
    }
    double b[4] = {};
    const int parsed = std::sscanf(json.c_str() + pos + 1, "bounds\": [%lf, %lf, %lf, %lf]",
                                   &b[0], &b[1], &b[2], &b[3]);
    if (parsed != 4) {
        ADD_FAILURE() << "bounds 解析失败（期望 4 个浮点数）: " << json;
        return {0.0, 0.0, 0.0, 0.0};
    }
    return {b[0], b[1], b[2], b[3]};
}

// 把产物集合拼成一行，失败时直接可读（新用例的 diagnostics）。
std::string t17JoinFiles(const std::set<std::string>& files) {
    std::string joined;
    for (const auto& f : files) joined += f + " ";
    return joined;
}

// ---------------------------------------------------------------------------
// Task 18 的夹具与读回资产
// ---------------------------------------------------------------------------

// 写"多波段 + 逐波段像元数据"的夹具栅格。
// WHY 不复用 gis::tests::writeConstantBandsRaster：它每波段只能写**一个常数**，而本任务的
//   两条判别用例都要"波段之间分布不同"（R-T18-3：常值三波段夹具下逐瓦片统计与整幅统计
//   产出逐像元相同，抓不到拉伸口径）与"某一个波段局部 NoData"（R-T18-9：add_alpha 的
//   true/false 只在多波段的局部空洞上才可区分，单波段下两者恒等 ⇒ 这正是 N65 藏了那么久的原因）。
// WHY 复用 createFixtureRaster + applyFixtureHeader：CRS / 仿射 / nodata 的口径与
//   writeFloatRaster、writeConstantBandsRaster 完全一致（R-T18-7 复用既有资产，不另起一套），
//   nodata 仍然按 options.hasNoData 声明到**全部**波段。
std::string t18WriteBandsRaster(const fs::path& dir, const std::string& fileName,
                                int width, int height,
                                const std::vector<std::vector<float>>& bandData,
                                const gis::tests::RasterFixtureOptions& options) {
    if (bandData.empty() || width <= 0 || height <= 0) {
        ADD_FAILURE() << "t18WriteBandsRaster: invalid request (" << width << "x" << height
                      << ", bands=" << bandData.size() << ")";
        return {};
    }
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t b = 0; b < bandData.size(); ++b) {
        if (bandData[b].size() < pixels) {
            ADD_FAILURE() << "t18WriteBandsRaster: band " << b + 1 << " has "
                          << bandData[b].size() << " pixels < " << pixels;
            return {};
        }
    }
    const auto path = (dir / fileName).string();
    auto ds = gis::tests::createFixtureRaster(path, width, height,
                                              static_cast<int>(bandData.size()), GDT_Float32,
                                              "t18WriteBandsRaster");
    if (!ds) return {};   // 失败已由 createFixtureRaster 报过（D2/D4 口径）
    gis::tests::applyFixtureHeader(ds.get(), options, width, height);
    for (size_t b = 0; b < bandData.size(); ++b) {
        GDALRasterBand* band = ds->GetRasterBand(static_cast<int>(b) + 1);
        if (!band) {
            ADD_FAILURE() << "t18WriteBandsRaster: band " << b + 1 << " is null for " << path;
            return {};
        }
        band->RasterIO(GF_Write, 0, 0, width, height,
                       const_cast<float*>(bandData[b].data()), width, height, GDT_Float32, 0, 0);
        band->FlushCache();
    }
    return path;
}

// 读回一张瓦片的全部波段（R-T18-7：一律走 gis::core::openRaster，禁用 cv::imread——
// gis_tests 只链 opencv_core+imgproc，imgcodecs 会 LNK2019）。
struct T18TileBands {
    int bands = 0;
    int width = 0;
    int height = 0;
    std::vector<std::vector<uchar>> data;   // data[i] == 第 i+1 波段，行主序 width 步长
    bool ok() const { return bands > 0 && width > 0 && height > 0; }
};

T18TileBands t18ReadTile(const fs::path& path) {
    T18TileBands out;
    auto ds = gis::core::openRaster(path.string(), true);
    if (!ds) {
        ADD_FAILURE() << "t18ReadTile: 打开瓦片失败 " << utf8PathString(path);
        return out;
    }
    out.bands = ds->GetRasterCount();
    out.width = ds->GetRasterXSize();
    out.height = ds->GetRasterYSize();
    out.data.clear();
    for (int b = 1; b <= out.bands; ++b) {
        std::vector<uchar> buf(static_cast<size_t>(out.width) * static_cast<size_t>(out.height));
        if (ds->GetRasterBand(b)->RasterIO(GF_Read, 0, 0, out.width, out.height, buf.data(),
                                           out.width, out.height, GDT_Byte, 0, 0) != CE_None) {
            ADD_FAILURE() << "t18ReadTile: 读波段失败 " << utf8PathString(path) << " band=" << b;
            return out;
        }
        out.data.push_back(std::move(buf));
    }
    return out;
}

// 不透明像元（最后一波段 alpha>0）上的统计量。
struct T18OpaqueStats {
    int count = 0;
    int minValue = 0;
    int maxValue = 0;
    double mean = 0.0;
};

// band 是 1-based 的 openRaster 波段号；alpha 取**最后一个**波段（本任务的容器口径：
// 输出恒为 CV_8UC4 ⇒ 读回恒为 4 波段，见 TileRendersThreeBandsAsColorNotGray 的头注释）。
T18OpaqueStats t18StatsOverOpaque(const T18TileBands& t, int band) {
    T18OpaqueStats s;
    if (!t.ok() || band < 1 || band > t.bands) return s;
    const std::vector<uchar>& target = t.data[static_cast<size_t>(band) - 1];
    const std::vector<uchar>& alpha = t.data[t.data.size() - 1];
    s.minValue = 255;
    s.maxValue = 0;
    double sum = 0.0;
    for (size_t i = 0; i < alpha.size(); ++i) {
        if (alpha[i] == 0) continue;
        const int v = target[i];
        sum += v;
        if (v < s.minValue) s.minValue = v;
        if (v > s.maxValue) s.maxValue = v;
        ++s.count;
    }
    if (s.count == 0) {
        s.minValue = 0;
        s.maxValue = 0;
    } else {
        s.mean = sum / s.count;
    }
    return s;
}

// 指定矩形窗口内的波段统计（不看 alpha）——给"没有 alpha 通道的输出格式"（JPEG）用，
// 也用来把统计限制在**已覆盖子矩形**内（z=8 该夹具的 dx1=105 由既有锚 #296 钉死）。
T18OpaqueStats t18StatsInRect(const T18TileBands& t, int band, int x0, int y0, int w, int h) {
    T18OpaqueStats s;
    if (!t.ok() || band < 1 || band > t.bands) return s;
    const std::vector<uchar>& target = t.data[static_cast<size_t>(band) - 1];
    s.minValue = 255;
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) {
            if (x < 0 || y < 0 || x >= t.width || y >= t.height) continue;
            const int v = target[static_cast<size_t>(y) * t.width + x];
            s.minValue = std::min(s.minValue, v);
            s.maxValue = std::max(s.maxValue, v);
            s.mean += v;
            ++s.count;
        }
    }
    if (s.count > 0) s.mean /= s.count;
    return s;
}

} // namespace

// F1 + R-T17-7/9 的主几何钉：z=10 与 z=11 的瓦片路径集合必须**恰等于**字面集合。
// 期望集合的字面出处（R-T17-9 的独立重算，与控制器一致）：
//   z=10 瓦片米宽 = 40075016.686/1024 = 39135.758 m；夹具 x∈[0,64000], y∈[-64000,0]
//   ⇒ 列 floor((20037508.342789+0)/39135.758)=512 到 floor((+64000)/39135.758)=513，
//   行同理 ⇒ 4 张；z=11 米宽减半 ⇒ {1024..1027}² 16 张。
// 边界 0 m 恰好落在 512/1024 的瓦片线上：浮点若把 floor 打成 511，该列与世界矩形
// 交集为空、不得写出文件——所以"恰等于"（而非"包含于"）仍然成立，这正是把越界
// 空瓦片也钉死的原因。
TEST_F(PluginTest, TileLevelGeometryUsesWebMercatorGrid) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    options.hasNoData = true;   // -9999 声明进栅格 ⇒ warp 后 hasNd=true，NoData 走透明通道
    std::vector<float> data(64 * 64, 0.0f);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 32; ++x) data[y * 64 + x] = 200.0f;   // 左半更亮（形态照抄计划，亮度断言留给 Task 18）
    for (int y = 56; y < 64; ++y)          // 南端 8 行（y∈[-64000,-56000]）铺 NoData：
        for (int x = 0; x < 64; ++x) data[y * 64 + x] = -9999.0f;
    for (int y = 0; y < 56; ++y)           // 东端 8 列（x∈[56000,64000]）铺 NoData：列向同款。
        for (int x = 56; x < 64; ++x) data[y * 64 + x] = -9999.0f;
    // WHY 加 NoData 带（行+列）：变异取证实测（M8：把 syTop 反号 ⇒ 行放错的那一轮），
    // 只钉"列覆盖 + 文件集合"时把 syTop 反号的**行放错**变异全绿——透明边界不含行信息；
    // 列向 (sx0 反号) 同族变异也需要覆盖缘的可断量。alpha 自此能区分
    // "源行/列被摆到瓦片哪些位置"：这是几何放置断言，不碰亮度/对比度
    // （后者是 Task 18 射程，R-T17-9/5 红线不动）。
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t17_geom_input.tif", 64, 64, data, options);
    const fs::path output = getTestDir() / "t17_geom_output";
    fs::remove_all(output);
    const std::string outputStr = utf8PathString(output);

    auto params = t17BaseTileParams(input, outputStr);
    params["min_zoom"] = 10;
    params["max_zoom"] = 11;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    const std::set<std::string> expectedZ10 = {
        "10/512/512.png", "10/512/513.png", "10/513/512.png", "10/513/513.png",
    };
    std::set<std::string> expectedZ11;
    for (int x = 1024; x <= 1026; ++x)
        for (int y = 1024; y <= 1026; ++y)
            expectedZ11.insert("11/" + std::to_string(x) + "/" + std::to_string(y) + ".png");
    // WHY 9 张而不是控制器 R-T17-9 表格里的 16 张：16 张的算术在原"全有数据"夹具上
    // 成立（本用例初版实测 20 张全写、集合恰等于）；NoData 带是我为杀死"行/列放置"
    // 变异加的（见上）。z=11 的列 1027（x∈[58704,78272]）与夹具的交集落在东端 NoData
    // 带 x∈[56000,64000] 内、行 1027 同理落南端带内 ⇒ 这些瓦片全透明 ⇒ 走既有的
    // "全 NoData 瓦片跳过"路径 ⇒ 9 张。若实现把源行/列窗放错（M8/M9 形态），放错的
    // 瓦片会读到有效区并写出 ⇒ "恰等于"立刻红。

    const auto produced = t17CollectTileFiles(output);
    std::string producedList;
    for (const auto& f : produced) producedList += f + " ";
    std::set<std::string> expectedAll = expectedZ10;
    expectedAll.insert(expectedZ11.begin(), expectedZ11.end());
    EXPECT_EQ(produced, expectedAll) << "z=10/11 的瓦片集合不等于字面期望 [" << producedList << "]";

    // 每张瓦片必须是 tileSize×tileSize（256），钉住"窗口裁剪没有把边界瓦片裁歪"。
    // WHY 读回用 GDAL 而不是 cv::imread：tests/CMakeLists.txt:43-44 的 gis_tests 只链
    // opencv_core/imgproc，imgcodecs 会 LNK2019（R-T17-9 第 1 点处置①，零新增依赖）。
    for (const auto& rel : expectedZ10) {
        auto ds = gis::core::openRaster((output / fs::path(rel)).string(), true);
        ASSERT_NE(ds.get(), nullptr) << rel;
        EXPECT_EQ(ds->GetRasterXSize(), 256) << rel;
        EXPECT_EQ(ds->GetRasterYSize(), 256) << rel;
    }

    // R-T17-7 第 3 点"未覆盖的边填 NoData/透明"的产物级断言：
    // 10/513/512.png 的覆盖止于 x=(64000-39135.758)/((78271.516-39135.758)/256)=162.64
    // ⇒ 第 163 列起（取 dx1=163）右侧必透明、左侧必不透明。alpha 在 band 4。
    {
        auto ds = gis::core::openRaster((output / fs::path("10/513/512.png")).string(), true);
        ASSERT_NE(ds.get(), nullptr);
        ASSERT_EQ(ds->GetRasterCount(), 4);
        std::vector<uchar> alpha(256 * 256);
        ASSERT_EQ(ds->GetRasterBand(4)->RasterIO(GF_Read, 0, 0, 256, 256,
                                                 alpha.data(), 256, 256, GDT_Byte, 0, 0), CE_None);
        // 覆盖区（左侧）内每一行都应有不透明像元；未覆盖区（右侧远端）应全透明；
        // 东端 NoData 带在瓦片 513 的列 110.3 起 ⇒ 列 130 对每一行都必须透明
        // （列放置变异 M9 的判别点：反号后 NoData 带会被挤到列 142.6 之后）。
        for (int row = 0; row < 256; ++row) {
            EXPECT_EQ(alpha[static_cast<size_t>(row) * 256 + 250], 0) << "右侧未覆盖仍不透明 row=" << row;
            EXPECT_GT(alpha[static_cast<size_t>(row) * 256 + 100], 0) << "左侧覆盖区被垫成透明 row=" << row;
            EXPECT_EQ(alpha[static_cast<size_t>(row) * 256 + 130], 0) << "东端 NoData 带列 130 不透明 row=" << row;
        }
    }

    // 行维度的同款钉：10/513/513.png 的 y 向内容止于 NoData 带上缘 y=-56000
    // ⇒ 瓦片行 (56000-39135.758)/152.874 = 110.3 起必透明；行 100（y≈-54423，
    // 有效行区）必不透明。只钉列覆盖时"源行 → 瓦片行"放错的变异实测全绿（M8），
    // 这一组断言就是它的判别力。
    {
        auto ds = gis::core::openRaster((output / fs::path("10/513/513.png")).string(), true);
        ASSERT_NE(ds.get(), nullptr);
        std::vector<uchar> alpha(256 * 256);
        ASSERT_EQ(ds->GetRasterBand(4)->RasterIO(GF_Read, 0, 0, 256, 256,
                                                 alpha.data(), 256, 256, GDT_Byte, 0, 0), CE_None);
        EXPECT_GT(alpha[100 * 256 + 10], 0) << "有效源行区被垫成透明";
        EXPECT_EQ(alpha[130 * 256 + 10], 0) << "NoData 带未走透明 ⇒ 行窗放错";
        EXPECT_EQ(alpha[200 * 256 + 10], 0) << "瓦片覆盖缘以下未保持透明";
        EXPECT_EQ(alpha[100 * 256 + 130], 0) << "东端 NoData 带未走透明 ⇒ 列窗放错";
    }
}

// F2/R-T17-8：metadata.json 的 bounds 必须是正确的 lon/lat 顺序（用户可见产物）。
// 夹具用 EPSG:3857 的矩形 [112°,113°]×[约39.2°,40°]（南边界由像素数决定，
// 北边界与两条经线是精确字面量）；断言形态按 R-T17-8"只断产物、不断进度行"。
TEST_F(PluginTest, TileMetadataBoundsKeepTraditionalLonLatOrder) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    const double kHalf = 20037508.342789244;   // x = lon*kHalf/180（输入侧公式，独立于插件的 OGR 变换）
    const double R = 6378137.0;
    auto mercY = [R](double latDeg) {
        const double lat = latDeg * M_PI / 180.0;
        return R * std::log(std::tan(M_PI / 4.0 + lat / 2.0));
    };

    const double westX = 112.0 * kHalf / 180.0;
    const double northY = mercY(40.0);
    const double pixelSize = (kHalf / 180.0) / 64.0;   // 64 像元 = 1° 经度跨度

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = westX; options.originY = northY; options.pixelSize = pixelSize;
    std::vector<float> data(64 * 64, 10.0f);
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t17_bounds_input.tif", 64, 64, data, options);
    const fs::path output = getTestDir() / "t17_bounds_output";
    fs::remove_all(output);

    auto params = t17BaseTileParams(input, utf8PathString(output));
    params["min_zoom"] = 8;
    params["max_zoom"] = 8;
    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    const std::string json = t17ReadWholeFile(output / "metadata.json");
    ASSERT_FALSE(json.empty());
    const auto pos = json.find("\"bounds\"");
    ASSERT_NE(pos, std::string::npos) << json;
    double b[4] = {};
    // writeMetadataJson 输出形如 "bounds": [a, b, c, d]：跳过 pos 处的起始引号后从
    // 'b' 起匹配；冒号与逗号后的空格由格式中的空白符吸收（sscanf 空白匹配任意个空白）。
    const int parsed = std::sscanf(json.c_str() + pos + 1, "bounds\": [%lf, %lf, %lf, %lf]",
                                   &b[0], &b[1], &b[2], &b[3]);
    ASSERT_EQ(parsed, 4) << json;

    // lon 边是精确字面量（EPSG:3857 x 与经度线性），lat 上界同理；南界只断区间。
    EXPECT_NEAR(b[0], 112.0, 1e-4) << json;
    EXPECT_NEAR(b[2], 113.0, 1e-4) << json;
    EXPECT_NEAR(b[3], 40.0, 1e-4) << json;
    EXPECT_GT(b[1], 38.0);
    EXPECT_LT(b[1], b[3]);
    EXPECT_LE(std::abs(b[1]), 85.06) << "lat 越界 ⇒ 轴序又错了: " << json;
    EXPECT_LE(std::abs(b[3]), 85.06) << json;
    EXPECT_LT(b[0], b[2]);
}

// R-T17-1 第 3 点：geodetic/raster 必须显式失败且 message 点名该 profile 值。
TEST_F(PluginTest, TileRejectsGeodeticProfileAndNamesIt) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);
    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    std::vector<float> data(8 * 8, 1.0f);
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t17_profile_geodetic_input.tif", 8, 8, data, options);

    auto params = t17BaseTileParams(input, utf8PathString(getTestDir() / "t17_profile_geodetic_output"));
    params["profile"] = std::string("geodetic");
    params["min_zoom"] = 8;
    params["max_zoom"] = 8;
    // 21-FixA part2 I-3：判据由"含子串"升级为"含字面整句 + 不含本用例的字面输入文件名"。
    // 子串 "geodetic" 自己不够：profile 的取值本身就叫 geodetic，任何把这个取值回显进 message 的
    // 失败（打不开输入、层级非法…）都能让它绿 ⇒ 字面整句才钉住"就是这条 profile 拒绝"。
    // 反向那条钉"不是打不开输入"：core 的打不开消息形如 Cannot open raster: <路径> (<CPLError>)
    // （src/core/src/gdal_wrapper.cpp:50），必然带上本用例的输入文件名 ⇒ 原因一换立刻红。
    // 本例与 TileRejectsRasterProfileAndNamesIt 原先共用同名夹具 t17_profile_input.tif（并发下
    // 彼此 trunc 对方的夹具），现在各用各的字面文件名，两条用例的夹具互不为对方所动。
    ASSERT_TRUE(fs::exists(fs::path(input)))
        << "夹具没落盘：这条用例的失败原因就已经不可能是 profile 被拒";
    const auto result = p->execute(params, progress_);
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.message.find("tile profile \"geodetic\" is not supported"), std::string::npos)
        << result.message;
    EXPECT_EQ(result.message.find("t17_profile_geodetic_input.tif"), std::string::npos)
        << "失败消息点名了本用例的输入文件 ⇒ 原因是打不开输入而不是 profile 被拒：" << result.message;
}

TEST_F(PluginTest, TileRejectsRasterProfileAndNamesIt) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);
    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    std::vector<float> data(8 * 8, 1.0f);
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t17_profile_raster_input.tif", 8, 8, data, options);

    auto params = t17BaseTileParams(input, utf8PathString(getTestDir() / "t17_profile_raster_output"));
    params["profile"] = std::string("raster");
    params["min_zoom"] = 8;
    params["max_zoom"] = 8;
    // 21-FixA part2 I-3：判据由"含子串"升级为"含字面整句 + 不含本用例的字面输入文件名"。
    // 子串 "raster" 自己不够：profile 的取值本身就叫 raster，任何把这个取值回显进 message 的
    // 失败（打不开输入、层级非法…）都能让它绿 ⇒ 字面整句才钉住"就是这条 profile 拒绝"。
    // 反向那条钉"不是打不开输入"：core 的打不开消息形如 Cannot open raster: <路径> (<CPLError>)
    // （src/core/src/gdal_wrapper.cpp:50），必然带上本用例的输入文件名 ⇒ 原因一换立刻红。
    // 本例与 TileRejectsGeodeticProfileAndNamesIt 原先共用同名夹具 t17_profile_input.tif（并发下
    // 彼此 trunc 对方的夹具），现在各用各的字面文件名，两条用例的夹具互不为对方所动。
    ASSERT_TRUE(fs::exists(fs::path(input)))
        << "夹具没落盘：这条用例的失败原因就已经不可能是 profile 被拒";
    const auto result = p->execute(params, progress_);
    ASSERT_FALSE(result.success);
    EXPECT_NE(result.message.find("tile profile \"raster\" is not supported"), std::string::npos)
        << result.message;
    EXPECT_EQ(result.message.find("t17_profile_raster_input.tif"), std::string::npos)
        << "失败消息点名了本用例的输入文件 ⇒ 原因是打不开输入而不是 profile 被拒：" << result.message;
}

// R-T17-11 处置①：overview_resampling 必须真的驱动逐瓦片缩放插值。
// 判据形态："同一输入两种插值产出不同字节且都成功" ⇒ 把映射删掉（硬回 INTER_LINEAR）
// 两次输出必然一致 ⇒ 必红。取 z=8/8 单瓦片：源 64×64@1000m 落进 256px 瓦片只需
// ~105px 覆盖 ⇒ 缩放真实发生（nearest 保持 0/255 二值、average 出过渡灰）。
TEST_F(PluginTest, TileOverviewResamplingDrivesOutputBytes) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);
    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    std::vector<float> data(64 * 64, 0.0f);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 32; ++x) data[y * 64 + x] = 200.0f;
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t17_ovres_input.tif", 64, 64, data, options);

    std::string nearestBytes, averageBytes;
    for (int pass = 0; pass < 2; ++pass) {
        const std::string method = pass == 0 ? "nearest" : "average";
        const fs::path output = getTestDir() /
            (pass == 0 ? "t17_ovres_nearest_output" : "t17_ovres_average_output");
        fs::remove_all(output);
        auto params = t17BaseTileParams(input, utf8PathString(output));
        params["min_zoom"] = 8;
        params["max_zoom"] = 8;
        params["overview_resampling"] = method;
        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << method << ": " << result.message;
        const auto files = t17CollectTileFiles(output);
        ASSERT_EQ(files.size(), 1u) << method << " 产出集合应为单瓦片";
        const std::string bytes = t17ReadWholeFile(output / fs::path(*files.begin()));
        ASSERT_FALSE(bytes.empty());
        // 覆盖缘透明也要钉（本夹具**没有**声明 NoData ⇒ hasNd=false，垫边的 -9999 只能
        // 靠 coverMask 走透明；rgba 循环里删掉该臂 ⇒ 边缘变不透明黑 ⇒ 这里红。
        // z=8 瓦片米宽 156543，夹具只覆盖左上 (64000/156543)*256 ≈ 104.7 px）：
        {
            auto ds = gis::core::openRaster((output / fs::path(*files.begin())).string(), true);
            ASSERT_NE(ds.get(), nullptr);
            ASSERT_EQ(ds->GetRasterCount(), 4);
            std::vector<uchar> alpha(256 * 256);
            ASSERT_EQ(ds->GetRasterBand(4)->RasterIO(GF_Read, 0, 0, 256, 256,
                                                     alpha.data(), 256, 256, GDT_Byte, 0, 0), CE_None);
            EXPECT_GT(alpha[50 * 256 + 50], 0) << method << " 覆盖区被垫成透明";
            EXPECT_EQ(alpha[200 * 256 + 200], 0) << method << " 未覆盖边缘仍不透明（coverMask 失效）";
            // F5（修复轮 1，审查 m-5）：把"子像素放置"从**字节巧合**升级成**放置钉**。
            // 覆盖缘的字面算术（按 EPSG:3857 定义独立算，不调被测函数）：
            //   z=8 单瓦片米宽 = 2·20037508.342789244/256 = 156543.0339；夹具 x∈[0,64000]、y∈[-64000,0]
            //   ⇒ 覆盖比例 64000/156543.0339 = 0.4088332671… ⇒ dx1 = ceil(0.4088332671·256) = ceil(104.6613) = 105
            //   ⇒ 不透明列只能是 [0,104]，列 105 起必须透明；y 向 tileResY 同值 ⇒ 行同款。
            // WHY 要钉具体列/行：审查者实测 ceil→floor 的变异当时只有 #300 红，而红的机理是
            // nearest 与 average 两次输出**字节恰好撞成一致**才触发 EXPECT_NE（R3a 那一轮：ceil→floor，
            // 14 条聚焦里只有 #300 一条红），#296 对 1px 放置完全不敏感 ⇒ 偶发红、机理脆弱。
            // 现在缘列 104/105 直接进断言，floor 把缘缩到 103 就立刻红，与插值组合无关
            // （这 4 行在 nearest 与 average 两个 pass 里各跑一遍，就是"与插值无关"的自证）。
            EXPECT_GT(alpha[50 * 256 + 104], 0) << method << " 覆盖缘列 104 应不透明（dx1=ceil(104.66)=105）";
            EXPECT_EQ(alpha[50 * 256 + 105], 0) << method << " 覆盖缘外一列 105 应透明";
            EXPECT_GT(alpha[104 * 256 + 50], 0) << method << " 覆盖缘行 104 应不透明（dy1=ceil(104.66)=105）";
            EXPECT_EQ(alpha[105 * 256 + 50], 0) << method << " 覆盖缘外一行 105 应透明";
        }
        if (pass == 0) nearestBytes = bytes; else averageBytes = bytes;
    }
    EXPECT_NE(nearestBytes, averageBytes)
        << "nearest 与 average 产出字节一致 ⇒ overview_resampling 没有驱动瓦片缩放（R-T17-11①）";
}

// B16（N36）：doTile 原先读的是**未声明**的 "resampling"，paramSpecs 声明的 "resample"
// 对它完全无效（用户传 --resample= 既无效也无报错；改前实测过：传 --resample= 既无效也无报错）。
// 改读 "resample" 之后，这个键必须真的驱动 warp 的 -r。
// 夹具用 t36ResampleFixtureInput（4326 棋盘格）：3857→3857 的重合网格下 -r 不改变像素值，
// 用 3857 夹具这条会恒绿。
TEST_F(PluginTest, TileResampleDrivesOutputBytes) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);
    const std::string input = t36ResampleFixtureInput("t36_resample_switch_input.tif");
    const std::string nearestBytes = t36RunTileCollectBytes(
        p, input, "t36_resample_nearest_output", "nearest", progress_);
    const std::string averageBytes = t36RunTileCollectBytes(
        p, input, "t36_resample_average_output", "average", progress_);
    ASSERT_FALSE(nearestBytes.empty());
    ASSERT_FALSE(averageBytes.empty());
    EXPECT_NE(nearestBytes, averageBytes)
        << "resample=nearest 与 average 的瓦片逐字节相同 ⇒ doTile 没在读声明的 resample"
           "（修前形态就是读死键 resampling，N36）";
}

// B16（N36）第二腿：缺省值必须**仍是** "average"，即不传 resample 的产物与显式
// resample=average 逐字节相同。参数面声明的是 default=nearest（:paramSpecs 的 resample），
// 这处"实际缺省 average ≠ spec 声明 nearest"是**故意**的（保持所有既有 tile 产物不变）⇒
// 一旦有人把 doTile 的内联默认改成 nearest，本用例立刻红。
TEST_F(PluginTest, TileOmittedResampleIsByteIdenticalToExplicitAverage) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);
    const std::string input = t36ResampleFixtureInput("t36_resample_default_input.tif");
    const std::string omittedBytes = t36RunTileCollectBytes(
        p, input, "t36_resample_omitted_output", "", progress_);
    const std::string explicitBytes = t36RunTileCollectBytes(
        p, input, "t36_resample_explicit_average_output", "average", progress_);
    ASSERT_FALSE(omittedBytes.empty());
    ASSERT_FALSE(explicitBytes.empty());
    EXPECT_EQ(omittedBytes, explicitBytes)
        << "不传 resample 与显式 average 的瓦片不同 ⇒ doTile 的缺省值被改成了别的"
           "（spec 的 nearest？那会一次性换掉所有既有 tile 产物的插值）";
}

// R-T17-11①：cv::resize 无对应项的取值必须显式失败并点名取值，不许静默降级。
TEST_F(PluginTest, TileRejectsOverviewResamplingWithoutResizeEquivalent) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);
    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    std::vector<float> data(8 * 8, 1.0f);
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t17_ovbad_input.tif", 8, 8, data, options);

    for (const std::string bad : {"cubicspline", "lanczos", "mode"}) {
        auto params = t17BaseTileParams(
            input, utf8PathString(getTestDir() / ("t17_ovbad_" + bad + "_output")));
        params["min_zoom"] = 8;
        params["max_zoom"] = 8;
        params["overview_resampling"] = bad;
        const auto result = p->execute(params, progress_);
        ASSERT_FALSE(result.success) << bad << " 不该被静默接受";
        EXPECT_NE(result.message.find(bad), std::string::npos) << bad << " 的失败必须点名取值: " << result.message;
    }
}

// R-T17-7 第 3 点：旋转/斜切源栅格不许静默近似，显式失败。
// （几何反算只支持正北像元网格；见 cutting doTile 的 north-up 守卫。）
// WHY 四个变体逐个钉守卫的四个臂：单跑 [2]/[4] 同时非零的夹具时，删掉守卫任意一个
// || 臂都被另一个臂兜住 ⇒ 变异全绿（M5a 实测）；每个臂必须有独立夹具变体才有判别力。
TEST_F(PluginTest, TileFailsOnRotatedSourceRaster) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    struct Variant { double gt[6]; };
    // 基准 gt：origin(0,0)、1000m/px、正北；每个变体只破坏一个臂。
    const std::vector<Variant> variants = {
        { {0.0, 1000.0, 100.0, 0.0, 0.0, -1000.0} },    // gt[2] 旋转项非零
        { {0.0, 1000.0, 0.0, 0.0, 50.0, -1000.0} },     // gt[4] 斜切项非零
        { {64000.0, -1000.0, 0.0, 0.0, 0.0, -1000.0} }, // gt[1] ≤ 0（x 轴反向）
        { {0.0, 1000.0, 0.0, -64000.0, 0.0, 1000.0} },  // gt[5] ≥ 0（y 轴正向）
    };

    for (size_t i = 0; i < variants.size(); ++i) {
        gis::tests::RasterFixtureOptions options;
        options.crs = gis::tests::FixtureCrs::Epsg3857;
        options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
        std::vector<float> data(16 * 16, 5.0f);
        const std::string input = gis::tests::writeFloatRaster(
            getTestDir(), "t17_rot_input_" + std::to_string(i) + ".tif", 16, 16, data, options);

        {   // 把夹具改成对应"非正北"变体。
            auto ds = gis::core::openRaster(input, false);
            ASSERT_NE(ds.get(), nullptr);
            ASSERT_EQ(ds->SetGeoTransform(const_cast<double*>(variants[i].gt)), CE_None);
            ds->FlushCache();
        }

        auto params = t17BaseTileParams(input,
            utf8PathString(getTestDir() / ("t17_rot_output_" + std::to_string(i))));
        params["min_zoom"] = 8;
        params["max_zoom"] = 8;
        const auto result = p->execute(params, progress_);
        ASSERT_FALSE(result.success) << "变体 #" << i << " 不该被静默接受";
        EXPECT_NE(result.message.find("north-up"), std::string::npos)
            << "变体 #" << i << ": " << result.message;
    }
}

// R-T17-4：throwIfCancelled 提前返回路径必须清理 warped 临时栅格（前缀扫描兜住
// 新旧两种命名：修正前泄漏的无 pid 名也会被这里钉住）。
TEST_F(PluginTest, TileCleansWarpedTempOnCancel) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);
    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    std::vector<float> data(64 * 64, 1.0f);
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t17_cancel_input.tif", 64, 64, data, options);
    const fs::path output = getTestDir() / "t17_cancel_output";
    fs::remove_all(output);

    t17ExpectNoWarpedLeftovers();   // 前提自证：进场时系统临时目录没有残留

    auto params = t17BaseTileParams(input, utf8PathString(output));
    params["min_zoom"] = 8;
    params["max_zoom"] = 8;
    T17CancellableProgress progress(3);   // 第 3 次进度回调后进入已取消态
    EXPECT_THROW(p->execute(params, progress), gis::core::CancelledException);
    t17ExpectNoWarpedLeftovers();
}

// R-T17-4：Result::fail 提前返回路径（reprojection 失败，输入无 CRS）同样不许泄漏。
TEST_F(PluginTest, TileCleansWarpedTempOnWarpFailure) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);
    gis::tests::RasterFixtureOptions options;   // crs=None ⇒ -t_srs EPSG:3857 必然失败
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    std::vector<float> data(8 * 8, 1.0f);
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t17_nocrs_input.tif", 8, 8, data, options);
    const fs::path output = getTestDir() / "t17_nocrs_output";
    fs::remove_all(output);

    auto params = t17BaseTileParams(input, utf8PathString(output));
    params["min_zoom"] = 8;
    params["max_zoom"] = 8;
    const auto result = p->execute(params, progress_);
    EXPECT_FALSE(result.success);
    t17ExpectNoWarpedLeftovers();
}

// Task B6 / m-4：warped 临时栅格名必须**每次调用**唯一，不能只带 pid。
// 缺陷形态（改前的 cutting_plugin.cpp）：名字 = gis_tile_warped_<CPLGetPID()>.tif ⇒
//   ① 同进程内两次 tile（并发或先后）写的是同一个路径；
//   ② 崩溃后 pid 被复用 ⇒ 新进程直接落在上一个进程残件的名字上（覆盖它，或读到它的残体）。
// 判据（形状照 #315/#316：只看盘上事实，不读产品代码里的计数器）：
//   在**旧名字那一格**种一个哨兵文件，然后连跑两次 tile。
//     改前 ⇒ 调用按旧名创建 warped：GDALWarp 覆盖写把哨兵字节换掉，收尾时
//            WarpedRasterCloser 又把该路径删掉 ⇒ 哨兵**消失** ⇒ 下面的断言红；
//     改后 ⇒ 每次调用用的是 gis_tile_warped_<pid>_<seq>.tif，与哨兵那一格不同名字 ⇒
//            哨兵两次都原样留存（字节没变、文件还在），且临时目录里没有本进程的残留。
//   哨兵必须用**显式 ignore** 交给残留扫描：它本身就是 gis_tile_warped_<pid>.tif 形态的
//   普通文件，不排除会让"无残留"这条断言在本用例里永远红（假红会掩盖真缺陷）。
// 如实登记的判据边界：这条判据钉住的是"不再复用 pid-only 那一格"，两次调用**互相**是否
//   不同名靠的是产品侧那个进程静态原子序号（fetch_add，见 cutting_plugin.cpp 拼名字那行）。
//   想从测试侧直接观察第二次调用的名字，就得在临时目录里预种一片 <pid>_<seq> 候选名再数
//   被吃掉哪一格 —— 本进程内 tile 调用总数随测试增减，候选窗口大小没有上界：窗口不够就假红，
//   窗口够大就要在共享临时目录里种上百个本进程形态的文件，别的 tile 用例的残留扫描会先把
//   它们当成泄漏（跨用例污染）。权衡后不做，把这条边界写在报告与这里，而不是配一条恒真断言。
TEST_F(PluginTest, TileWarpedTempNameIsUniquePerCall) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);
    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    std::vector<float> data(64 * 64, 1.0f);
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "b6_unique_input.tif", 64, 64, data, options);

    // 哨兵种在**旧命名**那一格（m-4 要拆掉的正是"每次都算出同一个名字"）。
    const fs::path sentinel =
        t17TempDir() / ("gis_tile_warped_" + std::to_string(CPLGetPID()) + ".tif");
    const std::string sentinelBytes = "B6-M4-SENTINEL-NOT-A-TIFF";
    t17ExpectNoWarpedLeftovers();          // 前提自证：进场时本进程没有残留
    std::error_code sec;
    fs::remove(sentinel, sec);             // 上一轮崩溃可能留下的同名文件
    {
        std::ofstream ofs(sentinel, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(ofs)) << "哨兵写不进去: " << utf8PathString(sentinel);
        ofs << sentinelBytes;
    }
    ASSERT_TRUE(fs::exists(sentinel));

    for (const char* outputTag : { "b6_unique_output_1", "b6_unique_output_2" }) {
        const fs::path output = getTestDir() / outputTag;
        fs::remove_all(output);
        auto params = t17BaseTileParams(input, utf8PathString(output));
        params["min_zoom"] = 8;
        params["max_zoom"] = 8;
        const auto result = p->execute(params, progress_);
        EXPECT_TRUE(result.success) << outputTag << " 这次 tile 本该成功: " << result.message;
        // 核心断言：这一次调用**没有**占用 pid-only 那一格。
        std::string now;
        {
            std::ifstream ifs(sentinel, std::ios::binary);
            now.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        }
        EXPECT_TRUE(fs::exists(sentinel))
            << outputTag << ": 临时栅格仍按 pid-only 命名，同进程两次调用互踩（m-4）";
        EXPECT_EQ(now, sentinelBytes) << outputTag << ": 哨兵字节被改写 ⇒ 本次调用复用了那一格";
    }

    // 收尾：哨兵不是产品产物，先删再判残留，否则"无残留"这条断言会因为哨兵自己变红。
    fs::remove(sentinel, sec);
    // 探针自证（guards the guard）：新命名形态的残留必须**扫得出来**，否则 #315/#316 这两条
    //   清理判据会在"产品侧改名"之后悄悄变成恒真（扫描器只认旧名字 ⇒ 永远扫不到东西）。
    //   这里手工造一个 <pid>_<seq> 形态的文件让扫描器认，认不出就红——它证明的是判据本身有效。
    const fs::path probe = t17TempDir() /
        ("gis_tile_warped_" + std::to_string(CPLGetPID()) + "_987654321.tif");
    {
        std::ofstream ofs(probe, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(ofs)) << "探针文件写不进去: " << utf8PathString(probe);
        ofs << "probe";
    }
    EXPECT_EQ(t17ScanWarpedLeftovers().size(), 1u)
        << "扫描器认不出 gis_tile_warped_<pid>_<seq>.tif 这一格 ⇒ 清理判据对新命名失效";
    // 同一 pid 前缀但**不是**本进程那一格的名字不得被认进来（F3 收窄口径的另一半）。
    const fs::path otherProc = t17TempDir() /
        ("gis_tile_warped_" + std::to_string(CPLGetPID()) + "x.tif");
    {
        std::ofstream ofs(otherProc, std::ios::binary | std::ios::trunc);
        if (ofs) ofs << "other";
    }
    EXPECT_EQ(t17ScanWarpedLeftovers().size(), 1u)
        << "扫描器把 <pid>x 这种非本进程/非本形态的名字也算进来了（假红的来源）";
    fs::remove(probe, sec);
    fs::remove(otherProc, sec);
    t17ExpectNoWarpedLeftovers();
}

// 全局闸门 (ii)：tile 自己的面板锁（形状照抄 TerrainFillSinksGuiPanel…，R-T17-11
// ①/② 都要求的第 (ii) 件事）。overview_resampling 经处置①成为"真被读"的键，必须
// 在渲染集合里；tile 不读的键（resample/num_threads/tile_size/overlap/extent/
// cutline/bands/dst_srs）必须被隐藏。渲染判据用 ParamCardWidget::hasParam(entries_)，
// 不用 ParamWidget::hasParam（它只扫 specs_，等于把有效集合再抄一遍）。
TEST_F(PluginTest, TileGuiPanelHidesParametersTheTileActionNeverReads) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);
    const auto specs = p->paramSpecs();
    const auto* config = gis::gui::findActionUiConfig("cutting", "tile");
    ASSERT_NE(config, nullptr) << "cutting.tile 缺少 actionUiConfig 条目";

    const auto effective = gis::gui::buildEffectiveGuiParamSpecs(
        "cutting", "tile", specs, config->visibleKeys, config->requiredKeys);
    std::set<std::string> effectiveKeys;
    for (const auto& spec : effective) effectiveKeys.insert(spec.key);

    EXPECT_EQ(effectiveKeys, config->visibleKeys) << "cutting.tile 的有效键集与白名单不一致";
    EXPECT_GT(effectiveKeys.count("overview_resampling"), 0u)
        << "overview_resampling 必须被渲染（R-T17-11① 之后 doTile 真的读它）";
    // 两类「不在面板里」的键要分开钉，因为理由不同；混在一堆里会让红字说假话。
    for (const char* deadKey : {"num_threads", "tile_size", "overlap",
                                "extent", "cutline", "bands", "dst_srs"}) {
        EXPECT_EQ(effectiveKeys.count(deadKey), 0u)
            << deadKey << " 不被 doTile 读取，不得出现在 tile 面板";
    }
    // resample 是另一类：Task B16 之后 doTile 真的读它（cutting_plugin.cpp 里
    // getParam(params, "resample", "average")），它留在面板外是**有意藏键**，理由是
    // GUI 白名单会实发 spec 默认值（R-B20）——显示它 ⇒ 用户没改过就提交也会把缺省插值
    // 从 average 换成 nearest ⇒ 既有产物静默改变。所以这条断言钉的是「有意不显示」，
    // 不是「没被读」。
    EXPECT_EQ(config->visibleKeys.count("resample"), 0u)
        << "resample 被 doTile 读取，但必须不出现在面板：显示它会让未改动提交实发 spec 默认"
        << " nearest，从而改变既有默认产物（不是因为它没被读）";
    EXPECT_EQ(config->visibleKeys.count("action"), 0u);

    ParamWidget panel;
    panel.setUiContext("cutting", "tile");
    panel.setParamSpecs(effective);
    const auto cards = panel.findChildren<ParamCardWidget*>();
    ASSERT_FALSE(cards.empty()) << "cutting.tile 没有生成任何参数卡片";
    std::set<std::string> renderedKeys;
    for (const auto& spec : specs) {
        if (spec.key == "action") continue;
        for (const auto* card : cards) {
            if (card && card->hasParam(spec.key)) renderedKeys.insert(spec.key);
        }
    }
    EXPECT_EQ(renderedKeys, effectiveKeys) << "cutting.tile 的 widget 树与有效 spec 不一致";
    EXPECT_GT(renderedKeys.count("overview_resampling"), 0u);
    EXPECT_EQ(renderedKeys.count("resample"), 0u);
}

// F1（Task 17 修复轮 1，审查发现 I-2）：tile_extent 前向投影路径 + 两道零面积守卫的判别用例。
// 修复前的盘上事实（本代理现跑）：
//   grep -rn "tile_extent" tests/ ⇒ exit=1（零命中）⇒ hasUserExtent 那条分支
//   （cutting_plugin.cpp 改前形态 = 10829d9:897-901 读 extent、10829d9:945-947 走 lonLatToMercatorXY 前向投影）
//   自 Task 17 写下后**没有任何用例执行过**；
//   审查者实测把守卫①（236327d:1061 `if (c1 <= c0 || r1 <= r0)`）或守卫②（236327d:1078
//   `if (dx1 <= dx0 || dy1 <= dy0)`）各自改成 `if (false)` ⇒ 14 条聚焦全绿（R7 零覆盖守卫、R8 亚像素守卫
//   两轮各测一次，均 0 红）。
// 本用例把"用户矩形"作为唯一的瓦片范围来源（不再由数据包络推导），钉三件事：
//   (a) 与源包络零交集的瓦片一张都不写——期望集合**恰等于**字面集合（不用"少于 N"的弱断言）；
//   (b) 走 extent 前向投影那条路之后，metadata.json 的 bounds 仍是传统 (lon, lat) 顺序，
//       期望值写死字面量（不调用任何被测函数，避免同义反复）；
//   (c) 夹具的候选里同时含"只在 x 向零交集"（列 536/540）与"只在 y 向零交集"（行 296/301）的档，
//       于是守卫①的两个 `||` 臂各自都被真实执行到；取证结论（字面/提变量两形态 + 同删对照）如实登记为：
//       **同时**删掉两道守卫才红，单删任一道 14 条聚焦仍全绿（两道在可达域内互相蕴含）⇒ 属防御臂冗余，
//       不追打成测试缺口。
// 期望集合的字面算术（独立按 EPSG:3857 定义算：x=lon·πR/180、y=R·ln tan(45°+φ/2)、
// z=10 单瓦片米宽 = 2·20037508.342789244/1024 = 39135.7584820098，未调用 gis::core::*）：
//   源包络 x∈[1000000,1072000]、y∈[8272000,8400000]
//   ⇒ 与之相交的列 = {537,538,539}（536=[939258,978394] 差 21.6 km、540=[1095801,1134937] 差 23.8 km）
//   ⇒ 与之相交的行 = {297,298,299,300}（296 上缘 8414188 > 数据北缘 8400000 ⇒ 零交集；
//      301=[8218509,8257645] 与数据南缘 8272000 差 14.4 km）
//   extent lon∈[8.5,9.9]→x∈[946216,1102063] ⇒ 候选列 {536..540}；lat∈[59.3,60.1]→y∈[8245513,8422036]
//   ⇒ 候选行 {296..301} ⇒ 30 张候选、12 张写、18 张必须被零面积守卫跳过。
// WHY 夹具放在纬度 60°：赤道上 x=lon·πR/180 与 y≈lat·πR/180 到 1e-4 相对量级相同，
// "把经纬度调换/把投影实参调换"的变异在度量面上几乎不变；高纬度 merc(60°)=8399738 m 而
// 60° 经度只有 6679169 m ⇒ 调换后候选整体挪到列 680..682 / 行 483..487 ⇒ 期望集合必红。
TEST_F(PluginTest, TileUserExtentDrivesMetricProjectionAndSkipsZeroAreaTiles) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 1000000.0; options.originY = 8400000.0; options.pixelSize = 1000.0;
    std::vector<float> data(static_cast<size_t>(72) * 128, 0.0f);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 32; ++x) data[static_cast<size_t>(y) * 72 + x] = 200.0f;
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t17_extent_input.tif", 72, 128, data, options);

    // 档 A：用户矩形整体落在源包络之外（南半球 lon∈[100,101]、lat∈[-40,-39]，z=8）。
    // 候选 = 列 {199} × 行 {158,159} 共 2 张，全部与源包络零交集 ⇒ 一张都不许写，
    // 但整体仍 Success 且 metadata.json 照写（"零交集不产文件、不失败"的既端口径）。
    {
        const fs::path output = getTestDir() / "t17_extent_far_output";
        fs::remove_all(output);
        auto params = t17BaseTileParams(input, utf8PathString(output));
        params["min_zoom"] = 8;
        params["max_zoom"] = 8;
        params["tile_extent"] = std::array<double, 4>{100.0, -40.0, 101.0, -39.0};

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << result.message;

        const auto produced = t17CollectTileFiles(output);
        EXPECT_EQ(produced, std::set<std::string>{})
            << "tile_extent 与源包络零交集时不得写出任何瓦片，实际 [" << t17JoinFiles(produced) << "]";
        EXPECT_TRUE(fs::exists(output / "metadata.json")) << "零瓦片也要写出 metadata.json";

        const auto bounds = t17MetadataBounds(output);
        EXPECT_NEAR(bounds[0], 100.0, 1e-6) << "bounds[0] 必须是用户给的 minLon";
        EXPECT_NEAR(bounds[1], -40.0, 1e-6) << "bounds[1] 必须是用户给的 minLat";
        EXPECT_NEAR(bounds[2], 101.0, 1e-6) << "bounds[2] 必须是用户给的 maxLon";
        EXPECT_NEAR(bounds[3], -39.0, 1e-6) << "bounds[3] 必须是用户给的 maxLat";
    }

    // 档 B：extent 罩住源包络（高纬度，不对称的经纬度区间），候选 30 张、期望恰好 12 张。
    // "恰等于"同时钉住两头：写出的 12 张一张不能少（前向投影算错/轴序错 ⇒ 整块挪走 ⇒ 红），
    // 18 张零交集候选一张不能多（守卫错 ⇒ 负宽 cv::Mat 或错位置瓦片 ⇒ 红）。
    {
        const fs::path output = getTestDir() / "t17_extent_highlat_output";
        fs::remove_all(output);
        auto params = t17BaseTileParams(input, utf8PathString(output));
        params["min_zoom"] = 10;
        params["max_zoom"] = 10;
        params["tile_extent"] = std::array<double, 4>{8.5, 59.3, 9.9, 60.1};

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << result.message;

        const std::set<std::string> expected = {
            "10/537/297.png", "10/537/298.png", "10/537/299.png", "10/537/300.png",
            "10/538/297.png", "10/538/298.png", "10/538/299.png", "10/538/300.png",
            "10/539/297.png", "10/539/298.png", "10/539/299.png", "10/539/300.png",
        };
        const auto produced = t17CollectTileFiles(output);
        EXPECT_EQ(produced, expected)
            << "z=10 高纬度 extent 的瓦片集合不等于字面期望，实际 [" << t17JoinFiles(produced) << "]";

        const auto bounds = t17MetadataBounds(output);
        EXPECT_NEAR(bounds[0], 8.5, 1e-6) << "extent 分支的 bounds 仍是 lon,lat 传统序（minLon）";
        EXPECT_NEAR(bounds[1], 59.3, 1e-6) << "extent 分支的 bounds 仍是 lon,lat 传统序（minLat）";
        EXPECT_NEAR(bounds[2], 9.9, 1e-6) << "extent 分支的 bounds 仍是 lon,lat 传统序（maxLon）";
        EXPECT_NEAR(bounds[3], 60.1, 1e-6) << "extent 分支的 bounds 仍是 lon,lat 传统序（maxLat）";
    }
}

// F2（Task 17 修复轮 1，审查发现 I-1）：tile_scheme=tms 的行号反演用例。
// 修复前的盘上事实：grep -rn "tms" tests/ ⇒ exit=1（零命中），而 cutting_plugin.cpp:1027-1029
// 这行反演正是 Task 17 刚从 `(1<<z)-1-ty` 改成 `b.tiles-1-ty` 的改动点 ⇒ 改动零判别。
// 审查者的 R4（分母退回 1<<z）实测 0 红（那一轮 14 条聚焦全绿）。
// 本用例钉两件事：
//   (i) 写出的**文件名 y 号**是反演后的值（字面路径共 8 条：expectedXyz 4 条 + expectedTms 4 条；
//       z=10 时 1023-512=511、1023-513=510）；
//   (ii) 集合与同夹具同层级的 xyz 产出**同规模**，且只有 y 号不同（x/层级/张数一致）。
// WHY 字面期望写死 511/510 而不是在用例里算 `tiles-1-y`：反演本身就是被测性质，
// 用被测式子推期望等于把断言让给实现（R-T17-9(2) 同口径）。1023=2^10-1 是 XYZ/TMS 的定义。
// 等价变异如实登记（不硬造期望去杀它）：`b.tiles` 在**未被钳位的层级**恒等于 1<<z
// ⇒ "分母退回 1<<z" 在本档必存活；两者只在 mercatorTileGrid 钳位臂生效的层级
// （z=21/22 × ts=512/1024，见挂账 N68）才分道，而那档网格本身有损，
// 把产物文件名钉上去等于把一个待修的有损行为固化成锚 ⇒ 本环不钉，交控制器随 N68 处置。
TEST_F(PluginTest, TileSchemeTmsInvertsWrittenRowNumberButKeepsTileSetSize) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    std::vector<float> data(64 * 64, 0.0f);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 32; ++x) data[static_cast<size_t>(y) * 64 + x] = 200.0f;
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t17_tms_input.tif", 64, 64, data, options);

    // 源包络 x∈[0,64000]、y∈[-64000,0] ⇒ z=10（瓦片米宽 39135.7584820098）落进
    // 列 {512,513} × 行 {512,513} 共 4 张；xyz 方案写入行号 == 计算行号（既有锚 #296 同源算术）。
    std::set<std::string> producedXyz;
    std::set<std::string> producedTms;
    for (int pass = 0; pass < 2; ++pass) {
        const std::string schemeName = pass == 0 ? "xyz" : "tms";
        const fs::path output = getTestDir() /
            (pass == 0 ? "t17_tms_xyz_output" : "t17_tms_tms_output");
        fs::remove_all(output);
        auto params = t17BaseTileParams(input, utf8PathString(output));
        params["min_zoom"] = 10;
        params["max_zoom"] = 10;
        params["tile_scheme"] = schemeName;
        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << schemeName << ": " << result.message;
        const auto files = t17CollectTileFiles(output);
        if (pass == 0) producedXyz = files; else producedTms = files;
        const std::string json = t17ReadWholeFile(output / "metadata.json");
        EXPECT_NE(json.find("\"scheme\": \"" + schemeName + "\""), std::string::npos)
            << schemeName << " 的 metadata.scheme 未如实记录: " << json;
    }

    const std::set<std::string> expectedXyz = {
        "10/512/512.png", "10/512/513.png", "10/513/512.png", "10/513/513.png",
    };
    // TMS：行号自下（南）而上 ⇒ 同一张瓦片写出的文件名是 (2^10-1)-y。
    const std::set<std::string> expectedTms = {
        "10/512/511.png", "10/512/510.png", "10/513/511.png", "10/513/510.png",
    };
    EXPECT_EQ(producedXyz, expectedXyz)
        << "xyz 基准集合变了（实际 [" << t17JoinFiles(producedXyz) << "]）";
    EXPECT_EQ(producedTms, expectedTms)
        << "tms 反演后的文件名 y 号不对（实际 [" << t17JoinFiles(producedTms) << "]）";
    EXPECT_EQ(producedTms.size(), producedXyz.size()) << "tms 与 xyz 同输入必须同规模";
}

// F6（Task 17 修复轮 1，控制器新增的 N62 同形残留）：tiles_generated 必须是**实际交付的文件数**。
// 改前 `++tilesGenerated` 挂在每一个 continue 分支上（零交集/零面积/读失败/全 NoData/skipBlank/resume），
// 而 metadata["tiles_generated"] 与成功消息都吃这个数 ⇒ "报 N tiles 但磁盘上没有 N 个文件"
// 这条本任务刚消灭的形态在新代码里又活了；`grep -rn "tiles_generated" tests/` 改前 = exit 1（零命中）。
// 夹具沿用 #296（南端+东端 NoData 带）：z=10 候选 4 全写、z=11 候选 16 只写 9（列 1027 与行 1027
// 整体落 NoData 带 ⇒ 16−4−4+1=9）⇒ **本轮跑里同时存在被跳过的瓦片**（13 写 / 7 跳 / 20 候选）。
// WHY 必须用含跳过的夹具：全写的夹具下"含跳过"与"只算写出"两口径同值，
// 任何把计数退回旧口径的变异都测不出来 ⇒ 这条断言就没牙。
TEST_F(PluginTest, TileCountersMatchWrittenTileFiles) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    options.hasNoData = true;
    std::vector<float> data(64 * 64, 0.0f);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 32; ++x) data[static_cast<size_t>(y) * 64 + x] = 200.0f;
    for (int y = 56; y < 64; ++y)
        for (int x = 0; x < 64; ++x) data[static_cast<size_t>(y) * 64 + x] = -9999.0f;
    for (int y = 0; y < 56; ++y)
        for (int x = 56; x < 64; ++x) data[static_cast<size_t>(y) * 64 + x] = -9999.0f;
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t17_counters_input.tif", 64, 64, data, options);

    const fs::path output = getTestDir() / "t17_counters_output";
    fs::remove_all(output);
    auto params = t17BaseTileParams(input, utf8PathString(output));
    params["min_zoom"] = 10;
    params["max_zoom"] = 11;
    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    // t17CollectTileFiles 已经排除 metadata.json ⇒ 它数到的就是 output 下的瓦片文件数。
    const auto produced = t17CollectTileFiles(output);
    ASSERT_EQ(produced.size(), 13u) << "夹具的产出规模变了，实际 [" << t17JoinFiles(produced) << "]";

    // 主断言：metadata 报的数**恰等于**磁盘上数到的文件数（N62 的收口形态就是这个等式）。
    EXPECT_EQ(result.metadata.at("tiles_generated"), std::to_string(produced.size()))
        << "tiles_generated 与 output 下的瓦片文件数不一致";
    EXPECT_EQ(result.metadata.at("tiles_generated"), "13") << "字面期望：13 张写出";
    EXPECT_EQ(result.metadata.at("tiles_skipped"), "7") << "字面期望：7 张被跳过（16−9=7 张全 NoData）";

    // 成功消息里的数字也要能在磁盘上数出来（用户可见的那一句同样受 N62 口径约束）。
    EXPECT_NE(result.message.find("13 tiles"), std::string::npos) << result.message;
    EXPECT_EQ(result.message.find("20 tiles"), std::string::npos)
        << "成功消息又回到含跳过的旧口径了: " << result.message;
}

// ===========================================================================
// Task 18：多波段渲染 + 整幅统一拉伸 + 真 alpha（R-T18-3/4/6/9）
// ===========================================================================

// T18-S5(1)（R-T18-3 后半、R-T18-4(i)(ii)）：三波段输入必须渲染成彩色，不许退化成灰度。
//
// 通道顺序与容器口径（R-T18-4(ii) 要求"哪两个通道该相等"不许靠猜，这里把口径写死）：
//   输出矩阵恒为 CV_8UC4，OpenCV 的内存序是 (B,G,R,A)，cv::imwrite 写 PNG 时转成
//   (R,G,B,A)；GDAL 的 PNG 驱动按 (R,G,B,A) 暴露波段 ⇒ **openRaster 的波段号 i 就是
//   图像的第 i 个颜色通道**（band1=R、band2=G、band3=B、band4=A），用例里不再做第二次翻转。
//   插件侧的映射规则是 bandForChannel(c) = min(c+1, colorBands)，c=0,1,2 → R,G,B：
//     1 波段 → R=G=B=band1（灰度）  2 波段 → R=b1,G=b2,B=b2  3 波段 → R=b1,G=b2,B=b3
//     4 波段 → R=b1,G=b2,B=b3，band4 进 alpha
//   ⇒ 单波段灰度的口径是"前三带逐像元相等"（不是"只写 1 个波段"）：既有锚 #298 的
//     ASSERT_EQ(ds->GetRasterCount(), 4) 对单波段输入钉住了 4 波段容器（GOLD），
//     R-T18-4(i) 要求的"定死口径"就定在这里，TileSingleBandInputRendersGrayTriple 锁它。
//
// 夹具为什么长这样（R-T18-3）：三个波段各自"某列之后翻到 200"，翻转列不同 ⇒ 三个波段的
//   **分布**不同。常值三波段夹具（writeConstantBandsRaster {250,128,20}）做不到这件事：
//   每波段整幅恒定 ⇒ 拉伸后三通道逐像元相等 ⇒ 灰度退化与彩色产出无法区分，用例没牙。
//   翻到同一个 200 还顺带把"波段间亮度差"和"拉伸参数不同"解耦：三个波段的 (min,max)
//   都是 (0,200) ⇒ 归一化后只能是 0 或 255 ⇒ 期望均值可以用算术写死（下面逐条给）。
//
// 期望算术（z=8 单瓦片，瓦片米宽 156543.0339、tileRes=611.496；夹具 64×64@1000 m ⇒
//   覆盖子矩形 dx1=ceil(64000/611.496)=105，即不透明列/行区间 [0,104]，105×105 像元）：
//   overview_resampling 取默认 nearest ⇒ INTER_NEAREST 把 64 列放大到 105 列：
//   目标列 d 取源列 floor(d*64/105)，于是"源列 >= k"翻到 255 的目标列起点是 ceil(k*105/64)：
//     band1 k=32 ⇒ d>=53 ⇒ 52 列亮 ⇒ mean(R)=255*52/105=126.3
//     band2 k=48 ⇒ d>=79 ⇒ 26 列亮 ⇒ mean(G)=255*26/105= 63.1
//     band3 k=16 ⇒ d>=27 ⇒ 78 列亮 ⇒ mean(B)=255*78/105=189.4
//   ⇒ 三者两两相差 63 左右，容差取 15（warp 栅格与源栅格若有半像元错位，只会让边界挪 1~2
//   列，均值挪 2.4~4.9，远在容差内）。
TEST_F(PluginTest, TileRendersThreeBandsAsColorNotGray) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    const auto makeBand = [](int splitCol, float high) {
        std::vector<float> data(static_cast<size_t>(64) * 64, 0.0f);
        for (int y = 0; y < 64; ++y)
            for (int x = splitCol; x < 64; ++x) data[static_cast<size_t>(y) * 64 + x] = high;
        return data;
    };
    const std::string input = t18WriteBandsRaster(
        getTestDir(), "t18_color_input.tif", 64, 64,
        std::vector<std::vector<float>>{makeBand(32, 200.0f), makeBand(48, 200.0f), makeBand(16, 200.0f)},
        options);
    ASSERT_FALSE(input.empty());

    const fs::path output = getTestDir() / "t18_color_output";
    fs::remove_all(output);
    auto params = t17BaseTileParams(input, utf8PathString(output));
    params["min_zoom"] = 8;
    params["max_zoom"] = 8;
    params["resampling"] = std::string("nearest");   // warp 侧也别糊化边界（口径见上）

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    const auto files = t17CollectTileFiles(output);
    ASSERT_EQ(files.size(), 1u) << "z=8 夹具应产出单瓦片，实际 [" << t17JoinFiles(files) << "]";
    const T18TileBands tile = t18ReadTile(output / fs::path(*files.begin()));
    ASSERT_TRUE(tile.ok());
    EXPECT_EQ(tile.bands, 4) << "容器口径：输出恒为 RGBA 四波段（#298 同款）";

    const T18OpaqueStats r = t18StatsOverOpaque(tile, 1);
    const T18OpaqueStats g = t18StatsOverOpaque(tile, 2);
    const T18OpaqueStats b = t18StatsOverOpaque(tile, 3);
    ASSERT_GT(r.count, 0);
    EXPECT_EQ(r.count, g.count);   // 同一批不透明像元上比较三个通道
    EXPECT_EQ(g.count, b.count);

    // 主判别：三通道必须两两不等（现实现只读 band 1 ⇒ 三通道逐像元相等 ⇒ 这里红）
    EXPECT_GT(std::abs(r.mean - g.mean), 40.0)
        << "R/G 通道均值相同 ⇒ 三波段被退化成灰度：r=" << r.mean << " g=" << g.mean;
    EXPECT_GT(std::abs(g.mean - b.mean), 40.0)
        << "G/B 通道均值相同 ⇒ 三波段被退化成灰度：g=" << g.mean << " b=" << b.mean;
    EXPECT_GT(std::abs(r.mean - b.mean), 40.0)
        << "R/B 通道均值相同 ⇒ 三波段被退化成灰度：r=" << r.mean << " b=" << b.mean;

    // 顺序判别：band1→R、band2→G、band3→B 的字面期望（交换 R/B 的变异只红这一组）
    EXPECT_NEAR(r.mean, 126.3, 15.0) << "band1→R 的期望均值不对";
    EXPECT_NEAR(g.mean, 63.1, 15.0) << "band2→G 的期望均值不对";
    EXPECT_NEAR(b.mean, 189.4, 15.0) << "band3→B 的期望均值不对";
    EXPECT_GT(b.mean, r.mean) << "通道顺序应为 B(=band3) > R(=band1)";
    EXPECT_GT(r.mean, g.mean) << "通道顺序应为 R(=band1) > G(=band2)";
}

// T18-S5(2)（R-T18-3 前半的落地形态、R-T18-6）：拉伸参数必须按**整幅一次**统计，
// 同一地物在不同瓦片里的亮度必须一致。
//
// 为什么这条夹具能区分"整幅统计"与"逐瓦片统计"，而常值夹具不能（R-T18-3/R-T18-6）：
//   夹具只有 1 个波段，但把**暗像素全部放在西半**（源列 0..15 = 0，其余 = 200）。
//   z=10 的瓦片米宽 39135.758 ⇒ 列 512 覆盖 x∈[0,39135.758)（源列 0..39，含 0 与 200 两档），
//   列 513 覆盖 x∈[39135.758,78271.516)（源列 40..63，**整幅只有 200 一档**）。
//     逐瓦片统计：513 那几张 bandMin=bandMax=200 ⇒ range 走 :1157 的 1.0 兜底
//                 ⇒ 255*(200-200)/1.0 = 0 ⇒ 同一块 200 的地物在 513 里是**全黑**；
//     整幅统计：  全幅 (min,max)=(0,200) ⇒ 200 → 255 ⇒ 512 与 513 里同一地物都是 255。
//   ⇒ 红的是"瓦片 513 的亮度"这一组断言（EXPECT_EQ(east.maxValue, 255) /
//     EXPECT_EQ(east.minValue, 255) / EXPECT_EQ(west.maxValue, east.maxValue)），
//     而瓦片 512 的断言在两种实现下都绿 ⇒ 点名红色来自东列，不是西列。
//   另加 west.minValue==0 一条，防"整幅统计把对比度洗白"（若有人把 range 兜底改成
//   max=min+1 之类，暗档会被抬起来 ⇒ 这条红）。
TEST_F(PluginTest, TileStretchIsWholeSceneWideNotPerTile) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    std::vector<float> data(static_cast<size_t>(64) * 64, 200.0f);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 16; ++x) data[static_cast<size_t>(y) * 64 + x] = 0.0f;   // 暗档只住西半
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t18_stretch_input.tif", 64, 64, data, options);
    ASSERT_FALSE(input.empty());

    const fs::path output = getTestDir() / "t18_stretch_output";
    fs::remove_all(output);
    auto params = t17BaseTileParams(input, utf8PathString(output));
    params["min_zoom"] = 10;
    params["max_zoom"] = 10;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    const std::set<std::string> expected = {
        "10/512/512.png", "10/512/513.png", "10/513/512.png", "10/513/513.png",
    };
    const auto files = t17CollectTileFiles(output);
    EXPECT_EQ(files, expected) << "瓦片集合变了，实际 [" << t17JoinFiles(files) << "]";

    const T18TileBands west = t18ReadTile(output / fs::path("10/512/512.png"));
    const T18TileBands east = t18ReadTile(output / fs::path("10/513/512.png"));
    ASSERT_TRUE(west.ok());
    ASSERT_TRUE(east.ok());

    const T18OpaqueStats westR = t18StatsOverOpaque(west, 1);
    const T18OpaqueStats eastR = t18StatsOverOpaque(east, 1);
    ASSERT_GT(westR.count, 0);
    ASSERT_GT(eastR.count, 0);

    // 东列瓦片整幅只有值 200 一档：逐瓦片统计会把它压成 0（见头注释的算术）。
    EXPECT_EQ(eastR.maxValue, 255) << "全 200 的瓦片被按自己的 min/max 拉伸 ⇒ 亮度 " << eastR.maxValue;
    EXPECT_EQ(eastR.minValue, 255) << "同一张瓦片内部亮度必须均匀（整幅统计下 200 恒映射到 255）";
    // 跨瓦片的同一地物（源值 200）亮度一致 —— 这就是 Produces 承诺的那件事。
    EXPECT_EQ(westR.maxValue, eastR.maxValue)
        << "值 200 的地物在两张瓦片里亮度不同：west=" << westR.maxValue << " east=" << eastR.maxValue;
    // 西列瓦片仍要保住自己的暗档（防"整幅统计洗白对比度"）。
    EXPECT_EQ(westR.minValue, 0) << "暗档被抬起来了：west.min=" << westR.minValue;
    EXPECT_EQ(westR.maxValue, 255) << "亮档没到满量程：west.max=" << westR.maxValue;
}

// T18-S5(2b)（修复轮 F4，钉的是 S1/S2 交付的核心性质）：拉伸区间必须**逐波段各算各的**，
// 不许所有波段共用 band1 的 (min,max)。
// WHY 这条必须单独存在（疑虑 5(a) 在原工单里被登记为"现有夹具下不可判别"）：
//   #311/#312/#316 用的三波段夹具每个波段都是 {0,200} ⇒ 三条 (min,max) 逐字段相同 ⇒
//   "全部复用 band1 区间"这个变异在那些夹具下产出**逐像元相同**的瓦片 ⇒ 无从判别。
//   本用例把三个波段的量程做成互不相同（band1 {0,200}、band2 {0,50}、band3 {100,1000}），
//   共用区间与逐波段区间的产物立刻分道，红的是下面点名那两条。
// 期望算术（沿用 #311 已核过的 z=8 单瓦片几何：覆盖子矩形 [0,104]²，resampling=nearest 保证
//   warp 与 cv::resize 两级都不糊边界 ⇒ 瓦片里只出现源值集合，拉伸后只出现各波段自己的 0/255）：
//   每波段都是"列 >= 32 翻到高档"，64 列放大到 105 列 ⇒ 亮档 52 列、暗档 53 列 ⇒
//     band1 逐波段 (0,200)   ⇒ 0→0、200→255            mean = 255*52/105 = 126.3
//     band2 逐波段 (0,50)    ⇒ 0→0、 50→255            mean = 126.3
//     band3 逐波段 (100,1000)⇒ 100→0、1000→255         mean = 126.3
//   ⇒ 三个通道**两两相等**（这正是"每波段都被归一到自己的满量程"的签名），
//     且每通道 min=0、max=255。
//   变异"全部复用 band1 的 (0,200)"后的算术：
//     band2: 0→0、50→255*50/200=63   ⇒ maxValue 63（红在 EXPECT_EQ(g.maxValue,255)）
//     band3: 100→127、1000→255       ⇒ minValue 127（红在 EXPECT_EQ(b.minValue,0)）
//     band1 自己不变 ⇒ 控制组，证明红色来自"区间被共用"而不是夹具坏了。
//   另两条均值断言（126.3±15）在变异下变成 31.2 与 190.4 ⇒ 同样红，作二次证据。
TEST_F(PluginTest, TileStretchUsesEachBandOwnRange) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    // 三个波段同一翻转列（32）、**不同量程**：低档/高档分别是 (0,200)、(0,50)、(100,1000)。
    const auto makeBand = [](float low, float high) {
        std::vector<float> data(static_cast<size_t>(64) * 64, low);
        for (int y = 0; y < 64; ++y)
            for (int x = 32; x < 64; ++x) data[static_cast<size_t>(y) * 64 + x] = high;
        return data;
    };
    const std::string input = t18WriteBandsRaster(
        getTestDir(), "t18_perband_input.tif", 64, 64,
        std::vector<std::vector<float>>{
            makeBand(0.0f, 200.0f), makeBand(0.0f, 50.0f), makeBand(100.0f, 1000.0f),
        },
        options);
    ASSERT_FALSE(input.empty());

    const fs::path output = getTestDir() / "t18_perband_output";
    fs::remove_all(output);
    auto params = t17BaseTileParams(input, utf8PathString(output));
    params["min_zoom"] = 8;
    params["max_zoom"] = 8;
    params["resampling"] = std::string("nearest");

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;

    const auto files = t17CollectTileFiles(output);
    ASSERT_EQ(files.size(), 1u) << "z=8 夹具应产出单瓦片，实际 [" << t17JoinFiles(files) << "]";
    const T18TileBands tile = t18ReadTile(output / fs::path(*files.begin()));
    ASSERT_TRUE(tile.ok());
    ASSERT_EQ(tile.bands, 4) << "容器口径：输出恒为 RGBA 四波段";

    const T18OpaqueStats r = t18StatsOverOpaque(tile, 1);
    const T18OpaqueStats g = t18StatsOverOpaque(tile, 2);
    const T18OpaqueStats b = t18StatsOverOpaque(tile, 3);
    ASSERT_GT(r.count, 0);
    ASSERT_EQ(r.count, g.count) << "三个通道必须落在同一批不透明像元上";
    ASSERT_EQ(g.count, b.count);

    // 主判别：每波段都被归一到**自己**的满量程 ⇒ 三通道都必须是 0..255。
    EXPECT_EQ(r.minValue, 0) << "band1 暗档（控制组）：min=" << r.minValue;
    EXPECT_EQ(r.maxValue, 255) << "band1 亮档（控制组）：max=" << r.maxValue;
    EXPECT_EQ(g.minValue, 0) << "band2 暗档：min=" << g.minValue;
    EXPECT_EQ(g.maxValue, 255)
        << "band2 的 50 没被按自己的 (0,50) 归一（复用 band1 的 (0,200) 时它是 63）：max="
        << g.maxValue;
    EXPECT_EQ(b.minValue, 0)
        << "band3 的 100 没被按自己的 (100,1000) 归一（复用 band1 的 (0,200) 时它是 127）：min="
        << b.minValue;
    EXPECT_EQ(b.maxValue, 255) << "band3 亮档：max=" << b.maxValue;

    // 二次证据：三通道均值两两相等且都等于算术值 126.3（变异下变成 31.2 / 190.4）。
    EXPECT_NEAR(r.mean, 126.3, 15.0) << "band1 均值：r=" << r.mean;
    EXPECT_NEAR(g.mean, 126.3, 15.0) << "band2 均值：g=" << g.mean;
    EXPECT_NEAR(b.mean, 126.3, 15.0) << "band3 均值：b=" << b.mean;
    EXPECT_LT(std::abs(r.mean - g.mean), 10.0)
        << "逐波段归一后 R/G 均值应当相等：r=" << r.mean << " g=" << g.mean;
    EXPECT_LT(std::abs(r.mean - b.mean), 10.0)
        << "逐波段归一后 R/B 均值应当相等：r=" << r.mean << " b=" << b.mean;
}

// T18-S5(3)（R-T18-4(i)）：单波段输入 → 灰度口径 = 前三带逐像元相等 + 容器仍是 4 波段。
// 这是一条**口径锁**（现实现已满足），它的判别对象是"多波段改造时把 1 波段只写进 R 通道、
// G/B 留 0"这种实现错误：变异 bandForChannel(c)=c+1（不钳位）⇒ 本用例红。
TEST_F(PluginTest, TileSingleBandInputRendersGrayTriple) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    std::vector<float> data(static_cast<size_t>(64) * 64, 200.0f);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 32; ++x) data[static_cast<size_t>(y) * 64 + x] = 0.0f;
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t18_gray_input.tif", 64, 64, data, options);
    ASSERT_FALSE(input.empty());

    const fs::path output = getTestDir() / "t18_gray_output";
    fs::remove_all(output);
    auto params = t17BaseTileParams(input, utf8PathString(output));
    params["min_zoom"] = 8;
    params["max_zoom"] = 8;

    const auto result = p->execute(params, progress_);
    ASSERT_TRUE(result.success) << result.message;
    const auto files = t17CollectTileFiles(output);
    ASSERT_EQ(files.size(), 1u) << "实际 [" << t17JoinFiles(files) << "]";

    const T18TileBands tile = t18ReadTile(output / fs::path(*files.begin()));
    ASSERT_TRUE(tile.ok());
    EXPECT_EQ(tile.bands, 4) << "单波段输入的容器口径也是 RGBA 四波段（#298 同款锚）";
    const size_t pixels = static_cast<size_t>(tile.width) * static_cast<size_t>(tile.height);
    size_t diffRG = 0, diffGB = 0;
    for (size_t i = 0; i < pixels; ++i) {
        if (tile.data[0][i] != tile.data[1][i]) ++diffRG;
        if (tile.data[1][i] != tile.data[2][i]) ++diffGB;
    }
    EXPECT_EQ(diffRG, 0u) << "灰度口径要求 R==G 逐像元相等，不等像元数 " << diffRG;
    EXPECT_EQ(diffGB, 0u) << "灰度口径要求 G==B 逐像元相等，不等像元数 " << diffGB;
    // 单波段仍然要有真实对比度（整幅 min/max=(0,200) ⇒ 亮档 255、暗档 0）。
    const T18OpaqueStats gray = t18StatsOverOpaque(tile, 1);
    ASSERT_GT(gray.count, 0);
    EXPECT_EQ(gray.maxValue, 255);
    EXPECT_EQ(gray.minValue, 0);
}

// T18-S5(4)（R-T18-9 / N65）：add_alpha 必须是真参数，true/false 对同一含 NoData 的输入
// 要产出**可区分的产物**。
//
// 为什么必须用多波段夹具（这条也解释 N65 为什么能藏这么久）：
//   单波段源下"任一波段有效"与"全部波段有效"是同一个判据 ⇒ true/false 恒等，
//   所以把 :1166-1168 的退化三元 `addAlpha ? 255 : 255` 改回来、或把两支写成同一个掩膜，
//   在单波段用例（#296/#298）上**都不会红**。判别取证只能建在"某一个波段局部 NoData、
//   其余波段有效"的多波段源上。
// 语义（写进插件注释，同样在这里钉住）：
//   add_alpha=true  ⇒ 只有**所有**颜色波段都没有有效像元时才透明（洞留在颜色里）；
//   add_alpha=false ⇒ **任一**颜色波段 NoData 就遮掉（不请求 alpha ⇒ 透明度取最小口径，
//                    与既有锚 #298 对单波段 NoData 的 alpha=0 一致）。
//   ⇒ alpha 关掉时透明度**减少**（不是"完全没有 alpha"）：GOLD #298/#296 用 add_alpha=false
//     钉住了"单波段 NoData ⇒ alpha=0"，所以本参数不可能被实现成"全不透明"，这是盘上事实。
//
// 夹具算术：64×64@1000 m，3 个波段都声明 nodata=-9999（applyFixtureHeader 的口径），
//   band1/band3：源列 >=16 为 200，否则 0；band2：同款，但源列 24..31 铺 -9999（洞带）。
//   z=8 单瓦片、resampling 与 overview_resampling 都取 nearest ⇒ 不透明区是 [0,104]×[0,104]，
//   洞带落在目标列 40..51（x∈[24000,32000) ⇒ d=floor 反解 ceil(24*105/64)=40 起）。
//   统计窗口取 0..99 见方（离覆盖缘留 5 列/行余量）：10000 个像元。
//     true  ⇒ 窗口内没有任何"三波段全无效"的像元 ⇒ alpha==0 计数**恒为 0**；
//     false ⇒ 洞带整条（约 12 列 × 100 行 ≈ 1200 像元）alpha==0 ⇒ 计数 > 200。
TEST_F(PluginTest, TileAddAlphaChangesTransparencyForPartialBandNoData) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    options.hasNoData = true;
    options.noData = -9999.0;
    const size_t pixels = static_cast<size_t>(64) * 64;
    std::vector<float> solid(pixels, 0.0f);
    std::vector<float> holey(pixels, 0.0f);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const size_t i = static_cast<size_t>(y) * 64 + x;
            const float v = (x >= 16) ? 200.0f : 0.0f;
            solid[i] = v;
            holey[i] = (x >= 24 && x < 32) ? -9999.0f : v;   // band2 的洞带
        }
    }
    const std::string input = t18WriteBandsRaster(
        getTestDir(), "t18_alpha_input.tif", 64, 64,
        std::vector<std::vector<float>>{solid, holey, solid}, options);
    ASSERT_FALSE(input.empty());

    int transparentOn = -1, transparentOff = -1, opaqueOn = -1, opaqueOff = -1;
    int holeGreenOn = -1, holeGreenOff = -1;
    std::set<std::string> filesOn, filesOff;
    for (int pass = 0; pass < 2; ++pass) {
        const bool addAlpha = pass == 0;
        const fs::path output = getTestDir() / (addAlpha ? "t18_alpha_on_output" : "t18_alpha_off_output");
        fs::remove_all(output);
        auto params = t17BaseTileParams(input, utf8PathString(output));
        params["min_zoom"] = 8;
        params["max_zoom"] = 8;
        params["resampling"] = std::string("nearest");
        params["add_alpha"] = addAlpha;

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << result.message;
        const auto files = t17CollectTileFiles(output);
        ASSERT_EQ(files.size(), 1u) << "add_alpha=" << addAlpha << " 实际 [" << t17JoinFiles(files) << "]";
        const T18TileBands tile = t18ReadTile(output / fs::path(*files.begin()));
        ASSERT_TRUE(tile.ok());
        EXPECT_EQ(tile.bands, 4) << "add_alpha 不改变容器波段数（口径见头注释）";

        // 只在覆盖区内统计（目标列/行 [0,99]，留 5 像元余量避开覆盖缘）
        int transparent = 0, opaque = 0, holeGreen = 0;
        for (int y = 0; y < 100; ++y) {
            for (int x = 0; x < 100; ++x) {
                const size_t i = static_cast<size_t>(y) * tile.width + x;
                const uchar a = tile.data[3][i];
                if (a == 0) {
                    ++transparent;
                } else {
                    ++opaque;
                    // 洞带里的颜色（钉"透明即整像素清零 / 不透明洞留黑色通道"这条产物口径）：
                    // add_alpha=true 时洞带像元**不透明**（dstalpha 的口径是"任一波段有效即
                    // 不透明"，实测 GDAL 正是这个口径 ⇒ transparentOn==0 那条断言也在钉它），
                    // 而 band2 在洞带里的色值是 **-dstalpha 写进去的 0**（不是源 NoData 原值 -9999）。
                    // 机理：`-dstalpha` 剥掉色波段的 NODATA 元数据 ⇒ 整幅统计把 0 当成数据，band2 的
                    // min 就是 **0**（夹具里 band2 源列 <16 本来就是 0，洞带写回的也是 0）
                    // ⇒ 洞带像元 `255*(0-0)/(200-0)=0` ⇒ 绿道为 0、红道仍 255（band1 在洞带列有效值 200）。
                    // 反证 min 不是 -9999：若下界真是 -9999，0 会被映射到 255·9999/10199 ≈ 250，
                    // 下面 `holeGreenOn` 那条"红=255 且绿=0"的断言当场就红（这条是**算术推论**；
                    // 同一族比值在 #320 的夹具上有实测：低档字节 63 = 255·100/400，只可能出自 min=0）。
                    // 注：这里**不是** stretchByte 的 clamp 的判别点——变异 R9（去掉 clamp，字面形态）
                    // 实测 26/26 全绿，因为 scaled 恒在 [0,255]（min/max 就来自同一批数据），
                    // 因此登记为"不可判别之防御臂"，不拿它当测试缺口追打。
                    if (tile.data[0][i] == 255 && tile.data[1][i] == 0) ++holeGreen;
                }
            }
        }
        if (addAlpha) { transparentOn = transparent; opaqueOn = opaque; holeGreenOn = holeGreen; filesOn = files; }
        else { transparentOff = transparent; opaqueOff = opaque; holeGreenOff = holeGreen; filesOff = files; }
    }

    // 同一张瓦片、同一个洞带：透明度必须可区分（N65 的判别取证）
    EXPECT_EQ(filesOn, filesOff) << "add_alpha 只该改透明度，不该改交付的瓦片集合";
    EXPECT_EQ(transparentOn, 0)
        << "add_alpha=true 时局部单波段空洞不该被遮成透明（还有两个波段有效）：" << transparentOn;
    EXPECT_GT(transparentOff, 200)
        << "add_alpha=false 时任一波段 NoData 就该遮掉，洞带约 12 列 × 100 行没被遮： " << transparentOff;
    EXPECT_LT(transparentOn, transparentOff)
        << "add_alpha=true/false 产出不可区分 ⇒ N65 退化三元又活了";
    EXPECT_GT(opaqueOn - opaqueOff, 200) << "不透明像元数也必须随 add_alpha 变化";
    EXPECT_LT(transparentOff, 10000) << "false 也不该把覆盖区整块遮完";
    EXPECT_GT(opaqueOff, 200) << "false 时非洞区仍应不透明";
    // 洞带在 true 档必须是"绿道=0 的黑洞 + 不透明"（0 的来源见上面循环里的注释：
    // 洞带在色波段里是 -dstalpha 写回的 0，而 band2 的整幅 min 也是 0）；false 档整像素为 0（含颜色），
    // 所以 R=255 且 G=0 的像元不存在。这两条各钉一个产物口径，"把 true/false 两支写成同一个掩膜"
    // 那类退化形态（R1b 那一组变异）实测红的就是这一组。
    EXPECT_GT(holeGreenOn, 200)
        << "true 档的洞带没把绿道落成 0（红道应仍是 255）：" << holeGreenOn;
    EXPECT_EQ(holeGreenOff, 0)
        << "false 档的透明像元不该留下 R=255 的颜色（透明即整像素清零）：" << holeGreenOff;
}

// T18-S5(5)（R-T18-4(iii)、R-T18-6 的"跳过语义必须保留"、F6 计数口径）：
// 全 NoData 瓦片的跳过守卫 × skip_blank 的组合矩阵。
//
// 组合矩阵为什么是这个形状（照 R-T18-4(iii) 的"文件在/不在"落地时的盘上冲突）：
//   R-T18-4(iii) 要的是 skip_blank=true/false × 整张全 NoData 瓦片"断该层级里文件在/不在"。
//   但既有锚已经把"全 NoData ⇒ 不写文件"钉成了**与 skip_blank 无关**的事实：
//     #298 TileLevelGeometryUsesWebMercatorGrid：t17BaseTileParams 的 skip_blank=false，
//         z=11 期望 9 张（16−7），那 7 张正是全 NoData 瓦片，注释写明"走既有的全 NoData
//         瓦片跳过路径"；
//     #310 TileCountersMatchWrittenTileFiles：同一夹具 skip_blank=false 下
//         tiles_generated=="13" && tiles_skipped=="7"。
//   ⇒ 若把守卫改挂到 skip_blank 上，#298 与 #310 同时红（GOLD 冻结不许改期望）。
//   ⇒ 且若 isTileBlank 的阈值按 Step 3 改成"有效像元数为 0 才算空"，它就与这条**无条件**
//     守卫完全同判据 ⇒ skip_blank 变成死键（N65 同形）。所以本环保留 isTileBlank 的
//     非零容差（cutting_plugin.cpp 的 kBlankOpaquePixelTolerance），把组合用例拆成两半：
//       档 A：全 NoData 瓦片在 4 种 (add_alpha × skip_blank) 组合下都**不写文件**，
//             且计数计入 tiles_skipped —— 钉"守卫保留"（删守卫 ⇒ 这里红）；
//       档 B：只有几个不透明像元的"近空瓦片"在 skip_blank=false 时**写**、true 时**不写**
//             —— 钉 skip_blank 仍是活键，同时钉住容差本身（容差改成 0 ⇒ true 档红）。
TEST_F(PluginTest, TileBlankGuardsAndSkipBlankCombination) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    options.hasNoData = true;
    options.noData = -9999.0;

    // ---- 档 A：整幅全 NoData ⇒ 一张都不写，四种参数组合口径一致 ----
    const std::vector<float> allNoData(8 * 8, -9999.0f);
    const std::string deadInput =
        gis::tests::writeFloatRaster(getTestDir(), "t18_blank_input.tif", 8, 8, allNoData, options);
    ASSERT_FALSE(deadInput.empty());
    for (int variant = 0; variant < 4; ++variant) {
        const bool addAlpha = (variant & 1) != 0;
        const bool skipBlank = (variant & 2) != 0;
        const fs::path output = getTestDir() /
            ("t18_blank_" + std::string(addAlpha ? "a" : "n") + (skipBlank ? "s" : "k"));
        fs::remove_all(output);
        auto params = t17BaseTileParams(deadInput, utf8PathString(output));
        params["min_zoom"] = 8;
        params["max_zoom"] = 8;
        params["add_alpha"] = addAlpha;
        params["skip_blank"] = skipBlank;

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << result.message;
        const auto files = t17CollectTileFiles(output);
        EXPECT_EQ(files, std::set<std::string>{})
            << "add_alpha=" << addAlpha << " skip_blank=" << skipBlank
            << " 全 NoData 瓦片不得写出（原 :1151 语义必须保留）";
        EXPECT_TRUE(fs::exists(output / "metadata.json")) << "零瓦片也要写出 metadata.json";
        EXPECT_EQ(result.metadata.at("tiles_generated"), "0") << "variant=" << variant;
        EXPECT_EQ(result.metadata.at("tiles_skipped"), "1")
            << "新增/保留的跳过臂必须同时 ++tilesSkipped（F6 口径），variant=" << variant;
    }

    // ---- 档 B：近空瓦片（不透明像元 ≤ 容差）⇒ skip_blank 决定文件在不在 ----
    // 夹具算术（z=8 单瓦片，tileRes=611.496 m，源像元 1000 m）：
    //   1×1 源栅格 ⇒ 读窗 1×1，目标子矩形 dx1=ceil(1000/611.496)=2、dy1=2 ⇒ 2×2=4 个不透明像元
    //               ⇒ 恰在容差 kBlankOpaquePixelTolerance=4 之内 ⇒ skip_blank=true 时判空跳过；
    //   2×1 源栅格 ⇒ dx1=ceil(2000/611.496)=4、dy1=2 ⇒ 4×2=8 个不透明像元 > 4 ⇒ 不判空，
    //               两种 skip_blank 都写出（钉住"判空谓词不是恒真"，否则 isTileBlank
    //               退化成 `return true` 的变异在这里红）。
    gis::tests::RasterFixtureOptions alive;
    alive.crs = gis::tests::FixtureCrs::Epsg3857;
    alive.originX = 0.0; alive.originY = 0.0; alive.pixelSize = 1000.0;   // 不声明 NoData
    const std::string onePixel =
        gis::tests::writeFloatRaster(getTestDir(), "t18_near1_input.tif", 1, 1, std::vector<float>{200.0f}, alive);
    const std::string twoPixel =
        gis::tests::writeFloatRaster(getTestDir(), "t18_near2_input.tif", 2, 1, std::vector<float>{200.0f, 200.0f}, alive);
    ASSERT_FALSE(onePixel.empty());
    ASSERT_FALSE(twoPixel.empty());

    struct Case { std::string input; bool skipBlank; size_t expectedFiles; int expectedGenerated; };
    const std::vector<Case> cases = {
        {onePixel, false, 1u, 1},    // 4 个不透明像元，skip_blank=false ⇒ 写出
        {onePixel, true,  0u, 0},    // 同一产物，skip_blank=true  ⇒ 判空跳过
        {twoPixel, false, 1u, 1},
        {twoPixel, true,  1u, 1},    // 8 个不透明像元 > 容差 ⇒ 不判空
    };
    for (size_t i = 0; i < cases.size(); ++i) {
        const Case& c = cases[i];
        const fs::path output = getTestDir() / ("t18_near_case_" + std::to_string(i));
        fs::remove_all(output);
        auto params = t17BaseTileParams(c.input, utf8PathString(output));
        params["min_zoom"] = 8;
        params["max_zoom"] = 8;
        params["skip_blank"] = c.skipBlank;

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << "case " << i << ": " << result.message;
        const auto files = t17CollectTileFiles(output);
        EXPECT_EQ(files.size(), c.expectedFiles)
            << "case " << i << "（近空规模 " << (i < 2 ? "1x1" : "2x1")
            << " skip_blank=" << c.skipBlank << "）期望 " << c.expectedFiles
            << " 张，实际 [" << t17JoinFiles(files) << "]";
        EXPECT_EQ(result.metadata.at("tiles_generated"), std::to_string(c.expectedGenerated))
            << "case " << i;
        if (!c.skipBlank) {
            // 写出档还要证明它真的是"少数不透明像元"，否则档 B 的判别力来自巧合。
            ASSERT_EQ(files.size(), 1u);
            const T18TileBands tile = t18ReadTile(output / fs::path(*files.begin()));
            const T18OpaqueStats st = t18StatsOverOpaque(tile, 1);
            EXPECT_EQ(st.count, i < 2 ? 4 : 8) << "case " << i << " 的不透明像元规模变了";
        }
    }
}

// T18-S10：三波段彩色在**每种输出格式**上都必须是同一个通道顺序
// （GDAL 读回的波段号 == 图像通道：1=R、2=G、3=B、4=A）。
// WHY 这条在本任务的射程内（不是顺手扩大范围）：encodeTile 的 jpeg 分支原来写
//   cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR)，把 CV_8UC4 的内存序 (B,G,R,A) 当成
//   (R,G,B,A) 读 ⇒ 产出 (R,G,B) 再被 imwrite 按 BGR 解释 ⇒ 红蓝互换。
//   改前瓦片只有灰度（R=G=B），这个错**看不出来**；S1 让彩色成为可能之后它就是真错色
//   （RED：改前跑本用例，jpeg 档红在 band1/band3 的顺序断言）。
//   png/webp/gtiff 三支改前就是对的（OpenCV 一律按 (B,G,R[,A]) 解释矩阵）⇒ 本用例对它们是
//   **口径锁**：谁以后再"顺手对齐一下顺序"（例如给 gtiff 加 BGRA2RGBA），当场红。
//   盘上事实：改前 `grep -n "output_format" tests/` 在 tile 侧零命中，四个格式一条用例都没有。
// 期望算术沿用 #311：band1/2/3 的翻转列分别是 32/48/16 ⇒ 覆盖子矩形 [0,104]² 内
//   mean(R)=255*52/105=126.3、mean(G)=255*26/105=63.1、mean(B)=255*78/105=189.4。
//   JPEG 是有损的，且它**没有 alpha**（3 波段）⇒ 统计一律按覆盖子矩形做（t18StatsInRect），
//   不借 alpha 筛不透明像元；容差 25。
TEST_F(PluginTest, TileColorOrderConsistentAcrossOutputFormats) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    const auto makeBand = [](int splitCol) {
        std::vector<float> data(static_cast<size_t>(64) * 64, 0.0f);
        for (int y = 0; y < 64; ++y)
            for (int x = splitCol; x < 64; ++x) data[static_cast<size_t>(y) * 64 + x] = 200.0f;
        return data;
    };
    const std::string input = t18WriteBandsRaster(
        getTestDir(), "t18_fmt_input.tif", 64, 64,
        std::vector<std::vector<float>>{makeBand(32), makeBand(48), makeBand(16)}, options);
    ASSERT_FALSE(input.empty());

    struct FormatCase { const char* fmt; const char* ext; int expectedBands; };
    const std::vector<FormatCase> cases = {
        {"png", ".png", 4}, {"jpeg", ".jpg", 3}, {"gtiff", ".tif", 4},
    };
    for (const FormatCase& c : cases) {
        const fs::path output = getTestDir() / (std::string("t18_fmt_") + c.fmt + "_output");
        fs::remove_all(output);
        auto params = t17BaseTileParams(input, utf8PathString(output));
        params["min_zoom"] = 8;
        params["max_zoom"] = 8;
        params["resampling"] = std::string("nearest");
        params["output_format"] = std::string(c.fmt);

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << c.fmt << ": " << result.message;
        const auto files = t17CollectTileFiles(output);
        ASSERT_EQ(files.size(), 1u) << c.fmt << " 实际 [" << t17JoinFiles(files) << "]";
        // 扩展名也得对上（tileFileExt 的口径：jpeg ⇒ .jpg、gtiff ⇒ .tif）
        EXPECT_EQ(fs::path(*files.begin()).extension().string(), c.ext) << c.fmt;

        const T18TileBands tile = t18ReadTile(output / fs::path(*files.begin()));
        ASSERT_TRUE(tile.ok()) << c.fmt;
        ASSERT_EQ(tile.bands, c.expectedBands) << c.fmt << " 的波段数口径";
        const T18OpaqueStats r = t18StatsInRect(tile, 1, 0, 0, 105, 105);
        const T18OpaqueStats g = t18StatsInRect(tile, 2, 0, 0, 105, 105);
        const T18OpaqueStats b = t18StatsInRect(tile, 3, 0, 0, 105, 105);
        EXPECT_NEAR(r.mean, 126.3, 25.0) << c.fmt << " 的 band1 不是红通道（均值 " << r.mean << "）";
        EXPECT_NEAR(g.mean, 63.1, 25.0) << c.fmt << " 的 band2 不是绿通道（均值 " << g.mean << "）";
        EXPECT_NEAR(b.mean, 189.4, 25.0) << c.fmt << " 的 band3 不是蓝通道（均值 " << b.mean << "）";
        EXPECT_GT(b.mean, r.mean) << c.fmt << " 的 B>R 顺序不对";
        EXPECT_GT(r.mean, g.mean) << c.fmt << " 的 R>G 顺序不对";
    }
}

// T18 fix round 2 F-3（复审 I-3）：Produces 承诺的 "**4→RGBA**" 分支此前**零用例**。
// 盘上事实（复审 round C 的 M0）：`if (bands == 4) { band4 → alpha }` 改成 `bands == 5` 之后
// **没有一条用例为它变红**（同一轮把 M0 与 M4"统计循环写成 b <= bandCount - 1"一起做了，那轮 27 条里
// 红的 12 条全部归因于 M4：#311/#315/#317 三条 Failed + 9 条单波段夹具 0xc0000409，与 alpha 分支无关
// ⇒ 红集与 M0 可分 ⇒ M0 存活），因为 `t18WriteBandsRaster` 的
// 四个调用点（#311/#313/#315/#317）全是 1 或 3 波段 ⇒ 4 波段源**从没被驱动过**。
// 本用例把这条分支钉住，并同时钉两件事（复审 §I-3 连带的"同一发测试能一起锁的内容"）：
//   ① 输出的第 4 波段必须来自**源 band4**，不是常量 255、也不是 dstalpha；
//   ② "部分透明（alpha 中间值）如何归一"的口径：band4 与颜色波段一样吃**整幅拉伸**，
//      于是源里"最不透明的那档"会被归一到**全透明**（下面 51→0 那条），
//      这是 bands==4 的可见后果，此前没被任何用例记录过（复审 §7 第 5 点：读码推断、未实测）。
//
// 夹具算术（64×64@1000 m、origin (0,0)、EPSG:3857、**不声明 NoData**；z=8 ⇒ 单瓦片、
//   覆盖子矩形 [0,105)² 沿用 #311/#317 已核过的几何；resampling=nearest ⇒ warp 与
//   cv::resize 两级都不糊边界，瓦片里只出现源值集合）：
//   band1 (R)：源列 <32 → 0，否则 200      整幅区间 (0,200)  ⇒ 0→0、200→255
//   band2 (G)：源行 <32 → 0，否则 200      整幅区间 (0,200)  ⇒ 同上（**行列反向**，用于分辨 R/G 串台）
//   band3 (B)：(列<32) XOR (行<32) → 200   整幅区间 (0,200)  ⇒ 四象限里唯一与 band1/2 都不同
//                                                       的图样 ⇒ "B 复制 G" 这类变异当场红
//   band4 (A)：源行 <16 → 51、16..31 → 102、>=32 → 204
//             整幅区间 (51,204)，range=153 ⇒ 51→0、102→85（255*51/153=85 整除）、204→255
//   瓦片像元 (行 r, 列 c) ← 源像元 (floor((r+0.5)*64/105), floor((c+0.5)*64/105))：
//     (5, 5)→(3, 3) ⇒ (R,G,B,A) = (0, 0, 0,   0)
//     (5,80)→(3,49) ⇒                    (255, 0, 255, 0)
//     (40,5)→(24,3) ⇒                    (0, 0, 0,  85)
//     (80,5)→(49,3) ⇒                    (0, 255, 255, 255)
//     (80,80)→(49,49)⇒                    (255, 255, 0, 255)
//   三档 alpha 的行数（26 / 26 / 53 行 × 105 列 = 2730 / 2730 / 5565）只断"每档都成规模存在"
//   （下限 2000/2000/4000），不断死边界行数 ⇒ 抗 ±1 行的取整漂移；精确值由上面 5 个点位钉。
//
// 判别取证（工单 F-3："== 4 ⇒ == 5 必须红"）实测结论：
//   字面形态改 `bands == 4` 为 `bands == 5` ⇒ alpha 恒 255 ⇒ 红在"三档 alpha 齐全"与
//   (5,5)/(5,80)/(40,5) 三个点位；提变量形态把 4 提成常量再改成 5 ⇒ 同一批断言红。
//   另两条**顺带**被钉住的实现错误：band4 当颜色用（R 变成 85 ⇒ 红在 R 的取值集合）、
//   通道映射整体右移（B 取到 band4 ⇒ 红在 B 的取值集合）。
// add_alpha 两档都跑：源不含 NoData ⇒ dstalpha 在覆盖区内恒 255，而 alpha 字节仍必须取
//   band4 的拉伸值 ⇒ 这一档顺带钉住"bands==4 时 band4 的 alpha **压过** dstalpha"的口径。
TEST_F(PluginTest, TileFourBandSourcePutsBandFourIntoAlpha) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;   // 不声明 NoData
    std::vector<float> band1(static_cast<size_t>(64) * 64, 0.0f);
    std::vector<float> band2(static_cast<size_t>(64) * 64, 0.0f);
    std::vector<float> band3(static_cast<size_t>(64) * 64, 0.0f);
    std::vector<float> band4(static_cast<size_t>(64) * 64, 0.0f);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const size_t i = static_cast<size_t>(y) * 64 + x;
            const bool left = (x < 32);
            const bool north = (y < 32);
            band1[i] = left ? 0.0f : 200.0f;
            band2[i] = north ? 0.0f : 200.0f;
            band3[i] = (left != north) ? 200.0f : 0.0f;
            band4[i] = (y < 16) ? 51.0f : (y < 32 ? 102.0f : 204.0f);
        }
    }
    const std::string input = t18WriteBandsRaster(
        getTestDir(), "t18_rgba4_input.tif", 64, 64,
        std::vector<std::vector<float>>{band1, band2, band3, band4}, options);
    ASSERT_FALSE(input.empty());

    // 瓦片内 (行, 列) → 期望 (R,G,B,A)（算术出处见头注释）
    struct Spot { int row; int col; int r; int g; int b; int a; };
    const std::vector<Spot> spots = {
        { 5,   5,   0,   0,   0,   0},
        { 5,  80, 255,   0, 255,   0},
        {40,   5,   0,   0,   0,  85},
        {80,   5,   0, 255, 255, 255},
        {80,  80, 255, 255,   0, 255},
    };

    for (int pass = 0; pass < 2; ++pass) {
        const bool addAlpha = pass == 0;
        const fs::path output = getTestDir() /
            (addAlpha ? "t18_rgba4_on_output" : "t18_rgba4_off_output");
        fs::remove_all(output);
        auto params = t17BaseTileParams(input, utf8PathString(output));
        params["min_zoom"] = 8;
        params["max_zoom"] = 8;
        params["resampling"] = std::string("nearest");
        params["add_alpha"] = addAlpha;

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << "add_alpha=" << addAlpha << ": " << result.message;
        const auto files = t17CollectTileFiles(output);
        ASSERT_EQ(files.size(), 1u)
            << "add_alpha=" << addAlpha << " z=8 夹具应产出单瓦片，实际 ["
            << t17JoinFiles(files) << "]";
        const T18TileBands tile = t18ReadTile(output / fs::path(*files.begin()));
        ASSERT_TRUE(tile.ok());
        ASSERT_EQ(tile.bands, 4) << "add_alpha=" << addAlpha << " 的 4→RGBA 容器口径";

        // ① 三个颜色波段各自只取 {0,255}：证明 band4 **没有**被当成颜色用，
        //    也没有发生"通道整体右移"（那会把 85 带进 R 或 B）。
        for (int band = 1; band <= 3; ++band) {
            const T18OpaqueStats st = t18StatsInRect(tile, band, 0, 0, 105, 105);
            ASSERT_GT(st.count, 0);
            EXPECT_EQ(st.minValue, 0) << "add_alpha=" << addAlpha << " band" << band
                                     << " 的暗档不是 0（min=" << st.minValue << "）⇒ 85/255 串进颜色了";
            EXPECT_EQ(st.maxValue, 255) << "add_alpha=" << addAlpha << " band" << band
                                        << " 的亮档不是 255（max=" << st.maxValue << "）";
            int odd = 0;
            for (int y = 0; y < 105; ++y) {
                for (int x = 0; x < 105; ++x) {
                    const uchar v = tile.data[static_cast<size_t>(band) - 1][
                        static_cast<size_t>(y) * tile.width + x];
                    if (v != 0 && v != 255) ++odd;
                }
            }
            EXPECT_EQ(odd, 0) << "add_alpha=" << addAlpha << " band" << band
                              << " 出现了非 {0,255} 的像元数 " << odd;
        }

        // ② 第 4 波段必须真的来自源 band4：三档 {0,85,255} 都在，且**不是**常量 255。
        const T18OpaqueStats alpha = t18StatsInRect(tile, 4, 0, 0, 105, 105);
        ASSERT_GT(alpha.count, 0);
        EXPECT_EQ(alpha.minValue, 0)
            << "add_alpha=" << addAlpha << " 的 alpha 下界不是 0 ⇒ 源 band4 的 51 档没被归一到全透明"
            << "（min=" << alpha.minValue << "）";
        EXPECT_EQ(alpha.maxValue, 255) << "add_alpha=" << addAlpha << " alpha 上界 " << alpha.maxValue;
        int nTransp = 0, nMid = 0, nOpaque = 0;
        for (int y = 0; y < 105; ++y) {
            for (int x = 0; x < 105; ++x) {
                const uchar a = tile.data[3][static_cast<size_t>(y) * tile.width + x];
                if (a == 0) ++nTransp; else if (a == 85) ++nMid; else if (a == 255) ++nOpaque;
            }
        }
        EXPECT_GE(nTransp, 2000)
            << "add_alpha=" << addAlpha << " 源 band4=51 那一档没被归一成 alpha=0（像元数 "
            << nTransp << "）⇒ bands==4 分支又变成常量 255";
        EXPECT_GE(nMid, 2000)
            << "add_alpha=" << addAlpha << " 源 band4=102 的**部分透明**没落到 85（像元数 "
            << nMid << "）⇒ 口径②（alpha 吃整幅拉伸）没被实现";
        EXPECT_GE(nOpaque, 4000) << "add_alpha=" << addAlpha << " alpha=255 档像元数 " << nOpaque;

        // ③ 五个点位的逐像元精确值（R,G,B,A 四个通道一起钉）。
        for (const auto& s : spots) {
            const size_t i = static_cast<size_t>(s.row) * tile.width + s.col;
            EXPECT_EQ(tile.data[0][i], static_cast<uchar>(s.r))
                << "add_alpha=" << addAlpha << " 像元(" << s.row << "," << s.col << ") 的 R";
            EXPECT_EQ(tile.data[1][i], static_cast<uchar>(s.g))
                << "add_alpha=" << addAlpha << " 像元(" << s.row << "," << s.col << ") 的 G";
            EXPECT_EQ(tile.data[2][i], static_cast<uchar>(s.b))
                << "add_alpha=" << addAlpha << " 像元(" << s.row << "," << s.col << ") 的 B";
            EXPECT_EQ(tile.data[3][i], static_cast<uchar>(s.a))
                << "add_alpha=" << addAlpha << " 像元(" << s.row << "," << s.col << ") 的 A"
                << "（期望来自源 band4）";
        }
    }
}

// T18 fix round 2 F-1（复审 I-1，**默认路径**上的亮度一致性）：同一含 NoData 的**单波段**源，
// `add_alpha=true` 与 `false` 两档产出的**颜色波段必须逐像元相同**。
//
// 工单原话点名现有 #315 不算钉住：它断的是"透明度可区分"，其颜色断言（洞带 G=0/R=255）
// 在**两种机理下都成立** ⇒ 错实现照样绿。本用例断的是**逐像元相等**，错实现下必红。
//
// 缺陷机理（复审真实数据实测，`cutting_plugin.cpp` 的 F-1 段已把算术写进注释）：
//   add_alpha=true ⇒ warp 带 `-dstalpha` ⇒ 色波段**不再声明 NODATA**（S9 的盘上事实）
//   ⇒ 被遮掉的像元以**写入值 0** 进整幅统计 ⇒ 拉伸下界被 0 顶掉 ⇒ 同一个源值映射成不同字节。
//   复审的数字：源 Min=391/Max=2729 ⇒ alpha=false 区间 (391,2729) 给 255*(1776-391)/2338=151.2，
//   alpha=true 区间 (0,2729) 给 255*1776/2729=165.9 ⇒ 与 gdalinfo 实测 166/151 逐字节吻合。
//   而 add_alpha 的默认值是 **true** ⇒ 这是默认路径，不是边缘组合。
//
// 夹具算术（64×64@1000 m、origin (0,0)、EPSG:3857、**声明** nodata=-9999、resampling=nearest、
//   z=8 单瓦片、覆盖子矩形 [0,105)²）：
//     源列 0..31  → 50        源列 32..47 → 200       源列 48..63 → -9999（NoData）
//   ⇒ 有效像元的值域是 **(50,200)**（两档都必须得到这个区间，否则亮度就随 add_alpha 变）。
//   INTER_NEAREST 的取整式 `src = floor((t+0.5)*64/105)` ⇒
//     瓦片列 0..51 → 源列 0..31（值 50）；52..78 → 源列 32..47（值 200）；79..104 → 源列 ≥48（NoData）
//     （t=51 → 31.6→31 ✓，t=52 → 32.0→32 ✓，t=78 → 47.8→47 ✓，t=79 → 48.2→48 ✓）
//   ⇒ 覆盖区内 不透明 ≈ 79×105 = 8295 像元、透明 ≈ 26×105 = 2730 像元（两档同一批像元）。
//   正确实现的字节：50 → 255*(50-50)/150 = **0**、200 → 255*(200-50)/150 = **255**（整除）。
//   F-1 之前 add_alpha=true 的字节：区间被 0 顶成 (0,200) ⇒ 50 → 255*50/200 = 63.75 → **63**、
//     200 → 255 ⇒ 与 alpha=false 档**逐像元不等** ⇒ 下面 EXPECT_EQ(diffRGBPixels, 0u) 红，
//     且 EXPECT_EQ(opaque.minValue, 0) 红在 63。这就是工单要的判别。
TEST_F(PluginTest, TileColorBandsDoNotDependOnAddAlpha) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    options.hasNoData = true;
    options.noData = -9999.0;
    std::vector<float> data(static_cast<size_t>(64) * 64, 50.0f);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const size_t i = static_cast<size_t>(y) * 64 + x;
            data[i] = (x < 32) ? 50.0f : (x < 48 ? 200.0f : -9999.0f);
        }
    }
    const std::string input =
        gis::tests::writeFloatRaster(getTestDir(), "t18_alpha_color_input.tif", 64, 64, data, options);
    ASSERT_FALSE(input.empty());

    std::vector<T18TileBands> collected;
    std::vector<std::set<std::string>> collectedFiles;
    for (int pass = 0; pass < 2; ++pass) {
        const bool addAlpha = pass == 0;
        const fs::path output = getTestDir() /
            (addAlpha ? "t18_alpha_color_on_output" : "t18_alpha_color_off_output");
        fs::remove_all(output);
        auto params = t17BaseTileParams(input, utf8PathString(output));
        params["min_zoom"] = 8;
        params["max_zoom"] = 8;
        params["resampling"] = std::string("nearest");
        params["add_alpha"] = addAlpha;

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << "add_alpha=" << addAlpha << ": " << result.message;
        const auto files = t17CollectTileFiles(output);
        ASSERT_EQ(files.size(), 1u) << "add_alpha=" << addAlpha << " 实际 ["
                                    << t17JoinFiles(files) << "]";
        const T18TileBands tile = t18ReadTile(output / fs::path(*files.begin()));
        ASSERT_TRUE(tile.ok());
        ASSERT_EQ(tile.bands, 4) << "add_alpha=" << addAlpha << " 的容器口径（#298 同款锚）";
        collected.push_back(tile);
        collectedFiles.push_back(files);

        // 每档自己的绝对正确性（防"两档一起错 ⇒ 相等断言白绿"这种同型错误）：
        //   不透明像元上的亮度必须取到 0（源值 50）与 255（源值 200）⇒ 区间只能是 (50,200)。
        const T18OpaqueStats opaqueR = t18StatsOverOpaque(tile, 1);
        ASSERT_GT(opaqueR.count, 500) << "add_alpha=" << addAlpha << " 不透明像元太少，夹具失效";
        EXPECT_EQ(opaqueR.minValue, 0)
            << "add_alpha=" << addAlpha << " 档里源值 50 没被映射成 0（min=" << opaqueR.minValue
            << "）⇒ 整幅统计的区间下界被别的值顶掉了（F-1 的形态：被 -dstalpha 遮掉的像元"
            << "以写入值 0 进统计 ⇒ 区间 (0,200) ⇒ 50 会映成 63）";
        EXPECT_EQ(opaqueR.maxValue, 255)
            << "add_alpha=" << addAlpha << " 档里源值 200 没被映射成 255（max="
            << opaqueR.maxValue << "）";
        // 夹具必须真的有 NoData（否则"逐像元相同"会被"两档都没有洞"这种退化实现满足）。
        int transparent = 0;
        for (int y = 0; y < 105; ++y) {
            for (int x = 0; x < 105; ++x) {
                if (tile.data[3][static_cast<size_t>(y) * tile.width + x] == 0) ++transparent;
            }
        }
        EXPECT_GT(transparent, 500)
            << "add_alpha=" << addAlpha << " 覆盖区内的透明像元数 " << transparent
            << " ⇒ NoData 洞没被遮出来，夹具失效（期望 ≈2730）";
    }

    // 主判别：**颜色波段逐像元相同**（整张瓦片、三个通道都算，不看 alpha）。
    const T18TileBands& on = collected[0];
    const T18TileBands& off = collected[1];
    EXPECT_EQ(collectedFiles[0], collectedFiles[1]) << "两档交付的瓦片集合都不同，谈不上逐像元相同";
    ASSERT_EQ(on.width, off.width);
    ASSERT_EQ(on.height, off.height);
    ASSERT_EQ(on.bands, off.bands);
    const size_t pixels = static_cast<size_t>(on.width) * static_cast<size_t>(on.height);
    for (int band = 1; band <= 3; ++band) {
        size_t diff = 0;
        int firstDiffX = -1, firstDiffY = -1, onVal = -1, offVal = -1;
        for (size_t i = 0; i < pixels; ++i) {
            const uchar a = on.data[static_cast<size_t>(band) - 1][i];
            const uchar b = off.data[static_cast<size_t>(band) - 1][i];
            if (a != b) {
                if (diff == 0) {
                    firstDiffX = static_cast<int>(i % static_cast<size_t>(on.width));
                    firstDiffY = static_cast<int>(i / static_cast<size_t>(on.width));
                    onVal = a; offVal = b;
                }
                ++diff;
            }
        }
        EXPECT_EQ(diff, 0u)
            << "add_alpha 改了颜色波段 band" << band << "：不等的像元数 " << diff
            << "，首个 (" << firstDiffY << "," << firstDiffX << ") true=" << onVal
            << " false=" << offVal
            << " ⇒ 亮度随 add_alpha 变（复审 I-1），Produces 承诺的\"同一地物亮度一致\"不成立";
    }
}

// T18-F3（fix round 3 / 复审 round 2 §I-1-new）：F-1 的 alpha 掩膜过滤**必须逐波段生效**。
//
// 这条用例的存在理由（复审用探针用例实测的）：
//   把 `cutting_plugin.cpp` 的过滤判据写成"只对 band 1 过滤、band 2..n 不过滤"
//   （`if (b == 1 && alphaBuf[i] <= 0.0f) continue;`）⇒ 当时 29/29 **全绿**。
//   原因是现有夹具里没有"能让 band 2..n 的过滤结果可观测"的形状：
//     #319 是单波段源 ⇒ 只驱动 b==1；#315 是 3 波段但洞只在**一条**波段里 ⇒
//     洞带像元 alpha>0 ⇒ 过滤不动它们，而它没有全波段同洞的像元 ⇒ 掩膜一条都没滤掉。
//
// 判别波段为什么必须是 band 3（本轮实测出的**等价类**，不是偷懒选型）：
//   -dstalpha 在"某波段自己 NoData、其余波段有效"的像元上会把**写入值 0** 留在该色波段里
//   （盘上事实见下面"夹具算术"的字节推导：进这条换算的是 -dstalpha 写进去的 **0**，不是源 NoData
//    原值 -9999 ⇒ 区间下界是 0。#315 的头注释现在写的也是这个机理（min=0 外加一条反证），
//    cutting_plugin.cpp 的 M-16 段还留着"原文写成 -9999 参与、机理错了"的更正记录。这一细节不影响
//    #315 的判别力：洞带绿道=0 在两种写法下都成立，所以那条断言本来就分不出来）。
//   ⇒ 只要某波段有"自己的洞"（alpha>0 的洞），它的像元集合里**本来就含 0**，
//     于是"过不过滤全掩膜像元（值也是 0）"对它是**等价**的 ⇒ 该波段看不见这条修复。
//   ⇒ 能看见这条修复的波段必须满足：它的 NoData 像元**全部**落在"三条波段同时 NoData"
//     （alpha==0）的区域里，且它自己的有效值域不含 0。本用例把 band 3 造成这个形状，
//     band 1/band 2 保留各自不同位置的单波段洞（⇒ 它们同时把上面这条等价类钉在断言里：
//     它们的产物字节对 (a) 变异**不敏感**，见下面 hist 的字面期望）。
//
// 夹具算术（64×64@1000 m、origin (0,0)、EPSG:3857、`t18WriteBandsRaster` ⇒ NODATA=-9999
//   按 `applyFixtureHeader` 声明到**全部三条**波段、resampling=nearest、z=8 单瓦片、
//   覆盖子矩形 [0,105)²；瓦片列↔源列的取整边界与 #319 同一张表：
//   源列 0..31→瓦片列 0..52（53 列）、32..47→53..78（26 列）、48..63→79..104（26 列））：
//     三条波段各自的**有效值域互不相同**：band1 (100,400)、band2 (200,500)、band3 (300,600)。
//     洞的位置也互不相同：
//       A = 源 x∈[0,8)  且 y<32   ⇒ 只有 band1 在这里 NoData（左上角块）
//       B = 源 x∈[8,16) 且 y>=32  ⇒ 只有 band2 在这里 NoData（下半条）
//       C = 源 x∈[48,64) 全行      ⇒ 三条波段同时 NoData（右缘带，band3 的洞只在这里）
//     ⇒ alpha 口径：true 档掩膜 = C ⇒ 不透明 105²−26×105 = 8295、透明 2730；
//                  false 档 = 任一波段 NoData 即遮 ⇒ A∪B∪C ⇒ 不透明 6877、透明 4148。
//     ⇒ 字节推导（`stretchByte` = clamp(255*(v-min)/(max-min)) 截断取整）：
//        true  band1：像元集合含 A 的写入值 0 ⇒ 区间 (0,400) ⇒ 100→**63**、400→**255**、A→**0**
//        true  band2：像元集合含 B 的写入值 0 ⇒ 区间 (0,500) ⇒ 200→**102**、500→**255**、B→**0**
//        true  band3：C 被掩膜滤掉 ⇒ 区间 **(300,600)** ⇒ 300→**0**、600→**255**
//        false 三条波段同走 GDAL 精算（跳过声明的 NODATA）⇒ 区间各自 (100,400)/(200,500)/
//               (300,600) ⇒ 有效低档→**0**、高档→**255**，被遮的像元整像素清零 ⇒ 也是 0。
//     ⇒ 计数（105×105 内，全部与实测逐格核对过）：A=742、B=676、低档块 5565、高档块 2730，
//       5565−742=4823（band1 的低档数）、5565−676=4889（band2 的低档数）、
//       false 档不透明低档 5565−742−676=4147。
//
// 判别取证（工单 fix-18-round3-findings.md 的 (a)）：
//   变异 (a) "只对 band 1 过滤" ⇒ band3 的像元集合被塞进 C 的写入值 0 ⇒ 区间变 (0,600)
//   ⇒ 300→**127** ⇒ 下面三条 band3 断言同时红：hist 期望 {0:5565,255:2730} 变 {127:5565,
//   255:2730}、minValue 0→127、两档逐像元 diff 0→5565。band1/band2 的字节**不变**（等价类）。
//   变异 (b) "完全不过滤"（退回 round 2 改前）⇒ band3 同样红，且 #319 交叉验证也红。
TEST_F(PluginTest, TileAlphaMaskFilterAppliesToEveryBand) {
    auto* p = mgr_.find("cutting");
    ASSERT_NE(p, nullptr);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.originX = 0.0; options.originY = 0.0; options.pixelSize = 1000.0;
    options.hasNoData = true;
    options.noData = -9999.0;

    const size_t pixels = static_cast<size_t>(64) * 64;
    std::vector<float> band1(pixels, 0.0f), band2(pixels, 0.0f), band3(pixels, 0.0f);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const size_t i = static_cast<size_t>(y) * 64 + x;
            const bool allHole = (x >= 48);            // C：三条波段同时 NoData
            band1[i] = allHole ? -9999.0f : (x < 32 ? 100.0f : 400.0f);
            band2[i] = allHole ? -9999.0f : (x < 32 ? 200.0f : 500.0f);
            band3[i] = allHole ? -9999.0f : (x < 32 ? 300.0f : 600.0f);
            if (!allHole && x < 8 && y < 32) band1[i] = -9999.0f;              // A：只有 band1
            if (!allHole && x >= 8 && x < 16 && y >= 32) band2[i] = -9999.0f;  // B：只有 band2
        }
    }
    const std::string input = t18WriteBandsRaster(
        getTestDir(), "t18_f3_bands_input.tif", 64, 64,
        std::vector<std::vector<float>>{band1, band2, band3}, options);
    ASSERT_FALSE(input.empty());

    // 不透明像元上的字节直方图（整张瓦片；未覆盖区 alpha==0 ⇒ 天然不计入）。
    auto opaqueByteHist = [](const T18TileBands& t, int band) {
        std::map<int, int> hist;
        const std::vector<uchar>& value = t.data[static_cast<size_t>(band) - 1];
        const std::vector<uchar>& alpha = t.data[t.data.size() - 1];
        for (size_t i = 0; i < alpha.size(); ++i) {
            if (alpha[i] == 0) continue;
            ++hist[static_cast<int>(value[i])];
        }
        return hist;
    };
    auto transparentCount = [](const T18TileBands& t) {
        // 只在**覆盖子矩形** [0,105)² 内数（瓦片是 256×256，覆盖缘之外本来全透明，
        // 与 #315/#319 的取窗口口径一致）。
        int n = 0;
        const std::vector<uchar>& alpha = t.data[t.data.size() - 1];
        for (int y = 0; y < 105; ++y) {
            for (int x = 0; x < 105; ++x) {
                if (alpha[static_cast<size_t>(y) * static_cast<size_t>(t.width) + x] == 0) ++n;
            }
        }
        return n;
    };

    // 两档的期望直方图（全部按上面"夹具算术"推死，用例里不调被测函数反算）。
    const std::map<int, int> expOn1{{0, 742}, {63, 4823}, {255, 2730}};
    const std::map<int, int> expOn2{{0, 676}, {102, 4889}, {255, 2730}};
    const std::map<int, int> expOn3{{0, 5565}, {255, 2730}};
    const std::map<int, int> expOffLow{{0, 4147}, {255, 2730}};
    // 两档各自的透明度（夹具完整性：C 只有 2730 ⇒ A、B 也必须真的洞出来）。
    const std::map<bool, int> expTransparent{{true, 2730}, {false, 4148}};

    std::vector<T18TileBands> collected;
    std::vector<std::set<std::string>> collectedFiles;
    for (int pass = 0; pass < 2; ++pass) {
        const bool addAlpha = pass == 0;
        const fs::path output = getTestDir() /
            (addAlpha ? "t18_f3_on_output" : "t18_f3_off_output");
        fs::remove_all(output);
        auto params = t17BaseTileParams(input, utf8PathString(output));
        params["min_zoom"] = 8;
        params["max_zoom"] = 8;
        params["resampling"] = std::string("nearest");
        params["add_alpha"] = addAlpha;

        const auto result = p->execute(params, progress_);
        ASSERT_TRUE(result.success) << "add_alpha=" << addAlpha << ": " << result.message;
        const auto files = t17CollectTileFiles(output);
        ASSERT_EQ(files.size(), 1u) << "add_alpha=" << addAlpha << " 实际 ["
                                    << t17JoinFiles(files) << "]";
        const T18TileBands tile = t18ReadTile(output / fs::path(*files.begin()));
        ASSERT_TRUE(tile.ok());
        ASSERT_EQ(tile.bands, 4) << "add_alpha=" << addAlpha << " 的容器口径（#298 同款锚）";
        collected.push_back(tile);
        collectedFiles.push_back(files);

        EXPECT_EQ(transparentCount(tile), expTransparent.at(addAlpha))
            << "add_alpha=" << addAlpha << " 档的透明度与夹具算术不符 ⇒ A/B/C 三块洞的形状变了";

        // 判别 1（本用例的主断言）：**band 3** 的归一化区间只由它自己的有效像元决定 ⇒
        // 全掩膜像元（值 0）不进区间 ⇒ 低档字节是 0 而不是 127。
        const std::map<int, int> h3 = opaqueByteHist(tile, 3);
        EXPECT_EQ(h3, addAlpha ? expOn3 : expOffLow)
            << "add_alpha=" << addAlpha << " 档 band3（洞只在全掩膜区里的那条波段）的字节直方图"
            << " 与推导不符 ⇒ 掩膜过滤在 band 3 上没生效（band3 区间被写成值 0 顶成 (0,600) "
            << "时低档会变 127，这正是复审 F1-D 探针的形态）";
        const T18OpaqueStats s3 = t18StatsOverOpaque(tile, 3);
        EXPECT_EQ(s3.minValue, 0) << "add_alpha=" << addAlpha << " 档 band3 低档字节 "
                                  << s3.minValue << "（期望 0）";
        EXPECT_EQ(s3.maxValue, 255) << "add_alpha=" << addAlpha << " 档 band3 高档字节 "
                                    << s3.maxValue << "（期望 255）";

        // 判别 2：band 1 / band 2 各自的区间由**自己**的像元集合决定（含它们自己单波段洞的
        // 写入值 0 ⇒ 低档 63 / 102，两档不同）。这两条把"漏的是 0 而不是 -9999"与"这两条
        // 波段对 (a) 变异是盲区"同时钉死：若实现改成"整幅共用一条区间"，band1/band2 的
        // 63/102 会同时变成同一个值 ⇒ 这里红。
        if (addAlpha) {
            EXPECT_EQ(opaqueByteHist(tile, 1), expOn1)
                << "band1 的字节直方图变了：它的洞 A 里的写入值 0 进区间 ⇒ (0,400) ⇒ 低档 63";
            EXPECT_EQ(opaqueByteHist(tile, 2), expOn2)
                << "band2 的字节直方图变了：它的洞 B 里的写入值 0 进区间 ⇒ (0,500) ⇒ 低档 102";
        } else {
            // false 档三条波段的低档一律 0（没有 0 顶区间）⇒ 与 true 档的 63/102 成对，
            // 这两条断言就是"亮度仍随 add_alpha 变"的那一半盘上事实（等价类，见头注释）。
            EXPECT_EQ(opaqueByteHist(tile, 1), expOffLow) << "false 档 band1";
            EXPECT_EQ(opaqueByteHist(tile, 2), expOffLow) << "false 档 band2";
        }
    }

    // 判别 3（#319 的多波段推广）：**band 3 在两档之间逐像元相同**；band 1/band 2 的
    // diff 也按字面钉住（4823 / 4889），它们是"单波段洞漏 0"的账，不是这条用例的承诺。
    const T18TileBands& on = collected[0];
    const T18TileBands& off = collected[1];
    EXPECT_EQ(collectedFiles[0], collectedFiles[1]) << "两档交付的瓦片集合都不同，谈不上逐像元相同";
    ASSERT_EQ(on.width, off.width);
    ASSERT_EQ(on.height, off.height);
    ASSERT_EQ(on.bands, off.bands);
    const size_t total = static_cast<size_t>(on.width) * static_cast<size_t>(on.height);
    // band1/band2 的 4823 / 4889 是**前提锁**（挂账 N81 的可观察面），不是"期望两档不等"的承诺：
    // 它锁的是"当前实现下 `-dstalpha` 只保护 alpha、颜色波段自己那 0.01 阈值洞仍会把写入值 0
    // 带进整幅统计区间"这一前提。彻底修 N81（弃用 `-dstalpha`、alpha 改由逐波段掩膜自己合成）
    // 之后 band1/band2 必须与 band3 一样**两档逐像元相同** ⇒ 届时把这两个字面量改成 0u（三条期望全等），
    // **不得删断言**——删了就等于把 N81 的回归面重新打开。
    // 同一前提还牵着另外四条字面期望，改 N81 时**必须一起**重推，只改 expDiff 会当场在这几条上撞红：
    // 洞像元一旦转成透明就从不透明直方图里掉出去 ⇒ 按上面"夹具算术"的计数，expOn1/expOn2/expOn3 全部
    // 回落到 expOffLow（不透明数 8295 → 6877，扣掉的正是 A=742 与 B=676），expTransparent 的 true 档
    // 也从 2730 变成与 false 档相同的 4148。
    // 处置模式同本文件的 现状锁（N51） 那一段（在 tests/ 里 grep 现状锁 就取到它）与两条
    // RejectedFeatureWrite 用例的头注释（grep RejectedFeatureWrite）：前提被修好就把期望改到新口径，
    // 不得整段删除。工单原话引的 #312 属逐波段量程那一族、盘上没有前提锁，N45/N55 也没有测试侧锚点，
    // 所以这里改成只指盘上取得到的两处。
    const std::map<int, size_t> expDiff{{1, 4823u}, {2, 4889u}, {3, 0u}};
    for (int band = 1; band <= 3; ++band) {
        size_t diff = 0;
        int firstDiffX = -1, firstDiffY = -1, onVal = -1, offVal = -1;
        for (size_t i = 0; i < total; ++i) {
            const uchar a = on.data[static_cast<size_t>(band) - 1][i];
            const uchar b = off.data[static_cast<size_t>(band) - 1][i];
            if (a != b) {
                if (diff == 0) {
                    firstDiffX = static_cast<int>(i % static_cast<size_t>(on.width));
                    firstDiffY = static_cast<int>(i / static_cast<size_t>(on.width));
                    onVal = a; offVal = b;
                }
                ++diff;
            }
        }
        EXPECT_EQ(diff, expDiff.at(band))
            << "add_alpha 两档在颜色波段 band" << band << " 上的不等像元数 " << diff
            << "，首个 (" << firstDiffY << "," << firstDiffX << ") true=" << onVal
            << " false=" << offVal
            << (band == 3
                    ? " ⇒ band3 的洞只落在 alpha==0 区，两档必须逐像元相同（复审 I-1-new）"
                      "（变异 (a) 就把这条打成 5565）"
                    : " ⇒ band1/band2 的自有洞把写入值 0 带进自己的区间 ⇒ 已登记的等价类，"
                      "推导见本用例头注释里'判别波段为什么必须是 band 3'那一段");
    }
}

// ════════════════════════════════════════════════════════════════════════════════════
// C3n-1：矢量输出的失败契约「先写同目录临时件 → 全部成功 → 显式关闭 → 替换目标」
// ════════════════════════════════════════════════════════════════════════════════════
// 契约出处：用户 2026-10-23 的裁定，原文登记在 docs/RELEASE.md 的 N137 条目。要治的病是
// 今天矢量动作的失败形态：输出文件在动作开头就被 drv->Create() 截断（未迁移的点还在 Create
// 之前 fs::remove 掉整个目标）⇒ 写出失败时**旧产物已经没了，新产物又不可用**。
// C3n-2d 收口时 24 个动作全部迁完，J5 的待迁移名单为空 ⇒ 契约已全覆盖；任何新增写出点都必须
// 自己接进 VectorStagedOutput，否则 J5 的"扫到 24 个 (文件,函数)"与"名单外命中"两条会红。
// 论域现读（C3n-2d 收口）：已迁 **24**、待迁 **0**；`drv->Create(` 全目录 **2** 处
// （helper 1 + 论域外 doRasterize 1）。
// 四条判据的分工（J5 是结构守卫，合计五条）：
//   J1 VectorBoundaryMidFailureKeepsOldProductAndLeavesNoTempFile —— 中途失败 ⇒ 旧产物逐字节不变 + 不留临时件
//   J2 VectorStagedOutputKeepsProductForm                        —— 成功路径的产物形态一个字没变（11 个动作）
//   J3 VectorStagedOutputLeavesNoJournalSidecar                   —— 替换之前真的关闭过数据集（无 -wal/-journal 边车，11 个动作）
//   J4 VectorOutputMustNotOverwriteInput                         —— output == input 硬失败且输入完好
//   J5 VectorVectorWritePointsAreStagedOrListed                  —— 迁移名单（已迁移 == 24 / 待迁移 == 0）

namespace {

// 目标目录里的**普通文件**清单（升序）；目录不存在返回空。
// WHY 列清单而不是 exists(某个名)：工单 J1 明确"不要只 exists() 一个名"——临时件机制最坏的
// 泄漏形态是"多出一个谁都不认识的文件"，只有前后对撞整份清单才看得见它。
std::vector<std::string> c3n1ListFiles(const fs::path& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) {
        return names;
    }
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code secer;
        if (it->is_regular_file(secer) && !secer) {
            names.push_back(it->path().filename().u8string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string c3n1ReadWholeFile(const fs::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

// 目录内每个文件的**全部字节**（文件名 → 内容）。工单说"记 md5 再比"，这里直接比字节：
// md5 只是字节的代理，全字节比较是它的严格上界，且红了能顺带报出差异位置。
std::map<std::string, std::string> c3n1CollectBytes(const fs::path& dir) {
    std::map<std::string, std::string> out;
    for (const auto& name : c3n1ListFiles(dir)) {
        out[name] = c3n1ReadWholeFile(dir / fs::u8path(name));
    }
    return out;
}

// 两段字节的差异描述（空串 = 完全相同）。不打印内容本身：栅格/二进制产物动辄几十 KB，
// 把整段吐进 gtest 日志既读不了，也会把真正的判点淹没。
std::string c3n1ByteDiff(const std::string& expected, const std::string& actual) {
    if (expected == actual) {
        return {};
    }
    std::size_t pos = 0;
    const std::size_t common = std::min(expected.size(), actual.size());
    while (pos < common && expected[pos] == actual[pos]) {
        ++pos;
    }
    return "首个不等字节 @ " + std::to_string(pos) + "（改前 " + std::to_string(expected.size()) +
        " 字节 / 改后 " + std::to_string(actual.size()) + " 字节）";
}

std::string c3n1JoinNames(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& name : names) {
        out += " " + name;
    }
    return out;
}

// 把"旧产物逐字节不变 + 目录里不多一个文件 + 没有 *.gis-tmp-* 残留"三件事一次说完。
// J1 与 J4 共用：两者的差别只在"这一次为什么会失败"，违约形态是同一族。
void c3n1ExpectDirectoryUntouched(const fs::path& dir,
                                  const std::vector<std::string>& beforeNames,
                                  const std::map<std::string, std::string>& beforeBytes,
                                  const char* what) {
    const auto afterNames = c3n1ListFiles(dir);
    const auto afterBytes = c3n1CollectBytes(dir);

    std::string residue;
    for (const auto& name : afterNames) {
        if (name.find(".gis-tmp-") != std::string::npos) {
            residue += " " + name;
        }
    }
    EXPECT_TRUE(residue.empty())
        << what << "：目标目录里残留了临时件（*.gis-tmp-*）：" << residue
        << "；目录 = " << dir.u8string();

    EXPECT_EQ(afterNames, beforeNames)
        << what << "：目标目录的文件清单前后不一致"
        << "；改前 =" << c3n1JoinNames(beforeNames)
        << "；改后 =" << c3n1JoinNames(afterNames)
        << "；目录 = " << dir.u8string();

    for (const auto& [name, bytes] : beforeBytes) {
        const auto after = afterBytes.find(name);
        if (after == afterBytes.end()) {
            ADD_FAILURE() << what << "：改前存在的文件不见了 —— " << name
                          << "（" << bytes.size() << " 字节）；目录 = " << dir.u8string();
            continue;
        }
        const std::string diff = c3n1ByteDiff(bytes, after->second);
        EXPECT_TRUE(diff.empty())
            << what << "：产物 " << name << " 的字节被改动 " << diff
            << "；目录 = " << dir.u8string();
    }
}

// 两只面的 GPKG 夹具 —— 与上面 writeTwoPolygonFixture 同一几何（非对称 L + 小矩形），
// 但落到用例自己的目录里（不与其它用例共享产物， remove_all 才敢整目录对撞）。
bool c3n1WriteTwoPolygonGpkg(const std::string& path) {
    auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!driver) {
        return false;
    }
    auto* ds = driver->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) {
        return false;
    }
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);
    auto* layer = ds->CreateLayer("shapes", srs.get(), wkbPolygon, nullptr);
    if (!layer) {
        GDALClose(ds);
        return false;
    }
    OGRFieldDefn nameField("name", OFTString);
    if (layer->CreateField(&nameField) != OGRERR_NONE) {
        GDALClose(ds);
        return false;
    }

    auto addPolygon = [&](const char* name,
                          const std::vector<std::pair<double, double>>& ring) {
        OGRPolygon poly;
        OGRLinearRing linearRing;
        for (const auto& point : ring) {
            linearRing.addPoint(point.first, point.second);
        }
        linearRing.addPoint(ring.front().first, ring.front().second);
        poly.addRing(&linearRing);
        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", name);
        feat->SetGeometry(&poly);
        const bool ok = layer->CreateFeature(feat) == OGRERR_NONE;
        OGRFeature::DestroyFeature(feat);
        return ok;
    };

    bool ok = addPolygon("A", {{0.0, 0.0}, {12.0, 0.0}, {12.0, 3.0},
                               {4.0, 3.0}, {4.0, 8.0}, {0.0, 8.0}});
    ok = addPolygon("B", {{20.0, 0.0}, {24.0, 0.0}, {24.0, 2.0}, {20.0, 2.0}}) && ok;
    GDALClose(ds);
    return ok;
}

// 同一只 L + 同一只小矩形的 Shapefile 版（J4 的 .shp 臂要的是"输入就是那个多文件集本身"，
// 只有真 Shapefile 输入才能把"输出覆盖输入"这条打到 .shp + 边车上）。
bool c3n1WritePolygonShapefile(const std::string& path) {
    auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
    if (!driver) {
        return false;
    }
    auto* ds = driver->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) {
        return false;
    }
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);
    auto* layer = ds->CreateLayer("shapes", srs.get(), wkbPolygon, nullptr);
    if (!layer) {
        GDALClose(ds);
        return false;
    }
    OGRFieldDefn nameField("name", OFTString);
    if (layer->CreateField(&nameField) != OGRERR_NONE) {
        GDALClose(ds);
        return false;
    }

    auto addPolygon = [&](const char* name,
                          const std::vector<std::pair<double, double>>& ring) {
        OGRPolygon poly;
        OGRLinearRing linearRing;
        for (const auto& point : ring) {
            linearRing.addPoint(point.first, point.second);
        }
        linearRing.addPoint(ring.front().first, ring.front().second);
        poly.addRing(&linearRing);
        auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feat->SetField("name", name);
        feat->SetGeometry(&poly);
        const bool ok = layer->CreateFeature(feat) == OGRERR_NONE;
        OGRFeature::DestroyFeature(feat);
        return ok;
    };

    bool ok = addPolygon("A", {{0.0, 0.0}, {12.0, 0.0}, {12.0, 3.0},
                               {4.0, 3.0}, {4.0, 8.0}, {0.0, 8.0}});
    ok = addPolygon("B", {{20.0, 0.0}, {24.0, 0.0}, {24.0, 2.0}, {20.0, 2.0}}) && ok;
    GDALClose(ds);
    return ok;
}

// 三点折线的 Shapefile 版（endpoints_extract / midpoints_extract 只收折线层；J4 的 .shp 臂
// 要在这两个动作上同样能造出"输入就是多文件集"的形态）。
bool c3n1WriteLineShapefile(const std::string& path) {
    auto* driver = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
    if (!driver) {
        return false;
    }
    auto* ds = driver->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) {
        return false;
    }
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);
    auto* layer = ds->CreateLayer("shapes", srs.get(), wkbLineString, nullptr);
    if (!layer) {
        GDALClose(ds);
        return false;
    }
    OGRFieldDefn nameField("name", OFTString);
    if (layer->CreateField(&nameField) != OGRERR_NONE) {
        GDALClose(ds);
        return false;
    }
    OGRLineString line;
    line.addPoint(0, 0);
    line.addPoint(5, 5);
    line.addPoint(10, 0);
    auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
    feat->SetField("name", "A");
    feat->SetGeometry(&line);
    const bool ok = layer->CreateFeature(feat) == OGRERR_NONE;
    OGRFeature::DestroyFeature(feat);
    GDALClose(ds);
    return ok;
}

// 三只点的折线 GPKG 夹具（vertices_extract 的既有执行用例 VectorVerticesExtractExecution
// 用的就是这条 (0,0)-(5,5)-(10,0) ⇒ 这里复用同一份数据，期望 3 个顶点、包框 (0,0,10,5)）。
bool c3n1WriteThreePointLineGpkg(const std::string& path) {
    auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!driver) {
        return false;
    }
    auto* ds = driver->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) {
        return false;
    }
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);
    auto* layer = ds->CreateLayer("shapes", srs.get(), wkbLineString, nullptr);
    if (!layer) {
        GDALClose(ds);
        return false;
    }
    OGRFieldDefn nameField("name", OFTString);
    if (layer->CreateField(&nameField) != OGRERR_NONE) {
        GDALClose(ds);
        return false;
    }
    OGRLineString line;
    line.addPoint(0, 0);
    line.addPoint(5, 5);
    line.addPoint(10, 0);
    auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
    feat->SetField("name", "A");
    feat->SetGeometry(&line);
    const bool ok = layer->CreateFeature(feat) == OGRERR_NONE;
    OGRFeature::DestroyFeature(feat);
    GDALClose(ds);
    return ok;
}

// 两值栅格夹具（polygonize 用）：10×10 Byte，左半列 1 / 右半列 2，四连通下每个等值区各成
// 一个面 ⇒ 产物**手算恰好 2 个面**、包框 = 栅格范围 (100,190)-(110,200)。不设 NoData：
// GDALPolygonize 的掩膜只看 NoData 与 alpha，设了 0 当 NoData 也不影响本例（没有 0 像元），
// 但留空少一层前提。真值 1/2 让两条面各自可辨（DN 字段的两种取值）。
bool c3n1WriteTwoValueRaster(const std::string& path) {
    auto* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        return false;
    }
    auto* ds = driver->Create(path.c_str(), 10, 10, 1, GDT_Byte, nullptr);
    if (!ds) {
        return false;
    }
    double gt[6] = { 100.0, 1.0, 0.0, 200.0, 0.0, -1.0 };
    ds->SetGeoTransform(gt);
    // 与 J2 其它臂同一个坐标系：polygonize 会把栅格投影抄进产物（srs 来自 GetProjectionRef），
    // 不设投影时产物没有 CRS，"产物 CRS 是 EPSG:3857"那条会红成夹具缺陷而不是产品缺陷。
    {
        OGRSpatialReference srs;
        srs.importFromEPSG(3857);
        char* wkt = nullptr;
        if (srs.exportToWkt(&wkt) == OGRERR_NONE && wkt) {
            ds->SetProjection(wkt);
            CPLFree(wkt);
        }
    }
    std::vector<uint8_t> data(100, 0);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            data[static_cast<size_t>(y) * 10 + static_cast<size_t>(x)] = (x < 5) ? 1 : 2;
        }
    }
    auto* band = ds->GetRasterBand(1);
    const bool ok = band->RasterIO(GF_Write, 0, 0, 10, 10, data.data(), 10, 10, GDT_Byte, 0, 0)
        == CE_None;
    GDALClose(ds);
    return ok;
}

// clip 的叠加层夹具：只盖住两多边形夹具里的 A（A 的外包框 (0,0)-(12,8) 那个方框），
// 与 B (20,0)-(24,2) 不相交 ⇒ clip 产物**手算恰好 1 个要素**、包框 (0,0)-(12,8)。
// A 整体落在方框内（与方框左/底边重合）⇒ 走的是 Contains 那条"原样 clone"支，
// 几何不做布尔运算 ⇒ 期望值不依赖 GEOS 的取整行为。
bool c3n1WriteClipOverlayGpkg(const std::string& path) {
    auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!driver) {
        return false;
    }
    auto* ds = driver->Create(path.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) {
        return false;
    }
    auto srs = std::make_unique<OGRSpatialReference>();
    srs->importFromEPSG(3857);
    auto* layer = ds->CreateLayer("overlay", srs.get(), wkbPolygon, nullptr);
    if (!layer) {
        GDALClose(ds);
        return false;
    }
    OGRPolygon poly;
    OGRLinearRing ring;
    ring.addPoint(0.0, 0.0);
    ring.addPoint(12.0, 0.0);
    ring.addPoint(12.0, 8.0);
    ring.addPoint(0.0, 8.0);
    ring.addPoint(0.0, 0.0);
    poly.addRing(&ring);
    auto* feat = OGRFeature::CreateFeature(layer->GetLayerDefn());
    feat->SetGeometry(&poly);
    const bool ok = layer->CreateFeature(feat) == OGRERR_NONE;
    OGRFeature::DestroyFeature(feat);
    GDALClose(ds);
    return ok;
}

fs::path c3n1FreshDir(const std::string& tag) {
    const fs::path dir = getTestDir() / fs::u8path("c3n1_" + tag);
    std::error_code ec;
    fs::remove_all(dir, ec);
    ec.clear();
    fs::create_directories(dir, ec);
    return dir;
}

// ".gpkg" → "gpkg"：夹具目录名里不要带点（点名目录带点会让 Windows 把它认成扩展名的一部分）。
std::string c3n1ExtName(const char* ext) {
    return std::string(ext + 1);
}

// 一次动作的结局，按"能否让用户拿到一句可读的失败"摊平成同一种形状。
// WHY 要把"抛出"单独记下来而不让它掀掉整条用例：J4 要判的是"动作必须在任何删除/创建**之前**
// 干净失败"，而今天（未迁移的实现）这条路径上抛的是 `fs::remove` 的 filesystem_error
//（Windows 共享冲突：输入还开着就被删）——**掀桌子本身就是要抓的违约**，但它会顺带把
// 同一条用例剩下的臂全部跳过 ⇒ 拿不到逐臂证据。摊平后每条臂都能给出自己的红/绿，
// "抛了" 与 "没拦下来" 与 "拦下来了但消息没点名" 三种形态在日志里是可区分的。
struct C3n1Run {
    bool threw = false;
    std::string throwMessage;
    gis::framework::Result result;

    std::string outcome() const {
        if (threw) {
            return "threw: " + throwMessage;
        }
        return (result.success ? "success: " : "fail: ") + result.message;
    }
};

C3n1Run c3n1RunAction(gis::framework::IGisPlugin* plugin,
                      const std::map<std::string, gis::framework::ParamValue>& params,
                      gis::core::ProgressReporter& progress) {
    C3n1Run run;
    try {
        run.result = plugin->execute(params, progress);
    } catch (const std::exception& e) {
        run.threw = true;
        run.throwMessage = e.what();
    }
    return run;
}

} // namespace

// ── J1：中途失败 ⇒ 旧产物逐字节不变 + 一个临时件都不留 ─────────────────────────────
// 载体是取消式 reporter（本文件上面 T17 那一段的 T17CancellableProgress，同一个 TU 内直接复用，
// 不需要产品代码里开任何测试缝）：第 3 次 onProgress 之后 isCancelled() 置真，
// 下一次 throwIfCancelled() 抛 gis::core::CancelledException。boundary 的三个固定进度点
//（0.1 / 0.2 / 0.35）都在 drv->Create 之后，抛出点落在要素写循环里（boundary 的循环体内
// 就有 throwIfCancelled，见 vector_plugin_boundary.inc 循环里那三处）⇒ 此刻目标已经被打开、
// 并且已经写进了至少一条要素，这正是"半截新产物"最容易落盘的时刻。
// 两条格式臂：GPKG 是单文件产物；ESRI Shapefile 是多文件集 ⇒ "边车逐个替换 + 旧边车要等
// 新件全部完工之后才清"这条（工单 §2.5）只有 .shp 臂能打到。
// ⚠ 本条在**未迁移的实现**上必红：旧代码在 drv->Create 之前就把目标整个 fs::remove 掉
//（16 个点走 removeExistingVectorOutput），且写的是真目标而不是临时件。
TEST_F(PluginTest, VectorBoundaryMidFailureKeepsOldProductAndLeavesNoTempFile) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr) << "plugin not loaded: vector";

    for (const char* ext : {".gpkg", ".shp"}) {
        const std::string tag = std::string("j1_boundary_") + c3n1ExtName(ext);
        const fs::path dir = c3n1FreshDir(tag);
        ASSERT_TRUE(fs::exists(dir)) << "夹具目录建不出来：" << dir.u8string();

        const std::string input = utf8PathString(dir / "shapes.gpkg");
        ASSERT_TRUE(c3n1WriteTwoPolygonGpkg(input)) << "夹具写入失败：" << input;

        const fs::path target = dir / fs::u8path(std::string("boundary_out") + ext);
        const std::string targetPath = utf8PathString(target);

        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("boundary");
        params["input"] = input;
        params["output"] = targetPath;

        // ① 先跑一次不取消的，把"改前就存在的完好旧产物"做出来。
        const auto first = p->execute(params, progress_);
        ASSERT_TRUE(first.success)
            << ext << "：前置产物没做出来，J1 无从谈起：" << first.message;
        ASSERT_TRUE(fs::exists(target)) << ext << "：前置产物文件不存在：" << targetPath;

        const auto beforeNames = c3n1ListFiles(dir);
        const auto beforeBytes = c3n1CollectBytes(dir);
        ASSERT_GE(beforeBytes.size(), 1u) << ext << "：前置产物目录是空的，夹具没生效";

        // ② 再跑同一次动作，这次中途被取消。
        const std::string whatStr = std::string("J1[") + ext + "] 中途取消";
        T17CancellableProgress cancelling(3);
        EXPECT_THROW(p->execute(params, cancelling), gis::core::CancelledException)
            << whatStr << "：动作没有按取消抛出 CancelledException ⇒ 取消缝没打到写循环";

        // ③④ 旧产物逐字节不变、目录前后一致、没有 *.gis-tmp-* 残留。
        c3n1ExpectDirectoryUntouched(dir, beforeNames, beforeBytes, whatStr.c_str());
    }
}

// ── J1 的同形第二臂（C3n-2a）：载体换成 convex_hull ──────────────────────────────────
// 工单 C3n-2a §3 要"先判据、后实现"：这条在**未迁移**的 doConvexHull 上必须先红，迁完之后转绿。
// 除动作名与产物名外逐字照抄 J1：同一个取消式 reporter（第 3 次进度回调后置取消）、同一份
// 双面 GPKG 夹具、同一组"旧产物字节不变 + 清单不变 + 无 *.gis-tmp-*"三段期望。
// WHY 只给这一个点打臂：本刀要迁的 8 个点全是同一种形状（单输入、无事务、单输出、同一个
// removeExistingVectorOutput 前删），逐个重打臂在 C3n-1 已经撞过一次回合上限；其余 7 个点的
// 红态由这一条 + J5 名单承重代证（工单 §3 明许）。
TEST_F(PluginTest, VectorConvexHullMidFailureKeepsOldProductAndLeavesNoTempFile) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr) << "plugin not loaded: vector";

    for (const char* ext : {".gpkg", ".shp"}) {
        const std::string tag = std::string("j1_convex_hull_") + c3n1ExtName(ext);
        const fs::path dir = c3n1FreshDir(tag);
        ASSERT_TRUE(fs::exists(dir)) << "夹具目录建不出来：" << dir.u8string();

        const std::string input = utf8PathString(dir / "shapes.gpkg");
        ASSERT_TRUE(c3n1WriteTwoPolygonGpkg(input)) << "夹具写入失败：" << input;

        const fs::path target = dir / fs::u8path(std::string("convex_hull_out") + ext);
        const std::string targetPath = utf8PathString(target);

        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string("convex_hull");
        params["input"] = input;
        params["output"] = targetPath;

        // ① 先跑一次不取消的，把"改前就存在的完好旧产物"做出来。
        const auto first = p->execute(params, progress_);
        ASSERT_TRUE(first.success)
            << ext << "：前置产物没做出来，本臂无从谈起：" << first.message;
        ASSERT_TRUE(fs::exists(target)) << ext << "：前置产物文件不存在：" << targetPath;

        const auto beforeNames = c3n1ListFiles(dir);
        const auto beforeBytes = c3n1CollectBytes(dir);
        ASSERT_GE(beforeBytes.size(), 1u) << ext << "：前置产物目录是空的，夹具没生效";

        // ② 再跑同一次动作，这次中途被取消（convex_hull 的三个固定进度点与 boundary 同形：
        // 0.1 / 0.2 / 0.35 都在建数据集之后 ⇒ 抛出点落在要素写循环里，目标已被截断/新建）。
        const std::string whatStr = std::string("J1-convex_hull[") + ext + "] 中途取消";
        T17CancellableProgress cancelling(3);
        EXPECT_THROW(p->execute(params, cancelling), gis::core::CancelledException)
            << whatStr << "：动作没有按取消抛出 CancelledException ⇒ 取消缝没打到写循环";

        // ③④ 旧产物逐字节不变、目录前后一致、没有 *.gis-tmp-* 残留。
        c3n1ExpectDirectoryUntouched(dir, beforeNames, beforeBytes, whatStr.c_str());
    }
}

// ── J4：output == input 必须硬失败，而且输入要完好无损 ─────────────────────────────
// 工单 §1 的说法是"16 个点在 drv->Create 之前 fs::remove(outputPath) ⇒ 输入文件被删掉"。
// C3n-2d 把动作数组从 16 扩到 **24**（追加 buffer / clip / dissolve / polygonize / convert /
// union / difference / intersect）；这 8 条新臂在旧实现上会红：旧 overlay 三点在 .shp 输出上
// 先按 stem fs::remove 输入边车、在 .gpkg 输出上直接 drv->Create 截断输入，且没有等价闸
// ⇒ J4 的"不许抛异常 / 必须失败且消息点名 / 目录逐字节不变"三条至少有一条不成立
//（旧码摘录见报告 §4，`git show e4cfd0d:src/plugins/vector/vector_plugin_overlay.inc`）。
// 现读复核（见 task-C3n1-report.md）：**删除动作确实在 Create 之前对着输入路径发**，但今天
// 在这三个单输入动作上它的实际结局不是"输入没了"，而是 **fs::remove 抛出 filesystem_error**
//（Windows 共享冲突：输入此刻还被本动作的 openVector(input, readOnly) 握在手里，而 GDAL 的
// VSI 打开不带 FILE_SHARE_DELETE）⇒ 异常从 plugin->execute 一路甩出去（CLI 侧由 executeGuarded
// 兜成 Result::fail + rc=1，插件 API 侧无人兜）。两种结局都违反"干净失败"这条契约。
// 全仓零保护（`grep -rn "fs::equivalent|output == input" src/` 现读只有 nearest / spatial_join
// 那两处，且都是输入 vs 输入）⇒ 本条在未迁移的实现上必红。
// 两种拼法都要打：逐字同一个路径，以及指向同一个文件的 dir\.\work<ext> 拼法
//（后者专打"拿字符串 == 当闸"那种假实现：字符串不同、文件相同，只有等价判定才拦得住）。
// .shp 臂 additionally 钉"输入本身是多文件集"的形态（删边车会连 .dbf/.prj 一起删）。
// 口径参照同插件 spatial_join 对"字段不存在"那条硬失败（Result::fail + 消息点名是哪件事），
// 不自创第三种说法。
TEST_F(PluginTest, VectorOutputMustNotOverwriteInput) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr) << "plugin not loaded: vector";

    // 24 个已迁移动作：射程与 J5 的 kStagedActions 相同（J5 认"文件+函数"，这里认 CLI 动作名）。
    // 每项带上让动作能走到 staged.start 入口闸的最小参数/夹具形态：
    //   lineLayer    —— endpoints_extract / midpoints_extract 只收折线层；
    //   secondKey    —— 第二个输入（nearest / spatial_join / clip / overlay 三点），
    //                   必须与 input 分开成两个夹具文件；
    //   where        —— filter 要求 --where 或 --extent 至少给一个；
    //   distance     —— buffer 的距离；
    //   dissolveField—— dissolve 的字段；
    //   outputFormat —— convert 的 format 参数（它的驱动按参数选，不看扩展名）；
    //   rasterInput  —— polygonize 的输入必须是栅格（格式臂用 .tif）。
    struct ActionArm {
        const char* action;
        bool lineLayer;
        const char* secondKey;
        const char* where;
        double distance;
        const char* dissolveField;
        const char* outputFormat;
        bool rasterInput;
    };
    const ActionArm actions[] = {
        { "boundary", false, nullptr, nullptr, 0.0, nullptr, nullptr, false },
        { "centroid", false, nullptr, nullptr, 0.0, nullptr, nullptr, false },
        { "convex_hull", false, nullptr, nullptr, 0.0, nullptr, nullptr, false },
        { "endpoints_extract", true, nullptr, nullptr, 0.0, nullptr, nullptr, false },
        { "envelope", false, nullptr, nullptr, 0.0, nullptr, nullptr, false },
        { "filter", false, nullptr, "name = 'A'", 0.0, nullptr, nullptr, false },
        { "geom_metrics", false, nullptr, nullptr, 0.0, nullptr, nullptr, false },
        { "interior_point", false, nullptr, nullptr, 0.0, nullptr, nullptr, false },
        { "midpoints_extract", true, nullptr, nullptr, 0.0, nullptr, nullptr, false },
        { "nearest", false, "nearest_vector", nullptr, 0.0, nullptr, nullptr, false },
        { "repair", false, nullptr, nullptr, 0.0, nullptr, nullptr, false },
        { "simplify", false, nullptr, nullptr, 0.0, nullptr, nullptr, false },
        { "singlepart", false, nullptr, nullptr, 0.0, nullptr, nullptr, false },
        { "sliver_remove", false, nullptr, nullptr, 0.0, nullptr, nullptr, false },
        { "spatial_join", false, "join_vector", nullptr, 0.0, nullptr, nullptr, false },
        { "vertices_extract", false, nullptr, nullptr, 0.0, nullptr, nullptr, false },
        // C3n-2d 追加 8 个动作（16 → 24）：
        { "buffer", false, nullptr, nullptr, 5.0, nullptr, nullptr, false },
        { "clip", false, "clip_vector", nullptr, 0.0, nullptr, nullptr, false },
        { "dissolve", false, nullptr, nullptr, 0.0, "name", nullptr, false },
        { "polygonize", false, nullptr, nullptr, 0.0, nullptr, nullptr, true },
        { "convert", false, nullptr, nullptr, 0.0, nullptr, "GPKG", false },
        { "union", false, "overlay_vector", nullptr, 0.0, nullptr, nullptr, false },
        { "difference", false, "overlay_vector", nullptr, 0.0, nullptr, nullptr, false },
        { "intersect", false, "overlay_vector", nullptr, 0.0, nullptr, nullptr, false },
    };

    // 动作数组本身也要承重：少一条不是"少跑一条"，而是"射程缩水"（J5 认 24 条，这条认 24 条，
    // 两边各数各的，任何一边掉队都红）。
    ASSERT_EQ(sizeof(actions) / sizeof(actions[0]), 24u)
        << "J4 的动作数组必须覆盖全部 24 个已迁移动作；少一条就是射程缩水";

    for (const auto& item : actions) {
        const char* action = item.action;
        const bool lineLayer = item.lineLayer;
        // polygonize 的输入是栅格：格式臂只有 GTiff（.tif）一种，两种拼法仍然保留。
        const std::vector<const char*> exts = item.rasterInput
            ? std::vector<const char*>{".tif"}
            : std::vector<const char*>{".gpkg", ".shp"};
        for (const char* ext : exts) {
            for (int spelling = 0; spelling < 2; ++spelling) {
                const std::string tag =
                    std::string("j4_") + action + "_" + c3n1ExtName(ext) +
                    (spelling == 0 ? "_same" : "_dotslash");
                const fs::path dir = c3n1FreshDir(tag);
                ASSERT_TRUE(fs::exists(dir)) << dir.u8string();

                const fs::path work = dir / fs::u8path(std::string("work") + ext);
                const std::string workPath = utf8PathString(work);
                const bool fixtureOk = item.rasterInput
                    ? c3n1WriteTwoValueRaster(workPath)
                    : ((std::string(ext) == ".gpkg")
                        ? (lineLayer ? c3n1WriteThreePointLineGpkg(workPath)
                                     : c3n1WriteTwoPolygonGpkg(workPath))
                        : (lineLayer ? c3n1WriteLineShapefile(workPath)
                                     : c3n1WritePolygonShapefile(workPath)));
                ASSERT_TRUE(fixtureOk) << "夹具写入失败：" << workPath;

                std::string secondPath;
                if (item.secondKey) {
                    const fs::path second = dir / fs::u8path(std::string("probe") + ext);
                    secondPath = utf8PathString(second);
                    const bool secondOk = (std::string(ext) == ".gpkg")
                        ? c3n1WriteTwoPolygonGpkg(secondPath)
                        : c3n1WritePolygonShapefile(secondPath);
                    ASSERT_TRUE(secondOk) << "第二个输入夹具写入失败：" << secondPath;
                }

                // output 的两种拼法：逐字同一个路径，或指向同一个文件的 dir\.\work<ext>。
                // 第二条臂的意义在于"字符串不同、文件相同"⇒ 它专打"用 == 比字符串"那种假闸；
                // 所以先钉住前提：两个串确实不等，否则这条臂会悄悄退化成第一条。
                std::string output = workPath;
                if (spelling == 1) {
                    output = utf8PathString(dir / "." / fs::u8path(std::string("work") + ext));
                    ASSERT_NE(output, workPath) << "拼法没造出差异，等价臂退化成同串臂";
                }

                const auto beforeNames = c3n1ListFiles(dir);
                const auto beforeBytes = c3n1CollectBytes(dir);
                ASSERT_GE(beforeBytes.size(), 1u) << "夹具目录是空的：" << dir.u8string();

                std::map<std::string, gis::framework::ParamValue> params;
                params["action"] = std::string(action);
                params["input"] = workPath;
                params["output"] = output;
                if (item.secondKey) {
                    params[item.secondKey] = secondPath;
                }
                if (item.where) {
                    params["where"] = std::string(item.where);
                }
                if (item.distance > 0.0) {
                    params["distance"] = item.distance;
                }
                if (item.dissolveField) {
                    params["dissolve_field"] = std::string(item.dissolveField);
                }
                if (item.outputFormat) {
                    params["format"] = std::string(item.outputFormat);
                }

                const std::string what = std::string("J4[") + action + ext +
                    (spelling == 0 ? "/same]" : "/dotslash]");

                const C3n1Run run = c3n1RunAction(p, params, progress_);

                EXPECT_FALSE(run.threw)
                    << what << "：动作没有干净失败而是**抛出了**异常（" << run.outcome()
                    << "）⇒ 删除/创建已经发生在本该拦住它的闸之前；契约要的是返回一个点名规则的"
                    << "失败结果，而不是把 filesystem_error 甩给调用方";

                EXPECT_FALSE(run.result.success)
                    << what << "：输出覆盖输入这件事没有被硬失败拦下来（返回 success，"
                    << "outcome = " << run.outcome() << "）";

                if (!run.threw && !run.result.success) {
                    EXPECT_NE(run.result.message.find("same file as input"), std::string::npos)
                        << what << "：失败消息没有点名『输出不能覆盖输入』这条规则，实际 message = "
                        << run.result.message;
                }

                c3n1ExpectDirectoryUntouched(dir, beforeNames, beforeBytes, what.c_str());
            }
        }
    }
}

// ── J4 双输入臂：output == 第二个输入 同样必须硬失败，两个输入都要完好 ───────────────
// nearest / spatial_join 的 `fs::equivalent(input, secondInput)` 判的是"输入 vs 输入"，
// 从来不是"输出 vs 输入"：把 --nearest_vector / --join_vector 指到输出路径，今天就会拿
// 第二个输入当目标重写。staged.start 的保护清单必须**两个输入都收**；只收 input 的实现在
// 这条臂上要么在 commit 换名时才失败（消息不点名规则），要么直接把第二个输入换掉。
// 形状与 J4 同：两格式 × 两拼法（逐字同一个路径 / dir\.\probe<ext>）。
TEST_F(PluginTest, VectorDoubleInputOutputMustNotOverwriteSecondInput) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr) << "plugin not loaded: vector";

    struct Case {
        const char* action;
        const char* secondKey;
    };
    const Case cases[] = {
        { "nearest", "nearest_vector" },
        { "spatial_join", "join_vector" },
    };

    for (const auto& item : cases) {
        for (const char* ext : {".gpkg", ".shp"}) {
            for (int spelling = 0; spelling < 2; ++spelling) {
                const std::string tag = std::string("j4_second_") + item.action + "_" +
                    c3n1ExtName(ext) + (spelling == 0 ? "_same" : "_dotslash");
                const fs::path dir = c3n1FreshDir(tag);
                ASSERT_TRUE(fs::exists(dir)) << dir.u8string();

                const fs::path inputFile = dir / fs::u8path(std::string("work") + ext);
                const fs::path secondFile = dir / fs::u8path(std::string("probe") + ext);
                const std::string inputPath = utf8PathString(inputFile);
                const std::string secondPath = utf8PathString(secondFile);
                const bool fixturesOk = (std::string(ext) == ".gpkg")
                    ? (c3n1WriteTwoPolygonGpkg(inputPath) && c3n1WriteTwoPolygonGpkg(secondPath))
                    : (c3n1WritePolygonShapefile(inputPath) && c3n1WritePolygonShapefile(secondPath));
                ASSERT_TRUE(fixturesOk) << "夹具写入失败：" << inputPath << " / " << secondPath;

                std::string output = secondPath;
                if (spelling == 1) {
                    output = utf8PathString(dir / "." / fs::u8path(std::string("probe") + ext));
                    ASSERT_NE(output, secondPath) << "拼法没造出差异，等价臂退化成同串臂";
                }

                const auto beforeNames = c3n1ListFiles(dir);
                const auto beforeBytes = c3n1CollectBytes(dir);
                ASSERT_GE(beforeBytes.size(), 2u) << "夹具目录里不足两个输入：" << dir.u8string();

                std::map<std::string, gis::framework::ParamValue> params;
                params["action"] = std::string(item.action);
                params["input"] = inputPath;
                params["output"] = output;
                params[item.secondKey] = secondPath;

                const std::string what = std::string("J4-second[") + item.action + ext +
                    (spelling == 0 ? "/same]" : "/dotslash]");

                const C3n1Run run = c3n1RunAction(p, params, progress_);

                EXPECT_FALSE(run.threw)
                    << what << "：动作没有干净失败而是**抛出了**异常（" << run.outcome()
                    << "）⇒ 输出 == 第二个输入没有被入口闸拦住";

                EXPECT_FALSE(run.result.success)
                    << what << "：输出指向第二个输入这件事没有被硬失败拦下来（返回 success，"
                    << "outcome = " << run.outcome() << "）";

                if (!run.threw && !run.result.success) {
                    EXPECT_NE(run.result.message.find("same file as input"), std::string::npos)
                        << what << "：失败消息没有点名『输出不能覆盖输入』这条规则（多半是等到 commit "
                        << "换名时才失败的），实际 message = " << run.result.message;
                }

                c3n1ExpectDirectoryUntouched(dir, beforeNames, beforeBytes, what.c_str());
            }
        }
    }
}

// ── J2：成功路径的产物形态一个字没变 ────────────────────────────────────────────────
// 这条防的是"临时件机制把产物名搞错 / 把边车落错"：机制改的是**写到哪儿**，一旦 commit 的落点
// 算错（例如 Shapefile 的 .dbf 落成了 .shp、或 GPKG 换名后 .gpkg-wal 留在目录里），
// 要素数与几何看起来都对，但用户拿到的是一份开不了/属性表缺失的产物。
// 判据比的是**产物本身**（层名、要素数、字段、CRS、rtree 可用性、目录里应有的那几个文件），
// 不比"我们写了什么"（exercised ≠ conformant）。
// 夹具一律复用同动作既有执行用例的那份（面类 = writeTwoPolygonFixture 的 A/B，
// 折线 = VectorVerticesExtractExecution 的三点折线），期望数全部按夹具坐标手算写死。
// C3n-2c 把动作数组从 3 个扩到 8 个（追加 buffer / clip / dissolve / polygonize / convert），
// C3n-2d 再追加 overlay 三点（union / difference / intersect）⇒ **11** 个动作。
// 每个新动作只取 GPKG 一条臂（见 cases 上方注释）。C3n-2a/2b 的 13 个已迁移动作仍**没有**
// J2 覆盖 —— 那是"判据扩面批"的活，登记在 task-C3n2c-report.md 的缺口里。
TEST_F(PluginTest, VectorStagedOutputKeepsProductForm) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr) << "plugin not loaded: vector";

    struct Case {
        const char* action;
        const char* ext;
        const char* inputName;        // 夹具名：面类 shapes.gpkg / 折线 shapes.gpkg / 栅格 shapes.tif
        const char* outputName;
        long long expectedFeatures;
        double filterMinX;
        double filterMinY;
        double filterMaxX;
        double filterMaxY;
        const char* expectedLayer;    // 仅 GPKG 臂用；.shp 臂的层名由目标文件名推
        OGRwkbGeometryType expectedType;
        std::vector<const char*> fields;
        double distance;              // buffer 的距离；<= 0 ⇒ 不传（其它动作也不传）
        const char* dissolveField;    // dissolve 的字段；nullptr ⇒ 不传
        const char* overlayName;      // clip 的叠加层；nullptr ⇒ 本动作不需要第二个输入
    };
    // C3n-1/2a/2b 的 3 个动作（boundary / centroid / vertices_extract）保持原臂，其中 boundary 与
    // vertices_extract 各再跑一条 Shapefile 臂（Shapefile 是**多文件集** ⇒ 边车落点这一层只有
    // .shp 臂能判）。C3n-2c 追加本刀 5 个动作（buffer / clip / dissolve / polygonize / convert），
    // 每个动作先只取 GPKG 格式（工单 §5 的降规模口径：> 30 s 才降，这里直接按最低规模铺，
    // 省下的是"再跑一遍 .shp 边车"的重复面）⇒ 覆盖动作数 3 → **8**。
    // 每个新动作的参数按它自己的语义给全（不许"无参数调用然后断言失败"）：
    //   buffer 要 distance、clip 要第二个输入 clip_vector、dissolve 要 dissolve_field、
    //   polygonize 要栅格输入、convert 要 format（它的驱动是按参数选的，不看扩展名）。
    const Case cases[] = {
        { "boundary", ".gpkg", "shapes.gpkg", "form_boundary.gpkg", 2, 0.0, 0.0, 24.0, 8.0,
          "shapes", wkbMultiLineString, { "name" }, 0.0, nullptr, nullptr },
        { "centroid", ".gpkg", "shapes.gpkg", "form_centroid.gpkg", 2, 0.0, 0.0, 24.0, 8.0,
          "shapes", wkbPoint, { "name" }, 0.0, nullptr, nullptr },
        { "vertices_extract", ".gpkg", "shapes.gpkg", "form_vertices.gpkg", 3, 0.0, 0.0, 10.0, 5.0,
          "shapes", wkbPoint, { "name", "source_fid", "vertex_idx" }, 0.0, nullptr, nullptr },
        { "boundary", ".shp", "shapes.gpkg", "form_boundary.shp", 2, 0.0, 0.0, 24.0, 8.0,
          "", wkbLineString, { "name" }, 0.0, nullptr, nullptr },
        { "vertices_extract", ".shp", "shapes.gpkg", "form_vertices.shp", 3, 0.0, 0.0, 10.0, 5.0,
          "", wkbPoint, { "name", "source_fid", "vertex_idx" }, 0.0, nullptr, nullptr },
        // 本刀 5 个动作（GPKG）。期望值全部按夹具坐标手算：
        //   buffer    两只面各外扩 5 ⇒ 2 个 MultiPolygon，包框 (-5,-5)-(29,13)，层名 <源层名>+"_buffer"；
        //   clip      A 被 (0,0)-(12,8) 方框整体包含（clone 支）、B 不与方框相交 ⇒ **1 个**要素；
        //   dissolve  按 name 分两组（A / B 各一条）⇒ **2 个** MultiPolygon，层名 "dissolved"；
        //   polygonize 10×10 两值栅格 ⇒ 左半 1 / 右半 2 两个等值区 ⇒ **2 个**面，层名 "polygonized"；
        //   convert   照抄两只面到 GPKG ⇒ **2 个** Polygon，层名 "shapes"（源层名）。
        { "buffer", ".gpkg", "shapes.gpkg", "form_buffer.gpkg", 2, -6.0, -6.0, 30.0, 14.0,
          "shapes_buffer", wkbMultiPolygon, { "name" }, 5.0, nullptr, nullptr },
        { "clip", ".gpkg", "shapes.gpkg", "form_clip.gpkg", 1, 0.0, 0.0, 12.0, 8.0,
          "shapes", wkbMultiPolygon, { "name" }, 0.0, nullptr, "overlay.gpkg" },
        { "dissolve", ".gpkg", "shapes.gpkg", "form_dissolve.gpkg", 2, 0.0, 0.0, 24.0, 8.0,
          "dissolved", wkbMultiPolygon, { "name" }, 0.0, "name", nullptr },
        { "polygonize", ".gpkg", "shapes.tif", "form_polygonize.gpkg", 2, 100.0, 190.0, 110.0, 200.0,
          "polygonized", wkbPolygon, { "DN" }, 0.0, nullptr, nullptr },
        { "convert", ".gpkg", "shapes.gpkg", "form_convert.gpkg", 2, 0.0, 0.0, 24.0, 8.0,
          "shapes", wkbPolygon, { "name" }, 0.0, nullptr, nullptr },
        // C3n-2d 追加 overlay 三点（GPKG）。叠加层都用 c3n1WriteClipOverlayGpkg 的方框
        // (0,0)-(12,8)：A 被整体包含、B 完全在外 ⇒ 手算：
        //   union       A 只出 both 块（56）、B 出 input 块（8）、方框−A 出 overlay 块（40）
        //               ⇒ **3 个**；层名 "union"；叠加夹具没有字段 ⇒ 产物字段 = 源 name + 标记 source
        //               （没有 name_2 这一列）；
        //               图层声明是 wkbUnknown（GPKG 登记为 GEOMETRY），落盘要素是 Polygon ——
        //               这是 union 的既有产物形态，下面用 action 名单独取期望。
        //   difference  A−方框 = 空、B 不相交原样保留 ⇒ **1 个**（B，包框 20,0..24,2）；
        //   intersect   A∩方框 = A、B 不相交 ⇒ **1 个**（A，包框 0,0..12,8）。
        { "union", ".gpkg", "shapes.gpkg", "form_union.gpkg", 3, 0.0, 0.0, 24.0, 8.0,
          "union", wkbUnknown, { "name", "source" }, 0.0, nullptr, "overlay.gpkg" },
        { "difference", ".gpkg", "shapes.gpkg", "form_difference.gpkg", 1, 20.0, 0.0, 24.0, 2.0,
          "difference", wkbMultiPolygon, { "name" }, 0.0, nullptr, "overlay.gpkg" },
        { "intersect", ".gpkg", "shapes.gpkg", "form_intersect.gpkg", 1, 0.0, 0.0, 12.0, 8.0,
          "intersect", wkbMultiPolygon, { "name" }, 0.0, nullptr, "overlay.gpkg" },
    };
    ASSERT_EQ(sizeof(cases) / sizeof(cases[0]), 13u)
        << "J2 的臂数必须与本刀收口后的射程一致（3 个既有动作的 5 臂 + C3n-2c 5 个动作的 5 臂 "
           "+ C3n-2d 3 个动作的 3 臂 == 13 臂 / 11 个动作）";

    for (const auto& item : cases) {
        const std::string tag = std::string("j2_") + item.action + "_" + c3n1ExtName(item.ext) + "_form";
        const fs::path dir = c3n1FreshDir(tag);
        ASSERT_TRUE(fs::exists(dir)) << dir.u8string();

        // 输入三类：面类两只面 / vertices_extract 的三点折线 / polygonize 的两值栅格
        //（都复用或对齐同动作既有执行用例的那份数据）。
        const std::string input = utf8PathString(dir / item.inputName);
        if (std::string(item.action) == "vertices_extract") {
            ASSERT_TRUE(c3n1WriteThreePointLineGpkg(input)) << input;
        } else if (std::string(item.action) == "polygonize") {
            ASSERT_TRUE(c3n1WriteTwoValueRaster(input)) << input;
        } else {
            ASSERT_TRUE(c3n1WriteTwoPolygonGpkg(input)) << input;
        }

        const fs::path output = dir / fs::u8path(item.outputName);
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string(item.action);
        params["input"] = input;
        params["output"] = utf8PathString(output);
        if (item.distance > 0.0) {
            params["distance"] = item.distance;
        }
        if (item.dissolveField) {
            params["dissolve_field"] = std::string(item.dissolveField);
        }
        if (std::string(item.action) == "convert") {
            // doConvert 的驱动由 format 参数选（不是从扩展名推）：不给它就会拿 GeoJSON 驱动
            // 写一个 .gpkg 名的 GeoJSON，产物形态判据会以"打不开"的形式红。
            params["format"] = std::string("GPKG");
        }
        if (item.overlayName) {
            const std::string overlay = utf8PathString(dir / item.overlayName);
            ASSERT_TRUE(c3n1WriteClipOverlayGpkg(overlay)) << overlay;
            // clip 的第二个输入叫 clip_vector；overlay 三点叫 overlay_vector。
            params[std::string(item.action) == "clip" ? "clip_vector" : "overlay_vector"] = overlay;
        }

        const std::string what = std::string("J2[") + item.action + item.ext + "]";
        const C3n1Run run = c3n1RunAction(p, params, progress_);
        ASSERT_FALSE(run.threw) << what << "：" << run.outcome();
        ASSERT_TRUE(run.result.success) << what << "：" << run.result.message;
        ASSERT_TRUE(fs::exists(output)) << what << "：产物不存在 " << output.u8string();

        // 目录里只应留下"夹具 + 本轮产物"这一组文件：任何 *.gis-tmp-* 或 SQLite 附属件都算外物。
        // 夹具名不再写死 shapes.gpkg：polygonize 的输入是 shapes.tif，clip 另有 overlay.gpkg。
        std::vector<std::string> expectedNames{ item.inputName };
        if (item.overlayName) {
            expectedNames.push_back(item.overlayName);
        }
        const std::string outputName{ item.outputName };
        if (std::string(item.ext) == ".gpkg") {
            expectedNames.push_back(outputName);
        } else {
            const std::string stem = outputName.substr(0, outputName.size() - 4);
            expectedNames.push_back(stem + ".dbf");
            expectedNames.push_back(stem + ".prj");
            expectedNames.push_back(stem + ".shp");
            expectedNames.push_back(stem + ".shx");
        }
        std::sort(expectedNames.begin(), expectedNames.end());
        const auto actualNames = c3n1ListFiles(dir);
        EXPECT_EQ(actualNames, expectedNames)
            << what << "：目录里的文件集合不是『夹具 + 应有产物』这一组"
            << "；实际 =" << c3n1JoinNames(actualNames)
            << "；期望 =" << c3n1JoinNames(expectedNames)
            << "；目录 = " << dir.u8string();

        GDALDataset* outDs = static_cast<GDALDataset*>(GDALOpenEx(
            output.u8string().c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
        ASSERT_NE(outDs, nullptr) << what << "：产物打不开（临时件换名后形态不对）";
        OGRLayer* outLayer = outDs->GetLayer(0);
        ASSERT_NE(outLayer, nullptr) << what << "：产物里没有图层";

        // 层名的两种形状都要判，而且**两边不一样**：
        //   GPKG ⇒ 动作按各自动作的层名规则建层（多数是源层名 "shapes"；buffer 是 "shapes_buffer"、
        //     dissolve 是 "dissolved"、polygonize 是 "polygonized"）⇒ 期望值逐动作写死在 cases 里；
        //   Shapefile ⇒ OGR 在**打开时**按文件名推层名（本机实测：boundary_out.shp 的
        //     `ogrinfo -so -al` 报 `Layer name: boundary_out`）⇒ 换名之后读到的必须是目标名。
        // 这一条就是把"临时件的名字漏进产物"钉死的地方：GPKG 那侧漏进来会是层名变了，
        // Shapefile 那侧漏进来会是 "boundary_out.gis-tmp-<pid>-<seq>"。
        const std::string outputStem = std::string(item.outputName).substr(
            0, std::string(item.outputName).find_last_of('.'));
        const std::string expectedLayerName =
            std::string(item.ext) == ".gpkg" ? std::string(item.expectedLayer) : outputStem;
        EXPECT_STREQ(outLayer->GetName(), expectedLayerName.c_str())
            << what << "：产物层名不是期望值（期望 " << expectedLayerName
            << "，临时件机制把名字写歪了就是这一条红）";

        EXPECT_EQ(outLayer->GetFeatureCount(TRUE), item.expectedFeatures)
            << what << "：产物要素数与夹具手算期望不符";

        const OGRSpatialReference* outSrs = outLayer->GetSpatialRef();
        ASSERT_NE(outSrs, nullptr) << what << "：产物没有 CRS（夹具是 EPSG:3857，.prj/gpkg_srs 都要在）";
        const char* authority = outSrs->GetAuthorityCode(nullptr);
        ASSERT_NE(authority, nullptr);
        EXPECT_STREQ(authority, "3857") << what << "：产物 CRS 不是 EPSG:3857";

        if (std::string(item.ext) == ".gpkg") {
            // rtree 可用性沿用 C3m 那三条审计用例的同一支判据（行数 == 非空几何数 + 按包框查得到）。
            expectGpkgSpatialIndexUsable(outLayer, item.expectedFeatures,
                item.filterMinX, item.filterMinY, item.filterMaxX, item.filterMaxY,
                utf8PathString(output));
        }

        // 字段集合逐动作写死在 cases 里（照抄源字段的 6 个动作是 {"name"}；vertices_extract 另加
        // source_fid / vertex_idx；polygonize 只有它自己建的 DN）。
        {
            const auto* defn = outLayer->GetLayerDefn();
            std::vector<std::string> fields;
            for (int i = 0; i < defn->GetFieldCount(); ++i) {
                fields.push_back(defn->GetFieldDefn(i)->GetNameRef());
            }
            std::sort(fields.begin(), fields.end());
            std::vector<std::string> expectedFields(item.fields.begin(), item.fields.end());
            std::sort(expectedFields.begin(), expectedFields.end());
            EXPECT_EQ(fields, expectedFields)
                << what << "：产物字段集合不符（Shapefile 截断到 10 字符是既有事实，这里两边同源）";
        }

        // 几何类别逐动作写死（含 Shapefile 的既有拍平事实：MultiLineString ⇒ LineString）。
        // ⚠ Shapefile 那一侧驱动会把 MultiLineString 拍平成 LineString（改前改后同形：
        //   本机实测旧码产物 c3n1_j1_boundary_shp/boundary_out.shp 的 `ogrinfo -so -al` 报
        //   `Geometry: Line String`）⇒ 期望值必须按**格式的既有事实**给，不许拿声明类型硬要。
        // ⚠ union 例外：它用 wkbUnknown 建层（GPKG 在 gpkg_geometry_columns 里登记为 GEOMETRY），
        //   写入的分块是 Polygon（图层声明未知 ⇒ writeGeometryFeature 不提升）⇒ 层声明与
        //   落盘要素类型**本来就不同**，这两个期望分开给。其余动作两者相同。
        const bool unionUnknownLayer = std::string(item.action) == "union";
        const OGRwkbGeometryType expectedFeatureType =
            unionUnknownLayer ? wkbPolygon : item.expectedType;
        EXPECT_EQ(wkbFlatten(outLayer->GetLayerDefn()->GetGeomType()), item.expectedType)
            << what << "：产物几何类别不符";
        outLayer->ResetReading();
        if (OGRFeature* feat = outLayer->GetNextFeature()) {
            EXPECT_EQ(wkbFlatten(feat->GetGeometryRef()->getGeometryType()), expectedFeatureType)
                << what << "：落盘要素的几何类别与图层声明不一致";
            OGRFeature::DestroyFeature(feat);
        }
        GDALClose(outDs);
    }
}

// ── J3：替换之前真的关闭过数据集 ────────────────────────────────────────────────────
// 工单 §2.3 把这条单独立一档：全插件今天 0 处显式 Close()，dstDS 靠作用域析构才 GDALClose
// ⇒ "写完就改名"会撞在 GDAL 还握着句柄的时刻。本条判的是**结果合规**：
//   ① GPKG 产物旁边不许有 -wal / -journal / -shm（没关闭就没收日志）；
//   ② 产物能被独立打开并读到全部要素。
// C3n-2c 把动作数组从 3 个扩到 8 个（追加 buffer / clip / dissolve / polygonize / convert），
// C3n-2d 再追加 overlay 三点（union / difference / intersect）⇒ **11** 个动作，与 J2 同射程。
// 参数按各动作自己的语义给全（clip 要 clip_vector、overlay 三点要 overlay_vector、
// buffer 要 distance、dissolve 要字段、polygonize 要栅格、convert 要 format）。
// ⚠ 顺序本身的可证伪性在报告里做（反证臂：把 commit 里那句 close 删掉 ⇒ 替换在 Windows 上
//   直接撞 ERROR_ACCESS_DENIED，动作报失败而不是假成功）。本用例只兜结果形态。
TEST_F(PluginTest, VectorStagedOutputLeavesNoJournalSidecar) {
    auto* p = mgr_.find("vector");
    ASSERT_NE(p, nullptr) << "plugin not loaded: vector";

    struct Action {
        const char* action;
        const char* inputName;       // shapes.gpkg / shapes.tif（polygonize）
        const char* overlayName;     // clip 的叠加层；nullptr ⇒ 不需要
        double distance;             // buffer 的距离；<= 0 ⇒ 不传
        const char* dissolveField;   // dissolve 的字段；nullptr ⇒ 不传
    };
    const Action actions[] = {
        { "boundary", "shapes.gpkg", nullptr, 0.0, nullptr },
        { "centroid", "shapes.gpkg", nullptr, 0.0, nullptr },
        { "vertices_extract", "shapes.gpkg", nullptr, 0.0, nullptr },
        { "buffer", "shapes.gpkg", nullptr, 5.0, nullptr },
        { "clip", "shapes.gpkg", "overlay.gpkg", 0.0, nullptr },
        { "dissolve", "shapes.gpkg", nullptr, 0.0, "name" },
        { "polygonize", "shapes.tif", nullptr, 0.0, nullptr },
        { "convert", "shapes.gpkg", nullptr, 0.0, nullptr },
        { "union", "shapes.gpkg", "overlay.gpkg", 0.0, nullptr },
        { "difference", "shapes.gpkg", "overlay.gpkg", 0.0, nullptr },
        { "intersect", "shapes.gpkg", "overlay.gpkg", 0.0, nullptr },
    };
    ASSERT_EQ(sizeof(actions) / sizeof(actions[0]), 11u)
        << "J3 的动作数组必须覆盖 11 个动作（3 个既有 + C3n-2c 5 个 + C3n-2d overlay 3 个）；"
        << "少一条就是射程缩水";

    for (const auto& item : actions) {
        const char* action = item.action;
        const fs::path dir = c3n1FreshDir(std::string("j3_") + action);
        ASSERT_TRUE(fs::exists(dir)) << dir.u8string();

        const std::string input = utf8PathString(dir / item.inputName);
        if (std::string(action) == "vertices_extract") {
            ASSERT_TRUE(c3n1WriteThreePointLineGpkg(input)) << input;
        } else if (std::string(action) == "polygonize") {
            ASSERT_TRUE(c3n1WriteTwoValueRaster(input)) << input;
        } else {
            ASSERT_TRUE(c3n1WriteTwoPolygonGpkg(input)) << input;
        }

        const std::string output = utf8PathString(dir / fs::u8path(
            std::string("out_") + action + ".gpkg"));
        std::map<std::string, gis::framework::ParamValue> params;
        params["action"] = std::string(action);
        params["input"] = input;
        params["output"] = output;
        if (item.distance > 0.0) {
            params["distance"] = item.distance;
        }
        if (item.dissolveField) {
            params["dissolve_field"] = std::string(item.dissolveField);
        }
        if (std::string(action) == "convert") {
            params["format"] = std::string("GPKG");
        }
        if (item.overlayName) {
            const std::string overlay = utf8PathString(dir / item.overlayName);
            ASSERT_TRUE(c3n1WriteClipOverlayGpkg(overlay)) << overlay;
            params[std::string(action) == "clip" ? "clip_vector" : "overlay_vector"] = overlay;
        }

        const C3n1Run run = c3n1RunAction(p, params, progress_);
        ASSERT_FALSE(run.threw) << action << "：" << run.outcome();
        ASSERT_TRUE(run.result.success) << action << "：" << run.result.message;

        const std::string base = output;
        for (const char* suffix : { "-wal", "-journal", "-shm" }) {
            const std::string sidecar = base + suffix;
            EXPECT_FALSE(fs::exists(fs::u8path(sidecar)))
                << "J3[" << action << "]：产物旁边留着 SQLite 附属件 " << sidecar
                << " ⇒ 数据集没有在替换之前被真正关闭（或者旧产物的日志被留在了新产物旁边）";
        }

        const auto names = c3n1ListFiles(dir);
        std::string residue;
        for (const auto& name : names) {
            if (name.find(".gis-tmp-") != std::string::npos) {
                residue += " " + name;
            }
        }
        EXPECT_TRUE(residue.empty())
            << "J3[" << action << "]：成功路径之后目录里还有临时件" << residue;

        // 独立打开一次并读到全部要素（"产物可用"而不是"我们自报写完"）。
        GDALDataset* ds = static_cast<GDALDataset*>(GDALOpenEx(
            output.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
        ASSERT_NE(ds, nullptr) << "J3[" << action << "]：产物打不开";
        OGRLayer* layer = ds->GetLayer(0);
        ASSERT_NE(layer, nullptr);
        EXPECT_GT(layer->GetFeatureCount(TRUE), 0) << "J3[" << action << "]：产物里 0 个要素";
        GDALClose(ds);
    }
}

// ── J5：结构守卫 ──「契约已全覆盖」必须承重 ──────────────────────────────────────
// 全覆盖最坏的结局不是"某个点还没改"，而是**大家以为契约已经全覆盖了**。所以名单必须承重，
// 而且双向可红：
//   ① 已迁移点数 == 写死的"已迁移名单"条数（== 24；不是"≥1"，≥1 会让后面几刀做完也测不出东西）；
//   ② 待迁移名单 == 0 条（本刀之后不应再有"待迁移"）；该循环今天不执行，扫射程缩水改由 ⑤0 兜；
//   ③ 名单**外**若出现任何一处仍以 output 直接建数据集 ⇒ 红（新写的点、或别人绕开名单改的点）；
//   ④ 名单**里**的点如果被迁移完了而名单没减 ⇒ 也红（防名单变死条目）；
//   ⑤ 全目录 drv->Create 的总数也要对上账（1 处 helper + 0 处待迁 + 1 处论域外 = 2），
//      这样"新加一个写出点却不进任何名单"同样会红。
//   ⑤0 **防真空跑闸**：扫描必须数到 **24** 个带 VectorStagedOutput 声明的 (文件,函数) 坐标。
//      名单为空以后，若函数边界解析退化（两个函数被并成一个）或目录里少了文件，
//      ① 的"点数 == 名单条数"可能仍然成立 ⇒ 这一条用**绝对数**把"扫射程缩水"钉死。
// 论域现读（C3n-2d 迁移后复核）：`grep -rn "drv->Create(" src/plugins/vector/` = 2 处
//   ＝ helper 1 处 + 论域外 doRasterize 1 处；直接写 output 的矢量点 0 处。
//   账的来路：C3n-1 开工时是 25，迁走 3 处、helper 新增 1 处 ⇒ 23；C3n-2a 再迁走 8 处 ⇒ 15；
//   C3n-2b 再迁走 5 处 ⇒ 10；C3n-2c 再迁走 5 处 ⇒ 5；C3n-2d 再迁走 3 处 ⇒ 2。
//   doRasterize（写的是栅格）按用户裁定留在射程之外，
//   但它仍然是一个"直接写 output 的建数据集点"，所以**单独点名登记**在这一支里，
//   免得它被 ③ 当成名单外命中，也免得它被误当成已覆盖。
// 判据的形：只认 (文件, 动作函数) 这一对**函数级**坐标，不认行号（行号会随任何一次改动漂移）。
// （GIS_SOURCE_DIR 这个编译宏由 tests/CMakeLists.txt 发，本文件上面 C3m 那条源码守卫也用同一个。）
TEST_F(PluginTest, VectorStagedOutputHalfMigratedRosterIsLoadBearing) {
    // 已迁移动作（C3n-1 的 3 个 + C3n-2a 的 8 个 + C3n-2b 的 5 个 + C3n-2c 的 5 个
    // + C3n-2d 的 overlay 3 个 = 24）。
    static const std::vector<std::pair<std::string, std::string>> kStagedActions = {
        { "vector_plugin_boundary.inc", "doBoundary" },
        { "vector_plugin_buffer_clip.inc", "doBuffer" },
        { "vector_plugin_buffer_clip.inc", "doClip" },
        { "vector_plugin_centroid.inc", "doCentroid" },
        { "vector_plugin_convex_hull.inc", "doConvexHull" },
        { "vector_plugin_dissolve.inc", "doDissolve" },
        { "vector_plugin_endpoints_extract.inc", "doEndpointsExtract" },
        { "vector_plugin_envelope.inc", "doEnvelope" },
        { "vector_plugin_geom_metrics.inc", "doGeomMetrics" },
        { "vector_plugin_info_filter.inc", "doFilter" },
        { "vector_plugin_interior_point.inc", "doInteriorPoint" },
        { "vector_plugin_midpoints_extract.inc", "doMidpointsExtract" },
        { "vector_plugin_nearest.inc", "doNearest" },
        { "vector_plugin_overlay.inc", "doDifference" },
        { "vector_plugin_overlay.inc", "doIntersect" },
        { "vector_plugin_overlay.inc", "doUnion" },
        { "vector_plugin_raster_convert.inc", "doConvert" },
        { "vector_plugin_raster_convert.inc", "doPolygonize" },
        { "vector_plugin_repair.inc", "doRepair" },
        { "vector_plugin_simplify.inc", "doSimplify" },
        { "vector_plugin_singlepart.inc", "doSinglepart" },
        { "vector_plugin_sliver_remove.inc", "doSliverRemove" },
        { "vector_plugin_spatial_join.inc", "doSpatialJoin" },
        { "vector_plugin_vertices_extract.inc", "doVerticesExtract" },
    };
    // 待迁移名单：24 个矢量写出点全部迁完 ⇒ **0 条**（C3n-2d 收口）。
    // 名单为空以后，② 的逐条循环不执行；"扫射程缩水"由下面 ⑤0 的绝对数闸承重。
    static const std::vector<std::pair<std::string, std::string>> kPendingActions = {
    };
    // 论域之外但**仍是直接写 output** 的那一个点（写栅格，用户裁定"先限 vector 的矢量输出"）。
    static const std::pair<std::string, std::string> kOutsideDomain = {
        "vector_plugin_raster_convert.inc", "doRasterize",
    };

    EXPECT_EQ(kPendingActions.size(), 0u)
        << "待迁移名单本身不是 0 条 ⇒ 这条守卫的账先就错了（射程 24 = 已迁 24 + 待迁 0）";

    const fs::path dir = fs::u8path(std::string(GIS_SOURCE_DIR) + "/src/plugins/vector");
    ASSERT_TRUE(fs::exists(dir))
        << "扫描根不存在 ⇒ 这条守卫没在扫任何东西，判红而不是放过：" << dir.u8string();

    // (文件, 函数) → 该函数体内 drv->Create( 的第一实参清单
    std::map<std::pair<std::string, std::string>, std::vector<std::string>> createArgs;
    std::map<std::pair<std::string, std::string>, int> stagedDeclarations;
    std::size_t scannedFiles = 0;
    std::size_t totalCreates = 0;
    bool sawHelper = false;

    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code secer;
        if (!it->is_regular_file(secer) || secer) {
            continue;
        }
        const fs::path path = it->path();
        const std::string filename = path.filename().u8string();
        const std::string ext = path.extension().u8string();
        if (ext != ".inc" && ext != ".cpp") {
            continue;
        }
        ++scannedFiles;
        std::ifstream in(path, std::ios::binary);
        ASSERT_TRUE(in.is_open()) << "射程内的文件读不开，判据无法覆盖它：" << path.u8string();
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        // 按源码文本切函数： Gis 插件的 .inc 里每个动作都是一个
        // `gis::framework::Result VectorPlugin::doXxx(` 开头，helper 那一个在 common.inc 的类里。
        std::string currentFunction;
        std::size_t pos = 0;
        while (pos < text.size()) {
            const std::size_t eol = text.find('\n', pos);
            const std::size_t stop = (eol == std::string::npos) ? text.size() : eol;
            const std::string line = text.substr(pos, stop - pos);
            pos = stop + 1;

            const std::size_t fnAt = line.find("gis::framework::Result VectorPlugin::");
            if (fnAt != std::string::npos) {
                const std::size_t marker = line.find("VectorPlugin::");
                currentFunction = line.substr(marker + 14);
                const std::size_t paren = currentFunction.find('(');
                if (paren != std::string::npos) {
                    currentFunction = currentFunction.substr(0, paren);
                }
                continue;
            }
            if (line.find("bool start(const std::string& output") != std::string::npos) {
                currentFunction = "VectorStagedOutput::start";
                continue;
            }
            // 注释行一律不参与计数：WHY 注释里会提"drv->Create""VectorStagedOutput"这些名字
            //（本仓已经吃过一次"为解释某事写的注释污染了数那件事的 grep"）。
            std::size_t indent = 0;
            while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
                ++indent;
            }
            if (line.compare(indent, 2, "//") == 0) {
                continue;
            }

            const std::size_t createAt = line.find("drv->Create(");
            if (createAt != std::string::npos && !currentFunction.empty()) {
                const std::size_t argStart = createAt + 12;
                const std::size_t comma = line.find(',', argStart);
                const std::size_t argStop = (comma == std::string::npos) ? line.size() : comma;
                createArgs[{ filename, currentFunction }].push_back(
                    line.substr(argStart, argStop - argStart));
                ++totalCreates;
                if (currentFunction == "VectorStagedOutput::start") {
                    sawHelper = true;
                }
            }

            const std::size_t stagedAt = line.find("VectorStagedOutput staged");
            if (stagedAt != std::string::npos && !currentFunction.empty()) {
                ++stagedDeclarations[{ filename, currentFunction }];
            }
        }
    }
    EXPECT_FALSE(bool(ec)) << "目录扫描中断于：" << ec.message();

    // 三道"不许空跑"闸（本仓同族守卫都带；少了它们，扫描根写错就能让全部判点假绿）。
    ASSERT_GE(scannedFiles, 30u)
        << "只扫到 " << scannedFiles << " 个 *.inc|*.cpp ⇒ 判据在空跑，扫描根是：" << dir.u8string();
    EXPECT_TRUE(sawHelper)
        << "没在 vector_plugin_common.inc 里扫到 VectorStagedOutput::start 里的 drv->Create "
        << "⇒ helper 没进射程，①②③④⑤ 全是假绿";

    // ⑤0 防真空跑闸（工单 §3）：扫到的 (文件,函数) 坐标总数必须 == 24。
    // 名单为空时 ① 的"点数 == 名单条数"是 24 == 24；若解析把两个函数并成一个、或目录少了文件，
    // 点数会变 23 而名单仍是 24 ⇒ ① 红；但若两边一起缩水（例如名单被顺手删掉一条），
    // ① 会静默转绿。这一条用绝对数把射程钉死，刻意**不读** kStagedActions.size()。
    EXPECT_EQ(stagedDeclarations.size(), 24u)
        << "扫到的 (文件,函数) 坐标总数是 " << stagedDeclarations.size()
        << "，不是 24 ⇒ 扫描射程缩水（函数边界解析退化 / 目录少文件 / 名单被顺手删了一条），"
        << "这份守卫正在空跑或漏扫";

    // ① 已迁移点数 == 名单条数（== 24），且必须是**点名**命中而不是"扫到了就叫"。
    std::size_t stagedCount = 0;
    for (const auto& entry : stagedDeclarations) {
        stagedCount += static_cast<std::size_t>(entry.second);
    }
    EXPECT_EQ(stagedCount, kStagedActions.size())
        << "已迁移点数与'已迁移名单'条数不符（实际数到 " << stagedCount
        << "，名单写的是 " << kStagedActions.size()
        << "）⇒ 本刀的第一段要么多迁了、要么少迁了，名单必须一起改";
    for (const auto& entry : kStagedActions) {
        EXPECT_EQ(stagedDeclarations[entry], 1)
            << "已迁移名单里的 " << entry.first << "::" << entry.second
            << " 在源码里应当恰好数到一处 VectorStagedOutput 声明（数到 "
            << stagedDeclarations[entry] << "）⇒ 名单变死条目或函数体写重了";
        // 迁进临时件机制之后，这个函数体内**不该再有**直接写 output 的建数据集点。
        EXPECT_EQ(createArgs[entry].size(), 0u)
            << "已迁移的 " << entry.first << "::" << entry.second
            << " 里还留着直接建到 output 的写出点 ⇒ 契约在这个点上没生效";
    }

    // ② 待迁移名单逐条点名：每条都要真的数到一处 `output.c_str()`。
    for (const auto& entry : kPendingActions) {
        const auto it = createArgs.find(entry);
        ASSERT_NE(it, createArgs.end())
            << "待迁移名单里的 " << entry.first << "::" << entry.second
            << " 在源码里已经数不到 drv->Create ⇒ 它已经被迁走了，名单必须减（工单 §4④）";
        int direct = 0;
        for (const auto& arg : it->second) {
            if (arg.find("output.c_str()") != std::string::npos) {
                ++direct;
            }
        }
        EXPECT_GE(direct, 1)
            << entry.first << "::" << entry.second << " 的 drv->Create 第一实参不是 output.c_str()："
            << "要么已被迁移（名单要减），要么写成了第三种形态";
    }

    // ③ + ⑤ 名单外不许出现直接写 output 的点；总账也要对上。
    std::string offenders;
    std::size_t directTotal = 0;
    for (const auto& entry : createArgs) {
        if (entry.first.second == "VectorStagedOutput::start") {
            continue;   // helper 自己那处发的就是临时件路径
        }
        for (const auto& arg : entry.second) {
            if (arg.find("output.c_str()") == std::string::npos) {
                continue;
            }
            ++directTotal;
            const bool listed = std::find(kPendingActions.begin(), kPendingActions.end(), entry.first)
                != kPendingActions.end();
            const bool outside = (entry.first == kOutsideDomain);
            if (!listed && !outside) {
                offenders += "\n  " + entry.first.first + "::" + entry.first.second;
            }
        }
    }
    EXPECT_TRUE(offenders.empty())
        << "名单外出现了仍以 output 直接建数据集的矢量写出点：" << offenders
        << " ⇒ 要么是新加的写出点没进名单，要么是有人绕开机制自己写";
    EXPECT_EQ(directTotal, kPendingActions.size() + 1u)
        << "直接写 output 的建数据集点总数对不上账（数到 " << directTotal
        << "，期望 0 个待迁移 + 1 个论域外的 doRasterize = 1）";
    EXPECT_EQ(totalCreates, directTotal + 1u)
        << "drv->Create( 的总数对不上账（数到 " << totalCreates << "，期望 "
        << directTotal + 1u << "）⇒ 有新的写出点没被任何名单接住";

    // ⑥ 24 个已迁移点必须**真的用上**替换收口，而不是只 new 了个对象放着。
    for (const auto& entry : kStagedActions) {
        EXPECT_EQ(stagedDeclarations[entry], 1) << entry.first << "::" << entry.second;
    }
}
