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
// 每条用例各自一个进程（日志原文 `Note: Google Test filter = <单条用例名>`），所以红日志
// build/t15_tier1_red_ctest.log 三条用例全部跑到并有汇总行 `33% tests passed, 2 tests failed out of 3`。
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
    // 显式写两个方向，不靠 spec 默认值漂移来制造非法组合（sobel_dx/sobel_dy 的 spec 默认本就是 1/1，见 :52-:53）。
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
// 改前实现兜底是 9/2.0（提交 54df42c:1159-1160），spec 是 5/1.5（:35-:36）⇒ 本用例红。
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
    const std::string expectedFromSpec = *declaredDefault == 4 ? "2" : "1";
    EXPECT_EQ(run(*declaredDefault, "cc_diag_specdefault_explicit_output.tif"), expectedFromSpec)
        << "显式传 spec 默认值必须与本 action 的 spec 语义一致";
    EXPECT_EQ(run(std::nullopt, "cc_diag_specdefault_unset_output.tif"), expectedFromSpec)
        << "不传该键时的兜底与 paramSpecs() 的默认值分叉了（兜底漂移）";
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
// 调用方据此既不写 metadata 键也不追加提示。这条降级分支此前只被手动跑过两次命令行检查
// （build/t13_tier1_cli_probe_nogt.log / _mixedgt.log），ctest 里一条都没有 ⇒
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
    // （= 只有两侧都缺才返回 -1）时，未补下面两段的全量 577 条零红（实测见本报告 Fix round 2 节），
    // 而命令行混合探针会给每一次"PAN 没有地理参考"的正常使用刷一句假的
    // `pan_ms_extent_mismatch: 1 (relative difference 1 over tolerance 1e-06)`：失败方拿不到四角、
    // 结构体停在默认的 {0,0,0,0}，与另一幅 300 m 的包络一比 ⇒ 边差 300 / 较大边长 300 = 1。
    // ⇒ 危害不是"少个提示"而是**凭空报出一个范围不一致**，把用户推向完全错误的方向；
    //   契约原文（task-21 发布说明素材）："两侧都没有地理变换、或只有一侧有变换时都不报警"。
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
        << "上界被放宽（修前聚焦 33 条实测全绿，见 task9r1_mutMF_ctest.log；"
           "round-1 复审在全量 544 条上复跑 MF，唯一红的是本用例）"
           "等于把 EVI 的背景因子取值域开了口子";

    // T15-B：required 漂移同样没人守（变异 false→true 时全量 544/544 仍绿），后果是可观察的：
    // src/cli/main.cpp 会把它打成 [required]、并且不再打印 "Default: 1"，
    // param_card_widget.cpp 会加必填标记 ⇒ "标成必填、隐藏默认值、实际又不必填"。
    EXPECT_FALSE(eviLSpec->required)
        << "evi_l 的 spec 是可选、有默认值的键；标成必填会让 CLI 隐藏默认值、面板加必填标记，"
           "而 EVI 分支不传它照样按兜底 1.0 算（spindex_plugin.cpp 里读 evi_l 的那一行）";
}

// T15-A：SAVI 侧的同一缺陷形状（Task 9 round-1 复审实测：把 spindex_plugin.cpp 里
// l_value 的 spec 默认 double{0.5} 改成 double{0.3} ⇒ 全量 544/544 仍然全绿，
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

// ── Task 16 Tier 1：分水岭自动标记路径缺 OpenCV 约定的 `markers += 1` ─────────────
// 为什么主鉴别断言是**关系式**而不是计数（R-T16-1）：
//   改前 sureFg 的两个圆盘就是 connectedComponents 的标签 1 和 2，其余像元（含"确定背景"）全是 0，
//   而 cv::watershed 把 0 一律当"未知区"向外淹没 ⇒ 整幅背景被并进标签 1 的盆地
//   ⇒ 旧的 maxLabel 计数改前改后都得 "2" ⇒ 计划 Step 1 的 EXPECT_EQ(segment_count,"2") **没有鉴别力**。
//   实测改前的输出矩阵（build/t16_probe_ext_output.txt 同批的 ws_t16_auto_output.tif，见报告 §2.1）：
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
// 逐条鉴别力（一手证据：改前红日志 build/t16_redfinal_ctest.log:16 与 :19，实测背景(16,2)=1、A心=1、B心=2）：
//   改前**只有 2 条会红** —— (i) 的 `background != centerA`（1 vs 1）与 (iii) 的 `centerA >= 2`（1 vs 2）。
//   B 盘在改前就独立带着标签 2，所以 (i) 的 `background != centerB`、(ii)、(iii) 的 `centerB >= 2` 改前都是绿的；
//   (i) 处紧跟的 `EXPECT_EQ(background, 1)` 改前改后**同值 = 1**（build/task16rev_auto_pre.txt 与
//   build/task16rev_auto_post.txt 的 (16,2) 都是 1）⇒ **不作鉴别证据**，它是行为锁：钉住"自动分支写出的背景位
//   是标签 1"这一对外可见口径（改前的 1 来自"0 背景被洪水吞进区域 1"、改后的 1 来自"背景独占标签 1"，
//   同一个数字来自两条相反的路径），与下面 EXPECT_EQ(segment_count,"2")、EXPECT_GE(distinctLabels,3)
//   两处"不作鉴别证据"注释同口径 ⇒ 不删也不加强。
//   其余几条并非无用，只是各挡别的假实现：(ii) 挡"两盘并成一盆"（变异 A1 实测会红，build/t16_mutA1_ctest.log:21），
//   (iv) 挡"先归零、再整体 +1"的顺序颠倒（变异 A2b 实测会红，build/t16_mutA2b_ctest.log:15 与 :22）。
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
    //   硬约束：**距 -1 谷线须 >= 2 像元**。当前实现下 y=16 行实测（build/task16rev_auto_post.txt）
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
    //   build/t16_mutA2b_ctest.log:15 与 :22）。这两个点**距 -1 谷线只有 2 像元**（余量约束及其下限
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
// 既回退到已废弃的键 "k"，又把 5 当默认，而 spec（:68）声明的是 4 ⇒ 本用例红。
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
// （模拟取证见 task-11-report.md）。
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
// 哨兵断言恒真：把 setTo 整块删掉，上一轮的全量 559 条仍然全绿（审查者实测，本轮 §判别取证复现），
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
    // 下面 EXPECT_LT / EXPECT_EQ 的失败消息，而取证时的实测值（2819 / 517）保存在
    // build/task12_tier1_red.log:15 与 build/task12_tier1_green_measure.log:12 两份日志里。
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
    // 下面的字面量取自实测：build/task12f3_measure.log。
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
    // （复审 F-4）：改前/改后的原始行已留在 build/task12_tier3_red.log:11-12 与
    // build/task12_tier3_green.log:39-40，而断言失败时 gtest 会自动把比较双方的实际值打出来，
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

// R-T10-8：zonal_stats 原先没有 actionUiConfig 条目 ⇒ 面板渲染插件级全部 9 个参数。
// 读取点取证：doZonalStats 只取 input/vector/output/feature_id_field
// （raster_manage_plugin.cpp:258-261），:264 对空 vector 直接 fail，所以 vector 进 requiredKeys。
// WHY 白名单里没有 band：派发时给的键表含 band，但 raster_manage_plugin.cpp 的 getParam 调用
// 只有上述四处（band 只在 :208 的 nodata 动作里读），列进来就是放一个填了不生效的键。
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
