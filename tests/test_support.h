#pragma once

#include <filesystem>
#include <string>

namespace gis::tests {

std::filesystem::path testExecutableDir();
std::filesystem::path defaultTestOutputDir(const std::string& suiteName);
std::filesystem::path testPluginDir();
void ensureDirectory(const std::filesystem::path& dir);

// 21-FixA part2 I-6：**本进程私有的** QSettings 落点（按 pid 分道）。
// test_main.cpp 在 RUN_ALL_TESTS 之前把它设成 UserScope/SystemScope 的 ini 目录，
// test_mainwindow.cpp 断言"落点确实就是它"。两处共用同一个算式，谁改了谁红。
std::filesystem::path privateSettingsDir();

} // namespace gis::tests
