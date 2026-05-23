#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace gis::framework {

namespace ResultMetadataKeys {
constexpr const char* kAction = "action";
constexpr const char* kInput = "input";
constexpr const char* kOutput = "output";
constexpr const char* kBand = "band";
constexpr const char* kCrs = "crs";
constexpr const char* kEpsg = "epsg";
constexpr const char* kActualSrs = "actual_srs";
constexpr const char* kWidth = "width";
constexpr const char* kHeight = "height";
constexpr const char* kOutputWidth = "output_width";
constexpr const char* kOutputHeight = "output_height";
constexpr const char* kDataType = "data_type";
constexpr const char* kFeatureCount = "feature_count";
constexpr const char* kElapsedMs = "elapsed_ms";
constexpr const char* kSummary = "__summary__";
} // namespace ResultMetadataKeys

std::vector<std::string> standardMetadataKeyOrder();
std::vector<std::pair<std::string, std::string>> orderedMetadataEntries(
    const std::map<std::string, std::string>& metadata);
void mergeResultMetadata(std::map<std::string, std::string>& target,
                         const std::map<std::string, std::string>& fields);

} // namespace gis::framework
