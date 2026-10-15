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

// D5（Task 1 deferred minor）：把"夹具只支持 Float32"这条判据抽成纯谓词 dtypeSupported 后，
// 它就能被直接断言——不需要 gtest-spi 去捕获写入函数里的 ADD_FAILURE()，
// 也不会像哨兵分支那样"可达但无法正向测试"。三个写入函数的守卫调的就是这同一个谓词，
// 所以本用例锁住的是判据本身，而不是某条消息文本。
TEST(RasterFixture, DtypeSupportedAcceptsOnlyFloat32) {
    gis::tests::RasterFixtureOptions defaults;
    EXPECT_EQ(defaults.dtype, GDT_Float32);
    EXPECT_TRUE(gis::tests::dtypeSupported(defaults));

    // 逐个非 Float32 类型都必须被拒：整型夹具还没有实现，静默按 Float32 写会得到
    // 与调用方预期不符的栅格（详见 raster_fixture.h 的数据缓冲约定）。
    const GDALDataType rejected[] = {GDT_Byte, GDT_UInt16, GDT_Int16, GDT_Int32, GDT_Float64};
    for (const GDALDataType dtype : rejected) {
        gis::tests::RasterFixtureOptions options;
        options.dtype = dtype;
        EXPECT_FALSE(gis::tests::dtypeSupported(options)) << "dtype=" << dtype;
    }

    // 判据只看 dtype：CRS / NoData 之类的正交开关不得影响它。
    gis::tests::RasterFixtureOptions orthogonal;
    orthogonal.crs = gis::tests::FixtureCrs::Epsg3857;
    orthogonal.hasNoData = true;
    orthogonal.noData = -1.0;
    EXPECT_TRUE(gis::tests::dtypeSupported(orthogonal));
}

// R-T6-3：writeFloatBandsRaster 的独立自检（Task 6 的跨波段污染用例依赖它）。
// 与插件用例分开，是为了让"输出波段数不对/取值不对"这类红能确定归到插件而不是夹具：
// 夹具自己的按波段+按像元写入必须先在无插件参与的情况下被证明是对的。
TEST(RasterFixture, WritesPerBandPixelDataAndReadsBack) {
    gis::core::initGDAL();
    const auto dir = gis::tests::defaultTestOutputDir("fixture_output");
    gis::tests::ensureDirectory(dir);

    gis::tests::RasterFixtureOptions options;
    options.crs = gis::tests::FixtureCrs::Epsg4326;
    options.hasNoData = true;
    options.noData = -9999.0;

    const auto path = gis::tests::writeFloatBandsRaster(
        dir, "fixture_per_band.tif", 2, 1, {{0.0f, 9.0f}, {7.0f, 7.0f}}, options);
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(gis::tests::readBandCount(path), 2);
    EXPECT_EQ(gis::tests::readCrsAuthorityCode(path), "EPSG:4326");
    // 逐波段、逐像元都各归各的值（writeConstantBandsRaster 只能逐波段常量，表达不出 band1）。
    EXPECT_FLOAT_EQ(gis::tests::readPixel(path, 1, 0, 0), 0.0f);
    EXPECT_FLOAT_EQ(gis::tests::readPixel(path, 1, 1, 0), 9.0f);
    EXPECT_FLOAT_EQ(gis::tests::readPixel(path, 2, 0, 0), 7.0f);
    EXPECT_FLOAT_EQ(gis::tests::readPixel(path, 2, 1, 0), 7.0f);
    double noData = 0.0;
    ASSERT_TRUE(gis::tests::readNoDataValue(path, 2, noData));
    EXPECT_DOUBLE_EQ(noData, -9999.0);
}

// F2（fix round 1，Minor 3）：钉住 writeFloatBandsRaster 的元素主序是**行主序**。
// 上面那条自检用的是 2×1 栅格——只有一行时"行主序 / 列主序 / 转置写出"给出的像元序列完全相同；
// writeFloatRaster 的 3×3 自检也只断了转置对称的 (1,1)。这个夹具是后续所有任务数值断言的
// 数据入口，主序错了会让一批用例集体取到错像元却全绿，所以这里用**非对称**的 2×2 波段把
// (x, y) → data[y*width + x] 显式钉死（红证据：把写出临时改成列主序后本用例必须变红）。
TEST(RasterFixture, WritesBandDataInRowMajorPixelOrder) {
    gis::core::initGDAL();
    const auto dir = gis::tests::defaultTestOutputDir("fixture_output");
    gis::tests::ensureDirectory(dir);

    // 每个像元值都唯一编码了"波段 + 行 + 列"，任何转置、列主序或波段错位都会撞到另一个值。
    const auto path = gis::tests::writeFloatBandsRaster(
        dir, "fixture_row_major.tif", 2, 2,
        {{1.0f, 2.0f, 3.0f, 4.0f},       // 行主序：row0 = (1, 2)，row1 = (3, 4)
         {11.0f, 12.0f, 13.0f, 14.0f}});
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(gis::tests::readBandCount(path), 2);

    // 这两条是主序鉴别力所在：列主序写出会得到 (x=1,y=0)→3、(x=0,y=1)→2，两条同时变红。
    EXPECT_FLOAT_EQ(gis::tests::readPixel(path, 1, 1, 0), 2.0f);
    EXPECT_FLOAT_EQ(gis::tests::readPixel(path, 1, 0, 1), 3.0f);
    // 补齐另外两个角，排除"只写了半个波段/半幅图"的形态。
    EXPECT_FLOAT_EQ(gis::tests::readPixel(path, 1, 0, 0), 1.0f);
    EXPECT_FLOAT_EQ(gis::tests::readPixel(path, 1, 1, 1), 4.0f);
    // 波段维也一起钉住：band2 的 (1,0)、(0,1) 必须是 12、13，不是 band1 的值。
    EXPECT_FLOAT_EQ(gis::tests::readPixel(path, 2, 1, 0), 12.0f);
    EXPECT_FLOAT_EQ(gis::tests::readPixel(path, 2, 0, 1), 13.0f);
    EXPECT_FLOAT_EQ(gis::tests::readPixel(path, 2, 1, 1), 14.0f);
}
