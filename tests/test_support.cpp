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

std::filesystem::path privateSettingsDir() {
    // 根目录默认落在 exe 同级的 qsettings_output/（与其余用例产物同一棵树，跑完全量也不会
    // 往用户 profile 里写任何东西）；GIS_TEST_SETTINGS_ROOT 只给手工复现用。
    const char* overrideRoot = std::getenv("GIS_TEST_SETTINGS_ROOT");
    const std::filesystem::path base = (overrideRoot != nullptr && overrideRoot[0] != '\0')
        ? std::filesystem::path(overrideRoot)
        : defaultTestOutputDir("qsettings_output");
#ifdef _WIN32
    const std::string pidTag = "pid_" + std::to_string(static_cast<unsigned long>(GetCurrentProcessId()));
#else
    const std::string pidTag = "pid_" + std::to_string(static_cast<unsigned long>(::getpid()));
#endif
    // WHY 按 pid：一个 ctest 用例 = 一个 gis_tests 进程（DISCOVERY_MODE PRE_TEST），
    //   13 个 MainWindowTest 进程原先共享**同一个定名** ini（settings/GISTool/GISTool.ini，
    //   无 pid）⇒ -j 下互相 trunc/抢 SQLite 式的写锁。pid 分道后并发写互不为敌，
    //   也不再依赖"哪条用例先跑"这种 ctest 从不保证的顺序假设。
    std::filesystem::path dir = base / pidTag;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);  // 建不出来不致命：QSettings 落笔时还会再试
    return dir;
}

void ensureDirectory(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
}

} // namespace gis::tests
