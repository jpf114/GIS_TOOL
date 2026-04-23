#pragma once
#include <gis/framework/plugin.h>
#include <memory>
#include <string>
#include <vector>

namespace gis::framework {

class PluginManager {
public:
    ~PluginManager();

    // Scan directory for plugin DLLs and load them
    void loadFromDirectory(const std::string& dir);

    // Get all loaded plugins
    const std::vector<IGisPlugin*>& plugins() const { return plugins_; }

    // Find plugin by name
    IGisPlugin* find(const std::string& name) const;

    // Unload all plugins
    void unloadAll();

private:
    void loadPlugin(const std::string& path);

    struct PluginHandle {
        void* library = nullptr;
        IGisPlugin* plugin = nullptr;
    };

    std::vector<PluginHandle> handles_;
    std::vector<IGisPlugin*> plugins_;
};

} // namespace gis::framework
