#pragma once

#include <array>
#include <map>
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
                                     const std::string& action);
DataAutoFillInfo inspectDataForAutoFill(const std::string& path);
std::string localizeResultMessage(const std::string& message);
std::string buildResultSummaryText(const gis::framework::Result& result);
std::string validateExecutionParams(
    const std::vector<gis::framework::ParamSpec>& specs,
    const std::map<std::string, gis::framework::ParamValue>& params);
std::string findFirstInvalidParamKey(
    const std::vector<gis::framework::ParamSpec>& specs,
    const std::map<std::string, gis::framework::ParamValue>& params);
std::vector<BindableParamOption> collectBindableParamOptions(
    const std::vector<gis::framework::ParamSpec>& specs,
    DataKind dataKind);

} // namespace gis::gui
