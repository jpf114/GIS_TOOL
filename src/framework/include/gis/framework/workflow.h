#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <gis/framework/param_spec.h>

namespace gis::framework {

class IGisPlugin;
struct Result;
class PluginManager;

struct WorkflowStep {
    std::string pluginName;
    std::string actionKey;
    std::map<std::string, std::string> params;
    std::map<std::string, std::string> inputMapping;
};

struct Workflow {
    std::string name;
    std::string description;
    std::vector<WorkflowStep> steps;
};

struct WorkflowResult {
    bool success = false;
    std::string message;
    int completedSteps = 0;
    int totalSteps = 0;
    std::vector<std::string> stepMessages;
    std::string lastOutputPath;
};

class WorkflowRunner {
public:
    explicit WorkflowRunner(PluginManager& pluginManager);

    WorkflowResult execute(const Workflow& workflow,
                           const std::map<std::string, std::string>& initialParams);

private:
    PluginManager& pluginManager_;
};

class WorkflowPresets {
public:
    static std::vector<Workflow> allPresets();
    static Workflow remoteSensingPreprocess();
    static Workflow terrainAnalysis();
    static Workflow changeDetection();
};

} // namespace gis::framework
