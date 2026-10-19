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
#include <iostream>
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

// ---------------------------------------------------------------- 判据的原文加工（N90）
//
// 去掉 // 行注释与 /* */ 块注释，但**原样保留字符串/字符/原始字符串字面量的内容**。
// 换行留着：失败消息报的是"第几行的什么"，行号感一丢就没法对着原文核。
//
// WHY 保留字符串内容（本任务指定的判别方向）：F-b 的归属形状 `params["action"] = std::string("x")`
// 的键与值整个活在字符串里，把字符串内容一起删掉等于把归属判据的依据删掉；另一方面字面量里的
// `//`（JSON 夹具、URL、路径）不能被当成注释起点，所以这个函数必须自己走状态机，
// 不能拿 `find("//")` 一把梭。
std::string stripCppComments(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = text[i];
        const char next = (i + 1 < n) ? text[i + 1] : '\0';
        if (c == '/' && next == '/') {
            while (i < n && text[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && next == '*') {
            i += 2;
            while (i < n) {
                if (text[i] == '*' && (i + 1 < n) && text[i + 1] == '/') {
                    i += 2;
                    break;
                }
                if (text[i] == '\n') out.push_back('\n');
                ++i;
            }
            continue;
        }
        if (c == 'R' && next == '"') {
            // 原始字符串 R"delim( ... )delim"：整段按原文抄走，里面的引号与括号都不参与配对。
            const std::size_t open = text.find('(', i + 2);
            const std::size_t close = open == std::string::npos ? std::string::npos : text.find(')', open);
            if (open != std::string::npos && close != std::string::npos) {
                const std::string delim = text.substr(i + 2, open - (i + 2));
                const std::string terminator = ")" + delim + "\"";
                const std::size_t end = text.find(terminator, close);
                if (end != std::string::npos) {
                    out.append(text, i, end + terminator.size() - i);
                    i = end + terminator.size();
                    continue;
                }
            }
        }
        if (c == '"' || c == '\'') {
            const char quote = c;
            out.push_back(c);
            ++i;
            while (i < n) {
                if (text[i] == '\\' && i + 1 < n) {
                    out.push_back(text[i]);
                    out.push_back(text[i + 1]);
                    i += 2;
                    continue;
                }
                out.push_back(text[i]);
                if (text[i] == quote) {
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

// 代码（剥掉注释后的原文）里是否还有"把数值钉住"的断言。N90 之后的判据：注释里写的不算。
bool codeHasNumericAssertion(const std::string& code) {
    for (const auto& assertion : kNumericAssertions) {
        if (code.find(assertion) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool blockHasNumericAssertion(const GTestBlock& block) {
    return codeHasNumericAssertion(stripCppComments(block.body));
}

// ---------------------------------------------------------------- 判据：归属（F-b）
//
// 归属要的是"这个块确实执行了这一行的 plugin.action"，不是"这个块里出现过那个字符串"。
// 区分靠动作槽：参数表交给插件的那个键就叫 "action"，所以 action 的字面量必须出现在
// `["action"] = …` 的右值里（或经 case 表的成员流进去）。而 `params["threshold"] = 1.0` 里的
// "threshold" 只是一个**参数键**，跟 action 无关 —— 批次 A 复审的洗白配方吃的就是旧判据。

const std::string kActionSlot = "[\"action\"]";

bool isSpaceAt(const std::string& text, std::size_t pos) {
    return pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r');
}

std::size_t skipSpaces(const std::string& text, std::size_t pos) {
    while (isSpaceAt(text, pos)) ++pos;
    return pos;
}

// text 在 pos 处以 needle 开头则返回其后的位置，否则 npos。
std::size_t matchAt(const std::string& text, std::size_t pos, const std::string& needle) {
    if (pos + needle.size() > text.size()) return std::string::npos;
    if (text.compare(pos, needle.size(), needle) != 0) return std::string::npos;
    return pos + needle.size();
}

bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// 逐处试：对每一处 `["action"]` 调用 read 读右值，读通了就返回其后的**位置**，读不通就找下一处，
// 全部读不通返回 npos。
// WHY 返回位置而不是 bool：npos 转成 bool 是**真**，`return anyActionSlot(...)` 直接进 bool 函数
// 会造出一条恒真的判据，所以调用方一律写 `!= std::string::npos`。
template <typename Reader>
std::size_t anyActionSlot(const std::string& code, Reader read) {
    std::size_t at = 0;
    while ((at = code.find(kActionSlot, at)) != std::string::npos) {
        const std::size_t done = read(code, at + kActionSlot.size());
        if (done != std::string::npos) return done;
        ++at;
    }
    return std::string::npos;
}

// `["action"]` 之后是不是 `= std::string("<action>")` 或 `= "<action>"`。
// WHY 两种都收：ParamValue 是 variant，tests/test_plugins.cpp 的动作槽一律用 std::string() 包一层
// （现读：`grep -c 'params\["action"\] = std::string(' tests/test_plugins.cpp` 与
// `grep -c 'params\["action"\]' tests/test_plugins.cpp` 的差就是那唯一一处表驱动的成员赋值，
// 它走下面 actionSlotHoldsLoopMember 那条形态），登记表头部说明里另有裸字面量的写法。
// 两种都把 action 字面量放进了动作槽，归属强度一样，所以不必强迫裸字面量改写。
bool actionSlotHoldsLiteral(const std::string& code, const std::string& action) {
    const std::string quoted = '"' + action + '"';
    return anyActionSlot(code, [&quoted](const std::string& text, std::size_t pos) {
        std::size_t p = matchAt(text, skipSpaces(text, pos), "=");
        if (p == std::string::npos) return std::string::npos;
        p = skipSpaces(text, p);
        const std::size_t wrapper = matchAt(text, p, "std::string");
        if (wrapper != std::string::npos) {
            p = matchAt(text, skipSpaces(text, wrapper), "(");
            if (p == std::string::npos) return std::string::npos;
            p = skipSpaces(text, p);
        }
        p = matchAt(text, p, quoted);
        if (p == std::string::npos) return std::string::npos;
        return skipSpaces(text, p);
    }) != std::string::npos;
}

// `["action"] = <标识符>.action`：动作槽接的是循环变量的成员（表驱动用例）。
bool actionSlotHoldsLoopMember(const std::string& code) {
    return anyActionSlot(code, [](const std::string& text, std::size_t pos) {
        std::size_t p = matchAt(text, skipSpaces(text, pos), "=");
        if (p == std::string::npos) return std::string::npos;
        p = skipSpaces(text, p);
        std::size_t ident = p;
        while (ident < text.size() && isIdentChar(text[ident])) ++ident;
        if (ident == p) return std::string::npos;  // 等号后面连标识符都没有
        p = matchAt(text, ident, ".action");
        if (p == std::string::npos) return std::string::npos;
        if (p < text.size() && isIdentChar(text[p])) return std::string::npos;  // .actionX 不算
        return p;
    }) != std::string::npos;
}

// `{"<action>"`：action 的字面量作为某个初始化列表的**首元素**出现（case 表里的那一行）。
bool caseTableEntryForAction(const std::string& code, const std::string& action) {
    const std::string quoted = '"' + action + '"';
    std::size_t at = 0;
    while ((at = code.find(quoted, at)) != std::string::npos) {
        std::size_t back = at;
        while (back > 0 && isSpaceAt(code, back - 1)) --back;
        if (back > 0 && code[back - 1] == '{') return true;
        at += quoted.size();
    }
    return false;
}

// 一个块体是否属于这一行的 plugin.action（输入必须是 stripCppComments 的结果）。
bool blockAttributesAction(const std::string& code, const std::string& plugin, const std::string& action) {
    if (code.find('"' + plugin + '"') == std::string::npos) return false;
    if (actionSlotHoldsLiteral(code, action)) return true;
    // 表驱动形态要两条同时成立：动作槽由 case 表的成员喂，且该 action 的字面量确实在那张表里。
    // WHY 两条都要：只查成员访问，任意带 `.action` 的块都能蒙过去；只查 `{"x"`，
    // 同一份字面量出现在文案表里也能蒙过去。两条合起来保住的是"删掉表项 ⇒ 对应 GOLD 行立刻红"
    // —— 今天靠这条形态成立的是 spindex 那张 15 行的 case 表。
    return actionSlotHoldsLoopMember(code) && caseTableEntryForAction(code, action);
}

// F-c：requiredKeys 里不在 visibleKeys 中的键（面板不呈现的键，必填校验永远静默失效）。
std::vector<std::string> requiredKeysOutsideVisibleKeys(const std::string& prefix,
                                                        const std::set<std::string>& visibleKeys,
                                                        const std::set<std::string>& requiredKeys) {
    std::vector<std::string> violations;
    for (const auto& key : requiredKeys) {
        if (visibleKeys.find(key) == visibleKeys.end()) violations.push_back(prefix + "." + key);
    }
    return violations;
}

// 一条 GOLD 行的库内证据审计：空串表示证据成立，非空是可直接拼进 EXPECT 消息的失败原因。
std::string goldEvidenceProblem(const RegistryRow& row, const std::vector<GTestBlock>& blocks) {
    if (row.evidence.empty()) {
        return "\n  " + row.key() + " 缺第 4 列证据用例名（第 " + std::to_string(row.lineNumber) + " 行）";
    }
    const GTestBlock* block = nullptr;
    for (const auto& candidate : blocks) {
        if (candidate.id() == row.evidence) {
            block = &candidate;
            break;
        }
    }
    if (!block) {
        return "\n  " + row.key() + "." + row.evidence +
            " 在 tests/test_plugins.cpp 里找不到这个 TEST/TEST_F/TEST_P 用例名";
    }
    // 两条判据都读**剥掉注释后的块体**（N90 修的就是"判据读原文"这件事），
    // 所以注释里的形状既撑不起归属、也撑不起数值断言。
    const std::string code = stripCppComments(block->body);
    std::string problem;
    if (!blockAttributesAction(code, row.plugin, row.action)) {
        problem += "\n  " + row.key() + "." + row.evidence +
            " 块体不属于这一行：要同时出现 \"" + row.plugin + "\" 和把 \"" + row.action +
            "\" 送进 [\"action\"] 动作槽的形状（引用别的插件/别的 action 的用例记不了 GOLD）";
    }
    if (!codeHasNumericAssertion(code)) {
        problem += "\n  " + row.key() + "." + row.evidence +
            " 块体的代码里没有 EXPECT_NEAR/EXPECT_FLOAT_EQ/EXPECT_DOUBLE_EQ 数值断言（写在注释里不算），GOLD 站不住";
    }
    return problem;
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
    // 上限 = 登记表里 PENDING 的真实数量，不是估算值。
    // B8（判据加严那次）用新判据复核过全表，降级 0 行，所以这个数与批次 A 收尾时相同。
    // 现读命令（口径：一律先排掉以 # 开头的注释行 —— 本表头部为解释判据加了不少注释，
    //   裸 grep 会把注释里的字样一起数进去，那是本项目真发生过的"注释污染计数"）：
    //     数据行：grep -v '^#' tests/action_gold_cases.txt | wc -l                = 126
    //     GOLD  ：grep -v '^#' tests/action_gold_cases.txt | grep -c '[,]GOLD'    = 52
    //     PENDING：grep -v '^#' tests/action_gold_cases.txt | grep -c '[,]PENDING' = 74
    //   恒等式 126 = 52 + 74；只有"把某个 action 补上数值测试并改成 GOLD"才允许把这个上限调小，
    //   逐行核对标准见 tests/action_gold_cases.txt 头部注释。
    EXPECT_LE(pending, 74) << "PENDING 数量不得增加；若某 action 已补数值测试，请把登记表改为 GOLD";
}

TEST(CoverageMeta, GoldEvidenceIsAuditable) {
    // F1 / R-F1-A：GOLD 行的凭据必须落在版本库里，并且能被机器复核。
    // 登记表第 4 列写用例名，这里回到 tests/test_plugins.cpp 的**那个块体内**核对三件事，
    // 判据本身是 goldEvidenceProblem（N90/F-b 加严后）：
    //   （a）用例存在；
    //   （b）块体**属于这一行**：剥掉注释后的原文里同时出现 "<plugin>" 和把 "<action>" 送进
    //        ["action"] 动作槽的形状（字面量直接进槽，或经 case 表的成员进槽）；
    //   （c）剥掉注释后的原文里有 EXPECT_NEAR/EXPECT_FLOAT_EQ/EXPECT_DOUBLE_EQ。
    // 任何人软化或删掉那条数值断言、把动作槽那行注释掉、给用例改名、或引用一个不执行该 action
    // 的块，对应 GOLD 行立刻变红。判据自己的判别力由 CoverageMetaJudge 那组用例钉住。
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
        const std::string problem = goldEvidenceProblem(row, blocks);
        if (!problem.empty()) {
            failures += problem;
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
    std::vector<std::string> requiredOutsideVisible;
    int requiredBearingConfigs = 0;

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
            const std::set<std::string>& panelVisible =
                config ? config->visibleKeys : visible;
            const auto effective = gis::gui::buildEffectiveGuiParamSpecs(
                name, action, specs, panelVisible, panelRequired);

            // F-c（本任务新加的元断言）：requiredKeys 必须是 visibleKeys 的子集。
            // WHY 需要单独一条：required 的加严发生在**过滤之后**，所以列一个不在 visibleKeys 里的键
            // 既不会报错也不会生效 —— 面板上连那个输入框都没有，星号与提交前校验无从附着。
            // 上面那条 deadRequiredKeys 查的是"键在插件 paramSpecs 里存在"，覆盖不到这件事。
            if (config && !config->requiredKeys.empty()) ++requiredBearingConfigs;
            for (const auto& key : requiredKeysOutsideVisibleKeys(name + "." + action, panelVisible, panelRequired)) {
                requiredOutsideVisible.push_back(key);
            }

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

    // F-c：requiredKeys ⊆ visibleKeys。今天全表无实例（现读命令：把本用例的 requiredOutsideVisible
    // 打印出来数，或跑 CoverageMetaJudge.RequiredKeyOutsideVisibleKeysIsReported 那条合成用例，
    // 它证明这条臂真的会咬人，不是恒真断言），所以这是一条"未来同类静默"防线，成本三行。
    std::string outsideVisible;
    for (const auto& key : requiredOutsideVisible) {
        outsideVisible += "\n  " + key;
    }
    EXPECT_TRUE(requiredOutsideVisible.empty())
        << "以下 GUI 面板的 requiredKeys 键没有出现在同一个 action 的 visibleKeys 里："
        << "必填校验加在一个面板上不呈现的键上，永远静默失效:" << outsideVisible;
    // 非空检查：整张表一个 requiredKeys 都没有的话，上面那条 EXPECT 就是空检查。
    ASSERT_GT(requiredBearingConfigs, 0)
        << "GUI 面板表里没有任何非空 requiredKeys，F-c 的臂失去对象（表被清空？）";

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

// ============================================================ Task 21-C 反向参数守卫的导出侧
//
// 命题（与 #334 方向相反）：某 action 的 visibleKeys 里**不得**出现该 action 根本不读的键。
// 白名单在这里（GUI 侧，运行时真值），实读在 src/plugins/**/*.cpp（编译期文本），
// 两侧不在同一可执行域里，所以本用例只负责把**运行时白名单全量导出成文本产物**，
// 比对由 tests/check_action_visible_keys_read.cmake（ctest 用例）完成。
//
// WHY 枚举源用 Task 3 的动作登记册而不是 planAActions()：登记册由
// CoverageMeta.EveryActionIsRegistered 双向钉住（漏登记红、陈旧行也红），所以它等于运行时 action 全集；
// planAActions() 是手抄清单，会随任务漂移，拿它当枚举源 ⇒ 守卫的覆盖面跟着清单一起烂掉。
//
// WHY 只依赖 findActionUiConfig 而不加载插件 DLL：本用例要的是展示层的真值，
// gui_data_support.cpp 与测试编在同一个可执行文件里（tests/CMakeLists.txt:15），
// 不加载插件 ⇒ 产物不受 `--target` 不刷新插件 DLL 那条坑影响，也更快更确定。
//
// WHY 产物落在可执行文件同目录而不是 CMakeBinaryDir 根：gis_tests 的 TARGET_FILE_DIR 已经按
// 配置（Debug/Release）分叉，两棵树各写各的，互不覆盖；cmake -P 用例用
// $<TARGET_FILE_DIR:gis_tests> 拿到同一个目录，脚本里不出现任何机器绝对路径。
TEST(GuiContract, ExportActionVisibleKeys) {
    const auto registry = readRegistry();
    ASSERT_FALSE(registry.rows.empty()) << "缺少 " << kRegistryPath;
    EXPECT_TRUE(registry.malformed.empty())
        << "登记表存在无法识别的行，导出集合因此不可信:" << describeMalformed(registry);

    // 登记表按 (plugin,action) 去重后排序：产物必须逐字节确定，否则 diff 出来的变化分不清
    // 是真改动还是遍历顺序改动。
    std::set<std::string> pairs;
    for (const auto& row : registry.rows) {
        pairs.insert(row.key());
    }
    ASSERT_EQ(pairs.size(), registry.rows.size())
        << "登记表里有重复的 plugin.action 行，导出会跟着重复计数";

    std::vector<std::string> lines;
    lines.push_back("# GIS_TOOL GUI action visibleKeys export");
    lines.push_back("# 由 gis_tests 的 GuiContract.ExportActionVisibleKeys 生成，勿手改。");
    lines.push_back("# 每行一条三元组，竖线分隔，按字典序：plugin|action|key");
    lines.push_back("# 特殊行 plugin|action|@NO_ACTION_UI_CONFIG 表示该 action 在 GUI 侧没有条目，");
    lines.push_back("# 此时 mainwindow.cpp 会传空集给 buildEffectiveGuiParamSpecs，面板不做过滤、");
    lines.push_back("# 直接把插件级全部参数铺出来（键集合无法从 ActionUiConfig 导出），扫描器单独报。");
    lines.push_back("plugin|action|key");

    int configured = 0;
    int unconfigured = 0;
    std::size_t keyRows = 0;
    for (const auto& pair : pairs) {
        const std::size_t dot = pair.find('.');
        const std::string plugin = pair.substr(0, dot);
        const std::string action = pair.substr(dot + 1);
        const auto* config = gis::gui::findActionUiConfig(plugin, action);
        if (!config) {
            ++unconfigured;
            lines.push_back(plugin + "|" + action + "|@NO_ACTION_UI_CONFIG");
            continue;
        }
        ++configured;
        for (const auto& key : config->visibleKeys) {
            lines.push_back(plugin + "|" + action + "|" + key);
            ++keyRows;
        }
    }
    ASSERT_GT(configured, 0) << "一条 ActionUiConfig 都没取到，导出的产物没有意义";

    const auto outPath = gis::tests::testExecutableDir() / "gui_action_visible_keys.txt";
    std::ofstream ofs(outPath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(static_cast<bool>(ofs)) << "无法写入 " << outPath.string();
    for (const auto& line : lines) {
        ofs << line << "\n";
    }
    ofs.close();
    ASSERT_TRUE(static_cast<bool>(ofs)) << "写入 " << outPath.string() << " 时出错";

    // 断言的是"产物存在且形状对"，具体比对在 cmake -P 侧。
    std::ifstream check(outPath, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("plugin|action|key"), std::string::npos);
    // 产物里不许出现机器绝对路径（守卫用例的确定性要求）。
    EXPECT_EQ(content.find("D:/"), std::string::npos);
    EXPECT_EQ(content.find("C:\\\\"), std::string::npos);
    // 汇总打到标准输出，故意违反注入与首轮真实运行都要靠这几行读数。
    std::cout << "[21C-EXPORT] actions=" << pairs.size()
              << " configured=" << configured
              << " no_ui_config=" << unconfigured
              << " visible_key_rows=" << keyRows
              << " path=" << outPath.string() << std::endl;
}

// ============================================================ 判据自身的单测（N90 / F-a / F-b / F-c）
//
// WHY 判据要有自己的用例，而不是只跑登记表：CoverageMeta.GoldEvidenceIsAuditable 只对**今天这张表**
// 成立，判据本身被改软（把剥注释那一步删掉、或把归属要求退回"action 字符串在块里出现过"）时它
// 照样全绿 —— 判据的判别力只能由合成块钉住。

namespace {

GTestBlock syntheticBlock(const std::string& body) {
    GTestBlock block;
    block.suite = "Synthetic";
    block.name = "Block";
    block.body = body;
    return block;
}

RegistryRow syntheticRow(const std::string& plugin, const std::string& action, const std::string& evidence) {
    RegistryRow row;
    row.lineNumber = 0;
    row.plugin = plugin;
    row.action = action;
    row.status = "GOLD";
    row.evidence = evidence;
    return row;
}

}  // namespace

// N90 的核心可杀性：块里唯一的 EXPECT_NEAR 写在注释里 ⇒ 判 false。
// 判据今天直接读块体原文，所以这条用例复现的就是"删掉真断言、只留那句注释，GOLD 行照样绿"。
TEST(CoverageMetaJudge, LineCommentOnlyNumericAssertionIsNotCounted) {
    const GTestBlock block = syntheticBlock(
        "\n    const auto result = p->execute(params, progress_);\n"
        "    EXPECT_TRUE(result.success);\n"
        "    // EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.5f, 1e-4f);\n");
    EXPECT_FALSE(blockHasNumericAssertion(block))
        << "注释里的 EXPECT_NEAR 不是断言：那条数值钉住的凭据并不存在";
}

// 同上，只是换成 /* */ 形态（本项目大量用块注释写"为什么这样断言"）。
TEST(CoverageMetaJudge, BlockCommentOnlyNumericAssertionIsNotCounted) {
    const GTestBlock block = syntheticBlock(
        "\n    /* 这一版本来想钉像素值：\n"
        "       EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.5f, 1e-4f);\n"
        "       EXPECT_DOUBLE_EQ 也一样，写在注释里都不算 */\n"
        "    EXPECT_TRUE(fs::exists(output));\n");
    EXPECT_FALSE(blockHasNumericAssertion(block));
}

// 控制组：注释 + 真断言共存时必须仍然判 true（加严不许把有证据的块一起打掉）。
TEST(CoverageMetaJudge, RealAssertionAlongsideCommentIsStillCounted) {
    const GTestBlock block = syntheticBlock(
        "\n    // EXPECT_NEAR 的历史写法已废弃\n"
        "    EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.5f, 1e-4f);\n");
    EXPECT_TRUE(blockHasNumericAssertion(block));
}

// 判别方向（本任务指定的那条）：剥注释只剥注释，字符串字面量的内容原样保留 ——
// 归属形状与 metadata 键名都活在字符串里，顺手删掉会让 F-b 失去依据；
// 同时字面量里的 // 与引号不得被当成注释起点/字符串终点。
TEST(CoverageMetaJudge, StripCppCommentsKeepsStringLiteralContents) {
    const std::string code = stripCppComments(
        "    EXPECT_EQ(params.at(\"note\"), \"EXPECT_NEAR 钉住的是像素值\");\n"
        "    const std::string url = \"http://example.com/a.tif\";  // 真注释\n"
        "    /* 块注释里的 \" 不参与字符串配对 */\n"
        "    params[\"action\"] = std::string(\"threshold\");\n");
    EXPECT_NE(code.find("EXPECT_NEAR 钉住的是像素值"), std::string::npos)
        << "字符串字面量内容必须原样保留";
    EXPECT_NE(code.find("http://example.com/a.tif"), std::string::npos)
        << "字面量里的 // 不得被当成注释起点";
    EXPECT_EQ(code.find("真注释"), std::string::npos);
    EXPECT_EQ(code.find("不参与字符串配对"), std::string::npos);
    EXPECT_NE(code.find("params[\"action\"] = std::string(\"threshold\")"), std::string::npos);
}

// F-b：同名的**参数键**不是归属。块体属于 matching/change，
// 里面的 "threshold" 是 params["threshold"] 这个参数键，不能拿去给 processing.threshold 记证据。
TEST(CoverageMetaJudge, ParamKeyNamedLikeTheActionIsNotAttribution) {
    const std::string code = stripCppComments(
        "    auto* p = mgr_.find(\"matching\");\n"
        "    params[\"action\"] = std::string(\"change\");\n"
        "    params[\"threshold\"] = 1.0;\n"
        "    EXPECT_NEAR(std::stod(result.metadata.at(\"change_ratio\")), 0.03, 1e-6);\n");
    EXPECT_FALSE(blockAttributesAction(code, "processing", "threshold"))
        << "引用别的插件/别的 action 的用例记不了 GOLD";
    EXPECT_TRUE(blockAttributesAction(code, "matching", "change")) << "正对照：真正的执行块要仍然成立";
    EXPECT_FALSE(blockAttributesAction(code, "matching", "register")) << "plugin 对得上但 action 对不上也不行";
}

// F-a 与 F-b 的交点：动作槽形状只写在注释里 ⇒ 剥完注释后归属不成立。
TEST(CoverageMetaJudge, CommentedOutActionSlotShapeIsNotAttribution) {
    const std::string code = stripCppComments(
        "    auto* p = mgr_.find(\"processing\");\n"
        "    // params[\"action\"] = std::string(\"threshold\");\n"
        "    EXPECT_NEAR(readRasterPixel(output, 1, 1), 0.0f, 1e-6f);\n");
    EXPECT_FALSE(blockAttributesAction(code, "processing", "threshold"));
    EXPECT_FALSE(actionSlotHoldsLiteral(code, "threshold"))
        << "注释里的形状被剥掉之后不该还剩下来";
}

// 表驱动形态（今天 spindex 那一族 15 行就靠它）：动作槽接的是 case 表的成员，
// 于是该 action 的字面量必须作为表项首元素出现在同一个块里。两条各删一次都要能红。
TEST(CoverageMetaJudge, TableDrivenLoopBindingIsAttributed) {
    const std::string withEntry = stripCppComments(
        "    auto* p = mgr_.find(\"spindex\");\n"
        "    const std::vector<CaseSpec> cases = {\n"
        "        {\"ndbi\", \"e2e_spindex_ndbi_output.tif\", {{\"swir1_band\", 5}}, 0.125f},\n"
        "    };\n"
        "    for (const auto& testCase : cases) {\n"
        "        params[\"action\"] = testCase.action;\n"
        "        EXPECT_NEAR(readRasterPixel(output, 5, 5), testCase.expectedValue, 1e-4f);\n"
        "    }\n");
    EXPECT_TRUE(blockAttributesAction(withEntry, "spindex", "ndbi"));

    const std::string withoutEntry = stripCppComments(
        "    auto* p = mgr_.find(\"spindex\");\n"
        "    const std::vector<CaseSpec> cases = {\n"
        "        {\"ndwi\", \"e2e_spindex_ndwi_output.tif\", {{\"green_band\", 2}}, 0.5f},\n"
        "    };\n"
        "    for (const auto& testCase : cases) {\n"
        "        params[\"action\"] = testCase.action;\n"
        "        EXPECT_NEAR(readRasterPixel(output, 5, 5), testCase.expectedValue, 1e-4f);\n"
        "    }\n");
    EXPECT_FALSE(blockAttributesAction(withoutEntry, "spindex", "ndbi"))
        << "表项被删掉就是覆盖被偷走，这一行必须变红";

    // 只有字面量、动作槽里没有流向它的赋值（比如那张表只是文案表）⇒ 不算归属。
    const std::string noSlotBinding = stripCppComments(
        "    auto* p = mgr_.find(\"spindex\");\n"
        "    const std::vector<Label> labels = {{\"ndbi\", \"归一化差值植被指数\"}};\n"
        "    EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.125f, 1e-4f);\n");
    EXPECT_FALSE(blockAttributesAction(noSlotBinding, "spindex", "ndbi"));
}

// 批次 A 复审给的确切洗白配方，拿**真源码**复跑一次：
// 引用 PluginTest.MatchingChangeExecutionReportsChangedPixels 给 processing.threshold 记 GOLD
// （那里面的 "threshold" 是 params["threshold"] 这个参数键）。
TEST(CoverageMetaJudge, BatchAReviewExploitIsRejectedOnRealSource) {
    const std::string source = readFileOrEmpty(kPluginTestPath);
    ASSERT_FALSE(source.empty()) << "缺少 " << kPluginTestPath;
    const auto blocks = collectGTestBlocks(source);
    ASSERT_FALSE(blocks.empty());

    const RegistryRow exploit = syntheticRow(
        "processing", "threshold", "PluginTest.MatchingChangeExecutionReportsChangedPixels");
    EXPECT_FALSE(goldEvidenceProblem(exploit, blocks).empty())
        << "洗白配方必须被拦住：这个块执行的是 matching/change，不是 processing/threshold";

    // 正对照：同一个块对它真正执行的那个 action 仍然算证据（加严不许把好证据打掉）。
    const RegistryRow honest = syntheticRow(
        "matching", "change", "PluginTest.MatchingChangeExecutionReportsChangedPixels");
    EXPECT_TRUE(goldEvidenceProblem(honest, blocks).empty());
}

// F-c：requiredKeys 列了 visibleKeys 里没有的键 ⇒ 点名（今天全表无实例，所以这里用合成集证明臂会咬人）。
TEST(CoverageMetaJudge, RequiredKeyOutsideVisibleKeysIsReported) {
    const std::set<std::string> visible{"input", "output"};
    const std::set<std::string> required{"input", "output", "band"};
    const auto violations = requiredKeysOutsideVisibleKeys("raster_manage.nodata", visible, required);
    ASSERT_EQ(violations.size(), 1u) << "列进 requiredKeys 却没列进 visibleKeys 的键必须被点名";
    EXPECT_NE(violations[0].find("band"), std::string::npos) << violations[0];
    EXPECT_NE(violations[0].find("raster_manage.nodata"), std::string::npos) << violations[0];
    EXPECT_TRUE(requiredKeysOutsideVisibleKeys("raster_manage.nodata", visible,
                                              std::set<std::string>{"input"}).empty());
}

