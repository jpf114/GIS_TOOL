#pragma once

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <gis/framework/param_spec.h>
#include <gis/framework/result.h>

namespace gis::gui {

enum class DataKind {
    Unknown,
    Raster,
    Vector,
};

enum class DataOrigin {
    Input,
    Output,
};

struct DataAutoFillInfo {
    std::string crs;
    std::string layerName;
    std::array<double, 4> extent{0.0, 0.0, 0.0, 0.0};
    bool hasExtent = false;
};

struct BindableParamOption {
    std::string key;
    std::string displayName;
};

struct ActionValidationIssue {
    std::string key;
    std::string message;
};

struct FileParamUiConfig {
    bool isOutput = false;
    bool selectDirectory = false;
    bool allowMultiSelect = false;
    std::string placeholder;
    std::string openFilter;
    std::string saveFilter;
    std::string suggestedSuffix;
};

struct DerivedOutputUpdate {
    std::string value;
    std::string autoValue;
    bool shouldApply = false;
};

struct ExecuteButtonState {
    bool enabled = false;
    std::string tooltip;
    std::string statusText;
    std::string statusObjectName;
};

DataKind detectDataKind(const std::string& path);
bool isSupportedDataPath(const std::string& path);
std::vector<std::string> collectSupportedDataPaths(const std::vector<std::string>& paths);
std::vector<std::string> collectSupportedDataPathsRecursively(const std::vector<std::string>& paths);
bool canPreviewData(const std::string& path);
std::string dataKindDisplayName(DataKind kind);
std::string dataOriginDisplayName(DataOrigin origin);
bool isOutputDataOrigin(DataOrigin origin);
std::string buildDataDisplayLabel(const std::string& path,
                                  DataKind kind,
                                  DataOrigin origin,
                                  bool isActive = false);
std::string buildSuggestedOutputPath(const std::string& inputPath,
                                     const std::string& pluginName,
                                     const std::string& action,
                                     const std::string& paramKey = "output");
DerivedOutputUpdate computeDerivedOutputUpdate(const std::string& currentValue,
                                               const std::string& lastAutoValue,
                                               const std::string& primaryPath,
                                               const std::string& pluginName,
                                               const std::string& action,
                                               const std::string& paramKey = "output",
                                               const std::string& formatValue = "");
bool shouldAutoFillLayerValue(const std::string& currentValue,
                              const std::string& lastAutoValue,
                              const std::string& suggestedValue);
bool shouldAutoFillExtentValue(const std::optional<std::array<double, 4>>& currentValue,
                               const std::optional<std::array<double, 4>>& lastAutoValue,
                               bool hasSuggestedExtent);
FileParamUiConfig buildFileParamUiConfig(const std::string& pluginName,
                                         const std::string& action,
                                         const std::string& paramKey,
                                         gis::framework::ParamType paramType);
std::string buildTextParamPlaceholder(const std::string& pluginName,
                                      const std::string& action,
                                      const gis::framework::ParamSpec& spec);
std::vector<std::string> spindexCustomIndexPresetValues();
std::string spindexCustomIndexPresetExpression(const std::string& presetKey);
DerivedOutputUpdate computeDerivedExpressionUpdate(const std::string& currentValue,
                                                   const std::string& lastAutoValue,
                                                   const std::string& pluginName,
                                                   const std::string& action,
                                                   const std::string& presetKey);
DataAutoFillInfo inspectDataForAutoFill(const std::string& path);
std::string localizeResultMessage(const std::string& message);
std::string buildResultSummaryText(const gis::framework::Result& result);
std::string validateExecutionParams(
    const std::vector<gis::framework::ParamSpec>& specs,
    const std::map<std::string, gis::framework::ParamValue>& params);
std::optional<ActionValidationIssue> validateActionSpecificParams(
    const std::string& pluginName,
    const std::string& action,
    const std::map<std::string, gis::framework::ParamValue>& params);
std::string findFirstInvalidParamKey(
    const std::vector<gis::framework::ParamSpec>& specs,
    const std::map<std::string, gis::framework::ParamValue>& params);
std::vector<BindableParamOption> collectBindableParamOptions(
    const std::vector<gis::framework::ParamSpec>& specs,
    DataKind dataKind);
std::vector<gis::framework::ParamSpec> buildEffectiveGuiParamSpecs(
    const std::string& pluginName,
    const std::string& action,
    const std::vector<gis::framework::ParamSpec>& specs,
    const std::set<std::string>& visibleKeys,
    const std::set<std::string>& requiredKeys);
ExecuteButtonState buildExecuteButtonState(bool hasSelection,
                                           const std::string& validationMessage);
std::string resolveHighlightedParamKey(
    bool hasSelection,
    const std::vector<gis::framework::ParamSpec>& specs,
    const std::map<std::string, gis::framework::ParamValue>& params,
    const std::optional<ActionValidationIssue>& actionIssue);

} // namespace gis::gui
