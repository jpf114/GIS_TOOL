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
            {"threshold", "filter", "enhance", "band_math", "stats", "edge", "contour", "template_match", "pansharpen"}
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
            "expression", "表达式", "波段运算表达式(如B1+B2, B1*0.5+B2*0.5)",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "edge_method", "边缘检测方法", "边缘检测算子",
            gis::framework::ParamType::Enum, false, std::string{"canny"},
            int{0}, int{0},
            {"canny", "sobel", "laplacian", "scharr"}
        },
        gis::framework::ParamSpec{
            "low_threshold", "低阈值", "Canny边缘检测低阈值",
            gis::framework::ParamType::Double, false, double{50.0}
        },
        gis::framework::ParamSpec{
            "high_threshold", "高阈值", "Canny边缘检测高阈值",
            gis::framework::ParamType::Double, false, double{150.0}
        },
        gis::framework::ParamSpec{
            "sobel_dx", "Sobel dx", "Sobel x方向导数阶数",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "sobel_dy", "Sobel dy", "Sobel y方向导数阶数",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "min_area", "最小面积", "轮廓过滤的最小面积(像素)",
            gis::framework::ParamType::Double, false, double{100.0}
        },
        gis::framework::ParamSpec{
            "template_file", "模板文件", "模板匹配的模板影像路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "match_method", "匹配方法", "模板匹配算法",
            gis::framework::ParamType::Enum, false, std::string{"ccoeff_normed"},
            int{0}, int{0},
            {"sqdiff", "sqdiff_normed", "ccorr", "ccorr_normed", "ccoeff", "ccoeff_normed"}
        },
        gis::framework::ParamSpec{
            "pan_file", "全色影像", "全色锐化用的高分辨率全色影像路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "pan_method", "融合方法", "全色锐化融合算法",
            gis::framework::ParamType::Enum, false, std::string{"brovey"},
            int{0}, int{0},
            {"brovey", "simple_mean", "ihs"}
        },
    };
}

gis::framework::Result ProcessingPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string action = gis::framework::getParam<std::string>(params, "action", "");

    if (action == "threshold")       return doThreshold(params, progress);
    if (action == "filter")          return doFilter(params, progress);
    if (action == "enhance")         return doEnhance(params, progress);
    if (action == "band_math")       return doBandMath(params, progress);
    if (action == "stats")           return doStats(params, progress);
    if (action == "edge")            return doEdge(params, progress);
    if (action == "contour")         return doContour(params, progress);
    if (action == "template_match")  return doTemplateMatch(params, progress);
    if (action == "pansharpen")      return doPansharpen(params, progress);

    return gis::framework::Result::fail("Unknown action: " + action);
}

static cv::Mat readBandAsMat(const std::string& path, int bandIndex,
                              gis::core::ProgressReporter& progress) {
    progress.onMessage("Reading band " + std::to_string(bandIndex) + " from " + path);
    return gis::core::readBandAsMat(path, bandIndex);
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

    cv::Mat gray = gis::core::toUint8(mat);
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
        cv::Mat u8 = gis::core::toUint8(mat);
        cv::medianBlur(u8, result, kernelSize);
        result.convertTo(result, CV_32F, 1.0 / 255.0);
    } else if (filterType == "bilateral") {
        cv::Mat u8 = gis::core::toUint8(mat);
        cv::bilateralFilter(u8, result, kernelSize, 75, 75);
        result.convertTo(result, CV_32F, 1.0 / 255.0);
    } else if (filterType == "morph_open" ||
               filterType == "morph_close" ||
               filterType == "morph_dilate" ||
               filterType == "morph_erode") {
        cv::Mat u8 = gis::core::toUint8(mat);
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
        cv::Mat u8 = gis::core::toUint8(mat);
        cv::equalizeHist(u8, result);
        result.convertTo(result, CV_32F, 1.0 / 255.0);
    } else if (enhanceType == "clahe") {
        cv::Mat u8 = gis::core::toUint8(mat);
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

gis::framework::Result ProcessingPlugin::doEdge(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);
    std::string edgeMethod = gis::framework::getParam<std::string>(params, "edge_method", "canny");
    double lowThresh  = gis::framework::getParam<double>(params, "low_threshold", 50.0);
    double highThresh = gis::framework::getParam<double>(params, "high_threshold", 150.0);
    int sobelDx = gis::framework::getParam<int>(params, "sobel_dx", 1);
    int sobelDy = gis::framework::getParam<int>(params, "sobel_dy", 1);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);
    cv::Mat mat = readBandAsMat(input, band, progress);
    progress.onProgress(0.3);

    cv::Mat gray = gis::core::toUint8(mat);
    progress.onProgress(0.4);

    cv::Mat result;

    if (edgeMethod == "canny") {
        cv::Canny(gray, result, lowThresh, highThresh);
    } else if (edgeMethod == "sobel") {
        if (sobelDx < 0 || sobelDx > 2) sobelDx = 1;
        if (sobelDy < 0 || sobelDy > 2) sobelDy = 1;
        if (sobelDx == 0 && sobelDy == 0) sobelDx = 1;
        cv::Sobel(gray, result, CV_16S, sobelDx, sobelDy, 3);
        cv::convertScaleAbs(result, result);
    } else if (edgeMethod == "laplacian") {
        cv::Laplacian(gray, result, CV_16S, 3);
        cv::convertScaleAbs(result, result);
    } else if (edgeMethod == "scharr") {
        cv::Scharr(gray, result, CV_16S, sobelDx > 0 ? 1 : 0, sobelDy > 0 ? 1 : 0);
        cv::convertScaleAbs(result, result);
    } else {
        return gis::framework::Result::fail("Unknown edge method: " + edgeMethod);
    }

    progress.onProgress(0.7);

    cv::Mat outFloat;
    result.convertTo(outFloat, CV_32F, 1.0 / 255.0);
    return writeMatOutput(outFloat, input, output, band, progress);
}

gis::framework::Result ProcessingPlugin::doContour(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);
    double minArea = gis::framework::getParam<double>(params, "min_area", 100.0);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);
    cv::Mat mat = readBandAsMat(input, band, progress);
    progress.onProgress(0.3);

    cv::Mat gray = gis::core::toUint8(mat);

    cv::Mat binary;
    cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    progress.onProgress(0.4);

    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(binary, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    progress.onProgress(0.6);

    cv::Mat resultImg = cv::Mat::zeros(mat.size(), CV_8U);
    int keptCount = 0;
    for (size_t i = 0; i < contours.size(); ++i) {
        double area = cv::contourArea(contours[i]);
        if (area >= minArea) {
            cv::drawContours(resultImg, contours, static_cast<int>(i), cv::Scalar(255), cv::FILLED);
            keptCount++;
        }
    }

    progress.onProgress(0.8);

    std::ostringstream oss;
    oss << "Contour extraction: " << contours.size() << " total, "
        << keptCount << " kept (area >= " << minArea << ")";
    progress.onMessage(oss.str());

    cv::Mat outFloat;
    resultImg.convertTo(outFloat, CV_32F, 1.0 / 255.0);

    auto result = writeMatOutput(outFloat, input, output, band, progress);
    result.metadata["total_contours"] = std::to_string(contours.size());
    result.metadata["kept_contours"] = std::to_string(keptCount);
    return result;
}

gis::framework::Result ProcessingPlugin::doTemplateMatch(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string templateFile = gis::framework::getParam<std::string>(params, "template_file", "");
    std::string matchMethod = gis::framework::getParam<std::string>(params, "match_method", "ccoeff_normed");
    int band = gis::framework::getParam<int>(params, "band", 1);

    if (input.empty())         return gis::framework::Result::fail("input is required");
    if (output.empty())        return gis::framework::Result::fail("output is required");
    if (templateFile.empty())  return gis::framework::Result::fail("template_file is required");

    progress.onProgress(0.1);
    cv::Mat srcMat = readBandAsMat(input, band, progress);
    progress.onProgress(0.2);
    cv::Mat tplMat = readBandAsMat(templateFile, band, progress);
    progress.onProgress(0.3);

    cv::Mat srcGray = gis::core::toUint8(srcMat);
    cv::Mat tplGray = gis::core::toUint8(tplMat);

    if (tplGray.cols > srcGray.cols || tplGray.rows > srcGray.rows) {
        return gis::framework::Result::fail("Template is larger than source image");
    }

    int method;
    if (matchMethod == "sqdiff")          method = cv::TM_SQDIFF;
    else if (matchMethod == "sqdiff_normed")   method = cv::TM_SQDIFF_NORMED;
    else if (matchMethod == "ccorr")           method = cv::TM_CCORR;
    else if (matchMethod == "ccorr_normed")    method = cv::TM_CCORR_NORMED;
    else if (matchMethod == "ccoeff")          method = cv::TM_CCOEFF;
    else                                       method = cv::TM_CCOEFF_NORMED;

    progress.onMessage("Running template matching...");
    cv::Mat matchResult;
    cv::matchTemplate(srcGray, tplGray, matchResult, method);
    progress.onProgress(0.7);

    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(matchResult, &minVal, &maxVal, &minLoc, &maxLoc);

    cv::Point matchLoc;
    double matchVal;
    if (method == cv::TM_SQDIFF || method == cv::TM_SQDIFF_NORMED) {
        matchLoc = minLoc;
        matchVal = minVal;
    } else {
        matchLoc = maxLoc;
        matchVal = maxVal;
    }

    std::ostringstream oss;
    oss << "Template match found at (" << matchLoc.x << ", " << matchLoc.y << ") "
        << "with score " << matchVal;
    progress.onMessage(oss.str());

    cv::Mat outFloat;
    matchResult.convertTo(outFloat, CV_32F);

    auto result = writeMatOutput(outFloat, input, output, band, progress);
    result.metadata["match_x"] = std::to_string(matchLoc.x);
    result.metadata["match_y"] = std::to_string(matchLoc.y);
    result.metadata["match_score"] = std::to_string(matchVal);
    result.metadata["template_width"] = std::to_string(tplGray.cols);
    result.metadata["template_height"] = std::to_string(tplGray.rows);
    return result;
}

gis::framework::Result ProcessingPlugin::doPansharpen(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string panFile = gis::framework::getParam<std::string>(params, "pan_file", "");
    std::string panMethod = gis::framework::getParam<std::string>(params, "pan_method", "brovey");

    if (input.empty())   return gis::framework::Result::fail("input is required (multispectral image)");
    if (output.empty())  return gis::framework::Result::fail("output is required");
    if (panFile.empty()) return gis::framework::Result::fail("pan_file is required (panchromatic image)");

    progress.onProgress(0.05);

    auto msDS = gis::core::openRaster(input, true);
    auto panDS = gis::core::openRaster(panFile, true);

    int msBands = msDS->GetRasterCount();
    if (msBands < 2) {
        return gis::framework::Result::fail("Input must have at least 2 bands for pansharpening");
    }

    int panWidth  = panDS->GetRasterXSize();
    int panHeight = panDS->GetRasterYSize();
    int msWidth   = msDS->GetRasterXSize();
    int msHeight  = msDS->GetRasterYSize();

    progress.onMessage("MS: " + std::to_string(msWidth) + "x" + std::to_string(msHeight) +
                       " PAN: " + std::to_string(panWidth) + "x" + std::to_string(panHeight));

    progress.onProgress(0.1);

    cv::Mat panMat = gis::core::gdalBandToMat(panDS.get(), 1);
    if (panMat.type() != CV_32F) panMat.convertTo(panMat, CV_32F);
    progress.onProgress(0.2);

    std::vector<cv::Mat> msBandsMat;
    for (int b = 1; b <= msBands; ++b) {
        cv::Mat band = gis::core::gdalBandToMat(msDS.get(), b);
        if (band.type() != CV_32F) band.convertTo(band, CV_32F);
        msBandsMat.push_back(band);
    }
    progress.onProgress(0.35);

    if (panWidth != msWidth || panHeight != msHeight) {
        progress.onMessage("Resampling MS bands to PAN resolution...");
        for (auto& band : msBandsMat) {
            cv::resize(band, band, cv::Size(panWidth, panHeight), 0, 0, cv::INTER_CUBIC);
        }
    }
    progress.onProgress(0.5);

    std::vector<cv::Mat> sharpenedBands;

    if (panMethod == "brovey") {
        cv::Mat intensity = msBandsMat[0].clone();
        for (int b = 1; b < msBands; ++b) {
            intensity += msBandsMat[b];
        }
        intensity = intensity / static_cast<double>(msBands);

        for (int b = 0; b < msBands; ++b) {
            cv::Mat ratio;
            cv::divide(msBandsMat[b], intensity + 1e-10f, ratio);
            cv::Mat sharp;
            cv::multiply(ratio, panMat, sharp);
            sharpenedBands.push_back(sharp);
        }
    } else if (panMethod == "simple_mean") {
        for (int b = 0; b < msBands; ++b) {
            cv::Mat sharp;
            cv::add(msBandsMat[b], panMat, sharp);
            sharp = sharp * 0.5;
            sharpenedBands.push_back(sharp);
        }
    } else if (panMethod == "ihs") {
        if (msBands < 3) {
            return gis::framework::Result::fail("IHS method requires at least 3 bands (RGB)");
        }

        cv::Mat r = msBandsMat[0];
        cv::Mat g = msBandsMat[1];
        cv::Mat b = msBandsMat[2];

        std::vector<cv::Mat> rgbChannels = {b, g, r};
        cv::Mat rgbImage;
        cv::merge(rgbChannels, rgbImage);

        cv::Mat hsvImage;
        cv::cvtColor(rgbImage, hsvImage, cv::COLOR_BGR2HSV);

        std::vector<cv::Mat> hsvChannels;
        cv::split(hsvImage, hsvChannels);

        cv::Mat panNorm;
        double panMin, panMax;
        cv::minMaxLoc(panMat, &panMin, &panMax);
        if (panMax - panMin > 1e-10) {
            panNorm = (panMat - panMin) / (panMax - panMin) * 255.0;
        } else {
            panNorm = cv::Mat::zeros(panMat.size(), CV_32F);
        }
        panNorm.convertTo(hsvChannels[2], CV_32F);

        cv::merge(hsvChannels, hsvImage);
        cv::Mat bgrResult;
        cv::cvtColor(hsvImage, bgrResult, cv::COLOR_HSV2BGR);

        std::vector<cv::Mat> resultChannels;
        cv::split(bgrResult, resultChannels);

        sharpenedBands.push_back(resultChannels[2]);
        sharpenedBands.push_back(resultChannels[1]);
        sharpenedBands.push_back(resultChannels[0]);

        for (int i = 3; i < msBands; ++i) {
            sharpenedBands.push_back(msBandsMat[i]);
        }
    } else {
        return gis::framework::Result::fail("Unknown pansharpen method: " + panMethod);
    }

    progress.onProgress(0.8);

    gis::core::matsToGdalTiff(sharpenedBands, msDS.get(), output);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Pansharpen completed: " + panMethod + " method, " +
        std::to_string(msBands) + " bands at " +
        std::to_string(panWidth) + "x" + std::to_string(panHeight),
        output);
    result.metadata["method"] = panMethod;
    result.metadata["bands"] = std::to_string(msBands);
    result.metadata["pan_width"] = std::to_string(panWidth);
    result.metadata["pan_height"] = std::to_string(panHeight);
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::ProcessingPlugin)
