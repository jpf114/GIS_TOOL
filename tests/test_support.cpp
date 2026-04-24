#include "test_support.h"

#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

namespace gis::tests {

std::filesystem::path testExecutableDir() {
#ifdef _WIN32
    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(path).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

std::filesystem::path defaultTestOutputDir(const std::string& suiteName) {
    return testExecutableDir() / suiteName;
}

std::filesystem::path testPluginDir() {
    return testExecutableDir().parent_path().parent_path() / "plugins";
}

void ensureDirectory(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
}

} // namespace gis::tests
