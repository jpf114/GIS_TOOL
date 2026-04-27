#include <gis/core/runtime_env.h>

#include <opencv2/core/utils/logger.hpp>

#include <cstdlib>
#include <filesystem>
#include <system_error>
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

bool isPluginLibraryPath(const std::filesystem::directory_entry& entry) {
    if (!entry.is_regular_file()) {
        return false;
    }

    const auto filename = entry.path().filename().string();
    if (filename.rfind("plugin_", 0) != 0) {
        return false;
    }

#ifdef _WIN32
    return entry.path().extension() == ".dll";
#else
    return entry.path().extension() == ".so" || entry.path().extension() == ".dylib";
#endif
}

} // namespace

std::filesystem::path findRuntimePathFrom(
    const std::filesystem::path& startDir,
    const std::filesystem::path& relativePath) {
    return findRuntimePath(startDir, relativePath);
}

std::filesystem::path findPluginDirectoryFrom(const std::filesystem::path& startDir) {
    auto current = startDir;
    while (!current.empty()) {
        const auto candidate = current / "plugins";
        if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate)) {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(candidate, ec)) {
                if (ec) {
                    break;
                }
                if (isPluginLibraryPath(entry)) {
                    return candidate;
                }
            }
        }

        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return {};
}

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
