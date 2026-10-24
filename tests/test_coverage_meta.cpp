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

// ---------------------------------------------------------------- 登记表头部的机器可核对计数
//
// WHY 把注释里的数字做成键值对来读，而不是把散文拿去跟真值比对：判据要能在"B11 晋升了行、
// 却忘改头部"这件事上变红，前提是那个数字**有名字**、能被单独取出来对撞。
// 形状：`#   COUNT <key> = <digits>`，行尾剩下的部分是给人现跑的 grep 命令。

struct DeclaredCount {
    std::string key;
    int value = 0;
    int lineNumber = 0;
};

// 一行是不是 COUNT 行；是就填 out 并返回 true。
// 键后面必须紧跟 `=`，`=` 后面必须紧跟十进制数字 —— 任一处不成立就当这行不存在（调用方
// CoverageMeta.RegistryHeaderCountsMatchData 会另外断言"含 COUNT 的行数 == 解析成功的条数"，
// 所以这里放过畸形行不会把判据变松，只会让它红得更直接）。
bool parseDeclaredCount(const std::string& line, std::size_t offset, DeclaredCount& out) {
    static const std::string marker = "COUNT ";
    const std::size_t at = line.find(marker, offset);
    if (at == std::string::npos) return false;
    std::size_t p = at + marker.size();
    const std::size_t keyBegin = p;
    while (p < line.size() && line[p] != ' ' && line[p] != '\t') ++p;
    const std::string key = line.substr(keyBegin, p - keyBegin);
    if (key.empty()) return false;
    while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
    if (p >= line.size() || line[p] != '=') return false;
    ++p;
    while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
    const std::size_t digitsBegin = p;
    while (p < line.size() && line[p] >= '0' && line[p] <= '9') ++p;
    if (p == digitsBegin) return false;
    // 位数上限：判据不许因为头部被人写进一个装不进 int 的数字而抛异常（抛异常 = 整个用例崩，
    // 崩了的消息比红掉的消息难读得多）。
    if (p - digitsBegin > 9) return false;
    out.key = key;
    out.value = std::stoi(line.substr(digitsBegin, p - digitsBegin));
    return true;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // 与 readRegistry 同一套行尾归一化
        lines.push_back(line);
    }
    return lines;
}

// 头部声明的全部计数，以及文件里出现过 "COUNT" 的行数（后者用来抓"写了 COUNT 但不是合法计数行"）。
struct DeclaredCounts {
    std::vector<DeclaredCount> entries;
    int linesMentioningCount = 0;
};

DeclaredCounts readDeclaredCounts(const std::vector<std::string>& lines) {
    DeclaredCounts result;
    int lineNumber = 0;
    for (const auto& line : lines) {
        ++lineNumber;
        if (line.find("COUNT") == std::string::npos) continue;
        ++result.linesMentioningCount;
        std::size_t offset = 0;
        DeclaredCount parsed;
        while (parseDeclaredCount(line, offset, parsed)) {
            parsed.lineNumber = lineNumber;
            result.entries.push_back(parsed);
            offset = line.find("COUNT", offset) + 6;
        }
    }
    return result;
}

// 数据行算出来的真值。口径与头部每行行尾写死的现读命令逐字一致：
// 除 gold_rows_all_lines 以外，一律**先排掉以 # 开头的注释行**，
// 因为本项目真发生过"为解释判据而写的注释把数那条判据的 grep 撑大"。
struct ComputedCounts {
    int dataRows = 0;             // 非注释非空行条数（机器口径，= 头部现读命令改后的口径）
    int nonCommentLines = 0;      // 只排注释行、**不**排空行（= 旧现读命令 `grep -v '^#' | wc -l` 的口径）
    int goldRows = 0;             // 其中含 ",GOLD" 的行数
    int pendingRows = 0;          // 其中含 ",PENDING" 的行数
    int goldRowsAllLines = 0;     // 不排注释行的裸计数（头部专门记了它比 goldRows 多几）
    int goldRowsTableDriven = 0;  // 其中第 4 列指向 spindex 那张 case 表用例的行数
};

ComputedCounts computeRegistryCounts(const std::vector<std::string>& lines) {
    ComputedCounts c;
    for (const auto& line : lines) {
        if (line.find(",GOLD") != std::string::npos) ++c.goldRowsAllLines;
        if (line.empty() || line[0] != '#') ++c.nonCommentLines;  // 空行算进去：这一条就是命令的口径
        if (line.empty() || line[0] == '#') continue;
        ++c.dataRows;
        if (line.find(",GOLD") != std::string::npos) ++c.goldRows;
        if (line.find(",PENDING") != std::string::npos) ++c.pendingRows;
        if (line.find("SpindexOtherIndicesExecution") != std::string::npos) ++c.goldRowsTableDriven;
    }
    return c;
}

// PENDING 的预算上限。
// WHY 做成符号而不是在 EXPECT_LE 里写一遍、在注释里再抄一遍：这个上限的本义就是"登记表里
// PENDING 的真实数量"，字面量抄两处就会出现"改了表、忘了改上限"这种没人红的漂移。
// CoverageMeta.RegistryHeaderCountsMatchData 把它与登记表头部的 COUNT pending_rows 对撞，
// 于是"表 / 上限 / 头部"三者要么一起改，要么当场红。
constexpr int kPendingCeiling = 50;

// ---------------------------------------------------------------- 词法（R-B11a 收口后全仓只有这一份）
//
// 一趟扫描产出**两份坐标逐字节对齐**的视图，对齐是靠"只删注释字符、字面量一律原地改写而不是删掉"
// 保证的：code[i] 与 blanked[i] 来自原文同一个下标（见 origin），两份视图等长，
// 所以在其中一份里算出来的下标可以直接用到另一份上。
//
//   code     —— 剥掉 // 与 /* */ 注释，字符串/字符/原始字符串字面量的内容**原样保留**。
//               归属判据（F-b）只能读这份：`params["action"]` 的键与值整个活在字面量里。
//   blanked  —— 同上，再把每种字面量的**内容**抹成空格（换行留着）。
//               引号只有**开引号**留着：下面处理字符串/字符字面量的那一段进字面量后先原样抄走
//               开引号，其后每个字符一律走 blankOf（非换行抹成空格），撞上闭引号时它已经先被抹成
//               空格才跳出循环 ⇒ 闭引号位置是一个空格。原始字符串是另一条分支，两端定界符都留着。
//               括号配对只能读这份：字面量里的 `{` `}`（JSON 夹具、raw string）不再需要
//               块扫描器自己认字面量，也就不存在"第二套词法"。
//
// 两份视图都**保留换行**：行号感一丢，GTestBlock 的边界与失败消息里的行号就集体漂
// （CoverageMetaJudge.CommentLexingPreservesLineCountAndViewAlignment 钉这条，真文件上也钉）。
struct CppViews {
    std::string code;
    std::string blanked;
    std::vector<std::size_t> origin;  // origin[i] = 这个字符在原文里的下标（code 与 blanked 共用）
};

// 定义在本节之后（stripCppComments 那段注释解释了它为什么必须自己走状态机）。
CppViews lexCppViews(const std::string& text);

// ---------------------------------------------------------------- 源码用例块

struct GTestBlock {
    std::string suite;
    std::string name;
    std::string body;  // 外层大括号内的**原文**（判据自己负责剥注释，见 blockHasNumericAssertion）
    std::string id() const { return suite + "." + name; }
};

// 从 from 起找到第一个 '{'，做括号配对并返回闭括号后的位置；找不到返回 npos。
// ⚠ 输入必须是 lexCppViews 的 blanked 视图（或同等"注释与字面量内容都已消失"的文本）：
//   这个函数**只数大括号**，没有注释状态、没有字面量状态，因此它不可能和剥离器对
//   "注释到哪里结束"给出两种说法 —— 这正是 R-B11a 要收的口：旧实现自带一套只认 '\n'、
//   不认"行尾反斜杠 + 换行"拼接的行注释词法，于是被续行注释吞掉的那个 `{` 会多算一次 depth，
//   块边界向右漂到邻居甚至 namespace 的收尾 `}`，把邻居的 EXPECT_NEAR 吞进本块 ⇒ **假绿**
//   （复现夹具见 CoverageMetaJudge.SplicedLineCommentCannotDriftBlockBraceBoundary）。
//   传原文进来的话它会把 JSON 夹具里的 `{` 当代码括号，所以调用点只有一处、且传的是 blanked。
std::size_t scanBracedBlock(const std::string& structural, std::size_t from, std::size_t& bodyBegin) {
    const std::size_t open = structural.find('{', from);
    if (open == std::string::npos) {
        return std::string::npos;
    }
    bodyBegin = open + 1;
    std::size_t depth = 0;
    for (std::size_t p = open; p < structural.size(); ++p) {
        const char c = structural[p];
        if (c == '{') {
            ++depth;
            continue;
        }
        if (c == '}') {
            --depth;
            if (depth == 0) {
                return p + 1;
            }
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
// 行的遍历与括号配对都读 lexCppViews 的 blanked 视图（R-B11a：全仓只有剥离器那一份词法），
// 块体仍然回原文取 —— 判据自己负责剥注释（这样 CoverageMetaJudge 才能拿带注释的原文喂它）。
// 顺带修掉的两件事：① 注释里的 `TEST(` 不再被当成用例（旧实现扫原文）；
// ② 字面量里的 `TEST(` 也不再被当成用例（blanked 里那份内容已经没了）。
std::vector<GTestBlock> collectGTestBlocks(const std::string& text) {
    std::vector<GTestBlock> blocks;
    const CppViews views = lexCppViews(text);
    std::size_t lineStart = 0;
    while (lineStart < views.blanked.size()) {
        std::size_t lineEnd = views.blanked.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = views.blanked.size();
        }
        const std::string line = views.blanked.substr(lineStart, lineEnd - lineStart);
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
            const std::size_t after = scanBracedBlock(views.blanked, lineStart, bodyBegin);
            if (after != std::string::npos && !block.name.empty()) {
                // blanked 与原文的坐标用 origin 换算：bodyBegin 是 '{' 后第一个字符，
                // after - 1 是配对的 '}'，两者都落在保留下来的字符上，所以一定有 origin。
                const std::size_t rawBegin = views.origin[bodyBegin];
                const std::size_t rawClose = views.origin[after - 1];
                block.body = text.substr(rawBegin, rawClose - rawBegin);
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
// 去掉 // 行注释与 /* */ 块注释，但**原样保留字符串/字符/原始字符串字面量的内容**（= code 视图），
// 同一趟扫描再产出把字面量内容抹成空格的 blanked 视图（两份的用途与"坐标对齐"见上面 CppViews）。
// 换行留着：失败消息报的是"第几行的什么"，行号感一丢就没法对着原文核。
//
// WHY 保留字符串内容（本任务指定的判别方向）：F-b 的归属形状 `params["action"] = std::string("x")`
// 的键与值整个活在字符串里，把字符串内容一起删掉等于把归属判据的依据删掉；另一方面字面量里的
// `//`（JSON 夹具、URL、路径）不能被当成注释起点，所以这个函数必须自己走状态机，
// 不能拿 `find("//")` 一把梭。
//
// WHY 它是全仓唯一的词法（R-B11a）：块扫描器 scanBracedBlock 现在吃 blanked 视图，
// 自己不再判断"注释到哪里结束"。它过去自带一套只认 '\n'、不认"行尾反斜杠 + 换行"拼接的行注释
// 词法，于是同一份原文有两个答案，实测能把 GOLD 判据从红翻成绿（夹具见
// CoverageMetaJudge.SplicedLineCommentCannotDriftBlockBraceBoundary）。
CppViews lexCppViews(const std::string& text) {
    CppViews v;
    v.code.reserve(text.size());
    v.blanked.reserve(text.size());
    v.origin.reserve(text.size());
    // 字面量内部的字符在 blanked 里只留换行，其余抹成空格 ⇒ 两份等长、下标可以互换着用。
    auto blankOf = [](char ch) { return ch == '\n' ? '\n' : ' '; };
    auto putChar = [&v](char codeCh, char blankCh, std::size_t at) {
        v.code.push_back(codeCh);
        v.blanked.push_back(blankCh);
        v.origin.push_back(at);
    };
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = text[i];
        const char next = (i + 1 < n) ? text[i + 1] : '\0';
        if (c == '/' && next == '/') {
            // WHY 不能停在第一个换行：C++ 翻译阶段 3 先把 `\`+换行 拼接掉，行注释才开始找换行，
            // 所以 `// 这一版没有数值断言 \` 连它的下一行在编译器眼里是**同一条注释**。
            // 只走到 '\n' 就会把下一行那句 EXPECT_NEAR 留在"代码"里 ⇒ N90 堵掉的
            // "留一行注释顶替被删掉的真断言"从这种拼写重新打开
            // （CoverageMetaJudge.LineCommentWithBackslashContinuationIsNotCounted 钉这一条）。
            // 换行仍然原样留下：判据只读剥完的文本，但失败消息报的是"第几行的什么"，行号感不能丢。
            const std::size_t commentStart = i;
            while (i < n) {
                if (text[i] != '\n') {
                    ++i;
                    continue;
                }
                std::size_t prev = i;
                if (prev > 0 && text[prev - 1] == '\r') --prev;  // CRLF 原文：'\r' 不算注释行内容
                const bool splices = prev > commentStart && text[prev - 1] == '\\';
                putChar('\n', '\n', i);
                ++i;
                if (!splices) break;
            }
            continue;
        }
        if (c == '/' && next == '*') {
            i += 2;
            while (i < n) {
                if (text[i] == '*' && (i + 1 < n) && text[i + 1] == '/') {
                    i += 2;
                    break;
                }
                if (text[i] == '\n') putChar('\n', '\n', i);
                ++i;
            }
            continue;
        }
        if (c == 'R' && next == '"') {
            // 原始字符串 R"delim( ... )delim"：code 里整段按原文抄走；blanked 里只留
            // `R"delim(` 与 `)delim"` 两端，中间的内容抹掉 ⇒ 内容里的 { } 不参与块配对，
            // 也不必让块扫描器再认一遍 raw string（旧实现完全不认，靠引号奇偶巧合）。
            const std::size_t open = text.find('(', i + 2);
            const std::size_t close = open == std::string::npos ? std::string::npos : text.find(')', open);
            if (open != std::string::npos && close != std::string::npos) {
                const std::string delim = text.substr(i + 2, open - (i + 2));
                const std::string terminator = ")" + delim + "\"";
                const std::size_t end = text.find(terminator, close);
                if (end != std::string::npos) {
                    const std::size_t stop = end + terminator.size();
                    for (std::size_t p = i; p <= open; ++p) putChar(text[p], text[p], p);
                    for (std::size_t p = open + 1; p < end; ++p) putChar(text[p], blankOf(text[p]), p);
                    for (std::size_t p = end; p < stop; ++p) putChar(text[p], text[p], p);
                    i = stop;
                    continue;
                }
            }
        }
        if (c == '"' || c == '\'') {
            const char quote = c;
            putChar(c, c, i);
            ++i;
            while (i < n) {
                if (text[i] == '\\' && i + 1 < n) {
                    putChar(text[i], blankOf(text[i]), i);
                    putChar(text[i + 1], blankOf(text[i + 1]), i + 1);
                    i += 2;
                    continue;
                }
                putChar(text[i], blankOf(text[i]), i);
                if (text[i] == quote) {
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }
        putChar(c, c, i);
        ++i;
    }
    return v;
}

// 薄封装：只要 code 视图的那几个调用点（判据单测与 ② 之前的谓词）继续用它，
// 别再往仓库里加第三种"我自己剥一遍注释"的实现。
std::string stripCppComments(const std::string& text) {
    return lexCppViews(text).code;
}

// 代码（剥掉注释后的原文）里是否还有"把数值钉住"的断言。N90 之后的判据：注释里写的不算。
// ⚠ 参数应当是 lexCppViews 的 **blanked** 视图（字面量内容已抹掉）而不是 code 视图：
//   这个名字表是三个裸标识符的子串匹配，喂 code 视图的话
//   `qInfo() << "EXPECT_NEAR 钉住的是像素值";` 这种**只写在字符串里**的文案就能顶替真断言
//   （钉这条的用例：CoverageMetaJudge.AssertionNameOnlyInsideStringLiteralIsNotNumericEvidence）。
bool codeHasNumericAssertion(const std::string& code) {
    for (const auto& assertion : kNumericAssertions) {
        if (code.find(assertion) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool blockHasNumericAssertion(const GTestBlock& block) {
    // ②（原 B8 复审第 2 条）：F-a 与 F-b 不再各读各的视图，而是共用 lexCppViews 一趟扫描的产物 ——
    // 归属读 code（字面量内容必须留着，否则 `params["action"]` 的形状就没依据了），
    // 数值断言读 blanked（字面量内容里的断言名不算断言）。
    // 复审已经否掉过"两个视图不能同时供两者用"那句假两难：能不能，取决于视图是不是同一趟扫描出来的，
    // 现在它们是同一次 lex 的两个字段，所以不存在"第二套词法"。
    return codeHasNumericAssertion(lexCppViews(block.body).blanked);
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
// ③：把那个**标识符的名字**带出来 —— 只判"等号后面是 `某东西.action`"是不够的，
// 那个东西可能是函数参数、成员、外层作用域的变量，跟本块里那张 case 表毫无关系。
bool actionSlotHoldsLoopMemberIdent(const std::string& code, std::string& ident) {
    return anyActionSlot(code, [&ident](const std::string& text, std::size_t pos) {
        std::size_t p = matchAt(text, skipSpaces(text, pos), "=");
        if (p == std::string::npos) return std::string::npos;
        p = skipSpaces(text, p);
        std::size_t nameBegin = p;
        std::size_t nameEnd = p;
        while (nameEnd < text.size() && isIdentChar(text[nameEnd])) ++nameEnd;
        if (nameEnd == p) return std::string::npos;  // 等号后面连标识符都没有
        p = matchAt(text, nameEnd, ".action");
        if (p == std::string::npos) return std::string::npos;
        if (p < text.size() && isIdentChar(text[p])) return std::string::npos;  // .actionX 不算
        ident = text.substr(nameBegin, nameEnd - nameBegin);
        return p;
    }) != std::string::npos;
}

bool actionSlotHoldsLoopMember(const std::string& code) {
    std::string ignored;
    return actionSlotHoldsLoopMemberIdent(code, ignored);
}

// ident 是不是**在这个块体内**以 `for (… <ident> :` 的形式声明的循环变量（③ 加强的那一条）。
// 只认 range-for 的绑定名：从 `for` 后面那对括号里取头部，找深度 0 的第一个 ':'，
// 冒号前（跳过空白后）必须以 ident 结尾，且 ident 前面不能还是标识符字符（`xitem` 不算 `item`）。
// WHY 不判"容器名对不对"：那是更强的口径（要求 `{"<action>"` 就在这条 for 遍历的那张表里），
// 但范围书 ③ 指定的是"同一块体内声明的循环变量"这一条；剩下那半截松弛写在报告里登记成缺口，
// 不在这里假装已经关掉。
bool declaresRangeForBinding(const std::string& code, const std::string& ident) {
    if (ident.empty()) return false;
    std::size_t at = code.find("for");
    while (at != std::string::npos) {
        const bool leftOk = (at == 0) || !isIdentChar(code[at - 1]);
        const bool rightOk = (at + 3 >= code.size()) || !isIdentChar(code[at + 3]);
        std::size_t p = leftOk && rightOk ? skipSpaces(code, at + 3) : code.size();
        if (p < code.size() && code[p] == '(') {
            const std::size_t headerBegin = p + 1;
            int nest = 1;
            std::size_t q = headerBegin;
            std::size_t headerEnd = std::string::npos;
            for (; q < code.size(); ++q) {
                if (code[q] == '(') ++nest;
                else if (code[q] == ')' && --nest == 0) { headerEnd = q; break; }
            }
            if (headerEnd != std::string::npos) {
                // 头部的 ':'（跳过嵌套的 []/{} 里可能出现的，比如默认参数里的初始化列表）
                int bracket = 0, brace = 0;
                for (std::size_t r = headerBegin; r < headerEnd; ++r) {
                    if (code[r] == '[') ++bracket;
                    else if (code[r] == ']') --bracket;
                    else if (code[r] == '{') ++brace;
                    else if (code[r] == '}') --brace;
                    else if (code[r] == ':' && bracket == 0 && brace == 0) {
                        std::size_t end = r;
                        while (end > headerBegin && isSpaceAt(code, end - 1)) --end;
                        if (end >= headerBegin + ident.size() &&
                            code.compare(end - ident.size(), ident.size(), ident) == 0) {
                            const std::size_t before = end - ident.size();
                            if (before == headerBegin || !isIdentChar(code[before - 1])) return true;
                        }
                        break;  // 只看这一处的 ':'：一个 for 头部只有一个 range 冒号
                    }
                }
            }
        }
        at = code.find("for", at + 3);
    }
    return false;
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

// 一个块体是否属于这一行的 plugin.action。
// ⚠ 输入必须是 lexCppViews 的 **code** 视图（剥注释、**保留**字面量内容）：
//   归属形状 `params["action"] = std::string("threshold")` 的键与值整个活在字面量里，
//   喂 blanked 就等于把 F-a 的依据删掉（② 的"两个视图"就是这么分的，不是假两难）。
//   残留松弛（如实登记，见报告 §5）：raw string 的内容也在 code 视图里，
//   把 `params["action"] = std::string("x")` 整条写进 R"(...)" 里仍会被当成形状 —— 今天 0 实例，
//   归 B20 挂账；这一条不是 blanked 能顺手解决的（blanked 会连真归属形状一起抹掉）。
bool blockAttributesAction(const std::string& code, const std::string& plugin, const std::string& action) {
    if (code.find('"' + plugin + '"') == std::string::npos) return false;
    if (actionSlotHoldsLiteral(code, action)) return true;
    // 表驱动形态要三条同时成立：动作槽由某个循环变量的成员喂，那个变量是**本块里 `for (… x :` 声明的**，
    // 且该 action 的字面量确实在这个块里作为 case 表项的首元素出现。
    // WHY 三条：只查成员访问，任意带 `.action` 的块都能蒙过去；只查 `{"x"`，同一份字面量出现在
    // 文案表里也能蒙过去；不查"这个变量是本块的循环变量"，那么动作槽可以接**块外**那个循环的变量
    // （③ 的残留松弛：本块的表 + 别的地方的循环 = 归属照样成立，见
    //  CoverageMetaJudge.TableDrivenLoopBindingIsAttributed 的第 4 个反例）。
    // 今天靠这条形态成立的是 spindex 那张 15 行的 case 表（`for (const auto& testCase : cases)`）。
    std::string loopIdent;
    return actionSlotHoldsLoopMemberIdent(code, loopIdent) &&
        declaresRangeForBinding(code, loopIdent) &&
        caseTableEntryForAction(code, action);
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
    // 数值断言这一条走 wrapper `blockHasNumericAssertion` 而不是就地再剥一次：
    // WHY：那条 wrapper 被 CoverageMetaJudge 的三条单测钉着，审计原先抄的是第二条路径
    // （自己调 codeHasNumericAssertion），于是"把 wrapper 里的 stripCppComments 摘掉"只会红掉
    // 单测、动不到真判据，而"把上面那行 strip 改回原文"又只会红掉审计 —— 两条路各自半瞎。
    // 现在只剩一条路，且 CoverageMetaJudge.AuditStripsCommentsOnBothCriteriaEndToEnd
    // 与 .AuditStripsBackslashContinuedLineCommentEndToEnd 从链子末端（合成源文件 →
    // collectGTestBlocks → 本函数）把这条路的剥注释能力钉死。
    if (!blockHasNumericAssertion(*block)) {
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
        if (name == "lifecycle_test") continue;  // 夹具插件 name() 的真值，见 tests/plugins/test_lifecycle_plugin.cpp
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
    // 上限 = 登记表里 PENDING 的真实数量，不是估算值（常量定义见 kPendingCeiling）。
    // 现读口径全部写在 tests/action_gold_cases.txt 头部的 COUNT 行里，数字只在那里出现一次：
    // CoverageMeta.RegistryHeaderCountsMatchData 同时核对"头部 COUNT == 数据行真值"和
    // "头部 COUNT pending_rows == 本用例的上限"，所以 B11 晋升后改表、改上限、改头部必须一起改齐。
    EXPECT_LE(pending, kPendingCeiling)
        << "PENDING 数量不得增加；若某 action 已补数值测试，请把登记表改为 GOLD 并同步下调 kPendingCeiling"
        << " 与登记表头部的 COUNT pending_rows（现值真值见 CoverageMeta.RegistryHeaderCountsMatchData 的失败消息）";
}

// 头部注释里的那几个数必须等于数据行算出来的真值。
// WHY 需要这条用例：`EXPECT_LE(pending, 74)` 只朝一个方向卡（多了会红，**少了不会红**），
// 而头部注释是这张表唯一的"人读的规模感"。B11 会晋升行，届时忘改头部就是注释说谎，
// 全表照样绿 —— 这条用例把注释与数据的对撞变成机器判据。
// 现算口径全部来自文件自身：注释行的 COUNT 键值 vs 数据行（先排注释行）的 grep 等价计数。
TEST(CoverageMeta, RegistryHeaderCountsMatchData) {
    const std::string text = readFileOrEmpty(kRegistryPath);
    ASSERT_FALSE(text.empty()) << "缺少 " << kRegistryPath;
    const auto lines = splitLines(text);

    const ComputedCounts truth = computeRegistryCounts(lines);
    // 恒等式先钉住"真值这半边的形状"：行数若不等于两种状态之和，说明有行的状态写歪了，
    // 那么下面任何"注释 vs 数据"的对撞都失去了意义。
    EXPECT_EQ(truth.dataRows, truth.goldRows + truth.pendingRows)
        << "数据行数 != GOLD 行数 + PENDING 行数：登记表里有既不算 GOLD 也不算 PENDING 的行";
    // ⑤-b：机器口径跳空行、头部**旧**现读命令（`wc -l`）不跳。今天二者重合，所以"插一个空行"
    // 这件事判据抓不到（复审 §2 行 9 实测：16/16 全绿而命令给 127）。这条臂把两个口径钉成同一个数，
    // 于是空行要么被删掉、要么头部命令必须一起改 —— 注释里的命令不许变成假账。
    EXPECT_EQ(truth.nonCommentLines, truth.dataRows)
        << "数据区里有 " << truth.nonCommentLines - truth.dataRows
        << " 个空行：`grep -v '^#'` 的口径与 computeRegistryCounts 的口径已经分叉"
        << "（头部声明的 data_rows 只有后者会认，命令会多数这么多行）";

    // 与全部其它审计用例同一个解析器：grep 口径与 readRegistry 口径必须数出同一个集合，
    // 否则"头部注释 == 真值"里的真值本身是两套说法。
    const auto registry = readRegistry();
    ASSERT_TRUE(registry.malformed.empty()) << "登记表存在无法识别的行:" << describeMalformed(registry);
    EXPECT_EQ(truth.dataRows, static_cast<int>(registry.rows.size()));
    int goldByStatus = 0, pendingByStatus = 0;
    for (const auto& row : registry.rows) {
        if (row.status == "GOLD") ++goldByStatus;
        if (row.status == "PENDING") ++pendingByStatus;
    }
    EXPECT_EQ(truth.goldRows, goldByStatus) << ",GOLD 的行数与状态列解析出的 GOLD 行数不一致";
    EXPECT_EQ(truth.pendingRows, pendingByStatus) << ",PENDING 同理";

    const std::map<std::string, int> computed = {
        {"data_rows", truth.dataRows},
        {"gold_rows", truth.goldRows},
        {"pending_rows", truth.pendingRows},
        {"gold_rows_all_lines", truth.goldRowsAllLines},
        {"gold_rows_table_driven", truth.goldRowsTableDriven},
        {"gold_rows_literal_slot", truth.goldRows - truth.goldRowsTableDriven},
    };

    const DeclaredCounts declared = readDeclaredCounts(lines);
    std::map<std::string, int> declaredByKey;
    for (const auto& entry : declared.entries) {
        ASSERT_TRUE(declaredByKey.insert({entry.key, entry.value}).second)
            << "第 " + std::to_string(entry.lineNumber) + " 行把同一个计数键写了两遍，后一个会被前一个掩盖";
    }
    // ⑤-a：反向核对（"头部有键但 computed 里没有 ⇒ 点名没人核对它"）**必须放在条数闸之前**。
    // 放在后面它就是一条结构上恒真的装饰：要走到那里必须先过 `entries.size() == computed.size()`、
    // 无重复键、以及"6 个 computed 键全在头部"三道闸 ⇒ 两边的键集已经相等 ⇒ 这条永不成立。
    // 复审 §2 行 14 实测过：注入第 7 条 COUNT bogus_key 时红的是条数闸，这条一次都没出声。
    // 挪到前面之后，"多一个键"这件事由**它**点名（说得出是哪个键、第几行），条数闸退化成兜底。
    for (const auto& entry : declared.entries) {
        EXPECT_TRUE(computed.count(entry.key) != 0)
            << "第 " << entry.lineNumber << " 行的计数键 " << entry.key
            << " 没有任何判据会去核对它（改名或新增都要同时给 computed 加一条）";
    }
    // 对照物本身要先存在：头部被删干净时，下面逐键的对撞会退化成"零次比较"的空检查。
    ASSERT_EQ(declared.entries.size(), computed.size())
        << "登记表头部的计数行应当恰有 " << computed.size() << " 条（键见下面 computed），"
        << "实际 " << declared.entries.size() << " 条；解析到 " << declared.linesMentioningCount
        << " 行提到 COUNT —— 提到了却没解析成功的那几行是坏形状";
    EXPECT_EQ(declared.linesMentioningCount, declared.entries.size())
        << "有注释行写了 COUNT 但不是 `COUNT <键> = <数字>` 的形状：那条数字不会被任何判据核对";

    for (const auto& expect : computed) {
        const auto it = declaredByKey.find(expect.first);
        ASSERT_NE(it, declaredByKey.end()) << "登记表头部没有名为 " << expect.first << " 的计数行";
        EXPECT_EQ(it->second, expect.second)
            << "登记表头部写的 COUNT " << expect.first << " = " << it->second
            << "，与数据行算出来的 " << expect.second << " 不符（口径见该行行尾的现读命令）";
    }

    // 上限与头部必须是同一个数：`EXPECT_LE(pending, kPendingCeiling)` 只朝"别涨"那半边卡，
    // 表降了而上限忘降 ⇒ 预算被静默放宽，那是唯一还没被任何判据覆盖的漂移方向。
    const auto ceiling = declaredByKey.find("pending_rows");
    ASSERT_NE(ceiling, declaredByKey.end());
    EXPECT_EQ(ceiling->second, kPendingCeiling)
        << "登记表头部的 COUNT pending_rows(" << ceiling->second << ") 与 "
        << "CoverageMeta.PendingCountDoesNotGrow 的上限(" << kPendingCeiling << ") 不是同一个数";
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
        if (name == "lifecycle_test") continue;  // 夹具插件 name() 的真值，见 tests/plugins/test_lifecycle_plugin.cpp
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

// 续行拼写（F-a 的另一扇门）：C++ 翻译阶段 3 先做 `\`+换行 的拼接，**然后**才让行注释在换行处
// 结束，于是 `// 这一版没有数值断言 \` 加它的下一行在编译器眼里是**同一条注释**。
// 剥离器要是只走到第一个 '\n' 就停，那句 EXPECT_NEAR 就被留在"代码"里，留一行注释就能顶替
// 被删掉的真断言（N90）于是从这种拼写重新打开。
// 现役可达性：`grep -cE '\\$' tests/test_plugins.cpp` = 0（现读：该文件今天没有任何行尾反斜杠），
// 所以这条用例钉的是"一行可伪造"这扇门，不是已有的谎报。
TEST(CoverageMetaJudge, LineCommentWithBackslashContinuationIsNotCounted) {
    const std::string continued =
        "\n    const auto result = p->execute(params, progress_);\n"
        "    // 这一版没有数值断言 \\\n"
        "    EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.5f, 1e-4f);\n";
    EXPECT_FALSE(blockHasNumericAssertion(syntheticBlock(continued)))
        << "行尾反斜杠把注释续到了下一行：那句 EXPECT_NEAR 不是代码";

    // 控制组：去掉那个行尾反斜杠，同一具块体就是"注释 + 真断言"，必须仍然判 true。
    // WHY 非要有这条：少了它，"多吞一行"这种过度剥离也能让上面那条绿着过去。
    const std::string notContinued =
        "\n    const auto result = p->execute(params, progress_);\n"
        "    // 这一版没有数值断言\n"
        "    EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.5f, 1e-4f);\n";
    EXPECT_TRUE(blockHasNumericAssertion(syntheticBlock(notContinued)))
        << "没有行尾反斜杠时不许多吞一行代码";

    // 行号感要保住：续行时换行是留在输出里的（失败消息报的是"第几行的什么"）。
    auto countNewlines = [](const std::string& s) {
        int n = 0;
        for (const char c : s) {
            if (c == '\n') ++n;
        }
        return n;
    };
    EXPECT_EQ(countNewlines(stripCppComments(continued)), countNewlines(continued))
        << "剥注释不许改变行数，否则行号对不上原文";

    // 同一扇门对**归属**判据同样成立：动作槽形状写在续行注释里 ⇒ 剥完不该还剩下来。
    const std::string attribution = stripCppComments(
        "    auto* p = mgr_.find(\"processing\");\n"
        "    // 动作槽那行早废了 \\\n"
        "    params[\"action\"] = std::string(\"threshold\");\n");
    EXPECT_FALSE(actionSlotHoldsLiteral(attribution, "threshold"))
        << "续行注释里的动作槽形状也是注释";
    EXPECT_TRUE(blockAttributesAction(
        stripCppComments(
            "    auto* p = mgr_.find(\"processing\");\n"
            "    params[\"action\"] = std::string(\"threshold\");  // 阈值钉在 0.5 \\\n"
            "    EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.5f, 1e-4f);\n"),
        "processing", "threshold"))
        << "正对照：真代码里的动作槽不许被续行分支吞掉";
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

// ②（原 B8 复审第 2 条）：断言名**只**活在字符串字面量里 ⇒ 数值断言判据必须判 false。
// F-b 的名字表是三个裸标识符的子串匹配，喂"剥注释但保留字面量内容"那份视图的话，
// 一行 qInfo("本来该有 EXPECT_NEAR …") 的文案就能顶替被删掉的真断言（复审 §3 的 X6 实测过这个口径：
// 旧形状判 true）。现在 F-b 读 blanked（字面量内容抹成空格），F-a 仍读 code（归属的依据在字面量里）。
TEST(CoverageMetaJudge, AssertionNameOnlyInsideStringLiteralIsNotNumericEvidence) {
    // 负例：整块里除了字符串文案，没有任何真数值断言。
    const GTestBlock onlyString = syntheticBlock(
        "\n    auto* p = mgr_.find(\"processing\");\n"
        "    qInfo(\"这里本来该有 EXPECT_NEAR 钉住像素值，暂时只判成功\");\n"
        "    EXPECT_TRUE(result.success);\n");
    EXPECT_FALSE(blockHasNumericAssertion(onlyString))
        << "字符串里的 EXPECT_NEAR 只是文案，不是把数值钉住的凭据";

    // 控制组：同一串名字写进真代码 ⇒ 必须判 true（少了这条，"把字面量整段删掉"这种过度剥离也能绿）。
    const GTestBlock realCode = syntheticBlock(
        "\n    auto* p = mgr_.find(\"processing\");\n"
        "    EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.5f, 1e-4f);\n"
        "    EXPECT_TRUE(result.success);\n");
    EXPECT_TRUE(blockHasNumericAssertion(realCode))
        << "真代码里的断言不许被 blanked 视图抹掉";

    // 端到端走审计链：这条合成 GOLD 的归属是齐的（归属读 code，字面量内容必须在），
    // 缺的只有数值断言 ⇒ 报的必须是"数值断言"这一条，不是"不属于这一行"。
    const std::string source =
        "TEST(SyntheticGold, AssertionInStringOnly) {\n"
        "    auto* p = mgr_.find(\"processing\");\n"
        "    params[\"action\"] = std::string(\"threshold\");\n"
        "    qInfo(\"本来该有 EXPECT_NEAR 钉像素值\");\n"
        "}\n";
    const auto blocks = collectGTestBlocks(source);
    ASSERT_EQ(blocks.size(), 1u) << "合成源文件解析不出块 ⇒ 本用例是空检查";
    const std::string problem = goldEvidenceProblem(
        syntheticRow("processing", "threshold", "SyntheticGold.AssertionInStringOnly"), blocks);
    EXPECT_NE(problem.find("数值断言"), std::string::npos)
        << "审计必须报这一条（F-b 读 blanked）:" << problem;
    EXPECT_EQ(problem.find("不属于这一行"), std::string::npos)
        << "F-a 仍然读 code：动作槽形状不该被一起抹掉（过度剥离）:" << problem;
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

    // 反例 4（③ 加强前蒙得过的那一型，"两个分子来自不同的表"）：本块的 case 表 tableX 里确实有
    // {"ndbi"，动作槽也确实是 `某东西.action` —— 但那个"某东西"是 **c**，而本块的 range-for
    // 声明的循环变量叫 item（遍历的是 tableY）。c 来自块外（成员/参数/外层作用域），
    // 于是字面量与循环变量根本不在同一张表上，归属不该成立。
    const std::string mixedTables = stripCppComments(
        "    auto* p = mgr_.find(\"spindex\");\n"
        "    const std::vector<CaseSpec> tableX = {\n"
        "        {\"ndbi\", \"e2e_spindex_ndbi_output.tif\", {{\"swir1_band\", 5}}, 0.125f},\n"
        "    };\n"
        "    for (const auto& item : tableY) {\n"
        "        params[\"action\"] = c.action;\n"
        "        EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.125f, 1e-4f);\n"
        "    }\n");
    EXPECT_FALSE(blockAttributesAction(mixedTables, "spindex", "ndbi"))
        << "动作槽那个变量必须由本块里的 `for (… 它 :` 声明，否则两个分子可以来自不同的表";

    // 上面那条红不能是"整段读不出形状"造成的 ⇒ 同一具夹具只把 c 换成本块声明的 item，归属就必须成立。
    // ⚠ 这条 EXPECT_TRUE 钉的是**当前的松**（范围书 ③ 指定的加强只到"ident 在块内被 for 声明"这一层，
    //   没有收紧到"字面量必须就在这条 for 遍历的那张表里"）：item 遍历的是 tableY，字面量却在 tableX。
    //   将来有人把它收紧到容器级，必须顺手把这条改成 EXPECT_FALSE —— 报告 §5 已登记。
    const std::string slotUsesBlockDeclaredVar = stripCppComments(
        "    auto* p = mgr_.find(\"spindex\");\n"
        "    const std::vector<CaseSpec> tableX = {\n"
        "        {\"ndbi\", \"e2e_spindex_ndbi_output.tif\", {{\"swir1_band\", 5}}, 0.125f},\n"
        "    };\n"
        "    for (const auto& item : tableY) {\n"
        "        params[\"action\"] = item.action;\n"
        "        EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.125f, 1e-4f);\n"
        "    }\n");
    EXPECT_TRUE(blockAttributesAction(slotUsesBlockDeclaredVar, "spindex", "ndbi"))
        << "控制组：块内声明的循环变量喂的动作槽这一条分子是读得出的（反例 4 的红来自 declaresRangeForBinding）";
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

// 端到端钉住"审计这条链剥注释"（不是只钉 wrapper）：合成源文件 → collectGTestBlocks →
// goldEvidenceProblem，走的正是 CoverageMeta.GoldEvidenceIsAuditable 对每行 GOLD 用的那条调用链。
// WHY 要这条：wrapper 的三条单测只证明 wrapper 自己会剥；审计当初读的是 :509 那个临时量，
// 把那一行改回 `block->body` 原文，全量照样绿 ⇒ 判据形同虚设。
// 三条臂各自只留一个缺陷，并把"报的是哪一条判据"也钉住：
//   臂 1 只缺数值断言（形状写在注释里）⇒ 必须报"数值断言"，且不报归属；
//   臂 2 只缺归属（动作槽写在注释里）⇒ 必须报"不属于这一行"，且不报数值断言；
//   臂 3 两条都齐 ⇒ 必须无问题（少了这条，前两条会因为"审计什么都报 problem"而恒绿）。
TEST(CoverageMetaJudge, AuditStripsCommentsOnBothCriteriaEndToEnd) {
    const std::string commentOnlyAssertion =
        "TEST(SyntheticGold, CommentOnlyNumericAssertion) {\n"
        "    auto* p = mgr_.find(\"processing\");\n"
        "    params[\"action\"] = std::string(\"threshold\");\n"
        "    // EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.5f, 1e-4f);\n"
        "    EXPECT_TRUE(result.success);\n"
        "}\n";
    const auto commentOnlyBlocks = collectGTestBlocks(commentOnlyAssertion);
    ASSERT_EQ(commentOnlyBlocks.size(), 1u) << "合成源文件解析不出块 ⇒ 本用例是空检查";
    const std::string problemA = goldEvidenceProblem(
        syntheticRow("processing", "threshold", "SyntheticGold.CommentOnlyNumericAssertion"),
        commentOnlyBlocks);
    EXPECT_NE(problemA.find("数值断言"), std::string::npos)
        << "注释里的 EXPECT_NEAR 撑不起 GOLD，审计必须报这一条:" << problemA;
    EXPECT_EQ(problemA.find("不属于这一行"), std::string::npos) << problemA;

    const std::string commentedOutSlot =
        "TEST(SyntheticGold, CommentedOutActionSlot) {\n"
        "    auto* p = mgr_.find(\"processing\");\n"
        "    // params[\"action\"] = std::string(\"threshold\");\n"
        "    EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.5f, 1e-4f);\n"
        "}\n";
    const auto commentedSlotBlocks = collectGTestBlocks(commentedOutSlot);
    ASSERT_EQ(commentedSlotBlocks.size(), 1u);
    const std::string problemB = goldEvidenceProblem(
        syntheticRow("processing", "threshold", "SyntheticGold.CommentedOutActionSlot"),
        commentedSlotBlocks);
    EXPECT_NE(problemB.find("不属于这一行"), std::string::npos)
        << "动作槽形状只写在注释里 ⇒ 归属不成立，审计必须报这一条:" << problemB;
    EXPECT_EQ(problemB.find("数值断言"), std::string::npos)
        << "这条块体的数值断言是真的，不该被一起报出来（过度剥离）:" << problemB;

    const std::string honestEvidence =
        "TEST(SyntheticGold, HonestEvidence) {\n"
        "    auto* p = mgr_.find(\"processing\");\n"
        "    params[\"action\"] = std::string(\"threshold\");\n"
        "    EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.5f, 1e-4f);  // 阈值钉在 0.5\n"
        "}\n";
    const auto honestBlocks = collectGTestBlocks(honestEvidence);
    ASSERT_EQ(honestBlocks.size(), 1u);
    EXPECT_TRUE(goldEvidenceProblem(
        syntheticRow("processing", "threshold", "SyntheticGold.HonestEvidence"), honestBlocks).empty())
        << "正对照：两条判据都齐的块必须判成合格证据";
}

// 续行拼写的同一扇门，端到端走一遍审计链（臂 1 的注释带行尾反斜杠，把下一行的真断言一起吃掉）。
TEST(CoverageMetaJudge, AuditStripsBackslashContinuedLineCommentEndToEnd) {
    const std::string source =
        "TEST(SyntheticGold, BackslashContinuedComment) {\n"
        "    auto* p = mgr_.find(\"processing\");\n"
        "    params[\"action\"] = std::string(\"threshold\");\n"
        "    // 这一版没有数值断言 \\\n"
        "    EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.5f, 1e-4f);\n"
        "}\n";
    const auto blocks = collectGTestBlocks(source);
    ASSERT_EQ(blocks.size(), 1u) << "合成源文件解析不出块 ⇒ 本用例是空检查";
    const std::string problem = goldEvidenceProblem(
        syntheticRow("processing", "threshold", "SyntheticGold.BackslashContinuedComment"), blocks);
    EXPECT_NE(problem.find("数值断言"), std::string::npos)
        << "行尾反斜杠续行后，那句 EXPECT_NEAR 整条都在注释里 ⇒ 审计必须报 problem:" << problem;
}

// R-B11a 的假绿夹具（抄 task-B8-rereview.md §1 的 S1，合法 C++，`namespace {` 提供那个多余的 `}`）：
// victim 里唯一的"数值断言"整条活在**续行注释**里，而注释的下一行是一个真 `{`（编译器眼里它也是注释）。
// 旧 scanBracedBlock 自带一套只认 '\n' 的行注释词法 ⇒ 那个 `{` 被多算一次 depth ⇒ 块尾向右漂到
// namespace 的收尾 `}` ⇒ victim 把邻居的 EXPECT_NEAR 吞进来 ⇒ 审计判 victim **合格**（假绿），
// 同时邻居整个消失（复审实测：blocks=1、bodyLen=350、hasNestedTEST=1）。
    // ⚠ victim 只留一个"被注释吞掉的 {"、不给它配对的 }：那个补偿的 `}` 由 namespace 的收尾提供，
    //   所以整份夹具在编译器眼里是合法 C++（续行注释把 `    {` 那行一起吃掉了）。
    //   给 victim 补一个自己的 } 就会就地归零、块边界不漂 ⇒ 这条用例退化成恒绿装饰。
// 现在块扫描吃 lexCppViews 的 blanked 视图，注释与字面量里的括号在到达扫描器之前就已经没了。
TEST(CoverageMetaJudge, SplicedLineCommentCannotDriftBlockBraceBoundary) {
    const std::string source =
        "namespace {\n"
        "TEST(SyntheticGold, Victim) {\n"
        "    auto* p = mgr_.find(\"processing\");\n"
        "    params[\"action\"] = std::string(\"threshold\");\n"
        "    // 这一版没有数值断言 \\\n"
        "    {\n"
        "}\n"
        "TEST(SyntheticGold, Neighbor) {\n"
        "    auto* p = mgr_.find(\"processing\");\n"
        "    params[\"action\"] = std::string(\"threshold\");\n"
        "    EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.5f, 1e-4f);\n"
        "}\n"
        "}  // namespace\n";

    // 先钉住"块边界"本身：两个 TEST 必须各自成块（旧词法在这里就报 1 块）。
    const auto blocks = collectGTestBlocks(source);
    EXPECT_EQ(blocks.size(), 2u) << "被续行注释吞掉的 '{' 不该算进 depth：块边界会因此向右漂";
    ASSERT_FALSE(blocks.empty()) << "合成源文件解析不出块 ⇒ 本用例是空检查";

    // victim 的 EXPECT_NEAR 一条都没有 ⇒ 审计必须报"数值断言"（旧词法下这里是**空串**＝假绿）。
    const std::string victimProblem = goldEvidenceProblem(
        syntheticRow("processing", "threshold", "SyntheticGold.Victim"), blocks);
    EXPECT_NE(victimProblem.find("数值断言"), std::string::npos)
        << "victim 那句断言整个在续行注释里，邻居的不算它的证据 ⇒ 审计必须报 problem:" << victimProblem;

    // 邻居既不能被吞掉、也不能被误伤（正对照：改词法不许把好证据一起打掉）。
    EXPECT_TRUE(goldEvidenceProblem(
        syntheticRow("processing", "threshold", "SyntheticGold.Neighbor"), blocks).empty())
        << "邻居的块体被漂走的边界截掉了（过度剥离）";
}

// ④ 的硬约束：剥离器必须继续保留换行，否则块边界与失败消息里的行号集体漂。
// 只在合成串上钉过一次（LineCommentWithBackslashContinuationIsNotCounted），这里在**真文件**上钉，
// 并且顺带钉住两份视图"等长 + 逐字符同下标"这条对齐性（collectGTestBlocks 靠它把 blanked 的
// 下标用到原文上）。
TEST(CoverageMetaJudge, CommentLexingPreservesLineCountAndViewAlignment) {
    const std::string text = readFileOrEmpty(kPluginTestPath);
    ASSERT_FALSE(text.empty()) << "缺少 " << kPluginTestPath;
    const CppViews views = lexCppViews(text);

    auto countNewlines = [](const std::string& s) {
        std::size_t n = 0;
        for (const char c : s) {
            if (c == '\n') ++n;
        }
        return n;
    };
    EXPECT_EQ(countNewlines(views.code), countNewlines(text))
        << "剥注释不许改变行数，否则块边界与行号对不上原文";
    EXPECT_EQ(countNewlines(views.blanked), countNewlines(text)) << "blanked 同理";
    EXPECT_EQ(views.code.size(), views.blanked.size()) << "两份视图必须等长，下标才能互用";
    EXPECT_EQ(views.origin.size(), views.code.size());
    EXPECT_LT(views.code.size(), text.size()) << "真文件里有注释，剥完必须变短（否则本用例是空检查）";
    EXPECT_LT(views.blanked.size(), text.size());

    // ---- I-2 逐位对位（修复轮 1）BEGIN：名字里的 "ViewAlignment" 由这一段兑现 ----
    // 上面那四条只钉住"等长 + 行数相等 + 确实变短了"，**推不出对位**：把 origin 填成顺序递增、
    // 或整段 +1 平移，那四条照样绿（复审 I-2）。判据取**全量逐位**、不采样——"采样规则是否可复算"
    // 这条约束在这里直接让位于更强的口径：对 [0, code.size()) 的**每一个**下标 i 同时核三件事，
    //   ① code[i] == text[origin[i]]                        留下来的每个字符都确实取自原文那个下标
    //   ② blanked[i] == text[origin[i]] 或 blanked[i]==' '   抹除只许原地变空格，不许挪位
    //   ③ code[i]=='\n' ⇒ blanked[i]=='\n' 且原文 origin[i] 也是 '\n'   两份视图与原文的换行同位
    // ①②③ 合起来就是"origin 是视图下标到原文下标的真映射"，B11 要靠它把失败消息里的行号钉回原文。
    // 违例只记**第一处**下标、最后一次报错（68 万下标逐条 EXPECT 会把日志刷爆），所以取字符前先把
    // i==npos 与 origin 越界都挡住——越界时只登记下标不取字符，免得变异体把测试进程打崩。
    std::size_t scanned = 0;    // 真正核到的下标数
    std::size_t drifted = 0;    // origin[i] != i 的下标数：证明这份输入上映射不是恒等
    std::size_t newlines = 0;   // ③ 实际核到的换行数
    std::size_t wiped = 0;      // blanked[i] != text[origin[i]] 的下标数 = 被抹掉的位置
    std::size_t idxOob = std::string::npos;    // origin[i] 越出原文长度
    std::size_t idxCode = std::string::npos;   // 违反 ①
    std::size_t idxBlank = std::string::npos;  // 违反 ②
    std::size_t idxNl = std::string::npos;     // 违反 ③
    for (std::size_t i = 0; i < views.code.size(); ++i) {
        const std::size_t o = views.origin[i];
        if (o >= text.size()) {
            if (idxOob == std::string::npos) idxOob = i;
            continue;
        }
        ++scanned;
        if (o != i) ++drifted;
        const char t = text[o];
        const char c = views.code[i];
        const char b = views.blanked[i];
        if (b != t) ++wiped;
        if (c == '\n') {
            ++newlines;
            if ((b != '\n' || t != '\n') && idxNl == std::string::npos) idxNl = i;
        }
        if (c != t && idxCode == std::string::npos) idxCode = i;
        if (b != t && b != ' ' && idxBlank == std::string::npos) idxBlank = i;
    }
    // 只在违例时才取字符拼现场；i==npos（通过）时返回占位串，不会越界读。
    auto where = [&](std::size_t i) {
        if (i == std::string::npos) return std::string("(无违例)");
        const std::size_t o = views.origin[i];
        return std::string("i=") + std::to_string(i) + " origin=" + std::to_string(o) +
               " code='" + views.code[i] + "' blanked='" + views.blanked[i] + "'" +
               (o < text.size() ? std::string(" text='") + text[o] + "'"
                                : std::string(" text=<越界>"));
    };
    std::cout << "[I2-VIEW-ALIGNMENT] scanned=" << scanned << " drifted=" << drifted
              << " newlines=" << newlines << " wiped=" << wiped
              << " codeSize=" << views.code.size() << " textSize=" << text.size() << "\n";
    // 四条空检查闸：任何一条没跑到，上面三条判据就都是恒真的装饰。
    ASSERT_GT(scanned, 0u) << "逐位核一条都没跑到 ⇒ 本用例是空检查";
    ASSERT_GT(drifted, 0u) << "origin 在这份输入上处处等于下标 ⇒ ① 退化成恒等检查，平移类变异拦不住";
    ASSERT_GT(newlines, 0u) << "没核到任何换行 ⇒ ③ 是空检查";
    ASSERT_GT(wiped, 0u) << "没有任何位置被抹成空格 ⇒ ② 是空检查";
    EXPECT_EQ(idxOob, std::string::npos)
        << "有下标的 origin 越出了原文长度 ⇒ 视图坐标已与原文脱钩：" << where(idxOob);
    EXPECT_EQ(idxCode, std::string::npos)
        << "① code 视图与原文对不上 ⇒ 视图里算出的下标不能直接用到原文上，"
        << "块边界与失败消息里的行号都会错：" << where(idxCode);
    EXPECT_EQ(idxBlank, std::string::npos)
        << "② blanked 里出现了既非原文那个字符、又非空格的字符 ⇒ 抹除挪了位：" << where(idxBlank);
    EXPECT_EQ(idxNl, std::string::npos)
        << "③ 换行在 code / blanked / 原文三者之间不同位 ⇒ 行号会漂：" << where(idxNl);
    // ---- I-2 逐位对位（修复轮 1）END ----

    // blanked 必须真的把字面量内容抹掉了：字面量里的 EXPECT_NEAR 在 blanked 里不该还剩下来。
    const CppViews probe = lexCppViews(
        "    qDebug() << \"EXPECT_NEAR 只是文案\";  // 注释里的 EXPECT_NEAR\n"
        "    EXPECT_NEAR(readRasterPixel(output, 5, 5), 0.5f, 1e-4f);\n");
    EXPECT_NE(probe.code.find("EXPECT_NEAR 只是文案"), std::string::npos)
        << "code 视图要保留字面量内容（归属判据的依据）";
    EXPECT_EQ(probe.blanked.find("EXPECT_NEAR 只是文案"), std::string::npos)
        << "blanked 视图必须抹掉字面量内容";
    EXPECT_NE(probe.blanked.find("EXPECT_NEAR(readRasterPixel"), std::string::npos)
        << "真代码里的断言在 blanked 里必须留下来（控制组）";
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

