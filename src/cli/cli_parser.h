#pragma once
#include <string>
#include <vector>
#include <map>

namespace gis::cli {

struct CliArgs {
    bool listPlugins = false;
    bool showHelp = false;
    std::string pluginName;
    std::map<std::string, std::string> params;
    std::vector<std::string> positional;
};

CliArgs parseArgs(int argc, char* argv[]);
void printUsage(const char* progName);

} // namespace gis::cli
