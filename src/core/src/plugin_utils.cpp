#include <gis/core/plugin_utils.h>

#include <cmath>
#include <stack>
#include <vector>

namespace gis::core {

ProcessingMetadata buildPluginMetadata(const std::string& input, GDALDataset* srcDs,
                                        const std::string& algorithm) {
    ProcessingMetadata meta;
    meta.sourceFile = input;
    meta.sourceCrs = getSRSWKT(srcDs);
    meta.processingAlgorithm = algorithm;
    return meta;
}

double evalExpression(const std::string& expr,
                      const std::map<std::string, double>& bandValues) {
    // Security: reject overly long expressions
    if (expr.size() > kMaxExpressionLength) return 0.0;

    // Substitute band variables with their numeric values
    std::string e = expr;
    std::vector<std::string> keys;
    keys.reserve(bandValues.size());
    for (const auto& [key, val] : bandValues) {
        keys.push_back(key);
    }
    // Sort by length descending to avoid partial replacement (e.g., B10 before B1)
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

    // Security: validate that the resolved expression contains only safe characters
    for (char c : e) {
        if (std::isdigit(static_cast<unsigned char>(c))) continue;
        if (c == '.') continue;
        if (c == '+' || c == '-' || c == '*' || c == '/') continue;
        if (c == '(' || c == ')') continue;
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        return 0.0; // Unsafe character detected
    }

    try {
        std::stack<double> vals;
        std::stack<char> ops;
        size_t depth = 0;

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
                ++depth;
                // Security: reject excessive nesting
                if (depth > kMaxExpressionDepth) return 0.0;
                ++i;
            } else if (e[i] == ')') {
                while (!ops.empty() && ops.top() != '(') {
                    if (vals.size() < 2) return 0.0;
                    const double b = vals.top(); vals.pop();
                    const double a = vals.top(); vals.pop();
                    vals.push(applyOp(a, b, ops.top()));
                    ops.pop();
                }
                if (!ops.empty()) {
                    ops.pop();
                }
                if (depth > 0) --depth;
                ++i;
            } else if (e[i] == '+' || e[i] == '-' || e[i] == '*' || e[i] == '/') {
                // Security: limit operator stack depth
                if (ops.size() >= kMaxExpressionDepth) return 0.0;
                while (!ops.empty() && precedence(ops.top()) >= precedence(e[i])) {
                    if (vals.size() < 2) return 0.0;
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
            if (vals.size() < 2) return 0.0;
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

} // namespace gis::core
