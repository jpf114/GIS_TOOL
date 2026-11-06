#pragma once
#include <string>
#include <vector>
#include <variant>
#include <array>
#include <map>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <type_traits>

namespace gis::framework {

/** Parameter type enumeration for plugin parameter specifications. */
enum class ParamType {
    String,   ///< Free-form string
    Int,      ///< Integer value
    Double,   ///< Floating-point value
    Bool,     ///< Boolean value
    FilePath, ///< File path string
    DirPath,  ///< Directory path string
    Enum,     ///< Enumerated value (one of enumValues)
    Extent,   ///< Geographic extent (xmin, ymin, xmax, ymax)
    CRS,      ///< Coordinate reference system identifier
};

/** Variant type holding a parameter value. */
using ParamValue = std::variant<
    std::string,
    int,
    double,
    bool,
    std::vector<std::string>,
    std::array<double, 4>
>;

/** Specification for a single plugin parameter. */
struct ParamSpec {
    std::string key;            ///< Unique parameter key identifier
    std::string displayName;    ///< User-visible parameter name
    std::string description;    ///< Parameter description
    ParamType type = ParamType::String; ///< Parameter type
    bool required = true;       ///< Whether the parameter is required
    ParamValue defaultValue = std::string{}; ///< Default value when not provided
    ParamValue minValue = int{0};   ///< Minimum allowed value (for Int/Double)
    ParamValue maxValue = int{0};   ///< Maximum allowed value (for Int/Double)
    std::vector<std::string> enumValues; ///< Allowed values for Enum type
    bool exclusiveMin = false;  ///< true ⇒ 值必须严格大于 minValue（下界开区间，(min, max]）
    bool exclusiveMax = false;  ///< true ⇒ 值必须严格小于 maxValue（上界开区间，[min, max)）
    bool requireOdd = false;    ///< 仅对 Int 生效：true ⇒ 只接受奇数（核大小这类卷积窗口）

    // Builder-style helpers
    ParamSpec& setName(const std::string& n) { displayName = n; return *this; }
    ParamSpec& setDesc(const std::string& d) { description = d; return *this; }
    ParamSpec& setRequired(bool r) { required = r; return *this; }
    ParamSpec& setDefault(const ParamValue& v) { defaultValue = v; return *this; }
    ParamSpec& setEnum(const std::vector<std::string>& v) { enumValues = v; return *this; }
    // 聚合初始化是位置写死的八~九个字段，开/闭与奇偶用这两个方法补在末尾，避免把布尔位塞进位置串里读不出含义。
    ParamSpec& setExclusive(bool min, bool max) { exclusiveMin = min; exclusiveMax = max; return *this; }
    ParamSpec& setRequireOdd() { requireOdd = true; return *this; }
};

// Get a value from the params map, with type checking and default fallback
template<typename T>
T getParam(const std::map<std::string, ParamValue>& params,
           const std::string& key, const T& fallback = T{}) {
    auto it = params.find(key);
    if (it == params.end()) return fallback;
    const T* ptr = std::get_if<T>(&it->second);
    return ptr ? *ptr : fallback;
}

inline std::string paramDisplayName(const ParamSpec& spec) {
    return spec.displayName.empty() ? spec.key : spec.displayName;
}

inline bool tryParseParamValue(const ParamSpec& spec,
                               const std::string& text,
                               ParamValue& outValue,
                               std::string& error) {
    switch (spec.type) {
        case ParamType::Int:
            try {
                size_t index = 0;
                const int value = std::stoi(text, &index);
                if (index != text.size()) {
                    error = "参数“" + paramDisplayName(spec) + "”不是有效整数";
                    return false;
                }
                outValue = value;
                return true;
            } catch (...) {
                error = "参数“" + paramDisplayName(spec) + "”不是有效整数";
                return false;
            }
        case ParamType::Double:
            try {
                size_t index = 0;
                const double value = std::stod(text, &index);
                if (index != text.size()) {
                    error = "参数“" + paramDisplayName(spec) + "”不是有效数字";
                    return false;
                }
                outValue = value;
                return true;
            } catch (...) {
                error = "参数“" + paramDisplayName(spec) + "”不是有效数字";
                return false;
            }
        case ParamType::Bool: {
            std::string lowered = text;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (lowered == "true" || lowered == "1" || lowered == "yes" ||
                lowered == "on") {
                outValue = true;
                return true;
            }
            if (lowered == "false" || lowered == "0" || lowered == "no" ||
                lowered == "off") {
                outValue = false;
                return true;
            }
            error = "参数“" + paramDisplayName(spec) + "”不是有效布尔值";
            return false;
        }
        case ParamType::Extent: {
            std::array<double, 4> extent{0.0, 0.0, 0.0, 0.0};
            std::istringstream iss(text);
            char sep1 = '\0';
            char sep2 = '\0';
            char sep3 = '\0';
            if (!(iss >> extent[0] >> sep1 >> extent[1] >> sep2 >> extent[2] >> sep3 >> extent[3]) ||
                sep1 != ',' || sep2 != ',' || sep3 != ',' || (iss >> std::ws, !iss.eof())) {
                error = "参数“" + paramDisplayName(spec) + "”不是有效范围，格式应为 xmin,ymin,xmax,ymax";
                return false;
            }
            outValue = extent;
            return true;
        }
        case ParamType::String:
        case ParamType::FilePath:
        case ParamType::DirPath:
        case ParamType::Enum:
        case ParamType::CRS:
        default:
            outValue = text;
            return true;
    }
}

// Task B12 / N58：CLI 与工作流派发原先各有一份"在 specs 里线性查找 + 未命中透传字符串"
// 的同一段逻辑：CLI 侧是 src/cli/main.cpp 的 static convertParam，工作流侧 inline 在
// workflow.cpp 的 typedParams 循环里。抽到这里让两侧共享同一形态（Step 4 选①）。
// 语义保持不变：命中 spec 走 tryParseParamValue，未知键 outValue = text 并返回 true
// ——未知键透传是 N35/B14 的既有口径，本任务不收紧。
inline bool convertParamValue(const std::vector<ParamSpec>& specs,
                              const std::string& key,
                              const std::string& text,
                              ParamValue& outValue,
                              std::string& error) {
    for (const auto& spec : specs) {
        if (spec.key == key) {
            return tryParseParamValue(spec, text, outValue, error);
        }
    }
    outValue = text;
    return true;
}

// 区间文案的唯一拼装点：括号形态跟着实际生效的开/闭位走。
// WHY 要单源：手写层过去把同一个键写成 "[0, 100) 范围内" 而实现判的是闭区间上界，
// 文案与实到边界不符（N172 的实测形态之一），此后凡是区间文案都从这里出。
template<typename T>
inline std::string paramIntervalText(const ParamSpec& spec, T lo, T hi) {
    std::ostringstream oss;
    oss << (spec.exclusiveMin ? '(' : '[') << lo << ", " << hi << (spec.exclusiveMax ? ')' : ']');
    return oss.str();
}

// Int / Double 两支共用的数值判据：区间（含开/闭位）与奇偶位都在这里，调用方不再各写一份比较。
template<typename T>
inline std::string validateNumericParam(const ParamSpec& spec, T value) {
    const std::string issuePrefix = "参数“" + paramDisplayName(spec) + "”";
    const T* minValue = std::get_if<T>(&spec.minValue);
    const T* maxValue = std::get_if<T>(&spec.maxValue);
    const bool rangeActive = minValue && maxValue && *maxValue > *minValue;
    if (rangeActive) {
        const bool rejected = value < *minValue || value > *maxValue ||
            (spec.exclusiveMin && value == *minValue) ||
            (spec.exclusiveMax && value == *maxValue);
        if (rejected) {
            return issuePrefix + "超出范围 " + paramIntervalText(spec, *minValue, *maxValue);
        }
    }
    if constexpr (std::is_same_v<T, int>) {
        // requireOdd 只对 Int 生效：Double 上写它是声明错误，宁可让区间位去判、这里静默不判。
        if (spec.requireOdd && value % 2 == 0) {
            if (rangeActive) {
                return issuePrefix + "必须是" + paramIntervalText(spec, *minValue, *maxValue) + " 内的奇数";
            }
            return issuePrefix + "必须是奇数";
        }
    }
    return {};
}

inline std::string validateParamValue(const ParamSpec& spec,
                                      const ParamValue* value) {
    const std::string issuePrefix = "参数“" + paramDisplayName(spec) + "”";
    if (!value) {
        return spec.required ? issuePrefix + "不能为空" : std::string{};
    }

    switch (spec.type) {
        case ParamType::String:
        case ParamType::FilePath:
        case ParamType::DirPath:
        case ParamType::CRS:
        case ParamType::Enum: {
            const auto* text = std::get_if<std::string>(value);
            if (!text) {
                return issuePrefix + "类型无效";
            }
            if (spec.required && text->empty()) {
                return issuePrefix + "不能为空";
            }
            if (spec.type == ParamType::Enum && !text->empty() && !spec.enumValues.empty()) {
                const auto it = std::find(spec.enumValues.begin(), spec.enumValues.end(), *text);
                if (it == spec.enumValues.end()) {
                    return issuePrefix + "取值无效";
                }
            }
            return {};
        }
        case ParamType::Int: {
            const auto* intValue = std::get_if<int>(value);
            if (!intValue) {
                return issuePrefix + "类型无效";
            }
            return validateNumericParam(spec, *intValue);
        }
        case ParamType::Double: {
            const auto* doubleValue = std::get_if<double>(value);
            if (!doubleValue) {
                return issuePrefix + "类型无效";
            }
            return validateNumericParam(spec, *doubleValue);
        }
        case ParamType::Bool:
            return std::get_if<bool>(value) ? std::string{} : issuePrefix + "类型无效";
        case ParamType::Extent:
            return std::get_if<std::array<double, 4>>(value) ? std::string{} : issuePrefix + "类型无效";
    }

    return {};
}

inline std::string validateParams(const std::vector<ParamSpec>& specs,
                                  const std::map<std::string, ParamValue>& params) {
    for (const auto& spec : specs) {
        const auto it = params.find(spec.key);
        const ParamValue* value = (it == params.end()) ? nullptr : &it->second;
        const std::string issue = validateParamValue(spec, value);
        if (!issue.empty()) {
            return issue;
        }
    }
    return {};
}

inline std::string findFirstInvalidParamKey(const std::vector<ParamSpec>& specs,
                                            const std::map<std::string, ParamValue>& params) {
    for (const auto& spec : specs) {
        const auto it = params.find(spec.key);
        const ParamValue* value = (it == params.end()) ? nullptr : &it->second;
        if (!validateParamValue(spec, value).empty()) {
            return spec.key;
        }
    }
    return {};
}

} // namespace gis::framework
