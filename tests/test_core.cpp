#include <gtest/gtest.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>
#include <gis/core/types.h>
#include <gis/core/error.h>
#include <gis/core/progress.h>
#include <gis/core/runtime_env.h>
#include <gis/core/spindex_presets.h>
#include <gis/core/text_escape.h>
#include <gis/core/logger_init.h>
#include <gis/core/logger.h>
#include <gis/core/expression_engine.h>
#include <gis/core/tile_grid.h>
#include <gdal_priv.h>
#include <opencv2/core.hpp>
#include <filesystem>
#include <fstream>
#include <set>
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

TEST_F(CoreTest, InitGDALIsIdempotentAndDriversAvailable) {
    EXPECT_NO_THROW(gis::core::initGDAL());
    auto* mgr = GetGDALDriverManager();
    ASSERT_NE(mgr, nullptr);
    EXPECT_GT(mgr->GetDriverCount(), 0);
}

TEST_F(CoreTest, GdalDriverCatalogProbe) {
    auto* driverManager = GetGDALDriverManager();
    ASSERT_NE(driverManager, nullptr);
    ASSERT_GT(driverManager->GetDriverCount(), 0);

    std::set<std::string> driverNames;
    for (int i = 0; i < driverManager->GetDriverCount(); ++i) {
        auto* driver = driverManager->GetDriver(i);
        if (!driver) {
            continue;
        }

        const char* name = driver->GetDescription();
        if (name && *name) {
            driverNames.insert(name);
        }
    }

    EXPECT_TRUE(driverNames.count("GTiff") > 0);
    EXPECT_TRUE(driverNames.count("GPKG") > 0);

    const std::vector<std::string> pointCloudCandidates = {
        "LAS", "LAZ", "COPC", "EPT", "E57", "TileDB"
    };

    std::vector<std::string> supportedPointCloudDrivers;
    for (const auto& candidate : pointCloudCandidates) {
        if (driverNames.count(candidate) > 0) {
            supportedPointCloudDrivers.push_back(candidate);
        }
    }

    std::ostringstream oss;
    oss << "GDAL version=" << GDALVersionInfo("RELEASE_NAME")
        << ", pointcloud_drivers=";
    if (supportedPointCloudDrivers.empty()) {
        oss << "<none>";
    } else {
        for (size_t i = 0; i < supportedPointCloudDrivers.size(); ++i) {
            if (i > 0) {
                oss << ",";
            }
            oss << supportedPointCloudDrivers[i];
        }
    }
    std::cout << oss.str() << std::endl;
}

TEST_F(CoreTest, SpindexPresetCatalogIsStable) {
    const auto values = gis::core::spindexCustomIndexPresetValues();
    const std::vector<std::string> expected = {
        "none",
        "ndvi_alias",
        "ndmi_alias",
        "ndwi_alias",
        "mndwi_alias",
        "ndbi_alias",
        "bsi_alias",
        "gndvi_alias",
        "savi_alias",
        "evi_alias",
        "evi2_alias"
    };
    EXPECT_EQ(values, expected);
    EXPECT_EQ(
        gis::core::spindexCustomIndexPresetExpression("ndvi_alias"),
        "(NIR-RED)/(NIR+RED)");
    EXPECT_EQ(
        gis::core::spindexCustomIndexPresetExpression("mndwi_alias"),
        "(GREEN-SWIR1)/(GREEN+SWIR1)");
    EXPECT_TRUE(gis::core::spindexCustomIndexPresetExpression("missing").empty());
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

TEST_F(CoreTest, CreateRasterCreatesParentDirectories) {
    std::string path = (getTestDir() / "nested" / "auto_created" / "create_parent_test.tif").string();
    {
        auto ds = gis::core::createRaster(path, 16, 12, 1, GDT_Float32);
        ASSERT_NE(ds, nullptr);
    }

    EXPECT_TRUE(fs::exists(path));
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

TEST_F(CoreTest, CliProgressDefaultBehavior) {
    gis::core::CliProgress progress;
    progress.onProgress(0.5);
    progress.onMessage("test");
    EXPECT_FALSE(progress.isCancelled());
    EXPECT_NO_THROW(progress.throwIfCancelled());
}

TEST_F(CoreTest, CancelledExceptionType) {
    gis::core::CancelledException ex("custom message");
    EXPECT_STREQ(ex.what(), "custom message");
    try {
        throw gis::core::CancelledException();
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "操作已取消");
    } catch (...) {
        FAIL() << "CancelledException should be catchable as std::runtime_error";
    }
}

class CancellableProgress : public gis::core::ProgressReporter {
public:
    void onProgress(double) override {}
    void onMessage(const std::string&) override {}
    bool isCancelled() const override { return cancelled_; }
    void setCancelled(bool v) { cancelled_ = v; }
private:
    bool cancelled_ = false;
};

TEST_F(CoreTest, ThrowIfCancelledThrowsWhenCancelled) {
    CancellableProgress progress;
    EXPECT_NO_THROW(progress.throwIfCancelled());
    progress.setCancelled(true);
    EXPECT_THROW(progress.throwIfCancelled(), gis::core::CancelledException);
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
    uint64_t total = 0;
    for (auto& bin : hist) total += bin.count;
    EXPECT_EQ(total, 2500u);
}

TEST_F(CoreTest, FindRuntimePathFromWalksUpToAncestorResourceDir) {
    const fs::path root = gis::tests::testExecutableDir().parent_path().parent_path().parent_path()
        / "tmp" / "gis_tool_runtime_path_case";
    std::filesystem::remove_all(root);
    const fs::path exeDir = root / "build" / "src" / "cli" / "Debug";
    const fs::path pluginsDir = root / "build" / "plugins";
    gis::tests::ensureDirectory(exeDir);
    gis::tests::ensureDirectory(pluginsDir);

    const auto resolved = gis::core::findRuntimePathFrom(exeDir, "plugins");
    EXPECT_EQ(fs::weakly_canonical(resolved), fs::weakly_canonical(pluginsDir));
}

TEST_F(CoreTest, FindPluginDirectoryFromSkipsEmptyLocalPluginsDir) {
    const fs::path root = gis::tests::testExecutableDir().parent_path().parent_path().parent_path()
        / "tmp" / "gis_tool_plugin_dir_case";
    std::filesystem::remove_all(root);
    const fs::path exeDir = root / "build" / "src" / "cli" / "Debug";
    const fs::path localPluginsDir = exeDir / "plugins";
    const fs::path actualPluginsDir = root / "build" / "plugins";
    gis::tests::ensureDirectory(localPluginsDir);
    gis::tests::ensureDirectory(actualPluginsDir);

    std::ofstream(actualPluginsDir / "plugin_vector.dll").put('\n');
    std::ofstream(localPluginsDir / "opencv_core.dll").put('\n');

    const auto resolved = gis::core::findPluginDirectoryFrom(exeDir);
    EXPECT_EQ(fs::weakly_canonical(resolved), fs::weakly_canonical(actualPluginsDir));
}

TEST_F(CoreTest, FindPluginDirectoryFromPrefersNearestPluginsDir) {
    const fs::path root = gis::tests::testExecutableDir().parent_path().parent_path().parent_path()
        / "tmp" / "gis_tool_plugin_dir_outer_case";
    std::filesystem::remove_all(root);
    const fs::path exeDir = root / "build" / "src" / "cli" / "Debug";
    const fs::path localPluginsDir = exeDir / "plugins";
    const fs::path outerPluginsDir = root / "build" / "plugins";
    gis::tests::ensureDirectory(localPluginsDir);
    gis::tests::ensureDirectory(outerPluginsDir);

    std::ofstream(localPluginsDir / "plugin_vector.dll").put('\n');
    std::ofstream(outerPluginsDir / "plugin_vector.dll").put('\n');

    const auto resolved = gis::core::findPluginDirectoryFrom(exeDir);
    EXPECT_EQ(fs::weakly_canonical(resolved), fs::weakly_canonical(localPluginsDir));
}

TEST_F(CoreTest, EscapeForHtmlEscapesSpecialCharacters) {
    EXPECT_EQ(gis::core::escapeForHtml("a&b<c>\"d'e"),
              "a&amp;b&lt;c&gt;&quot;d&#39;e");
}

TEST_F(CoreTest, EscapeForJsSingleQuotedEscapesSpecialCharacters) {
    EXPECT_EQ(gis::core::escapeForJsSingleQuoted("line\nbreak\\quote'"),
              "line\\nbreak\\\\quote\\'");
}

TEST_F(CoreTest, InitDefaultLoggingIsIdempotent) {
    gis::core::initDefaultLogging();
    gis::core::Logger::instance().info("logger smoke", "CoreTest");
    EXPECT_NO_THROW(gis::core::initDefaultLogging());
}

// ============================================================================
// Task 4：表达式引擎（编译一次 + 一元负号 + 变量边界 + 显式失败）
// 回归的是旧字符串替换求值器的已知缺陷：负操作数被算成 0、未知变量静默返回 0.0、
// B12 被当成 B1 后跟数字 2、一元负号不支持。
// ============================================================================

TEST(ExpressionEngine, SupportsUnaryMinusAndNegativeOperands) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    ASSERT_TRUE(gis::core::compileExpression("(B2-B1)/(B2+B1)", {"B1", "B2"}, program, error)) << error;
    bool failed = false;
    // B1 = -0.5, B2 = 0.5  ->  (0.5 - (-0.5)) / (0.5 + (-0.5)) = 1.0 / 0.0 -> 除零
    EXPECT_DOUBLE_EQ(gis::core::evaluateExpression(program, {-0.5, 0.5}, failed), 0.0);
    EXPECT_TRUE(failed);
    // B1 = -0.5, B2 = 1.5  ->  2.0 / 1.0 = 2.0
    failed = false;
    EXPECT_DOUBLE_EQ(gis::core::evaluateExpression(program, {-0.5, 1.5}, failed), 2.0);
    EXPECT_FALSE(failed);
}

TEST(ExpressionEngine, NDVIWithNegativeBandIsComputedNotZeroed) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    ASSERT_TRUE(gis::core::compileExpression("(NIR-RED)/(NIR+RED)", {"RED", "NIR"}, program, error)) << error;
    bool failed = true;
    const double value = gis::core::evaluateExpression(program, {30.0, 70.0}, failed);
    EXPECT_FALSE(failed);
    EXPECT_NEAR(value, 40.0 / 100.0, 1e-12);
}

TEST(ExpressionEngine, RejectsUnknownVariableExplicitly) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    EXPECT_FALSE(gis::core::compileExpression("B9*2", {"B1", "B2", "B3", "B4"}, program, error));
    EXPECT_NE(error.find("B9"), std::string::npos) << error;
}

TEST(ExpressionEngine, DoesNotTreatB12AsB1FollowedByTwo) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    EXPECT_FALSE(gis::core::compileExpression("B12+1", {"B1", "B2"}, program, error)) << error;
}

TEST(ExpressionEngine, HandlesLeadingUnaryMinusAndNestedParens) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    ASSERT_TRUE(gis::core::compileExpression("-B1 * (B2 + 3)", {"B1", "B2"}, program, error)) << error;
    bool failed = false;
    EXPECT_NEAR(gis::core::evaluateExpression(program, {2.0, 5.0}, failed), -16.0, 1e-12);
    EXPECT_FALSE(failed);
}

TEST(ExpressionEngine, KeepsFullDoublePrecisionOfBandValues) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    ASSERT_TRUE(gis::core::compileExpression("B1 * 2", {"B1"}, program, error)) << error;
    bool failed = false;
    const double value = gis::core::evaluateExpression(program, {123456.789012345}, failed);
    EXPECT_NEAR(value, 246913.57802469, 1e-9);
    EXPECT_FALSE(failed);
}

TEST(ExpressionEngine, RejectsUnbalancedParenthesis) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    EXPECT_FALSE(gis::core::compileExpression("(B1 + B2", {"B1", "B2"}, program, error));
    EXPECT_FALSE(error.empty());
}

// R-T4-2：一元 '+' 在数学上是恒等，参考实现把它压成 Negate 会让 +(expr) 反号。
TEST(ExpressionEngine, UnaryPlusIsIdentity) {
    std::vector<gis::core::ExpressionNode> plusProgram;
    std::vector<gis::core::ExpressionNode> plainProgram;
    std::string error;
    ASSERT_TRUE(gis::core::compileExpression("+B1 * (B2 + 3)", {"B1", "B2"}, plusProgram, error)) << error;
    ASSERT_TRUE(gis::core::compileExpression("B1 * (B2 + 3)", {"B1", "B2"}, plainProgram, error)) << error;

    bool plusFailed = false;
    const double plusPositive = gis::core::evaluateExpression(plusProgram, {2.0, 5.0}, plusFailed);
    bool plainFailed = false;
    const double plainPositive = gis::core::evaluateExpression(plainProgram, {2.0, 5.0}, plainFailed);
    EXPECT_FALSE(plusFailed);
    EXPECT_FALSE(plainFailed);
    EXPECT_DOUBLE_EQ(plusPositive, 16.0);
    EXPECT_DOUBLE_EQ(plusPositive, plainPositive);

    plusFailed = false;
    const double plusNegative = gis::core::evaluateExpression(plusProgram, {-2.0, 5.0}, plusFailed);
    plainFailed = false;
    const double plainNegative = gis::core::evaluateExpression(plainProgram, {-2.0, 5.0}, plainFailed);
    EXPECT_FALSE(plusFailed);
    // 正向分支有两条对称断言，负值分支此前只有一条：负号一旦被误弹出，
    // plainProgram 也会跟着判失败，这条补上才有区分力。
    EXPECT_FALSE(plainFailed);
    EXPECT_DOUBLE_EQ(plusNegative, -16.0);
    EXPECT_DOUBLE_EQ(plusNegative, plainNegative);
}

// R-T4-3：连续一元负号必须右结合，--B1 等价于 B1，不得在求值期被判失败。
TEST(ExpressionEngine, DoubleNegationRoundTrip) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    ASSERT_TRUE(gis::core::compileExpression("--B1", {"B1"}, program, error)) << error;
    bool failed = true;
    const double roundTrip = gis::core::evaluateExpression(program, {2.0}, failed);
    EXPECT_FALSE(failed);
    EXPECT_DOUBLE_EQ(roundTrip, 2.0);

    program.clear();
    failed = true;
    ASSERT_TRUE(gis::core::compileExpression("---B1", {"B1"}, program, error)) << error;
    const double triple = gis::core::evaluateExpression(program, {2.0}, failed);
    EXPECT_FALSE(failed);
    EXPECT_DOUBLE_EQ(triple, -2.0);
}

// ============================================================================
// Task 4 fix round 1（F1/F2/F3）
// ============================================================================

namespace {
// 把编译产物压成可读的逆波兰串，用于钉死**算子顺序**而不只是取值。
// 只读对外契约里的 Op 枚举与两个字段，不触碰任何内部实现细节。
// 结构断言不是锦上添花：同优先级链存在"左右结合取值相同"的算子组合
// （见 MultiplyThenDivideChainIsLeftAssociative），只有顺序能区分。
std::string describeProgram(const std::vector<gis::core::ExpressionNode>& program) {
    std::string text;
    for (const auto& node : program) {
        if (!text.empty()) text += ' ';
        switch (node.op) {
            case gis::core::ExpressionNode::Op::Number:
                text += "n" + std::to_string(node.number);
                break;
            case gis::core::ExpressionNode::Op::Variable:
                text += "v" + std::to_string(node.variableIndex);
                break;
            case gis::core::ExpressionNode::Op::Add: text += "Add"; break;
            case gis::core::ExpressionNode::Op::Subtract: text += "Sub"; break;
            case gis::core::ExpressionNode::Op::Multiply: text += "Mul"; break;
            case gis::core::ExpressionNode::Op::Divide: text += "Div"; break;
            case gis::core::ExpressionNode::Op::Negate: text += "Neg"; break;
        }
    }
    return text;
}
} // namespace

// F3 防回归钉：原套件 9 例里**没有任何一条含两个同优先级二元运算符**，
// Negate 也只出现在 `*` 旁边或另一个 Negate 旁边，而负号对乘法是分配的，
// 于是两类"错误但全绿"的构建都检不出来：
//   V1 同优先级不弹出 → `-` 与 `/` 退化成右结合；
//   V2 负号豁免放宽 → Negate 拖到加法之后才弹出，`-B1+B2` 变成 -(B1+B2)。
// 期望值均按正确语义手算，并与审查者实测（build/tmp-r4probe/out2.txt）一致。
// 注意：2-3-4 的左结合答案是 -5 而不是 -3，8/4/2 是 1 而不是 4。
TEST(ExpressionEngine, SubtractThenAddChainIsLeftAssociative) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    ASSERT_TRUE(gis::core::compileExpression("B1-B2+B3", {"B1", "B2", "B3"}, program, error)) << error;
    EXPECT_EQ(describeProgram(program), "v0 v1 Sub v2 Add");
    bool failed = true;
    // (1 - 2) + 3 = 2；右结合会算成 1 - (2 + 3) = -4。
    EXPECT_DOUBLE_EQ(gis::core::evaluateExpression(program, {1.0, 2.0, 3.0}, failed), 2.0);
    EXPECT_FALSE(failed);
}

TEST(ExpressionEngine, MultiplyThenDivideChainIsLeftAssociative) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    ASSERT_TRUE(gis::core::compileExpression("B1*B2/B3", {"B1", "B2", "B3"}, program, error)) << error;
    bool failed = true;
    // (8 * 4) / 2 = 16，而 8 * (4 / 2) = 16 —— 这组取值下两种结合方式**同值**，
    // 所以这条用例的价值全在结构断言上：右结合构建会产出 "v0 v1 v2 Div Mul"。
    EXPECT_EQ(describeProgram(program), "v0 v1 Mul v2 Div");
    EXPECT_DOUBLE_EQ(gis::core::evaluateExpression(program, {8.0, 4.0, 2.0}, failed), 16.0);
    EXPECT_FALSE(failed);
}

TEST(ExpressionEngine, DivideThenDivideChainIsLeftAssociative) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    ASSERT_TRUE(gis::core::compileExpression("B1/B2/B3", {"B1", "B2", "B3"}, program, error)) << error;
    EXPECT_EQ(describeProgram(program), "v0 v1 Div v2 Div");
    bool failed = true;
    // (8 / 4) / 2 = 1；右结合会算成 8 / (4 / 2) = 4。
    EXPECT_DOUBLE_EQ(gis::core::evaluateExpression(program, {8.0, 4.0, 2.0}, failed), 1.0);
    EXPECT_FALSE(failed);
}

TEST(ExpressionEngine, NegateAdjacentToAddAppliesOnlyToFirstOperand) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    ASSERT_TRUE(gis::core::compileExpression("-B1+B2", {"B1", "B2"}, program, error)) << error;
    EXPECT_EQ(describeProgram(program), "v0 Neg v1 Add");
    bool failed = true;
    // -2 + 5 = 3；若 Negate 的右结合豁免被放宽成"负号碰上任何运算符都不弹出"，
    // 就会拖到加法之后才写进 output，得到 -(2 + 5) = -7。
    // 负号对乘法是分配的，所以现有每条 Negate 用例都区分不了这个错误——只有加法邻接能。
    EXPECT_DOUBLE_EQ(gis::core::evaluateExpression(program, {2.0, 5.0}, failed), 3.0);
    EXPECT_FALSE(failed);
}

// F1：'(' 与 ')' 两个位置此前无条件改写 expectOperand，
// 六个畸形输入编译成功、只在逐像元求值时失败——正是本批次要消灭的"错误出现得太晚且被 true 掩盖"。
TEST(ExpressionEngine, RejectsOperandAdjacentToParenthesis) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    // 实测泄漏形态：rpn = "v0 v1 1 Add"（隐式乘法，静默编译成功）。
    EXPECT_FALSE(gis::core::compileExpression("B1(B2+1)", {"B1", "B2"}, program, error));
    EXPECT_EQ(error, "表达式语法错误：操作数相邻，缺少运算符");
    // 实测泄漏形态：rpn = "v0 v1"（两栈值 → 求值期判失败，编译期却返回 true）。
    program.clear();
    error.clear();
    EXPECT_FALSE(gis::core::compileExpression("(B1)(B2)", {"B1", "B2"}, program, error));
    EXPECT_FALSE(error.empty()) << error;
    EXPECT_TRUE(program.empty());
}

TEST(ExpressionEngine, RejectsEmptyOrOperatorTerminatedParenGroup) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    // 四条都曾在编译期返回 true，只在求值时因栈不足判失败。
    const char* shapes[] = {"()", "B1 * ()", "(B1+)", "-()"};
    for (const char* shape : shapes) {
        program.clear();
        error.clear();
        EXPECT_FALSE(gis::core::compileExpression(shape, {"B1", "B2", "B3"}, program, error)) << shape;
        EXPECT_FALSE(error.empty()) << shape;
        EXPECT_TRUE(program.empty()) << shape;
    }
}

// F1 的正面对照：新增的两个守卫不得把合法形态一起拒掉。
TEST(ExpressionEngine, AcceptsRedundantParenthesesAndUnaryMinusForms) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    bool failed = false;

    ASSERT_TRUE(gis::core::compileExpression("(-B1)", {"B1"}, program, error)) << error;
    EXPECT_DOUBLE_EQ(gis::core::evaluateExpression(program, {2.0}, failed), -2.0);
    EXPECT_FALSE(failed);

    program.clear();
    failed = false;
    ASSERT_TRUE(gis::core::compileExpression("(B1)*(B2)", {"B1", "B2"}, program, error)) << error;
    EXPECT_DOUBLE_EQ(gis::core::evaluateExpression(program, {3.0, 4.0}, failed), 12.0);
    EXPECT_FALSE(failed);

    program.clear();
    failed = false;
    ASSERT_TRUE(gis::core::compileExpression("-(-B1)", {"B1"}, program, error)) << error;
    EXPECT_DOUBLE_EQ(gis::core::evaluateExpression(program, {2.0}, failed), 2.0);
    EXPECT_FALSE(failed);

    // 审查者实测的正确形态，别改坏：B1*-B2*B3 于 {2,3,4} = -24。
    program.clear();
    failed = true;
    ASSERT_TRUE(gis::core::compileExpression("B1*-B2*B3", {"B1", "B2", "B3"}, program, error)) << error;
    EXPECT_EQ(describeProgram(program), "v0 v1 Neg Mul v2 Mul");
    EXPECT_DOUBLE_EQ(gis::core::evaluateExpression(program, {2.0, 3.0, 4.0}, failed), -24.0);
    EXPECT_FALSE(failed);
}

// F2：除零必须**立即**返回。原实现在 :221-224 置了 flag 与 0.0 却继续把毒化值压栈，
// 于是交回"用 0.0 参与后续运算"得到的数，与 expression_engine.h:21 的承诺不符。
// 现有 (B2-B1)/(B2+B1) 那条恰好把 Divide 放在最后一个算子，改与不改都返回 0.0，抓不到缺陷；
// 这里用两条"除法不在末尾"的形状。
TEST(ExpressionEngine, DivideByZeroStopsEvaluationImmediately) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;
    ASSERT_TRUE(gis::core::compileExpression("5-B1/B2", {"B1", "B2"}, program, error)) << error;
    bool failed = false;
    // 5 - (1/0)：继续求值得 5 - 0.0 = 5.0（一个看似合理的数），必须交回 0.0。
    EXPECT_DOUBLE_EQ(gis::core::evaluateExpression(program, {1.0, 0.0}, failed), 0.0);
    EXPECT_TRUE(failed);

    program.clear();
    error.clear();
    ASSERT_TRUE(gis::core::compileExpression("(B1+B2)/B3-4", {"B1", "B2", "B3"}, program, error)) << error;
    failed = false;
    // 真实形态 (1+2)/0 - 4：继续求值得 -4.0，正是会写进栅格的那类脏值。
    EXPECT_DOUBLE_EQ(gis::core::evaluateExpression(program, {1.0, 2.0, 0.0}, failed), 0.0);
    EXPECT_TRUE(failed);
}

// ============================================================================
// Task 5 追加义务 1：编译输入的卫生界限（表达式是 GUI/CLI 用户可直接控制的字符串）
//
// 旧求值器有 kMaxExpressionLength=1024 / kMaxExpressionDepth=64，Task 5 删掉它的同一次提交
// 必须补上界限，否则是一次静默的输入校验倒退。
// 这里刻意**没有**重新引入嵌套深度上限：新引擎的解析与求值都用堆上的 std::vector/std::stack，
// 不存在递归爆栈（Task 4 实测 20000 层嵌套不崩），所以这两条界限是"输入卫生 + 逐像元成本可预期"，
// 不是内存安全。产物规模上限定在 512 = 输入预算 1024 的一半，理由是"编译产物不可能比输入更密"：
// 一个节点至少要消耗一个输入字符，而括号与空格消耗的字符不产生节点，
// 所以产物规模上必须严格低于 1024 字符输入所能产出的最大节点数（1023），否则这条分支不可达、不可测。
// ============================================================================

TEST(ExpressionEngine, RejectsInputLengthAboveBound) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;

    // 用空格补足长度：空格被分词器跳过、不产生节点，所以本用例只咬住"输入长度"这一条界限，
    // 产物恒为 2 个数字 + 1 个加法 = 3 节点，远低于 512 的产物上限，两条界限不会互相掩盖。
    const std::string atLimit = std::string(1024 - 5, ' ') + "B1+B2";   // 恰好 1024 字符
    const std::string overLimit = std::string(1025 - 5, ' ') + "B1+B2"; // 上限 + 1
    ASSERT_EQ(atLimit.size(), 1024u);
    ASSERT_EQ(overLimit.size(), 1025u);

    EXPECT_TRUE(gis::core::compileExpression(atLimit, {"B1", "B2"}, program, error)) << error;
    EXPECT_EQ(program.size(), 3u);

    program.clear();
    error.clear();
    EXPECT_FALSE(gis::core::compileExpression(overLimit, {"B1", "B2"}, program, error));
    EXPECT_TRUE(program.empty());
    // 错误必须可读且把"实际长度"与"上限"都给出来（旧实现是静默返回 0.0）。
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("1025"), std::string::npos) << error;
    EXPECT_NE(error.find("1024"), std::string::npos) << error;
}

TEST(ExpressionEngine, RejectsCompiledProgramAboveBound) {
    std::vector<gis::core::ExpressionNode> program;
    std::string error;

    // n 个 "1" 由 n-1 个加号相连：2n-1 个字符、2n-1 个节点（每个数字与每个运算符各一个节点）。
    auto ones = [](int count) {
        std::string text;
        for (int i = 0; i < count; ++i) {
            if (i != 0) text += '+';
            text += '1';
        }
        return text;
    };

    // 恰好 512 个节点 = 256 个数字 + 255 个加号 + 1 个一元负号（括号不产生节点），
    // 长度 514 字符 < 1024，因此拒不拒只由产物规模这条界限决定。
    const std::string atLimit = "-(" + ones(256) + ")";
    EXPECT_TRUE(gis::core::compileExpression(atLimit, {"B1", "B2"}, program, error)) << error;
    ASSERT_EQ(program.size(), 512u);

    // 上限 + 1 个节点 = 257 个数字 + 256 个加号，长度 513 字符，同样不触碰输入长度界限。
    program.clear();
    error.clear();
    const std::string overLimit = ones(257);
    EXPECT_FALSE(gis::core::compileExpression(overLimit, {"B1", "B2"}, program, error));
    EXPECT_TRUE(program.empty());
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("513"), std::string::npos) << error;
    EXPECT_NE(error.find("512"), std::string::npos) << error;
}

// ===== Task 17: XYZ 瓦片网格纯函数（gis::core::tile_grid）=====
// WHY 纯函数层先钉死（R-T17-7 第 1 点）：tile 插件的端到端用例要消费"瓦片索引 → EPSG:3857
// 世界矩形"这条几何；若这层几何没有独立断言，端到端就只能拿插件自己的换算去验插件自己的
// 产物，断言退化成同义反复（R-T17-9 第 2 点对 tileWorldRect 的处置口径）。
// kWebMercatorHalfWidth 与实现共用同一常量口径：20037508.342789244（EPSG:3857 世界半宽，米）。

TEST(TileGrid, LevelZeroIsSingleTileCoveringWorld) {
    const auto grid = gis::core::mercatorTileGrid(0, 256);
    EXPECT_EQ(grid.globalWidth, 256);
    EXPECT_EQ(grid.globalHeight, 256);
    EXPECT_NEAR(grid.worldMaxX, 20037508.342789244, 1e-3);
    EXPECT_NEAR(grid.worldMinY, -20037508.342789244, 1e-3);

    const auto win = gis::core::tileWindow(grid, 256, 0, 0);
    ASSERT_TRUE(win.valid);
    EXPECT_EQ(win.xOff, 0);
    EXPECT_EQ(win.yOff, 0);
    EXPECT_EQ(win.xSize, 256);
}

TEST(TileGrid, LevelOneUsesTwoByTwoGrid) {
    const auto grid = gis::core::mercatorTileGrid(1, 256);
    EXPECT_EQ(grid.globalWidth, 512);
    const auto win = gis::core::tileWindow(grid, 256, 1, 1);
    ASSERT_TRUE(win.valid);
    EXPECT_EQ(win.xOff, 256);
    EXPECT_EQ(win.yOff, 256);
    EXPECT_FALSE(gis::core::tileWindow(grid, 256, 2, 0).valid);
}

TEST(TileGrid, ResolutionHalvesPerZoom) {
    const auto z0 = gis::core::mercatorTileGrid(0, 256);
    const auto z1 = gis::core::mercatorTileGrid(1, 256);
    const double res0 = (z0.worldMaxX - z0.worldMinX) / z0.globalWidth;
    const double res1 = (z1.worldMaxX - z1.worldMinX) / z1.globalWidth;
    EXPECT_NEAR(res1, res0 / 2.0, 1e-9);
}

// R-T17-7 第 1 点要求的钉边用例：z=1 时世界半宽切成 2×2 瓦片，四边必须逐条钉死。
// 数值出处：res = 2*20037508.342789244/512 = 78271.51696...，单瓦片 256px 跨 256*res。
// (0,0) 是西北角瓦片：x∈[-k,0]，y∈[0,k]；(1,1) 是东南角瓦片：x∈[0,k]，y∈[-k,0]。
// WHY 连瓦片越界也要断：tileWindow 已经钉了非法索引 valid=false，世界矩形同样不得静默
// 给出一个"看似合法"的窗口，否则越界瓦片会把几何错误带进端到端产物。
TEST(TileGrid, TileWorldRectPinsCornersAtLevelOne) {
    const auto grid = gis::core::mercatorTileGrid(1, 256);
    const double k = 20037508.342789244;

    const auto nw = gis::core::tileWorldRect(grid, 256, 0, 0);
    ASSERT_TRUE(nw.valid);
    EXPECT_NEAR(nw.minX, -k, 1e-6);
    EXPECT_NEAR(nw.maxX, 0.0, 1e-6);
    EXPECT_NEAR(nw.maxY, k, 1e-6);
    EXPECT_NEAR(nw.minY, 0.0, 1e-6);

    const auto se = gis::core::tileWorldRect(grid, 256, 1, 1);
    ASSERT_TRUE(se.valid);
    EXPECT_NEAR(se.minX, 0.0, 1e-6);
    EXPECT_NEAR(se.maxX, k, 1e-6);
    EXPECT_NEAR(se.minY, -k, 1e-6);
    EXPECT_NEAR(se.maxY, 0.0, 1e-6);

    EXPECT_FALSE(gis::core::tileWorldRect(grid, 256, 2, 0).valid);
    EXPECT_FALSE(gis::core::tileWorldRect(grid, 256, 0, 2).valid);
}

// F7（Task 17 修复轮 1，审查 m-2）：mercatorTileGrid 的**钳位臂**契约。
// 只钉现状、不改行为（收窄或报错都是未授权的 API 语义变更）。
// 数值按 tile_grid.cpp:33-38 的定义独立算（count = ts<<z、cap = (INT_MAX/ts)*ts、取 min），
// 不调用被测实现推期望：
//   (z=22, ts=1024) count=4294967296 cap=2147482624 ⇒ globalWidth=2147482624 ⇒ 瓦片数 2097151
//   (z=21, ts=1024) count=2147483648 cap=2147482624 ⇒ globalWidth=2147482624 ⇒ 瓦片数 2097151
//   (z=40~45, ts=256) z 先钳到 40 ⇒ count=281474976710656 cap=2147483392 ⇒ 瓦片数 8388607
//   安全域内不生效（头注释承诺的那两档）：(z=20, ts=1024) 与 (z=22, ts=256) 都恰好 1073741824
// **这个契约有损，承认它有损**：z=22/ts=1024 的真值是 2^22=4194304 列，钳位后只剩 2097151 列
// （少一半）；z=21/ts=1024 真值 2097152 列，钳位后少 1 列。而 tile_pixel_size 的声明域是 [128,1024]、
// doTile 的 z 封顶 22 ⇒ (ts=512,z=22)、(ts=1024,z=21)、(ts=1024,z=22) 都是**面板合法但网格被静默
// 缩档**的组合。登记挂账 **N68**（本报告 F7 小节），处置（收窄域 / 显式报错 / 换 64 位网格）留后续任务。
TEST(TileGrid, ClampArmShrinksGridLossilyButStaysSelfConsistent) {
    const int kClampedWidth1024 = 2147482624;   // (INT_MAX/1024)*1024
    const int kClampedWidth256 = 2147483392;    // (INT_MAX/256)*256

    const auto z22 = gis::core::mercatorTileGrid(22, 1024);
    EXPECT_EQ(z22.globalWidth, kClampedWidth1024);
    EXPECT_EQ(z22.globalHeight, kClampedWidth1024);
    // 世界四边不受钳位影响（它一直是 EPSG:3857 全幅），只有像元数被缩档。
    EXPECT_NEAR(z22.worldMaxX, 20037508.342789244, 1e-3);
    EXPECT_NEAR(z22.worldMinY, -20037508.342789244, 1e-3);

    // 钳位后网格自洽：瓦片数 = globalWidth/ts，最后一张仍合法、越界一张必须 invalid。
    const int tiles = z22.globalWidth / 1024;
    EXPECT_EQ(tiles, 2097151);
    const auto last = gis::core::tileWindow(z22, 1024, tiles - 1, tiles - 1);
    ASSERT_TRUE(last.valid);
    EXPECT_EQ(last.xOff, (tiles - 1) * 1024);
    EXPECT_EQ(last.xSize, 1024);
    EXPECT_FALSE(gis::core::tileWindow(z22, 1024, tiles, 0).valid);
    EXPECT_FALSE(gis::core::tileWindow(z22, 1024, 0, tiles).valid);
    const auto lastRect = gis::core::tileWorldRect(z22, 1024, tiles - 1, tiles - 1);
    ASSERT_TRUE(lastRect.valid);
    EXPECT_LT(lastRect.minX, lastRect.maxX);
    EXPECT_LE(lastRect.maxX, z22.worldMaxX + 1e-6);
    EXPECT_GE(lastRect.minY, z22.worldMinY - 1e-6);

    // z=21 与 z=22 在 ts=1024 下钳位到**同一个** globalWidth ⇒ 两档层级给出同一张网格，
    // 分辨率不再逐档减半（真值 2097152 列 vs 钳位后 2097151 列）。这条就是 N68 的可观测形态。
    const auto z21 = gis::core::mercatorTileGrid(21, 1024);
    EXPECT_EQ(z21.globalWidth, kClampedWidth1024);
    EXPECT_EQ(z21.globalWidth, z22.globalWidth);
    EXPECT_EQ(z21.globalWidth / 1024, 2097151);

    // zoom 上界臂：z>40 一律按 40 走（ts=256 ⇒ 2147483392 / 8388607 张）。
    const auto z45 = gis::core::mercatorTileGrid(45, 256);
    const auto z40 = gis::core::mercatorTileGrid(40, 256);
    EXPECT_EQ(z45.globalWidth, kClampedWidth256);
    EXPECT_EQ(z45.globalWidth, z40.globalWidth);
    EXPECT_EQ(z45.globalWidth / 256, 8388607);

    // 反向钉：头注释承诺的安全域内钳位**不生效**（这两档真值与 globalWidth 逐位一致）。
    EXPECT_EQ(gis::core::mercatorTileGrid(20, 1024).globalWidth, 1073741824);
    EXPECT_EQ(gis::core::mercatorTileGrid(22, 256).globalWidth, 1073741824);
    EXPECT_EQ(gis::core::mercatorTileGrid(22, 256).globalWidth / 256, 4194304);   // == 2^22
}

// F7 第 (ii) 件：zoom<0 按 0、tileSize<1 按 1 的臂（头注释 :35-37 的承诺，此前零测试）。
// 承诺的形态是"不拒非法输入，但不得产生 0 宽网格或除零"⇒ 这里钉的就是 globalWidth>0 与同源窗口自洽。
TEST(TileGrid, IllegalZoomAndTileSizeFallBackToOneSizedArms) {
    const auto negZoom = gis::core::mercatorTileGrid(-3, 256);
    EXPECT_EQ(negZoom.globalWidth, 256);          // clamp(-3,0,40) = 0 ⇒ 256<<0
    EXPECT_EQ(negZoom.globalHeight, 256);
    EXPECT_TRUE(gis::core::tileWindow(negZoom, 256, 0, 0).valid);
    EXPECT_FALSE(gis::core::tileWindow(negZoom, 256, 1, 0).valid);   // 与 z=0 同形：只有一张

    // tileSize=0 / 负数：ts 取 1 ⇒ z=1 时全球 2×2 像元、2 张瓦片；不得出现 0 宽网格或除零。
    const auto tsZero = gis::core::mercatorTileGrid(1, 0);
    const auto tsNegative = gis::core::mercatorTileGrid(1, -7);
    EXPECT_GT(tsZero.globalWidth, 0);
    EXPECT_EQ(tsZero.globalWidth, 2);
    EXPECT_EQ(tsNegative.globalWidth, 2);
    const auto win = gis::core::tileWindow(tsZero, 0, 1, 1);        // tileWindow 侧同样按 1 处理
    ASSERT_TRUE(win.valid);
    EXPECT_EQ(win.xOff, 1);
    EXPECT_EQ(win.xSize, 1);
    const auto rect = gis::core::tileWorldRect(tsZero, -5, 1, 0);
    ASSERT_TRUE(rect.valid);
    EXPECT_GT(rect.maxX, rect.minX);
    EXPECT_GT(rect.maxY, rect.minY);
}
