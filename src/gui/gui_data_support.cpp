#include "gui_data_support.h"

#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>

#include <gdal_priv.h>
#include <opencv2/imgproc.hpp>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

namespace {

struct DatasetCloser {
    void operator()(GDALDataset* ds) const {
        if (ds) {
            GDALClose(ds);
        }
    }
};

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

std::string sanitizeSuffixPart(const std::string& value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            sanitized.push_back(static_cast<char>(std::tolower(ch)));
        } else if (ch == '-' || ch == '_') {
            sanitized.push_back('_');
        }
    }
    return sanitized;
}

std::string spatialReferenceText(const OGRSpatialReference* srs) {
    if (!srs) {
        return {};
    }

    const char* authName = srs->GetAuthorityName(nullptr);
    const char* authCode = srs->GetAuthorityCode(nullptr);
    if (authName && authCode) {
        return std::string(authName) + ":" + authCode;
    }

    char* wkt = nullptr;
    OGRSpatialReference cloned(*srs);
    if (cloned.exportToWkt(&wkt) != OGRERR_NONE || !wkt) {
        return {};
    }

    std::string text = wkt;
    CPLFree(wkt);
    return text;
}

std::array<double, 4> envelopeToExtent(const OGREnvelope& envelope) {
    return {envelope.MinX, envelope.MinY, envelope.MaxX, envelope.MaxY};
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

bool isLikelyVectorParamKey(const std::string& key) {
    static const std::vector<std::string> hints = {
        "vector", "shape", "shp", "overlay", "clip"
    };
    for (const auto& hint : hints) {
        if (key.find(hint) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool isLikelyRasterParamKey(const std::string& key) {
    static const std::vector<std::string> hints = {
        "reference", "template", "pan", "raster", "image", "img"
    };
    for (const auto& hint : hints) {
        if (key.find(hint) != std::string::npos) {
            return true;
        }
    }
    return false;
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

bool isSupportedDataPath(const std::string& path) {
    return detectDataKind(path) != DataKind::Unknown;
}

std::vector<std::string> collectSupportedDataPaths(const std::vector<std::string>& paths) {
    std::vector<std::string> supported;
    supported.reserve(paths.size());
    for (const auto& path : paths) {
        if (isSupportedDataPath(path)) {
            supported.push_back(path);
        }
    }
    return supported;
}

std::vector<std::string> collectSupportedDataPathsRecursively(const std::vector<std::string>& paths) {
    namespace fs = std::filesystem;

    std::vector<std::string> supported;
    std::unordered_set<std::string> seen;

    auto appendIfSupported = [&](const fs::path& path) {
        if (!path.has_filename()) {
            return;
        }
        const std::string normalized = path.lexically_normal().generic_string();
        if (!isSupportedDataPath(normalized)) {
            return;
        }
        if (seen.insert(normalized).second) {
            supported.push_back(normalized);
        }
    };

    for (const auto& rawPath : paths) {
        if (rawPath.empty()) {
            continue;
        }

        fs::path path(rawPath);
        std::error_code ec;
        const fs::file_status status = fs::status(path, ec);
        if (ec) {
            continue;
        }

        if (fs::is_regular_file(status)) {
            appendIfSupported(path);
            continue;
        }

        if (!fs::is_directory(status)) {
            continue;
        }

        fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
        fs::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            if (ec || !it->is_regular_file()) {
                continue;
            }
            appendIfSupported(it->path());
        }
    }

    std::sort(supported.begin(), supported.end());
    return supported;
}

bool canPreviewData(const std::string& path) {
    return isSupportedDataPath(path);
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

std::string dataOriginDisplayName(DataOrigin origin) {
    switch (origin) {
        case DataOrigin::Input:
            return "输入";
        case DataOrigin::Output:
            return "正式结果";
        case DataOrigin::QuickPreview:
            return "预览影像";
        case DataOrigin::QuickRun:
            return "快速试算";
        default:
            return "未知";
    }
}

bool isOutputDataOrigin(DataOrigin origin) {
    return origin != DataOrigin::Input;
}

std::string buildDataDisplayLabel(const std::string& path,
                                  DataKind kind,
                                  DataOrigin origin,
                                  bool isActive) {
    const std::string active = isActive ? "[当前]" : "";
    return "[" + dataKindDisplayName(kind) + "][" + dataOriginDisplayName(origin) + "]" + active + " "
        + std::filesystem::path(path).filename().string();
}

std::string buildPreviewStatusText(DataKind kind, double scale, bool fitMode, bool isPanning) {
    return "当前预览: " + dataKindDisplayName(kind)
        + " | 缩放: " + previewScaleText(kind, scale)
        + " | 模式: " + previewModeText(kind, fitMode, isPanning);
}

std::string buildSuggestedOutputPath(const std::string& inputPath,
                                     const std::string& pluginName,
                                     const std::string& action) {
    namespace fs = std::filesystem;

    fs::path input = fs::path(inputPath);
    if (input.empty()) {
        return {};
    }

    const std::string pluginSuffix = sanitizeSuffixPart(pluginName);
    const std::string actionSuffix = sanitizeSuffixPart(action);

    std::string suffix = "result";
    if (!pluginSuffix.empty()) {
        suffix = pluginSuffix;
        if (!actionSuffix.empty()) {
            suffix += "_" + actionSuffix;
        }
    } else if (!actionSuffix.empty()) {
        suffix = actionSuffix;
    }

    const fs::path suggested = input.parent_path() /
        fs::path(input.stem().string() + "_" + suffix + input.extension().string());
    return suggested.generic_string();
}

std::string buildQuickPreviewOutputPath(const std::string& inputPath) {
    namespace fs = std::filesystem;

    fs::path input = fs::path(inputPath);
    if (input.empty()) {
        return {};
    }

    const fs::path output = input.parent_path() /
        fs::path(input.stem().string() + "_preview8" + input.extension().string());
    return output.generic_string();
}

std::string buildQuickPreviewResultPath(const std::string& inputPath,
                                        const std::string& pluginName,
                                        const std::string& action) {
    namespace fs = std::filesystem;

    fs::path input = fs::path(inputPath);
    if (input.empty()) {
        return {};
    }

    std::string suffix = sanitizeSuffixPart(pluginName);
    const std::string actionSuffix = sanitizeSuffixPart(action);
    if (!actionSuffix.empty()) {
        suffix += suffix.empty() ? actionSuffix : "_" + actionSuffix;
    }
    if (suffix.empty()) {
        suffix = "preview";
    } else {
        suffix += "_preview";
    }

    const fs::path output = input.parent_path() /
        fs::path(input.stem().string() + "_" + suffix + input.extension().string());
    return output.generic_string();
}

bool buildQuickPreviewRaster(const std::string& inputPath,
                             const std::string& outputPath,
                             int maxLongEdge) {
    if (detectDataKind(inputPath) != DataKind::Raster || outputPath.empty() || maxLongEdge <= 0) {
        return false;
    }

    auto srcDs = gis::core::openRaster(inputPath, true);
    if (!srcDs) {
        return false;
    }

    cv::Mat band = gis::core::gdalBandToMat(srcDs.get(), 1);
    if (band.empty()) {
        return false;
    }

    const int srcWidth = band.cols;
    const int srcHeight = band.rows;
    const int longEdge = std::max(srcWidth, srcHeight);
    const double scale = longEdge > maxLongEdge
        ? static_cast<double>(maxLongEdge) / static_cast<double>(longEdge)
        : 1.0;

    cv::Mat resized = band;
    if (scale < 1.0) {
        const cv::Size targetSize(
            std::max(1, static_cast<int>(std::round(srcWidth * scale))),
            std::max(1, static_cast<int>(std::round(srcHeight * scale))));
        cv::resize(band, resized, targetSize, 0.0, 0.0, cv::INTER_AREA);
    }

    cv::Mat preview = gis::core::toUint8(resized);
    auto dstDs = gis::core::createRaster(outputPath, preview.cols, preview.rows, 1, GDT_Byte);
    if (!dstDs) {
        return false;
    }

    double gt[6] = {};
    if (srcDs->GetGeoTransform(gt) == CE_None) {
        const double scaleX = static_cast<double>(srcWidth) / static_cast<double>(preview.cols);
        const double scaleY = static_cast<double>(srcHeight) / static_cast<double>(preview.rows);
        gt[1] *= scaleX;
        gt[2] *= scaleY;
        gt[4] *= scaleX;
        gt[5] *= scaleY;
        dstDs->SetGeoTransform(gt);
    }

    const std::string projection = gis::core::getSRSWKT(srcDs.get());
    if (!projection.empty()) {
        dstDs->SetProjection(projection.c_str());
    }

    auto* srcBand = srcDs->GetRasterBand(1);
    auto* dstBand = dstDs->GetRasterBand(1);
    if (!srcBand || !dstBand) {
        return false;
    }

    int hasNoData = 0;
    const double noDataValue = srcBand->GetNoDataValue(&hasNoData);
    if (hasNoData) {
        dstBand->SetNoDataValue(noDataValue);
    }

    return dstBand->RasterIO(
        GF_Write, 0, 0, preview.cols, preview.rows,
        preview.ptr<uint8_t>(), preview.cols, preview.rows, GDT_Byte, 0, 0) == CE_None;
}

bool buildQuickPreviewExecutionParams(
    const std::vector<gis::framework::ParamSpec>& specs,
    const std::map<std::string, gis::framework::ParamValue>& params,
    const std::string& pluginName,
    const std::string& action,
    std::map<std::string, gis::framework::ParamValue>& outParams,
    int maxLongEdge) {
    outParams = params;

    std::string primaryInput;
    if (auto it = params.find("input"); it != params.end()) {
        if (const auto* text = std::get_if<std::string>(&it->second)) {
            primaryInput = *text;
        }
    }
    for (const auto& spec : specs) {
        if (spec.type != gis::framework::ParamType::FilePath) {
            continue;
        }

        auto it = outParams.find(spec.key);
        if (it == outParams.end()) {
            continue;
        }

        auto* text = std::get_if<std::string>(&it->second);
        if (!text || text->empty()) {
            continue;
        }

        if (spec.key == "output") {
            continue;
        }

        if (detectDataKind(*text) != DataKind::Raster) {
            continue;
        }

        const std::string originalPath = *text;

        const std::string previewPath = buildQuickPreviewOutputPath(originalPath);
        if (!buildQuickPreviewRaster(originalPath, previewPath, maxLongEdge)) {
            return false;
        }
        it->second = previewPath;
    }

    auto outputIt = outParams.find("output");
    if (outputIt != outParams.end() && !primaryInput.empty()) {
        outputIt->second = buildQuickPreviewResultPath(primaryInput, pluginName, action);
    }

    return true;
}

bool canBuildQuickPreviewExecution(
    const std::vector<gis::framework::ParamSpec>& specs,
    const std::map<std::string, gis::framework::ParamValue>& params) {
    for (const auto& spec : specs) {
        if (spec.type != gis::framework::ParamType::FilePath || spec.key == "output") {
            continue;
        }

        auto it = params.find(spec.key);
        if (it == params.end()) {
            continue;
        }

        const auto* text = std::get_if<std::string>(&it->second);
        if (!text || text->empty()) {
            continue;
        }

        if (detectDataKind(*text) == DataKind::Raster) {
            return true;
        }
    }

    return false;
}

DataAutoFillInfo inspectDataForAutoFill(const std::string& path) {
    DataAutoFillInfo info;
    const DataKind kind = detectDataKind(path);

    if (kind == DataKind::Raster) {
        std::unique_ptr<GDALDataset, DatasetCloser> ds(
            static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly)));
        if (!ds) {
            return info;
        }

        info.crs = spatialReferenceText(ds->GetSpatialRef());

        double gt[6] = {};
        if (ds->GetGeoTransform(gt) == CE_None) {
            const double minX = gt[0];
            const double maxY = gt[3];
            const double maxX = gt[0] + gt[1] * ds->GetRasterXSize() + gt[2] * ds->GetRasterYSize();
            const double minY = gt[3] + gt[4] * ds->GetRasterXSize() + gt[5] * ds->GetRasterYSize();
            info.extent = {
                std::min(minX, maxX),
                std::min(minY, maxY),
                std::max(minX, maxX),
                std::max(minY, maxY)
            };
            info.hasExtent = true;
        }
        return info;
    }

    if (kind == DataKind::Vector) {
        std::unique_ptr<GDALDataset, DatasetCloser> ds(
            static_cast<GDALDataset*>(GDALOpenEx(path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                                 nullptr, nullptr, nullptr)));
        if (!ds) {
            return info;
        }

        auto* layer = ds->GetLayer(0);
        if (!layer) {
            return info;
        }

        info.layerName = layer->GetName();
        info.crs = spatialReferenceText(layer->GetSpatialRef());

        OGREnvelope envelope{};
        if (layer->GetExtent(&envelope, TRUE) == OGRERR_NONE) {
            info.extent = envelopeToExtent(envelope);
            info.hasExtent = true;
        }
    }

    return info;
}

std::string buildResultSummaryText(const gis::framework::Result& result) {
    std::ostringstream oss;
    oss << "状态: " << (result.success ? "成功" : "失败") << "\n";
    oss << "消息: " << result.message;

    if (!result.outputPath.empty()) {
        oss << "\n输出: " << result.outputPath;
    }

    if (!result.metadata.empty()) {
        oss << "\n元数据:";
        for (const auto& [key, value] : result.metadata) {
            oss << "\n- " << key << ": " << value;
        }
    }

    return oss.str();
}

std::string validateExecutionParams(
    const std::vector<gis::framework::ParamSpec>& specs,
    const std::map<std::string, gis::framework::ParamValue>& params) {
    auto displayNameFor = [](const gis::framework::ParamSpec& spec) {
        return spec.displayName.empty() ? spec.key : spec.displayName;
    };

    for (const auto& spec : specs) {
        auto it = params.find(spec.key);
        const auto issuePrefix = "参数“" + displayNameFor(spec) + "”";

        if (it == params.end()) {
            if (spec.required) {
                return issuePrefix + "不能为空";
            }
            continue;
        }

        switch (spec.type) {
            case gis::framework::ParamType::String:
            case gis::framework::ParamType::FilePath:
            case gis::framework::ParamType::DirPath:
            case gis::framework::ParamType::Enum:
            case gis::framework::ParamType::CRS: {
                const auto* text = std::get_if<std::string>(&it->second);
                if (spec.required && (!text || text->empty())) {
                    return issuePrefix + "不能为空";
                }
                break;
            }
            case gis::framework::ParamType::Int: {
                const auto* value = std::get_if<int>(&it->second);
                if (!value) {
                    return issuePrefix + "类型无效";
                }

                const auto* minValue = std::get_if<int>(&spec.minValue);
                const auto* maxValue = std::get_if<int>(&spec.maxValue);
                if (minValue && maxValue && *maxValue > *minValue &&
                    (*value < *minValue || *value > *maxValue)) {
                    std::ostringstream oss;
                    oss << issuePrefix << "超出范围 [" << *minValue << ", " << *maxValue << "]";
                    return oss.str();
                }
                break;
            }
            case gis::framework::ParamType::Double: {
                const auto* value = std::get_if<double>(&it->second);
                if (!value) {
                    return issuePrefix + "类型无效";
                }

                const auto* minValue = std::get_if<double>(&spec.minValue);
                const auto* maxValue = std::get_if<double>(&spec.maxValue);
                if (minValue && maxValue && *maxValue > *minValue &&
                    (*value < *minValue || *value > *maxValue)) {
                    std::ostringstream oss;
                    oss << issuePrefix << "超出范围 [" << *minValue << ", " << *maxValue << "]";
                    return oss.str();
                }
                break;
            }
            case gis::framework::ParamType::Bool:
            case gis::framework::ParamType::Extent:
                break;
        }
    }

    return {};
}

std::string findFirstInvalidParamKey(
    const std::vector<gis::framework::ParamSpec>& specs,
    const std::map<std::string, gis::framework::ParamValue>& params) {
    for (const auto& spec : specs) {
        auto it = params.find(spec.key);
        if (it == params.end()) {
            if (spec.required) {
                return spec.key;
            }
            continue;
        }

        switch (spec.type) {
            case gis::framework::ParamType::String:
            case gis::framework::ParamType::FilePath:
            case gis::framework::ParamType::DirPath:
            case gis::framework::ParamType::Enum:
            case gis::framework::ParamType::CRS: {
                const auto* text = std::get_if<std::string>(&it->second);
                if (spec.required && (!text || text->empty())) {
                    return spec.key;
                }
                break;
            }
            case gis::framework::ParamType::Int: {
                const auto* value = std::get_if<int>(&it->second);
                if (!value) {
                    return spec.key;
                }
                const auto* minValue = std::get_if<int>(&spec.minValue);
                const auto* maxValue = std::get_if<int>(&spec.maxValue);
                if (minValue && maxValue && *maxValue > *minValue &&
                    (*value < *minValue || *value > *maxValue)) {
                    return spec.key;
                }
                break;
            }
            case gis::framework::ParamType::Double: {
                const auto* value = std::get_if<double>(&it->second);
                if (!value) {
                    return spec.key;
                }
                const auto* minValue = std::get_if<double>(&spec.minValue);
                const auto* maxValue = std::get_if<double>(&spec.maxValue);
                if (minValue && maxValue && *maxValue > *minValue &&
                    (*value < *minValue || *value > *maxValue)) {
                    return spec.key;
                }
                break;
            }
            case gis::framework::ParamType::Bool:
            case gis::framework::ParamType::Extent:
                break;
        }
    }

    return {};
}

std::vector<BindableParamOption> collectBindableParamOptions(
    const std::vector<gis::framework::ParamSpec>& specs,
    DataKind dataKind) {
    std::vector<BindableParamOption> options;
    for (const auto& spec : specs) {
        if (spec.type != gis::framework::ParamType::FilePath) {
            continue;
        }
        if (spec.key == "input" || spec.key == "output") {
            continue;
        }

        const bool looksVector = isLikelyVectorParamKey(spec.key);
        const bool looksRaster = isLikelyRasterParamKey(spec.key);
        if (dataKind == DataKind::Vector && looksRaster && !looksVector) {
            continue;
        }
        if (dataKind == DataKind::Raster && looksVector && !looksRaster) {
            continue;
        }

        options.push_back({spec.key, spec.displayName.empty() ? spec.key : spec.displayName});
    }
    return options;
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
