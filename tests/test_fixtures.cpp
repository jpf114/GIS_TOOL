#include <gtest/gtest.h>
#include <gis/core/gdal_wrapper.h>
#include "raster_fixture.h"
#include "test_support.h"

namespace fs = std::filesystem;

TEST(RasterFixture, WritesProjectedRasterWithNoDataAndReadsBack) {
    gis::core::initGDAL();
    const auto dir = gis::tests::defaultTestOutputDir("fixture_output");
    gis::tests::ensureDirectory(dir);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg3857;
    options.hasNoData = true;
    options.noData = -9999.0;

    std::vector<float> data(9, 0.0f);
    data[4] = 5.0f;
    data[0] = -9999.0f;

    const auto path = gis::tests::writeFloatRaster(dir, "fixture_3857.tif", 3, 3, data, options);

    EXPECT_EQ(gis::tests::readCrsAuthorityCode(path), "EPSG:3857");
    EXPECT_EQ(gis::tests::readBandCount(path), 1);
    EXPECT_FLOAT_EQ(gis::tests::readPixel(path, 1, 1, 1), 5.0f);
    double noData = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(path, 1, noData));
    EXPECT_DOUBLE_EQ(noData, -9999.0);
}

TEST(RasterFixture, WritesGeographicBandsWithCustomGeoTransform) {
    gis::core::initGDAL();
    const auto dir = gis::tests::defaultTestOutputDir("fixture_output");

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg4326;
    options.pixelSize = 0.5;

    const auto path = gis::tests::writeConstantBandsRaster(
        dir, "fixture_4326.tif", 2, 2, {10.0f, 20.0f, 30.0f}, options);

    EXPECT_EQ(gis::tests::readCrsAuthorityCode(path), "EPSG:4326");
    EXPECT_EQ(gis::tests::readBandCount(path), 3);
    EXPECT_FLOAT_EQ(gis::tests::readPixel(path, 2, 0, 0), 20.0f);
    const auto gt = gis::tests::readGeoTransform(path);
    EXPECT_DOUBLE_EQ(gt[1], 0.5);
    EXPECT_DOUBLE_EQ(gt[5], -0.5);
}

TEST(RasterFixture, NoCrsIsReportedAsEmptyAuthority) {
    gis::core::initGDAL();
    const auto dir = gis::tests::defaultTestOutputDir("fixture_output");
    const auto path = gis::tests::writeFloatRaster(
        dir, "fixture_nocrs.tif", 2, 2, std::vector<float>(4, 1.0f));

    // 先正向钉住"文件确实写出来并且读得回来"，再断言"没有 CRS"。
    // 空串既是 writeFloatRaster 的失败哨兵，也是 readCrsAuthorityCode 的失败哨兵，
    // 只有下面两行把"文件可读"与"无 CRS"分开断言，本用例才不会 vacuous pass
    // （readBandCount 的失败哨兵是 -1，与任何合法波段数都不撞车）。
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(gis::tests::readBandCount(path), 1);
    EXPECT_TRUE(gis::tests::readCrsAuthorityCode(path).empty());
}
