#include <gtest/gtest.h>
#include <gis/framework/plugin.h>
#include <gis/framework/param_spec.h>
#include <gis/framework/plugin_manager.h>
#include <gis/framework/result.h>

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
