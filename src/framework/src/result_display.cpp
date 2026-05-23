#include <gis/framework/result_display.h>

#include <algorithm>

namespace gis::framework {

std::vector<std::string> standardMetadataKeyOrder() {
    return {
        "action",
        "input",
        "output",
        "band",
        "crs",
        "epsg",
        "actual_srs",
        "source_crs",
        "dst_srs",
        "width",
        "height",
        "output_width",
        "output_height",
        "data_type",
        "output_format",
        "feature_count",
        "elapsed_ms",
    };
}

std::vector<std::pair<std::string, std::string>> orderedMetadataEntries(
    const std::map<std::string, std::string>& metadata) {
    if (metadata.empty()) {
        return {};
    }

    std::vector<std::pair<std::string, std::string>> entries;
    entries.reserve(metadata.size());

    const auto preferred = standardMetadataKeyOrder();
    for (const auto& key : preferred) {
        const auto it = metadata.find(key);
        if (it != metadata.end()) {
            entries.emplace_back(it->first, it->second);
        }
    }

    std::vector<std::string> remaining;
    remaining.reserve(metadata.size());
    for (const auto& [key, value] : metadata) {
        if (std::find(preferred.begin(), preferred.end(), key) != preferred.end()) {
            continue;
        }
        remaining.push_back(key);
    }
    std::sort(remaining.begin(), remaining.end());
    for (const auto& key : remaining) {
        const auto it = metadata.find(key);
        if (it != metadata.end()) {
            entries.emplace_back(it->first, it->second);
        }
    }

    return entries;
}

} // namespace gis::framework
