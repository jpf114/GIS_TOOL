#include "processing_plugin.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <cpl_conv.h>
#include <opencv2/opencv.hpp>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <map>
#include <functional>
#include <stack>

namespace gis::plugins {

std::vector<gis::framework::ParamSpec> ProcessingPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"threshold", "filter", "enhance", "band_math", "stats"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入影像文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出文件", "输出影像文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "band", "波段序号", "处理的波段序号(从1开始)",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "method", "方法", "处理方法",
            gis::framework::ParamType::Enum, false, std::string{},
            int{0}, int{0},
            {"binary", "binary_inv", "truncate", "tozero", "otsu", "adaptive_gaussian", "adaptive_mean"}
        },
        gis::framework::ParamSpec{
            "threshold_value", "阈值", "阈值大小",
            gis::framework::ParamType::Double, false, double{128.0}
        },
        gis::framework::ParamSpec{
            "max_value", "最大值", "阈值化后的最大值",
            gis::framework::ParamType::Double, false, double{255.0}
        },
        gis::framework::ParamSpec{
            "filter_type", "滤波类型", "滤波算法",
            gis::framework::ParamType::Enum, false, std::string{"gaussian"},
            int{0}, int{0},
            {"gaussian", "median", "bilateral", "morph_open", "morph_close", "morph_dilate", "morph_erode"}
        },
        gis::framework::ParamSpec{
            "kernel_size", "核大小", "滤波核大小(奇数)",
            gis::framework::ParamType::Int, false, int{5}
        },
        gis::framework::ParamSpec{
            "sigma", "sigma值", "高斯滤波sigma参数",
            gis::framework::ParamType::Double, false, double{1.5}
        },
        gis::framework::ParamSpec{
            "enhance_type", "增强类型", "增强算法",
            gis::framework::ParamType::Enum, false, std::string{"equalize"},
            int{0}, int{0},
            {"equalize", "clahe", "normalize", "log", "gamma"}
        },
        gis::framework::ParamSpec{
            "clip_limit", "CLAHE裁剪限制", "CLAHE算法的裁剪限制参数",
            gis::framework::ParamType::Double, false, double{2.0}
        },
        gis::framework::ParamSpec{
            "gamma", "Gamma值", "Gamma校正参数",
            gis::framework::ParamType::Double, false, double{1.0}
        },
        gis::framework::ParamSpec{
            "expression", "表达式", "波段运算表达式(如 B1+B2, B1*0.5+B2*0.5)",
            gis::framework::ParamType::String, false, std::string{}
        },
    };
}

gis::framework::Result ProcessingPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string action = gis::framework::getParam<std::string>(params, "action", "");

    if (action == "threshold")  return doThreshold(params, progress);
    if (action == "filter")     return doFilter(params, progress);
    if (action == "enhance")    return doEnhance(params, progress);
    if (action == "band_math")  return doBandMath(params, progress);
    if (action == "stats")      return doStats(params, progress);

    return gis::framework::Result::fail("Unknown action: " + action);
}

static cv::Mat readBandAsMat(const std::string& path, int bandIndex,
                              gis::core::ProgressReporter& progress) {
    auto ds = gis::core::openRaster(path, true);
    progress.onMessage("Reading band " + std::to_string(bandIndex) + " from " + path);
    auto mat = gis::core::gdalBandToMat(ds.get(), bandIndex);
    return mat;
}

static gis::framework::Result writeMatOutput(
    const cv::Mat& mat, const std::string& inputPath, const std::string& outputPath,
    int bandIndex, gis::core::ProgressReporter& progress) {

    if (outputPath.empty()) {
        return gis::framework::Result::fail("output is required");
    }

    progress.onMessage("Writing output: " + outputPath);
    gis::core::matToGdalTiff(mat, inputPath, outputPath, bandIndex);
    progress.onProgress(1.0);
    return gis::framework::Result::ok("Processing completed successfully", outputPath);
}

static cv::Mat toUint8(const cv::Mat& mat) {
    if (mat.type() == CV_8U) return mat.clone();
    cv::Mat u8;
    double minVal, maxVal;
    cv::minMaxLoc(mat, &minVal, &maxVal);
    if (maxVal - minVal < 1e-10) {
        return cv::Mat::zeros(mat.size(), CV_8U);
    }
    mat.convertTo(u8, CV_8U, 255.0 / (maxVal - minVal), -minVal * 255.0 / (maxVal - minVal));
    return u8;
}

gis::framework::Result ProcessingPlugin::doThreshold(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);
    std::string method = gis::framework::getParam<std::string>(params, "method", "otsu");
    double threshVal = gis::framework::getParam<double>(params, "threshold_value", 128.0);
    double maxVal    = gis::framework::getParam<double>(params, "max_value", 255.0);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);
    cv::Mat mat = readBandAsMat(input, band, progress);
    progress.onProgress(0.3);

    cv::Mat gray = toUint8(mat);
    progress.onProgress(0.4);

    cv::Mat result;
    if (method == "otsu") {
        cv::threshold(gray, result, 0, maxVal, cv::THRESH_BINARY | cv::THRESH_OTSU);
    } else if (method == "adaptive_gaussian") {
        cv::adaptiveThreshold(gray, result, maxVal,
            cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 11, 2);
    } else if (method == "adaptive_mean") {
        cv::adaptiveThreshold(gray, result, maxVal,
            cv::ADAPTIVE_THRESH_MEAN_C, cv::THRESH_BINARY, 11, 2);
    } else if (method == "binary") {
        cv::threshold(gray, result, threshVal, maxVal, cv::THRESH_BINARY);
    } else if (method == "binary_inv") {
        cv::threshold(gray, result, threshVal, maxVal, cv::THRESH_BINARY_INV);
    } else if (method == "truncate") {
        cv::threshold(gray, result, threshVal, maxVal, cv::THRESH_TRUNC);
    } else if (method == "tozero") {
        cv::threshold(gray, result, threshVal, maxVal, cv::THRESH_TOZERO);
    } else {
        return gis::framework::Result::fail("Unknown threshold method: " + method);
    }

    progress.onProgress(0.7);

    cv::Mat outFloat;
    result.convertTo(outFloat, CV_32F);
    return writeMatOutput(outFloat, input, output, band, progress);
}

gis::framework::Result ProcessingPlugin::doFilter(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);
    std::string filterType = gis::framework::getParam<std::string>(params, "filter_type", "gaussian");
    int kernelSize = gis::framework::getParam<int>(params, "kernel_size", 5);
    double sigma   = gis::framework::getParam<double>(params, "sigma", 1.5);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    if (kernelSize % 2 == 0) kernelSize++;
    if (kernelSize < 3) kernelSize = 3;

    progress.onProgress(0.1);
    cv::Mat mat = readBandAsMat(input, band, progress);
    progress.onProgress(0.3);

    cv::Mat result;

    if (filterType == "gaussian") {
        cv::GaussianBlur(mat, result, cv::Size(kernelSize, kernelSize), sigma);
    } else if (filterType == "median") {
        cv::Mat u8 = toUint8(mat);
        cv::medianBlur(u8, result, kernelSize);
        result.convertTo(result, CV_32F, 1.0 / 255.0);
    } else if (filterType == "bilateral") {
        cv::Mat u8 = toUint8(mat);
        cv::bilateralFilter(u8, result, kernelSize, 75, 75);
        result.convertTo(result, CV_32F, 1.0 / 255.0);
    } else if (filterType == "morph_open" ||
               filterType == "morph_close" ||
               filterType == "morph_dilate" ||
               filterType == "morph_erode") {
        cv::Mat u8 = toUint8(mat);
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT,
            cv::Size(kernelSize, kernelSize));
        int morphType;
        if (filterType == "morph_open")   morphType = cv::MORPH_OPEN;
        else if (filterType == "morph_close") morphType = cv::MORPH_CLOSE;
        else if (filterType == "morph_dilate") morphType = cv::MORPH_DILATE;
        else morphType = cv::MORPH_ERODE;

        cv::morphologyEx(u8, result, morphType, kernel);
        result.convertTo(result, CV_32F, 1.0 / 255.0);
    } else {
        return gis::framework::Result::fail("Unknown filter type: " + filterType);
    }

    progress.onProgress(0.7);
    return writeMatOutput(result, input, output, band, progress);
}

gis::framework::Result ProcessingPlugin::doEnhance(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);
    std::string enhanceType = gis::framework::getParam<std::string>(params, "enhance_type", "equalize");
    double clipLimit = gis::framework::getParam<double>(params, "clip_limit", 2.0);
    double gammaVal  = gis::framework::getParam<double>(params, "gamma", 1.0);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);
    cv::Mat mat = readBandAsMat(input, band, progress);
    progress.onProgress(0.3);

    cv::Mat result;

    if (enhanceType == "equalize") {
        cv::Mat u8 = toUint8(mat);
        cv::equalizeHist(u8, result);
        result.convertTo(result, CV_32F, 1.0 / 255.0);
    } else if (enhanceType == "clahe") {
        cv::Mat u8 = toUint8(mat);
        auto clahe = cv::createCLAHE(clipLimit, cv::Size(8, 8));
        clahe->apply(u8, result);
        result.convertTo(result, CV_32F, 1.0 / 255.0);
    } else if (enhanceType == "normalize") {
        cv::normalize(mat, result, 0, 1, cv::NORM_MINMAX, CV_32F);
    } else if (enhanceType == "log") {
        cv::Mat shifted;
        cv::max(mat, 0.0);
        mat.convertTo(shifted, CV_32F);
        shifted = shifted + 1.0;
        cv::log(shifted, result);
        double minVal, maxVal;
        cv::minMaxLoc(result, &minVal, &maxVal);
        if (maxVal - minVal > 1e-10) {
            result = (result - minVal) / (maxVal - minVal);
        }
    } else if (enhanceType == "gamma") {
        if (gammaVal <= 0) {
            return gis::framework::Result::fail("gamma must be positive");
        }
        cv::Mat normalized;
        double minVal, maxVal;
        cv::minMaxLoc(mat, &minVal, &maxVal);
        if (maxVal - minVal < 1e-10) {
            result = cv::Mat::zeros(mat.size(), CV_32F);
        } else {
            normalized = (mat - minVal) / (maxVal - minVal);
            cv::pow(normalized, 1.0 / gammaVal, result);
        }
    } else {
        return gis::framework::Result::fail("Unknown enhance type: " + enhanceType);
    }

    progress.onProgress(0.7);
    return writeMatOutput(result, input, output, band, progress);
}

static double evalExpression(const std::string& expr,
                              const std::map<std::string, double>& bandValues) {
    std::string e = expr;
    for (auto& [key, val] : bandValues) {
        std::string placeholder = key;
        std::string replacement = std::to_string(val);
        size_t pos = 0;
        while ((pos = e.find(placeholder, pos)) != std::string::npos) {
            e.replace(pos, placeholder.length(), replacement);
            pos += replacement.length();
        }
    }

    try {
        std::stack<double> vals;
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
                case '/': return (std::abs(b) < 1e-15) ? 0.0 : a / b;
                default: return 0.0;
            }
        };

        size_t i = 0;
        while (i < e.size()) {
            if (std::isspace(e[i])) { i++; continue; }
            if (std::isdigit(e[i]) || e[i] == '.') {
                size_t start = i;
                while (i < e.size() && (std::isdigit(e[i]) || e[i] == '.')) i++;
                vals.push(std::stod(e.substr(start, i - start)));
            } else if (e[i] == '(') {
                ops.push('('); i++;
            } else if (e[i] == ')') {
                while (!ops.empty() && ops.top() != '(') {
                    double b = vals.top(); vals.pop();
                    double a = vals.top(); vals.pop();
                    vals.push(applyOp(a, b, ops.top())); ops.pop();
                }
                if (!ops.empty()) ops.pop();
                i++;
            } else if (e[i] == '+' || e[i] == '-' || e[i] == '*' || e[i] == '/') {
                while (!ops.empty() && precedence(ops.top()) >= precedence(e[i])) {
                    double b = vals.top(); vals.pop();
                    double a = vals.top(); vals.pop();
                    vals.push(applyOp(a, b, ops.top())); ops.pop();
                }
                ops.push(e[i]); i++;
            } else {
                i++;
            }
        }
        while (!ops.empty()) {
            double b = vals.top(); vals.pop();
            double a = vals.top(); vals.pop();
            vals.push(applyOp(a, b, ops.top())); ops.pop();
        }
        return vals.empty() ? 0.0 : vals.top();
    } catch (...) {
        return 0.0;
    }
}

gis::framework::Result ProcessingPlugin::doBandMath(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string expression = gis::framework::getParam<std::string>(params, "expression", "");

    if (input.empty())     return gis::framework::Result::fail("input is required");
    if (output.empty())    return gis::framework::Result::fail("output is required");
    if (expression.empty()) return gis::framework::Result::fail("expression is required (e.g., B1+B2, B1*0.5+B2*0.5)");

    progress.onProgress(0.1);
    auto ds = gis::core::openRaster(input, true);
    int width  = ds->GetRasterXSize();
    int height = ds->GetRasterYSize();
    int bands  = ds->GetRasterCount();

    progress.onMessage("Reading " + std::to_string(bands) + " bands...");
    std::vector<cv::Mat> bandMats;
    for (int b = 1; b <= bands; ++b) {
        bandMats.push_back(gis::core::gdalBandToMat(ds.get(), b));
    }
    progress.onProgress(0.4);

    progress.onMessage("Evaluating expression: " + expression);
    cv::Mat result(height, width, CV_32F);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::map<std::string, double> bandValues;
            for (int b = 0; b < bands; ++b) {
                bandValues["B" + std::to_string(b + 1)] = bandMats[b].at<float>(y, x);
            }
            result.at<float>(y, x) = static_cast<float>(evalExpression(expression, bandValues));
        }
        if (y % 100 == 0) {
            progress.onProgress(0.4 + 0.5 * static_cast<double>(y) / height);
        }
    }

    progress.onProgress(0.9);
    return writeMatOutput(result, input, output, 1, progress);
}

gis::framework::Result ProcessingPlugin::doStats(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    int band = gis::framework::getParam<int>(params, "band", 1);

    if (input.empty()) return gis::framework::Result::fail("input is required");

    progress.onProgress(0.1);
    auto ds = gis::core::openRaster(input, true);
    auto* rasterBand = ds->GetRasterBand(band);
    if (!rasterBand) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    progress.onProgress(0.3);

    double minVal = 0, maxVal = 0, meanVal = 0, stdDev = 0;
    rasterBand->ComputeStatistics(false, &minVal, &maxVal, &meanVal, &stdDev, nullptr, nullptr);

    progress.onProgress(0.6);

    int width  = ds->GetRasterXSize();
    int height = ds->GetRasterYSize();
    GDALDataType dataType = rasterBand->GetRasterDataType();
    int noDataOk = 0;
    double noDataVal = rasterBand->GetNoDataValue(&noDataOk);

    std::string typeName;
    switch (dataType) {
        case GDT_Byte:    typeName = "Byte (uint8)"; break;
        case GDT_UInt16:  typeName = "UInt16"; break;
        case GDT_Int16:   typeName = "Int16"; break;
        case GDT_UInt32:  typeName = "UInt32"; break;
        case GDT_Int32:   typeName = "Int32"; break;
        case GDT_Float32: typeName = "Float32"; break;
        case GDT_Float64: typeName = "Float64"; break;
        default:          typeName = "Unknown"; break;
    }

    std::ostringstream oss;
    oss << "Statistics for " << input << " (Band " << band << ")\n";
    oss << "  Size:   " << width << " x " << height << "\n";
    oss << "  Type:   " << typeName << "\n";
    oss << "  Min:    " << minVal << "\n";
    oss << "  Max:    " << maxVal << "\n";
    oss << "  Mean:   " << meanVal << "\n";
    oss << "  StdDev: " << stdDev << "\n";
    if (noDataOk) {
        oss << "  NoData: " << noDataVal << "\n";
    }

    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(oss.str());
    result.metadata["min"]    = std::to_string(minVal);
    result.metadata["max"]    = std::to_string(maxVal);
    result.metadata["mean"]   = std::to_string(meanVal);
    result.metadata["stddev"] = std::to_string(stdDev);
    result.metadata["width"]  = std::to_string(width);
    result.metadata["height"] = std::to_string(height);
    result.metadata["type"]   = typeName;
    if (noDataOk) {
        result.metadata["nodata"] = std::to_string(noDataVal);
    }
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::ProcessingPlugin)
