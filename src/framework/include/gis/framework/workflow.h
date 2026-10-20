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
    /// Task B15 / N66：某一步"被取消"（`Result::isCancelled`）时置真。取消**不算成功**，
    /// 但也不等于失败：改前用户取消被写成 `Step n (p.a) threw: 操作已取消`，与插件崩溃同格。
    /// 只增字段、默认 false ⇒ 既有消费点（workflow_page.cpp 只读 success/completedSteps/
    /// totalSteps/stepMessages/message）一个字都不用改。
    bool isCancelled = false;              ///< Whether execution stopped because of cancellation
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
