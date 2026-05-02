#include "georef_plugin.h"

#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>

#include <cpl_conv.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <opencv2/core.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace gis::plugins {

namespace {

namespace fs = std::filesystem;

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string lowerString(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

std::string upperString(const std::string& value) {
    std::string uppered = value;
    std::transform(uppered.begin(), uppered.end(), uppered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return uppered;
}

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> columns;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) {
        columns.push_back(trim(item));
    }
    return columns;
}

bool tryParseDouble(const std::string& text, double& value) {
    try {
        std::size_t parsed = 0;
        value = std::stod(text, &parsed);
        return parsed == text.size();
    } catch (...) {
        return false;
    }
}

struct ParsedGcps {
    std::vector<GDAL_GCP> gcps;
};

struct RadiometricCoefficients {
    double gain = 1.0;
    double offset = 0.0;
};

bool loadGcpsFromCsv(const std::string& csvPath,
                     ParsedGcps& parsedGcps,
                     std::string& error) {
    std::ifstream ifs(csvPath, std::ios::binary);
    if (!ifs.is_open()) {
        error = "Cannot open gcp_file: " + csvPath;
        return false;
    }

    std::string headerLine;
    if (!std::getline(ifs, headerLine)) {
        error = "gcp_file is empty";
        return false;
    }

    const auto headers = splitCsvLine(headerLine);
    int pixelXIndex = -1;
    int pixelYIndex = -1;
    int mapXIndex = -1;
    int mapYIndex = -1;
    int mapZIndex = -1;
    for (int i = 0; i < static_cast<int>(headers.size()); ++i) {
        const std::string key = lowerString(headers[i]);
        if (key == "pixel_x") pixelXIndex = i;
        else if (key == "pixel_y") pixelYIndex = i;
        else if (key == "map_x") mapXIndex = i;
        else if (key == "map_y") mapYIndex = i;
        else if (key == "map_z") mapZIndex = i;
    }

    if (pixelXIndex < 0 || pixelYIndex < 0 || mapXIndex < 0 || mapYIndex < 0) {
        error = "gcp_file must contain header pixel_x,pixel_y,map_x,map_y";
        return false;
    }

    std::string line;
    int lineNumber = 1;
    while (std::getline(ifs, line)) {
        ++lineNumber;
        if (trim(line).empty()) {
            continue;
        }

        const auto columns = splitCsvLine(line);
        const int requiredColumns = std::max({pixelXIndex, pixelYIndex, mapXIndex, mapYIndex, mapZIndex});
        if (static_cast<int>(columns.size()) <= requiredColumns) {
            error = "Invalid GCP row at line " + std::to_string(lineNumber);
            return false;
        }

        double pixelX = 0.0;
        double pixelY = 0.0;
        double mapX = 0.0;
        double mapY = 0.0;
        double mapZ = 0.0;
        if (!tryParseDouble(columns[pixelXIndex], pixelX) ||
            !tryParseDouble(columns[pixelYIndex], pixelY) ||
            !tryParseDouble(columns[mapXIndex], mapX) ||
            !tryParseDouble(columns[mapYIndex], mapY)) {
            error = "Invalid numeric value in gcp_file at line " + std::to_string(lineNumber);
            return false;
        }

        if (mapZIndex >= 0 && !columns[mapZIndex].empty() &&
            !tryParseDouble(columns[mapZIndex], mapZ)) {
            error = "Invalid map_z value in gcp_file at line " + std::to_string(lineNumber);
            return false;
        }

        GDAL_GCP gcp;
        GDALInitGCPs(1, &gcp);
        gcp.dfGCPPixel = pixelX;
        gcp.dfGCPLine = pixelY;
        gcp.dfGCPX = mapX;
        gcp.dfGCPY = mapY;
        gcp.dfGCPZ = mapZ;
        parsedGcps.gcps.push_back(gcp);
    }

    if (parsedGcps.gcps.size() < 3) {
        error = "gcp_file must contain at least 3 control points";
        return false;
    }

    return true;
}

void freeParsedGcps(ParsedGcps& parsedGcps) {
    if (!parsedGcps.gcps.empty()) {
        GDALDeinitGCPs(static_cast<int>(parsedGcps.gcps.size()), parsedGcps.gcps.data());
        parsedGcps.gcps.clear();
    }
}

std::string uniqueTempVrtPath(const std::string& outputPath) {
    const fs::path basePath = fs::temp_directory_path() /
        ("gis_tool_georef_" + std::to_string(std::hash<std::string>{}(outputPath)));
    fs::create_directories(basePath);
    return (basePath / "gcp_register_input.vrt").string();
}

float percentileValue(std::vector<float>& values, double percentile) {
    if (values.empty()) {
        return 0.0f;
    }
    const double clamped = std::clamp(percentile, 0.0, 100.0);
    const std::size_t index = static_cast<std::size_t>(
        std::round((clamped / 100.0) * static_cast<double>(values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

std::optional<double> lookupMetadataValue(
    const std::map<std::string, std::string>& metadata,
    const std::vector<std::string>& keys) {
    for (const auto& key : keys) {
        const auto it = metadata.find(key);
        if (it == metadata.end()) {
            continue;
        }
        double value = 0.0;
        if (tryParseDouble(it->second, value)) {
            return value;
        }
    }
    return std::nullopt;
}

bool loadRadiometricCoefficients(const std::string& metadataFile,
                                 int band,
                                 RadiometricCoefficients& coefficients,
                                 std::string& error) {
    std::ifstream ifs(metadataFile, std::ios::binary);
    if (!ifs.is_open()) {
        error = "Cannot open metadata_file: " + metadataFile;
        return false;
    }

    std::map<std::string, std::string> metadata;
    std::string line;
    while (std::getline(ifs, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const auto eqPos = line.find('=');
        const auto colonPos = line.find(':');
        std::size_t sepPos = std::string::npos;
        if (eqPos != std::string::npos) {
            sepPos = eqPos;
        } else if (colonPos != std::string::npos) {
            sepPos = colonPos;
        }
        if (sepPos == std::string::npos) {
            continue;
        }

        std::string key = upperString(trim(line.substr(0, sepPos)));
        std::string value = trim(line.substr(sepPos + 1));
        if (!key.empty() && !value.empty()) {
            metadata[key] = value;
        }
    }

    const std::string bandSuffix = std::to_string(band);
    const auto gain = lookupMetadataValue(metadata, {
        "GAIN",
        "RADIANCE_MULT_BAND_" + bandSuffix,
        "REFLECTANCE_MULT_BAND_" + bandSuffix,
        "BAND_" + bandSuffix + "_GAIN"
    });
    const auto offset = lookupMetadataValue(metadata, {
        "OFFSET",
        "RADIANCE_ADD_BAND_" + bandSuffix,
        "REFLECTANCE_ADD_BAND_" + bandSuffix,
        "BAND_" + bandSuffix + "_OFFSET"
    });

    if (!gain.has_value() || !offset.has_value()) {
        error = "metadata_file does not contain gain/offset for requested band";
        return false;
    }

    coefficients.gain = *gain;
    coefficients.offset = *offset;
    return true;
}

} // namespace

std::vector<gis::framework::ParamSpec> GeorefPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "几何校正与辐射处理功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0}, {
                "dos_correction", "radiometric_calibration", "gcp_register",
                "cosine_correction", "minnaert_correction", "c_correction",
                "quac_correction", "rpc_orthorectify"
            }
        },
        gis::framework::ParamSpec{
            "input", "输入栅格", "待处理栅格影像路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出栅格", "输出结果栅格路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "band", "波段序号", "待处理波段序号，从 1 开始",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "dark_object_value", "暗像元值", "小于 0 时自动使用当前波段最小值",
            gis::framework::ParamType::Double, false, double{-1.0}
        },
        gis::framework::ParamSpec{
            "gain", "增益", "辐射定标增益系数",
            gis::framework::ParamType::Double, false, double{1.0}
        },
        gis::framework::ParamSpec{
            "offset", "偏移", "辐射定标偏移量",
            gis::framework::ParamType::Double, false, double{0.0}
        },
        gis::framework::ParamSpec{
            "metadata_file", "元数据文件", "可选，自动读取辐射定标系数的元数据文件",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "gcp_file", "控制点文件", "CSV 控制点文件，表头应包含 pixel_x,pixel_y,map_x,map_y",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "dst_srs", "目标坐标系", "控制点坐标对应的目标坐标系，例如 EPSG:4326",
            gis::framework::ParamType::CRS, false, std::string{}
        },
        gis::framework::ParamSpec{
            "resample", "重采样方法", "控制点配准后的重采样算法",
            gis::framework::ParamType::Enum, false, std::string{"nearest"},
            int{0}, int{0}, {"nearest", "bilinear", "cubic", "cubicspline", "lanczos", "average"}
        },
        gis::framework::ParamSpec{
            "slope_raster", "坡度栅格", "坡度栅格文件，像元值单位为度",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "aspect_raster", "坡向栅格", "坡向栅格文件，像元值单位为度",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "sun_zenith_deg", "太阳天顶角", "太阳天顶角，单位为度",
            gis::framework::ParamType::Double, false, double{30.0}
        },
        gis::framework::ParamSpec{
            "sun_azimuth_deg", "太阳方位角", "太阳方位角，单位为度",
            gis::framework::ParamType::Double, false, double{180.0}
        },
        gis::framework::ParamSpec{
            "minnaert_k", "Minnaert 系数", "Minnaert 地形校正系数",
            gis::framework::ParamType::Double, false, double{0.5}
        },
        gis::framework::ParamSpec{
            "c_value", "C 系数", "C 地形校正系数",
            gis::framework::ParamType::Double, false, double{0.1}
        },
        gis::framework::ParamSpec{
            "dark_percentile", "暗像元百分位", "简化 QUAC 使用的暗像元百分位",
            gis::framework::ParamType::Double, false, double{1.0}
        },
        gis::framework::ParamSpec{
            "bright_percentile", "亮像元百分位", "简化 QUAC 使用的亮像元百分位",
            gis::framework::ParamType::Double, false, double{99.0}
        },
        gis::framework::ParamSpec{
            "dem_file", "DEM 文件", "RPC 正射校正使用的可选 DEM 文件",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "rpc_height", "固定高程", "不使用 DEM 时的固定高程值",
            gis::framework::ParamType::Double, false, double{0.0}
        },
    };
}

gis::framework::Result GeorefPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string action = gis::framework::getParam<std::string>(params, "action", "");
    if (action == "dos_correction") {
        return doDosCorrection(params, progress);
    }
    if (action == "radiometric_calibration") {
        return doRadiometricCalibration(params, progress);
    }
    if (action == "gcp_register") {
        return doGcpRegister(params, progress);
    }
    if (action == "cosine_correction") {
        return doCosineCorrection(params, progress);
    }
    if (action == "minnaert_correction") {
        return doMinnaertCorrection(params, progress);
    }
    if (action == "c_correction") {
        return doCCorrection(params, progress);
    }
    if (action == "quac_correction") {
        return doQuacCorrection(params, progress);
    }
    if (action == "rpc_orthorectify") {
        return doRpcOrthorectify(params, progress);
    }
    return gis::framework::Result::fail("Unknown action: " + action);
}

gis::framework::Result GeorefPlugin::doDosCorrection(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const int band = gis::framework::getParam<int>(params, "band", 1);
    double darkObject = gis::framework::getParam<double>(params, "dark_object_value", -1.0);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");

    progress.onMessage("Reading raster band for DOS correction");
    progress.onProgress(0.2);
    cv::Mat bandMat = gis::core::readBandAsMat(input, band);

    double minValue = 0.0;
    double maxValue = 0.0;
    cv::minMaxLoc(bandMat, &minValue, &maxValue);
    if (darkObject < 0.0) {
        darkObject = minValue;
    }

    cv::Mat corrected = bandMat - darkObject;
    cv::max(corrected, 0.0, corrected);

    const double scaleDenominator = maxValue - darkObject;
    if (scaleDenominator > 1e-12) {
        corrected.convertTo(corrected, CV_32F, 1.0 / scaleDenominator);
    } else {
        corrected = cv::Mat::zeros(corrected.size(), CV_32F);
    }

    progress.onMessage("Writing DOS correction result");
    progress.onProgress(0.75);
    gis::core::matToGdalTiff(corrected, input, output, band);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("DOS correction completed", output);
    result.metadata["action"] = "dos_correction";
    result.metadata["band"] = std::to_string(band);
    result.metadata["dark_object_value"] = std::to_string(darkObject);
    return result;
}

gis::framework::Result GeorefPlugin::doRadiometricCalibration(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string metadataFile = gis::framework::getParam<std::string>(params, "metadata_file", "");
    const int band = gis::framework::getParam<int>(params, "band", 1);
    double gain = gis::framework::getParam<double>(params, "gain", 1.0);
    double offset = gis::framework::getParam<double>(params, "offset", 0.0);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");

    if (!metadataFile.empty()) {
        RadiometricCoefficients coefficients;
        std::string error;
        if (!loadRadiometricCoefficients(metadataFile, band, coefficients, error)) {
            return gis::framework::Result::fail(error);
        }
        gain = coefficients.gain;
        offset = coefficients.offset;
    }

    progress.onMessage("Reading raster band for radiometric calibration");
    progress.onProgress(0.2);
    cv::Mat bandMat = gis::core::readBandAsMat(input, band);

    cv::Mat calibrated = bandMat * gain + offset;
    calibrated.convertTo(calibrated, CV_32F);

    progress.onMessage("Writing radiometric calibration result");
    progress.onProgress(0.75);
    gis::core::matToGdalTiff(calibrated, input, output, band);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("Radiometric calibration completed", output);
    result.metadata["action"] = "radiometric_calibration";
    result.metadata["band"] = std::to_string(band);
    result.metadata["gain"] = std::to_string(gain);
    result.metadata["offset"] = std::to_string(offset);
    if (!metadataFile.empty()) {
        result.metadata["metadata_file"] = metadataFile;
    }
    return result;
}

gis::framework::Result GeorefPlugin::doGcpRegister(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string gcpFile = gis::framework::getParam<std::string>(params, "gcp_file", "");
    const std::string dstSrs = gis::framework::getParam<std::string>(params, "dst_srs", "");
    const std::string resample = gis::framework::getParam<std::string>(params, "resample", "nearest");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (gcpFile.empty()) return gis::framework::Result::fail("gcp_file is required");
    if (dstSrs.empty()) return gis::framework::Result::fail("dst_srs is required");

    ParsedGcps parsedGcps;
    std::string parseError;
    if (!loadGcpsFromCsv(gcpFile, parsedGcps, parseError)) {
        return gis::framework::Result::fail(parseError);
    }

    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        freeParsedGcps(parsedGcps);
        return gis::framework::Result::fail("Cannot open input raster: " + input);
    }

    const std::string tempVrtPath = uniqueTempVrtPath(output);
    GDALDriver* vrtDriver = GetGDALDriverManager()->GetDriverByName("VRT");
    if (!vrtDriver) {
        freeParsedGcps(parsedGcps);
        return gis::framework::Result::fail("VRT driver is unavailable");
    }

    progress.onMessage("Preparing temporary dataset with control points");
    progress.onProgress(0.2);

    GDALDataset* vrtRaw = vrtDriver->CreateCopy(tempVrtPath.c_str(), srcDs.get(), FALSE, nullptr, nullptr, nullptr);
    if (!vrtRaw) {
        freeParsedGcps(parsedGcps);
        return gis::framework::Result::fail("Failed to create temporary VRT dataset");
    }
    gis::core::GdalDatasetPtr vrtDs(vrtRaw);

    std::unique_ptr<OGRSpatialReference> dstSrsRef(gis::core::parseSRS(dstSrs));
    if (!dstSrsRef) {
        freeParsedGcps(parsedGcps);
        return gis::framework::Result::fail("Invalid dst_srs: " + dstSrs);
    }

    char* dstSrsWkt = nullptr;
    if (dstSrsRef->exportToWkt(&dstSrsWkt) != OGRERR_NONE || !dstSrsWkt) {
        freeParsedGcps(parsedGcps);
        return gis::framework::Result::fail("Failed to export dst_srs to WKT");
    }

    if (vrtDs->SetGCPs(static_cast<int>(parsedGcps.gcps.size()), parsedGcps.gcps.data(), dstSrsWkt) != CE_None) {
        CPLFree(dstSrsWkt);
        freeParsedGcps(parsedGcps);
        return gis::framework::Result::fail("Failed to apply GCPs: " + std::string(CPLGetLastErrorMsg()));
    }
    CPLFree(dstSrsWkt);

    progress.onMessage("Warping raster with GCP transform");
    progress.onProgress(0.55);

    std::vector<std::string> argStorage = {
        "-t_srs", dstSrs,
        "-r", resample,
        "-order", "1"
    };

    std::vector<const char*> warpArgs;
    for (auto& arg : argStorage) {
        warpArgs.push_back(arg.c_str());
    }
    warpArgs.push_back(nullptr);

    GDALWarpAppOptions* warpOpts = GDALWarpAppOptionsNew(const_cast<char**>(warpArgs.data()), nullptr);
    if (!warpOpts) {
        freeParsedGcps(parsedGcps);
        return gis::framework::Result::fail("Failed to create warp options: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALDatasetH srcHandle = static_cast<GDALDatasetH>(vrtDs.get());
    int errCode = 0;
    GDALDatasetH dstHandle = GDALWarp(output.c_str(), nullptr, 1, &srcHandle, warpOpts, &errCode);
    GDALWarpAppOptionsFree(warpOpts);
    const std::size_t gcpCount = parsedGcps.gcps.size();
    freeParsedGcps(parsedGcps);

    if (!dstHandle || errCode) {
        if (dstHandle) {
            GDALClose(dstHandle);
        }
        return gis::framework::Result::fail("GCP registration failed: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALClose(dstHandle);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("GCP registration completed", output);
    result.metadata["action"] = "gcp_register";
    result.metadata["gcp_file"] = gcpFile;
    result.metadata["dst_srs"] = dstSrs;
    result.metadata["gcp_count"] = std::to_string(gcpCount);
    result.metadata["resample"] = resample;
    return result;
}

gis::framework::Result GeorefPlugin::doCosineCorrection(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string slopeRaster = gis::framework::getParam<std::string>(params, "slope_raster", "");
    const std::string aspectRaster = gis::framework::getParam<std::string>(params, "aspect_raster", "");
    const int band = gis::framework::getParam<int>(params, "band", 1);
    const double sunZenithDeg = gis::framework::getParam<double>(params, "sun_zenith_deg", 30.0);
    const double sunAzimuthDeg = gis::framework::getParam<double>(params, "sun_azimuth_deg", 180.0);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (slopeRaster.empty()) return gis::framework::Result::fail("slope_raster is required");
    if (aspectRaster.empty()) return gis::framework::Result::fail("aspect_raster is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");

    progress.onMessage("Reading raster, slope and aspect for cosine correction");
    progress.onProgress(0.2);

    cv::Mat inputMat = gis::core::readBandAsMat(input, band);
    cv::Mat slopeMat = gis::core::readBandAsMat(slopeRaster, 1);
    cv::Mat aspectMat = gis::core::readBandAsMat(aspectRaster, 1);

    if (inputMat.size() != slopeMat.size() || inputMat.size() != aspectMat.size()) {
        return gis::framework::Result::fail("input, slope_raster and aspect_raster must have the same size");
    }

    cv::Mat input32f;
    cv::Mat slope32f;
    cv::Mat aspect32f;
    inputMat.convertTo(input32f, CV_32F);
    slopeMat.convertTo(slope32f, CV_32F);
    aspectMat.convertTo(aspect32f, CV_32F);

    const float degToRad = static_cast<float>(CV_PI / 180.0);
    const float sunZenithRad = static_cast<float>(sunZenithDeg) * degToRad;
    const float sunAzimuthRad = static_cast<float>(sunAzimuthDeg) * degToRad;
    const float cosSunZenith = std::cos(sunZenithRad);

    cv::Mat corrected = cv::Mat::zeros(input32f.size(), CV_32F);
    for (int row = 0; row < input32f.rows; ++row) {
        const float* inputPtr = input32f.ptr<float>(row);
        const float* slopePtr = slope32f.ptr<float>(row);
        const float* aspectPtr = aspect32f.ptr<float>(row);
        float* outputPtr = corrected.ptr<float>(row);
        for (int col = 0; col < input32f.cols; ++col) {
            const float slopeRad = slopePtr[col] * degToRad;
            const float aspectRad = aspectPtr[col] * degToRad;
            const float cosIncidence =
                std::cos(slopeRad) * cosSunZenith +
                std::sin(slopeRad) * std::sin(sunZenithRad) * std::cos(sunAzimuthRad - aspectRad);

            if (cosIncidence > 1e-6f) {
                outputPtr[col] = inputPtr[col] * (cosSunZenith / cosIncidence);
            } else {
                outputPtr[col] = 0.0f;
            }
        }
    }

    progress.onMessage("Writing cosine correction result");
    progress.onProgress(0.8);
    gis::core::matToGdalTiff(corrected, input, output, band);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("Cosine correction completed", output);
    result.metadata["action"] = "cosine_correction";
    result.metadata["band"] = std::to_string(band);
    result.metadata["sun_zenith_deg"] = std::to_string(sunZenithDeg);
    result.metadata["sun_azimuth_deg"] = std::to_string(sunAzimuthDeg);
    return result;
}

gis::framework::Result GeorefPlugin::doMinnaertCorrection(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string slopeRaster = gis::framework::getParam<std::string>(params, "slope_raster", "");
    const std::string aspectRaster = gis::framework::getParam<std::string>(params, "aspect_raster", "");
    const int band = gis::framework::getParam<int>(params, "band", 1);
    const double sunZenithDeg = gis::framework::getParam<double>(params, "sun_zenith_deg", 30.0);
    const double sunAzimuthDeg = gis::framework::getParam<double>(params, "sun_azimuth_deg", 180.0);
    const double minnaertK = gis::framework::getParam<double>(params, "minnaert_k", 0.5);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (slopeRaster.empty()) return gis::framework::Result::fail("slope_raster is required");
    if (aspectRaster.empty()) return gis::framework::Result::fail("aspect_raster is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");

    progress.onMessage("Reading raster, slope and aspect for Minnaert correction");
    progress.onProgress(0.2);

    cv::Mat inputMat = gis::core::readBandAsMat(input, band);
    cv::Mat slopeMat = gis::core::readBandAsMat(slopeRaster, 1);
    cv::Mat aspectMat = gis::core::readBandAsMat(aspectRaster, 1);

    if (inputMat.size() != slopeMat.size() || inputMat.size() != aspectMat.size()) {
        return gis::framework::Result::fail("input, slope_raster and aspect_raster must have the same size");
    }

    cv::Mat input32f;
    cv::Mat slope32f;
    cv::Mat aspect32f;
    inputMat.convertTo(input32f, CV_32F);
    slopeMat.convertTo(slope32f, CV_32F);
    aspectMat.convertTo(aspect32f, CV_32F);

    const float degToRad = static_cast<float>(CV_PI / 180.0);
    const float sunZenithRad = static_cast<float>(sunZenithDeg) * degToRad;
    const float sunAzimuthRad = static_cast<float>(sunAzimuthDeg) * degToRad;
    const float cosSunZenith = std::cos(sunZenithRad);

    cv::Mat corrected = cv::Mat::zeros(input32f.size(), CV_32F);
    for (int row = 0; row < input32f.rows; ++row) {
        const float* inputPtr = input32f.ptr<float>(row);
        const float* slopePtr = slope32f.ptr<float>(row);
        const float* aspectPtr = aspect32f.ptr<float>(row);
        float* outputPtr = corrected.ptr<float>(row);
        for (int col = 0; col < input32f.cols; ++col) {
            const float slopeRad = slopePtr[col] * degToRad;
            const float aspectRad = aspectPtr[col] * degToRad;
            const float cosIncidence =
                std::cos(slopeRad) * cosSunZenith +
                std::sin(slopeRad) * std::sin(sunZenithRad) * std::cos(sunAzimuthRad - aspectRad);

            if (cosIncidence > 1e-6f && cosSunZenith > 1e-6f) {
                outputPtr[col] = inputPtr[col] *
                    std::pow(cosSunZenith / cosIncidence, static_cast<float>(minnaertK));
            } else {
                outputPtr[col] = 0.0f;
            }
        }
    }

    progress.onMessage("Writing Minnaert correction result");
    progress.onProgress(0.8);
    gis::core::matToGdalTiff(corrected, input, output, band);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("Minnaert correction completed", output);
    result.metadata["action"] = "minnaert_correction";
    result.metadata["band"] = std::to_string(band);
    result.metadata["sun_zenith_deg"] = std::to_string(sunZenithDeg);
    result.metadata["sun_azimuth_deg"] = std::to_string(sunAzimuthDeg);
    result.metadata["minnaert_k"] = std::to_string(minnaertK);
    return result;
}

gis::framework::Result GeorefPlugin::doCCorrection(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string slopeRaster = gis::framework::getParam<std::string>(params, "slope_raster", "");
    const std::string aspectRaster = gis::framework::getParam<std::string>(params, "aspect_raster", "");
    const int band = gis::framework::getParam<int>(params, "band", 1);
    const double sunZenithDeg = gis::framework::getParam<double>(params, "sun_zenith_deg", 30.0);
    const double sunAzimuthDeg = gis::framework::getParam<double>(params, "sun_azimuth_deg", 180.0);
    const double cValue = gis::framework::getParam<double>(params, "c_value", 0.1);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (slopeRaster.empty()) return gis::framework::Result::fail("slope_raster is required");
    if (aspectRaster.empty()) return gis::framework::Result::fail("aspect_raster is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");

    progress.onMessage("Reading raster, slope and aspect for C correction");
    progress.onProgress(0.2);

    cv::Mat inputMat = gis::core::readBandAsMat(input, band);
    cv::Mat slopeMat = gis::core::readBandAsMat(slopeRaster, 1);
    cv::Mat aspectMat = gis::core::readBandAsMat(aspectRaster, 1);

    if (inputMat.size() != slopeMat.size() || inputMat.size() != aspectMat.size()) {
        return gis::framework::Result::fail("input, slope_raster and aspect_raster must have the same size");
    }

    cv::Mat input32f;
    cv::Mat slope32f;
    cv::Mat aspect32f;
    inputMat.convertTo(input32f, CV_32F);
    slopeMat.convertTo(slope32f, CV_32F);
    aspectMat.convertTo(aspect32f, CV_32F);

    const float degToRad = static_cast<float>(CV_PI / 180.0);
    const float sunZenithRad = static_cast<float>(sunZenithDeg) * degToRad;
    const float sunAzimuthRad = static_cast<float>(sunAzimuthDeg) * degToRad;
    const float cosSunZenith = std::cos(sunZenithRad);

    cv::Mat corrected = cv::Mat::zeros(input32f.size(), CV_32F);
    for (int row = 0; row < input32f.rows; ++row) {
        const float* inputPtr = input32f.ptr<float>(row);
        const float* slopePtr = slope32f.ptr<float>(row);
        const float* aspectPtr = aspect32f.ptr<float>(row);
        float* outputPtr = corrected.ptr<float>(row);
        for (int col = 0; col < input32f.cols; ++col) {
            const float slopeRad = slopePtr[col] * degToRad;
            const float aspectRad = aspectPtr[col] * degToRad;
            const float cosIncidence =
                std::cos(slopeRad) * cosSunZenith +
                std::sin(slopeRad) * std::sin(sunZenithRad) * std::cos(sunAzimuthRad - aspectRad);

            if (cosIncidence + static_cast<float>(cValue) > 1e-6f) {
                outputPtr[col] = inputPtr[col] *
                    ((cosSunZenith + static_cast<float>(cValue)) /
                     (cosIncidence + static_cast<float>(cValue)));
            } else {
                outputPtr[col] = 0.0f;
            }
        }
    }

    progress.onMessage("Writing C correction result");
    progress.onProgress(0.8);
    gis::core::matToGdalTiff(corrected, input, output, band);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("C correction completed", output);
    result.metadata["action"] = "c_correction";
    result.metadata["band"] = std::to_string(band);
    result.metadata["sun_zenith_deg"] = std::to_string(sunZenithDeg);
    result.metadata["sun_azimuth_deg"] = std::to_string(sunAzimuthDeg);
    result.metadata["c_value"] = std::to_string(cValue);
    return result;
}

gis::framework::Result GeorefPlugin::doQuacCorrection(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const double darkPercentile = gis::framework::getParam<double>(params, "dark_percentile", 1.0);
    const double brightPercentile = gis::framework::getParam<double>(params, "bright_percentile", 99.0);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (darkPercentile < 0.0 || darkPercentile >= 100.0) {
        return gis::framework::Result::fail("dark_percentile must be in [0, 100)");
    }
    if (brightPercentile <= 0.0 || brightPercentile > 100.0) {
        return gis::framework::Result::fail("bright_percentile must be in (0, 100]");
    }
    if (darkPercentile >= brightPercentile) {
        return gis::framework::Result::fail("dark_percentile must be less than bright_percentile");
    }

    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        return gis::framework::Result::fail("Cannot open input raster: " + input);
    }

    const int bandCount = srcDs->GetRasterCount();
    std::vector<cv::Mat> correctedBands;
    correctedBands.reserve(bandCount);

    progress.onMessage("Reading raster bands for QUAC correction");
    progress.onProgress(0.15);

    for (int bandIndex = 1; bandIndex <= bandCount; ++bandIndex) {
        cv::Mat bandMat = gis::core::gdalBandToMat(srcDs.get(), bandIndex);
        bandMat.convertTo(bandMat, CV_32F);

        std::vector<float> darkPixels;
        darkPixels.assign(reinterpret_cast<float*>(bandMat.data),
                          reinterpret_cast<float*>(bandMat.data) + static_cast<std::size_t>(bandMat.total()));
        std::vector<float> brightPixels = darkPixels;

        const float darkValue = percentileValue(darkPixels, darkPercentile);
        const float brightValue = percentileValue(brightPixels, brightPercentile);

        cv::Mat corrected = bandMat - darkValue;
        cv::max(corrected, 0.0, corrected);

        const float denominator = brightValue - darkValue;
        if (denominator > 1e-6f) {
            corrected.convertTo(corrected, CV_32F, 1.0 / denominator);
        } else {
            corrected = cv::Mat::zeros(corrected.size(), CV_32F);
        }
        cv::min(corrected, 1.0, corrected);
        correctedBands.push_back(corrected);
    }

    progress.onMessage("Writing QUAC correction result");
    progress.onProgress(0.8);
    gis::core::matsToGdalTiff(correctedBands, srcDs.get(), output);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("QUAC correction completed", output);
    result.metadata["action"] = "quac_correction";
    result.metadata["band_count"] = std::to_string(bandCount);
    result.metadata["dark_percentile"] = std::to_string(darkPercentile);
    result.metadata["bright_percentile"] = std::to_string(brightPercentile);
    return result;
}

gis::framework::Result GeorefPlugin::doRpcOrthorectify(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string dstSrs = gis::framework::getParam<std::string>(params, "dst_srs", "EPSG:4326");
    const std::string demFile = gis::framework::getParam<std::string>(params, "dem_file", "");
    const std::string resample = gis::framework::getParam<std::string>(params, "resample", "nearest");
    const double rpcHeight = gis::framework::getParam<double>(params, "rpc_height", 0.0);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (dstSrs.empty()) return gis::framework::Result::fail("dst_srs is required");

    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        return gis::framework::Result::fail("Cannot open input raster: " + input);
    }

    char** rpcMetadata = srcDs->GetMetadata("RPC");
    if (!rpcMetadata || CSLCount(rpcMetadata) == 0) {
        return gis::framework::Result::fail("Input raster does not contain RPC metadata");
    }

    progress.onMessage("Running RPC orthorectification");
    progress.onProgress(0.2);

    std::vector<std::string> argStorage = {
        "-rpc",
        "-t_srs", dstSrs,
        "-r", resample
    };

    if (!demFile.empty()) {
        argStorage.push_back("-to");
        argStorage.push_back("RPC_DEM=" + demFile);
    } else {
        argStorage.push_back("-to");
        argStorage.push_back("RPC_HEIGHT=" + std::to_string(rpcHeight));
    }

    std::vector<const char*> warpArgs;
    for (auto& arg : argStorage) {
        warpArgs.push_back(arg.c_str());
    }
    warpArgs.push_back(nullptr);

    GDALWarpAppOptions* warpOpts = GDALWarpAppOptionsNew(const_cast<char**>(warpArgs.data()), nullptr);
    if (!warpOpts) {
        return gis::framework::Result::fail("Failed to create warp options: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALDatasetH srcHandle = static_cast<GDALDatasetH>(srcDs.get());
    int errCode = 0;
    GDALDatasetH dstHandle = GDALWarp(output.c_str(), nullptr, 1, &srcHandle, warpOpts, &errCode);
    GDALWarpAppOptionsFree(warpOpts);

    if (!dstHandle || errCode) {
        if (dstHandle) {
            GDALClose(dstHandle);
        }
        return gis::framework::Result::fail("RPC orthorectification failed: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALClose(dstHandle);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("RPC orthorectification completed", output);
    result.metadata["action"] = "rpc_orthorectify";
    result.metadata["dst_srs"] = dstSrs;
    result.metadata["resample"] = resample;
    if (!demFile.empty()) {
        result.metadata["dem_file"] = demFile;
    } else {
        result.metadata["rpc_height"] = std::to_string(rpcHeight);
    }
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::GeorefPlugin)
