#include <gis/framework/plugin.h>
#include <gis/framework/result.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
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
        // Task B15：加载/卸载生命周期之外，本插件同时是"插件抛异常三侧兜获"这一族用例的
        // **test-only 夹具**（产品插件一律不改）。动作只有四个：正常返回、std 抛出、
        // 非 std 抛出（今天 CLI 会直接终止进程的那一类）、以及用户取消。
        gis::framework::ParamSpec action;
        action.key = "action";
        action.displayName = "测试动作";
        action.description = "选择夹具行为：ok / throw_std / throw_nonstd / throw_cancelled";
        action.type = gis::framework::ParamType::Enum;
        action.required = false;
        action.defaultValue = std::string("ok");
        action.enumValues = {"ok", "throw_std", "throw_nonstd", "throw_cancelled"};
        return {action};
    }

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter&) override {
        const std::string action =
            gis::framework::getParam<std::string>(params, "action", std::string("ok"));
        if (action == "throw_std") {
            throw std::runtime_error("synthetic failure");
        }
        if (action == "throw_nonstd") {
            // 刻意不是 std::exception 派生：MSVC /EHsc 下照样能抛，而只 catch
            // std::exception 的调用点接不住 ⇒ 进程被 std::terminate 带走。
            throw 42;
        }
        if (action == "throw_cancelled") {
            throw gis::core::CancelledException();
        }
        gis::framework::Result result = gis::framework::Result::ok("ok");
        result.metadata["lifecycle"] = "ok";
        return result;
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
