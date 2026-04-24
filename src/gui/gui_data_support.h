#pragma once

#include <string>

namespace gis::gui {

enum class DataKind {
    Unknown,
    Raster,
    Vector,
};

DataKind detectDataKind(const std::string& path);
bool canPreviewData(const std::string& path);
std::string dataKindDisplayName(DataKind kind);
std::string buildDataDisplayLabel(const std::string& path, DataKind kind, bool isOutput);

double zoomInScale(double currentScale);
double zoomOutScale(double currentScale);
double fitScaleForSize(int contentWidth, int contentHeight, int viewportWidth, int viewportHeight);

} // namespace gis::gui
