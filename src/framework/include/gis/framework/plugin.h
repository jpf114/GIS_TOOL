#pragma once
#include <gis/framework/param_spec.h>
#include <gis/framework/result.h>
#include <gis/core/progress.h>
#include <vector>
#include <map>
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
     * Execute the plugin with the given parameters.
     * @param params Map of parameter key to value.
     * @param progress Progress reporter for cancellation and status updates.
     * @return Execution result.
     */
    virtual Result execute(
        const std::map<std::string, ParamValue>& params,
        gis::core::ProgressReporter& progress) = 0;
};

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
