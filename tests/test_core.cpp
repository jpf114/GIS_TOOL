#include <gtest/gtest.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>
#include <gis/core/types.h>
#include <gis/core/error.h>
#include <gis/core/progress.h>
#include <gdal_priv.h>
#include <opencv2/opencv.hpp>
#include <filesystem>
#include <fstream>
#include "test_support.h"

namespace fs = std::filesystem;

static fs::path getTestDir() {
    return gis::tests::defaultTestOutputDir("test_core_output");
}

class CoreTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        gis::core::initGDAL();
        gis::tests::ensureDirectory(getTestDir());
    }
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(CoreTest, InitGDAL) {
    EXPECT_NO_THROW(gis::core::initGDAL());
}

TEST_F(CoreTest, CreateAndOpenRaster) {
    std::string path = (getTestDir() / "create_test.tif").string();
    {
        auto ds = gis::core::createRaster(path, 100, 100, 1, GDT_Float32);
        ASSERT_NE(ds, nullptr);
        EXPECT_EQ(ds->GetRasterXSize(), 100);
        EXPECT_EQ(ds->GetRasterYSize(), 100);
        EXPECT_EQ(ds->GetRasterCount(), 1);
    }

    auto ds2 = gis::core::openRaster(path, true);
    ASSERT_NE(ds2, nullptr);
    EXPECT_EQ(ds2->GetRasterXSize(), 100);
}

TEST_F(CoreTest, GdalBandToMat) {
    std::string path = (getTestDir() / "band_test.tif").string();
    {
        auto ds = gis::core::createRaster(path, 50, 50, 1, GDT_Float32);
        auto* band = ds->GetRasterBand(1);
        std::vector<float> data(50 * 50, 42.0f);
        band->RasterIO(GF_Write, 0, 0, 50, 50, data.data(), 50, 50, GDT_Float32, 0, 0);
        band->FlushCache();
    }

    auto ds2 = gis::core::openRaster(path, true);
    cv::Mat mat = gis::core::gdalBandToMat(ds2.get(), 1);
    EXPECT_EQ(mat.rows, 50);
    EXPECT_EQ(mat.cols, 50);
    EXPECT_FLOAT_EQ(mat.at<float>(0, 0), 42.0f);
}

TEST_F(CoreTest, MatToGdalTiff) {
    std::string srcPath = (getTestDir() / "mat_src.tif").string();
    std::string dstPath = (getTestDir() / "mat_dst.tif").string();

    auto srcDS = gis::core::createRaster(srcPath, 30, 30, 1, GDT_Float32);
    double adfGT[6] = {100.0, 0.5, 0.0, 200.0, 0.0, -0.5};
    srcDS->SetGeoTransform(adfGT);

    cv::Mat mat(30, 30, CV_32F, cv::Scalar(99.0f));
    gis::core::matToGdalTiff(mat, srcDS.get(), dstPath, 1);

    auto dstDS = gis::core::openRaster(dstPath, true);
    ASSERT_NE(dstDS, nullptr);
    cv::Mat result = gis::core::gdalBandToMat(dstDS.get(), 1);
    EXPECT_FLOAT_EQ(result.at<float>(0, 0), 99.0f);
}

TEST_F(CoreTest, ComputeBandStats) {
    std::string path = (getTestDir() / "stats_test.tif").string();
    {
        auto ds = gis::core::createRaster(path, 100, 100, 1, GDT_Float32);
        auto* band = ds->GetRasterBand(1);
        std::vector<float> data(100 * 100);
        for (int i = 0; i < 100 * 100; ++i) data[i] = static_cast<float>(i % 256);
        band->RasterIO(GF_Write, 0, 0, 100, 100, data.data(), 100, 100, GDT_Float32, 0, 0);
        band->FlushCache();
    }

    auto ds2 = gis::core::openRaster(path, true);
    auto stats = gis::core::computeBandStats(ds2.get(), 1);
    EXPECT_EQ(stats.dataTypeName, "Float32");
    EXPECT_GE(stats.minVal, 0.0);
    EXPECT_LE(stats.maxVal, 255.0);
}

TEST_F(CoreTest, NoDataValue) {
    std::string path = (getTestDir() / "nodata_test.tif").string();
    {
        auto ds = gis::core::createRaster(path, 10, 10, 1, GDT_Float32);
        ds->GetRasterBand(1)->SetNoDataValue(-9999.0);
        ds->GetRasterBand(1)->FlushCache();
    }

    auto ds2 = gis::core::openRaster(path, true);
    bool hasNoData = false;
    double ndVal = gis::core::getNoDataValue(ds2.get(), 1, &hasNoData);
    EXPECT_TRUE(hasNoData);
    EXPECT_DOUBLE_EQ(ndVal, -9999.0);
}

TEST_F(CoreTest, SetNoDataValue) {
    std::string path = (getTestDir() / "set_nodata_test.tif").string();
    auto ds = gis::core::createRaster(path, 10, 10, 1, GDT_Float32);
    EXPECT_TRUE(gis::core::setNoDataValue(ds.get(), 1, -999.0));

    bool hasNoData = false;
    double ndVal = gis::core::getNoDataValue(ds.get(), 1, &hasNoData);
    EXPECT_TRUE(hasNoData);
    EXPECT_DOUBLE_EQ(ndVal, -999.0);
}

TEST_F(CoreTest, GetRasterInfo) {
    std::string path = (getTestDir() / "info_test.tif").string();
    {
        auto ds = gis::core::createRaster(path, 200, 150, 2, GDT_Byte, "GTiff");
        double adfGT[6] = {116.0, 0.01, 0.0, 40.0, 0.0, -0.01};
        ds->SetGeoTransform(adfGT);
        ds->GetRasterBand(1)->FlushCache();
        ds->GetRasterBand(2)->FlushCache();
    }

    auto ds2 = gis::core::openRaster(path, true);
    auto info = gis::core::getRasterInfo(ds2.get(), path);
    EXPECT_EQ(info.width, 200);
    EXPECT_EQ(info.height, 150);
    EXPECT_EQ(info.bandCount, 2);
    EXPECT_DOUBLE_EQ(info.geoTransform[0], 116.0);
}

TEST_F(CoreTest, ComputeHistogram) {
    std::string path = (getTestDir() / "hist_test.tif").string();
    {
        auto ds = gis::core::createRaster(path, 100, 100, 1, GDT_Byte);
        auto* band = ds->GetRasterBand(1);
        std::vector<uint8_t> data(100 * 100);
        for (int i = 0; i < 100 * 100; ++i) data[i] = static_cast<uint8_t>(i % 256);
        band->RasterIO(GF_Write, 0, 0, 100, 100, data.data(), 100, 100, GDT_Byte, 0, 0);
        band->FlushCache();
    }

    auto ds2 = gis::core::openRaster(path, true);
    auto hist = gis::core::computeHistogram(ds2.get(), 1, 16);
    EXPECT_EQ(hist.size(), 16);
    uint64_t total = 0;
    for (auto& bin : hist) total += bin.count;
    EXPECT_EQ(total, 10000u);
}

TEST_F(CoreTest, IsEpsgCode) {
    EXPECT_TRUE(gis::core::isEpsgCode("EPSG:4326"));
    EXPECT_TRUE(gis::core::isEpsgCode("EPSG:32650"));
    EXPECT_FALSE(gis::core::isEpsgCode("WGS84"));
    EXPECT_FALSE(gis::core::isEpsgCode(""));
}

TEST_F(CoreTest, ParseEpsgCode) {
    EXPECT_EQ(gis::core::parseEpsgCode("EPSG:4326"), 4326);
    EXPECT_EQ(gis::core::parseEpsgCode("EPSG:32650"), 32650);
}

TEST_F(CoreTest, GisError) {
    gis::core::GisError err("test error");
    EXPECT_STREQ(err.what(), "test error");
    EXPECT_THROW(throw gis::core::GisError("boom"), gis::core::GisError);
}

TEST_F(CoreTest, ProgressReporter) {
    gis::core::CliProgress progress;
    EXPECT_NO_THROW(progress.onProgress(0.5));
    EXPECT_NO_THROW(progress.onMessage("test"));
    EXPECT_FALSE(progress.isCancelled());
}

TEST_F(CoreTest, MatsToGdalTiff) {
    std::string srcPath = (getTestDir() / "multi_src.tif").string();
    std::string dstPath = (getTestDir() / "multi_dst.tif").string();

    auto srcDS = gis::core::createRaster(srcPath, 20, 20, 3, GDT_Float32);

    std::vector<cv::Mat> mats;
    for (int i = 0; i < 3; ++i) {
        mats.push_back(cv::Mat(20, 20, CV_32F, cv::Scalar(static_cast<float>(i * 10))));
    }
    gis::core::matsToGdalTiff(mats, srcDS.get(), dstPath);

    auto dstDS = gis::core::openRaster(dstPath, true);
    ASSERT_NE(dstDS, nullptr);
    EXPECT_EQ(dstDS->GetRasterCount(), 3);
}

TEST_F(CoreTest, BuildOverviews) {
    std::string path = (getTestDir() / "ovr_test.tif").string();
    {
        auto ds = gis::core::createRaster(path, 512, 512, 1, GDT_Byte);
        auto* band = ds->GetRasterBand(1);
        std::vector<uint8_t> data(512 * 512, 128);
        band->RasterIO(GF_Write, 0, 0, 512, 512, data.data(), 512, 512, GDT_Byte, 0, 0);
        band->FlushCache();
    }

    auto ds = gis::core::openRaster(path, false);
    bool ok = gis::core::buildOverviews(ds.get(), {2, 4}, "NEAREST");
    EXPECT_TRUE(ok);
}

TEST_F(CoreTest, RasterInfoStructure) {
    std::string path = (getTestDir() / "raster_info_struct.tif").string();
    {
        auto ds = gis::core::createRaster(path, 100, 80, 2, GDT_Float32);
        double adfGT[6] = {120.0, 0.01, 0.0, 30.0, 0.0, -0.01};
        ds->SetGeoTransform(adfGT);
        std::vector<float> band1Data(100 * 80, 12.0f);
        std::vector<float> band2Data(100 * 80, 24.0f);
        band1Data.front() = -9999.0f;
        ds->GetRasterBand(1)->SetNoDataValue(-9999.0);
        ds->GetRasterBand(1)->RasterIO(
            GF_Write, 0, 0, 100, 80, band1Data.data(), 100, 80, GDT_Float32, 0, 0);
        ds->GetRasterBand(2)->RasterIO(
            GF_Write, 0, 0, 100, 80, band2Data.data(), 100, 80, GDT_Float32, 0, 0);
        ds->GetRasterBand(1)->FlushCache();
        ds->GetRasterBand(2)->FlushCache();
    }

    auto ds = gis::core::openRaster(path, true);
    auto info = gis::core::getRasterInfo(ds.get(), path);
    EXPECT_EQ(info.width, 100);
    EXPECT_EQ(info.height, 80);
    EXPECT_EQ(info.bandCount, 2);
    EXPECT_DOUBLE_EQ(info.geoTransform[0], 120.0);
    EXPECT_EQ(info.bands.size(), 2u);
    EXPECT_TRUE(info.bands[0].hasNoData);
    EXPECT_DOUBLE_EQ(info.bands[0].noDataValue, -9999.0);
}

TEST_F(CoreTest, HistogramBins) {
    std::string path = (getTestDir() / "hist_bins.tif").string();
    {
        auto ds = gis::core::createRaster(path, 50, 50, 1, GDT_Byte);
        auto* band = ds->GetRasterBand(1);
        std::vector<uint8_t> data(50 * 50);
        for (int i = 0; i < 50 * 50; ++i) data[i] = static_cast<uint8_t>(i % 50);
        band->RasterIO(GF_Write, 0, 0, 50, 50, data.data(), 50, 50, GDT_Byte, 0, 0);
        band->FlushCache();
    }

    auto ds = gis::core::openRaster(path, true);
    auto hist = gis::core::computeHistogram(ds.get(), 1, 10);
    EXPECT_EQ(hist.size(), 10u);
    EXPECT_GT(hist[0].count, 0u);
}
