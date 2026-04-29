#include <gis/framework/plugin_manager.h>
#include <gis/core/progress.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/runtime_env.h>
#include "cli_parser.h"
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

static void listPlugins(gis::framework::PluginManager& mgr) {
    if (mgr.plugins().empty()) {
        std::cout << "No plugins found." << std::endl;
        return;
    }
    std::cout << "Available plugins:" << std::endl;
    for (auto* p : mgr.plugins()) {
        std::cout << "  " << p->name() << " - " << p->displayName()
                  << " (v" << p->version() << ")" << std::endl;
        std::cout << "    " << p->description() << std::endl;
    }
}

static void printPluginHelp(gis::framework::IGisPlugin* plugin) {
    std::cout << plugin->displayName() << " (" << plugin->name() << " v" << plugin->version() << ")" << std::endl;
    std::cout << plugin->description() << std::endl;
    std::cout << std::endl << "Parameters:" << std::endl;
    for (auto& spec : plugin->paramSpecs()) {
        std::cout << "  --" << spec.key;
        if (spec.required) std::cout << " [required]";
        else std::cout << " [optional]";
        std::cout << std::endl;
        std::cout << "    " << spec.displayName << ": " << spec.description << std::endl;
        if (!spec.enumValues.empty()) {
            std::cout << "    Options: ";
            for (size_t i = 0; i < spec.enumValues.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << spec.enumValues[i];
            }
            std::cout << std::endl;
        }
        if (!spec.required) {
            std::visit([&](auto&& val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    if (!val.empty()) std::cout << "    Default: " << val << std::endl;
                } else if constexpr (std::is_same_v<T, int>) {
                    std::cout << "    Default: " << val << std::endl;
                } else if constexpr (std::is_same_v<T, double>) {
                    std::cout << "    Default: " << val << std::endl;
                } else if constexpr (std::is_same_v<T, bool>) {
                    std::cout << "    Default: " << (val ? "true" : "false") << std::endl;
                }
            }, spec.defaultValue);
        }
    }
}

static bool convertParam(
    const std::string& key, const std::string& value,
    const std::vector<gis::framework::ParamSpec>& specs,
    gis::framework::ParamValue& outValue,
    std::string& error)
{
    for (auto& spec : specs) {
        if (spec.key == key) {
            return gis::framework::tryParseParamValue(spec, value, outValue, error);
        }
    }
    outValue = value;
    return true;
}

#ifdef _WIN32
static std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int sizeNeeded = WideCharToMultiByte(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) {
        return {};
    }

    std::string utf8(sizeNeeded, '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        utf8.data(), sizeNeeded, nullptr, nullptr);
    return utf8;
}

static std::vector<std::string> collectCliArgs() {
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argvW) {
        return {};
    }

    std::vector<std::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        args.push_back(wideToUtf8(argvW[i]));
    }
    LocalFree(argvW);
    return args;
}
#else
static std::vector<std::string> collectCliArgs(int argc, char* argv[]) {
    return std::vector<std::string>(argv, argv + argc);
}
#endif

int main(int argc, char* argv[]) {
    gis::core::initRuntimeEnvironment();
    gis::core::initGDAL();

#ifdef _WIN32
    auto cliArgv = collectCliArgs();
#else
    auto cliArgv = collectCliArgs(argc, argv);
#endif
    auto args = gis::cli::parseArgs(cliArgv);
    const std::string programName = cliArgv.empty() ? "gis-cli" : cliArgv.front();

    if (args.showHelp && args.pluginName.empty()) {
        gis::cli::printUsage(programName);
        return 0;
    }

    namespace fs = std::filesystem;
    auto exePath = fs::canonical(fs::path(programName).parent_path());
    const auto pluginsDir = gis::core::findPluginDirectoryFrom(exePath);

    gis::framework::PluginManager mgr;
    if (!pluginsDir.empty()) {
        mgr.loadFromDirectory(pluginsDir.string());
    }

    if (args.listPlugins) {
        listPlugins(mgr);
        return 0;
    }

    if (args.pluginName.empty()) {
        gis::cli::printUsage(programName);
        return 1;
    }

    auto* plugin = mgr.find(args.pluginName);
    if (!plugin) {
        std::cerr << "Unknown plugin: " << args.pluginName << std::endl;
        std::cerr << "Use --list to see available plugins." << std::endl;
        return 1;
    }

    if (args.showHelp) {
        printPluginHelp(plugin);
        return 0;
    }

    auto specs = plugin->paramSpecs();

    std::map<std::string, gis::framework::ParamValue> params;
    for (auto& [key, val] : args.params) {
        gis::framework::ParamValue converted;
        std::string error;
        if (!convertParam(key, val, specs, converted, error)) {
            std::cerr << "Error: " << error << std::endl;
            return 1;
        }
        params[key] = converted;
    }
    if (!args.positional.empty()) {
        gis::framework::ParamValue actionValue;
        std::string error;
        if (!convertParam("action", args.positional[0], specs, actionValue, error)) {
            std::cerr << "Error: " << error << std::endl;
            return 1;
        }
        params["action"] = actionValue;
    }

    const std::string validationError = gis::framework::validateParams(specs, params);
    if (!validationError.empty()) {
        std::cerr << "Error: " << validationError << std::endl;
        return 1;
    }

    gis::core::CliProgress progress;
    auto result = plugin->execute(params, progress);

    if (result.success) {
        std::cout << "Success: " << result.message << std::endl;
        if (!result.outputPath.empty()) {
            std::cout << "Output: " << result.outputPath << std::endl;
        }
        if (!result.metadata.empty()) {
            std::cout << "Metadata:" << std::endl;
            for (auto& [key, val] : result.metadata) {
                std::cout << "  " << key << ": " << val << std::endl;
            }
        }
        return 0;
    } else {
        std::cerr << "Error: " << result.message << std::endl;
        return 1;
    }
}
