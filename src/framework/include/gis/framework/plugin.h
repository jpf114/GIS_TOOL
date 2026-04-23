#pragma once
#include <gis/framework/param_spec.h>
#include <gis/framework/result.h>
#include <gis/core/progress.h>
#include <vector>
#include <map>
#include <string>

namespace gis::framework {

class IGisPlugin {
public:
    virtual ~IGisPlugin() = default;

    virtual std::string name() const = 0;
    virtual std::string displayName() const = 0;
    virtual std::string version() const = 0;
    virtual std::string description() const = 0;

    virtual std::vector<ParamSpec> paramSpecs() const = 0;

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
