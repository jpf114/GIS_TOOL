#pragma once

#include <filesystem>
#include <string>

namespace gis::tests {

std::filesystem::path testExecutableDir();
std::filesystem::path defaultTestOutputDir(const std::string& suiteName);
std::filesystem::path testPluginDir();
void ensureDirectory(const std::filesystem::path& dir);

} // namespace gis::tests
