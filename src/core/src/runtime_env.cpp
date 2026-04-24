#include <gis/core/runtime_env.h>

#include <opencv2/core/utils/logger.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace gis::core {

namespace {

std::filesystem::path executableDir() {
#ifdef _WIN32
    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(path).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

std::filesystem::path findRuntimePath(const std::filesystem::path& startDir,
                                     const std::filesystem::path& relativePath) {
    auto current = startDir;
    while (!current.empty()) {
        const auto candidate = current / relativePath;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }

        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return {};
}

void setEnvVarIfFound(const char* key, const std::filesystem::path& value) {
    if (value.empty()) {
        return;
    }

#ifdef _WIN32
    _putenv_s(key, value.string().c_str());
#else
    setenv(key, value.string().c_str(), 1);
#endif
}

bool envVarExists(const char* key) {
    const auto* value = std::getenv(key);
    return value != nullptr && value[0] != '\0';
}

} // namespace

void initRuntimeEnvironment() {
    const auto exeDir = executableDir();

    const auto projDir = findRuntimePath(exeDir, "share/proj");
    setEnvVarIfFound("PROJ_LIB", projDir);
    setEnvVarIfFound("PROJ_DATA", projDir);

    const auto gdalDir = findRuntimePath(exeDir, "share/gdal");
    setEnvVarIfFound("GDAL_DATA", gdalDir);

    if (!envVarExists("OPENCV_LOG_LEVEL")) {
#ifdef _WIN32
        _putenv_s("OPENCV_LOG_LEVEL", "ERROR");
#else
        setenv("OPENCV_LOG_LEVEL", "ERROR", 1);
#endif
    }

    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);
}

} // namespace gis::core
