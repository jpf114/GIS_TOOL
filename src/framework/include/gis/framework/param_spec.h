#pragma once
#include <string>
#include <vector>
#include <variant>
#include <array>
#include <map>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace gis::framework {

enum class ParamType {
    String,
    Int,
    Double,
    Bool,
    FilePath,
    DirPath,
    Enum,
    Extent,
    CRS,
};

using ParamValue = std::variant<
    std::string,
    int,
    double,
    bool,
    std::vector<std::string>,
    std::array<double, 4>
>;

struct ParamSpec {
    std::string key;
    std::string displayName;
    std::string description;
    ParamType type = ParamType::String;
    bool required = true;
    ParamValue defaultValue = std::string{};
    ParamValue minValue = int{0};
    ParamValue maxValue = int{0};
    std::vector<std::string> enumValues;

    // Builder-style helpers
    ParamSpec& setName(const std::string& n) { displayName = n; return *this; }
    ParamSpec& setDesc(const std::string& d) { description = d; return *this; }
    ParamSpec& setRequired(bool r) { required = r; return *this; }
    ParamSpec& setDefault(const ParamValue& v) { defaultValue = v; return *this; }
    ParamSpec& setEnum(const std::vector<std::string>& v) { enumValues = v; return *this; }
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
            const auto* minValue = std::get_if<int>(&spec.minValue);
            const auto* maxValue = std::get_if<int>(&spec.maxValue);
            if (minValue && maxValue && *maxValue > *minValue &&
                (*intValue < *minValue || *intValue > *maxValue)) {
                std::ostringstream oss;
                oss << issuePrefix << "超出范围 [" << *minValue << ", " << *maxValue << "]";
                return oss.str();
            }
            return {};
        }
        case ParamType::Double: {
            const auto* doubleValue = std::get_if<double>(value);
            if (!doubleValue) {
                return issuePrefix + "类型无效";
            }
            const auto* minValue = std::get_if<double>(&spec.minValue);
            const auto* maxValue = std::get_if<double>(&spec.maxValue);
            if (minValue && maxValue && *maxValue > *minValue &&
                (*doubleValue < *minValue || *doubleValue > *maxValue)) {
                std::ostringstream oss;
                oss << issuePrefix << "超出范围 [" << *minValue << ", " << *maxValue << "]";
                return oss.str();
            }
            return {};
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
