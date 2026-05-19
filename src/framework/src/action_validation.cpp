#include <gis/framework/action_validation.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace gis::framework {

namespace {

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string lowerString(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isZeroExtent(const std::array<double, 4>& extent) {
    return extent[0] == 0.0 && extent[1] == 0.0 &&
           extent[2] == 0.0 && extent[3] == 0.0;
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

bool parseIntegerList(const std::string& rawText,
                      std::vector<int>& values,
                      std::string& error) {
    values.clear();
    for (const auto& item : splitCommaList(rawText)) {
        try {
            size_t index = 0;
            const int value = std::stoi(item, &index);
            if (index != item.size()) {
                error = "参数“波段列表”应使用英文逗号分隔的整数，例如 1,2,3";
                return false;
            }
            values.push_back(value);
        } catch (...) {
            error = "参数“波段列表”应使用英文逗号分隔的整数，例如 1,2,3";
            return false;
        }
    }
    return true;
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
    const std::map<std::string, ParamValue>& params,
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
    const std::map<std::string, ParamValue>& params,
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
    const std::map<std::string, ParamValue>& params,
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

std::optional<ActionValidationIssue> validateActionSpecificParams(
    const std::string& pluginName,
    const std::string& actionKey,
    const std::map<std::string, ParamValue>& params) {
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

    if (pluginName == "cutting" && actionKey == "tile") {
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

    if (pluginName == "raster_manage" && actionKey == "cog") {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .tif 或 .tiff"};
        }
    }

    if (pluginName == "raster_inspect" && actionKey == "histogram") {
        const auto bins = intParamValue(params, "bins");
        if (bins.has_value() && *bins <= 0) {
            return ActionValidationIssue{"bins", "参数“分箱数”必须大于 0"};
        }
    }

    if (pluginName == "terrain") {
        if (const auto band = intParamValue(params, "band"); band.has_value() && *band <= 0) {
            return ActionValidationIssue{"band", "参数“波段号”必须大于 0"};
        }
        if (const auto zFactor = doubleParamValue(params, "z_factor"); zFactor.has_value() && *zFactor <= 0.0) {
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
            return ActionValidationIssue{"output", "参数“统计输出”当前只支持 .json 或 .csv"};
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
        (actionKey == "filter" || actionKey == "buffer" || actionKey == "clip" ||
         actionKey == "simplify" || actionKey == "repair" || actionKey == "geom_metrics" ||
         actionKey == "nearest" || actionKey == "convex_hull" || actionKey == "centroid" ||
         actionKey == "envelope" || actionKey == "boundary" || actionKey == "singlepart" ||
         actionKey == "vertices_extract" || actionKey == "endpoints_extract" ||
         actionKey == "midpoints_extract" || actionKey == "interior_point")) {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".geojson", ".json", ".gpkg", ".shp", ".kml"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .geojson、.json、.gpkg、.shp 或 .kml"};
        }
    }

    if (pluginName == "classification" &&
        (actionKey == "svm_classify" || actionKey == "random_forest_classify" ||
         actionKey == "max_likelihood_classify")) {
        const std::string inputPath = stringParam("input");
        const std::string trainingCsv = stringParam("training_csv");
        const std::string outputPath = stringParam("output");
        const std::string bandsText = stringParam("bands");
        if (trim(inputPath).empty()) {
            return ActionValidationIssue{"input", "参数“输入栅格”不能为空"};
        }
        if (trim(trainingCsv).empty()) {
            return ActionValidationIssue{"training_csv", "参数“训练CSV”不能为空"};
        }
        if (trim(outputPath).empty()) {
            return ActionValidationIssue{"output", "参数“输出文件”不能为空"};
        }
        if (!endsWithOneOf(trainingCsv, {".csv"})) {
            return ActionValidationIssue{"training_csv", "参数“训练样本 CSV”应选择 .csv 文件"};
        }
        if (!endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .tif 或 .tiff"};
        }
        if (!bandsText.empty()) {
            std::string error;
            std::vector<int> bands;
            if (!parseIntegerList(bandsText, bands, error)) {
                return ActionValidationIssue{"bands", error};
            }
            for (int band : bands) {
                if (band <= 0) {
                    return ActionValidationIssue{"bands", "参数“波段列表”中的波段号必须大于 0"};
                }
            }
        }
    }

    if (pluginName == "classification" && actionKey == "accuracy_assessment") {
        const std::string classifiedRaster = stringParam("classified_raster");
        const std::string referenceRaster = stringParam("reference_raster");
        const std::string outputPath = stringParam("output");
        if (trim(classifiedRaster).empty()) {
            return ActionValidationIssue{"classified_raster", "参数“分类结果栅格”不能为空"};
        }
        if (trim(referenceRaster).empty()) {
            return ActionValidationIssue{"reference_raster", "参数“参考栅格”不能为空"};
        }
        if (trim(outputPath).empty()) {
            return ActionValidationIssue{"output", "参数“输出文件”不能为空"};
        }
        if (!endsWithOneOf(outputPath, {".json"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .json"};
        }
    }

    if (pluginName == "georef" && actionKey == "dos_correction") {
        const auto band = intParamValue(params, "band");
        if (band.has_value() && *band <= 0) {
            return ActionValidationIssue{"band", "参数“波段序号”必须大于 0"};
        }
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出栅格”应使用 .tif 或 .tiff"};
        }
    }

    if (pluginName == "georef" && actionKey == "radiometric_calibration") {
        const auto band = intParamValue(params, "band");
        if (band.has_value() && *band <= 0) {
            return ActionValidationIssue{"band", "参数“波段序号”必须大于 0"};
        }
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出栅格”应使用 .tif 或 .tiff"};
        }
        const std::string metadataFile = stringParam("metadata_file");
        if (!metadataFile.empty() && !endsWithOneOf(metadataFile, {".txt", ".mtl", ".xml", ".json"})) {
            return ActionValidationIssue{"metadata_file", "参数“元数据文件”建议选择 .txt/.mtl/.xml/.json"};
        }
    }

    if (pluginName == "georef" && actionKey == "gcp_register") {
        const std::string gcpFile = stringParam("gcp_file");
        if (!gcpFile.empty() && !endsWithOneOf(gcpFile, {".csv"})) {
            return ActionValidationIssue{"gcp_file", "参数“控制点文件”应选择 .csv"};
        }
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出栅格”应使用 .tif 或 .tiff"};
        }
    }

    if (pluginName == "georef" &&
        (actionKey == "cosine_correction" || actionKey == "minnaert_correction" ||
         actionKey == "c_correction")) {
        const auto band = intParamValue(params, "band");
        const auto sunZenith = doubleParamValue(params, "sun_zenith_deg");
        const auto sunAzimuth = doubleParamValue(params, "sun_azimuth_deg");
        if (band.has_value() && *band <= 0) {
            return ActionValidationIssue{"band", "参数“波段序号”必须大于 0"};
        }
        if (sunZenith.has_value() && (*sunZenith < 0.0 || *sunZenith >= 90.0)) {
            return ActionValidationIssue{"sun_zenith_deg", "参数“太阳天顶角”应落在 [0, 90) 范围内"};
        }
        if (sunAzimuth.has_value() && (*sunAzimuth < 0.0 || *sunAzimuth > 360.0)) {
            return ActionValidationIssue{"sun_azimuth_deg", "参数“太阳方位角”应落在 [0, 360] 范围内"};
        }
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出栅格”应使用 .tif 或 .tiff"};
        }
        if (actionKey == "minnaert_correction") {
            const auto minnaertK = doubleParamValue(params, "minnaert_k");
            if (minnaertK.has_value() && *minnaertK <= 0.0) {
                return ActionValidationIssue{"minnaert_k", "参数“Minnaert 系数”必须大于 0"};
            }
        }
        if (actionKey == "c_correction") {
            const auto cValue = doubleParamValue(params, "c_value");
            if (cValue.has_value() && *cValue < 0.0) {
                return ActionValidationIssue{"c_value", "参数“C 系数”必须大于等于 0"};
            }
        }
    }

    if (pluginName == "georef" && actionKey == "percentile_stretch") {
        const auto darkPercentile = doubleParamValue(params, "dark_percentile");
        const auto brightPercentile = doubleParamValue(params, "bright_percentile");
        if (darkPercentile.has_value() && (*darkPercentile < 0.0 || *darkPercentile >= 100.0)) {
            return ActionValidationIssue{"dark_percentile", "参数“暗像元百分位”应落在 [0, 100) 范围内"};
        }
        if (brightPercentile.has_value() && (*brightPercentile <= 0.0 || *brightPercentile > 100.0)) {
            return ActionValidationIssue{"bright_percentile", "参数“亮像元百分位”应落在 (0, 100] 范围内"};
        }
        if (darkPercentile.has_value() && brightPercentile.has_value() &&
            *darkPercentile >= *brightPercentile) {
            return ActionValidationIssue{"bright_percentile", "参数“亮像元百分位”必须大于“暗像元百分位”"};
        }
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出栅格”应使用 .tif 或 .tiff"};
        }
    }

    if (pluginName == "georef" && actionKey == "rpc_orthorectify") {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出栅格”应使用 .tif 或 .tiff"};
        }
    }

    if (pluginName == "vector" &&
        (actionKey == "geom_metrics" || actionKey == "nearest" || actionKey == "adjacency" ||
         actionKey == "overlap_check" || actionKey == "topology_check" ||
         actionKey == "multipart_check" || actionKey == "duplicate_point_check" ||
         actionKey == "hole_check" || actionKey == "dangling_endpoint_check")) {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".csv"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .csv"};
        }
    }

    if (pluginName == "vector" && actionKey == "sliver_remove") {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".gpkg", ".geojson", ".json", ".shp", ".kml"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .gpkg、.geojson、.json、.shp 或 .kml"};
        }
        const auto minArea = doubleParamValue(params, "min_area");
        if (minArea.has_value() && *minArea <= 0.0) {
            return ActionValidationIssue{"min_area", "参数“最小面积”必须大于 0"};
        }
    }

    if (pluginName == "vector" && actionKey == "rasterize") {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .tif 或 .tiff"};
        }
        const auto resolution = doubleParamValue(params, "resolution");
        if (resolution.has_value() && *resolution <= 0.0) {
            return ActionValidationIssue{"resolution", "参数“像元分辨率”必须大于 0"};
        }
    }

    if (pluginName == "vector" && actionKey == "simplify") {
        const auto tolerance = doubleParamValue(params, "tolerance");
        if (tolerance.has_value() && *tolerance <= 0.0) {
            return ActionValidationIssue{"tolerance", "参数“简化容差”必须大于 0"};
        }
    }

    if (pluginName == "matching") {
        if (const auto ratio = doubleParamValue(params, "ratio_test");
            ratio.has_value() && (*ratio <= 0.0 || *ratio > 1.0)) {
            return ActionValidationIssue{"ratio_test", "参数“Lowe比率阈值”应落在 (0, 1] 范围内"};
        }
        if (const auto quality = doubleParamValue(params, "quality_level");
            quality.has_value() && (*quality <= 0.0 || *quality > 1.0)) {
            return ActionValidationIssue{"quality_level", "参数“质量水平”应落在 (0, 1] 范围内"};
        }
        if (const auto minDistance = doubleParamValue(params, "min_distance");
            minDistance.has_value() && *minDistance < 0.0) {
            return ActionValidationIssue{"min_distance", "参数“最小间距”必须大于等于 0"};
        }
    }

    if (pluginName == "processing") {
        if (actionKey == "filter" || actionKey == "gabor_filter") {
            const auto kernelSize = intParamValue(params, "kernel_size");
            if (kernelSize.has_value() && (*kernelSize < 3 || *kernelSize % 2 == 0)) {
                return ActionValidationIssue{"kernel_size", "参数“核大小”必须是不小于 3 的奇数"};
            }
        }
        if (actionKey == "enhance") {
            if (const auto gamma = doubleParamValue(params, "gamma");
                gamma.has_value() && *gamma <= 0.0) {
                return ActionValidationIssue{"gamma", "参数“Gamma值”必须大于 0"};
            }
        }
        if (actionKey == "kmeans") {
            const auto k = intParamValue(params, "kmeans_k");
            if (k.has_value() && *k < 2) {
                return ActionValidationIssue{"kmeans_k", "参数“KMeans 聚类数”必须大于等于 2"};
            }
        }
        if (actionKey == "gabor_filter") {
            const auto kernelSize = intParamValue(params, "kernel_size");
            if (kernelSize.has_value() && (*kernelSize < 3 || *kernelSize % 2 == 0)) {
                return ActionValidationIssue{"kernel_size", "参数“核大小”必须是不小于 3 的奇数"};
            }
            if (const auto sigma = doubleParamValue(params, "sigma");
                sigma.has_value() && *sigma <= 0.0) {
                return ActionValidationIssue{"sigma", "参数“sigma值”必须大于 0"};
            }
            if (const auto lambda = doubleParamValue(params, "gabor_lambda");
                lambda.has_value() && *lambda <= 0.0) {
                return ActionValidationIssue{"gabor_lambda", "参数“波长”必须大于 0"};
            }
            if (const auto gamma = doubleParamValue(params, "gabor_gamma");
                gamma.has_value() && *gamma <= 0.0) {
                return ActionValidationIssue{"gabor_gamma", "参数“纵横比”必须大于 0"};
            }
        }
        if (actionKey == "glcm_texture") {
            const auto kernelSize = intParamValue(params, "kernel_size");
            if (kernelSize.has_value() && (*kernelSize < 3 || *kernelSize % 2 == 0)) {
                return ActionValidationIssue{"kernel_size", "参数“窗口大小”必须是不小于 3 的奇数"};
            }
            const auto levels = intParamValue(params, "glcm_levels");
            if (levels.has_value() && *levels < 2) {
                return ActionValidationIssue{"glcm_levels", "参数“灰度级数”必须大于等于 2"};
            }
        }
        if (actionKey == "mean_shift_filter") {
            if (const auto spatialRadius = doubleParamValue(params, "spatial_radius");
                spatialRadius.has_value() && *spatialRadius <= 0.0) {
                return ActionValidationIssue{"spatial_radius", "参数“空间半径”必须大于 0"};
            }
            if (const auto colorRadius = doubleParamValue(params, "color_radius");
                colorRadius.has_value() && *colorRadius <= 0.0) {
                return ActionValidationIssue{"color_radius", "参数“颜色半径”必须大于 0"};
            }
            if (const auto pyramidLevel = intParamValue(params, "pyramid_level");
                pyramidLevel.has_value() && *pyramidLevel < 0) {
                return ActionValidationIssue{"pyramid_level", "参数“金字塔层数”必须大于等于 0"};
            }
        }
    }

    return std::nullopt;
}

} // namespace gis::framework
