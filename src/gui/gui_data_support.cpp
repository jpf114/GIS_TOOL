#include "gui_data_support.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>
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

std::string previewScaleText(gis::gui::DataKind kind, double scale) {
    if (kind == gis::gui::DataKind::Unknown) {
        return "无预览";
    }

    std::ostringstream oss;
    oss << static_cast<int>(std::round(scale * 100.0)) << "%";
    return oss.str();
}

std::string previewModeText(gis::gui::DataKind kind, bool fitMode, bool isPanning) {
    if (kind == gis::gui::DataKind::Unknown) {
        return "等待选择";
    }
    if (isPanning) {
        return "拖拽浏览";
    }
    if (fitMode) {
        return "适配视图";
    }
    return "手动缩放";
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

std::string buildDataDisplayLabel(const std::string& path, DataKind kind, bool isOutput, bool isActive) {
    const std::string role = isOutput ? "输出" : "输入";
    const std::string active = isActive ? "[当前]" : "";
    return "[" + dataKindDisplayName(kind) + "][" + role + "]" + active + " "
        + std::filesystem::path(path).filename().string();
}

std::string buildPreviewStatusText(DataKind kind, double scale, bool fitMode, bool isPanning) {
    return "当前预览: " + dataKindDisplayName(kind)
        + " | 缩放: " + previewScaleText(kind, scale)
        + " | 模式: " + previewModeText(kind, fitMode, isPanning);
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
