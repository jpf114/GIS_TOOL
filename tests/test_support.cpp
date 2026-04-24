#include "test_support.h"

#include <cstdlib>
#include <stdexcept>

namespace gis::tests {

std::filesystem::path testExecutableDir() {
    return std::filesystem::current_path();
}

std::filesystem::path defaultTestOutputDir(const std::string& suiteName) {
    return testExecutableDir() / suiteName;
}

std::filesystem::path testPluginDir() {
    const char* configuredDir = std::getenv("GIS_TEST_PLUGIN_DIR");
    if (configuredDir && *configuredDir) {
        return std::filesystem::path(configuredDir);
    }

#ifdef GIS_TEST_PLUGIN_DIR
    return std::filesystem::path(GIS_TEST_PLUGIN_DIR);
#else
    throw std::runtime_error("未配置测试插件目录");
#endif
}

void ensureDirectory(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
}

} // namespace gis::tests
