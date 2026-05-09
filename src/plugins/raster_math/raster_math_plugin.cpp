#include "raster_math_plugin.h"

#include <gis/core/error.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>

#include <gdal_priv.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <stack>
#include <string>
#include <vector>

namespace gis::plugins {

namespace {

static double evalExpression(const std::string& expr,
                             const std::map<std::string, double>& bandValues) {
    std::string e = expr;
    std::vector<std::string> keys;
    keys.reserve(bandValues.size());
    for (const auto& [key, val] : bandValues) {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end(),
              [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    for (const auto& key : keys) {
        const std::string replacement = std::to_string(bandValues.at(key));
        size_t pos = 0;
        while ((pos = e.find(key, pos)) != std::string::npos) {
            e.replace(pos, key.length(), replacement);
            pos += replacement.length();
        }
    }

    try {
        std::stack<double> vals;
        std::stack<char> ops;
        auto precedence = [](char op) -> int {
            if (op == '+' || op == '-') return 1;
            if (op == '*' || op == '/') return 2;
            return 0;
        };
        auto applyOp = [](double a, double b, char op) -> double {
            switch (op) {
                case '+': return a + b;
                case '-': return a - b;
                case '*': return a * b;
                case '/': return (std::abs(b) < 1e-15) ? 0.0 : a / b;
                default: return 0.0;
            }
        };

        size_t i = 0;
        while (i < e.size()) {
            if (std::isspace(static_cast<unsigned char>(e[i]))) {
                ++i;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(e[i])) || e[i] == '.') {
                const size_t start = i;
                while (i < e.size() &&
                       (std::isdigit(static_cast<unsigned char>(e[i])) || e[i] == '.')) {
                    ++i;
                }
                vals.push(std::stod(e.substr(start, i - start)));
            } else if (e[i] == '(') {
                ops.push('(');
                ++i;
            } else if (e[i] == ')') {
                while (!ops.empty() && ops.top() != '(') {
                    const double b = vals.top(); vals.pop();
                    const double a = vals.top(); vals.pop();
                    vals.push(applyOp(a, b, ops.top()));
                    ops.pop();
                }
                if (!ops.empty()) {
                    ops.pop();
                }
                ++i;
            } else if (e[i] == '+' || e[i] == '-' || e[i] == '*' || e[i] == '/') {
                while (!ops.empty() && precedence(ops.top()) >= precedence(e[i])) {
                    const double b = vals.top(); vals.pop();
                    const double a = vals.top(); vals.pop();
                    vals.push(applyOp(a, b, ops.top()));
                    ops.pop();
                }
                ops.push(e[i]);
                ++i;
            } else {
                ++i;
            }
        }
        while (!ops.empty()) {
            const double b = vals.top(); vals.pop();
            const double a = vals.top(); vals.pop();
            vals.push(applyOp(a, b, ops.top()));
            ops.pop();
        }
        return vals.empty() ? 0.0 : vals.top();
    } catch (...) {
        return 0.0;
    }
}

} // namespace

std::vector<gis::framework::ParamSpec> RasterMathPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"band_math", "reclassify", "raster_overlay"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入影像文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出文件", "输出影像文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "expression", "表达式", "波段运算表达式，例如 B1+B2",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "reclass_rules", "重分类规则", "每行一条规则，格式：旧值 or 旧最小值,旧最大值:新值\n例如: 1:10 / 2,5:20 / 6-9:30",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "default_value", "默认值", "未匹配到规则时的默认赋值，留空则保持原值",
            gis::framework::ParamType::Double, false, double{0.0},
            double{-1e15}, double{1e15}
        },
        gis::framework::ParamSpec{
            "keep_unmatched", "保留未匹配", "对于未匹配到规则的值：勾选=保留原值，不勾选=赋默认值",
            gis::framework::ParamType::Bool, false, bool{true}
        },
        gis::framework::ParamSpec{
            "reclass_mode", "重分类模式", "manual=自定义规则, interval=等间隔自动划分",
            gis::framework::ParamType::Enum, false, std::string{"manual"},
            int{0}, int{0},
            {"manual", "interval"}
        },
        gis::framework::ParamSpec{
            "interval_step", "间隔大小", "等间隔模式的步长，manual 模式忽略此参数",
            gis::framework::ParamType::Double, false, double{1.0},
            double{0.0}, double{1e15}
        },
        gis::framework::ParamSpec{
            "overlay_input", "叠加影像", "叠加分析的第二幅影像路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "overlay_method", "叠加方式", "叠加运算逻辑",
            gis::framework::ParamType::Enum, false, std::string{"max"},
            int{0}, int{0},
            {"max", "min", "mean", "sum", "subtract", "multiply", "divide", "and", "or", "cond"}
        },
        gis::framework::ParamSpec{
            "cond_expression", "条件表达式", "仅叠加方式为 cond 时生效\n例如: A > 100 & B > 50，满足赋 1，否则赋 0",
            gis::framework::ParamType::String, false, std::string{}
        },
    };
}

gis::framework::Result RasterMathPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string action = gis::framework::getParam<std::string>(params, "action", "");

    if (action == "band_math")      return doBandMath(params, progress);
    if (action == "reclassify")     return doReclassify(params, progress);
    if (action == "raster_overlay") return doRasterOverlay(params, progress);

    return gis::framework::Result::fail("Unknown action: " + action);
}

gis::framework::Result RasterMathPlugin::doBandMath(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string expression = gis::framework::getParam<std::string>(params, "expression", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (expression.empty()) {
        return gis::framework::Result::fail("expression is required (e.g., B1+B2, B1*0.5+B2*0.5)");
    }

    progress.throwIfCancelled();
    progress.onProgress(0.1);
    auto ds = gis::core::openRaster(input, true);
    const int width = ds->GetRasterXSize();
    const int height = ds->GetRasterYSize();
    const int bands = ds->GetRasterCount();

    progress.onMessage("Reading " + std::to_string(bands) + " bands...");
    std::vector<cv::Mat> bandMats;
    bandMats.reserve(static_cast<size_t>(bands));
    for (int b = 1; b <= bands; ++b) {
        bandMats.push_back(gis::core::gdalBandToMat(ds.get(), b));
    }
    progress.throwIfCancelled();
    progress.onProgress(0.4);

    progress.onMessage("Evaluating expression: " + expression);
    cv::Mat result(height, width, CV_32F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::map<std::string, double> bandValues;
            for (int b = 0; b < bands; ++b) {
                bandValues["B" + std::to_string(b + 1)] = bandMats[static_cast<size_t>(b)].at<float>(y, x);
            }
            result.at<float>(y, x) = static_cast<float>(evalExpression(expression, bandValues));
        }
        if ((y % 100) == 0) {
            progress.throwIfCancelled();
            progress.onProgress(0.4 + 0.5 * static_cast<double>(y) / height);
        }
    }

    progress.throwIfCancelled();
    progress.onProgress(0.9);
    progress.onMessage("Writing output: " + output);
    gis::core::matToGdalTiff(result, input, output, 1);
    progress.throwIfCancelled();
    progress.onProgress(1.0);
    return gis::framework::Result::ok("Processing completed successfully", output);
}

struct ReclassRule {
    double oldMin;
    double oldMax;
    double newVal;
    bool isRange;
};

static std::vector<ReclassRule> parseReclassRules(const std::string& rulesText) {
    std::vector<ReclassRule> rules;
    std::istringstream iss(rulesText);
    std::string line;
    while (std::getline(iss, line)) {
        auto trim = [](const std::string& s) -> std::string {
            const auto begin = s.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) return {};
            const auto end = s.find_last_not_of(" \t\r\n");
            return s.substr(begin, end - begin + 1);
        };
        line = trim(line);
        if (line.empty()) continue;

        const auto colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;
        const std::string oldPart = trim(line.substr(0, colonPos));
        const std::string newPart = trim(line.substr(colonPos + 1));
        if (oldPart.empty() || newPart.empty()) continue;

        ReclassRule rule;
        try {
            rule.newVal = std::stod(newPart);
        } catch (...) { continue; }

        const auto commaPos = oldPart.find(',');
        if (commaPos != std::string::npos) {
            rule.isRange = true;
            try {
                rule.oldMin = std::stod(trim(oldPart.substr(0, commaPos)));
                rule.oldMax = std::stod(trim(oldPart.substr(commaPos + 1)));
            } catch (...) { continue; }
        } else {
            rule.isRange = false;
            try {
                rule.oldMin = std::stod(oldPart);
                rule.oldMax = rule.oldMin;
            } catch (...) { continue; }
        }
        rules.push_back(rule);
    }
    return rules;
}

static double applyReclassRules(double value, const std::vector<ReclassRule>& rules,
                                 double defaultValue, bool keepUnmatched) {
    for (const auto& rule : rules) {
        if (value >= rule.oldMin && value <= rule.oldMax) {
            return rule.newVal;
        }
    }
    return keepUnmatched ? value : defaultValue;
}

gis::framework::Result RasterMathPlugin::doReclassify(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string rulesText = gis::framework::getParam<std::string>(params, "reclass_rules", "");
    const double defaultValue = gis::framework::getParam<double>(params, "default_value", 0.0);
    const bool keepUnmatched = gis::framework::getParam<bool>(params, "keep_unmatched", true);
    const std::string reclassMode = gis::framework::getParam<std::string>(params, "reclass_mode", "manual");
    const double intervalStep = gis::framework::getParam<double>(params, "interval_step", 1.0);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    std::vector<ReclassRule> rules;

    if (reclassMode == "interval") {
        if (intervalStep <= 0.0) {
            return gis::framework::Result::fail("interval_step must be positive");
        }

        progress.throwIfCancelled();
        progress.onProgress(0.02);
        progress.onMessage("Scanning raster for value range...");

        auto ds = gis::core::openRaster(input, true);
        const int bands = ds->GetRasterCount();

        for (int b = 1; b <= bands; ++b) {
            auto mat = gis::core::gdalBandToMat(ds.get(), b);
            double bandMin = 1e30;
            double bandMax = -1e30;
            int hasNoDataInt = 0;
            double nodata = ds->GetRasterBand(b)->GetNoDataValue(&hasNoDataInt);

            for (int y = 0; y < mat.rows; ++y) {
                for (int x = 0; x < mat.cols; ++x) {
                    const double val = static_cast<double>(mat.at<float>(y, x));
                    if (hasNoDataInt && std::abs(val - nodata) < 1e-9) continue;
                    if (val < bandMin) bandMin = val;
                    if (val > bandMax) bandMax = val;
                }
            }

            if (bandMin > bandMax) {
                return gis::framework::Result::fail(
                    "Band " + std::to_string(b) + " contains only NoData values");
            }

            const double roundedMin = std::floor(bandMin / intervalStep) * intervalStep;
            double current = roundedMin;
            int classIdx = 1;

            while (current < bandMax) {
                ReclassRule rule;
                rule.oldMin = current;
                rule.oldMax = current + intervalStep;
                rule.newVal = static_cast<double>(classIdx);
                rule.isRange = true;
                rules.push_back(rule);
                current += intervalStep;
                ++classIdx;
            }
        }

        if (rules.empty()) {
            return gis::framework::Result::fail("Failed to generate interval rules");
        }
    } else {
        if (rulesText.empty()) return gis::framework::Result::fail("reclass_rules is required for manual mode");

        rules = parseReclassRules(rulesText);
        if (rules.empty()) return gis::framework::Result::fail("No valid reclass rules parsed from:\n" + rulesText);
    }

    progress.throwIfCancelled();
    progress.onProgress(0.05);
    progress.onMessage("Loading input raster...");

    auto ds = gis::core::openRaster(input, true);
    const int width = ds->GetRasterXSize();
    const int height = ds->GetRasterYSize();
    const int bands = ds->GetRasterCount();

    progress.onMessage("Reclassifying " + std::to_string(bands) + " band(s) with " +
                       std::to_string(rules.size()) + " rule(s)...");

    for (int b = 1; b <= bands; ++b) {
        progress.throwIfCancelled();
        progress.onMessage("Processing band " + std::to_string(b) + "/" + std::to_string(bands));
        auto mat = gis::core::gdalBandToMat(ds.get(), b);
        cv::Mat result(height, width, mat.type());
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const double val = static_cast<double>(mat.at<float>(y, x));
                result.at<float>(y, x) = static_cast<float>(
                    applyReclassRules(val, rules, defaultValue, keepUnmatched));
            }
        }
        gis::core::matToGdalTiff(result, input, output, b);
        const double progressFrac = 0.05 + 0.9 * static_cast<double>(b) / bands;
        progress.onProgress(progressFrac);
    }

    progress.onProgress(1.0);
    return gis::framework::Result::ok("Reclassify completed successfully", output);
}

struct CondToken {
    enum Type { OPERAND, OP_EQ, OP_NE, OP_GT, OP_LT, OP_GE, OP_LE, OP_AND, OP_OR };
    Type type;
    double value;
    std::string operand;
};

static std::vector<CondToken> tokenizeCondExpr(const std::string& expr) {
    std::vector<CondToken> tokens;
    std::string e = expr;
    for (auto& ch : e) if (ch == '\t') ch = ' ';
    size_t i = 0;
    while (i < e.size()) {
        if (e[i] == ' ') { ++i; continue; }
        if (i + 1 < e.size() && e[i] == '>' && e[i + 1] == '=') { tokens.push_back({CondToken::OP_GE, 0, ""}); i += 2; continue; }
        if (i + 1 < e.size() && e[i] == '<' && e[i + 1] == '=') { tokens.push_back({CondToken::OP_LE, 0, ""}); i += 2; continue; }
        if (i + 1 < e.size() && e[i] == '!' && e[i + 1] == '=') { tokens.push_back({CondToken::OP_NE, 0, ""}); i += 2; continue; }
        if (i + 1 < e.size() && e[i] == '=' && e[i + 1] == '=') { tokens.push_back({CondToken::OP_EQ, 0, ""}); i += 2; continue; }
        if (e[i] == '>') { tokens.push_back({CondToken::OP_GT, 0, ""}); ++i; continue; }
        if (e[i] == '<') { tokens.push_back({CondToken::OP_LT, 0, ""}); ++i; continue; }
        if (e[i] == '&') { tokens.push_back({CondToken::OP_AND, 0, ""}); ++i; continue; }
        if (e[i] == '|') { tokens.push_back({CondToken::OP_OR, 0, ""}); ++i; continue; }
        if (std::isdigit(static_cast<unsigned char>(e[i])) || e[i] == '.' || e[i] == '-') {
            size_t start = i;
            if (e[i] == '-' && i + 1 < e.size() && std::isdigit(static_cast<unsigned char>(e[i + 1]))) ++i;
            while (i < e.size() && (std::isdigit(static_cast<unsigned char>(e[i])) || e[i] == '.')) ++i;
            tokens.push_back({CondToken::OPERAND, std::stod(e.substr(start, i - start)), ""});
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(e[i])) || e[i] == '_') {
            size_t start = i;
            while (i < e.size() && (std::isalnum(static_cast<unsigned char>(e[i])) || e[i] == '_')) ++i;
            tokens.push_back({CondToken::OPERAND, 0, e.substr(start, i - start)});
            continue;
        }
        ++i;
    }
    return tokens;
}

static bool evalCondExpr(const std::vector<CondToken>& tokens, double aVal, double bVal) {
    if (tokens.empty()) return true;
    std::vector<bool> results;
    std::vector<CondToken::Type> logicOps;
    size_t idx = 0;
    while (idx < tokens.size()) {
        if (tokens[idx].type == CondToken::OPERAND && !tokens[idx].operand.empty() &&
            idx + 1 < tokens.size() && idx + 2 < tokens.size() &&
            tokens[idx + 1].type != CondToken::OPERAND &&
            tokens[idx + 2].type == CondToken::OPERAND && tokens[idx + 2].operand.empty()) {

            const double lhs = tokens[idx].operand == "A" ? aVal : (tokens[idx].operand == "B" ? bVal : 0);
            const double rhs = tokens[idx + 2].value;
            bool cmp = false;
            switch (tokens[idx + 1].type) {
                case CondToken::OP_EQ: cmp = std::abs(lhs - rhs) < 1e-9; break;
                case CondToken::OP_NE: cmp = std::abs(lhs - rhs) >= 1e-9; break;
                case CondToken::OP_GT: cmp = lhs > rhs; break;
                case CondToken::OP_LT: cmp = lhs < rhs; break;
                case CondToken::OP_GE: cmp = lhs >= rhs; break;
                case CondToken::OP_LE: cmp = lhs <= rhs; break;
                default: break;
            }
            results.push_back(cmp);
            idx += 3;

            if (idx < tokens.size() && (tokens[idx].type == CondToken::OP_AND || tokens[idx].type == CondToken::OP_OR)) {
                logicOps.push_back(tokens[idx].type);
                ++idx;
            }
            continue;
        }
        ++idx;
    }

    if (results.empty()) return true;
    bool final = results[0];
    for (size_t j = 0; j < logicOps.size() && j + 1 < results.size(); ++j) {
        if (logicOps[j] == CondToken::OP_AND) final = final && results[j + 1];
        else final = final || results[j + 1];
    }
    return final;
}

gis::framework::Result RasterMathPlugin::doRasterOverlay(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string overlayInput = gis::framework::getParam<std::string>(params, "overlay_input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string method = gis::framework::getParam<std::string>(params, "overlay_method", "max");
    const std::string condExpr = gis::framework::getParam<std::string>(params, "cond_expression", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (overlayInput.empty()) return gis::framework::Result::fail("overlay_input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.throwIfCancelled();
    progress.onProgress(0.05);
    progress.onMessage("Loading input rasters...");

    auto dsA = gis::core::openRaster(input, true);
    auto dsB = gis::core::openRaster(overlayInput, true);

    const int widthA = dsA->GetRasterXSize();
    const int heightA = dsA->GetRasterYSize();
    const int widthB = dsB->GetRasterXSize();
    const int heightB = dsB->GetRasterYSize();

    const int width = std::min(widthA, widthB);
    const int height = std::min(heightA, heightB);
    const int bands = std::min(dsA->GetRasterCount(), dsB->GetRasterCount());

    std::vector<CondToken> condTokens;
    if (method == "cond" && !condExpr.empty()) {
        condTokens = tokenizeCondExpr(condExpr);
    }

    for (int b = 1; b <= bands; ++b) {
        progress.throwIfCancelled();
        progress.onMessage("Overlay band " + std::to_string(b) + "/" + std::to_string(bands) + " method=" + method);

        auto matA = gis::core::gdalBandToMat(dsA.get(), b);
        auto matB = gis::core::gdalBandToMat(dsB.get(), b);

        cv::Mat result(height, width, CV_32F);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const float aVal = matA.at<float>(y, x);
                const float bVal = matB.at<float>(y, x);
                float r = 0.0f;

                if (method == "max")          r = std::max(aVal, bVal);
                else if (method == "min")     r = std::min(aVal, bVal);
                else if (method == "mean")    r = (aVal + bVal) * 0.5f;
                else if (method == "sum")     r = aVal + bVal;
                else if (method == "subtract") r = aVal - bVal;
                else if (method == "multiply") r = aVal * bVal;
                else if (method == "divide")   r = (std::abs(bVal) < 1e-9f) ? 0.0f : aVal / bVal;
                else if (method == "and")      r = (aVal > 0 && bVal > 0) ? 1.0f : 0.0f;
                else if (method == "or")       r = (aVal > 0 || bVal > 0) ? 1.0f : 0.0f;
                else if (method == "cond")     r = evalCondExpr(condTokens, aVal, bVal) ? 1.0f : 0.0f;
                else r = aVal;

                result.at<float>(y, x) = r;
            }
            if ((y % 100) == 0) {
                progress.throwIfCancelled();
            }
        }

        gis::core::matToGdalTiff(result, input, output, b);
        const double progressFrac = 0.05 + 0.9 * static_cast<double>(b) / bands;
        progress.onProgress(progressFrac);
    }

    progress.onProgress(1.0);
    return gis::framework::Result::ok("Raster overlay completed successfully", output);
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::RasterMathPlugin)
