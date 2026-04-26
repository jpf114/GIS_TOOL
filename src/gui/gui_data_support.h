#pragma once

#include <array>
#include <string>

#include <gis/framework/result.h>

namespace gis::gui {

enum class DataKind {
    Unknown,
    Raster,
    Vector,
};

struct DataAutoFillInfo {
    std::string crs;
    std::string layerName;
    std::array<double, 4> extent{0.0, 0.0, 0.0, 0.0};
    bool hasExtent = false;
};

DataKind detectDataKind(const std::string& path);
bool canPreviewData(const std::string& path);
std::string dataKindDisplayName(DataKind kind);
std::string buildDataDisplayLabel(const std::string& path, DataKind kind, bool isOutput, bool isActive = false);
std::string buildPreviewStatusText(DataKind kind, double scale, bool fitMode, bool isPanning);
std::string buildSuggestedOutputPath(const std::string& inputPath,
                                     const std::string& pluginName,
                                     const std::string& action);
DataAutoFillInfo inspectDataForAutoFill(const std::string& path);
std::string buildResultSummaryText(const gis::framework::Result& result);

double zoomInScale(double currentScale);
double zoomOutScale(double currentScale);
double fitScaleForSize(int contentWidth, int contentHeight, int viewportWidth, int viewportHeight);

} // namespace gis::gui
