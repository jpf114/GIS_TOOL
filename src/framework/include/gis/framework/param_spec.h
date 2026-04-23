#pragma once
#include <string>
#include <vector>
#include <variant>
#include <array>
#include <map>

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

} // namespace gis::framework
