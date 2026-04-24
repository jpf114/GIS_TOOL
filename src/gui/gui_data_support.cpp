#include "gui_data_support.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_set>

namespace {

constexpr double kMinScale = 0.1;
constexpr double kMaxScale = 8.0;
constexpr double kZoomStep = 1.25;

std::string lowerExtension(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext;
}

double clampScale(double value) {
    return std::clamp(value, kMinScale, kMaxScale);
}

} // namespace

namespace gis::gui {

DataKind detectDataKind(const std::string& path) {
    static const std::unordered_set<std::string> rasterExts = {
        ".tif", ".tiff", ".img", ".vrt", ".png", ".jpg", ".jpeg", ".bmp"
    };
    static const std::unordered_set<std::string> vectorExts = {
        ".shp", ".geojson", ".json", ".gpkg", ".kml", ".csv"
    };

    const std::string ext = lowerExtension(path);
    if (rasterExts.count(ext) > 0) {
        return DataKind::Raster;
    }
    if (vectorExts.count(ext) > 0) {
        return DataKind::Vector;
    }
    return DataKind::Unknown;
}

bool canPreviewData(const std::string& path) {
    return detectDataKind(path) != DataKind::Unknown;
}

std::string dataKindDisplayName(DataKind kind) {
    switch (kind) {
        case DataKind::Raster:
            return "栅格";
        case DataKind::Vector:
            return "矢量";
        default:
            return "未知";
    }
}

std::string buildDataDisplayLabel(const std::string& path, DataKind kind, bool isOutput) {
    const std::string role = isOutput ? "输出" : "输入";
    return "[" + dataKindDisplayName(kind) + "][" + role + "] "
        + std::filesystem::path(path).filename().string();
}

double zoomInScale(double currentScale) {
    return clampScale(currentScale * kZoomStep);
}

double zoomOutScale(double currentScale) {
    return clampScale(currentScale / kZoomStep);
}

double fitScaleForSize(int contentWidth, int contentHeight, int viewportWidth, int viewportHeight) {
    if (contentWidth <= 0 || contentHeight <= 0 || viewportWidth <= 0 || viewportHeight <= 0) {
        return 1.0;
    }

    const double widthScale = static_cast<double>(viewportWidth) / static_cast<double>(contentWidth);
    const double heightScale = static_cast<double>(viewportHeight) / static_cast<double>(contentHeight);
    return clampScale(std::min({widthScale, heightScale, 1.0}));
}

} // namespace gis::gui
