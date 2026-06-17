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

/** A single step in a workflow, referencing a plugin and its parameters. */
struct WorkflowStep {
    std::string pluginName;                        ///< Name of the plugin to execute
    std::string actionKey;                         ///< Action key within the plugin
    std::map<std::string, std::string> params;     ///< Step parameters
    std::map<std::string, std::string> inputMapping; ///< Mapping from previous step outputs to this step's inputs
};

/** A named workflow consisting of sequential steps. */
struct Workflow {
    std::string name;                      ///< Workflow name
    std::string description;               ///< Workflow description
    std::vector<WorkflowStep> steps;       ///< Ordered list of steps
};

/** Result of executing a complete workflow. */
struct WorkflowResult {
    bool success = false;                  ///< Whether the entire workflow succeeded
    std::string message;                   ///< Summary message
    int completedSteps = 0;                ///< Number of steps completed successfully
    int totalSteps = 0;                    ///< Total number of steps in the workflow
    std::vector<std::string> stepMessages; ///< Per-step result messages
    std::string lastOutputPath;            ///< Output path from the last completed step
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
