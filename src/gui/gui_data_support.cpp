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

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

bool startsWithOutputKey(const std::string& key) {
    return key == "output" || key.find("output") != std::string::npos;
}

std::string normalizedLower(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

std::string replaceExtensionIfNeeded(const std::filesystem::path& path, const std::string& suffix) {
    if (suffix.empty()) {
        return path.generic_string();
    }
    std::filesystem::path rewritten = path;
    rewritten.replace_extension(suffix);
    return rewritten.generic_string();
}

std::string defaultSuffixForOutput(const std::string& pluginName,
                                   const std::string& action,
                                   const std::string& paramKey,
                                   const std::string& inputExt) {
    if (pluginName == "classification") {
        if (paramKey == "output") return ".json";
        if (paramKey == "vector_output") return ".gpkg";
        if (paramKey == "raster_output") return ".tif";
    }

    if (pluginName == "matching") {
        if (action == "detect" || action == "match" || action == "corner") {
            return ".json";
        }
        return ".tif";
    }

    if (pluginName == "utility") {
        if (action == "histogram") return ".json";
        if (action == "colormap" || action == "ndvi") return ".tif";
        return inputExt;
    }

    if (pluginName == "vector") {
        if (action == "rasterize") return ".tif";
        if (action == "polygonize") return ".gpkg";
        if (action == "convert") return ".geojson";
        return ".gpkg";
    }

    if (pluginName == "projection") {
        if (action == "reproject") {
            if (inputExt == ".shp" || inputExt == ".gpkg" || inputExt == ".geojson" ||
                inputExt == ".json" || inputExt == ".kml" || inputExt == ".csv") {
                return inputExt.empty() ? ".gpkg" : inputExt;
            }
            return ".tif";
        }
        return inputExt;
    }

    if (pluginName == "cutting") {
        if (action == "split") return {};
        return ".tif";
    }

    if (pluginName == "processing") {
        return ".tif";
    }

    return inputExt;
}

std::string filterForVectorOutputs() {
    return "GeoPackage (*.gpkg);;GeoJSON (*.geojson *.json);;Shapefile (*.shp);;KML (*.kml);;CSV (*.csv);;所有文件 (*)";
}

std::string filterForVectorOutputsWithoutCsv() {
    return "GeoPackage (*.gpkg);;GeoJSON (*.geojson *.json);;Shapefile (*.shp);;KML (*.kml);;所有文件 (*)";
}

std::string filterForVectorOutputsWithoutCsvOrKml() {
    return "GeoPackage (*.gpkg);;GeoJSON (*.geojson *.json);;Shapefile (*.shp);;所有文件 (*)";
}

std::string filterForPolygonizeOutputs() {
    return "GeoPackage (*.gpkg);;GeoJSON (*.geojson *.json);;Shapefile (*.shp);;所有文件 (*)";
}

std::string filterForClassificationVectorOutputs() {
    return "GeoPackage (*.gpkg);;所有文件 (*)";
}

std::string filterForVectorInputs() {
    return "矢量文件 (*.gpkg *.shp *.geojson *.json *.kml *.csv);;GeoPackage (*.gpkg);;Shapefile (*.shp);;GeoJSON (*.geojson *.json);;KML (*.kml);;CSV (*.csv);;所有文件 (*)";
}

std::string filterForRasterOutputs() {
    return "GeoTIFF (*.tif *.tiff);;所有文件 (*)";
}

std::string filterForRasterInputs() {
    return "栅格文件 (*.tif *.tiff *.img *.vrt *.png *.jpg *.jpeg *.bmp);;GeoTIFF (*.tif *.tiff);;IMG (*.img);;VRT (*.vrt);;JPEG (*.jpg *.jpeg);;PNG (*.png);;BMP (*.bmp);;所有文件 (*)";
}

std::string filterForProjectionInputs() {
    return "支持的数据 (*.tif *.tiff *.img *.vrt *.png *.jpg *.jpeg *.bmp *.gpkg *.shp *.geojson *.json *.kml *.csv);;栅格文件 (*.tif *.tiff *.img *.vrt *.png *.jpg *.jpeg *.bmp);;矢量文件 (*.gpkg *.shp *.geojson *.json *.kml *.csv);;所有文件 (*)";
}

std::string filterForProjectionOutputs() {
    return "GeoTIFF (*.tif *.tiff);;GeoPackage (*.gpkg);;GeoJSON (*.geojson *.json);;Shapefile (*.shp);;KML (*.kml);;CSV (*.csv);;所有文件 (*)";
}

std::string firstInputPath(const std::string& rawPath) {
    const auto pos = rawPath.find(',');
    if (pos == std::string::npos) {
        return trim(rawPath);
    }
    return trim(rawPath.substr(0, pos));
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
                                     const std::string& action,
                                     const std::string& paramKey) {
    namespace fs = std::filesystem;

    fs::path input = fs::path(firstInputPath(inputPath));
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
    const std::string resolvedSuffix = defaultSuffixForOutput(
        pluginName,
        action,
        paramKey,
        normalizedLower(input.extension().string()));
    if (resolvedSuffix.empty()) {
        return (input.parent_path() / fs::path(input.stem().string() + "_" + suffix)).generic_string();
    }
    return replaceExtensionIfNeeded(suggested, resolvedSuffix);
}

FileParamUiConfig buildFileParamUiConfig(const std::string& pluginName,
                                         const std::string& action,
                                         const std::string& paramKey,
                                         gis::framework::ParamType paramType) {
    FileParamUiConfig config;
    config.isOutput = startsWithOutputKey(paramKey);

    if (paramType == gis::framework::ParamType::CRS) {
        config.placeholder = "请输入 EPSG 代码，例如 EPSG:3857";
        return config;
    }

    if (pluginName == "classification" && paramKey == "class_map") {
        config.placeholder = "请选择 JSON 分类映射文件，例如 class_map.json";
        config.openFilter = "JSON 文件 (*.json);;所有文件 (*)";
        return config;
    }

    if (pluginName == "classification" && paramKey == "rasters") {
        config.placeholder = "请输入多个分类栅格路径，使用英文逗号分隔，例如 a.tif,b.tif";
        config.openFilter = filterForRasterInputs();
        return config;
    }

    if (pluginName == "cutting" && action == "split" && paramKey == "output") {
        config.placeholder = "请选择输出目录，图块会自动命名为 tile_x_y.tif";
        config.selectDirectory = true;
        config.isOutput = true;
        return config;
    }

    if (pluginName == "matching" && action == "stitch" && paramKey == "input") {
        config.placeholder = "请输入多个影像路径，使用英文逗号分隔，例如 a.tif,b.tif";
        config.openFilter = filterForRasterInputs();
        config.allowMultiSelect = true;
        return config;
    }

    if (pluginName == "cutting" &&
        (action == "mosaic" || action == "merge_bands") &&
        paramKey == "input") {
        config.placeholder = "请输入多个栅格路径，使用英文逗号分隔，例如 a.tif,b.tif";
        config.openFilter = filterForRasterInputs();
        config.allowMultiSelect = true;
        return config;
    }

    if (paramKey == "bands" || paramKey == "nodatas") {
        return config;
    }

    if (config.isOutput) {
        config.suggestedSuffix = defaultSuffixForOutput(pluginName, action, paramKey, ".tif");

        if (pluginName == "classification" && paramKey == "output") {
            config.placeholder = "请选择统计输出文件，支持 .json 或 .csv，默认建议 .json";
            config.saveFilter = "JSON 文件 (*.json);;CSV 文件 (*.csv);;所有文件 (*)";
        } else if (pluginName == "classification" && paramKey == "vector_output") {
            config.placeholder = "请选择分类面输出文件，当前实际仅支持 .gpkg";
            config.saveFilter = filterForClassificationVectorOutputs();
        } else if (pluginName == "projection" && action == "reproject") {
            config.placeholder = "请选择重投影输出文件；栅格建议 .tif，矢量建议 .gpkg 或 .geojson";
            config.saveFilter = filterForProjectionOutputs();
        } else if (config.suggestedSuffix == ".json") {
            config.placeholder = "请选择输出文件，建议使用 .json";
            config.saveFilter = "JSON 文件 (*.json);;所有文件 (*)";
        } else if (config.suggestedSuffix == ".csv") {
            config.placeholder = "请选择输出文件，建议使用 .csv";
            config.saveFilter = "CSV 文件 (*.csv);;所有文件 (*)";
        } else if (config.suggestedSuffix == ".gpkg" || config.suggestedSuffix == ".geojson" ||
                   config.suggestedSuffix == ".shp" || config.suggestedSuffix == ".kml") {
            config.placeholder = "请选择输出矢量文件，建议使用 .gpkg";
            if (pluginName == "vector" && action == "convert") {
                config.saveFilter = filterForVectorOutputs();
            } else if (pluginName == "vector" &&
                       (action == "union" || action == "difference" || action == "dissolve")) {
                config.saveFilter = filterForVectorOutputsWithoutCsvOrKml();
            } else if (pluginName == "vector" && action == "polygonize") {
                config.saveFilter = filterForPolygonizeOutputs();
            } else {
                config.saveFilter = filterForVectorOutputsWithoutCsv();
            }
        } else {
            config.placeholder = "请选择输出文件，建议使用 .tif";
            config.saveFilter = filterForRasterOutputs();
        }
        return config;
    }

    if (paramType == gis::framework::ParamType::DirPath) {
        config.selectDirectory = true;
        config.placeholder = "请选择目录";
        return config;
    }

    if (paramKey == "vector" || paramKey.find("vector") != std::string::npos ||
        paramKey == "cutline" || paramKey == "clip_vector" || paramKey == "overlay_vector") {
        config.placeholder = "请选择矢量文件，例如 .gpkg、.shp、.geojson";
        config.openFilter = filterForVectorInputs();
        return config;
    }

    if (pluginName == "projection" && action == "reproject" && paramKey == "input") {
        config.placeholder = "请选择待重投影数据，支持栅格或矢量";
        config.openFilter = filterForProjectionInputs();
        return config;
    }

    if (paramKey == "input" || paramKey == "reference" || paramKey == "template_file" ||
        paramKey == "pan_file" || paramKey == "marker_input") {
        if (pluginName == "vector" && action != "polygonize") {
            config.placeholder = "请选择矢量文件，例如 .gpkg、.shp、.geojson";
            config.openFilter = filterForVectorInputs();
            return config;
        }
        if (paramKey == "template_file") {
            config.placeholder = "请选择模板影像，尺寸需小于等于输入影像";
        } else if (paramKey == "marker_input") {
            config.placeholder = "请选择标记栅格，0 表示背景，1/2/3 表示不同种子区域";
        } else if (paramKey == "pan_file") {
            config.placeholder = "请选择全色影像，建议与输入多光谱影像覆盖同一区域";
        } else if (paramKey == "reference") {
            config.placeholder = "请选择参考影像";
        } else {
            config.placeholder = "请选择栅格文件，例如 .tif、.img、.vrt";
        }
        config.openFilter = filterForRasterInputs();
        return config;
    }

    config.placeholder = "请选择文件或输入路径";
    return config;
}

std::string buildTextParamPlaceholder(const std::string& pluginName,
                                      const std::string& action,
                                      const gis::framework::ParamSpec& spec) {
    if (pluginName == "classification") {
        if (spec.key == "rasters") {
            return "请输入多个分类栅格路径，英文逗号分隔，例如 D:/a.tif,D:/b.tif";
        }
        if (spec.key == "bands") {
            return "与分类栅格一一对应，英文逗号分隔，例如 1,1,1";
        }
        if (spec.key == "nodatas") {
            return "与分类栅格一一对应，英文逗号分隔，例如 0,0,255";
        }
    }

    if (pluginName == "cutting" && action == "merge_bands") {
        if (spec.key == "input") {
            return "可输入一个或多个单波段栅格路径，英文逗号分隔";
        }
        if (spec.key == "bands") {
            return "补充更多单波段栅格路径，英文逗号分隔，例如 band1.tif,band2.tif";
        }
    }

    if (pluginName == "vector" && action == "filter" && spec.key == "where") {
        return "请输入 SQL WHERE 条件，例如 population > 10000";
    }

    if (pluginName == "projection" && action == "transform") {
        if (spec.key == "x") {
            return "请输入待转换点的 X 坐标";
        }
        if (spec.key == "y") {
            return "请输入待转换点的 Y 坐标";
        }
    }

    if (pluginName == "utility" && action == "overviews" && spec.key == "levels") {
        return "请输入空格分隔层级，例如 2 4 8 16";
    }

    if (pluginName == "processing" && action == "band_math" && spec.key == "expression") {
        return "请输入表达式，例如 B1+B2 或 B1*0.5+B2*0.5";
    }

    if (spec.key == "bands") {
        return "请输入英文逗号分隔列表，例如 1,1,1";
    }
    if (spec.key == "nodatas") {
        return "请输入英文逗号分隔列表，例如 0,0,255";
    }

    return spec.description;
}

DataAutoFillInfo inspectDataForAutoFill(const std::string& path) {
    DataAutoFillInfo info;
    const std::string normalizedPath = firstInputPath(path);
    const DataKind kind = detectDataKind(normalizedPath);

    if (kind == DataKind::Raster) {
        std::unique_ptr<GDALDataset, DatasetCloser> ds(
            static_cast<GDALDataset*>(GDALOpen(normalizedPath.c_str(), GA_ReadOnly)));
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
            static_cast<GDALDataset*>(GDALOpenEx(normalizedPath.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                                 nullptr, nullptr, nullptr)));
        if (!ds) {
            return info;
        }

        auto* layer = ds->GetLayer(0);
        if (!layer) {
            return info;
        }

        if (lowerExtension(normalizedPath) != ".shp") {
            info.layerName = layer->GetName();
        }
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


