#pragma once

#include <string>
#include <vector>

namespace gis::core {

std::vector<std::string> spindexCustomIndexPresetValues();
std::string spindexCustomIndexPresetExpression(const std::string& presetKey);

} // namespace gis::core
