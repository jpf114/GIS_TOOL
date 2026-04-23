#include <gis/framework/plugin_manager.h>
#include <gis/core/error.h>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace gis::framework {

PluginManager::~PluginManager() {
    unloadAll();
}

void PluginManager::loadFromDirectory(const std::string& dir) {
    namespace fs = std::filesystem;
    if (!fs::exists(dir)) return;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        auto ext = path.extension().string();
#ifdef _WIN32
        if (ext == ".dll") {
            auto stem = path.stem().string();
            if (stem.find("plugin_") == 0) {
                loadPlugin(path.string());
            }
        }
#else
        if (ext == ".so") {
            auto stem = path.stem().string();
            if (stem.find("libplugin_") == 0) {
                loadPlugin(path.string());
            }
        }
#endif
    }
}

IGisPlugin* PluginManager::find(const std::string& name) const {
    for (auto* p : plugins_) {
        if (p->name() == name) return p;
    }
    return nullptr;
}

void PluginManager::unloadAll() {
    for (auto& h : handles_) {
        if (h.plugin) {
            h.plugin = nullptr;
        }
#ifdef _WIN32
        if (h.library) FreeLibrary(static_cast<HMODULE>(h.library));
#else
        if (h.library) dlclose(h.library);
#endif
    }
    handles_.clear();
    plugins_.clear();
}

void PluginManager::loadPlugin(const std::string& path) {
#ifdef _WIN32
    void* lib = LoadLibraryA(path.c_str());
#else
    void* lib = dlopen(path.c_str(), RTLD_NOW);
#endif
    if (!lib) {
        std::cerr << "Warning: cannot load plugin: " << path << std::endl;
        return;
    }

    using CreateFn = IGisPlugin*();
    using DestroyFn = void(IGisPlugin*);

#ifdef _WIN32
    auto createFn = reinterpret_cast<CreateFn*>(
        GetProcAddress(static_cast<HMODULE>(lib), "createPlugin"));
    auto destroyFn = reinterpret_cast<DestroyFn*>(
        GetProcAddress(static_cast<HMODULE>(lib), "destroyPlugin"));
#else
    auto createFn = reinterpret_cast<CreateFn*>(
        dlsym(lib, "createPlugin"));
    auto destroyFn = reinterpret_cast<DestroyFn*>(
        dlsym(lib, "destroyPlugin"));
#endif

    if (!createFn) {
        std::cerr << "Warning: no createPlugin symbol in: " << path << std::endl;
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(lib));
#else
        dlclose(lib);
#endif
        return;
    }

    IGisPlugin* plugin = createFn();
    if (!plugin) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(lib));
#else
        dlclose(lib);
#endif
        return;
    }

    handles_.push_back({lib, plugin});
    plugins_.push_back(plugin);
    std::cerr << "Loaded plugin: " << plugin->displayName()
              << " (" << plugin->name() << " v" << plugin->version() << ")" << std::endl;
}

} // namespace gis::framework
