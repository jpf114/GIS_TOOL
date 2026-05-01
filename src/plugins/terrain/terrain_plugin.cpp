#include "terrain_plugin.h"

#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>

#include <cpl_conv.h>
#include <gdal_alg.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <queue>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace gis::plugins {

namespace {

struct DemOptionsDeleter {
    void operator()(GDALDEMProcessingOptions* options) const {
        if (options) {
            GDALDEMProcessingOptionsFree(options);
        }
    }
};

struct DirectionStep {
    int dx;
    int dy;
    float distance;
    float code;
};

static const DirectionStep kD8Directions[] = {
    {1, 0, 1.0f, 1.0f},
    {1, 1, 1.41421356f, 2.0f},
    {0, 1, 1.0f, 4.0f},
    {-1, 1, 1.41421356f, 8.0f},
    {-1, 0, 1.0f, 16.0f},
    {-1, -1, 1.41421356f, 32.0f},
    {0, -1, 1.0f, 64.0f},
    {1, -1, 1.41421356f, 128.0f},
};

struct ProfilePoint {
    double x = 0.0;
    double y = 0.0;
};

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

gis::framework::Result runDemProcess(
    const char* processName,
    const std::string& successLabel,
    const std::string& input,
    const std::string& output,
    int band,
    double zFactor,
    double azimuth,
    double altitude,
    gis::core::ProgressReporter& progress) {

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");
    if (zFactor <= 0.0) return gis::framework::Result::fail("z_factor must be greater than 0");

    progress.onProgress(0.1);
    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        return gis::framework::Result::fail("Cannot open DEM raster: " + input);
    }
    if (!srcDs->GetRasterBand(band)) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    std::vector<std::string> argStorage = {
        "-of", "GTiff",
        "-b", std::to_string(band),
        "-z", std::to_string(zFactor)
    };
    if (std::string(processName) == "hillshade") {
        argStorage.push_back("-az");
        argStorage.push_back(std::to_string(azimuth));
        argStorage.push_back("-alt");
        argStorage.push_back(std::to_string(altitude));
    }

    std::vector<char*> argv;
    argv.reserve(argStorage.size() + 1);
    for (auto& item : argStorage) {
        argv.push_back(item.data());
    }
    argv.push_back(nullptr);

    std::unique_ptr<GDALDEMProcessingOptions, DemOptionsDeleter> options(
        GDALDEMProcessingOptionsNew(argv.data(), nullptr));
    if (!options) {
        return gis::framework::Result::fail("Failed to create terrain processing options");
    }

    progress.onMessage("Running terrain action: " + successLabel);
    progress.onProgress(0.4);

    int usageError = FALSE;
    GDALDatasetH outHandle = GDALDEMProcessing(
        output.c_str(),
        GDALDataset::ToHandle(srcDs.get()),
        processName,
        nullptr,
        options.get(),
        &usageError);

    if (!outHandle || usageError) {
        if (outHandle) {
            GDALClose(outHandle);
        }
        return gis::framework::Result::fail(
            "Terrain processing failed: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALClose(outHandle);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(successLabel + " completed", output);
    result.metadata["action"] = processName;
    result.metadata["band"] = std::to_string(band);
    result.metadata["z_factor"] = std::to_string(zFactor);
    if (std::string(processName) == "hillshade") {
        result.metadata["azimuth"] = std::to_string(azimuth);
        result.metadata["altitude"] = std::to_string(altitude);
    }
    return result;
}

gis::framework::Result runLocalTerrainProcess(
    const std::string& action,
    const std::string& successLabel,
    const std::string& input,
    const std::string& output,
    int band,
    double zFactor,
    gis::core::ProgressReporter& progress) {

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");
    if (zFactor <= 0.0) return gis::framework::Result::fail("z_factor must be greater than 0");

    progress.onProgress(0.1);
    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        return gis::framework::Result::fail("Cannot open DEM raster: " + input);
    }

    auto* srcBand = srcDs->GetRasterBand(band);
    if (!srcBand) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    progress.onMessage("Reading terrain raster band");
    cv::Mat elevation = gis::core::gdalBandToMat(srcDs.get(), band);
    if (zFactor != 1.0) {
        elevation *= static_cast<float>(zFactor);
    }
    progress.onProgress(0.35);

    cv::Mat result;
    if (action == "tpi") {
        cv::Mat neighborhoodMean;
        cv::boxFilter(
            elevation,
            neighborhoodMean,
            CV_32F,
            cv::Size(3, 3),
            cv::Point(-1, -1),
            true,
            cv::BORDER_REPLICATE);
        result = elevation - ((neighborhoodMean * 9.0f - elevation) / 8.0f);
    } else if (action == "roughness") {
        cv::Mat neighborhoodMax;
        cv::Mat neighborhoodMin;
        cv::dilate(elevation, neighborhoodMax, cv::Mat(), cv::Point(-1, -1), 1, cv::BORDER_REPLICATE);
        cv::erode(elevation, neighborhoodMin, cv::Mat(), cv::Point(-1, -1), 1, cv::BORDER_REPLICATE);
        result = neighborhoodMax - neighborhoodMin;
    } else {
        return gis::framework::Result::fail("Unknown local terrain action: " + action);
    }
    progress.onProgress(0.7);

    int hasNoData = 0;
    const double noDataValue = srcBand->GetNoDataValue(&hasNoData);
    if (hasNoData) {
        cv::Mat noDataMask;
        cv::compare(elevation, static_cast<float>(noDataValue * zFactor), noDataMask, cv::CMP_EQ);
        result.setTo(static_cast<float>(noDataValue), noDataMask);
    }

    progress.onMessage("Writing terrain output: " + output);
    gis::core::matToGdalTiff(result, srcDs.get(), output, band);
    progress.onProgress(1.0);

    auto terrainResult = gis::framework::Result::ok(successLabel + " completed", output);
    terrainResult.metadata["action"] = action;
    terrainResult.metadata["band"] = std::to_string(band);
    terrainResult.metadata["z_factor"] = std::to_string(zFactor);
    terrainResult.metadata["window_size"] = "3";
    return terrainResult;
}

gis::framework::Result runHydrologyTerrainProcess(
    const std::string& action,
    const std::string& successLabel,
    const std::string& input,
    const std::string& output,
    int band,
    double zFactor,
    double accumulationThreshold,
    gis::core::ProgressReporter& progress) {

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");
    if (zFactor <= 0.0) return gis::framework::Result::fail("z_factor must be greater than 0");
    if ((action == "stream_extract") && accumulationThreshold <= 0.0) {
        return gis::framework::Result::fail("accum_threshold must be greater than 0");
    }

    progress.onProgress(0.1);
    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        return gis::framework::Result::fail("Cannot open DEM raster: " + input);
    }

    auto* srcBand = srcDs->GetRasterBand(band);
    if (!srcBand) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    cv::Mat elevation = gis::core::gdalBandToMat(srcDs.get(), band);
    if (zFactor != 1.0) {
        elevation *= static_cast<float>(zFactor);
    }
    progress.onProgress(0.3);

    int hasNoData = 0;
    const double noDataValue = srcBand->GetNoDataValue(&hasNoData);
    cv::Mat noDataMask;
    if (hasNoData) {
        cv::compare(elevation, static_cast<float>(noDataValue * zFactor), noDataMask, cv::CMP_EQ);
    }

    auto buildFlowDirectionMat = [&]() {
        cv::Mat direction = cv::Mat::zeros(elevation.size(), CV_32F);
        for (int y = 1; y < elevation.rows - 1; ++y) {
            for (int x = 1; x < elevation.cols - 1; ++x) {
                if (hasNoData && noDataMask.at<unsigned char>(y, x) != 0) {
                    continue;
                }

                const float center = elevation.at<float>(y, x);
                float bestSlope = 0.0f;
                float bestCode = 0.0f;
                for (const auto& step : kD8Directions) {
                    const int nx = x + step.dx;
                    const int ny = y + step.dy;
                    if (hasNoData && noDataMask.at<unsigned char>(ny, nx) != 0) {
                        continue;
                    }

                    const float neighbor = elevation.at<float>(ny, nx);
                    const float slope = (center - neighbor) / step.distance;
                    if (slope > bestSlope) {
                        bestSlope = slope;
                        bestCode = step.code;
                    }
                }
                direction.at<float>(y, x) = bestCode;
            }
        }
        return direction;
    };

    cv::Mat result;
    if (action == "fill_sinks") {
        result = elevation.clone();
        cv::Mat kernel = cv::Mat::ones(3, 3, CV_8U);
        kernel.at<unsigned char>(1, 1) = 0;

        for (int iteration = 0; iteration < 256; ++iteration) {
            cv::Mat neighborMin;
            cv::erode(result, neighborMin, kernel, cv::Point(-1, -1), 1, cv::BORDER_REPLICATE);
            if (hasNoData) {
                neighborMin.setTo(result, noDataMask);
            }

            cv::Mat updated = cv::max(result, neighborMin);
            if (hasNoData) {
                updated.setTo(static_cast<float>(noDataValue), noDataMask);
            }

            cv::Mat changedMask;
            cv::compare(updated, result, changedMask, cv::CMP_NE);
            if (cv::countNonZero(changedMask) == 0) {
                result = updated;
                break;
            }

            result = updated;
            progress.onProgress(0.3 + 0.4 * static_cast<double>(iteration + 1) / 256.0);
        }
    } else if (action == "flow_direction") {
        result = buildFlowDirectionMat();
    } else if (action == "flow_accumulation") {
        const cv::Mat direction = buildFlowDirectionMat();
        cv::Mat indegree = cv::Mat::zeros(elevation.size(), CV_32S);
        result = cv::Mat::ones(elevation.size(), CV_32F);

        auto findDirectionStep = [](float code) -> const DirectionStep* {
            for (const auto& step : kD8Directions) {
                if (step.code == code) {
                    return &step;
                }
            }
            return static_cast<const DirectionStep*>(nullptr);
        };

        for (int y = 1; y < direction.rows - 1; ++y) {
            for (int x = 1; x < direction.cols - 1; ++x) {
                if (hasNoData && noDataMask.at<unsigned char>(y, x) != 0) {
                    result.at<float>(y, x) = 0.0f;
                    continue;
                }
                const auto* step = findDirectionStep(direction.at<float>(y, x));
                if (!step) {
                    continue;
                }
                indegree.at<int>(y + step->dy, x + step->dx) += 1;
            }
        }

        std::queue<cv::Point> queue;
        for (int y = 1; y < direction.rows - 1; ++y) {
            for (int x = 1; x < direction.cols - 1; ++x) {
                if (hasNoData && noDataMask.at<unsigned char>(y, x) != 0) {
                    continue;
                }
                if (indegree.at<int>(y, x) == 0) {
                    queue.emplace(x, y);
                }
            }
        }

        int processed = 0;
        while (!queue.empty()) {
            const cv::Point cell = queue.front();
            queue.pop();
            const auto* step = findDirectionStep(direction.at<float>(cell.y, cell.x));
            if (!step) {
                continue;
            }

            const cv::Point downstream(cell.x + step->dx, cell.y + step->dy);
            result.at<float>(downstream.y, downstream.x) += result.at<float>(cell.y, cell.x);
            const int nextDegree = --indegree.at<int>(downstream.y, downstream.x);
            if (nextDegree == 0) {
                queue.push(downstream);
            }

            ++processed;
            if ((processed % 256) == 0) {
                progress.onProgress(0.3 + 0.4 * static_cast<double>(processed) /
                    std::max(1, (direction.rows - 2) * (direction.cols - 2)));
            }
        }

        if (hasNoData) {
            result.setTo(static_cast<float>(noDataValue), noDataMask);
        }
    } else if (action == "stream_extract") {
        const auto accumulationResult = runHydrologyTerrainProcess(
            "flow_accumulation",
            "FlowAccumulation",
            input,
            output,
            band,
            zFactor,
            0.0,
            progress);
        if (!accumulationResult.success) {
            return accumulationResult;
        }

        auto accumulationDs = gis::core::openRaster(output, true);
        if (!accumulationDs) {
            return gis::framework::Result::fail("Cannot open accumulation output: " + output);
        }

        cv::Mat accumulation = gis::core::gdalBandToMat(accumulationDs.get(), 1);
        cv::Mat streamMask;
        cv::compare(accumulation, static_cast<float>(accumulationThreshold), streamMask, cv::CMP_GE);
        result = cv::Mat::zeros(accumulation.size(), CV_32F);
        result.setTo(1.0f, streamMask);
        if (hasNoData) {
            result.setTo(static_cast<float>(noDataValue), noDataMask);
        }
    } else if (action == "watershed") {
        const cv::Mat direction = buildFlowDirectionMat();
        cv::Mat indegree = cv::Mat::zeros(elevation.size(), CV_32S);
        cv::Mat basinIds = cv::Mat::zeros(elevation.size(), CV_32S);

        auto findDirectionStep = [](float code) -> const DirectionStep* {
            for (const auto& step : kD8Directions) {
                if (step.code == code) {
                    return &step;
                }
            }
            return static_cast<const DirectionStep*>(nullptr);
        };

        for (int y = 1; y < direction.rows - 1; ++y) {
            for (int x = 1; x < direction.cols - 1; ++x) {
                if (hasNoData && noDataMask.at<unsigned char>(y, x) != 0) {
                    continue;
                }
                const auto* step = findDirectionStep(direction.at<float>(y, x));
                if (!step) {
                    continue;
                }
                indegree.at<int>(y + step->dy, x + step->dx) += 1;
            }
        }

        int nextBasinId = 1;
        for (int y = 1; y < direction.rows - 1; ++y) {
            for (int x = 1; x < direction.cols - 1; ++x) {
                if (hasNoData && noDataMask.at<unsigned char>(y, x) != 0) {
                    continue;
                }

                const auto* step = findDirectionStep(direction.at<float>(y, x));
                bool isOutlet = !step;
                if (step) {
                    const int nx = x + step->dx;
                    const int ny = y + step->dy;
                    isOutlet = (nx <= 0 || nx >= direction.cols - 1 || ny <= 0 || ny >= direction.rows - 1);
                }
                if (isOutlet) {
                    basinIds.at<int>(y, x) = nextBasinId++;
                }
            }
        }

        std::vector<cv::Point> topoOrder;
        topoOrder.reserve((direction.rows - 2) * (direction.cols - 2));
        std::queue<cv::Point> queue;
        for (int y = 1; y < direction.rows - 1; ++y) {
            for (int x = 1; x < direction.cols - 1; ++x) {
                if (hasNoData && noDataMask.at<unsigned char>(y, x) != 0) {
                    continue;
                }
                if (indegree.at<int>(y, x) == 0) {
                    queue.emplace(x, y);
                }
            }
        }

        while (!queue.empty()) {
            const cv::Point cell = queue.front();
            queue.pop();
            topoOrder.push_back(cell);
            const auto* step = findDirectionStep(direction.at<float>(cell.y, cell.x));
            if (!step) {
                continue;
            }
            const cv::Point downstream(cell.x + step->dx, cell.y + step->dy);
            const int nextDegree = --indegree.at<int>(downstream.y, downstream.x);
            if (nextDegree == 0 &&
                downstream.x > 0 && downstream.x < direction.cols - 1 &&
                downstream.y > 0 && downstream.y < direction.rows - 1) {
                queue.push(downstream);
            }
        }

        for (auto it = topoOrder.rbegin(); it != topoOrder.rend(); ++it) {
            const cv::Point cell = *it;
            if (basinIds.at<int>(cell.y, cell.x) != 0) {
                continue;
            }
            const auto* step = findDirectionStep(direction.at<float>(cell.y, cell.x));
            if (!step) {
                continue;
            }
            const cv::Point downstream(cell.x + step->dx, cell.y + step->dy);
            basinIds.at<int>(cell.y, cell.x) = basinIds.at<int>(downstream.y, downstream.x);
        }

        basinIds.convertTo(result, CV_32F);
        if (hasNoData) {
            result.setTo(static_cast<float>(noDataValue), noDataMask);
        }
    } else {
        return gis::framework::Result::fail("Unknown hydrology terrain action: " + action);
    }

    progress.onProgress(0.8);
    progress.onMessage("Writing terrain output: " + output);
    gis::core::matToGdalTiff(result, srcDs.get(), output, band);
    progress.onProgress(1.0);

    auto terrainResult = gis::framework::Result::ok(successLabel + " completed", output);
    terrainResult.metadata["action"] = action;
    terrainResult.metadata["band"] = std::to_string(band);
    terrainResult.metadata["z_factor"] = std::to_string(zFactor);
    if (action == "stream_extract") {
        terrainResult.metadata["accum_threshold"] = std::to_string(accumulationThreshold);
    }
    return terrainResult;
}

std::vector<ProfilePoint> parseProfilePath(const std::string& text) {
    std::vector<ProfilePoint> points;
    std::stringstream ss(text);
    std::string token;
    while (std::getline(ss, token, ';')) {
        token = trim(token);
        if (token.empty()) {
            continue;
        }
        const auto comma = token.find(',');
        if (comma == std::string::npos) {
            throw std::runtime_error("profile_path 格式应为 x1,y1;x2,y2;...");
        }
        const std::string xText = trim(token.substr(0, comma));
        const std::string yText = trim(token.substr(comma + 1));
        points.push_back(ProfilePoint{std::stod(xText), std::stod(yText)});
    }
    if (points.size() < 2) {
        throw std::runtime_error("profile_path 至少需要两个点");
    }
    return points;
}

double segmentLength(const ProfilePoint& start, const ProfilePoint& end) {
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    return std::sqrt(dx * dx + dy * dy);
}

gis::framework::Result runProfileExtractProcess(
    const std::string& input,
    const std::string& output,
    int band,
    const std::string& profilePath,
    gis::core::ProgressReporter& progress) {

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");
    if (profilePath.empty()) return gis::framework::Result::fail("profile_path is required");

    progress.onProgress(0.1);
    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        return gis::framework::Result::fail("Cannot open DEM raster: " + input);
    }
    auto* srcBand = srcDs->GetRasterBand(band);
    if (!srcBand) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    double gt[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    if (srcDs->GetGeoTransform(gt) != CE_None) {
        return gis::framework::Result::fail("Cannot read raster geotransform");
    }
    double invGt[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    if (!GDALInvGeoTransform(gt, invGt)) {
        return gis::framework::Result::fail("Cannot invert raster geotransform");
    }

    std::vector<ProfilePoint> profilePoints;
    try {
        profilePoints = parseProfilePath(profilePath);
    } catch (const std::exception& ex) {
        return gis::framework::Result::fail(ex.what());
    }
    const double pixelStep = std::max(
        1e-9,
        std::min(
            std::hypot(gt[1], gt[2]),
            std::hypot(gt[4], gt[5])));

    const auto parent = std::filesystem::path(output).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream out(output, std::ios::binary);
    if (!out) {
        return gis::framework::Result::fail("Cannot write profile output: " + output);
    }
    out << "distance,x,y,elevation\n";

    double cumulativeDistance = 0.0;
    std::size_t sampleCount = 0;
    for (std::size_t segmentIndex = 0; segmentIndex + 1 < profilePoints.size(); ++segmentIndex) {
        const auto& start = profilePoints[segmentIndex];
        const auto& end = profilePoints[segmentIndex + 1];
        const double length = segmentLength(start, end);
        const int steps = std::max(1, static_cast<int>(std::ceil(length / pixelStep)));

        for (int step = 0; step <= steps; ++step) {
            if (segmentIndex > 0 && step == 0) {
                continue;
            }

            const double t = static_cast<double>(step) / steps;
            const double x = start.x + (end.x - start.x) * t;
            const double y = start.y + (end.y - start.y) * t;
            const double distance = cumulativeDistance + length * t;

            double pixel = 0.0;
            double line = 0.0;
            GDALApplyGeoTransform(invGt, x, y, &pixel, &line);
            const int px = static_cast<int>(std::lround(pixel));
            const int py = static_cast<int>(std::lround(line));
            if (px < 0 || px >= srcDs->GetRasterXSize() || py < 0 || py >= srcDs->GetRasterYSize()) {
                continue;
            }

            float elevation = 0.0f;
            if (srcBand->RasterIO(GF_Read, px, py, 1, 1, &elevation, 1, 1, GDT_Float32, 0, 0) != CE_None) {
                return gis::framework::Result::fail("Cannot sample profile raster");
            }

            out << distance << ',' << x << ',' << y << ',' << elevation << '\n';
            ++sampleCount;
        }

        cumulativeDistance += length;
        progress.onProgress(0.2 + 0.7 * static_cast<double>(segmentIndex + 1) / (profilePoints.size() - 1));
    }

    auto result = gis::framework::Result::ok("ProfileExtract completed", output);
    result.metadata["action"] = "profile_extract";
    result.metadata["band"] = std::to_string(band);
    result.metadata["sample_count"] = std::to_string(sampleCount);
    return result;
}

gis::framework::Result runViewshedProcess(
    const std::string& input,
    const std::string& output,
    int band,
    double observerX,
    double observerY,
    double observerHeight,
    double targetHeight,
    double maxDistance,
    gis::core::ProgressReporter& progress) {

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");
    if (observerHeight < 0.0) return gis::framework::Result::fail("observer_height must be greater than or equal to 0");
    if (targetHeight < 0.0) return gis::framework::Result::fail("target_height must be greater than or equal to 0");
    if (maxDistance < 0.0) return gis::framework::Result::fail("max_distance must be greater than or equal to 0");

    progress.onProgress(0.1);
    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        return gis::framework::Result::fail("Cannot open DEM raster: " + input);
    }

    auto* srcBand = srcDs->GetRasterBand(band);
    if (!srcBand) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    const auto parent = std::filesystem::path(output).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    progress.onMessage("Running terrain action: Viewshed");
    progress.onProgress(0.45);

    int usageError = FALSE;
    GDALDatasetH outHandle = GDALViewshedGenerate(
        GDALRasterBand::ToHandle(srcBand),
        "GTiff",
        output.c_str(),
        nullptr,
        observerX,
        observerY,
        observerHeight,
        targetHeight,
        255.0,
        0.0,
        0.0,
        0.0,
        0.0,
        GVM_Edge,
        maxDistance,
        nullptr,
        nullptr,
        GVOT_NORMAL,
        nullptr);

    if (!outHandle || usageError) {
        if (outHandle) {
            GDALClose(outHandle);
        }
        return gis::framework::Result::fail(
            "Viewshed processing failed: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALClose(outHandle);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("Viewshed completed", output);
    result.metadata["action"] = "viewshed";
    result.metadata["band"] = std::to_string(band);
    result.metadata["observer_x"] = std::to_string(observerX);
    result.metadata["observer_y"] = std::to_string(observerY);
    result.metadata["observer_height"] = std::to_string(observerHeight);
    result.metadata["target_height"] = std::to_string(targetHeight);
    result.metadata["max_distance"] = std::to_string(maxDistance);
    return result;
}

gis::framework::Result runCutFillProcess(
    const std::string& input,
    const std::string& reference,
    const std::string& output,
    int band,
    gis::core::ProgressReporter& progress) {

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (reference.empty()) return gis::framework::Result::fail("reference is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");

    progress.onProgress(0.1);
    auto inputDs = gis::core::openRaster(input, true);
    auto referenceDs = gis::core::openRaster(reference, true);
    if (!inputDs) {
        return gis::framework::Result::fail("Cannot open DEM raster: " + input);
    }
    if (!referenceDs) {
        return gis::framework::Result::fail("Cannot open reference raster: " + reference);
    }
    if (!inputDs->GetRasterBand(band)) {
        return gis::framework::Result::fail("Cannot get input band " + std::to_string(band));
    }
    if (!referenceDs->GetRasterBand(band)) {
        return gis::framework::Result::fail("Cannot get reference band " + std::to_string(band));
    }
    if (inputDs->GetRasterXSize() != referenceDs->GetRasterXSize() ||
        inputDs->GetRasterYSize() != referenceDs->GetRasterYSize()) {
        return gis::framework::Result::fail("input and reference raster size must match");
    }

    double inputGt[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    double referenceGt[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    if (inputDs->GetGeoTransform(inputGt) == CE_None &&
        referenceDs->GetGeoTransform(referenceGt) == CE_None) {
        for (int i = 0; i < 6; ++i) {
            if (std::abs(inputGt[i] - referenceGt[i]) > 1e-9) {
                return gis::framework::Result::fail("input and reference geotransform must match");
            }
        }
    }

    progress.onMessage("Reading terrain raster bands");
    cv::Mat inputMat = gis::core::gdalBandToMat(inputDs.get(), band);
    cv::Mat referenceMat = gis::core::gdalBandToMat(referenceDs.get(), band);
    progress.onProgress(0.45);

    cv::Mat diff = inputMat - referenceMat;

    const double pixelArea = std::abs(inputGt[1] * inputGt[5] - inputGt[2] * inputGt[4]);
    double fillVolume = 0.0;
    double cutVolume = 0.0;
    for (int y = 0; y < diff.rows; ++y) {
        const float* row = diff.ptr<float>(y);
        for (int x = 0; x < diff.cols; ++x) {
            const double value = row[x];
            if (value > 0.0) {
                fillVolume += value * pixelArea;
            } else if (value < 0.0) {
                cutVolume += (-value) * pixelArea;
            }
        }
    }

    const auto parent = std::filesystem::path(output).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    progress.onMessage("Writing terrain output: " + output);
    gis::core::matToGdalTiff(diff, inputDs.get(), output, band);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("CutFill completed", output);
    result.metadata["action"] = "cut_fill";
    result.metadata["band"] = std::to_string(band);
    result.metadata["fill_volume"] = std::to_string(fillVolume);
    result.metadata["cut_volume"] = std::to_string(cutVolume);
    result.metadata["net_volume"] = std::to_string(fillVolume - cutVolume);
    return result;
}

gis::framework::Result runReservoirVolumeProcess(
    const std::string& input,
    const std::string& output,
    int band,
    double waterLevel,
    gis::core::ProgressReporter& progress) {

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");

    progress.onProgress(0.1);
    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        return gis::framework::Result::fail("Cannot open DEM raster: " + input);
    }
    if (!srcDs->GetRasterBand(band)) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    progress.onMessage("Reading terrain raster band");
    cv::Mat elevation = gis::core::gdalBandToMat(srcDs.get(), band);
    progress.onProgress(0.45);

    cv::Mat depth = waterLevel - elevation;
    cv::max(depth, 0.0f, depth);

    double gt[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    srcDs->GetGeoTransform(gt);
    const double pixelArea = std::abs(gt[1] * gt[5] - gt[2] * gt[4]);
    double reservoirArea = 0.0;
    double reservoirVolume = 0.0;
    for (int y = 0; y < depth.rows; ++y) {
        const float* row = depth.ptr<float>(y);
        for (int x = 0; x < depth.cols; ++x) {
            const double value = row[x];
            if (value > 0.0) {
                reservoirArea += pixelArea;
                reservoirVolume += value * pixelArea;
            }
        }
    }

    const auto parent = std::filesystem::path(output).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    progress.onMessage("Writing terrain output: " + output);
    gis::core::matToGdalTiff(depth, srcDs.get(), output, band);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("ReservoirVolume completed", output);
    result.metadata["action"] = "reservoir_volume";
    result.metadata["band"] = std::to_string(band);
    result.metadata["water_level"] = std::to_string(waterLevel);
    result.metadata["reservoir_area"] = std::to_string(reservoirArea);
    result.metadata["reservoir_volume"] = std::to_string(reservoirVolume);
    return result;
}

} // namespace

std::vector<gis::framework::ParamSpec> TerrainPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "???", "?????????????",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"slope", "aspect", "hillshade", "tpi", "roughness", "fill_sinks", "flow_direction", "flow_accumulation", "stream_extract", "watershed", "profile_extract", "viewshed", "cut_fill", "reservoir_volume"}
        },
        gis::framework::ParamSpec{
            "input", "????", "?? DEM ????",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "reference", "????", "?? DEM ????",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "????", "??????",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "band", "????", "????????????? 1 ??",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "z_factor", "????", "???????????",
            gis::framework::ParamType::Double, false, double{1.0}
        },
        gis::framework::ParamSpec{
            "azimuth", "???", "??????????????",
            gis::framework::ParamType::Double, false, double{315.0}
        },
        gis::framework::ParamSpec{
            "altitude", "???", "??????????????",
            gis::framework::ParamType::Double, false, double{45.0}
        },
        gis::framework::ParamSpec{
            "accum_threshold", "????", "???????????????",
            gis::framework::ParamType::Double, false, double{10.0}
        },
        gis::framework::ParamSpec{
            "profile_path", "????", "????????? x1,y1;x2,y2;...",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "observer_x", "??? X", "??????? X",
            gis::framework::ParamType::Double, false, double{0.0}
        },
        gis::framework::ParamSpec{
            "observer_y", "??? Y", "??????? Y",
            gis::framework::ParamType::Double, false, double{0.0}
        },
        gis::framework::ParamSpec{
            "observer_height", "?????", "??????????",
            gis::framework::ParamType::Double, false, double{2.0}
        },
        gis::framework::ParamSpec{
            "target_height", "????", "?????????",
            gis::framework::ParamType::Double, false, double{0.0}
        },
        gis::framework::ParamSpec{
            "max_distance", "????", "?????????0 ?????",
            gis::framework::ParamType::Double, false, double{0.0}
        },
        gis::framework::ParamSpec{
            "water_level", "????", "?????????????",
            gis::framework::ParamType::Double, false, double{0.0}
        },
    };
}

gis::framework::Result TerrainPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const std::string action = gis::framework::getParam<std::string>(params, "action", "");
    if (action == "slope") return doSlope(params, progress);
    if (action == "aspect") return doAspect(params, progress);
    if (action == "hillshade") return doHillshade(params, progress);
    if (action == "tpi") return doTpi(params, progress);
    if (action == "roughness") return doRoughness(params, progress);
    if (action == "fill_sinks") return doFillSinks(params, progress);
    if (action == "flow_direction") return doFlowDirection(params, progress);
    if (action == "flow_accumulation") return doFlowAccumulation(params, progress);
    if (action == "stream_extract") return doStreamExtract(params, progress);
    if (action == "watershed") return doWatershed(params, progress);
    if (action == "profile_extract") return doProfileExtract(params, progress);
    if (action == "viewshed") return doViewshed(params, progress);
    if (action == "cut_fill") return doCutFill(params, progress);
    if (action == "reservoir_volume") return doReservoirVolume(params, progress);
    return gis::framework::Result::fail("Unknown action: " + action);
}

gis::framework::Result TerrainPlugin::doSlope(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runDemProcess(
        "slope",
        "Slope",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        315.0,
        45.0,
        progress);
}

gis::framework::Result TerrainPlugin::doAspect(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runDemProcess(
        "aspect",
        "Aspect",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        315.0,
        45.0,
        progress);
}

gis::framework::Result TerrainPlugin::doHillshade(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runDemProcess(
        "hillshade",
        "Hillshade",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        gis::framework::getParam<double>(params, "azimuth", 315.0),
        gis::framework::getParam<double>(params, "altitude", 45.0),
        progress);
}

gis::framework::Result TerrainPlugin::doTpi(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runLocalTerrainProcess(
        "tpi",
        "TPI",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        progress);
}

gis::framework::Result TerrainPlugin::doRoughness(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runLocalTerrainProcess(
        "roughness",
        "Roughness",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        progress);
}

gis::framework::Result TerrainPlugin::doFillSinks(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runHydrologyTerrainProcess(
        "fill_sinks",
        "FillSinks",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        0.0,
        progress);
}

gis::framework::Result TerrainPlugin::doStreamExtract(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runHydrologyTerrainProcess(
        "stream_extract",
        "StreamExtract",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        gis::framework::getParam<double>(params, "accum_threshold", 10.0),
        progress);
}

gis::framework::Result TerrainPlugin::doWatershed(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runHydrologyTerrainProcess(
        "watershed",
        "Watershed",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        0.0,
        progress);
}

gis::framework::Result TerrainPlugin::doFlowDirection(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runHydrologyTerrainProcess(
        "flow_direction",
        "FlowDirection",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        0.0,
        progress);
}

gis::framework::Result TerrainPlugin::doFlowAccumulation(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runHydrologyTerrainProcess(
        "flow_accumulation",
        "FlowAccumulation",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        0.0,
        progress);
}

gis::framework::Result TerrainPlugin::doProfileExtract(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runProfileExtractProcess(
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<std::string>(params, "profile_path", ""),
        progress);
}

gis::framework::Result TerrainPlugin::doViewshed(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runViewshedProcess(
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "observer_x", 0.0),
        gis::framework::getParam<double>(params, "observer_y", 0.0),
        gis::framework::getParam<double>(params, "observer_height", 2.0),
        gis::framework::getParam<double>(params, "target_height", 0.0),
        gis::framework::getParam<double>(params, "max_distance", 0.0),
        progress);
}

gis::framework::Result TerrainPlugin::doCutFill(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runCutFillProcess(
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "reference", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        progress);
}

gis::framework::Result TerrainPlugin::doReservoirVolume(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runReservoirVolumeProcess(
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "water_level", 0.0),
        progress);
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::TerrainPlugin)
