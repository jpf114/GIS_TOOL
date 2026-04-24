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

} // namespace gis::gui
