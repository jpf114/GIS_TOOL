#include "cli_parser.h"
#include <iostream>

namespace gis::cli {

CliArgs parseArgs(int argc, char* argv[]) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list" || arg == "-l") {
            args.listPlugins = true;
        } else if (arg == "--help" || arg == "-h") {
            args.showHelp = true;
        } else if (arg.substr(0, 2) == "--") {
            auto eqPos = arg.find('=');
            if (eqPos != std::string::npos) {
                std::string key = arg.substr(2, eqPos - 2);
                std::string val = arg.substr(eqPos + 1);
                args.params[key] = val;
            } else {
                std::string key = arg.substr(2);
                if (i + 1 < argc && std::string(argv[i + 1])[0] != '-') {
                    args.params[key] = argv[++i];
                } else {
                    args.params[key] = "true";
                }
            }
        } else {
            if (args.pluginName.empty()) {
                args.pluginName = arg;
            } else {
                args.positional.push_back(arg);
            }
        }
    }
    return args;
}

void printUsage(const char* progName) {
    std::cout << "GIS Tool - Geospatial processing toolkit" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "  " << progName << " --list" << std::endl;
    std::cout << "  " << progName << " <plugin> --help" << std::endl;
    std::cout << "  " << progName << " <plugin> <action> [options...]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --list, -l     List available plugins" << std::endl;
    std::cout << "  --help, -h     Show help" << std::endl;
    std::cout << "  --<key>=<val>  Pass parameter to plugin" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << progName << " projection reproject --input=dem.tif --output=out.tif --dst_srs=EPSG:4326" << std::endl;
    std::cout << "  " << progName << " cutting clip --input=img.tif --output=clip.tif --extent=116.3,39.8,116.5,40.0" << std::endl;
}

} // namespace gis::cli
