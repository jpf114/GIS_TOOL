#pragma once

#include <filesystem>

namespace gis::core {

void initRuntimeEnvironment();
std::filesystem::path findRuntimePathFrom(
    const std::filesystem::path& startDir,
    const std::filesystem::path& relativePath);
std::filesystem::path findPluginDirectoryFrom(
    const std::filesystem::path& startDir);

} // namespace gis::core
