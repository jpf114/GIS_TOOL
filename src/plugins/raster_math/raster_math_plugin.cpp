#include "raster_math_plugin.h"

#include <gis/core/error.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>

#include <gdal_priv.h>
#include <opencv2/opencv.hpp>

#include <cctype>
#include <cmath>
#include <map>
#include <stack>
#include <string>
#include <vector>

namespace gis::plugins {

namespace {

static double evalExpression(const std::string& expr,
                             const std::map<std::string, double>& bandValues) {
    std::string e = expr;
    for (const auto& [key, val] : bandValues) {
        const std::string replacement = std::to_string(val);
        size_t pos = 0;
        while ((pos = e.find(key, pos)) != std::string::npos) {
            e.replace(pos, key.length(), replacement);
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
            if (std::isspace(static_cast<unsigned char>(e[i]))) {
                ++i;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(e[i])) || e[i] == '.') {
                const size_t start = i;
                while (i < e.size() &&
                       (std::isdigit(static_cast<unsigned char>(e[i])) || e[i] == '.')) {
                    ++i;
                }
                vals.push(std::stod(e.substr(start, i - start)));
            } else if (e[i] == '(') {
                ops.push('(');
                ++i;
            } else if (e[i] == ')') {
                while (!ops.empty() && ops.top() != '(') {
                    const double b = vals.top(); vals.pop();
                    const double a = vals.top(); vals.pop();
                    vals.push(applyOp(a, b, ops.top()));
                    ops.pop();
                }
                if (!ops.empty()) {
                    ops.pop();
                }
                ++i;
            } else if (e[i] == '+' || e[i] == '-' || e[i] == '*' || e[i] == '/') {
                while (!ops.empty() && precedence(ops.top()) >= precedence(e[i])) {
                    const double b = vals.top(); vals.pop();
                    const double a = vals.top(); vals.pop();
                    vals.push(applyOp(a, b, ops.top()));
                    ops.pop();
                }
                ops.push(e[i]);
                ++i;
            } else {
                ++i;
            }
        }
        while (!ops.empty()) {
            const double b = vals.top(); vals.pop();
            const double a = vals.top(); vals.pop();
            vals.push(applyOp(a, b, ops.top()));
            ops.pop();
        }
        return vals.empty() ? 0.0 : vals.top();
    } catch (...) {
        return 0.0;
    }
}

} // namespace

std::vector<gis::framework::ParamSpec> RasterMathPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"band_math"}
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
            "expression", "表达式", "波段运算表达式，例如 B1+B2",
            gis::framework::ParamType::String, false, std::string{}
        },
    };
}

gis::framework::Result RasterMathPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string expression = gis::framework::getParam<std::string>(params, "expression", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (expression.empty()) {
        return gis::framework::Result::fail("expression is required (e.g., B1+B2, B1*0.5+B2*0.5)");
    }

    progress.onProgress(0.1);
    auto ds = gis::core::openRaster(input, true);
    const int width = ds->GetRasterXSize();
    const int height = ds->GetRasterYSize();
    const int bands = ds->GetRasterCount();

    progress.onMessage("Reading " + std::to_string(bands) + " bands...");
    std::vector<cv::Mat> bandMats;
    bandMats.reserve(static_cast<size_t>(bands));
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
                bandValues["B" + std::to_string(b + 1)] = bandMats[static_cast<size_t>(b)].at<float>(y, x);
            }
            result.at<float>(y, x) = static_cast<float>(evalExpression(expression, bandValues));
        }
        if ((y % 100) == 0) {
            progress.onProgress(0.4 + 0.5 * static_cast<double>(y) / height);
        }
    }

    if (output.empty()) {
        return gis::framework::Result::fail("output is required");
    }

    progress.onProgress(0.9);
    progress.onMessage("Writing output: " + output);
    gis::core::matToGdalTiff(result, input, output, 1);
    progress.onProgress(1.0);
    return gis::framework::Result::ok("Processing completed successfully", output);
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::RasterMathPlugin)
