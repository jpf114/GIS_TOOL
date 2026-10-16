#include <gtest/gtest.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/framework/plugin.h>
#include <gis/framework/plugin_manager.h>
#include "../src/gui/gui_data_support.h"   // 与 tests/test_framework.cpp:7 的写法一致（gui 头文件不在 include 路径中）
// R-T13-5 的 pan_method 标签表锁要读 ParamCardWidget::enumLabelKeys（标签表本体在 param_card_widget.cpp，
// gis_tests 把它与测试编在同一个可执行文件里，见 tests/CMakeLists.txt:19）。
#include "../src/gui/param_card_widget.h"
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
const std::string kPluginTestPath =
    std::string(GIS_SOURCE_DIR) + "/tests/test_plugins.cpp";

// GOLD 的证据判据（R-F1-A）：登记表的凭据必须在版本库内可复核，
// 所以这里不引用 build/ 下任何一次性产物，只回到 tests/test_plugins.cpp 原文核对。
// 三者都是"把数值钉住"的断言，因此等价；EXPECT_DOUBLE_EQ 是 fix round 1 才纳入的。
const std::vector<std::string> kNumericAssertions = {
    "EXPECT_NEAR",
    "EXPECT_FLOAT_EQ",
    "EXPECT_DOUBLE_EQ",
};

// 展示层与插件真值之间**已知且尚未清偿**的分歧（R-F3-A：这是"分歧覆写清单"，不是
// "允许覆写清单"）。键 = plugin.action.key + 违规种类，这样一个条目不可能顺手吃掉
// 同一个键上的其它分歧。
// 这张表只允许变短：条目对应分歧一旦被删掉，本用例就会因为"白名单已失效"变红，
// 所以它不是一张贴上去就撕不掉的封条，而是带到期检查的挂账。
// projection/transform 需要 src_srs 默认 EPSG:4326，而 projection/reproject 需要 src_srs
// 留空（空值才表示"从数据里读坐标系"）；ParamSpec 是插件级、不按 action 区分，
// 所以这一条必须由插件侧先具备 per-action 默认值能力才能删除，不在本任务范围内（Task 10）。
// required 不在这里：按 R-F2-A，展示层只允许**加严** required、永远不允许放宽，
// 所以合法的 required 加严不是分歧，不需要白名单。
const std::set<std::string> kKnownDivergentOverrides = {
    "projection.transform.src_srs.defaultValue",
};

// ---------------------------------------------------------------- 登记表

struct RegistryRow {
    int lineNumber = 0;
    std::string plugin;
    std::string action;
    std::string status;    // 原样保留（不 trim）：尾随空白与拼写错误必须被看见
    std::string evidence;  // GOLD 行的第 4 列：tests/test_plugins.cpp 里的 Suite.Test
    std::string key() const { return plugin + "." + action; }
};

struct Registry {
    std::vector<RegistryRow> rows;
    std::vector<std::string> malformed;  // F6：状态不是 GOLD/PENDING 等坏行，带行号点名
};

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

Registry readRegistry() {
    Registry registry;
    std::ifstream ifs(kRegistryPath);
    std::string line;
    int lineNumber = 0;
    while (std::getline(ifs, line)) {
        ++lineNumber;
        // 只归一化行尾（CRLF），不修剪字段内的空白：F6 要抓的正是 "GOLD " 这类尾随空白。
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto fields = splitFields(line);
        RegistryRow row;
        row.lineNumber = lineNumber;
        if (fields.size() < 3) {
            registry.malformed.push_back(
                "第 " + std::to_string(lineNumber) + " 行字段不足 3 列（应为 plugin,action,GOLD|PENDING[,用例名）: " + line);
            continue;
        }
        row.plugin = fields[0];
        row.action = fields[1];
        row.status = fields[2];
        if (fields.size() > 3) {
            row.evidence = fields[3];
        }
        if (fields.size() > 4) {
            registry.malformed.push_back(
                "第 " + std::to_string(lineNumber) + " 行超过 4 列，多余字段不会被任何检查读到: " + line);
        }
        // F6：状态必须是 GOLD 或 PENDING 之一。过去任意第三个 token 都会被登记上，
        // 于是 EveryActionIsRegistered 全绿、但该 action 既不计 GOLD 也不占 PENDING 预算，
        // 在本批次唯一的管理指标上静默漏数。
        if (row.status != "GOLD" && row.status != "PENDING") {
            registry.malformed.push_back(
                "第 " + std::to_string(lineNumber) + " 行状态既不是 GOLD 也不是 PENDING: \"" + row.status +
                "\" (" + row.plugin + "." + row.action + ")");
        }
        if (row.status == "GOLD" && row.evidence.empty()) {
            registry.malformed.push_back(
                "第 " + std::to_string(lineNumber) + " 行是 GOLD 但没有写第 4 列证据用例名: " +
                row.plugin + "." + row.action);
        }
        registry.rows.push_back(row);
    }
    return registry;
}

std::string describeMalformed(const Registry& registry) {
    std::string text;
    for (const auto& problem : registry.malformed) {
        text += "\n  " + problem;
    }
    return text;
}

// ---------------------------------------------------------------- 源码用例块

struct GTestBlock {
    std::string suite;
    std::string name;
    std::string body;  // 外层大括号内的原文（已跳过字符串/字符字面量与注释）
    std::string id() const { return suite + "." + name; }
};

// 从 from 起找到第一个 '{'，做括号配对并返回闭括号后的位置；找不到返回 npos。
// 跳过 // 与 \/* *\/ 注释、字符串与字符字面量里的括号，否则 JSON 夹具里的 { 会把块撑到文件末尾。
std::size_t scanBracedBlock(const std::string& text, std::size_t from, std::size_t& bodyBegin) {
    const std::size_t open = text.find('{', from);
    if (open == std::string::npos) {
        return std::string::npos;
    }
    bodyBegin = open + 1;
    std::size_t depth = 0;
    bool inString = false;
    bool inChar = false;
    bool inLineComment = false;
    bool inBlockComment = false;
    bool escaped = false;
    for (std::size_t p = open; p < text.size(); ++p) {
        const char c = text[p];
        const char next = (p + 1 < text.size()) ? text[p + 1] : '\0';
        if (inLineComment) {
            if (c == '\n') inLineComment = false;
            continue;
        }
        if (inBlockComment) {
            if (c == '*' && next == '/') {
                inBlockComment = false;
                ++p;
            }
            continue;
        }
        if (inString || inChar) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (inString && c == '"') {
                inString = false;
            } else if (inChar && c == '\'') {
                inChar = false;
            }
            continue;
        }
        if (c == '/' && next == '/') {
            inLineComment = true;
            ++p;
            continue;
        }
        if (c == '/' && next == '*') {
            inBlockComment = true;
            ++p;
            continue;
        }
        if (c == '"') {
            inString = true;
            continue;
        }
        if (c == '\'') {
            inChar = true;
            continue;
        }
        if (c == '{') {
            ++depth;
            continue;
        }
        if (c == '}') {
            --depth;
            if (depth == 0) {
                return p + 1;
            }
            continue;
        }
    }
    return std::string::npos;
}

std::string readFileOrEmpty(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return std::string();
    }
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return buffer.str();
}

// 按行首的 TEST/TEST_F/TEST_P 宏切块；块体是那对大括号内的原文。
std::vector<GTestBlock> collectGTestBlocks(const std::string& text) {
    std::vector<GTestBlock> blocks;
    std::size_t lineStart = 0;
    while (lineStart < text.size()) {
        std::size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = text.size();
        }
        const std::string line = text.substr(lineStart, lineEnd - lineStart);
        const std::size_t first = line.find_first_not_of(" \t");
        const std::string trimmed = (first == std::string::npos) ? std::string() : line.substr(first);

        std::string macro;
        if (trimmed.rfind("TEST_F(", 0) == 0) {
            macro = "TEST_F(";
        } else if (trimmed.rfind("TEST_P(", 0) == 0) {
            macro = "TEST_P(";
        } else if (trimmed.rfind("TEST(", 0) == 0) {
            macro = "TEST(";
        }

        if (!macro.empty()) {
            const std::size_t argsBegin = macro.size();
            const std::size_t argsEnd = trimmed.find(')', argsBegin);
            const std::string args = trimmed.substr(argsBegin, argsEnd - argsBegin);
            const std::size_t comma = args.find(',');
            GTestBlock block;
            auto trim = [](std::string s) {
                const std::size_t b = s.find_first_not_of(" \t");
                const std::size_t e = s.find_last_not_of(" \t");
                return (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
            };
            if (comma != std::string::npos) {
                block.suite = trim(args.substr(0, comma));
                block.name = trim(args.substr(comma + 1));
            } else {
                block.suite = trim(args);
            }
            std::size_t bodyBegin = 0;
            const std::size_t after = scanBracedBlock(text, lineStart, bodyBegin);
            if (after != std::string::npos && !block.name.empty()) {
                block.body = text.substr(bodyBegin, after - 1 - bodyBegin);
                blocks.push_back(block);
                lineStart = after;
                continue;
            }
        }
        lineStart = lineEnd + 1;
    }
    return blocks;
}

bool blockHasNumericAssertion(const GTestBlock& block) {
    for (const auto& assertion : kNumericAssertions) {
        if (block.body.find(assertion) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 一条展示层与插件真值之间的分歧。id 形如 plugin.action.key.required / .defaultValue，
// 白名单按 id 精确匹配（R-F3-A），所以一个条目不可能顺手豁免同一键上的另一种分歧。
struct Divergence {
    std::string id;
    std::string message;
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

} // namespace

TEST(CoverageMeta, EveryActionIsRegistered) {
    gis::core::initGDAL();
    gis::framework::PluginManager mgr;
    mgr.loadFromDirectory(gis::tests::testPluginDir().string());
    ASSERT_GE(mgr.plugins().size(), 13u);

    const auto registry = readRegistry();
    ASSERT_FALSE(registry.rows.empty()) << "缺少 " << kRegistryPath;
    // F6：坏状态行过去会被"登记上但不计数"地放过，这里先钉住行的形状。
    EXPECT_TRUE(registry.malformed.empty())
        << "登记表存在无法识别的行（状态必须是 GOLD 或 PENDING，GOLD 必须带第 4 列证据用例名）:"
        << describeMalformed(registry);

    std::set<std::string> registryKeys;
    for (const auto& row : registry.rows) {
        registryKeys.insert(row.key());
    }

    const auto actions = collectAllActions(mgr);
    ASSERT_FALSE(actions.items.empty()) << "插件加载后一个 action 都没有，登记核对失去意义";

    // 正向：真实 action 必须登记。
    std::vector<std::string> missing;
    for (const auto& item : actions.items) {
        const std::string key = item.first + "." + item.second;
        if (registryKeys.find(key) == registryKeys.end()) {
            missing.push_back(key);
        }
    }
    std::string joined;
    for (const auto& key : missing) joined += "\n  " + key;
    EXPECT_TRUE(missing.empty()) << "以下 action 未登记（新增 action 必须同步登记测试状态）:" << joined;

    // 反向（F6）：登记表比真实 action 集合多出来的陈旧行今天必须看得见，
    // 否则删除 action 之后会留下永远不红、也永远不被核对的脏行。
    std::string staleRows;
    for (const auto& row : registry.rows) {
        bool live = false;
        for (const auto& item : actions.items) {
            if (row.key() == item.first + "." + item.second) {
                live = true;
                break;
            }
        }
        if (!live) {
            staleRows += "\n  第 " + std::to_string(row.lineNumber) + " 行: " + row.key();
        }
    }
    EXPECT_TRUE(staleRows.empty())
        << "登记表里有当前插件不再提供的 action（删除 action 必须同步删掉这一行）:" << staleRows;
}

TEST(CoverageMeta, PendingCountDoesNotGrow) {
    gis::core::initGDAL();
    gis::framework::PluginManager mgr;
    mgr.loadFromDirectory(gis::tests::testPluginDir().string());
    // F4：插件加载失败时下面会变成 0 <= 74 而静默通过，所以先钉住"确实加载到了插件"。
    ASSERT_GE(mgr.plugins().size(), 13u);

    const auto registry = readRegistry();
    EXPECT_TRUE(registry.malformed.empty())
        << "登记表存在无法识别的行，PENDING 计数因此不可信:" << describeMalformed(registry);

    std::map<std::string, std::string> statusByKey;
    for (const auto& row : registry.rows) {
        statusByKey[row.key()] = row.status;
    }

    const auto actions = collectAllActions(mgr);
    ASSERT_FALSE(actions.items.empty()) << "插件加载后一个 action 都没有，PENDING 计数会静默变成 0";

    int pending = 0;
    for (const auto& item : actions.items) {
        const auto it = statusByKey.find(item.first + "." + item.second);
        if (it != statusByKey.end() && it->second == "PENDING") ++pending;
    }
    // 基线 2026-10-15：登记表里 PENDING 的真实数量，不是估算值。
    // 推导：grep -c ",PENDING$" tests/action_gold_cases.txt = 74（登记表共 126 行，GOLD 52 行），
    // 逐行核对标准见 tests/action_gold_cases.txt 头部注释。随批次 A/B 修复只允许下降。
    EXPECT_LE(pending, 74) << "PENDING 数量不得增加；若某 action 已补数值测试，请把登记表改为 GOLD";
}

TEST(CoverageMeta, GoldEvidenceIsAuditable) {
    // F1 / R-F1-A：GOLD 行的凭据必须落在版本库里，并且能被机器复核。
    // 登记表第 4 列写用例名，这里回到 tests/test_plugins.cpp 的**那个块体内**核对
    // （a）用例存在（b）块体内有数值断言（c）块体内出现该行的 action 字符串。
    // 任何人软化或删掉那条数值断言、或给用例改名，对应 GOLD 行立刻变红。
    const auto registry = readRegistry();
    ASSERT_FALSE(registry.rows.empty()) << "缺少 " << kRegistryPath;
    EXPECT_TRUE(registry.malformed.empty())
        << "登记表存在无法识别的行:" << describeMalformed(registry);

    const std::string source = readFileOrEmpty(kPluginTestPath);
    ASSERT_FALSE(source.empty()) << "缺少 " << kPluginTestPath << "，GOLD 证据无法在库内复核";
    const auto blocks = collectGTestBlocks(source);
    ASSERT_FALSE(blocks.empty()) << "在 " << kPluginTestPath << " 里解析不出任何 TEST/TEST_F/TEST_P 块";

    int goldRows = 0;
    std::string failures;
    for (const auto& row : registry.rows) {
        if (row.status != "GOLD") {
            continue;
        }
        ++goldRows;
        // 缺第 4 列的 GOLD 行在上面 readRegistry 的 malformed 里已经红过一次，
        // 这里再点一次名，是为了让"哪一行没有证据"这条消息出现在 GOLD 审计用例里。
        if (row.evidence.empty()) {
            failures += "\n  " + row.key() + " 缺第 4 列证据用例名（第 " +
                std::to_string(row.lineNumber) + " 行）";
            continue;
        }
        const GTestBlock* block = nullptr;
        for (const auto& candidate : blocks) {
            if (candidate.id() == row.evidence) {
                block = &candidate;
                break;
            }
        }
        if (!block) {
            failures += "\n  " + row.key() + "." + row.evidence +
                " 在 tests/test_plugins.cpp 里找不到这个 TEST/TEST_F/TEST_P 用例名";
            continue;
        }
        // action 字符串要出现**在同一个块体内**（按字符串字面量找，避免 ui/info 这类短名
        // 因为出现在别的单词里而蒙过去）。
        if (block->body.find('"' + row.action + '"') == std::string::npos) {
            failures += "\n  " + row.key() + "." + row.evidence +
                " 块体内没有出现该行的 action 字符串 \"" + row.action + "\"";
        }
        if (!blockHasNumericAssertion(*block)) {
            failures += "\n  " + row.key() + "." + row.evidence +
                " 块体内没有 EXPECT_NEAR/EXPECT_FLOAT_EQ/EXPECT_DOUBLE_EQ 数值断言，GOLD 站不住";
        }
    }

    EXPECT_GT(goldRows, 0) << "登记表里一个 GOLD 行都没有，本用例就成了空检查";
    EXPECT_TRUE(failures.empty())
        << "以下 GOLD 行的库内证据不成立（请补数值断言，或把该行降为 PENDING）:" << failures;
}

TEST(GuiContract, EffectiveSpecsAgreeWithPluginSpecs) {
    gis::core::initGDAL();
    gis::framework::PluginManager mgr;
    mgr.loadFromDirectory(gis::tests::testPluginDir().string());
    // F4：没有这条的话插件加载失败时下面的循环遍历空集，本用例什么都不断。
    ASSERT_GE(mgr.plugins().size(), 13u);

    std::vector<Divergence> violations;
    std::set<std::string> whitelistHits;
    std::vector<std::string> deadRequiredKeys;

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
            const std::set<std::string>& panelRequired =
                config ? config->requiredKeys : required;
            const auto effective = gis::gui::buildEffectiveGuiParamSpecs(
                name, action, specs,
                config ? config->visibleKeys : visible,
                panelRequired);

            // F4：GUI 面板把插件的参数字典筛成空集的话，星号与必填校验都无从谈起。
            ASSERT_FALSE(effective.empty())
                << name << "." << action << " 的有效参数集合为空，展示层没有可呈现的插件参数";

            // F2（R-F2-A）：requiredKeys 是"这个 action 需要哪些参数必填"的展示层知识，
            // 它只能往严了改；列一个插件 paramSpecs 根本没有的键是死条目 —— 过去它会静默失效，
            // 现在必须变红。
            for (const auto& key : panelRequired) {
                bool declared = false;
                for (const auto& spec : specs) {
                    if (spec.key == key) declared = true;
                }
                if (!declared) {
                    deadRequiredKeys.push_back(name + "." + action + "." + key);
                }
            }

            for (const auto& eff : effective) {
                const gis::framework::ParamSpec* origin = nullptr;
                for (const auto& spec : specs) {
                    if (spec.key == eff.key) origin = &spec;
                }
                ASSERT_NE(origin, nullptr) << name << "." << action << " " << eff.key;
                const std::string key = name + "." + action + "." + eff.key;

                // required 的允许值只有一个：插件声明的必填 ∪ 本面板追加的必填。
                // 放宽（把插件说必填的改成不必填）与"追加了却没生效"都算分歧。
                const bool allowedRequired = origin->required || panelRequired.count(eff.key) > 0;
                if (eff.required != allowedRequired) {
                    Divergence divergence{key + ".required",
                        key + " 的 required 既不是插件声明的必填、也不是面板追加的必填（展示层无权改动真值）"};
                    violations.push_back(divergence);
                    if (kKnownDivergentOverrides.count(divergence.id)) {
                        whitelistHits.insert(divergence.id);
                    }
                }
                if (!(eff.defaultValue == origin->defaultValue)) {
                    Divergence divergence{key + ".defaultValue",
                        key + " 的默认值被展示层覆写（GUI 与 CLI 因此会提交不同参数）"};
                    violations.push_back(divergence);
                    if (kKnownDivergentOverrides.count(divergence.id)) {
                        whitelistHits.insert(divergence.id);
                    }
                }
            }
        }
    }

    std::string unexpected;
    for (const auto& violation : violations) {
        if (!kKnownDivergentOverrides.count(violation.id)) {
            unexpected += "\n  " + violation.message;
        }
    }
    EXPECT_TRUE(unexpected.empty())
        << "展示层与插件 paramSpecs 出现未登记的 required/默认值分歧（GUI 与 CLI 因此会提交不同参数）:"
        << unexpected;

    std::string dead;
    for (const auto& key : deadRequiredKeys) {
        dead += "\n  " + key;
    }
    EXPECT_TRUE(deadRequiredKeys.empty())
        << "以下 GUI 面板的 requiredKeys 键在插件 paramSpecs 里不存在（死条目，必填校验不会生效）:" << dead;

    // 分歧被删掉却还留在白名单里 = 挂账已清但账没销，同样要变红。
    std::string stale;
    for (const auto& key : kKnownDivergentOverrides) {
        if (!whitelistHits.count(key)) stale += "\n  " + key;
    }
    EXPECT_TRUE(stale.empty())
        << "以下分歧白名单条目已经没有对应的分歧了，请删除:" << stale;

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

    // R-F2-A 的另一半：展示层永远不能取消插件自己声明的必填。
    // raster_manage.nodata 的 input 由插件声明必填、面板也列进 requiredKeys，
    // 所以这里钉的是"effective 必须仍为 true"：任何把 required 改成 false 的实现都会让这里变红。
    const gis::framework::ParamSpec* inputSpec = nullptr;
    for (const auto& spec : nodataEffective) {
        if (spec.key == "input") inputSpec = &spec;
    }
    ASSERT_NE(inputSpec, nullptr) << "raster_manage.nodata 的可见参数里必须仍有 input";
    EXPECT_TRUE(inputSpec->required)
        << "插件声明必填的键不允许被展示层取消（raster_manage.nodata 的 input）";

    // R-F2-A 允许的另一半：展示层可以按 action 追加必填 —— projection.transform 的 dst_srs
    // 就是唯一能表达 per-action 必填的地方（插件级 ParamSpec 对 reproject 必须是可选的）。
    auto* projection = mgr.find("projection");
    ASSERT_NE(projection, nullptr);
    const auto* transformConfig = gis::gui::findActionUiConfig("projection", "transform");
    ASSERT_NE(transformConfig, nullptr);
    const auto transformEffective = gis::gui::buildEffectiveGuiParamSpecs(
        "projection", "transform", projection->paramSpecs(),
        transformConfig->visibleKeys, transformConfig->requiredKeys);
    const gis::framework::ParamSpec* transformDst = nullptr;
    const gis::framework::ParamSpec* transformSrc = nullptr;
    for (const auto& spec : transformEffective) {
        if (spec.key == "dst_srs") transformDst = &spec;
        if (spec.key == "src_srs") transformSrc = &spec;
    }
    ASSERT_NE(transformDst, nullptr);
    ASSERT_NE(transformSrc, nullptr);
    EXPECT_TRUE(transformDst->required)
        << "面板把 dst_srs 列进 requiredKeys，projection.transform 就必须把它当必填（GUI 校验的根据）";
    EXPECT_FALSE(transformSrc->required)
        << "src_srs 既不被插件声明必填、也没被列进 requiredKeys，展示层无权凭喜好加严";
}

// R-T13-5：给 `pan_method` 建"界面不得供一个插件不接受的值"这条锁。
// WHY 必须是元断言而不是"跑一次 GUI 用例"：这张中文标签表在建成这条锁之前是**零测试覆盖**的
// —— 全仓只有 tests/test_plugins.cpp 以 brovey 跑过一次执行，没有任何用例碰过标签表本身。
// 于是"改了插件枚举、忘了改 param_card_widget.cpp 的标签表"这种漏改照样全量绿：
// 下拉的取值永远来自 spec.enumValues（param_card_widget.cpp 的 createEnumWidget 就是按 enumValues
// 逐项 addItem），标签表只负责给它们配中文文案，缺文案只会退化成显示裸值、不会失败。
// 只锁 pan_method 这一条：把整张标签表做成通用元断言属 Task 21 / K-14 的统一动作，
// 现在建会与本任务及后续任务的改名互相打断。
TEST(GuiContract, PanMethodLabelTableMatchesPluginEnumValues) {
    gis::core::initGDAL();
    gis::framework::PluginManager mgr;
    mgr.loadFromDirectory(gis::tests::testPluginDir().string());
    // 同 GuiContract.EffectiveSpecsAgreeWithPluginSpecs：插件没加载成功时下面会退化成空检查。
    ASSERT_GE(mgr.plugins().size(), 13u);

    auto* processing = mgr.find("processing");
    ASSERT_NE(processing, nullptr);
    // WHY 先落到具名变量：Plugin::paramSpecs() **按值返回** std::vector<ParamSpec>，
    // 直接在 range-for 里 &spec 会在语句结束时留下悬垂指针（实测读到空 enumValues）。
    const auto processingSpecs = processing->paramSpecs();
    const gis::framework::ParamSpec* methodSpec = nullptr;
    for (const auto& spec : processingSpecs) {
        if (spec.key == "pan_method") methodSpec = &spec;
    }
    ASSERT_NE(methodSpec, nullptr) << "processing 的 paramSpecs 里必须仍有 pan_method";
    ASSERT_FALSE(methodSpec->enumValues.empty()) << "pan_method 不是枚举参数了？标签表锁失去对象";

    const std::vector<std::string> labelKeys = ParamCardWidget::enumLabelKeys("pan_method");
    ASSERT_FALSE(labelKeys.empty())
        << "src/gui/param_card_widget.cpp 的中文标签表里没有 pan_method 这一组（整组被删？）";

    const std::set<std::string> enumValues(methodSpec->enumValues.begin(), methodSpec->enumValues.end());
    const std::set<std::string> labels(labelKeys.begin(), labelKeys.end());
    ASSERT_FALSE(enumValues.empty());

    // 方向 1（Produces 承诺的那半）：标签表不得供一个插件已不接受的取值。
    // 判别变异 M-G2：把标签表里 mean_blend 那一行改回 simple_mean ⇒ 这里两侧同时红。
    std::string stale;
    for (const auto& key : labels) {
        if (!enumValues.count(key)) stale += "\n  pan_method=" + key;
    }
    EXPECT_TRUE(stale.empty())
        << "GUI 中文标签表提供了 pan_method 不接受的取值（界面上选了必然失败）:" << stale;

    // 方向 2：每个 enumValues 都得有标签。缺了不会被"下拉里选不出来"暴露（取值来自 spec），
    // 只会静默显示裸值 ⇒ 判别变异 M-G1（删掉标签表里 mean_blend 那一行）靠这条变红。
    std::string missing;
    for (const auto& key : enumValues) {
        if (!labels.count(key)) missing += "\n  pan_method=" + key;
    }
    EXPECT_TRUE(missing.empty())
        << "pan_method 的取值在 GUI 标签表里没有登记的中文文案:" << missing;
}
