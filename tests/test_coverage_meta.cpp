#include <gtest/gtest.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/framework/plugin.h>
#include <gis/framework/plugin_manager.h>
#include "../src/gui/gui_data_support.h"   // 与 tests/test_framework.cpp:7 的写法一致（gui 头文件不在 include 路径中）
#include "test_support.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#ifndef GIS_SOURCE_DIR
#define GIS_SOURCE_DIR "."
#endif

namespace {

const std::string kRegistryPath =
    std::string(GIS_SOURCE_DIR) + "/tests/action_gold_cases.txt";

// 展示层仍然覆写的参数，按控制器裁定（R-T3-4：覆写区整体清理由 Task 10 负责）暂时保留。
// 这张表只允许变短：条目对应的覆写一旦被删掉，本用例就会因为"白名单已失效"变红，
// 所以它不是一张贴上去就撕不掉的封条，而是带到期检查的挂账。
// projection/transform 需要 src_srs 默认 EPSG:4326，而 projection/reproject 需要 src_srs
// 留空（空值才表示"从数据里读坐标系"）；ParamSpec 是插件级、不按 action 区分，
// 所以这一条必须由插件侧先具备 per-action 默认值能力才能删除，不在本任务范围内。
const std::set<std::string> kKnownPresentationOverrides = {
    "projection.transform.src_srs",
};

struct AllActions {
    std::vector<std::pair<std::string, std::string>> items;  // plugin, action
};

AllActions collectAllActions(gis::framework::PluginManager& mgr) {
    AllActions out;
    for (auto* plugin : mgr.plugins()) {
        if (!plugin) continue;
        const std::string name = plugin->name();
        if (name == "test_lifecycle") continue;
        for (const auto& spec : plugin->paramSpecs()) {
            if (spec.key == "action") {
                for (const auto& action : spec.enumValues) {
                    out.items.emplace_back(name, action);
                }
            }
        }
    }
    return out;
}

std::map<std::string, std::string> readRegistry() {
    std::map<std::string, std::string> registry;
    std::ifstream ifs(kRegistryPath);
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        std::string plugin, action, status;
        std::getline(ss, plugin, ',');
        std::getline(ss, action, ',');
        std::getline(ss, status, ',');
        registry[plugin + "." + action] = status;
    }
    return registry;
}

} // namespace

TEST(CoverageMeta, EveryActionIsRegistered) {
    gis::core::initGDAL();
    gis::framework::PluginManager mgr;
    mgr.loadFromDirectory(gis::tests::testPluginDir().string());
    ASSERT_GE(mgr.plugins().size(), 13u);

    const auto registry = readRegistry();
    ASSERT_FALSE(registry.empty()) << "缺少 " << kRegistryPath;

    std::vector<std::string> missing;
    for (const auto& item : collectAllActions(mgr).items) {
        const std::string key = item.first + "." + item.second;
        if (registry.find(key) == registry.end()) {
            missing.push_back(key);
        }
    }
    std::string joined;
    for (const auto& key : missing) joined += "\n  " + key;
    EXPECT_TRUE(missing.empty()) << "以下 action 未登记（新增 action 必须同步登记测试状态）:" << joined;
}

TEST(CoverageMeta, PendingCountDoesNotGrow) {
    gis::core::initGDAL();
    gis::framework::PluginManager mgr;
    mgr.loadFromDirectory(gis::tests::testPluginDir().string());

    const auto registry = readRegistry();
    int pending = 0;
    for (const auto& item : collectAllActions(mgr).items) {
        const auto it = registry.find(item.first + "." + item.second);
        if (it != registry.end() && it->second == "PENDING") ++pending;
    }
    // 基线 2026-10-15：登记表里 PENDING 的真实数量，不是估算值。
    // 推导：grep -c ",PENDING$" tests/action_gold_cases.txt = 74（登记表共 126 行，GOLD 52 行），
    // 逐行核对标准见 tests/action_gold_cases.txt 头部注释。随批次 A/B 修复只允许下降。
    EXPECT_LE(pending, 74) << "PENDING 数量不得增加；若某 action 已补数值测试，请把登记表改为 GOLD";
}

TEST(GuiContract, EffectiveSpecsNeverOverrideRequiredOrDefault) {
    gis::core::initGDAL();
    gis::framework::PluginManager mgr;
    mgr.loadFromDirectory(gis::tests::testPluginDir().string());

    std::vector<std::string> violations;
    std::set<std::string> whitelistHits;

    for (auto* plugin : mgr.plugins()) {
        if (!plugin) continue;
        const std::string name = plugin->name();
        if (name == "test_lifecycle") continue;
        const auto specs = plugin->paramSpecs();
        const gis::framework::ParamSpec* actionSpec = nullptr;
        for (const auto& spec : specs) {
            if (spec.key == "action") actionSpec = &spec;
        }
        if (!actionSpec) continue;

        for (const auto& action : actionSpec->enumValues) {
            const auto* config = gis::gui::findActionUiConfig(name, action);
            const std::set<std::string> visible;
            const std::set<std::string> required;
            const auto effective = gis::gui::buildEffectiveGuiParamSpecs(
                name, action, specs,
                config ? config->visibleKeys : visible,
                config ? config->requiredKeys : required);

            for (const auto& eff : effective) {
                const gis::framework::ParamSpec* origin = nullptr;
                for (const auto& spec : specs) {
                    if (spec.key == eff.key) origin = &spec;
                }
                ASSERT_NE(origin, nullptr) << name << "." << action << " " << eff.key;
                const std::string key = name + "." + action + "." + eff.key;
                bool overridden = false;
                if (eff.required != origin->required) {
                    violations.push_back(key + " required 被展示层覆写");
                    overridden = true;
                }
                if (!(eff.defaultValue == origin->defaultValue)) {
                    violations.push_back(key + " 默认值被展示层覆写");
                    overridden = true;
                }
                // 只有"确实发生了覆写"才算命中白名单：键存在但没有覆写时必须让下面的失效检查变红。
                if (overridden && kKnownPresentationOverrides.count(key)) {
                    whitelistHits.insert(key);
                }
            }
        }
    }

    std::string unexpected;
    for (const auto& violation : violations) {
        const std::string key = violation.substr(0, violation.find(' '));
        if (!kKnownPresentationOverrides.count(key)) {
            unexpected += "\n  " + violation;
        }
    }
    EXPECT_TRUE(unexpected.empty())
        << "展示层覆写了插件 paramSpecs 声明的 required/默认值（GUI 与 CLI 因此会提交不同参数）:"
        << unexpected;

    // 覆写被删掉却还留在白名单里 = 挂账已清但账没销，同样要变红。
    std::string stale;
    for (const auto& key : kKnownPresentationOverrides) {
        if (!whitelistHits.count(key)) stale += "\n  " + key;
    }
    EXPECT_TRUE(stale.empty())
        << "以下覆写白名单条目已经没有对应的覆写了，请删除:" << stale;

    // R-T3-3 要求的证据断言：以 GUI 的真实路径取 raster_manage/nodata 的有效 spec，
    // band 的默认值必须是 0（0 = 全波段）。上面的一致性比对测不到这件事
    // （spec 默认值改成 0 之后两边照样相等），所以这里把数值本身钉死。
    // ParamCardWidget::createIntWidget 正是用这个 defaultValue 初始化 QSpinBox，
    // 它也就是 GUI 全默认提交时发出去的值。
    auto* rasterManage = mgr.find("raster_manage");
    ASSERT_NE(rasterManage, nullptr);
    const auto nodataSpecs = rasterManage->paramSpecs();
    const auto* nodataConfig = gis::gui::findActionUiConfig("raster_manage", "nodata");
    ASSERT_NE(nodataConfig, nullptr);
    const auto nodataEffective = gis::gui::buildEffectiveGuiParamSpecs(
        "raster_manage", "nodata", nodataSpecs,
        nodataConfig->visibleKeys, nodataConfig->requiredKeys);

    const gis::framework::ParamSpec* bandSpec = nullptr;
    for (const auto& spec : nodataEffective) {
        if (spec.key == "band") bandSpec = &spec;
    }
    ASSERT_NE(bandSpec, nullptr) << "raster_manage.nodata 的可见参数里必须仍有 band";
    const int* bandDefault = std::get_if<int>(&bandSpec->defaultValue);
    ASSERT_NE(bandDefault, nullptr)
        << "raster_manage.nodata 的 band 默认值必须是 int，否则 GUI 无法用它初始化波段输入框";
    EXPECT_EQ(*bandDefault, 0)
        << "GUI 全默认提交 raster_manage.nodata 时 band 必须是 0（描述与 doSetNoData 兜底都是 0 = 全波段）";
}
