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

QString genericActionDisplayName(const std::string& actionKey) {
    static const std::map<std::string, QString> kLabels = {
        {"gabor_filter", QStringLiteral("Gabor 婊ゆ尝")},
        {"feature_stats", QStringLiteral("鍦扮墿鍒嗙被缁熻")},
        {"reproject", QStringLiteral("Reproject")},
        {"buffer", QStringLiteral("Buffer")},
        {"info", QStringLiteral("Info")},
        {"clip", QStringLiteral("Clip")},
        {"template_match", QStringLiteral("Template Match")},
    };

    const auto it = kLabels.find(actionKey);
    if (it != kLabels.end()) {
        return it->second;
    }
    return QString::fromStdString(actionKey);
}

const std::map<std::string, gis::gui::ParamText>& commonParamTextStorage() {
    static const std::map<std::string, gis::gui::ParamText> kTexts = {
        {"input", {QStringLiteral("杈撳叆鏂囦欢"), QStringLiteral("Input data path.")}},
        {"output", {QStringLiteral("杈撳嚭鏂囦欢"), QStringLiteral("Output result path.")}},
        {"reference", {QStringLiteral("鍙傝€冩枃浠?"), QStringLiteral("Reference data path.")}},
        {"dst_srs", {QStringLiteral("鐩爣鍧愭爣绯?"), QStringLiteral("Target spatial reference.")}},
        {"src_srs", {QStringLiteral("Source SRS"), QStringLiteral("Source spatial reference.")}},
        {"srs", {QStringLiteral("SRS"), QStringLiteral("Spatial reference to assign.")}},
        {"resample", {QStringLiteral("Resample"), QStringLiteral("Resampling method.")}},
        {"band", {QStringLiteral("Band"), QStringLiteral("Raster band index to process.")}},
        {"x", {QStringLiteral("X 鍧愭爣"), QStringLiteral("X coordinate to transform.")}},
        {"y", {QStringLiteral("Y 鍧愭爣"), QStringLiteral("Y coordinate to transform.")}},
        {"layer", {QStringLiteral("鍥惧眰鍚?"), QStringLiteral("Layer name to process.")}},
        {"where", {QStringLiteral("Where"), QStringLiteral("SQL WHERE expression.")}},
        {"distance", {QStringLiteral("璺濈"), QStringLiteral("Distance parameter.")}},
        {"clip_vector", {QStringLiteral("瑁佸垏鐭㈤噺"), QStringLiteral("Overlay vector path for clipping.")}},
        {"bins", {QStringLiteral("Bins"), QStringLiteral("Histogram bin count.")}},
        {"levels", {QStringLiteral("Levels"), QStringLiteral("Overview levels to build.")}},
        {"method", {QStringLiteral("Method"), QStringLiteral("Primary processing method.")}},
        {"change_method", {QStringLiteral("Change Method"), QStringLiteral("Method used for change detection.")}},
        {"ecc_motion", {QStringLiteral("ECC Motion"), QStringLiteral("Motion model used by ECC registration.")}},
        {"stitch_confidence", {QStringLiteral("Stitch Confidence"), QStringLiteral("Confidence threshold used for stitching.")}},
        {"threshold_value", {QStringLiteral("Threshold Value"), QStringLiteral("Threshold value used for segmentation.")}},
        {"max_value", {QStringLiteral("Max Value"), QStringLiteral("Maximum output value after thresholding.")}},
        {"filter_type", {QStringLiteral("Filter Type"), QStringLiteral("Spatial filter algorithm to apply.")}},
        {"kernel_size", {QStringLiteral("Kernel Size"), QStringLiteral("Kernel size used by the filter.")}},
        {"sigma", {QStringLiteral("Sigma"), QStringLiteral("Sigma parameter used by Gaussian-style filters.")}},
        {"nodata_value", {QStringLiteral("NoData"), QStringLiteral("NoData value to write.")}},
        {"template_file", {QStringLiteral("妯℃澘鏂囦欢"), QStringLiteral("Template raster path.")}},
    };
    return kTexts;
}

const std::map<std::string, std::map<std::string, std::map<std::string, gis::gui::ParamText>>>&
actionSpecificParamTextStorage() {
    static const std::map<std::string, std::map<std::string, std::map<std::string, gis::gui::ParamText>>> kTexts = {
        {"classification", {
            {"feature_stats", {
                {"vector_output", {QStringLiteral("鍒嗙被闈㈣緭鍑?"), QStringLiteral("Optional classified polygon output.")}},
            }},
        }},
        {"cutting", {
            {"split", {
                {"output", {QStringLiteral("杈撳嚭鐩綍"), QStringLiteral("Output directory for split tiles.")}},
            }},
            {"merge_bands", {
                {"input", {QStringLiteral("杈撳叆鏂囦欢"), QStringLiteral("Primary single-band raster path.")}},
                {"bands", {QStringLiteral("娉㈡鍒楄〃"), QStringLiteral("Additional single-band raster paths.")}},
            }},
        }},
        {"processing", {
            {"template_match", {
                {"template_file", {QStringLiteral("妯℃澘鏂囦欢"), QStringLiteral("Template raster path.")}},
            }},
        }},
        {"projection", {
            {"reproject", {
                {"input", {QStringLiteral("杈撳叆鏂囦欢"), QStringLiteral("Raster or vector input path.")}},
            }},
        }},
    };
    return kTexts;
}

const std::map<std::string, std::map<std::string, gis::gui::ActionUiConfig>>& actionUiConfigStorage() {
    static const std::map<std::string, std::map<std::string, gis::gui::ActionUiConfig>> kConfigs = {
        {"processing", {
            {"filter", {
                QStringLiteral("Filter"),
                QStringLiteral("Apply spatial filtering to raster data."),
                {"input", "output", "band", "filter_type", "kernel_size", "sigma"},
                {"input", "output"}
            }},
            {"threshold", {
                QStringLiteral("Threshold"),
                QStringLiteral("Segment raster data by thresholding."),
                {"input", "output", "band", "method", "threshold_value", "max_value"},
                {"input", "output"}
            }},
            {"stats", {
                QStringLiteral("Stats"),
                QStringLiteral("Inspect basic statistics for a raster band."),
                {"input", "band"},
                {"input"}
            }},
            {"gabor_filter", {
                QStringLiteral("Gabor 婊ゆ尝"),
                QStringLiteral("Gabor texture filtering."),
                {"input", "output", "band", "kernel_size", "sigma", "gabor_theta", "gabor_lambda", "gabor_gamma", "gabor_psi"},
                {"input", "output"}
            }},
        }},
        {"classification", {
            {"feature_stats", {
                QStringLiteral("鍦扮墿鍒嗙被缁熻"),
                QStringLiteral("Classification feature statistics."),
                {"vector", "class_map", "rasters", "output", "feature_id_field", "feature_name_field", "bands", "nodatas", "target_epsg", "vector_output", "raster_output"},
                {"vector", "class_map", "rasters", "output"}
            }},
        }},
        {"vector", {
            {"buffer", {
                QStringLiteral("Buffer"),
                QStringLiteral("Create vector buffers."),
                {"input", "output", "layer", "distance"},
                {"input", "output"}
            }},
            {"clip", {
                QStringLiteral("Clip"),
                QStringLiteral("Clip vector features with an overlay layer."),
                {"input", "output", "layer", "clip_vector"},
                {"input", "output", "clip_vector"}
            }},
            {"filter", {
                QStringLiteral("Filter"),
                QStringLiteral("Filter vector features by attribute or extent."),
                {"input", "output", "layer", "where", "extent"},
                {"input", "output"}
            }},
        }},
        {"projection", {
            {"reproject", {
                QStringLiteral("Reproject"),
                QStringLiteral("Reproject raster or vector data."),
                {"input", "output", "dst_srs", "src_srs", "resample"},
                {"input", "output", "dst_srs"}
            }},
            {"info", {
                QStringLiteral("Info"),
                QStringLiteral("Inspect spatial reference information."),
                {"input"},
                {"input"}
            }},
            {"transform", {
                QStringLiteral("Transform"),
                QStringLiteral("Transform a coordinate between spatial references."),
                {"src_srs", "dst_srs", "x", "y"},
                {"dst_srs"}
            }},
            {"assign_srs", {
                QStringLiteral("Assign SRS"),
                QStringLiteral("Assign spatial reference metadata to raster data."),
                {"input", "srs"},
                {"input", "srs"}
            }},
        }},
        {"raster_inspect", {
            {"histogram", {
                QStringLiteral("Histogram"),
                QStringLiteral("Inspect raster histogram statistics."),
                {"input", "output", "band", "bins"},
                {"input"}
            }},
            {"info", {
                QStringLiteral("Info"),
                QStringLiteral("Inspect raster dataset information."),
                {"input"},
                {"input"}
            }},
        }},
        {"matching", {
            {"detect", {
                QStringLiteral("Detect"),
                QStringLiteral("Detect feature points from raster data."),
                {"input", "output", "method", "max_points", "band"},
                {"input"}
            }},
            {"match", {
                QStringLiteral("Match"),
                QStringLiteral("Match feature points between reference and input rasters."),
                {"reference", "input", "output", "method", "match_method", "max_points", "ratio_test", "band"},
                {"reference", "input"}
            }},
            {"register", {
                QStringLiteral("Register"),
                QStringLiteral("Register input raster to reference raster using matched features."),
                {"reference", "input", "output", "method", "match_method", "transform", "resample", "max_points", "ratio_test", "band"},
                {"reference", "input", "output"}
            }},
            {"change", {
                QStringLiteral("Change"),
                QStringLiteral("Detect changes between reference and input rasters."),
                {"reference", "input", "output", "change_method", "threshold", "band"},
                {"reference", "input", "output"}
            }},
            {"ecc_register", {
                QStringLiteral("ECC Register"),
                QStringLiteral("Register raster data using ECC optimization."),
                {"reference", "input", "output", "ecc_motion", "ecc_iterations", "ecc_epsilon", "resample", "band"},
                {"reference", "input", "output"}
            }},
            {"corner", {
                QStringLiteral("Corner"),
                QStringLiteral("Detect corner features from raster data."),
                {"input", "output", "corner_method", "max_corners", "quality_level", "min_distance", "band"},
                {"input"}
            }},
            {"stitch", {
                QStringLiteral("Stitch"),
                QStringLiteral("Stitch multiple raster inputs into a panorama."),
                {"input", "output", "stitch_confidence"},
                {"input", "output"}
            }},
        }},
        {"raster_manage", {
            {"overviews", {
                QStringLiteral("Overviews"),
                QStringLiteral("Build raster overviews."),
                {"input", "levels", "resample"},
                {"input"}
            }},
            {"cog", {
                QStringLiteral("COG"),
                QStringLiteral("Convert raster data to Cloud Optimized GeoTIFF."),
                {"input", "output"},
                {"input", "output"}
            }},
            {"nodata", {
                QStringLiteral("NoData"),
                QStringLiteral("Assign NoData to raster bands."),
                {"input", "band", "nodata_value"},
                {"input"}
            }},
        }},
    };
    return kConfigs;
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
        if ((action == "svm_classify" || action == "random_forest_classify" ||
             action == "max_likelihood_classify") && paramKey == "output") return ".tif";
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
        if (action == "colormap" || action == "histogram_match") return ".tif";
        return inputExt;
    }

    if (pluginName == "raster_math") {
        if (action == "band_math") return ".tif";
        if (action == "reclassify") return ".tif";
        if (action == "raster_overlay") return ".tif";
    }

    if (pluginName == "raster_manage") {
        if (action == "cog") {
            return ".tif";
        }
        if (action == "zonal_stats") {
            return ".json";
        }
        if (action == "proximity") {
            return ".tif";
        }
        return inputExt;
    }

    if (pluginName == "georef") {
        return ".tif";
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
        if (action == "ndvi" || action == "ndmi" || action == "evi" ||
            action == "evi2" || action == "savi" || action == "osavi" ||
            action == "gndvi" || action == "ndwi" || action == "mndwi" ||
            action == "ndbi" || action == "bsi" || action == "arvi" ||
            action == "nbr" || action == "awei" || action == "ui" || action == "bi" ||
            action == "custom_index") return ".tif";
        return inputExt;
    }

    if (pluginName == "vector") {
        if (action == "rasterize") return ".tif";
        if (action == "polygonize") return ".gpkg";
        if (action == "convert") return ".geojson";
        if (action == "adjacency" || action == "overlap_check" || action == "topology_check" ||
            action == "multipart_check" || action == "duplicate_point_check" ||
            action == "hole_check" || action == "dangling_endpoint_check") return ".csv";
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
        if (action == "tile") return {};
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

bool parseIntegerList(const std::string& rawText,
                      std::vector<int>& values,
                      std::string& error) {
    values.clear();
    for (const auto& item : splitCommaList(rawText)) {
        try {
            std::size_t parsed = 0;
            const int value = std::stoi(item, &parsed);
            if (parsed != item.size()) {
                error = "invalid integer";
                return false;
            }
            values.push_back(value);
        } catch (...) {
            error = "invalid integer";
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

    if (pluginName == "classification" && paramKey == "training_csv") {
        config.placeholder = "请选择训练样本 CSV，例如 samples.csv";
        config.openFilter = "CSV 文件 (*.csv);;所有文件 (*)";
        return config;
    }

    if (pluginName == "georef" && paramKey == "gcp_file") {
        config.placeholder = "请选择控制点 CSV，表头示例 pixel_x,pixel_y,map_x,map_y";
        config.openFilter = "CSV 文件 (*.csv);;所有文件 (*)";
        return config;
    }

    if (pluginName == "georef" && paramKey == "metadata_file") {
        config.placeholder = "请选择辐射定标元数据文件，例如 .txt、.mtl";
        config.openFilter = "文本文件 (*.txt *.mtl *.xml *.json);;所有文件 (*)";
        return config;
    }

    if (pluginName == "georef" && paramKey == "dem_file") {
        config.placeholder = "请选择 DEM 栅格文件";
        config.openFilter = filterForRasterInputs();
        return config;
    }

    if (pluginName == "georef" && (paramKey == "slope_raster" || paramKey == "aspect_raster")) {
        config.placeholder = paramKey == "slope_raster"
            ? "请选择坡度栅格，像元值单位应为度"
            : "请选择坡向栅格，像元值单位应为度";
        config.openFilter = filterForRasterInputs();
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

        if (pluginName == "classification" &&
            (action == "svm_classify" || action == "random_forest_classify" ||
             action == "max_likelihood_classify") &&
            paramKey == "output") {
            config.placeholder = "请选择分类输出栅格，建议使用 .tif";
            config.saveFilter = filterForRasterOutputs();
        } else if (pluginName == "classification" && paramKey == "output") {
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
                       (action == "union" || action == "difference" ||
                        action == "intersect" || action == "dissolve" ||
                        action == "spatial_join")) {
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
        paramKey == "cutline" || paramKey == "clip_vector" || paramKey == "overlay_vector" ||
        paramKey == "nearest_vector") {
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
        if ((action == "svm_classify" || action == "random_forest_classify" ||
             action == "max_likelihood_classify") && spec.key == "bands") {
            return "输入波段列表，英文逗号分隔，例如 1,2,3";
        }
        if (spec.key == "label_column") {
            return "训练样本 CSV 中的标签列名，例如 label";
        }
        if (spec.key == "bands") {
            return "与分类栅格一一对应，英文逗号分隔，例如 1,1,1";
        }
        if (spec.key == "nodatas") {
            return "与分类栅格一一对应，英文逗号分隔，例如 0,0,255";
        }
    }

    if (pluginName == "georef" && spec.key == "dark_object_value") {
        return "填写暗像元值；小于 0 时自动使用当前波段最小值";
    }
    if (pluginName == "georef" && spec.key == "gain") {
        return "填写增益系数，例如 0.01 或 1.2";
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

QString actionDisplayName(const std::string& pluginName, const std::string& actionKey) {
    if (const auto* config = findActionUiConfig(pluginName, actionKey);
        config && !config->displayName.isEmpty()) {
        return config->displayName;
    }
    return genericActionDisplayName(actionKey);
}

QString actionDescription(const std::string& pluginName, const std::string& actionKey) {
    if (const auto* config = findActionUiConfig(pluginName, actionKey)) {
        return config->description;
    }
    return {};
}

const ActionUiConfig* findActionUiConfig(const std::string& pluginName,
                                         const std::string& actionKey) {
    const auto& all = actionUiConfigStorage();
    const auto pluginIt = all.find(pluginName);
    if (pluginIt == all.end()) {
        return nullptr;
    }

    const auto actionIt = pluginIt->second.find(actionKey);
    if (actionIt == pluginIt->second.end()) {
        return nullptr;
    }

    return &actionIt->second;
}

const ParamText* findCommonParamText(const std::string& paramKey) {
    const auto& all = commonParamTextStorage();
    const auto it = all.find(paramKey);
    if (it == all.end()) {
        return nullptr;
    }
    return &it->second;
}

const ParamText* findActionSpecificParamText(const std::string& pluginName,
                                             const std::string& actionKey,
                                             const std::string& paramKey) {
    const auto& all = actionSpecificParamTextStorage();
    const auto pluginIt = all.find(pluginName);
    if (pluginIt == all.end()) {
        return nullptr;
    }
    const auto actionIt = pluginIt->second.find(actionKey);
    if (actionIt == pluginIt->second.end()) {
        return nullptr;
    }
    const auto paramIt = actionIt->second.find(paramKey);
    if (paramIt == actionIt->second.end()) {
        return nullptr;
    }
    return &paramIt->second;
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
        {"Reclassify completed successfully", "重分类完成"},
        {"Raster overlay completed successfully", "栅格叠加完成"},
        {"Proximity map created", "欧氏距离计算完成"},
        {"Tile generation completed", "栅格切片完成"},
        {"Zonal statistics completed", "分区统计完成"},
        {"COG generation completed", "COG 生成完成"},
        {"Histogram match completed", "直方图匹配完成"},
        {"Band math completed", "波段运算完成"},
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

    std::string msg = message;

    if (msg.find("Cannot open") != std::string::npos || msg.find("No such file") != std::string::npos) {
        return "无法打开文件：" + msg + "\n建议：请检查文件路径是否正确，文件是否存在。";
    }
    if (msg.find("Permission denied") != std::string::npos) {
        return "权限不足：" + msg + "\n建议：请检查文件是否被其他程序占用，或是否有写入权限。";
    }
    if (msg.find("out of memory") != std::string::npos || msg.find("OutOfMemory") != std::string::npos) {
        return "内存不足：" + msg + "\n建议：请尝试处理更小的数据范围，或先分块处理。";
    }
    if (msg.find("band") != std::string::npos && msg.find("does not exist") != std::string::npos) {
        return "波段不存在：" + msg + "\n建议：请检查波段序号是否超出输入数据的波段数量。";
    }
    if (msg.find("Unsupported format") != std::string::npos || msg.find("not recognized") != std::string::npos) {
        return "格式不支持：" + msg + "\n建议：请确认输入文件格式是否受支持（如 TIFF、GeoJSON、SHP 等）。";
    }
    if (msg.find("Cancelled") != std::string::npos) {
        return "操作已取消";
    }

    return msg;
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

    if (pluginName == "cutting" && actionKey == "tile") {
        const std::string outputPath = stringParam("output");
        if (endsWithOneOf(outputPath, {".tif", ".tiff", ".img", ".vrt", ".png", ".jpg", ".jpeg", ".bmp"})) {
            return ActionValidationIssue{"output", "参数“输出目录”应填写目录，不应填写单个格格文件名"};
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
        const std::string trainingCsv = stringParam("training_csv");
        const std::string outputPath = stringParam("output");
        const std::string bandsText = stringParam("bands");
        if (!trainingCsv.empty() && !endsWithOneOf(trainingCsv, {".csv"})) {
            return ActionValidationIssue{"training_csv", "参数“训练样本 CSV”应选择 .csv 文件"};
        }
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .tif 或 .tiff"};
        }
        if (!bandsText.empty()) {
            std::string error;
            std::vector<int> bands;
            if (!parseIntegerList(bandsText, bands, error)) {
                return ActionValidationIssue{"bands", "参数“波段列表”应使用英文逗号分隔的整数，例如 1,2,3"};
            }
            for (int band : bands) {
                if (band <= 0) {
                    return ActionValidationIssue{"bands", "参数“波段列表”中的波段号必须大于 0"};
                }
            }
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
        const std::string outputPath = stringParam("output");
        if (!gcpFile.empty() && !endsWithOneOf(gcpFile, {".csv"})) {
            return ActionValidationIssue{"gcp_file", "参数“控制点文件”应选择 .csv 文件"};
        }
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出栅格”应使用 .tif 或 .tiff"};
        }
    }

    if (pluginName == "georef" && actionKey == "cosine_correction") {
        const auto band = intParamValue(params, "band");
        const auto sunZenith = doubleParamValue(params, "sun_zenith_deg");
        const auto sunAzimuth = doubleParamValue(params, "sun_azimuth_deg");
        const std::string outputPath = stringParam("output");
        if (band.has_value() && *band <= 0) {
            return ActionValidationIssue{"band", "参数“波段序号”必须大于 0"};
        }
        if (sunZenith.has_value() && (*sunZenith < 0.0 || *sunZenith >= 90.0)) {
            return ActionValidationIssue{"sun_zenith_deg", "参数“太阳天顶角”应落在 [0, 90) 范围内"};
        }
        if (sunAzimuth.has_value() && (*sunAzimuth < 0.0 || *sunAzimuth > 360.0)) {
            return ActionValidationIssue{"sun_azimuth_deg", "参数“太阳方位角”应落在 [0, 360] 范围内"};
        }
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出栅格”应使用 .tif 或 .tiff"};
        }
    }

    if (pluginName == "georef" && actionKey == "minnaert_correction") {
        const auto band = intParamValue(params, "band");
        const auto sunZenith = doubleParamValue(params, "sun_zenith_deg");
        const auto sunAzimuth = doubleParamValue(params, "sun_azimuth_deg");
        const auto minnaertK = doubleParamValue(params, "minnaert_k");
        const std::string outputPath = stringParam("output");
        if (band.has_value() && *band <= 0) {
            return ActionValidationIssue{"band", "参数“波段序号”必须大于 0"};
        }
        if (sunZenith.has_value() && (*sunZenith < 0.0 || *sunZenith >= 90.0)) {
            return ActionValidationIssue{"sun_zenith_deg", "参数“太阳天顶角”应落在 [0, 90) 范围内"};
        }
        if (sunAzimuth.has_value() && (*sunAzimuth < 0.0 || *sunAzimuth > 360.0)) {
            return ActionValidationIssue{"sun_azimuth_deg", "参数“太阳方位角”应落在 [0, 360] 范围内"};
        }
        if (minnaertK.has_value() && *minnaertK <= 0.0) {
            return ActionValidationIssue{"minnaert_k", "参数“Minnaert 系数”必须大于 0"};
        }
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出栅格”应使用 .tif 或 .tiff"};
        }
    }

    if (pluginName == "georef" && actionKey == "c_correction") {
        const auto band = intParamValue(params, "band");
        const auto sunZenith = doubleParamValue(params, "sun_zenith_deg");
        const auto sunAzimuth = doubleParamValue(params, "sun_azimuth_deg");
        const auto cValue = doubleParamValue(params, "c_value");
        const std::string outputPath = stringParam("output");
        if (band.has_value() && *band <= 0) {
            return ActionValidationIssue{"band", "参数“波段序号”必须大于 0"};
        }
        if (sunZenith.has_value() && (*sunZenith < 0.0 || *sunZenith >= 90.0)) {
            return ActionValidationIssue{"sun_zenith_deg", "参数“太阳天顶角”应落在 [0, 90) 范围内"};
        }
        if (sunAzimuth.has_value() && (*sunAzimuth < 0.0 || *sunAzimuth > 360.0)) {
            return ActionValidationIssue{"sun_azimuth_deg", "参数“太阳方位角”应落在 [0, 360] 范围内"};
        }
        if (cValue.has_value() && *cValue < 0.0) {
            return ActionValidationIssue{"c_value", "参数“C 系数”不能小于 0"};
        }
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".tif", ".tiff"})) {
            return ActionValidationIssue{"output", "参数“输出栅格”应使用 .tif 或 .tiff"};
        }
    }

    if (pluginName == "georef" && actionKey == "percentile_stretch") {
        const auto darkPercentile = doubleParamValue(params, "dark_percentile");
        const auto brightPercentile = doubleParamValue(params, "bright_percentile");
        const std::string outputPath = stringParam("output");
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

    if (pluginName == "vector" && actionKey == "adjacency") {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".csv"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .csv"};
        }
    }

    if (pluginName == "vector" && actionKey == "overlap_check") {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".csv"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .csv"};
        }
    }

    if (pluginName == "vector" && actionKey == "topology_check") {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".csv"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .csv"};
        }
    }

    if (pluginName == "vector" && actionKey == "multipart_check") {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".csv"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .csv"};
        }
    }

    if (pluginName == "vector" && actionKey == "duplicate_point_check") {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".csv"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .csv"};
        }
    }

    if (pluginName == "vector" && actionKey == "hole_check") {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".csv"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .csv"};
        }
    }

    if (pluginName == "vector" && actionKey == "dangling_endpoint_check") {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".csv"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .csv"};
        }
    }

    if (pluginName == "vector" && actionKey == "sliver_remove") {
        const std::string outputPath = stringParam("output");
        if (!outputPath.empty() && !endsWithOneOf(outputPath, {".gpkg", ".geojson", ".json", ".shp", ".kml"})) {
            return ActionValidationIssue{"output", "参数“输出文件”应使用 .gpkg/.geojson/.json/.shp/.kml"};
        }
        const auto minArea = doubleParamValue(params, "min_area");
        if (minArea.has_value() && *minArea <= 0.0) {
            return ActionValidationIssue{"min_area", "参数“最小面积”必须大于 0"};
        }
    }

    if (pluginName == "vector" &&
        (actionKey == "union" || actionKey == "difference" ||
         actionKey == "intersect" || actionKey == "dissolve" ||
         actionKey == "spatial_join")) {
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

    if (pluginName == "vector" && actionKey == "simplify") {
        const auto tolerance = doubleParamValue(params, "tolerance");
        if (tolerance.has_value() && *tolerance <= 0.0) {
            return ActionValidationIssue{"tolerance", "参数“简化容差”必须大于 0"};
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
        if (actionKey == "gabor_filter") {
            const auto kernelSize = intParamValue(params, "kernel_size");
            if (kernelSize.has_value()) {
                if (*kernelSize < 3) {
                    return ActionValidationIssue{"kernel_size", "参数“核大小”必须大于等于 3"};
                }
                if ((*kernelSize % 2) == 0) {
                    return ActionValidationIssue{"kernel_size", "参数“核大小”建议填写奇数，例如 3、5、7"};
                }
            }
            if (const auto sigma = doubleParamValue(params, "sigma");
                sigma.has_value() && *sigma <= 0.0) {
                return ActionValidationIssue{"sigma", "参数“Sigma”必须大于 0"};
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
            if (kernelSize.has_value()) {
                if (*kernelSize < 3) {
                    return ActionValidationIssue{"kernel_size", "参数“窗口大小”必须大于等于 3"};
                }
                if ((*kernelSize % 2) == 0) {
                    return ActionValidationIssue{"kernel_size", "参数“窗口大小”建议填写奇数，例如 3、5、7"};
                }
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
        if (pluginName == "vector" && (spec.key == "resolution" || spec.key == "tolerance")) {
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
            } else if (spec.key == "sigma" || spec.key == "gabor_lambda" || spec.key == "gabor_gamma") {
                adjustedSpec.minValue = 0.000001;
            } else if (spec.key == "glcm_levels") {
                adjustedSpec.minValue = 2;
            } else if (spec.key == "spatial_radius" || spec.key == "color_radius") {
                adjustedSpec.minValue = 0.000001;
            } else if (spec.key == "pyramid_level") {
                adjustedSpec.minValue = 0;
            }
        }
        if (pluginName == "georef" && spec.key == "band") {
            adjustedSpec.minValue = 1;
        } else if (pluginName == "georef" && spec.key == "sun_zenith_deg") {
            adjustedSpec.minValue = 0.0;
            adjustedSpec.maxValue = 89.999999;
        } else if (pluginName == "georef" && spec.key == "sun_azimuth_deg") {
            adjustedSpec.minValue = 0.0;
            adjustedSpec.maxValue = 360.0;
        } else if (pluginName == "georef" && spec.key == "minnaert_k") {
            adjustedSpec.minValue = 0.000001;
        } else if (pluginName == "georef" && spec.key == "c_value") {
            adjustedSpec.minValue = 0.0;
        } else if (pluginName == "georef" && spec.key == "dark_percentile") {
            adjustedSpec.minValue = 0.0;
            adjustedSpec.maxValue = 99.999999;
        } else if (pluginName == "georef" && spec.key == "bright_percentile") {
            adjustedSpec.minValue = 0.000001;
            adjustedSpec.maxValue = 100.0;
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
            "待修正",
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


