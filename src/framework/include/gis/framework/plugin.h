#pragma once
#include <gis/framework/param_spec.h>
#include <gis/framework/result.h>
#include <gis/core/progress.h>
#include <vector>
#include <map>
#include <set>
#include <string>

namespace gis::framework {

/** Plugin interface for GIS algorithm plugins. All algorithms must implement this. */
class IGisPlugin {
public:
    virtual ~IGisPlugin() = default;

    /** Returns the unique internal name of the plugin (e.g., "projection"). */
    virtual std::string name() const = 0;
    /** Returns the user-visible display name (e.g., "投影转换"). */
    virtual std::string displayName() const = 0;
    /** Returns the plugin version string. */
    virtual std::string version() const = 0;
    /** Returns a short description of what the plugin does. */
    virtual std::string description() const = 0;

    /** Returns the parameter specifications for this plugin. */
    virtual std::vector<ParamSpec> paramSpecs() const = 0;

    /**
     * Returns the keys that this action must be given before dispatch.
     *
     * ParamSpec::required is declared once per plugin and shared by every action of that plugin,
     * so it can only say "most actions need this key". An action whose implementation never reads
     * such a key would be rejected with a false "不能为空" (N183 / #146: processing.stats and
     * vector.info both demand --output and then never write it). Override this only to subtract
     * those false requirements. The CLI and the workflow dispatcher derive the required gate from
     * here, and GuiContract.RequiredParamKeysArePanelVisible rejects any action whose required set
     * contains a key that the GUI panel for that action does not render.
     */
    virtual std::set<std::string> requiredParamKeys(const std::string& actionKey) const {
        std::set<std::string> keys;
        for (const auto& spec : paramSpecs()) {
            if (spec.required) {
                keys.insert(spec.key);
            }
        }
        return keys;
    }

    /**
     * Execute the plugin with the given parameters.
     * @param params Map of parameter key to value.
     * @param progress Progress reporter for cancellation and status updates.
     * @return Execution result.
     */
    virtual Result execute(
        const std::map<std::string, ParamValue>& params,
        gis::core::ProgressReporter& progress) = 0;
};

/**
 * The plugin spec table with ParamSpec::required rewritten to this action's requiredness.
 * Dispatch layers (CLI / workflow) validate against this table instead of paramSpecs(), so the
 * required gate and findFirstInvalidParamKey always read the same vector.
 */
inline std::vector<ParamSpec> actionScopedParamSpecs(const IGisPlugin& plugin,
                                                      const std::string& actionKey) {
    const std::set<std::string> requiredKeys = plugin.requiredParamKeys(actionKey);
    std::vector<ParamSpec> specs = plugin.paramSpecs();
    for (auto& spec : specs) {
        spec.required = requiredKeys.count(spec.key) > 0;
    }
    return specs;
}

// Cross-platform export macro
#ifdef _WIN32
    #define GIS_EXPORT __declspec(dllexport)
#else
    #define GIS_EXPORT __attribute__((visibility("default")))
#endif

#define GIS_PLUGIN_EXPORT(PluginClass) \
    extern "C" GIS_EXPORT gis::framework::IGisPlugin* createPlugin() { \
        return new PluginClass(); \
    } \
    extern "C" GIS_EXPORT void destroyPlugin(gis::framework::IGisPlugin* p) { \
        delete p; \
    }

} // namespace gis::framework
