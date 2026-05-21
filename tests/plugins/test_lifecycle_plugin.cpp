#include <gis/framework/plugin.h>
#include <gis/framework/result.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

std::filesystem::path counterFile() {
    const char* value = std::getenv("GIS_TEST_LIFECYCLE_COUNTER");
    if (!value || !*value) {
        return {};
    }
    return std::filesystem::path(value);
}

void appendCounter(const std::string& text) {
    auto path = counterFile();
    if (path.empty()) {
        return;
    }

    std::ofstream out(path, std::ios::app);
    out << text << '\n';
}

class LifecyclePlugin final : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "lifecycle_test"; }
    std::string displayName() const override { return "生命周期测试插件"; }
    std::string version() const override { return "1.0.0"; }
    std::string description() const override { return "用于验证插件创建和销毁流程"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override {
        return {};
    }

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>&,
        gis::core::ProgressReporter&) override {
        return gis::framework::Result::ok("ok");
    }
};

} // namespace

extern "C" GIS_EXPORT gis::framework::IGisPlugin* createPlugin() {
    appendCounter("create");
    return new LifecyclePlugin();
}

extern "C" GIS_EXPORT void destroyPlugin(gis::framework::IGisPlugin* plugin) {
    appendCounter("destroy");
    delete plugin;
}
