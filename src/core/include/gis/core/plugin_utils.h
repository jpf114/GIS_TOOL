#pragma once

#include <gis/core/gdal_wrapper.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <string>

class GDALDataset;

namespace gis::core {

/** Trim whitespace from both ends of a string. */
inline std::string trimString(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

/** Convert string to lowercase. */
inline std::string toLowerString(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

/** Build standard processing metadata for a plugin result. */
ProcessingMetadata buildPluginMetadata(const std::string& input, GDALDataset* srcDs,
                                        const std::string& algorithm);

} // namespace gis::core
