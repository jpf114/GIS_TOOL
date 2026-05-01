#include "spindex_plugin.h"

#include <gis/core/error.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>
#include <gis/core/spindex_presets.h>

#include <gdal_priv.h>
#include <opencv2/opencv.hpp>

#include <cctype>
#include <cmath>
#include <map>
#include <stack>
#include <stdexcept>
#include <string>
#include <vector>

namespace gis::plugins {

namespace {

int getBandIndex(const std::map<std::string, gis::framework::ParamValue>& params,
                 const char* key,
                 int defaultValue,
                 int bandCount) {
    const int bandIndex = gis::framework::getParam<int>(params, key, defaultValue);
    if (bandIndex < 1 || bandIndex > bandCount) {
        throw std::runtime_error(
            std::string(key) + " " + std::to_string(bandIndex) +
            " out of range (1-" + std::to_string(bandCount) + ")");
    }
    return bandIndex;
}

cv::Mat readBandMat(const gis::core::GdalDatasetPtr& ds,
                    int bandIndex,
                    const std::string& bandLabel,
                    gis::core::ProgressReporter& progress) {
    progress.onMessage("Reading " + bandLabel + " band " + std::to_string(bandIndex));
    return gis::core::gdalBandToMat(ds.get(), bandIndex);
}

cv::Mat safeDivide(const cv::Mat& numerator, cv::Mat denominator) {
    denominator += 1e-10f;
    cv::Mat result;
    cv::divide(numerator, denominator, result);
    return result;
}

gis::framework::Result buildIndexResult(const std::string& indexName,
                                        const std::string& output,
                                        const cv::Mat& indexMat) {
    double minValue = 0.0;
    double maxValue = 0.0;
    cv::minMaxLoc(indexMat, &minValue, &maxValue);

    auto result = gis::framework::Result::ok(
        indexName + " computed: range [" + std::to_string(minValue) + ", " +
            std::to_string(maxValue) + "]",
        output);
    result.metadata["index"] = indexName;
    result.metadata["index_min"] = std::to_string(minValue);
    result.metadata["index_max"] = std::to_string(maxValue);
    return result;
}

double evalExpression(const std::string& expr,
                      const std::map<std::string, double>& bandValues) {
    std::string resolved = expr;
    for (const auto& [key, value] : bandValues) {
        const std::string replacement = std::to_string(value);
        size_t pos = 0;
        while ((pos = resolved.find(key, pos)) != std::string::npos) {
            resolved.replace(pos, key.length(), replacement);
            pos += replacement.length();
        }
    }

    try {
        std::stack<double> values;
        std::stack<char> ops;
        auto precedence = [](char op) -> int {
            if (op == '+' || op == '-') return 1;
            if (op == '*' || op == '/') return 2;
            return 0;
        };
        auto applyOp = [](double a, double b, char op) -> double {
            switch (op) {
                case '+': return a + b;
                case '-': return a - b;
                case '*': return a * b;
                case '/': return std::abs(b) < 1e-15 ? 0.0 : a / b;
                default: return 0.0;
            }
        };

        size_t i = 0;
        while (i < resolved.size()) {
            if (std::isspace(static_cast<unsigned char>(resolved[i]))) {
                ++i;
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(resolved[i])) || resolved[i] == '.') {
                const size_t start = i;
                while (i < resolved.size() &&
                       (std::isdigit(static_cast<unsigned char>(resolved[i])) || resolved[i] == '.')) {
                    ++i;
                }
                values.push(std::stod(resolved.substr(start, i - start)));
                continue;
            }

            if (resolved[i] == '(') {
                ops.push('(');
                ++i;
                continue;
            }

            if (resolved[i] == ')') {
                while (!ops.empty() && ops.top() != '(') {
                    const double b = values.top();
                    values.pop();
                    const double a = values.top();
                    values.pop();
                    values.push(applyOp(a, b, ops.top()));
                    ops.pop();
                }
                if (!ops.empty()) {
                    ops.pop();
                }
                ++i;
                continue;
            }

            if (resolved[i] == '+' || resolved[i] == '-' ||
                resolved[i] == '*' || resolved[i] == '/') {
                while (!ops.empty() && precedence(ops.top()) >= precedence(resolved[i])) {
                    const double b = values.top();
                    values.pop();
                    const double a = values.top();
                    values.pop();
                    values.push(applyOp(a, b, ops.top()));
                    ops.pop();
                }
                ops.push(resolved[i]);
            }
            ++i;
        }

        while (!ops.empty()) {
            const double b = values.top();
            values.pop();
            const double a = values.top();
            values.pop();
            values.push(applyOp(a, b, ops.top()));
            ops.pop();
        }

        return values.empty() ? 0.0 : values.top();
    } catch (...) {
        return 0.0;
    }
}

bool expressionContainsAlias(const std::string& expression, const std::string& alias) {
    return expression.find(alias) != std::string::npos;
}

int resolveOptionalAliasBand(const std::map<std::string, gis::framework::ParamValue>& params,
                             const char* key,
                             int defaultValue,
                             int bandCount,
                             const std::string& expression,
                             const char* alias) {
    if (expressionContainsAlias(expression, alias)) {
        return getBandIndex(params, key, defaultValue, bandCount);
    }
    if (bandCount <= 0) {
        throw std::runtime_error("invalid band count");
    }
    return std::min(defaultValue, bandCount);
}

std::map<std::string, double> buildCustomExpressionValues(
    const std::vector<cv::Mat>& bandMats,
    int x,
    int y,
    int blueBand,
    int greenBand,
    int redBand,
    int nirBand,
    int swir1Band) {
    std::map<std::string, double> values;
    for (size_t band = 0; band < bandMats.size(); ++band) {
        values["B" + std::to_string(band + 1)] = bandMats[band].at<float>(y, x);
    }

    auto bindAlias = [&](const char* alias, int bandIndex) {
        values[alias] = bandMats[static_cast<size_t>(bandIndex - 1)].at<float>(y, x);
    };
    bindAlias("BLUE", blueBand);
    bindAlias("GREEN", greenBand);
    bindAlias("RED", redBand);
    bindAlias("NIR", nirBand);
    bindAlias("SWIR1", swir1Band);
    return values;
}

gis::framework::Result doCustomIndex(const std::string& input,
                                     const std::string& output,
                                     const std::string& expression,
                                     const std::string& preset,
                                     int blueBand,
                                     int greenBand,
                                     int redBand,
                                     int nirBand,
                                     int swir1Band,
                                     gis::core::ProgressReporter& progress) {
    const std::string resolvedExpression =
        expression.empty() ? gis::core::spindexCustomIndexPresetExpression(preset) : expression;
    if (resolvedExpression.empty()) {
        return gis::framework::Result::fail(
            "expression or preset is required (e.g., ndvi_alias, (B4-B1)/(B4+B1))");
    }

    auto ds = gis::core::openRaster(input, true);
    const int width = ds->GetRasterXSize();
    const int height = ds->GetRasterYSize();
    const int bandCount = ds->GetRasterCount();

    progress.onMessage("Reading " + std::to_string(bandCount) + " bands...");
    std::vector<cv::Mat> bandMats;
    bandMats.reserve(bandCount);
    for (int band = 1; band <= bandCount; ++band) {
        bandMats.push_back(gis::core::gdalBandToMat(ds.get(), band));
    }
    progress.onProgress(0.4);

    progress.onMessage("Evaluating expression: " + resolvedExpression);
    cv::Mat indexMat(height, width, CV_32F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto bandValues = buildCustomExpressionValues(
                bandMats, x, y, blueBand, greenBand, redBand, nirBand, swir1Band);
            indexMat.at<float>(y, x) =
                static_cast<float>(evalExpression(resolvedExpression, bandValues));
        }
        if ((y % 100) == 0) {
            progress.onProgress(0.4 + 0.4 * static_cast<double>(y) / height);
        }
    }

    progress.onProgress(0.85);
    gis::core::matToGdalTiff(indexMat, input, output, 1);
    progress.onProgress(1.0);

    auto result = buildIndexResult("CUSTOM_INDEX", output, indexMat);
    result.metadata["expression"] = resolvedExpression;
    result.metadata["preset"] = preset;
    result.metadata["band_count"] = std::to_string(bandCount);
    result.metadata["blue_band"] = std::to_string(blueBand);
    result.metadata["green_band"] = std::to_string(greenBand);
    result.metadata["red_band"] = std::to_string(redBand);
    result.metadata["nir_band"] = std::to_string(nirBand);
    result.metadata["swir1_band"] = std::to_string(swir1Band);
    return result;
}

} // namespace

std::vector<gis::framework::ParamSpec> SpindexPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "操作", "选择要执行的光谱指数算法",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"ndvi", "evi", "savi", "gndvi", "ndwi", "mndwi", "ndbi", "arvi", "nbr", "awei", "ui", "custom_index"}
        },
        gis::framework::ParamSpec{
            "preset", "预设表达式", "选择内置的指数表达式预设，仅自定义指数时使用",
            gis::framework::ParamType::Enum, false, std::string{"none"},
            int{0}, int{0},
            gis::core::spindexCustomIndexPresetValues()
        },
        gis::framework::ParamSpec{
            "input", "输入栅格", "待计算指数的多波段栅格",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出栅格", "指数计算结果输出路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "red_band", "红光波段", "红光波段序号",
            gis::framework::ParamType::Int, false, int{3}
        },
        gis::framework::ParamSpec{
            "nir_band", "近红外波段", "近红外波段序号",
            gis::framework::ParamType::Int, false, int{4}
        },
        gis::framework::ParamSpec{
            "blue_band", "蓝光波段", "蓝光波段序号",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "green_band", "绿光波段", "绿光波段序号",
            gis::framework::ParamType::Int, false, int{2}
        },
        gis::framework::ParamSpec{
            "swir1_band", "短波红外1波段", "短波红外1波段序号",
            gis::framework::ParamType::Int, false, int{5}
        },
        gis::framework::ParamSpec{
            "swir2_band", "短波红外2波段", "短波红外2波段序号",
            gis::framework::ParamType::Int, false, int{6}
        },
        gis::framework::ParamSpec{
            "l_value", "L 参数", "SAVI 与 EVI 使用的 L 参数",
            gis::framework::ParamType::Double, false, double{0.5}
        },
        gis::framework::ParamSpec{
            "g_value", "G 参数", "EVI 使用的增益参数 G",
            gis::framework::ParamType::Double, false, double{2.5}
        },
        gis::framework::ParamSpec{
            "c1", "C1 参数", "EVI 使用的 C1 参数",
            gis::framework::ParamType::Double, false, double{6.0}
        },
        gis::framework::ParamSpec{
            "c2", "C2 参数", "EVI 使用的 C2 参数",
            gis::framework::ParamType::Double, false, double{7.5}
        },
        gis::framework::ParamSpec{
            "expression", "表达式", "自定义指数表达式，例如 (B4-B1)/(B4+B1)",
            gis::framework::ParamType::String, false, std::string{}
        },
    };
}

gis::framework::Result SpindexPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string action = gis::framework::getParam<std::string>(params, "action", "");
    if (action == "ndvi" || action == "evi" || action == "savi" ||
        action == "gndvi" || action == "ndwi" || action == "mndwi" ||
        action == "ndbi" || action == "arvi" || action == "nbr" ||
        action == "awei" || action == "ui" ||
        action == "custom_index") {
        return doExecuteAction(action, params, progress);
    }

    return gis::framework::Result::fail("Unknown action: " + action);
}

gis::framework::Result SpindexPlugin::doExecuteAction(
    const std::string& action,
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string preset = gis::framework::getParam<std::string>(params, "preset", "none");
    const std::string expression = gis::framework::getParam<std::string>(params, "expression", "");
    const std::string effectiveExpression =
        expression.empty() ? gis::core::spindexCustomIndexPresetExpression(preset) : expression;
    const int blueBand = gis::framework::getParam<int>(params, "blue_band", 1);
    const int greenBand = gis::framework::getParam<int>(params, "green_band", 2);
    const int redBand = gis::framework::getParam<int>(params, "red_band", 3);
    const int nirBand = gis::framework::getParam<int>(params, "nir_band", 4);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.05);

    auto ds = gis::core::openRaster(input, true);
    const int bands = ds.get()->GetRasterCount();
    try {
        if (action == "custom_index") {
            return doCustomIndex(
                input,
                output,
                expression,
                preset,
                resolveOptionalAliasBand(params, "blue_band", blueBand, bands, effectiveExpression, "BLUE"),
                resolveOptionalAliasBand(params, "green_band", greenBand, bands, effectiveExpression, "GREEN"),
                resolveOptionalAliasBand(params, "red_band", redBand, bands, effectiveExpression, "RED"),
                resolveOptionalAliasBand(params, "nir_band", nirBand, bands, effectiveExpression, "NIR"),
                resolveOptionalAliasBand(params, "swir1_band", 5, bands, effectiveExpression, "SWIR1"),
                progress);
        }

        if (action == "ndvi") {
            const int resolvedRedBand = getBandIndex(params, "red_band", redBand, bands);
            const int resolvedNirBand = getBandIndex(params, "nir_band", nirBand, bands);
            cv::Mat red = readBandMat(ds, resolvedRedBand, "Red", progress);
            progress.onProgress(0.25);
            cv::Mat nir = readBandMat(ds, resolvedNirBand, "NIR", progress);
            progress.onProgress(0.5);

            progress.onMessage("Computing NDVI = (NIR - Red) / (NIR + Red)...");
            cv::Mat indexMat = safeDivide(nir - red, nir + red);
            progress.onProgress(0.8);
            gis::core::matToGdalTiff(indexMat, input, output, 1);
            progress.onProgress(1.0);

            auto result = buildIndexResult("NDVI", output, indexMat);
            result.metadata["ndvi_min"] = result.metadata["index_min"];
            result.metadata["ndvi_max"] = result.metadata["index_max"];
            result.metadata["red_band"] = std::to_string(resolvedRedBand);
            result.metadata["nir_band"] = std::to_string(resolvedNirBand);
            return result;
        }

        if (action == "gndvi") {
            const int greenBand = getBandIndex(params, "green_band", 2, bands);
            const int resolvedNirBand = getBandIndex(params, "nir_band", nirBand, bands);
            cv::Mat green = readBandMat(ds, greenBand, "Green", progress);
            progress.onProgress(0.25);
            cv::Mat nir = readBandMat(ds, resolvedNirBand, "NIR", progress);
            progress.onProgress(0.5);

            progress.onMessage("Computing GNDVI = (NIR - Green) / (NIR + Green)...");
            cv::Mat indexMat = safeDivide(nir - green, nir + green);
            progress.onProgress(0.8);
            gis::core::matToGdalTiff(indexMat, input, output, 1);
            progress.onProgress(1.0);
            return buildIndexResult("GNDVI", output, indexMat);
        }

        if (action == "ndwi") {
            const int greenBand = getBandIndex(params, "green_band", 2, bands);
            const int resolvedNirBand = getBandIndex(params, "nir_band", nirBand, bands);
            cv::Mat green = readBandMat(ds, greenBand, "Green", progress);
            progress.onProgress(0.25);
            cv::Mat nir = readBandMat(ds, resolvedNirBand, "NIR", progress);
            progress.onProgress(0.5);

            progress.onMessage("Computing NDWI = (Green - NIR) / (Green + NIR)...");
            cv::Mat indexMat = safeDivide(green - nir, green + nir);
            progress.onProgress(0.8);
            gis::core::matToGdalTiff(indexMat, input, output, 1);
            progress.onProgress(1.0);
            return buildIndexResult("NDWI", output, indexMat);
        }

        if (action == "mndwi") {
            const int greenBand = getBandIndex(params, "green_band", 2, bands);
            const int swir1Band = getBandIndex(params, "swir1_band", 5, bands);
            cv::Mat green = readBandMat(ds, greenBand, "Green", progress);
            progress.onProgress(0.25);
            cv::Mat swir1 = readBandMat(ds, swir1Band, "SWIR1", progress);
            progress.onProgress(0.5);

            progress.onMessage("Computing MNDWI = (Green - SWIR1) / (Green + SWIR1)...");
            cv::Mat indexMat = safeDivide(green - swir1, green + swir1);
            progress.onProgress(0.8);
            gis::core::matToGdalTiff(indexMat, input, output, 1);
            progress.onProgress(1.0);
            return buildIndexResult("MNDWI", output, indexMat);
        }

        if (action == "ndbi") {
            const int swir1Band = getBandIndex(params, "swir1_band", 5, bands);
            const int resolvedNirBand = getBandIndex(params, "nir_band", nirBand, bands);
            cv::Mat swir1 = readBandMat(ds, swir1Band, "SWIR1", progress);
            progress.onProgress(0.25);
            cv::Mat nir = readBandMat(ds, resolvedNirBand, "NIR", progress);
            progress.onProgress(0.5);

            progress.onMessage("Computing NDBI = (SWIR1 - NIR) / (SWIR1 + NIR)...");
            cv::Mat indexMat = safeDivide(swir1 - nir, swir1 + nir);
            progress.onProgress(0.8);
            gis::core::matToGdalTiff(indexMat, input, output, 1);
            progress.onProgress(1.0);
            return buildIndexResult("NDBI", output, indexMat);
        }

        if (action == "arvi") {
            const int blueBand = getBandIndex(params, "blue_band", 1, bands);
            const int resolvedRedBand = getBandIndex(params, "red_band", redBand, bands);
            const int resolvedNirBand = getBandIndex(params, "nir_band", nirBand, bands);
            cv::Mat blue = readBandMat(ds, blueBand, "Blue", progress);
            progress.onProgress(0.2);
            cv::Mat red = readBandMat(ds, resolvedRedBand, "Red", progress);
            progress.onProgress(0.35);
            cv::Mat nir = readBandMat(ds, resolvedNirBand, "NIR", progress);
            progress.onProgress(0.5);

            progress.onMessage("Computing ARVI = (NIR - (2 * Red - Blue)) / (NIR + (2 * Red - Blue))...");
            cv::Mat rb = 2.0f * red - blue;
            cv::Mat indexMat = safeDivide(nir - rb, nir + rb);
            progress.onProgress(0.8);
            gis::core::matToGdalTiff(indexMat, input, output, 1);
            progress.onProgress(1.0);
            return buildIndexResult("ARVI", output, indexMat);
        }

        if (action == "nbr") {
            const int resolvedNirBand = getBandIndex(params, "nir_band", nirBand, bands);
            const int swir2Band = getBandIndex(params, "swir2_band", 6, bands);
            cv::Mat nir = readBandMat(ds, resolvedNirBand, "NIR", progress);
            progress.onProgress(0.25);
            cv::Mat swir2 = readBandMat(ds, swir2Band, "SWIR2", progress);
            progress.onProgress(0.5);

            progress.onMessage("Computing NBR = (NIR - SWIR2) / (NIR + SWIR2)...");
            cv::Mat indexMat = safeDivide(nir - swir2, nir + swir2);
            progress.onProgress(0.8);
            gis::core::matToGdalTiff(indexMat, input, output, 1);
            progress.onProgress(1.0);
            return buildIndexResult("NBR", output, indexMat);
        }

        if (action == "awei") {
            const int greenBand = getBandIndex(params, "green_band", 2, bands);
            const int resolvedNirBand = getBandIndex(params, "nir_band", nirBand, bands);
            const int swir1Band = getBandIndex(params, "swir1_band", 5, bands);
            const int swir2Band = getBandIndex(params, "swir2_band", 6, bands);
            cv::Mat green = readBandMat(ds, greenBand, "Green", progress);
            progress.onProgress(0.2);
            cv::Mat nir = readBandMat(ds, resolvedNirBand, "NIR", progress);
            progress.onProgress(0.35);
            cv::Mat swir1 = readBandMat(ds, swir1Band, "SWIR1", progress);
            progress.onProgress(0.5);
            cv::Mat swir2 = readBandMat(ds, swir2Band, "SWIR2", progress);
            progress.onProgress(0.65);

            progress.onMessage("Computing AWEI = 4 * (Green - SWIR1) - (0.25 * NIR + 2.75 * SWIR2)...");
            cv::Mat indexMat = 4.0f * (green - swir1) - (0.25f * nir + 2.75f * swir2);
            progress.onProgress(0.8);
            gis::core::matToGdalTiff(indexMat, input, output, 1);
            progress.onProgress(1.0);
            return buildIndexResult("AWEI", output, indexMat);
        }

        if (action == "ui") {
            const int resolvedNirBand = getBandIndex(params, "nir_band", nirBand, bands);
            const int swir2Band = getBandIndex(params, "swir2_band", 6, bands);
            cv::Mat nir = readBandMat(ds, resolvedNirBand, "NIR", progress);
            progress.onProgress(0.25);
            cv::Mat swir2 = readBandMat(ds, swir2Band, "SWIR2", progress);
            progress.onProgress(0.5);

            progress.onMessage("Computing UI = (SWIR2 - NIR) / (SWIR2 + NIR)...");
            cv::Mat indexMat = safeDivide(swir2 - nir, swir2 + nir);
            progress.onProgress(0.8);
            gis::core::matToGdalTiff(indexMat, input, output, 1);
            progress.onProgress(1.0);
            return buildIndexResult("UI", output, indexMat);
        }

        if (action == "savi") {
            const int resolvedRedBand = getBandIndex(params, "red_band", redBand, bands);
            const int resolvedNirBand = getBandIndex(params, "nir_band", nirBand, bands);
            const float lValue = static_cast<float>(gis::framework::getParam<double>(params, "l_value", 0.5));
            cv::Mat red = readBandMat(ds, resolvedRedBand, "Red", progress);
            progress.onProgress(0.25);
            cv::Mat nir = readBandMat(ds, resolvedNirBand, "NIR", progress);
            progress.onProgress(0.5);

            progress.onMessage("Computing SAVI = ((NIR - Red) / (NIR + Red + L)) * (1 + L)...");
            cv::Mat denominator = nir + red + lValue;
            cv::Mat indexMat = safeDivide((nir - red) * (1.0f + lValue), denominator);
            progress.onProgress(0.8);
            gis::core::matToGdalTiff(indexMat, input, output, 1);
            progress.onProgress(1.0);
            return buildIndexResult("SAVI", output, indexMat);
        }

        if (action == "evi") {
            const int blueBand = getBandIndex(params, "blue_band", 1, bands);
            const int resolvedRedBand = getBandIndex(params, "red_band", redBand, bands);
            const int resolvedNirBand = getBandIndex(params, "nir_band", nirBand, bands);
            const float gValue = static_cast<float>(gis::framework::getParam<double>(params, "g_value", 2.5));
            const float c1 = static_cast<float>(gis::framework::getParam<double>(params, "c1", 6.0));
            const float c2 = static_cast<float>(gis::framework::getParam<double>(params, "c2", 7.5));
            const float lValue = static_cast<float>(gis::framework::getParam<double>(params, "l_value", 1.0));

            cv::Mat blue = readBandMat(ds, blueBand, "Blue", progress);
            progress.onProgress(0.2);
            cv::Mat red = readBandMat(ds, resolvedRedBand, "Red", progress);
            progress.onProgress(0.35);
            cv::Mat nir = readBandMat(ds, resolvedNirBand, "NIR", progress);
            progress.onProgress(0.5);

            progress.onMessage("Computing EVI = G * (NIR - Red) / (NIR + C1 * Red - C2 * Blue + L)...");
            cv::Mat denominator = nir + c1 * red - c2 * blue + lValue;
            cv::Mat indexMat = safeDivide(gValue * (nir - red), denominator);
            progress.onProgress(0.8);
            gis::core::matToGdalTiff(indexMat, input, output, 1);
            progress.onProgress(1.0);
            return buildIndexResult("EVI", output, indexMat);
        }
    } catch (const std::exception& ex) {
        return gis::framework::Result::fail(ex.what());
    }

    return gis::framework::Result::fail("Unknown action: " + action);
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::SpindexPlugin)
