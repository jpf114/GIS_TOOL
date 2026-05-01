#pragma once

#include <string>
#include <vector>

namespace gis::gui {

struct CustomIndexUserPreset {
    std::string key;
    std::string name;
    std::string expression;
};

std::string customIndexUserPresetFilePath();
std::vector<CustomIndexUserPreset> loadCustomIndexUserPresets();
std::string findCustomIndexUserPresetExpression(const std::string& presetKey);
std::string findCustomIndexUserPresetName(const std::string& presetKey);
bool isCustomIndexUserPresetKey(const std::string& presetKey);
std::string saveCustomIndexUserPreset(const std::string& name,
                                      const std::string& expression,
                                      std::string* errorMessage = nullptr);
bool removeCustomIndexUserPreset(const std::string& presetKey,
                                 std::string* errorMessage = nullptr);

} // namespace gis::gui
