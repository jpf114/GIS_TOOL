#include "gui_data_support.h"

#include "custom_index_preset_store.h"

#include <gis/core/spindex_presets.h>
#include <gis/core/gdal_wrapper.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
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

    if (pluginName == "raster_render") {
        if (action == "colormap") return ".tif";
        return inputExt;
    }

    if (pluginName == "raster_manage") {
        return inputExt;
    }

    if (pluginName == "terrain") {
        if (action == "profile_extract") {
            return ".csv";
        }
        return ".tif";
    }

    if (pluginName == "raster_inspect") {
        if (action == "histogram") return ".json";
        return inputExt;
    }

    if (pluginName == "spindex") {
        if (action == "ndvi" || action == "evi" || action == "savi" ||
            action == "gndvi" || action == "ndwi" || action == "mndwi" ||
            action == "ndbi" || action == "arvi" || action == "nbr" ||
            action == "awei" || action == "ui" ||
            action == "custom_index") return ".tif";
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

std::string lowerString(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

bool isZeroExtent(const std::array<double, 4>& extent) {
    return extent[0] == 0.0 && extent[1] == 0.0 && extent[2] == 0.0 && extent[3] == 0.0;
}

std::vector<std::string> splitCommaList(const std::string& rawText) {
    std::vector<std::string> items;
    std::istringstream iss(rawText);
    std::string item;
    while (std::getline(iss, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            items.push_back(item);
        }
    }
    return items;
}

bool endsWithOneOf(const std::string& path, const std::vector<std::string>& suffixes) {
    const std::string lowerPath = lowerString(path);
    for (const auto& suffix : suffixes) {
        if (lowerPath.size() >= suffix.size() &&
            lowerPath.compare(lowerPath.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
        }
    }
    return false;
}

std::optional<std::array<double, 4>> extentParamValue(
    const std::map<std::string, gis::framework::ParamValue>& params,
    const std::string& key) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return std::nullopt;
    }
    if (const auto* arr = std::get_if<std::array<double, 4>>(&it->second)) {
        return *arr;
    }
    return std::nullopt;
}

std::optional<double> doubleParamValue(
    const std::map<std::string, gis::framework::ParamValue>& params,
    const std::string& key) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return std::nullopt;
    }
    if (const auto* value = std::get_if<double>(&it->second)) {
        return *value;
    }
    if (const auto* value = std::get_if<int>(&it->second)) {
        return static_cast<double>(*value);
    }
    return std::nullopt;
}

std::optional<int> intParamValue(
    const std::map<std::string, gis::framework::ParamValue>& params,
    const std::string& key) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return std::nullopt;
    }
    if (const auto* value = std::get_if<int>(&it->second)) {
        return *value;
    }
    return std::nullopt;
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

DerivedOutputUpdate computeDerivedOutputUpdate(const std::string& currentValue,
                                               const std::string& lastAutoValue,
                                               const std::string& primaryPath,
                                               const std::string& pluginName,
                                               const std::string& action,
                                               const std::string& paramKey,
                                               const std::string& formatValue) {
    DerivedOutputUpdate update;
    if (primaryPath.empty()) {
        return update;
    }

    std::string suggestedValue =
        buildSuggestedOutputPath(primaryPath, pluginName, action, paramKey);
    if (pluginName == "vector" && action == "convert" && paramKey == "output") {
        std::string preferredSuffix = ".geojson";
        if (formatValue == "ESRI Shapefile") preferredSuffix = ".shp";
        else if (formatValue == "GPKG") preferredSuffix = ".gpkg";
        else if (formatValue == "KML") preferredSuffix = ".kml";
        else if (formatValue == "CSV") preferredSuffix = ".csv";

        std::filesystem::path suggestedPath = std::filesystem::path(suggestedValue);
        suggestedPath.replace_extension(preferredSuffix);
        suggestedValue = suggestedPath.generic_string();
    }

    const bool valueWasAuto = !lastAutoValue.empty() && currentValue == lastAutoValue;
    update.value = suggestedValue;
    update.autoValue = suggestedValue;
    update.shouldApply = (currentValue.empty() || valueWasAuto) && currentValue != suggestedValue;
    return update;
}

bool shouldAutoFillLayerValue(const std::string& currentValue,
                              const std::string& lastAutoValue,
                              const std::string& suggestedValue) {
    if (suggestedValue.empty()) {
        return false;
    }
    const bool valueWasAuto = !lastAutoValue.empty() && currentValue == lastAutoValue;
    return (currentValue.empty() || valueWasAuto) && currentValue != suggestedValue;
}

bool shouldAutoFillExtentValue(const std::optional<std::array<double, 4>>& currentValue,
                               const std::optional<std::array<double, 4>>& lastAutoValue,
                               bool hasSuggestedExtent) {
    if (!hasSuggestedExtent) {
        return false;
    }
    const bool extentWasAuto = currentValue.has_value() && lastAutoValue.has_value()
        && *currentValue == *lastAutoValue;
    return (!currentValue.has_value() || isZeroExtent(*currentValue)) || extentWasAuto;
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

    if (pluginName == "raster_manage" && action == "overviews" && spec.key == "levels") {
        return "请输入空格分隔层级，例如 2 4 8 16";
    }

    if (pluginName == "raster_math" && action == "band_math" && spec.key == "expression") {
        return "请输入表达式，例如 B1+B2 或 B1*0.5+B2*0.5";
    }
    if (pluginName == "spindex" && action == "custom_index" && spec.key == "expression") {
        return "请输入指数表达式，例如 (NIR-RED)/(NIR+RED) 或 (B4-B1)/(B4+B1)";
    }

    if (spec.key == "bands") {
        return "请输入英文逗号分隔列表，例如 1,1,1";
    }
    if (spec.key == "nodatas") {
        return "请输入英文逗号分隔列表，例如 0,0,255";
    }

    return spec.description;
}

std::vector<std::string> spindexCustomIndexPresetValues() {
    auto values = gis::core::spindexCustomIndexPresetValues();
    for (const auto& preset : loadCustomIndexUserPresets()) {
        values.push_back(preset.key);
    }
    return values;
}

std::string spindexCustomIndexPresetExpression(const std::string& presetKey) {
    const std::string builtinExpression = gis::core::spindexCustomIndexPresetExpression(presetKey);
    if (!builtinExpression.empty()) {
        return builtinExpression;
    }
    return findCustomIndexUserPresetExpression(presetKey);
}

DerivedOutputUpdate computeDerivedExpressionUpdate(const std::string& currentValue,
                                                   const std::string& lastAutoValue,
                                                   const std::string& pluginName,
                                                   const std::string& action,
                                                   const std::string& presetKey) {
    DerivedOutputUpdate update;
    if (pluginName != "spindex" || action != "custom_index") {
        return update;
    }

    const std::string suggestedValue = spindexCustomIndexPresetExpression(presetKey);
    if (suggestedValue.empty()) {
        return update;
    }

    const bool valueWasAuto = !lastAutoValue.empty() && currentValue == lastAutoValue;
    update.value = suggestedValue;
    update.autoValue = suggestedValue;
    update.shouldApply = (currentValue.empty() || valueWasAuto) && currentValue != suggestedValue;
    return update;
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

std::optional<ActionValidationIssue> validateActionSpecificParams(
    const std::string& pluginName,
    const std::string& actionKey,
    const std::map<std::string, gis::framework::ParamValue>& params) {
    auto stringParam = [&](const std::string& key) {
        const auto it = params.find(key);
        if (it == params.end()) {
            return std::string{};
        }
        if (const auto* value = std::get_if<std::string>(&it->second)) {
            return *value;
        }
        return std::string{};
    };

    if (pluginName == "cutting" && actionKey == "clip") {
        const auto extent = extentParamValue(params, "extent");
        if ((!extent.has_value() || isZeroExtent(*extent)) && stringParam("cutline").empty()) {
            return ActionValidationIssue{"extent", "参数“裁切范围”或“裁切矢量”至少填写一个"};
        }
    }

    if (pluginName == "cutting" && actionKey == "merge_bands") {
        if (stringParam("input").empty() && stringParam("bands").empty()) {
            return ActionValidationIssue{"input", "参数“输入文件”或“波段列表”至少填写一个"};
        }
    }

    if (pluginName == "cutting" && actionKey == "mosaic") {
        if (splitCommaList(stringParam("input")).size() < 2) {
            return ActionValidationIssue{"input", "参数“输入文件”至少需要 2 个影像路径"};
        }
    }

    if (pluginName == "cutting" && actionKey == "split") {
        const std::string outputPath = stringParam("output");
        if (endsWithOneOf(outputPath, {".tif", ".tiff", ".img", ".vrt", ".png", ".jpg", ".jpeg", ".bmp"})) {
            return ActionValidationIssue{"output", "参数“输出目录”应填写目录，不应填写单个栅格文件名"};
        }
    }

    if (pluginName == "vector" && actionKey == "filter") {
        const auto extent = extentParamValue(params, "extent");
        if (stringParam("where").empty() && (!extent.has_value() || isZeroExtent(*extent))) {
            return ActionValidationIssue{"where", "参数“属性过滤”或“空间范围”至少填写一个"};
        }
    }

    if (pluginName == "matching" && actionKey == "stitch") {
        if (splitCommaList(stringParam("input")).size() < 2) {
            return ActionValidationIssue{"input", "参数“输入文件”至少需要 2 个影像路径"};
        }
    }

    if (pluginName == "projection" && actionKey == "reproject") {
        const std::string inputPath = stringParam("input");
        const std::string outputPath = stringParam("output");
        const bool inputLooksVector = endsWithOneOf(inputPath, {".shp", ".gpkg", ".geojson", ".json", ".kml", ".csv"});
        const bool outputLooksVector = endsWithOneOf(outputPath, {".shp", ".gpkg", ".geojson", ".json", ".kml", ".csv"});
        const bool outputLooksRaster = endsWithOneOf(outputPath, {".tif", ".tiff", ".img", ".vrt", ".png", ".jpg", ".jpeg", ".bmp"});

        if (inputLooksVector && !outputLooksVector) {
            return ActionValidationIssue{"output", "矢量重投影输出应使用 .gpkg、.geojson、.shp、.kml 或 .csv"};
        }
        if (!inputLooksVector && !outputLooksRaster) {
            return ActionValidationIssue{"output", "栅格重投影输出应使用 .tif、.tiff、.img 或 .vrt"};
        }
    }

    if (pluginName == "raster_manage" && actionKey == "overviews") {
        const std::string levelsText = stringParam("levels");
        std::istringstream iss(levelsText);
        int level = 0;
        bool hasValidLevel = false;
        while (iss >> level) {
            if (level > 1) {
                hasValidLevel = true;
                break;
            }
        }
        if (!hasValidLevel) {
            return ActionValidationIssue{"levels", "参数“金字塔层级”至少应包含一个大于 1 的整数，例如 2 4 8 16"};
        }
    }

    if (pluginName == "raster_inspect" && actionKey == "histogram") {
        const auto bins = intParamValue(params, "bins");
        if (bins.has_value() && *bins <= 0) {
            return ActionValidationIssue{"bins", "参数“分箱数”必须大于 0"};
        }
    }

    if (pluginName == "terrain") {
        if (const auto band = intParamValue(params, "band");
            band.has_value() && *band <= 0) {
            return ActionValidationIssue{"band", "参数“波段号”必须大于 0"};
        }
        if (const auto zFactor = doubleParamValue(params, "z_factor");
            zFactor.has_value() && *zFactor <= 0.0) {
            return ActionValidationIssue{"z_factor", "参数“高程缩放”必须大于 0"};
        }
        if (actionKey == "stream_extract") {
            if (const auto threshold = doubleParamValue(params, "accum_threshold");
                threshold.has_value() && *threshold <= 0.0) {
                return ActionValidationIssue{"accum_threshold", "参数“汇流阈值”必须大于 0"};
            }
        }
        if (actionKey == "profile_extract") {
            const std::string pathValue = stringParam("profile_path");
            if (trim(pathValue).empty()) {
                return ActionValidationIssue{"profile_path", "参数“剖面路径”不能为空"};
            }
            const std::string outputPath = stringParam("output");
            if (!outputPath.empty() && !endsWithOneOf(outputPath, {".csv"})) {
                return ActionValidationIssue{"output", "参数“输出文件”应使用 .csv"};
            }
        }
        if (actionKey == "viewshed_multi") {
            const std::string pointsValue = stringParam("observer_points");
            if (trim(pointsValue).empty()) {
                return ActionValidationIssue{"observer_points", "参数“观察点列表”不能为空"};
            }
        }
        if (actionKey == "viewshed" || actionKey == "viewshed_multi") {
            if (const auto observerHeight = doubleParamValue(params, "observer_height");
                observerHeight.has_value() && *observerHeight < 0.0) {
                return ActionValidationIssue{"observer_height", "参数“观察点高度”必须大于等于 0"};
            }
            if (const auto targetHeight = doubleParamValue(params, "target_height");
                targetHeight.has_value() && *targetHeight < 0.0) {
                return ActionValidationIssue{"target_height", "参数“目标高度”必须大于等于 0"};
            }
            if (const auto maxDistance = doubleParamValue(params, "max_distance");
                maxDistance.has_value() && *maxDistance < 0.0) {
                return ActionValidationIssue{"max_distance", "参数“最大距离”必须大于等于 0"};
            }
        }
        if (actionKey == "hillshade") {
            if (const auto azimuth = doubleParamValue(params, "azimuth");
                azimuth.has_value() && (*azimuth < 0.0 || *azimuth > 360.0)) {
                return ActionValidationIssue{"azimuth", "参数“方位角”应落在 [0, 360] 范围内"};
            }
            if (const auto altitude = doubleParamValue(params, "altitude");
                altitude.has_value() && (*altitude < 0.0 || *altitude > 90.0)) {
                return ActionValidationIssue{"altitude", "参数“高度角”应落在 [0, 90] 范围内"};
            }
        }
    }

    if (pluginName == "spindex") {
        const auto validatePositiveBand = [&](const std::string& key, const char* displayName)
            -> std::optional<ActionValidationIssue> {
            const auto bandValue = intParamValue(params, key);
            if (bandValue.has_value() && *bandValue <= 0) {
                return ActionValidationIssue{key, std::string("参数“") + displayName + "”必须大于 0"};
            }
            return std::nullopt;
        };

        for (const auto& [key, displayName] : std::vector<std::pair<std::string, const char*>>{
                 {"blue_band", "蓝光波段"},
                 {"green_band", "绿光波段"},
                 {"red_band", "红光波段"},
                 {"nir_band", "近红外波段"},
                 {"swir1_band", "短波红外1波段"},
                 {"swir2_band", "短波红外2波段"}}) {
            if (const auto issue = validatePositiveBand(key, displayName)) {
                return issue;
            }
        }
    }

    if (pluginName == "classification" && actionKey == "feature_stats") {
        const std::string rastersText = stringParam("rasters");
        const std::string bandsText = stringParam("bands");
        const std::string nodatasText = stringParam("nodatas");
        const std::string outputPath = stringParam("output");
        const std::string classMapPath = stringParam("class_map");

        const auto rasterItems = splitCommaList(rastersText);
        if (rasterItems.empty()) {
            return ActionValidationIssue{"rasters", "参数“分类栅格列表”至少填写一个栅格路径"};
        }
        if (!bandsText.empty() && splitCommaList(bandsText).size() != rasterItems.size()) {
            return ActionValidationIssue{"bands", "参数“波段列表”数量必须与“分类栅格列表”一致"};
        }
        if (!nodatasText.empty() && splitCommaList(nodatasText).size() != rasterItems.size()) {
            return ActionValidationIssue{"nodatas", "参数“NoData 列表”数量必须与“分类栅格列表”一致"};
        }
        if (!classMapPath.empty() && !endsWithOneOf(classMapPath, {".json"})) {
            return ActionValidationIssue{"class_map", "参数“分类映射”应选择 .json 文件"};
        }
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".json", ".csv"})) {
            return ActionValidationIssue{"output", "参数“统计输出”目前只支持 .json 或 .csv"};
        }
        const std::string vectorOutputPath = stringParam("vector_output");
        if (!vectorOutputPath.empty() && !endsWithOneOf(vectorOutputPath, {".gpkg"})) {
            return ActionValidationIssue{"vector_output", "参数“分类面输出”当前实际仅支持 .gpkg"};
        }
        const std::string rasterOutputPath = stringParam("raster_output");
        if (!rasterOutputPath.empty() && !endsWithOneOf(rasterOutputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"raster_output", "参数“分类栅格输出”当前实际仅支持 .tif 或 .tiff"};
        }
    }

    if (pluginName == "vector" && actionKey == "convert") {
        const std::string outputPath = stringParam("output");
        const std::string formatValue = stringParam("format");
        if (!outputPath.empty() && !formatValue.empty()) {
            if (formatValue == "GeoJSON" && !endsWithOneOf(outputPath, {".geojson", ".json"})) {
                return ActionValidationIssue{"output", "参数“输出文件”应与“输出格式”一致：GeoJSON 应使用 .geojson 或 .json"};
            }
            if (formatValue == "ESRI Shapefile" && !endsWithOneOf(outputPath, {".shp"})) {
                return ActionValidationIssue{"output", "参数“输出文件”应与“输出格式”一致：Shapefile 应使用 .shp"};
            }
            if (formatValue == "GPKG" && !endsWithOneOf(outputPath, {".gpkg"})) {
                return ActionValidationIssue{"output", "参数“输出文件”应与“输出格式”一致：GPKG 应使用 .gpkg"};
            }
            if (formatValue == "KML" && !endsWithOneOf(outputPath, {".kml"})) {
                return ActionValidationIssue{"output", "参数“输出文件”应与“输出格式”一致：KML 应使用 .kml"};
            }
            if (formatValue == "CSV" && !endsWithOneOf(outputPath, {".csv"})) {
                return ActionValidationIssue{"output", "参数“输出文件”应与“输出格式”一致：CSV 应使用 .csv"};
            }
        }
    }

    if (pluginName == "vector" && actionKey == "polygonize") {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".geojson", ".json", ".gpkg", ".shp"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .geojson、.json、.gpkg 或 .shp"};
        }
    }

    if (pluginName == "vector" &&
        (actionKey == "filter" || actionKey == "buffer" || actionKey == "clip")) {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".geojson", ".json", ".gpkg", ".shp", ".kml"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .geojson、.json、.gpkg、.shp 或 .kml"};
        }
    }

    if (pluginName == "vector" &&
        (actionKey == "union" || actionKey == "difference" || actionKey == "dissolve")) {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".geojson", ".json", ".gpkg", ".shp"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .geojson、.json、.gpkg 或 .shp"};
        }
    }

    if (pluginName == "vector" && actionKey == "rasterize") {
        const auto resolution = doubleParamValue(params, "resolution");
        if (resolution.has_value() && *resolution <= 0.0) {
            return ActionValidationIssue{"resolution", "参数“分辨率”必须大于 0"};
        }
    }

    if (pluginName == "matching") {
        if (const auto ratio = doubleParamValue(params, "ratio_test");
            ratio.has_value() && (*ratio <= 0.0 || *ratio > 1.0)) {
            return ActionValidationIssue{"ratio_test", "参数“比率阈值”应落在 (0, 1] 范围内"};
        }
        if (const auto quality = doubleParamValue(params, "quality_level");
            quality.has_value() && (*quality <= 0.0 || *quality > 1.0)) {
            return ActionValidationIssue{"quality_level", "参数“质量阈值”应落在 (0, 1] 范围内"};
        }
        if (const auto minDistance = doubleParamValue(params, "min_distance");
            minDistance.has_value() && *minDistance < 0.0) {
            return ActionValidationIssue{"min_distance", "参数“最小间距”不能小于 0"};
        }
    }

    if (pluginName == "processing") {
        if (actionKey == "filter") {
            const auto kernelSize = intParamValue(params, "kernel_size");
            if (kernelSize.has_value()) {
                if (*kernelSize < 3) {
                    return ActionValidationIssue{"kernel_size", "参数“核大小”必须大于等于 3"};
                }
                if ((*kernelSize % 2) == 0) {
                    return ActionValidationIssue{"kernel_size", "参数“核大小”建议填写奇数，例如 3、5、7"};
                }
            }
        }
        if (actionKey == "enhance") {
            const std::string enhanceType = stringParam("enhance_type");
            if (enhanceType == "gamma") {
                const auto gamma = doubleParamValue(params, "gamma");
                if (gamma.has_value() && *gamma <= 0.0) {
                    return ActionValidationIssue{"gamma", "参数“Gamma”必须大于 0"};
                }
            }
        }
        if (actionKey == "kmeans") {
            const auto k = intParamValue(params, "k");
            if (k.has_value() && *k <= 0) {
                return ActionValidationIssue{"k", "参数“聚类数”必须大于 0"};
            }
        }
    }

    return std::nullopt;
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

std::vector<gis::framework::ParamSpec> buildEffectiveGuiParamSpecs(
    const std::string& pluginName,
    const std::string& action,
    const std::vector<gis::framework::ParamSpec>& specs,
    const std::set<std::string>& visibleKeys,
    const std::set<std::string>& requiredKeys) {
    std::vector<gis::framework::ParamSpec> filtered;
    for (const auto& spec : specs) {
        if (spec.key == "action") {
            continue;
        }
        if (!visibleKeys.empty() && visibleKeys.count(spec.key) == 0) {
            continue;
        }

        auto adjustedSpec = spec;
        adjustedSpec.required = requiredKeys.count(spec.key) > 0;

        if (pluginName == "processing" && action == "threshold" && spec.key == "method") {
            adjustedSpec.defaultValue = std::string("otsu");
        }
        if (pluginName == "matching") {
            if (spec.key == "ratio_test" || spec.key == "quality_level") {
                adjustedSpec.minValue = 0.000001;
                adjustedSpec.maxValue = 1.0;
            } else if (spec.key == "min_distance") {
                adjustedSpec.minValue = 0.0;
            } else if (spec.key == "stitch_confidence") {
                adjustedSpec.minValue = 0.0;
                adjustedSpec.maxValue = 1.0;
            }
        }
        if (pluginName == "raster_manage" && action == "nodata" && spec.key == "band") {
            adjustedSpec.defaultValue = int{0};
            adjustedSpec.minValue = 0;
        }
        if (pluginName == "raster_inspect" && spec.key == "bins") {
            adjustedSpec.minValue = 1;
        }
        if (pluginName == "terrain") {
            if (spec.key == "band") {
                adjustedSpec.minValue = 1;
            } else if (spec.key == "z_factor") {
                adjustedSpec.minValue = 0.000001;
            } else if (spec.key == "azimuth") {
                adjustedSpec.minValue = 0.0;
                adjustedSpec.maxValue = 360.0;
            } else if (spec.key == "altitude") {
                adjustedSpec.minValue = 0.0;
                adjustedSpec.maxValue = 90.0;
            } else if (spec.key == "accum_threshold") {
                adjustedSpec.minValue = 0.000001;
            } else if (spec.key == "observer_height" || spec.key == "target_height" || spec.key == "max_distance") {
                adjustedSpec.minValue = 0.0;
            }
        }
        if (pluginName == "spindex" &&
            (spec.key == "blue_band" || spec.key == "green_band" ||
             spec.key == "red_band" || spec.key == "nir_band" ||
             spec.key == "swir1_band" || spec.key == "swir2_band")) {
            adjustedSpec.minValue = 1;
        }
        if (pluginName == "vector" && spec.key == "resolution") {
            adjustedSpec.minValue = 0.000001;
        }
        if (pluginName == "processing") {
            if (spec.key == "gamma") {
                adjustedSpec.minValue = 0.000001;
            } else if (spec.key == "k") {
                adjustedSpec.minValue = 1;
            } else if (spec.key == "clip_limit") {
                adjustedSpec.minValue = 0.0;
            } else if (spec.key == "kernel_size") {
                adjustedSpec.minValue = 3;
            }
        }
        if (pluginName == "projection" && action == "transform" && spec.key == "src_srs") {
            adjustedSpec.defaultValue = std::string("EPSG:4326");
        }

        filtered.push_back(std::move(adjustedSpec));
    }

    return filtered;
}

ExecuteButtonState buildExecuteButtonState(bool hasSelection,
                                           const std::string& validationMessage) {
    if (!hasSelection) {
        return ExecuteButtonState{
            false,
            "请先选择主功能和子功能",
            "就绪",
            "statusBadgeReady"
        };
    }
    if (!validationMessage.empty()) {
        return ExecuteButtonState{
            false,
            validationMessage,
            "待补充",
            "statusBadgeWarning"
        };
    }
    return ExecuteButtonState{
        true,
        "参数已就绪，可以执行当前功能",
        "可执行",
        "statusBadgeReady"
    };
}

std::string resolveHighlightedParamKey(
    bool hasSelection,
    const std::vector<gis::framework::ParamSpec>& specs,
    const std::map<std::string, gis::framework::ParamValue>& params,
    const std::optional<ActionValidationIssue>& actionIssue) {
    if (!hasSelection) {
        return {};
    }
    const std::string invalidKey = gis::gui::findFirstInvalidParamKey(specs, params);
    if (!invalidKey.empty()) {
        return invalidKey;
    }
    if (actionIssue.has_value()) {
        return actionIssue->key;
    }
    return {};
}

} // namespace gis::gui


