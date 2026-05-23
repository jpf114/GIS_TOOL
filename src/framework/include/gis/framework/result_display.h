#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace gis::framework {

std::vector<std::string> standardMetadataKeyOrder();
std::vector<std::pair<std::string, std::string>> orderedMetadataEntries(
    const std::map<std::string, std::string>& metadata);

} // namespace gis::framework
