#include <gtest/gtest.h>
#include <gis/framework/plugin.h>
#include <gis/framework/param_spec.h>
#include <gis/framework/plugin_manager.h>
#include <gis/framework/result.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "test_support.h"

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
