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
            {"threshold", "filter", "enhance", "stats", "edge", "contour", "template_match", "pansharpen", "hough", "watershed", "skeleton", "kmeans"}
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
        gis::framework::ParamSpec{
            "hough_type", "霍夫类型", "霍夫变换检测类型",
            gis::framework::ParamType::Enum, false, std::string{"lines"},
            int{0}, int{0},
            {"lines", "circles"}
        },
        gis::framework::ParamSpec{
            "hough_threshold", "霍夫阈值", "霍夫变换累加器阈值",
            gis::framework::ParamType::Double, false, double{50.0}
        },
        gis::framework::ParamSpec{
            "min_line_length", "最小线长", "霍夫直线检测的最小线段长度",
            gis::framework::ParamType::Double, false, double{50.0}
        },
        gis::framework::ParamSpec{
            "max_line_gap", "最大线间隙", "霍夫直线检测的最大间隙",
            gis::framework::ParamType::Double, false, double{10.0}
        },
        gis::framework::ParamSpec{
            "min_radius", "最小半径", "霍夫圆检测的最小半径",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "max_radius", "最大半径", "霍夫圆检测的最大半径(0=自动)",
            gis::framework::ParamType::Int, false, int{0}
        },
        gis::framework::ParamSpec{
            "circle_param2", "圆检测参数", "霍夫圆检测的累加器阈值",
            gis::framework::ParamType::Double, false, double{30.0}
        },
        gis::framework::ParamSpec{
            "marker_input", "标记输入", "分水岭分割的种子标记文件(不指定则自动生成)",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "k", "聚类数", "K-Means聚类的类别数",
            gis::framework::ParamType::Int, false, int{5}
        },
        gis::framework::ParamSpec{
            "max_iter", "最大迭代", "K-Means最大迭代次数",
            gis::framework::ParamType::Int, false, int{100}
        },
        gis::framework::ParamSpec{
            "epsilon_kmeans", "收敛阈值", "K-Means收敛阈值",
            gis::framework::ParamType::Double, false, double{0.001}
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
    if (action == "stats")           return doStats(params, progress);
    if (action == "edge")            return doEdge(params, progress);
    if (action == "contour")         return doContour(params, progress);
    if (action == "template_match")  return doTemplateMatch(params, progress);
    if (action == "pansharpen")      return doPansharpen(params, progress);
    if (action == "hough")           return doHough(params, progress);
    if (action == "watershed")       return doWatershed(params, progress);
    if (action == "skeleton")        return doSkeleton(params, progress);
    if (action == "kmeans")          return doKMeans(params, progress);

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

gis::framework::Result ProcessingPlugin::doHough(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);
    std::string houghType = gis::framework::getParam<std::string>(params, "hough_type", "lines");
    double houghThresh = gis::framework::getParam<double>(params, "hough_threshold", 50.0);
    double minLineLen  = gis::framework::getParam<double>(params, "min_line_length", 50.0);
    double maxLineGap  = gis::framework::getParam<double>(params, "max_line_gap", 10.0);
    int minRadius      = gis::framework::getParam<int>(params, "min_radius", 1);
    int maxRadius      = gis::framework::getParam<int>(params, "max_radius", 0);
    double circleParam2 = gis::framework::getParam<double>(params, "circle_param2", 30.0);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);
    cv::Mat mat = readBandAsMat(input, band, progress);
    progress.onProgress(0.3);

    cv::Mat gray = gis::core::toUint8(mat);

    cv::Mat edges;
    cv::Canny(gray, edges, 50, 150);
    progress.onProgress(0.5);

    cv::Mat resultImg = cv::Mat::zeros(gray.size(), CV_8U);
    int detectCount = 0;

    if (houghType == "lines") {
        std::vector<cv::Vec4i> lines;
        cv::HoughLinesP(edges, lines, 1, CV_PI / 180, houghThresh, minLineLen, maxLineGap);
        detectCount = static_cast<int>(lines.size());

        for (auto& l : lines) {
            cv::line(resultImg, cv::Point(l[0], l[1]), cv::Point(l[2], l[3]),
                     cv::Scalar(255), 2);
        }
    } else if (houghType == "circles") {
        std::vector<cv::Vec3f> circles;
        cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT, 1,
                         20, 100, circleParam2, minRadius, maxRadius > 0 ? maxRadius : INT_MAX);
        detectCount = static_cast<int>(circles.size());

        for (auto& c : circles) {
            cv::Point center(cvRound(c[0]), cvRound(c[1]));
            int radius = cvRound(c[2]);
            cv::circle(resultImg, center, radius, cv::Scalar(255), 2);
            cv::circle(resultImg, center, 2, cv::Scalar(255), -1);
        }
    } else {
        return gis::framework::Result::fail("Unknown hough type: " + houghType);
    }

    progress.onProgress(0.8);

    std::ostringstream oss;
    oss << "Hough " << houghType << " detection: " << detectCount << " detected";
    progress.onMessage(oss.str());

    cv::Mat outFloat;
    resultImg.convertTo(outFloat, CV_32F, 1.0 / 255.0);

    auto result = writeMatOutput(outFloat, input, output, band, progress);
    result.metadata["hough_type"] = houghType;
    result.metadata["detect_count"] = std::to_string(detectCount);
    return result;
}

gis::framework::Result ProcessingPlugin::doWatershed(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);
    std::string markerInput = gis::framework::getParam<std::string>(params, "marker_input", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);
    cv::Mat mat = readBandAsMat(input, band, progress);
    progress.onProgress(0.3);

    cv::Mat gray = gis::core::toUint8(mat);

    cv::Mat markers;
    if (!markerInput.empty()) {
        cv::Mat markerMat = readBandAsMat(markerInput, 1, progress);
        markerMat.convertTo(markers, CV_32S);
    } else {
        cv::Mat binary;
        cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
        cv::Mat sureBg;
        cv::dilate(binary, sureBg, kernel, cv::Point(-1, -1), 3);

        cv::Mat distTransform;
        cv::distanceTransform(binary, distTransform, cv::DIST_L2, 5);

        double maxDist;
        cv::minMaxLoc(distTransform, nullptr, &maxDist);
        cv::Mat sureFg;
        cv::threshold(distTransform, sureFg, 0.5 * maxDist, 255, cv::THRESH_BINARY);
        sureFg.convertTo(sureFg, CV_8U);

        cv::Mat unknown;
        cv::subtract(sureBg, sureFg, unknown);

        cv::connectedComponents(sureFg, markers);
        for (int y = 0; y < markers.rows; ++y) {
            for (int x = 0; x < markers.cols; ++x) {
                if (unknown.at<uint8_t>(y, x) == 255) {
                    markers.at<int32_t>(y, x) = 0;
                }
            }
        }
    }
    progress.onProgress(0.5);

    cv::Mat bgr;
    if (mat.channels() == 1) {
        cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
    } else {
        bgr = gray.clone();
    }

    cv::watershed(bgr, markers);
    progress.onProgress(0.8);

    cv::Mat resultImg = cv::Mat::zeros(markers.size(), CV_32F);
    int segmentCount = 0;
    double minLabel, maxLabel;
    cv::minMaxLoc(markers, &minLabel, &maxLabel);
    segmentCount = static_cast<int>(maxLabel);

    for (int y = 0; y < markers.rows; ++y) {
        for (int x = 0; x < markers.cols; ++x) {
            int label = markers.at<int32_t>(y, x);
            if (label == -1) {
                resultImg.at<float>(y, x) = -1.0f;
            } else if (label > 0) {
                resultImg.at<float>(y, x) = static_cast<float>(label);
            }
        }
    }

    progress.onMessage("Watershed segmentation: " + std::to_string(segmentCount) + " segments");

    auto result = writeMatOutput(resultImg, input, output, band, progress);
    result.metadata["segment_count"] = std::to_string(segmentCount);
    return result;
}

gis::framework::Result ProcessingPlugin::doSkeleton(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);
    cv::Mat mat = readBandAsMat(input, band, progress);
    progress.onProgress(0.3);

    cv::Mat binary = gis::core::toUint8(mat);
    cv::threshold(binary, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    cv::Mat skeleton = cv::Mat::zeros(binary.size(), CV_8U);
    cv::Mat work = binary.clone();
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_CROSS, cv::Size(3, 3));
    cv::Mat eroded;
    cv::Mat opened;
    cv::Mat temp;

    while (true) {
        cv::erode(work, eroded, kernel);
        cv::dilate(eroded, opened, kernel);
        cv::subtract(work, opened, temp);
        cv::bitwise_or(skeleton, temp, skeleton);
        eroded.copyTo(work);
        if (cv::countNonZero(work) == 0) {
            break;
        }
    }

    progress.onProgress(0.7);

    cv::Mat outFloat;
    skeleton.convertTo(outFloat, CV_32F);
    auto result = writeMatOutput(outFloat, input, output, band, progress);
    result.metadata["action"] = "skeleton";
    return result;
}

gis::framework::Result ProcessingPlugin::doKMeans(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);
    int k = gis::framework::getParam<int>(params, "k", 5);
    int maxIter = gis::framework::getParam<int>(params, "max_iter", 100);
    double epsilon = gis::framework::getParam<double>(params, "epsilon_kmeans", 0.001);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (k <= 0) return gis::framework::Result::fail("k must be positive");

    progress.onProgress(0.1);

    auto ds = gis::core::openRaster(input, true);
    if (!ds) return gis::framework::Result::fail("Cannot open raster file: " + input);

    int width = ds->GetRasterXSize();
    int height = ds->GetRasterYSize();
    int bands = ds->GetRasterCount();

    progress.onMessage("Reading " + std::to_string(bands) + " bands for K-Means...");

    std::vector<cv::Mat> bandMats;
    for (int b = 1; b <= bands; ++b) {
        bandMats.push_back(gis::core::gdalBandToMat(ds.get(), b));
    }
    progress.onProgress(0.3);

    cv::Mat samples(width * height, bands, CV_32F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int b = 0; b < bands; ++b) {
                samples.at<float>(y * width + x, b) = bandMats[b].at<float>(y, x);
            }
        }
    }

    progress.onMessage("Running K-Means with k=" + std::to_string(k) + "...");

    cv::Mat labels, centers;
    cv::TermCriteria criteria(cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS,
                               maxIter, epsilon);
    double compactness = cv::kmeans(samples, k, labels, criteria,
                                     3, cv::KMEANS_PP_CENTERS, centers);

    progress.onProgress(0.8);

    cv::Mat resultImg(height, width, CV_32F);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int label = labels.at<int>(y * width + x);
            resultImg.at<float>(y, x) = static_cast<float>(label);
        }
    }

    progress.onMessage("K-Means completed: " + std::to_string(k) +
                        " clusters, compactness=" + std::to_string(compactness));

    auto result = writeMatOutput(resultImg, input, output, 1, progress);
    result.metadata["k"] = std::to_string(k);
    result.metadata["compactness"] = std::to_string(compactness);
    result.metadata["bands"] = std::to_string(bands);
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::ProcessingPlugin)
