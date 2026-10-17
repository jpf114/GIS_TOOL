#include <gtest/gtest.h>
#include <gis/framework/plugin.h>
#include <gis/framework/param_spec.h>
#include <gis/framework/plugin_manager.h>
#include <gis/framework/result.h>
#include <gis/framework/result_display.h>
// Task 14：WorkflowRunner::execute 的第一批用例（改动前 tests/ 里没有任何 Workflow 用例）。
#include <gis/framework/workflow.h>
#include <gis/core/gdal_wrapper.h>
#include "../src/gui/gui_data_support.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "test_support.h"
#include "raster_fixture.h"

TEST(FrameworkTest, ResultOk) {
    auto r = gis::framework::Result::ok("success", "/path/to/output");
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.message, "success");
    EXPECT_EQ(r.outputPath, "/path/to/output");
}

TEST(FrameworkTest, ResultFail) {
    auto r = gis::framework::Result::fail("something went wrong");
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.message, "something went wrong");
    EXPECT_TRUE(r.outputPath.empty());
}

TEST(FrameworkTest, ResultCancelled) {
    auto r = gis::framework::Result::cancelled();
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(r.isCancelled);
    EXPECT_EQ(r.message, "操作已取消");
    EXPECT_TRUE(r.outputPath.empty());

    auto r2 = gis::framework::Result::cancelled("custom cancel");
    EXPECT_FALSE(r2.success);
    EXPECT_TRUE(r2.isCancelled);
    EXPECT_EQ(r2.message, "custom cancel");
}

TEST(FrameworkTest, ResultCancelledFieldDistinctFromFail) {
    auto fail = gis::framework::Result::fail("error");
    auto cancelled = gis::framework::Result::cancelled("error");
    EXPECT_FALSE(fail.isCancelled);
    EXPECT_TRUE(cancelled.isCancelled);
}

TEST(FrameworkTest, ParamSpecDefaults) {
    gis::framework::ParamSpec spec;
    spec.key = "test_key";
    spec.displayName = "Test Key";
    spec.description = "A test parameter";
    spec.type = gis::framework::ParamType::String;
    spec.required = true;

    EXPECT_EQ(spec.key, "test_key");
    EXPECT_EQ(spec.displayName, "Test Key");
    EXPECT_EQ(spec.type, gis::framework::ParamType::String);
    EXPECT_TRUE(spec.required);
}

TEST(FrameworkTest, ParamValueString) {
    gis::framework::ParamValue val = std::string("hello");
    auto result = gis::framework::getParam<std::string>(
        {{"key", val}}, "key", "default");
    EXPECT_EQ(result, "hello");
}

TEST(FrameworkTest, ParamValueInt) {
    gis::framework::ParamValue val = 42;
    auto result = gis::framework::getParam<int>(
        {{"key", val}}, "key", 0);
    EXPECT_EQ(result, 42);
}

TEST(FrameworkTest, ParamValueDouble) {
    gis::framework::ParamValue val = 3.14;
    auto result = gis::framework::getParam<double>(
        {{"key", val}}, "key", 0.0);
    EXPECT_NEAR(result, 3.14, 1e-10);
}

TEST(FrameworkTest, ParamValueBool) {
    gis::framework::ParamValue val = true;
    auto result = gis::framework::getParam<bool>(
        {{"key", val}}, "key", false);
    EXPECT_TRUE(result);
}

TEST(FrameworkTest, ParamValueDefault) {
    auto result = gis::framework::getParam<std::string>(
        {}, "missing_key", "fallback");
    EXPECT_EQ(result, "fallback");
}

TEST(FrameworkTest, ParamValueExtent) {
    std::array<double, 4> ext = {1.0, 2.0, 3.0, 4.0};
    gis::framework::ParamValue val = ext;
    auto result = gis::framework::getParam<std::array<double, 4>>(
        {{"extent", val}}, "extent", {0, 0, 0, 0});
    EXPECT_DOUBLE_EQ(result[0], 1.0);
    EXPECT_DOUBLE_EQ(result[1], 2.0);
    EXPECT_DOUBLE_EQ(result[2], 3.0);
    EXPECT_DOUBLE_EQ(result[3], 4.0);
}

TEST(FrameworkTest, TryParseParamValueRejectsInvalidInteger) {
    gis::framework::ParamSpec spec;
    spec.key = "band";
    spec.displayName = "波段";
    spec.type = gis::framework::ParamType::Int;

    gis::framework::ParamValue value;
    std::string error;
    EXPECT_FALSE(gis::framework::tryParseParamValue(spec, "abc", value, error));
    EXPECT_NE(error.find("不是有效整数"), std::string::npos);
}

TEST(FrameworkTest, TryParseParamValueRejectsInvalidExtent) {
    gis::framework::ParamSpec spec;
    spec.key = "extent";
    spec.displayName = "范围";
    spec.type = gis::framework::ParamType::Extent;

    gis::framework::ParamValue value;
    std::string error;
    EXPECT_FALSE(gis::framework::tryParseParamValue(spec, "1,2,3", value, error));
    EXPECT_NE(error.find("格式应为"), std::string::npos);
}

TEST(FrameworkTest, ValidateParamsRejectsInvalidEnumValue) {
    gis::framework::ParamSpec spec;
    spec.key = "action";
    spec.displayName = "动作";
    spec.type = gis::framework::ParamType::Enum;
    spec.required = true;
    spec.enumValues = {"stats", "threshold"};

    const std::map<std::string, gis::framework::ParamValue> params = {
        {"action", std::string("bogus")}
    };
    const auto issue = gis::framework::validateParams({spec}, params);
    EXPECT_NE(issue.find("取值无效"), std::string::npos);
}

TEST(FrameworkTest, PluginManagerEmpty) {
    gis::framework::PluginManager mgr;
    EXPECT_TRUE(mgr.plugins().empty());
    EXPECT_EQ(mgr.find("anything"), nullptr);
}

TEST(FrameworkTest, PluginManagerUnloadCallsDestroy) {
    auto outputDir = gis::tests::defaultTestOutputDir("test_framework_output");
    gis::tests::ensureDirectory(outputDir);

    auto counterFile = outputDir / "plugin_lifecycle_counter.txt";
    std::filesystem::remove(counterFile);

#ifdef _WIN32
    _putenv_s("GIS_TEST_LIFECYCLE_COUNTER", counterFile.string().c_str());
#else
    setenv("GIS_TEST_LIFECYCLE_COUNTER", counterFile.string().c_str(), 1);
#endif

    {
        gis::framework::PluginManager mgr;
        mgr.loadFromDirectory(gis::tests::testExecutableDir().string());
        auto* plugin = mgr.find("lifecycle_test");
        ASSERT_NE(plugin, nullptr);
    }

    std::ifstream in(counterFile);
    ASSERT_TRUE(in.is_open());

    std::string fileContent((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());

    EXPECT_NE(fileContent.find("create"), std::string::npos);
    EXPECT_NE(fileContent.find("destroy"), std::string::npos);
}

TEST(FrameworkTest, CliListLoadsRealPlugins) {
    const auto exeDir = gis::tests::testExecutableDir();
    const auto buildRoot = exeDir.parent_path().parent_path();
    const auto cliPath = buildRoot / "src" / "cli" / exeDir.filename() / "gis-cli.exe";

    ASSERT_TRUE(std::filesystem::exists(cliPath)) << cliPath.string();

    const std::string command = "\"" + cliPath.string() + "\" --list 2>&1";
    std::array<char, 512> buffer{};
    std::string output;

#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    ASSERT_NE(pipe, nullptr);

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

#ifdef _WIN32
    const int exitCode = _pclose(pipe);
#else
    const int exitCode = pclose(pipe);
#endif

    EXPECT_EQ(exitCode, 0) << output;
    EXPECT_NE(output.find("Available plugins:"), std::string::npos) << output;
    EXPECT_NE(output.find("vector - "), std::string::npos) << output;
    EXPECT_NE(output.find("processing - "), std::string::npos) << output;
}

TEST(FrameworkTest, CliRejectsInvalidTypedParam) {
    const auto exeDir = gis::tests::testExecutableDir();
    const auto buildRoot = exeDir.parent_path().parent_path();
    const auto cliPath = buildRoot / "src" / "cli" / exeDir.filename() / "gis-cli.exe";

    ASSERT_TRUE(std::filesystem::exists(cliPath)) << cliPath.string();

    const std::string command = "\"" + cliPath.string()
        + "\" processing stats --input=dummy.tif --band=abc 2>&1";
    std::array<char, 512> buffer{};
    std::string output;

#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    ASSERT_NE(pipe, nullptr);

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

#ifdef _WIN32
    const int exitCode = _pclose(pipe);
#else
    const int exitCode = pclose(pipe);
#endif

    EXPECT_NE(exitCode, 0);
    EXPECT_NE(output.find("不是有效整数"), std::string::npos) << output;
}

TEST(FrameworkTest, CliRejectsInvalidEnumAction) {
    const auto exeDir = gis::tests::testExecutableDir();
    const auto buildRoot = exeDir.parent_path().parent_path();
    const auto cliPath = buildRoot / "src" / "cli" / exeDir.filename() / "gis-cli.exe";

    ASSERT_TRUE(std::filesystem::exists(cliPath)) << cliPath.string();

    const std::string command = "\"" + cliPath.string()
        + "\" processing bogus --input=dummy.tif 2>&1";
    std::array<char, 512> buffer{};
    std::string output;

#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    ASSERT_NE(pipe, nullptr);

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

#ifdef _WIN32
    const int exitCode = _pclose(pipe);
#else
    const int exitCode = pclose(pipe);
#endif

    EXPECT_NE(exitCode, 0);
    EXPECT_NE(output.find("取值无效"), std::string::npos) << output;
}

TEST(FrameworkTest, OrderedMetadataEntriesPreferStandardKeys) {
    const std::map<std::string, std::string> metadata = {
        {"feature_count", "3"},
        {"action", "buffer"},
        {"input", "a.geojson"},
    };

    const auto entries = gis::framework::orderedMetadataEntries(metadata);
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].first, "action");
    EXPECT_EQ(entries[1].first, "input");
    EXPECT_EQ(entries[2].first, "feature_count");
}

TEST(FrameworkTest, MergeResultMetadataPreservesExistingKeys) {
    std::map<std::string, std::string> target = {{"action", "buffer"}};
    const std::map<std::string, std::string> extra = {
        {"feature_count", "3"},
        {"action", "clip"},
    };

    gis::framework::mergeResultMetadata(target, extra);
    EXPECT_EQ(target.at("action"), "clip");
    EXPECT_EQ(target.at("feature_count"), "3");
}

TEST(FrameworkTest, BuildResultSummaryTextSurfacesSummaryField) {
    gis::framework::Result result = gis::framework::Result::ok("done", "/tmp/out.tif");
    result.metadata[gis::framework::ResultMetadataKeys::kSummary] = "bands=3";
    result.metadata["feature_count"] = "1";

    const std::string summary = gis::gui::buildResultSummaryText(result);
    EXPECT_NE(summary.find("摘要: bands=3"), std::string::npos);
    EXPECT_NE(summary.find("feature_count"), std::string::npos);
    EXPECT_EQ(summary.rfind("__summary__"), std::string::npos);
}

// ============================================================================
// Task 14：工作流参数按 ParamSpec 类型化
//
// 缺陷原文：WorkflowRunner::execute 里那圈 typedParams 循环只是
// `typedParams[it->first] = it->second;`（全透传 std::string），于是插件侧
// getParam<int|double|bool> 在任何工作流里都取不到值、一律静默退回代码内联默认值。
// 本套件是 WorkflowRunner::execute 的第一批用例（改动前 grep -rn "Workflow" tests/ 零命中）。
// ============================================================================

namespace {

// 每个用例独占的子目录：数值断言不能被上一次运行的残留文件满足。
std::filesystem::path workflowTypingTestDir(const std::string& caseName) {
    const auto dir = gis::tests::defaultTestOutputDir("workflow_param_typing_output") / caseName;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

// R-T14-2 的夹具：5 波段、逐波段常量。
// band1=10 / band2=20 / band3=30 / band4=40 / band5=80。
// 用例把 blue/red/nir 写成 "2"/"4"/"5"，三个波段号**全都不同于**插件默认
// （blue=1 / red=3 / nir=4，见 spindex_plugin.cpp:711-713 与 spec 默认 :233-235），
// 因此本夹具对 Int 类型化有鉴别力：只类型化 Double、不类型化 Int 的半实现
// 会算出 5*(40-30)/(40+180-75+1)=50/146≈0.342，与期望 200/171≈1.170 相差 0.83。
std::string writeSpindexFixture(const std::filesystem::path& dir) {
    return gis::tests::writeConstantBandsRaster(
        dir, "wf_input.tif", 8, 8, {10.0f, 20.0f, 30.0f, 40.0f, 80.0f});
}

} // namespace

TEST(WorkflowParamTyping, NumericWorkflowParamsAreApplied) {
    gis::core::initGDAL();
    gis::framework::PluginManager mgr;
    mgr.loadFromDirectory(gis::tests::testPluginDir().string());
    ASSERT_GE(mgr.plugins().size(), 13u);
    ASSERT_NE(mgr.find("spindex"), nullptr);

    const auto dir = workflowTypingTestDir("evi_numeric");
    const std::string input = writeSpindexFixture(dir);
    ASSERT_FALSE(input.empty()) << "夹具栅格创建失败";

    gis::framework::Workflow wf;
    wf.name = "EVI 数值参数生效";
    gis::framework::WorkflowStep step;
    step.pluginName = "spindex";
    step.actionKey = "evi";
    step.params["input"] = "${input}";
    step.params["output"] = "${output_dir}/evi_g5.tif";
    step.params["blue_band"] = "2";   // 值 20（默认 1）
    step.params["red_band"] = "4";    // 值 40（默认 3）
    step.params["nir_band"] = "5";    // 值 80（默认 4）
    step.params["g_value"] = "5";     // Double：不类型化就退回 2.5
    // R-T14-5：未知键必须保持透传（改成显式报错撞挂账 N35，本任务不动）。
    // 这一行同时把"未知键被拒绝"的假实现钉红：本用例的 result.success 会变 false。
    step.params["not_a_spec_key"] = "kept-as-string";
    wf.steps.push_back(step);

    const std::string output = (dir / "out" / "evi_g5.tif").string();
    gis::framework::WorkflowRunner runner(mgr);
    const auto result = runner.execute(wf, {
        {"input", input},
        {"output_dir", (dir / "out").string()},
        {"basename", "wf"},
    });
    ASSERT_TRUE(result.success) << result.message;

    // 期望值推导（spindex_plugin.cpp:727-729，EVI = G*(NIR-RED)/(NIR+C1*RED-C2*BLUE+L)，
    // c1=6.0 / c2=7.5 / evi_l=1.0 均走代码内联默认，夹具没传）：
    //   类型化生效后：  5*(80-40) / (80 + 6*40 - 7.5*20 + 1) = 200/171 ≈ 1.169591
    //   类型化前（全退回默认：blue=1→10、red=3→30、nir=4→40、G=2.5）：
    //                  2.5*(40-30) / (40 + 6*30 - 7.5*10 + 1) = 25/146 ≈ 0.171233
    //   ⇒ 判别幅度 1.169591 - 0.171233 = 0.998358，是本用例容差 1e-4f 的 ~9984 倍，
    //     不是擦边断言。R-T14-9：safeDivide 先给分母 +1e-10f，实测 200/(171+1e-10)，
    //     偏差在 1e-12 量级；夹具是 Float32，200/171 自身的表示误差已在 1e-7 量级，
    //     所以容差停在 1e-4f，不得收到 1e-7f 以下去追浮尾数。
    const float value = gis::tests::readPixel(output, 1, 3, 3);
    EXPECT_NEAR(value, 200.0f / 171.0f, 1e-4f)
        << "g_value/*_band 未生效，工作流仍在用代码内联默认值（退回值应为 25/146≈0.171233）";
}

TEST(WorkflowParamTyping, InvalidNumericParamFailsStep) {
    gis::core::initGDAL();
    gis::framework::PluginManager mgr;
    mgr.loadFromDirectory(gis::tests::testPluginDir().string());
    ASSERT_NE(mgr.find("raster_math"), nullptr);

    const auto dir = workflowTypingTestDir("invalid_numeric");
    const std::string missingInput = (dir / "no_such_input.tif").string();
    const std::string output = (dir / "never_written.tif").string();

    gis::framework::Workflow wf;
    wf.name = "非法数值参数必须显式失败";
    gis::framework::WorkflowStep step;
    step.pluginName = "raster_math";
    step.actionKey = "reclassify";
    step.params["input"] = "${input}";
    step.params["output"] = "${output_dir}/never_written.tif";
    // reclass_mode 的 spec 是 Enum ⇒ tryParseParamValue 透传字符串（param_spec.h:135-142），
    // 插件用 getParam<std::string> 读它（raster_math_plugin.cpp:227）——读取类型与 spec 类型匹配。
    step.params["reclass_mode"] = "interval";
    step.params["interval_step"] = "abc";   // spec 类型 Double（raster_math_plugin.cpp:41）
    wf.steps.push_back(step);

    gis::framework::WorkflowRunner runner(mgr);
    const auto result = runner.execute(wf, {
        {"input", missingInput},
        {"output_dir", dir.string()},
        {"basename", "wf"},
    });

    EXPECT_FALSE(result.success);
    ASSERT_EQ(result.stepMessages.size(), 1u);
    const std::string& message = result.stepMessages[0];
    // 形状由 R-T14-3 定死：Step <n> (<plugin>.<action>) invalid parameter <key>: <parseError>
    // 这里只锁 workflow 侧拥有的那一半（前缀 + 键名），不锁 parseError 的完整文案——
    // 那段文案属于共用的 tryParseParamValue（CLI 与 test_framework.cpp:118/:130 都在依赖它），
    // 本任务不得改它，也不该在工作流用例里再抄一份逐字锁。
    const std::string expectedPrefix =
        "Step 1 (raster_math.reclassify) invalid parameter interval_step: ";
    EXPECT_EQ(message.rfind(expectedPrefix, 0), 0u) << message;
    // 键名与人类可读文案都要在：parseError 取的是 paramDisplayName（"间隔步长"），本身不含键名。
    EXPECT_NE(message.find("interval_step"), std::string::npos) << message;
    EXPECT_NE(message.find("间隔步长"), std::string::npos) << message;
    EXPECT_NE(message.find("invalid parameter"), std::string::npos) << message;
    EXPECT_NE(message.find("不是有效数字"), std::string::npos) << message;
    // completedSteps==0 + 输出文件不存在 ⇒ 失败发生在**进插件之前**，不是插件自己报的错。
    EXPECT_EQ(result.completedSteps, 0) << "失败被算成了已完成步数，说明没在进插件前拦住";
    EXPECT_FALSE(std::filesystem::exists(output)) << "非法参数那一步仍然产出了文件";
}

TEST(WorkflowParamTyping, ValidNumericParamReachesPluginAndChangesOutput) {
    // R-T14-4 的正向用例：只有"非法值被拒"这一半时，"一律拒绝任何带 spec 的键"的假实现
    // 也能全绿；本用例把解析器的另一个方向（合法值必须放行并真的生效）钉住。
    gis::core::initGDAL();
    gis::framework::PluginManager mgr;
    mgr.loadFromDirectory(gis::tests::testPluginDir().string());
    ASSERT_NE(mgr.find("raster_math"), nullptr);

    const auto dir = workflowTypingTestDir("valid_numeric");
    // 单波段 8x8，像元值 = x + y ⇒ bandMin=0、bandMax=14。
    std::vector<float> gradient;
    gradient.reserve(64);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            gradient.push_back(static_cast<float>(x + y));
        }
    }
    const std::string input = gis::tests::writeFloatRaster(dir, "reclass_input.tif", 8, 8, gradient);
    ASSERT_FALSE(input.empty()) << "重分类夹具栅格创建失败";

    gis::framework::Workflow wf;
    wf.name = "合法数值参数必须放行并生效";
    gis::framework::WorkflowStep step;
    step.pluginName = "raster_math";
    step.actionKey = "reclassify";
    step.params["input"] = "${input}";
    step.params["output"] = "${output_dir}/reclass_step5.tif";
    step.params["reclass_mode"] = "interval";
    step.params["interval_step"] = "5";   // 合法数字串：必须被解析成 double 5.0 交进插件
    wf.steps.push_back(step);

    const std::string output = (dir / "reclass_step5.tif").string();
    gis::framework::WorkflowRunner runner(mgr);
    const auto result = runner.execute(wf, {
        {"input", input},
        {"output_dir", dir.string()},
        {"basename", "wf"},
    });
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(result.stepMessages.size(), 1u);
    EXPECT_NE(result.stepMessages[0].find("completed"), std::string::npos) << result.stepMessages[0];
    EXPECT_EQ(result.stepMessages[0].find("invalid parameter"), std::string::npos)
        << "合法数字串被误拒：解析器的放行方向没有实现";
    EXPECT_TRUE(std::filesystem::exists(output));

    // 期望值推导（raster_math_plugin.cpp:270-281 生成区间、:205-212 首个匹配即返回）：
    //   bandMin=0、bandMax=14、step=5 ⇒ roundedMin=floor(0/5)*5=0，
    //   规则 [0,5]→1、[5,10]→2、[10,15]→3；像元 (3,3) 值 6 ⇒ 首个匹配 [5,10] ⇒ 输出 2。
    //   未类型化时 interval_step 退回代码内联默认 1.0 ⇒ 规则 [0,1]→1、[1,2]→2 … [5,6]→6，
    //   值 6 首个匹配 [5,6] ⇒ 输出 6 ⇒ 与 2 差 4 个类别，断言有牙。
    const float value = gis::tests::readPixel(output, 1, 3, 3);
    EXPECT_FLOAT_EQ(value, 2.0f) << "interval_step 未生效（退回 1.0 时该像元应为 6）";
}

TEST(WorkflowParamTyping, OutOfRangeBandParamFailsStepExplicitly) {
    // R-T14-2 的 Int 类型化在这里第二次显形：类型化之前 blue_band="999" 被静默忽略、
    // 插件用默认号 1 正常算完并返回成功；类型化之后它真的进到 getBandIndex 的范围判据里。
    gis::core::initGDAL();
    gis::framework::PluginManager mgr;
    mgr.loadFromDirectory(gis::tests::testPluginDir().string());
    ASSERT_NE(mgr.find("spindex"), nullptr);

    const auto dir = workflowTypingTestDir("band_out_of_range");
    const std::string input = writeSpindexFixture(dir);
    ASSERT_FALSE(input.empty()) << "夹具栅格创建失败";
    const std::string output = (dir / "out" / "evi_bad_band.tif").string();

    gis::framework::Workflow wf;
    wf.name = "越界波段号必须显式失败";
    gis::framework::WorkflowStep step;
    step.pluginName = "spindex";
    step.actionKey = "evi";
    step.params["input"] = "${input}";
    step.params["output"] = "${output_dir}/evi_bad_band.tif";
    step.params["blue_band"] = "999";   // 夹具只有 5 个波段
    wf.steps.push_back(step);

    gis::framework::WorkflowRunner runner(mgr);
    const auto result = runner.execute(wf, {
        {"input", input},
        {"output_dir", (dir / "out").string()},
        {"basename", "wf"},
    });

    EXPECT_FALSE(result.success);
    ASSERT_EQ(result.stepMessages.size(), 1u);
    const std::string& message = result.stepMessages[0];
    // 越界在 spindex 内部被自家 catch（spindex_plugin.cpp:774-776
    // `catch (const std::exception& ex) { return Result::fail(ex.what()); }`）转成插件失败，
    // 所以到不了 workflow 的异常兜底那一层：文案是 "…) failed: blue_band 999 out of range (1-5)"，
    // **不含 "threw"**。这一点与 R-T14-6 的预期（"含 threw"）不符，已在报告 §5 上报。
    EXPECT_NE(message.find("out of range"), std::string::npos) << message;
    EXPECT_NE(message.find("999"), std::string::npos) << message;
    EXPECT_NE(message.find("Step 1 (spindex.evi) failed:"), std::string::npos) << message;
    EXPECT_EQ(message.find("invalid parameter"), std::string::npos)
        << "999 是合法整数，不该被解析器拒掉；这里失败的是波段号对不上栅格";
    // 这里**故意不断** result.completedSteps：WorkflowRunner 在成功判据之前就做了
    // `result.completedSteps = i + 1`（workflow.cpp:72 与 :74 的顺序），所以"插件自己报的失败"
    // 也会把这一步计入已完成数（实测本用例为 1）。那是 WorkflowRunner 的既有语义、
    // 与本任务的类型化无关，改它属另一个口径（已在报告里作为新发现上报），
    // 留在本用例里断言等于把一个可疑行为锁死。
    EXPECT_FALSE(std::filesystem::exists(output)) << "越界波段仍然产出了文件";
}

TEST(WorkflowParamTyping, PluginExceptionIsReportedNotPropagated) {
    // 异常兜底的真实触发路径：raster_math 的 doReclassify 对 openRaster 不自家 catch
    // （raster_math_plugin.cpp 里三处 try 都在 parseReclassRules 内，:182-198），
    // 而不存在的输入会让 gis::core::openRaster 抛 GisError（gdal_wrapper.cpp:49-51，
    // GisError 派生自 std::runtime_error，error.h:7-12）。
    // 兜底缺失时这条用例会红成"异常穿出测试体"：实测 gtest 报
    // `unknown file: error: SEH exception with code 0xc0000005 thrown in auxiliary test code`
    // （见 build/task14_tier2_red_M0_debug.log 的 #69 同形态），而不是干净的用例失败。
    gis::core::initGDAL();
    gis::framework::PluginManager mgr;
    mgr.loadFromDirectory(gis::tests::testPluginDir().string());
    ASSERT_NE(mgr.find("raster_math"), nullptr);

    const auto dir = workflowTypingTestDir("plugin_throw");
    gis::framework::Workflow wf;
    wf.name = "插件抛异常必须由工作流兜住并报出来";
    gis::framework::WorkflowStep step;
    step.pluginName = "raster_math";
    step.actionKey = "reclassify";
    step.params["input"] = "${input}";
    step.params["output"] = "${output_dir}/never_written.tif";
    step.params["reclass_mode"] = "interval";
    step.params["interval_step"] = "5";   // 合法：解析放行，失败点在插件内部的 openRaster
    wf.steps.push_back(step);

    const auto output = dir / "never_written.tif";
    std::error_code ec;
    std::filesystem::remove(output, ec);

    gis::framework::WorkflowRunner runner(mgr);
    const auto result = runner.execute(wf, {
        {"input", (dir / "definitely_missing.tif").string()},
        {"output_dir", dir.string()},
        {"basename", "wf"},
    });

    EXPECT_FALSE(result.success);
    ASSERT_EQ(result.stepMessages.size(), 1u);
    const std::string& message = result.stepMessages[0];
    const std::string expectedPrefix = "Step 1 (raster_math.reclassify) threw: ";
    EXPECT_EQ(message.rfind(expectedPrefix, 0), 0u) << message;
    EXPECT_NE(message.find("Cannot open raster"), std::string::npos) << message;
    // 兜底的 catch 让 execute 在 `result.completedSteps = i + 1` **之前** return，
    // 所以抛异常的这一步不被计入已完成（与上一条用例里"插件自己报的失败计入 1"相对照）。
    EXPECT_EQ(result.completedSteps, 0);
    EXPECT_FALSE(std::filesystem::exists(output));

    // 兜底之后 runner 必须还能继续接活：同 mgr 上再跑一次空工作流不应受影响。
    gis::framework::Workflow empty;
    empty.name = "兜底后工作流仍可用";
    const auto followUp = runner.execute(empty, {});
    EXPECT_TRUE(followUp.success) << followUp.message;
}
