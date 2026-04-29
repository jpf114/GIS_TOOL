#include "gui_data_support.h"

#include <gis/core/gdal_wrapper.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <cctype>
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
            return "输出结果";
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

std::string localizeResultMessage(const std::string& message) {
    static const std::map<std::string, std::string> kKnownMessages = {
        {"Processing completed successfully", "处理完成"},
        {"Histogram computed", "直方图计算完成"},
        {"Overviews built successfully", "金字塔构建完成"},
        {"NoData updated successfully", "NoData 设置完成"},
        {"Color map applied successfully", "伪彩色处理完成"},
        {"NDVI computed successfully", "NDVI 计算完成"},
        {"Vector buffer completed successfully", "缓冲区处理完成"},
        {"Vector conversion completed successfully", "格式转换完成"},
        {"Vector dissolve completed successfully", "融合完成"},
        {"Vector clip completed successfully", "矢量裁切完成"},
        {"Vector union completed successfully", "并集处理完成"},
        {"Vector difference completed successfully", "差集处理完成"},
    };

    const auto it = kKnownMessages.find(message);
    if (it != kKnownMessages.end()) {
        return it->second;
    }

    if (message.rfind("==== Raster Info:", 0) == 0) {
        return "栅格信息读取完成";
    }
    if (message.rfind("==== Vector Info:", 0) == 0) {
        return "矢量信息读取完成";
    }

    return message;
}

std::string buildResultSummaryText(const gis::framework::Result& result) {
    std::ostringstream oss;
    oss << "状态: " << (result.success ? "成功" : "失败") << "\n";
    oss << "消息: " << localizeResultMessage(result.message);

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
    return gis::framework::validateParams(specs, params);
}

std::string findFirstInvalidParamKey(
    const std::vector<gis::framework::ParamSpec>& specs,
    const std::map<std::string, gis::framework::ParamValue>& params) {
    return gis::framework::findFirstInvalidParamKey(specs, params);
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

} // namespace gis::gui


