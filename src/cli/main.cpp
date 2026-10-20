#include <gis/framework/plugin_manager.h>
#include <gis/framework/action_validation.h>
#include <gis/framework/execute_guarded.h>
#include <gis/framework/result_display.h>
#include <gis/core/progress.h>
#include <gis/core/error.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/runtime_env.h>
#include <gis/core/logger_init.h>
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
    // Task B12 / N58：查找 + 未命中透传的同一段逻辑已收口到 framework 的
    // convertParamValue（param_spec.h），工作流侧调的是同一个函数。
    return gis::framework::convertParamValue(specs, key, value, outValue, error);
}

// Task B14 / N35：CLI 层拒绝"未声明的参数键"。
// 键名匹配规则：与 paramSpecs() 的 spec.key 逐字节比较（大小写敏感、不做别名、不做前缀匹配）。
// --help/-h、--list/-l 在 parseArgs 里已消费，不会落进 params；位置参数（插件名、动作）不是参数键。
// 边界：本检查只发生在 CLI 调用点，convertParamValue 的"未命中透传"保持原样（B12 明令，
// 工作流侧依赖它）；由此产生的"CLI 拒 / 工作流透传"不对称由 B19/B20 决定是否统一。
static std::vector<std::string> findUndeclaredParamKeys(
    const std::map<std::string, std::string>& params,
    const std::vector<gis::framework::ParamSpec>& specs)
{
    std::vector<std::string> unknown;
    for (const auto& entry : params) {
        bool declared = false;
        for (const auto& spec : specs) {
            if (spec.key == entry.first) {
                declared = true;
                break;
            }
        }
        if (!declared) {
            unknown.push_back(entry.first);
        }
    }
    return unknown;
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
    gis::core::initDefaultLogging();
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
    std::error_code ec;
    auto exePath = fs::canonical(fs::path(programName).parent_path(), ec);
    if (ec) {
        exePath = fs::current_path(ec);
    }
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

    // Task B14 / N35：未声明键必须点名报错（可能不止一个 ⇒ 全部列出），
    // 不能静默透传给插件后按默认值跑完还报 rc=0。
    const auto undeclaredKeys = findUndeclaredParamKeys(args.params, specs);
    if (!undeclaredKeys.empty()) {
        std::cerr << "Error: unknown parameter";
        if (undeclaredKeys.size() > 1) {
            std::cerr << "s";
        }
        for (size_t i = 0; i < undeclaredKeys.size(); ++i) {
            std::cerr << (i == 0 ? " " : ", ") << "\"" << undeclaredKeys[i] << "\"";
        }
        std::cerr << " (plugin " << plugin->name();
        if (!args.positional.empty()) {
            std::cerr << ", action " << args.positional[0];
        }
        std::cerr << "); parameters must be spelled exactly as in `gis-cli "
                  << plugin->name() << " --help` (case-sensitive, no aliases)." << std::endl;
        return 1;
    }

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

    std::string action;
    if (const auto actionIt = params.find("action");
        actionIt != params.end() && std::holds_alternative<std::string>(actionIt->second)) {
        action = std::get<std::string>(actionIt->second);
    }
    if (const auto issue =
            gis::framework::validateActionSpecificParams(plugin->name(), action, params);
        issue.has_value()) {
        std::cerr << "Error: " << issue->message << std::endl;
        return 1;
    }

    gis::core::CliProgress progress;
    // Task B15 / N66：异常兜获收口到 framework 的 executeGuarded（一处 catch 三件套）。
    // 改前这里只有 CancelledException / GisError / std::exception 三个 catch、**没有**
    // `catch (...)` ⇒ 非 std 派生的抛出（如 `throw 42;`）接不住，进程被 std::terminate
    // 带走（Release 实测 rc=0xC0000409、无任何输出；Debug 停在调试 CRT 的 abort 对话框）。
    // 取消仍走单独分支（rc=2），其余异常与"插件自己报的失败"合流到下面的 `Error:` + rc=1
    // —— 与改前的 GisError 分支同文案同 rc（它本来也是 `Error: <what>` / 1）。
    const gis::framework::Result result =
        gis::framework::executeGuarded(*plugin, params, progress);

    if (result.isCancelled) {
        std::cerr << "Cancelled." << std::endl;
        return 2;
    }

    if (result.success) {
        std::cout << "Success: " << result.message << std::endl;
        if (!result.outputPath.empty()) {
            std::cout << "Output: " << result.outputPath << std::endl;
        }
        if (!result.metadata.empty()) {
            std::cout << "Metadata:" << std::endl;
            for (const auto& [key, val] : gis::framework::orderedMetadataEntries(result.metadata)) {
                std::cout << "  " << key << ": " << val << std::endl;
            }
        }
        return 0;
    } else {
        std::cerr << "Error: " << result.message << std::endl;
        return 1;
    }
}
