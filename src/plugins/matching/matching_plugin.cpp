#include "matching_plugin.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <opencv2/opencv.hpp>
#include <opencv2/features2d.hpp>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace gis::plugins {

std::vector<gis::framework::ParamSpec> MatchingPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"detect", "match", "register", "change", "ecc_register", "corner"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入影像文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "reference", "参考影像", "参考影像文件路径(match/register/change)",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出文件", "输出文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "method", "特征方法", "特征检测/匹配方法",
            gis::framework::ParamType::Enum, false, std::string{"sift"},
            int{0}, int{0},
            {"sift", "orb", "akaze"}
        },
        gis::framework::ParamSpec{
            "match_method", "匹配方法", "特征匹配算法",
            gis::framework::ParamType::Enum, false, std::string{"bf"},
            int{0}, int{0},
            {"bf", "flann"}
        },
        gis::framework::ParamSpec{
            "max_points", "最大特征点数", "检测的最大特征点数量",
            gis::framework::ParamType::Int, false, int{5000}
        },
        gis::framework::ParamSpec{
            "ratio_test", "Lowe比率阈值", "Lowe比率测试阈值(0-1)",
            gis::framework::ParamType::Double, false, double{0.75}
        },
        gis::framework::ParamSpec{
            "transform", "变换模型", "配准变换模型",
            gis::framework::ParamType::Enum, false, std::string{"affine"},
            int{0}, int{0},
            {"affine", "projective", "similarity", "translation"}
        },
        gis::framework::ParamSpec{
            "resample", "重采样方式", "配准重采样方式",
            gis::framework::ParamType::Enum, false, std::string{"bilinear"},
            int{0}, int{0},
            {"nearest", "bilinear", "cubic"}
        },
        gis::framework::ParamSpec{
            "change_method", "变化检测方法", "变化检测算法",
            gis::framework::ParamType::Enum, false, std::string{"differencing"},
            int{0}, int{0},
            {"differencing", "ratio", "pcd"}
        },
        gis::framework::ParamSpec{
            "threshold", "变化阈值", "变化检测阈值(0=自动)",
            gis::framework::ParamType::Double, false, double{0.0}
        },
        gis::framework::ParamSpec{
            "band", "波段序号", "处理的波段序号(从1开始)",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "ecc_motion", "ECC运动模型", "ECC配准的运动模型",
            gis::framework::ParamType::Enum, false, std::string{"affine"},
            int{0}, int{0},
            {"translation", "euclidean", "affine", "homography"}
        },
        gis::framework::ParamSpec{
            "ecc_iterations", "ECC迭代次数", "ECC配准最大迭代次数",
            gis::framework::ParamType::Int, false, int{200}
        },
        gis::framework::ParamSpec{
            "ecc_epsilon", "ECC收敛阈值", "ECC配准收敛阈值",
            gis::framework::ParamType::Double, false, double{1e-6}
        },
        gis::framework::ParamSpec{
            "corner_method", "角点方法", "角点检测方法",
            gis::framework::ParamType::Enum, false, std::string{"harris"},
            int{0}, int{0},
            {"harris", "shi_tomasi"}
        },
        gis::framework::ParamSpec{
            "max_corners", "最大角点数", "检测的最大角点数量",
            gis::framework::ParamType::Int, false, int{5000}
        },
        gis::framework::ParamSpec{
            "quality_level", "质量水平", "角点检测质量水平(0-1)",
            gis::framework::ParamType::Double, false, double{0.01}
        },
        gis::framework::ParamSpec{
            "min_distance", "最小间距", "角点之间的最小欧氏距离",
            gis::framework::ParamType::Double, false, double{10.0}
        },
    };
}

gis::framework::Result MatchingPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string action = gis::framework::getParam<std::string>(params, "action", "");

    if (action == "detect")   return doDetect(params, progress);
    if (action == "match")    return doMatch(params, progress);
    if (action == "register") return doRegister(params, progress);
    if (action == "change")       return doChange(params, progress);
    if (action == "ecc_register") return doEccRegister(params, progress);
    if (action == "corner")       return doCornerDetect(params, progress);

    return gis::framework::Result::fail("Unknown action: " + action);
}

static cv::Ptr<cv::Feature2D> createDetector(const std::string& method, int maxPoints) {
    if (method == "orb")   return cv::ORB::create(maxPoints);
    if (method == "akaze") return cv::AKAZE::create();
    return cv::SIFT::create(maxPoints);
}

static cv::Ptr<cv::DescriptorMatcher> createMatcher(const std::string& method,
                                                     const std::string& matchMethod) {
    if (matchMethod == "flann") {
        if (method == "orb") {
            return cv::makePtr<cv::FlannBasedMatcher>(cv::makePtr<cv::flann::LshIndexParams>(6, 12, 1));
        }
        return cv::makePtr<cv::FlannBasedMatcher>();
    }
    if (method == "orb") {
        return cv::DescriptorMatcher::create(cv::DescriptorMatcher::BRUTEFORCE_HAMMING);
    }
    return cv::DescriptorMatcher::create(cv::DescriptorMatcher::BRUTEFORCE);
}

static cv::Mat readBandAsMat(const std::string& path, int band,
                              gis::core::ProgressReporter& progress) {
    progress.onMessage("Reading band " + std::to_string(band) + " from " + path);
    return gis::core::readBandAsMat(path, band);
}

static bool writeKeyPointsJSON(const std::string& path,
                                const std::vector<cv::KeyPoint>& kps) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    ofs << "{\"keypoints\":[\n";
    for (size_t i = 0; i < kps.size(); ++i) {
        ofs << "  {\"x\":" << kps[i].pt.x
            << ",\"y\":" << kps[i].pt.y
            << ",\"size\":" << kps[i].size
            << ",\"angle\":" << kps[i].angle
            << ",\"response\":" << kps[i].response
            << "}";
        if (i + 1 < kps.size()) ofs << ",";
        ofs << "\n";
    }
    ofs << "],\"count\":" << kps.size() << "}\n";
    return true;
}

static bool writeMatchesJSON(const std::string& path,
                              const std::vector<cv::DMatch>& matches,
                              const std::vector<cv::KeyPoint>& kps1,
                              const std::vector<cv::KeyPoint>& kps2) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    ofs << "{\"matches\":[\n";
    for (size_t i = 0; i < matches.size(); ++i) {
        const auto& m = matches[i];
        ofs << "  {\"idx1\":" << m.queryIdx
            << ",\"idx2\":" << m.trainIdx
            << ",\"distance\":" << m.distance
            << ",\"pt1\":[" << kps1[m.queryIdx].pt.x << "," << kps1[m.queryIdx].pt.y << "]"
            << ",\"pt2\":[" << kps2[m.trainIdx].pt.x << "," << kps2[m.trainIdx].pt.y << "]"
            << "}";
        if (i + 1 < matches.size()) ofs << ",";
        ofs << "\n";
    }
    ofs << "],\"count\":" << matches.size() << "}\n";
    return true;
}

gis::framework::Result MatchingPlugin::doDetect(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string method = gis::framework::getParam<std::string>(params, "method", "sift");
    int maxPoints      = gis::framework::getParam<int>(params, "max_points", 5000);
    int band           = gis::framework::getParam<int>(params, "band", 1);

    if (input.empty()) return gis::framework::Result::fail("input is required");

    progress.onProgress(0.1);
    cv::Mat mat = readBandAsMat(input, band, progress);
    cv::Mat gray = gis::core::toUint8(mat);
    progress.onProgress(0.3);

    auto detector = createDetector(method, maxPoints);
    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;

    progress.onMessage("Detecting " + method + " features...");
    detector->detectAndCompute(gray, cv::noArray(), keypoints, descriptors);
    progress.onProgress(0.8);

    progress.onMessage("Detected " + std::to_string(keypoints.size()) + " keypoints");

    if (!output.empty()) {
        if (!writeKeyPointsJSON(output, keypoints)) {
            return gis::framework::Result::fail("Failed to write output: " + output);
        }
        progress.onProgress(1.0);
        auto result = gis::framework::Result::ok(
            "Detected " + std::to_string(keypoints.size()) + " keypoints", output);
        result.metadata["keypoint_count"] = std::to_string(keypoints.size());
        result.metadata["method"] = method;
        return result;
    }

    progress.onProgress(1.0);
    auto result = gis::framework::Result::ok(
        "Detected " + std::to_string(keypoints.size()) + " keypoints using " + method);
    result.metadata["keypoint_count"] = std::to_string(keypoints.size());
    result.metadata["method"] = method;
    return result;
}

gis::framework::Result MatchingPlugin::doMatch(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string reference   = gis::framework::getParam<std::string>(params, "reference", "");
    std::string input       = gis::framework::getParam<std::string>(params, "input", "");
    std::string output      = gis::framework::getParam<std::string>(params, "output", "");
    std::string method      = gis::framework::getParam<std::string>(params, "method", "sift");
    std::string matchMethod = gis::framework::getParam<std::string>(params, "match_method", "bf");
    int maxPoints           = gis::framework::getParam<int>(params, "max_points", 5000);
    double ratioTest        = gis::framework::getParam<double>(params, "ratio_test", 0.75);
    int band                = gis::framework::getParam<int>(params, "band", 1);

    if (reference.empty()) return gis::framework::Result::fail("reference is required");
    if (input.empty())     return gis::framework::Result::fail("input is required");

    progress.onProgress(0.05);
    cv::Mat refMat = readBandAsMat(reference, band, progress);
    cv::Mat srcMat = readBandAsMat(input, band, progress);

    cv::Mat refGray = gis::core::toUint8(refMat);
    cv::Mat srcGray = gis::core::toUint8(srcMat);
    progress.onProgress(0.2);

    auto detector = createDetector(method, maxPoints);

    std::vector<cv::KeyPoint> kp1, kp2;
    cv::Mat desc1, desc2;

    progress.onMessage("Detecting features in reference...");
    detector->detectAndCompute(refGray, cv::noArray(), kp1, desc1);
    progress.onProgress(0.4);

    progress.onMessage("Detecting features in search image...");
    detector->detectAndCompute(srcGray, cv::noArray(), kp2, desc2);
    progress.onProgress(0.6);

    if (kp1.empty() || kp2.empty()) {
        return gis::framework::Result::fail("Not enough features detected in one or both images");
    }

    progress.onMessage("Matching features (" + matchMethod + ")...");
    auto matcher = createMatcher(method, matchMethod);

    std::vector<std::vector<cv::DMatch>> knnMatches;
    matcher->knnMatch(desc1, desc2, knnMatches, 2);

    std::vector<cv::DMatch> goodMatches;
    for (auto& m : knnMatches) {
        if (m.size() >= 2 && m[0].distance < ratioTest * m[1].distance) {
            goodMatches.push_back(m[0]);
        }
    }
    progress.onProgress(0.85);

    progress.onMessage("Found " + std::to_string(goodMatches.size()) + " good matches (from " +
                        std::to_string(knnMatches.size()) + " initial)");

    if (!output.empty()) {
        if (!writeMatchesJSON(output, goodMatches, kp1, kp2)) {
            return gis::framework::Result::fail("Failed to write output: " + output);
        }
    }

    progress.onProgress(1.0);
    auto result = gis::framework::Result::ok(
        "Matched " + std::to_string(goodMatches.size()) + " features", output);
    result.metadata["match_count"] = std::to_string(goodMatches.size());
    result.metadata["ref_keypoints"] = std::to_string(kp1.size());
    result.metadata["src_keypoints"] = std::to_string(kp2.size());
    result.metadata["method"] = method;
    result.metadata["match_method"] = matchMethod;
    return result;
}

gis::framework::Result MatchingPlugin::doRegister(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string reference  = gis::framework::getParam<std::string>(params, "reference", "");
    std::string input      = gis::framework::getParam<std::string>(params, "input", "");
    std::string output     = gis::framework::getParam<std::string>(params, "output", "");
    std::string method     = gis::framework::getParam<std::string>(params, "method", "sift");
    std::string matchMethod= gis::framework::getParam<std::string>(params, "match_method", "bf");
    std::string transformType = gis::framework::getParam<std::string>(params, "transform", "affine");
    std::string resample   = gis::framework::getParam<std::string>(params, "resample", "bilinear");
    int maxPoints          = gis::framework::getParam<int>(params, "max_points", 5000);
    double ratioTest       = gis::framework::getParam<double>(params, "ratio_test", 0.75);
    int band               = gis::framework::getParam<int>(params, "band", 1);

    if (reference.empty()) return gis::framework::Result::fail("reference is required");
    if (input.empty())     return gis::framework::Result::fail("input is required");
    if (output.empty())    return gis::framework::Result::fail("output is required");

    progress.onProgress(0.05);

    cv::Mat refMat = readBandAsMat(reference, band, progress);
    cv::Mat srcMat = readBandAsMat(input, band, progress);
    cv::Mat refGray = gis::core::toUint8(refMat);
    cv::Mat srcGray = gis::core::toUint8(srcMat);
    progress.onProgress(0.15);

    auto detector = createDetector(method, maxPoints);
    std::vector<cv::KeyPoint> kp1, kp2;
    cv::Mat desc1, desc2;

    progress.onMessage("Detecting features in reference...");
    detector->detectAndCompute(refGray, cv::noArray(), kp1, desc1);
    progress.onMessage("Detecting features in input...");
    detector->detectAndCompute(srcGray, cv::noArray(), kp2, desc2);
    progress.onProgress(0.35);

    if (kp1.empty() || kp2.empty()) {
        return gis::framework::Result::fail("Not enough features detected for registration");
    }

    progress.onMessage("Matching features...");
    auto matcher = createMatcher(method, matchMethod);
    std::vector<std::vector<cv::DMatch>> knnMatches;
    matcher->knnMatch(desc1, desc2, knnMatches, 2);

    std::vector<cv::DMatch> goodMatches;
    for (auto& m : knnMatches) {
        if (m.size() >= 2 && m[0].distance < ratioTest * m[1].distance) {
            goodMatches.push_back(m[0]);
        }
    }
    progress.onProgress(0.5);

    if (goodMatches.size() < 4) {
        return gis::framework::Result::fail(
            "Not enough good matches for registration: " + std::to_string(goodMatches.size()));
    }

    std::vector<cv::Point2f> pts1, pts2;
    for (auto& m : goodMatches) {
        pts1.push_back(kp1[m.queryIdx].pt);
        pts2.push_back(kp2[m.trainIdx].pt);
    }

    progress.onMessage("Computing " + transformType + " transform from " +
                        std::to_string(pts1.size()) + " point pairs...");

    cv::Mat H;
    if (transformType == "projective") {
        H = cv::findHomography(pts2, pts1, cv::RANSAC);
    } else {
        H = cv::estimateAffine2D(pts2, pts1, cv::noArray(), cv::RANSAC);
        if (H.empty()) {
            H = cv::estimateAffinePartial2D(pts2, pts1, cv::noArray(), cv::RANSAC);
        }
    }

    if (H.empty()) {
        return gis::framework::Result::fail("Failed to compute transformation matrix");
    }
    progress.onProgress(0.65);

    progress.onMessage("Warping image...");
    int interpFlag = cv::INTER_NEAREST;
    if (resample == "bilinear") interpFlag = cv::INTER_LINEAR;
    else if (resample == "cubic") interpFlag = cv::INTER_CUBIC;

    cv::Mat warped;
    if (H.rows == 3) {
        cv::warpPerspective(srcMat, warped, H, refMat.size(), interpFlag);
    } else {
        cv::warpAffine(srcMat, warped, H, refMat.size(), interpFlag);
    }
    progress.onProgress(0.85);

    progress.onMessage("Writing output: " + output);
    gis::core::matToGdalTiff(warped, reference, output, band);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Registration completed with " + std::to_string(goodMatches.size()) +
        " matches, transform=" + transformType, output);
    result.metadata["match_count"] = std::to_string(goodMatches.size());
    result.metadata["transform"] = transformType;
    return result;
}

gis::framework::Result MatchingPlugin::doChange(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string reference    = gis::framework::getParam<std::string>(params, "reference", "");
    std::string input        = gis::framework::getParam<std::string>(params, "input", "");
    std::string output       = gis::framework::getParam<std::string>(params, "output", "");
    std::string changeMethod = gis::framework::getParam<std::string>(params, "change_method", "differencing");
    double threshVal         = gis::framework::getParam<double>(params, "threshold", 0.0);
    int band                 = gis::framework::getParam<int>(params, "band", 1);

    if (reference.empty()) return gis::framework::Result::fail("reference is required (before image)");
    if (input.empty())     return gis::framework::Result::fail("input is required (after image)");
    if (output.empty())    return gis::framework::Result::fail("output is required");

    progress.onProgress(0.05);

    cv::Mat before = readBandAsMat(reference, band, progress);
    cv::Mat after  = readBandAsMat(input, band, progress);
    progress.onProgress(0.3);

    if (before.size() != after.size()) {
        return gis::framework::Result::fail(
            "Image sizes do not match: " +
            std::to_string(before.cols) + "x" + std::to_string(before.rows) + " vs " +
            std::to_string(after.cols) + "x" + std::to_string(after.rows));
    }

    cv::Mat changeMap;
    progress.onMessage("Computing change map (" + changeMethod + ")...");

    if (changeMethod == "differencing") {
        cv::absdiff(before, after, changeMap);
    } else if (changeMethod == "ratio") {
        cv::Mat ratio;
        cv::divide(after + 1e-10, before + 1e-10, ratio);
        cv::Mat logRatio;
        cv::log(ratio, logRatio);
        changeMap = cv::abs(logRatio);
    } else if (changeMethod == "pcd") {
        std::vector<cv::Mat> channels = {before, after};
        cv::Mat stacked;
        cv::merge(channels, stacked);
        cv::Mat mean;
        cv::reduce(stacked.reshape(1, stacked.rows * stacked.cols), mean, 1, cv::REDUCE_AVG);
        mean = mean.reshape(stacked.channels(), stacked.rows);
        cv::Mat diff1, diff2;
        cv::subtract(before, mean, diff1);
        cv::subtract(after, mean, diff2);
        cv::magnitude(diff1, diff2, changeMap);
    } else {
        return gis::framework::Result::fail("Unknown change method: " + changeMethod);
    }
    progress.onProgress(0.7);

    if (threshVal <= 0) {
        cv::Scalar meanVal, stdDev;
        cv::meanStdDev(changeMap, meanVal, stdDev);
        threshVal = meanVal[0] + stdDev[0];
        progress.onMessage("Auto threshold: " + std::to_string(threshVal) +
                           " (mean + stddev)");
    }

    cv::Mat changeMask;
    cv::threshold(changeMap, changeMask, threshVal, 255, cv::THRESH_BINARY);
    changeMask.convertTo(changeMask, CV_8U);
    progress.onProgress(0.85);

    int changedPixels = cv::countNonZero(changeMask);
    int totalPixels = changeMask.rows * changeMask.cols;
    double changeRatio = static_cast<double>(changedPixels) / totalPixels;

    progress.onMessage("Writing output: " + output);
    cv::Mat outFloat;
    changeMap.convertTo(outFloat, CV_32F);
    gis::core::matToGdalTiff(outFloat, reference, output, band);
    progress.onProgress(1.0);

    progress.onMessage("Changed pixels: " + std::to_string(changedPixels) + "/" +
                        std::to_string(totalPixels) + " (" +
                        std::to_string(changeRatio * 100.0) + "%)");

    std::ostringstream msg;
    msg << "Change detection completed: " << std::fixed << std::setprecision(2)
        << (changeRatio * 100.0) << "% changed pixels";

    auto result = gis::framework::Result::ok(msg.str(), output);
    result.metadata["changed_pixels"] = std::to_string(changedPixels);
    result.metadata["total_pixels"] = std::to_string(totalPixels);
    result.metadata["change_ratio"] = std::to_string(changeRatio);
    result.metadata["threshold"] = std::to_string(threshVal);
    result.metadata["method"] = changeMethod;
    return result;
}

gis::framework::Result MatchingPlugin::doEccRegister(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string reference  = gis::framework::getParam<std::string>(params, "reference", "");
    std::string input      = gis::framework::getParam<std::string>(params, "input", "");
    std::string output     = gis::framework::getParam<std::string>(params, "output", "");
    std::string eccMotion  = gis::framework::getParam<std::string>(params, "ecc_motion", "affine");
    int eccIterations      = gis::framework::getParam<int>(params, "ecc_iterations", 200);
    double eccEpsilon      = gis::framework::getParam<double>(params, "ecc_epsilon", 1e-6);
    std::string resample   = gis::framework::getParam<std::string>(params, "resample", "bilinear");
    int band               = gis::framework::getParam<int>(params, "band", 1);

    if (reference.empty()) return gis::framework::Result::fail("reference is required");
    if (input.empty())     return gis::framework::Result::fail("input is required");
    if (output.empty())    return gis::framework::Result::fail("output is required");

    progress.onProgress(0.05);

    cv::Mat refMat = readBandAsMat(reference, band, progress);
    cv::Mat srcMat = readBandAsMat(input, band, progress);
    cv::Mat refGray = gis::core::toUint8(refMat);
    cv::Mat srcGray = gis::core::toUint8(srcMat);
    progress.onProgress(0.2);

    if (refGray.size() != srcGray.size()) {
        cv::resize(srcGray, srcGray, refGray.size(), 0, 0, cv::INTER_LINEAR);
        cv::resize(srcMat, srcMat, refMat.size(), 0, 0, cv::INTER_LINEAR);
        progress.onMessage("Resized input to match reference dimensions");
    }

    int motionType;
    cv::Mat warpMat;
    if (eccMotion == "translation") {
        motionType = cv::MOTION_TRANSLATION;
        warpMat = cv::Mat::eye(2, 3, CV_32F);
    } else if (eccMotion == "euclidean") {
        motionType = cv::MOTION_EUCLIDEAN;
        warpMat = cv::Mat::eye(2, 3, CV_32F);
    } else if (eccMotion == "homography") {
        motionType = cv::MOTION_HOMOGRAPHY;
        warpMat = cv::Mat::eye(3, 3, CV_32F);
    } else {
        motionType = cv::MOTION_AFFINE;
        warpMat = cv::Mat::eye(2, 3, CV_32F);
    }

    progress.onMessage("Running ECC registration (" + eccMotion + ")...");

    cv::TermCriteria criteria(cv::TermCriteria::COUNT + cv::TermCriteria::EPS,
                               eccIterations, eccEpsilon);

    try {
        double cc = cv::findTransformECC(refGray, srcGray, warpMat, motionType, criteria);
        progress.onMessage("ECC correlation coefficient: " + std::to_string(cc));
    } catch (const cv::Exception& e) {
        return gis::framework::Result::fail(
            "ECC registration failed: " + std::string(e.what()));
    }

    progress.onProgress(0.7);

    progress.onMessage("Warping image...");
    int interpFlag = cv::INTER_NEAREST;
    if (resample == "bilinear") interpFlag = cv::INTER_LINEAR;
    else if (resample == "cubic") interpFlag = cv::INTER_CUBIC;

    cv::Mat warped;
    if (motionType == cv::MOTION_HOMOGRAPHY) {
        cv::warpPerspective(srcMat, warped, warpMat, refMat.size(), interpFlag);
    } else {
        cv::warpAffine(srcMat, warped, warpMat, refMat.size(), interpFlag);
    }
    progress.onProgress(0.85);

    progress.onMessage("Writing output: " + output);
    gis::core::matToGdalTiff(warped, reference, output, band);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "ECC registration completed, motion=" + eccMotion, output);
    result.metadata["motion_type"] = eccMotion;

    std::ostringstream warpOss;
    warpOss << warpMat;
    result.metadata["warp_matrix"] = warpOss.str();
    return result;
}

gis::framework::Result MatchingPlugin::doCornerDetect(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string cornerMethod = gis::framework::getParam<std::string>(params, "corner_method", "harris");
    int maxCorners    = gis::framework::getParam<int>(params, "max_corners", 5000);
    double qualityLevel = gis::framework::getParam<double>(params, "quality_level", 0.01);
    double minDistance = gis::framework::getParam<double>(params, "min_distance", 10.0);
    int band          = gis::framework::getParam<int>(params, "band", 1);

    if (input.empty()) return gis::framework::Result::fail("input is required");

    progress.onProgress(0.1);
    cv::Mat mat = readBandAsMat(input, band, progress);
    cv::Mat gray = gis::core::toUint8(mat);
    progress.onProgress(0.3);

    std::vector<cv::Point2f> corners;

    if (cornerMethod == "shi_tomasi") {
        cv::goodFeaturesToTrack(gray, corners, maxCorners, qualityLevel, minDistance);
    } else {
        cv::Mat harrisResp;
        cv::cornerHarris(gray, harrisResp, 2, 3, 0.04);

        cv::Mat harrisNorm;
        cv::normalize(harrisResp, harrisNorm, 0, 255, cv::NORM_MINMAX, CV_32F);
        cv::Mat harrisU8;
        harrisNorm.convertTo(harrisU8, CV_8U);

        double maxVal;
        cv::minMaxLoc(harrisResp, nullptr, &maxVal);
        double thresh = maxVal * qualityLevel;

        std::vector<cv::Point> candidates;
        for (int y = 1; y < harrisResp.rows - 1; ++y) {
            for (int x = 1; x < harrisResp.cols - 1; ++x) {
                if (harrisResp.at<float>(y, x) > thresh) {
                    bool isMax = true;
                    for (int dy = -1; dy <= 1 && isMax; ++dy) {
                        for (int dx = -1; dx <= 1 && isMax; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            if (harrisResp.at<float>(y + dy, x + dx) > harrisResp.at<float>(y, x)) {
                                isMax = false;
                            }
                        }
                    }
                    if (isMax) candidates.emplace_back(x, y);
                }
            }
        }

        std::sort(candidates.begin(), candidates.end(), [&](const cv::Point& a, const cv::Point& b) {
            return harrisResp.at<float>(a.y, a.x) > harrisResp.at<float>(b.y, b.x);
        });

        std::vector<bool> suppressed(gray.rows * gray.cols, false);
        int minDist = static_cast<int>(minDistance);
        for (auto& pt : candidates) {
            if (suppressed[pt.y * gray.cols + pt.x]) continue;
            corners.push_back(cv::Point2f(pt.x, pt.y));
            if (static_cast<int>(corners.size()) >= maxCorners) break;

            int yStart = std::max(0, pt.y - minDist);
            int yEnd = std::min(gray.rows - 1, pt.y + minDist);
            int xStart = std::max(0, pt.x - minDist);
            int xEnd = std::min(gray.cols - 1, pt.x + minDist);
            for (int yy = yStart; yy <= yEnd; ++yy) {
                for (int xx = xStart; xx <= xEnd; ++xx) {
                    suppressed[yy * gray.cols + xx] = true;
                }
            }
        }
    }

    progress.onProgress(0.8);

    progress.onMessage("Detected " + std::to_string(corners.size()) + " " + cornerMethod + " corners");

    if (!output.empty()) {
        if (!writeKeyPointsJSON(output,
                [&]() {
                    std::vector<cv::KeyPoint> kps;
                    for (auto& c : corners) {
                        kps.emplace_back(c.x, c.y, 3.0f, -1, 0.0f);
                    }
                    return kps;
                }())) {
            return gis::framework::Result::fail("Failed to write output: " + output);
        }
    }

    progress.onProgress(1.0);
    auto result = gis::framework::Result::ok(
        "Detected " + std::to_string(corners.size()) + " " + cornerMethod + " corners", output);
    result.metadata["corner_count"] = std::to_string(corners.size());
    result.metadata["method"] = cornerMethod;
    return result;
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::MatchingPlugin)
