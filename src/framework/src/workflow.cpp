#include <gis/framework/workflow.h>
#include <gis/framework/action_validation.h>
#include <gis/framework/plugin.h>
#include <gis/framework/plugin_manager.h>
#include <gis/core/progress.h>

#include <algorithm>
#include <exception>

namespace gis::framework {

WorkflowRunner::WorkflowRunner(PluginManager& pluginManager)
    : pluginManager_(pluginManager) {}

WorkflowResult WorkflowRunner::execute(
    const Workflow& workflow,
    const std::map<std::string, std::string>& initialParams) {

    WorkflowResult result;
    result.totalSteps = static_cast<int>(workflow.steps.size());

    if (workflow.steps.empty()) {
        result.success = true;
        result.message = "Empty workflow completed";
        return result;
    }

    std::map<std::string, std::string> context = initialParams;

    for (size_t i = 0; i < workflow.steps.size(); ++i) {
        const WorkflowStep& step = workflow.steps[i];

        auto* plugin = pluginManager_.find(step.pluginName);
        if (!plugin) {
            result.message = "Plugin not found: " + step.pluginName;
            result.stepMessages.push_back(result.message);
            return result;
        }

        std::map<std::string, std::string> resolvedStringParams;
        for (auto paramIt = step.params.begin(); paramIt != step.params.end(); ++paramIt) {
            std::string resolvedValue = paramIt->second;

            for (auto mapIt = step.inputMapping.begin(); mapIt != step.inputMapping.end(); ++mapIt) {
                if (paramIt->second == "${" + mapIt->first + "}") {
                    auto ctxIt = context.find(mapIt->second);
                    if (ctxIt != context.end()) {
                        resolvedValue = ctxIt->second;
                    }
                }
            }

            for (auto ctxIt = context.begin(); ctxIt != context.end(); ++ctxIt) {
                std::string placeholder = "${" + ctxIt->first + "}";
                size_t pos = resolvedValue.find(placeholder);
                while (pos != std::string::npos) {
                    resolvedValue.replace(pos, placeholder.size(), ctxIt->second);
                    pos = resolvedValue.find(placeholder, pos + ctxIt->second.size());
                }
            }

            resolvedStringParams[paramIt->first] = resolvedValue;
        }

        // Task 14：按插件声明的 ParamSpec 把字符串参数解析成带类型的 ParamValue。
        // 旧形态是这个循环体只有 `typedParams[it->first] = it->second;` 一句全透传，
        // 于是插件侧的 getParam<int|double|bool> 永远取不到对应备选、一律静默退回代码内联
        // 默认值——数值参数在任何工作流里都不生效。
        // Task B12 / N58：解析形态收口到 framework 的 convertParamValue（param_spec.h），
        // 与 CLI 的 convertParam 调的是同一个函数：找到 spec 就 tryParseParamValue，
        // 找不到的未知键保持透传字符串（把未知键改成显式报错会撞挂账 N35 的 CLI 同口径问题，
        // 本任务不动；B14 才管未知键）。specs 取整份 plugin->paramSpecs()，与 CLI 一致。
        const std::vector<ParamSpec> specs = plugin->paramSpecs();
        std::map<std::string, ParamValue> typedParams;
        for (auto it = resolvedStringParams.begin(); it != resolvedStringParams.end(); ++it) {
            ParamValue parsed;
            std::string parseError;
            if (!convertParamValue(specs, it->first, it->second, parsed, parseError)) {
                // 键名由工作流侧拼进去：tryParseParamValue 的文案取 paramDisplayName(spec)
                // （displayName 非空时用它，见 param_spec.h:66-68），里面**没有**键名，
                // 只有"参数“间隔步长”不是有效数字"这种人类可读串——两处都要，缺一不可定位。
                // 不改共用的 tryParseParamValue：它同时被 CLI 与既有断言依赖。
                result.message = "Step " + std::to_string(i + 1) + " (" + step.pluginName + "." +
                                 step.actionKey + ") invalid parameter " + it->first + ": " + parseError;
                result.stepMessages.push_back(result.message);
                return result;
            }
            typedParams[it->first] = parsed;
        }
        typedParams["action"] = step.actionKey;

        // Task B12：派发前跑与 CLI（src/cli/main.cpp:201/:212）同一套参数校验，顺序也一致：
        // 先 validateParams（整份 specs），再 validateActionSpecificParams（action 级组合）。
        // 缺这两段时，同一份越界参数 CLI 拒、工作流却一路送进插件（N94/N57）。
        {
            const std::string validationIssue = validateParams(specs, typedParams);
            if (!validationIssue.empty()) {
                // 键名取 findFirstInvalidParamKey：它与 validateParams 是同一份 specs、
                // 同一个 validateParamValue、同一个循环顺序（param_spec.h:213-236），
                // 两次扫描必然指向同一个键；为了省一趟扫描改成别的形态反而会引入不对称。
                const std::string invalidKey = findFirstInvalidParamKey(specs, typedParams);
                result.message = "Step " + std::to_string(i + 1) + " (" + step.pluginName + "." +
                                 step.actionKey + ") invalid parameter " + invalidKey + ": " +
                                 validationIssue;
                result.stepMessages.push_back(result.message);
                return result;
            }
        }
        if (const auto issue = validateActionSpecificParams(plugin->name(), step.actionKey,
                                                            typedParams);
            issue.has_value()) {
            // action 级那条直接用 issue.key / issue.message，不再调 findFirstInvalidParamKey。
            result.message = "Step " + std::to_string(i + 1) + " (" + step.pluginName + "." +
                             step.actionKey + ") invalid parameter " + issue->key + ": " +
                             issue->message;
            result.stepMessages.push_back(result.message);
            return result;
        }

        gis::core::NullProgressReporter nullProgress;
        // Task 14：插件抛出来的异常必须由工作流兜住并写成这一步的失败，
        // 不能让它穿出 execute 把调用方（GUI 线程 / CLI / 测试进程）一起带走。
        // 真实触发路径之一：gis::core::openRaster 打不开文件时抛 GisError
        // （src/core/src/gdal_wrapper.cpp:49-51，GisError 派生自 std::runtime_error），
        // 而 raster_math 的 doReclassify 对它没有自家 catch（不像 spindex
        // 在 spindex_plugin.cpp:774-776 会把异常转成 Result::fail）。
        Result stepResult;
        try {
            stepResult = plugin->execute(typedParams, nullProgress);
        } catch (const std::exception& e) {
            result.message = "Step " + std::to_string(i + 1) + " (" + step.pluginName + "." +
                             step.actionKey + ") threw: " + e.what();
            result.stepMessages.push_back(result.message);
            return result;
        }

        result.completedSteps = static_cast<int>(i + 1);

        if (!stepResult.success) {
            result.message = "Step " + std::to_string(i + 1) + " (" +
                             step.pluginName + "." + step.actionKey + ") failed: " +
                             stepResult.message;
            result.stepMessages.push_back(result.message);
            return result;
        }

        result.stepMessages.push_back(
            "Step " + std::to_string(i + 1) + " (" +
            step.pluginName + "." + step.actionKey + ") completed");

        if (!stepResult.outputPath.empty()) {
            context["output"] = stepResult.outputPath;
            context[step.pluginName + "." + step.actionKey + ".output"] = stepResult.outputPath;
            result.lastOutputPath = stepResult.outputPath;
        }

        for (auto metaIt = stepResult.metadata.begin(); metaIt != stepResult.metadata.end(); ++metaIt) {
            context[step.pluginName + "." + step.actionKey + "." + metaIt->first] = metaIt->second;
        }
    }

    result.success = true;
    result.message = "Workflow '" + workflow.name + "' completed successfully";
    return result;
}

std::vector<Workflow> WorkflowPresets::allPresets() {
    std::vector<Workflow> result;
    result.push_back(remoteSensingPreprocess());
    result.push_back(terrainAnalysis());
    result.push_back(changeDetection());
    return result;
}

Workflow WorkflowPresets::remoteSensingPreprocess() {
    Workflow wf;
    wf.name = "遥感预处理";
    wf.description = "依次执行辐射定标、大气校正（DOS）和裁切。";

    WorkflowStep s1;
    s1.pluginName = "georef";
    s1.actionKey = "radiometric";
    s1.params["input"] = "${input}";
    s1.params["output"] = "${output_dir}/${basename}_rad.tif";

    WorkflowStep s2;
    s2.pluginName = "georef";
    s2.actionKey = "dos";
    s2.params["input"] = "${georef.radiometric.output}";
    s2.params["output"] = "${output_dir}/${basename}_dos.tif";

    WorkflowStep s3;
    s3.pluginName = "cutting";
    s3.actionKey = "clip";
    s3.params["input"] = "${georef.dos.output}";
    s3.params["output"] = "${output_dir}/${basename}_clip.tif";

    wf.steps.push_back(s1);
    wf.steps.push_back(s2);
    wf.steps.push_back(s3);
    return wf;
}

Workflow WorkflowPresets::terrainAnalysis() {
    Workflow wf;
    wf.name = "地形分析";
    wf.description = "依次执行填洼、流向、汇流累积和河网提取。";

    WorkflowStep s1;
    s1.pluginName = "terrain";
    s1.actionKey = "fill_sinks";
    s1.params["input"] = "${input}";
    s1.params["output"] = "${output_dir}/${basename}_fill.tif";

    WorkflowStep s2;
    s2.pluginName = "terrain";
    s2.actionKey = "flow_direction";
    s2.params["input"] = "${terrain.fill_sinks.output}";
    s2.params["output"] = "${output_dir}/${basename}_dir.tif";

    WorkflowStep s3;
    s3.pluginName = "terrain";
    s3.actionKey = "flow_accumulation";
    s3.params["input"] = "${terrain.flow_direction.output}";
    s3.params["output"] = "${output_dir}/${basename}_accum.tif";

    WorkflowStep s4;
    s4.pluginName = "terrain";
    s4.actionKey = "stream_extract";
    s4.params["input"] = "${terrain.flow_accumulation.output}";
    s4.params["output"] = "${output_dir}/${basename}_stream.tif";

    wf.steps.push_back(s1);
    wf.steps.push_back(s2);
    wf.steps.push_back(s3);
    wf.steps.push_back(s4);
    return wf;
}

Workflow WorkflowPresets::changeDetection() {
    Workflow wf;
    wf.name = "变化检测";
    wf.description = "先执行影像配准，再进行变化检测。";

    WorkflowStep s1;
    s1.pluginName = "matching";
    s1.actionKey = "register";
    s1.params["input"] = "${input_before}";
    s1.params["reference"] = "${input_after}";
    s1.params["output"] = "${output_dir}/${basename}_registered.tif";

    WorkflowStep s2;
    s2.pluginName = "matching";
    s2.actionKey = "change";
    s2.params["input"] = "${matching.register.output}";
    s2.params["reference"] = "${input_after}";
    s2.params["output"] = "${output_dir}/${basename}_change.tif";

    wf.steps.push_back(s1);
    wf.steps.push_back(s2);
    return wf;
}

} // namespace gis::framework
