#pragma once

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QString>

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

struct ActionUiConfig {
    QString displayName;
    QString description;
    std::set<std::string> visibleKeys;
    std::set<std::string> requiredKeys;
};

struct ParamText {
    QString displayName;
    QString description;
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

struct DerivedParamTracking {
    std::string outputPath;
    std::string vectorOutputPath;
    std::string rasterOutputPath;
    std::string expressionValue;
    std::string layerName;
    std::optional<std::array<double, 4>> extent;
};

struct DerivedParamSyncResult {
    DerivedOutputUpdate outputUpdate;
    DerivedOutputUpdate vectorOutputUpdate;
    DerivedOutputUpdate rasterOutputUpdate;
    DerivedOutputUpdate expressionUpdate;
    bool shouldApplyLayer = false;
    std::string layerValue;
    bool shouldApplyExtent = false;
    std::array<double, 4> extent{0.0, 0.0, 0.0, 0.0};
    DerivedParamTracking tracking;
};

struct ExecuteButtonState {
    bool enabled = false;
    std::string tooltip;
    std::string statusText;
    std::string statusObjectName;
};

struct GroupSelectionText {
    QString title;
    QString description;
    QString metaText;
    QString statusText;
};

struct ActionSelectionText {
    QString title;
    QString description;
    QString metaText;
    QString statusText;
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
bool usesMultiFileTextPicker(const std::string& pluginName,
                             const std::string& action,
                             const std::string& paramKey);
std::string multiFileTextPickerFilter(const std::string& pluginName,
                                      const std::string& action,
                                      const std::string& paramKey);
const std::string& rasterToolsGroupKey();
const std::vector<std::string>& rasterToolsPluginNames();
bool isRasterToolsMember(const std::string& pluginName);
std::string displayGroupForPlugin(const std::string& pluginName);
QString rasterToolsGroupDisplayName();
QString rasterToolsGroupDescription();
GroupSelectionText buildGroupSelectionText(const std::string& pluginName,
                                          const std::string& pluginDisplayName,
                                          const std::string& pluginDescription,
                                          int subFunctionCount);
ActionSelectionText buildActionSelectionText(const std::string& displayGroupKey,
                                            const std::string& actionPluginName,
                                            const std::string& pluginDisplayName,
                                            const std::string& pluginDescription,
                                            const std::string& actionKey);
std::vector<std::string> spindexCustomIndexPresetValues();
std::string spindexCustomIndexPresetExpression(const std::string& presetKey);
DerivedOutputUpdate computeDerivedExpressionUpdate(const std::string& currentValue,
                                                   const std::string& lastAutoValue,
                                                   const std::string& pluginName,
                                                   const std::string& action,
                                                   const std::string& presetKey);
DerivedParamSyncResult computeDerivedParamSyncResult(
    const std::string& pluginName,
    const std::string& action,
    const std::string& primaryPath,
    const std::string& formatValue,
    bool hasOutputParam,
    const std::string& currentOutputValue,
    bool hasVectorOutputParam,
    const std::string& currentVectorOutputValue,
    bool hasRasterOutputParam,
    const std::string& currentRasterOutputValue,
    bool hasExpressionParam,
    const std::string& currentExpressionValue,
    const std::string& presetKey,
    bool hasLayerParam,
    const std::string& currentLayerValue,
    bool hasExtentParam,
    const std::optional<std::array<double, 4>>& currentExtentValue,
    const std::string& inputPath,
    const DataAutoFillInfo& inputInfo,
    const DerivedParamTracking& tracking);
DataAutoFillInfo inspectDataForAutoFill(const std::string& path);
QString actionDisplayName(const std::string& pluginName, const std::string& actionKey);
QString actionDescription(const std::string& pluginName, const std::string& actionKey);
const ActionUiConfig* findActionUiConfig(const std::string& pluginName,
                                         const std::string& actionKey);
const ParamText* findCommonParamText(const std::string& paramKey);
const ParamText* findActionSpecificParamText(const std::string& pluginName,
                                             const std::string& actionKey,
                                             const std::string& paramKey);
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
