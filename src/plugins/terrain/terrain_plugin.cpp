#include "terrain_plugin.h"

#include <gis/core/gdal_wrapper.h>
#include <gis/core/opencv_wrapper.h>
#include <gis/core/plugin_utils.h>

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
#include <utility>   // priority-flood 的堆元素 std::pair<float, int>
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

struct SurfaceDerivatives {
    cv::Mat zx;
    cv::Mat zy;
    cv::Mat zxx;
    cv::Mat zyy;
    cv::Mat zxy;
};

struct ProfilePoint {
    double x = 0.0;
    double y = 0.0;
};

SurfaceDerivatives computeSurfaceDerivatives(const cv::Mat& elevation) {
    SurfaceDerivatives derivs;
    cv::Sobel(elevation, derivs.zx, CV_32F, 1, 0, 3, 1.0 / 8.0, 0.0, cv::BORDER_REPLICATE);
    cv::Sobel(elevation, derivs.zy, CV_32F, 0, 1, 3, 1.0 / 8.0, 0.0, cv::BORDER_REPLICATE);
    cv::Sobel(elevation, derivs.zxx, CV_32F, 2, 0, 3, 1.0, 0.0, cv::BORDER_REPLICATE);
    cv::Sobel(elevation, derivs.zyy, CV_32F, 0, 2, 3, 1.0, 0.0, cv::BORDER_REPLICATE);
    cv::Sobel(elevation, derivs.zxy, CV_32F, 1, 1, 3, 1.0 / 4.0, 0.0, cv::BORDER_REPLICATE);
    return derivs;
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

    progress.throwIfCancelled();

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
    progress.throwIfCancelled();
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
    auto meta = gis::core::buildPluginMetadata(input, srcDs.get(), "terrain." + std::string(processName));
    meta.algorithmParams["band"] = std::to_string(band);
    meta.algorithmParams["z_factor"] = std::to_string(zFactor);
    if (std::string(processName) == "hillshade") {
        meta.algorithmParams["azimuth"] = std::to_string(azimuth);
        meta.algorithmParams["altitude"] = std::to_string(altitude);
    }
    gis::core::writeProcessingMetadata(output, meta);
    progress.throwIfCancelled();
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

    progress.throwIfCancelled();

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
    progress.throwIfCancelled();
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
    } else if (action == "curvature") {
        cv::Laplacian(elevation, result, CV_32F, 3, 1.0, 0.0, cv::BORDER_REPLICATE);
    } else if (action == "profile_curvature" || action == "plan_curvature") {
        const auto derivs = computeSurfaceDerivatives(elevation);
        result = cv::Mat::zeros(elevation.size(), CV_32F);
        for (int y = 0; y < elevation.rows; ++y) {
            for (int x = 0; x < elevation.cols; ++x) {
                const float zx = derivs.zx.at<float>(y, x);
                const float zy = derivs.zy.at<float>(y, x);
                const float zxx = derivs.zxx.at<float>(y, x);
                const float zyy = derivs.zyy.at<float>(y, x);
                const float zxy = derivs.zxy.at<float>(y, x);
                const float slopeSq = zx * zx + zy * zy;
                if (slopeSq < 1e-10f) {
                    result.at<float>(y, x) = 0.0f;
                    continue;
                }

                if (action == "profile_curvature") {
                    const float numerator =
                        zxx * zx * zx + 2.0f * zxy * zx * zy + zyy * zy * zy;
                    const float denominator = slopeSq * std::pow(1.0f + slopeSq, 1.5f);
                    result.at<float>(y, x) = -numerator / (denominator + 1e-10f);
                } else {
                    const float numerator =
                        zxx * zy * zy - 2.0f * zxy * zx * zy + zyy * zx * zx;
                    const float denominator = std::pow(slopeSq, 1.5f);
                    result.at<float>(y, x) = -numerator / (denominator + 1e-10f);
                }
            }
        }
    } else if (action == "tri") {
        result = cv::Mat::zeros(elevation.size(), CV_32F);
        for (int y = 0; y < elevation.rows; ++y) {
            for (int x = 0; x < elevation.cols; ++x) {
                const float center = elevation.at<float>(y, x);
                float sumSquares = 0.0f;
                int neighborCount = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        const int ny = std::clamp(y + dy, 0, elevation.rows - 1);
                        const int nx = std::clamp(x + dx, 0, elevation.cols - 1);
                        const float diff = elevation.at<float>(ny, nx) - center;
                        sumSquares += diff * diff;
                        ++neighborCount;
                    }
                }
                result.at<float>(y, x) =
                    neighborCount > 0 ? std::sqrt(sumSquares / static_cast<float>(neighborCount)) : 0.0f;
            }
        }
    } else if (action == "roughness") {
        cv::Mat neighborhoodMax;
        cv::Mat neighborhoodMin;
        cv::dilate(elevation, neighborhoodMax, cv::Mat(), cv::Point(-1, -1), 1, cv::BORDER_REPLICATE);
        cv::erode(elevation, neighborhoodMin, cv::Mat(), cv::Point(-1, -1), 1, cv::BORDER_REPLICATE);
        result = neighborhoodMax - neighborhoodMin;
    } else {
        return gis::framework::Result::fail("Unknown local terrain action: " + action);
    }
    progress.throwIfCancelled();
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
    auto meta = gis::core::buildPluginMetadata(input, srcDs.get(), "terrain." + action);
    meta.algorithmParams["band"] = std::to_string(band);
    meta.algorithmParams["z_factor"] = std::to_string(zFactor);
    meta.algorithmParams["window_size"] = "3";
    gis::core::writeProcessingMetadata(output, meta);
    progress.throwIfCancelled();
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
    gis::core::ProgressReporter& progress,
    // WHY 放在 progress 之后并带默认值：只有 fill_sinks 读它，默认 0.0 让
    // flow_direction / flow_accumulation / stream_extract / watershed 这 4 个调用点
    // 与 stream_extract 的自递归调用一行都不必改（R-T11-4 的"其它 action 不动"）。
    double epsilon = 0.0) {

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");
    if (zFactor <= 0.0) return gis::framework::Result::fail("z_factor must be greater than 0");
    if ((action == "stream_extract") && accumulationThreshold <= 0.0) {
        return gis::framework::Result::fail("accum_threshold must be greater than 0");
    }

    progress.throwIfCancelled();

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
    progress.throwIfCancelled();
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
        // Priority-flood（Wang & Liu 2006）。
        // WHY 换掉"重复若干轮：每像元取邻域极值再 max"的不动点迭代：
        // (a) 平底洼地填不平——坑内等高像元的邻域极值就是坑自己的值，max 之后纹丝不动，
        //     只有单像元坑会被一次抬走；
        // (b) 嵌套洼地要一层一层往外爬，固定轮次上限会把还没爬完的坑静默留下，
        //     输出仍带封闭洼地，直接污染下游 flow_direction / flow_accumulation / watershed。
        // priority-flood 按"每个像元沿非降路径可达数据集出口的最低水面"一次求解，
        // 平底与嵌套都是它的自然结果，且没有迭代上限这种隐式截断。
        result = elevation.clone();
        const int rows = result.rows;
        const int cols = result.cols;
        std::vector<uint8_t> inQueue(static_cast<size_t>(rows * cols), 0);
        // 堆元素为 (出水面高程, 线性索引)。
        // WHY 带索引做第二关键字：std::pair 的字典序让同高程像元按索引定序，
        // 于是同一输入的弹出序列唯一 ⇒ 结果可复现，不受堆的内部排列影响。
        std::vector<std::pair<float, int>> heap;
        heap.reserve(static_cast<size_t>(std::max(1, rows * cols)));

        // WHY 必须是最小堆：priority-flood 的定义就是"每次弹出当前最低的已入队像元"，
        // 弹出顺序即算法本身——只有从最低水面往外扩，被弹出像元的 top.first 才是它的最低出水面。
        // std::make_heap/pop_heap 不带比较子时是**最大堆**（从高到低弹出），那等于用
        // "到出口的最高路径"填洼 ⇒ 系统性过度抬升。判别器见用例
        // PluginTest.FillSinksFillsToLowestPourPathNotHighest（洼地与边缘之间有一条更低的沟）。
        // 注意：下面 make_heap / pop_heap / push_heap 三处必须共用这一个比较子，漏一处就是坏堆。
        const auto lowestFirst = [](const std::pair<float, int>& a,
                                    const std::pair<float, int>& b) { return a > b; };

        auto linear = [cols](int y, int x) { return y * cols + x; };
        // WHY NoData 当不可穿越的障壁，而不是出海口：NoData 的存储值是哨兵（如 -9999），
        // 它不代表地势低。若把 NoData 像元也塞进"边界出海口"，-9999 就成了整幅 DEM 的假出水路径，
        // "只抬不降"的判据（value < 出水面）对任何真实像元都不成立 ⇒ 所有洼地一律不抬。
        // WHY 还要挡掉 NaN：std::isfinite 之前 NaN 像元照常入队，而 lowestFirst（a > b）对含 NaN
        // 的元素违反严格弱序 ⇒ make_heap/pop_heap 是形式上的 UB（std::priority_queue 的前置条件
        // 被破坏，结果不受保证）。更实在的危害是"从 NaN 处漏水"：NaN 被弹出后 top.first 是 NaN，
        // 所有 "value < 出水面" 对 NaN 恒假 ⇒ 它的邻居被标记成已处理却一点没抬，洼地静默不填，
        // 下游 flow_direction 于是带着封闭汇跑。旧实现（cv::erode + max）对 NaN 走的是确定性的
        // max 语义，没有这个 UB，也没有这个漏；把 NaN 折进障壁判据是与本分支 NoData 同一套语义，
        // 也是最小改动。NaN 像元自己不写值：输出保留 NaN，下游按无效像元处理，我们不为它编高程。
        auto validCell = [&](int y, int x) {
            return y >= 0 && x >= 0 && y < rows && x < cols &&
                   (!hasNoData || noDataMask.at<uint8_t>(y, x) == 0) &&
                   std::isfinite(result.at<float>(y, x));
        };

        // 1) 数据集边缘作为出海口入队（inQueue 判重覆盖 rows==1 或 cols==1 的退化形状）
        for (int x = 0; x < cols; ++x) {
            for (int y : {0, rows - 1}) {
                if (!validCell(y, x) || inQueue[static_cast<size_t>(linear(y, x))]) continue;
                inQueue[static_cast<size_t>(linear(y, x))] = 1;
                heap.emplace_back(result.at<float>(y, x), linear(y, x));
            }
        }
        for (int y = 0; y < rows; ++y) {
            for (int x : {0, cols - 1}) {
                if (!validCell(y, x) || inQueue[static_cast<size_t>(linear(y, x))]) continue;
                inQueue[static_cast<size_t>(linear(y, x))] = 1;
                heap.emplace_back(result.at<float>(y, x), linear(y, x));
            }
        }
        std::make_heap(heap.begin(), heap.end(), lowestFirst);

        // 2) 从最低出水面开始往外淹没，把洼地抬到它自己的出水面高程
        static const int kDx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
        static const int kDy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        int processed = 0;
        while (!heap.empty()) {
            std::pop_heap(heap.begin(), heap.end(), lowestFirst);
            const auto top = heap.back();
            heap.pop_back();
            const int cy = top.second / cols;
            const int cx = top.second % cols;

            for (int d = 0; d < 8; ++d) {
                const int ny = cy + kDy[d];
                const int nx = cx + kDx[d];
                if (!validCell(ny, nx)) continue;
                const int idx = linear(ny, nx);
                if (inQueue[static_cast<size_t>(idx)]) continue;
                inQueue[static_cast<size_t>(idx)] = 1;

                float& value = result.at<float>(ny, nx);
                // WHY 判据是严格小于（value < top.first），不是 value <= top.first：
                // top.first 是"含 ε 之后"的水面。填洼只该把**低于**水面的像元抬到水面；恰好
                // 等高的像元没有"被填洼抬过"（抬到水面=原地不动），给它们加 ε 就不是洼地内的
                // 微坡度而是给背景地形加高。平底背景里每个像元都恰与其父像元等高，于是 ε 逐圈
                // 累积成锥体，锥体再反过来把洼地自己的出水面顶高。<= 版的实测：101x101 均匀 5.0
                // 的平地 ε=10 时 9801 个像元被改动、中央 +500 m；9x9 均匀 10.0 的单像元坑中心
                // 从 10.0 变 50.0。这些像元不是洼地 ⇒ 产品高程错，而参数描述写的是 inside filled
                // depressions ⇒ 描述也谎。判据收窄成严格小于后两处都复现不了（见 snap 对照表）。
                // 不能进一步收窄成"只对 ε=0 那趟真抬过的像元施 ε"（另开一张 filled 掩膜）：
                // 那样恰好落在水面高度上的鞍部/岛状像元保持原值，而它四周的水面已被 ε 抬高 ⇒
                // 重新出现封闭汇。实测 200x200 随机 DEM：ε=0.5 新增 18085 个封闭像元、ε=10 新增
                // 26945 个。严格小于因此是同时满足"背景不动"与"仍无封闭汇"两端的唯一判据：
                // 高于水面的背景一格不动，且每个像元仍沿非降路径可达出海口（推入堆的就是写回的值）。
                // 洼地的平底照样爬得出来——平底像元的原始值严格低于它的出水面，走的正是这条分支。
                // 残差（参数描述按这个口径写）：原始值恰好等于 ε=0 水面、却被抬高后的水面漫过的像元
                // （湖面上的岛/鞍部）仍然低于弹出键，于是也拿到 ε——不放它就得不到非降出口。实测 29 幅
                // 比对 DEM 上 ε∈{0.5,10} 时这类像元 619 个（改前 3007 个），且 619/619 都能经同类像元
                // 连到一个真被抬过的像元上 ⇒ 都在这片水域里，不在背景上。
                // ε=0 时这个分支只做一次 "value = top.first" 赋值、没有任何算术，且 value ==
                // top.first 时赋值与否同值 ⇒ 默认路径与 <= 版逐像元相同，也仍然零精度损失。
                // 反面的教训（R-T11-2）：条件也不能写回 "value > top.first 才施 ε"——刚被抬平的
                // 像元此刻恰好等于出水面，那样洼地一个 ε 都拿不到，反倒是真实地形被抬高。
                if (value < top.first) {
                    value = (epsilon > 0.0) ? static_cast<float>(top.first + epsilon) : top.first;
                }
                // 推进堆的是抬**后**的值 ⇒ 它成为子像元的出水面，抬升目标因此逐层传递：
                // 嵌套洼地不会只填到最外一层，ε 也因此在洼地内沿淹没方向单调累积（水仍能往外排）。
                heap.emplace_back(value, idx);
                std::push_heap(heap.begin(), heap.end(), lowestFirst);
            }
            if ((++processed % 4096) == 0) {
                progress.throwIfCancelled();
                progress.onProgress(0.3 + 0.4 * static_cast<double>(processed) /
                                           static_cast<double>(std::max(1, rows * cols)));
            }
        }
        if (hasNoData) {
            result.setTo(static_cast<float>(noDataValue), noDataMask);
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
                progress.throwIfCancelled();
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
        // WHY 两边各自先夹到 >=0 再转 size_t 相乘（照抄本文件 :410 由 Task 11 修好的形态）：
        // 原来的 `(direction.rows - 2) * (direction.cols - 2)` 是 **int** 乘积，
        //   · rows<2 而 cols>2（或反之）时乘积为负 ⇒ 隐式转成巨大 size_t ⇒ reserve 抛
        //     std::length_error（"vector too long"）。这条**已实测**：1×6 与 6×1 两种几何都在
        //     用例 #265 里，且 runHydrologyTerrainProcess 到此处之前只有 input/output/band/z_factor
        //     四项校验、没有任何尺寸闸门 ⇒ 异常直接穿出 execute。
        //   · 另一边约 46341×46341 以上的超大栅格会先溢出 int 再变负，同一条修正一并挡住
        //     （那条几何要 10GB 级内存，本仓测不了，只有算式：46341² = 2,147,488,281 > INT_MAX）。
        // 注意 2×N / N×2 的乘积是 **0**，正负都对不上 ⇒ 它不是这条缺陷的用例，别拿它当判别器。
        topoOrder.reserve(static_cast<size_t>(std::max(0, direction.rows - 2)) *
                          static_cast<size_t>(std::max(0, direction.cols - 2)));
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

    progress.throwIfCancelled();

    progress.onProgress(0.8);
    progress.onMessage("Writing terrain output: " + output);
    gis::core::matToGdalTiff(result, srcDs.get(), output, band);
    auto meta = gis::core::buildPluginMetadata(input, srcDs.get(), "terrain." + action);
    meta.algorithmParams["band"] = std::to_string(band);
    meta.algorithmParams["z_factor"] = std::to_string(zFactor);
    if (action == "stream_extract") {
        meta.algorithmParams["accum_threshold"] = std::to_string(accumulationThreshold);
    }
    gis::core::writeProcessingMetadata(output, meta);
    progress.throwIfCancelled();
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
        token = gis::core::trimString(token);
        if (token.empty()) {
            continue;
        }
        const auto comma = token.find(',');
        if (comma == std::string::npos) {
            throw std::runtime_error("profile_path requires at least two points");
        }
        const std::string xText = gis::core::trimString(token.substr(0, comma));
        const std::string yText = gis::core::trimString(token.substr(comma + 1));
        points.push_back(ProfilePoint{std::stod(xText), std::stod(yText)});
    }
    if (points.size() < 2) {
        throw std::runtime_error("profile_path requires at least two points");
    }
    return points;
}

std::vector<ProfilePoint> parseObserverPoints(const std::string& text) {
    std::vector<ProfilePoint> points;
    std::stringstream ss(text);
    std::string token;
    while (std::getline(ss, token, ';')) {
        token = gis::core::trimString(token);
        if (token.empty()) {
            continue;
        }
        const auto comma = token.find(',');
        if (comma == std::string::npos) {
            throw std::runtime_error("observer_points requires at least one point");
        }
        const std::string xText = gis::core::trimString(token.substr(0, comma));
        const std::string yText = gis::core::trimString(token.substr(comma + 1));
        points.push_back(ProfilePoint{std::stod(xText), std::stod(yText)});
    }
    if (points.empty()) {
        throw std::runtime_error("observer_points requires at least one point");
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

    progress.throwIfCancelled();

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
        progress.throwIfCancelled();
        progress.onProgress(0.2 + 0.7 * static_cast<double>(segmentIndex + 1) / (profilePoints.size() - 1));
    }

    auto result = gis::framework::Result::ok("ProfileExtract completed", output);
    result.metadata["action"] = "profile_extract";
    result.metadata["band"] = std::to_string(band);
    result.metadata["sample_count"] = std::to_string(sampleCount);
    return result;
}

// 非投影 CRS 的告警串。值是**契约的一部分**（spec Produces 定的就是这串英文），
// R-T12-2 要求用例断的是它本身而不是"crs_warning 键存在"，所以这里抽成常量、
// 两处（metadata 与 result.message）共用同一份字面量，避免两侧文案漂移。
constexpr const char* kViewshedProjectedCrsHint = "observer analysis expects projected CRS";

gis::framework::Result runViewshedProcess(
    const std::string& input,
    const std::string& output,
    int band,
    double observerX,
    double observerY,
    double observerHeight,
    double targetHeight,
    double maxDistance,
    double curvatureCoeff,
    gis::core::ProgressReporter& progress) {

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");
    if (observerHeight < 0.0) return gis::framework::Result::fail("observer_height must be greater than or equal to 0");
    if (targetHeight < 0.0) return gis::framework::Result::fail("target_height must be greater than or equal to 0");
    if (maxDistance < 0.0) return gis::framework::Result::fail("max_distance must be greater than or equal to 0");

    progress.throwIfCancelled();

    progress.onProgress(0.1);
    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        return gis::framework::Result::fail("Cannot open DEM raster: " + input);
    }

    auto* srcBand = srcDs->GetRasterBand(band);
    if (!srcBand) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    // 曲率/大气折射修正把距离换算成高程差（Δz ≈ coeff·d²/2R），R 用的是米；CRS 是地理坐标系时
    // 距离是"度"，Δz 会小到恒为 0，修正静默失效，而用户以为开了修正 ⇒ 非投影时必须告警。
    // WHY 只看 GetSpatialRef() 而不看单位：IsProjected() 就是 GDAL 对"线性单位是否为米"的判据，
    // 空 SRS（无 CRS）同样落进告警分支。
    std::string crsWarning;
    if (auto* srs = srcDs->GetSpatialRef(); !srs || !srs->IsProjected()) {
        crsWarning = kViewshedProjectedCrsHint;
    }

    const auto parent = std::filesystem::path(output).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    progress.onMessage("Running terrain action: Viewshed");
    progress.throwIfCancelled();
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
        // 第 13 个实参是 dfCurvCoeff（gdal_alg.h:349-357），改前这里硬编码 0.0 = 关闭
        // 地球曲率与大气折射修正。
        curvatureCoeff,
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
    auto meta = gis::core::buildPluginMetadata(input, srcDs.get(), "terrain.viewshed");
    meta.algorithmParams["band"] = std::to_string(band);
    meta.algorithmParams["observer_x"] = std::to_string(observerX);
    meta.algorithmParams["observer_y"] = std::to_string(observerY);
    meta.algorithmParams["observer_height"] = std::to_string(observerHeight);
    meta.algorithmParams["target_height"] = std::to_string(targetHeight);
    meta.algorithmParams["max_distance"] = std::to_string(maxDistance);
    meta.algorithmParams["curvature_coeff"] = std::to_string(curvatureCoeff);
    gis::core::writeProcessingMetadata(output, meta);
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    // 成功也要把 CRS 问题说出去：message 是 GUI 结果区唯一可见的文案，metadata 供程序化消费，
    // 两侧共用 kViewshedProjectedCrsHint 这一份字面量（Produces 的"追加同样提示"）。
    auto result = gis::framework::Result::ok("Viewshed completed", output);
    if (!crsWarning.empty()) {
        result.message += "; " + crsWarning;
        result.metadata["crs_warning"] = crsWarning;
    }
    result.metadata["action"] = "viewshed";
    result.metadata["band"] = std::to_string(band);
    result.metadata["observer_x"] = std::to_string(observerX);
    result.metadata["observer_y"] = std::to_string(observerY);
    result.metadata["observer_height"] = std::to_string(observerHeight);
    result.metadata["target_height"] = std::to_string(targetHeight);
    result.metadata["max_distance"] = std::to_string(maxDistance);
    result.metadata["curvature_coeff"] = std::to_string(curvatureCoeff);
    return result;
}

// 把累积出来的 0/255 视域掩膜写成**单波段 GDT_Byte** GeoTIFF。
// WHY 不走 gis::core::matToGdalTiff / matsToGdalTiff（简报 Step 3 点名的那两个函数）：
// 两者都把输出栅格的类型**硬编码成 GDT_Float32**（src/core/src/opencv_wrapper.cpp:43 与 :90），
// matsToGdalTiff 还在写出前把每个 Mat convertTo(CV_32F)（同文件 :106-111）⇒ 传 CV_8U 进去得到的仍是
// 一个声明为 Float32 的掩膜，R-T12-3 要的"两个视域动作输出类型都是 Byte"用那条路修不出来。
// 改这两个共享函数会连带 classification/georef/matching/processing/raster_manage 等 10+ 处
// 调用点的产品类型一起变，远超本任务范围，所以在这里本地写出。
// WHY 不声明 NoData：opencv_wrapper.cpp:51-57 会把源 DEM 波段声明的 NoData **无条件**抄到输出波段，
// 而这张掩膜只有 0（不可见/超出距离）与 255（可见）两种值、没有无效像元 ⇒ 抄过来就是给产品打一个
// "把不可见像元当 nodata 渲染"的声明。按简报要求改成"仅当输出真的存在无效像元才 SetNoDataValue"，
// 本函数的输出不存在无效像元，所以一律不声明。
// 挂账 N43（批次 A 复审 F-2 的验收语，不是架构选择）：本函数是绕过 opencv_wrapper 的**临时绕行**，
// 批次 B 的交付物是"新增按 dtype 参数化的 core 写出 API 并删除本 helper"，
// **不是**把这个本地写出器扶正成惯例、也不是留给后续任务当先例。
void writeViewshedMaskAsByte(const cv::Mat& merged, GDALDataset* srcDs, const std::string& output) {
    cv::Mat mask;
    merged.convertTo(mask, CV_8U);   // 饱和转换：掩膜只有 0/255，没有会被压成 0 的负值
    double gt[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    srcDs->GetGeoTransform(gt);
    const std::string proj = gis::core::getSRSWKT(srcDs);
    auto dstDs = gis::core::createRaster(output, mask.cols, mask.rows, 1, GDT_Byte);
    dstDs->SetGeoTransform(gt);
    if (!proj.empty()) {
        dstDs->SetProjection(proj.c_str());
    }
    auto* dstBand = dstDs->GetRasterBand(1);
    dstBand->RasterIO(GF_Write, 0, 0, mask.cols, mask.rows,
                      const_cast<uint8_t*>(mask.ptr<uint8_t>()), mask.cols, mask.rows,
                      GDT_Byte, 0, 0);
    dstBand->FlushCache();
}

gis::framework::Result runViewshedMultiProcess(
    const std::string& input,
    const std::string& output,
    int band,
    const std::string& observerPointsText,
    double observerHeight,
    double targetHeight,
    double maxDistance,
    gis::core::ProgressReporter& progress) {

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (band <= 0) return gis::framework::Result::fail("band must be greater than 0");
    if (observerPointsText.empty()) return gis::framework::Result::fail("observer_points is required");
    if (observerHeight < 0.0) return gis::framework::Result::fail("observer_height must be greater than or equal to 0");
    if (targetHeight < 0.0) return gis::framework::Result::fail("target_height must be greater than or equal to 0");
    if (maxDistance < 0.0) return gis::framework::Result::fail("max_distance must be greater than or equal to 0");

    std::vector<ProfilePoint> observerPoints;
    try {
        observerPoints = parseObserverPoints(observerPointsText);
    } catch (const std::exception& ex) {
        return gis::framework::Result::fail(ex.what());
    }

    progress.throwIfCancelled();

    progress.onProgress(0.1);
    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        return gis::framework::Result::fail("Cannot open DEM raster: " + input);
    }

    auto* srcBand = srcDs->GetRasterBand(band);
    if (!srcBand) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    cv::Mat merged(srcDs->GetRasterYSize(), srcDs->GetRasterXSize(), CV_32F, cv::Scalar(0.0f));
    progress.onMessage("Running terrain action: MultiViewshed");
    for (std::size_t index = 0; index < observerPoints.size(); ++index) {
        const auto& point = observerPoints[index];
        int usageError = FALSE;
        GDALDatasetH outHandle = GDALViewshedGenerate(
            GDALRasterBand::ToHandle(srcBand),
            "MEM",
            "",
            nullptr,
            point.x,
            point.y,
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
                "Multi viewshed processing failed: " + std::string(CPLGetLastErrorMsg()));
        }

        gis::core::GdalDatasetPtr viewshedDs(reinterpret_cast<GDALDataset*>(outHandle));
        cv::Mat viewshedMat = gis::core::gdalBandToMat(viewshedDs.get(), 1);
        cv::max(merged, viewshedMat, merged);
        progress.throwIfCancelled();
        progress.onProgress(0.2 + 0.7 * static_cast<double>(index + 1) / observerPoints.size());
    }

    const auto parent = std::filesystem::path(output).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    progress.onMessage("Writing terrain output: " + output);
    // R-T12-3：与单点视域（GDALViewshedGenerate 直接写 GDT_Byte）统一成同一种输出类型，
    // 并且不再把源 DEM 的 NoData 继承进这张 0/255 掩膜。
    writeViewshedMaskAsByte(merged, srcDs.get(), output);
    auto meta = gis::core::buildPluginMetadata(input, srcDs.get(), "terrain.viewshed_multi");
    meta.algorithmParams["band"] = std::to_string(band);
    meta.algorithmParams["observer_count"] = std::to_string(observerPoints.size());
    meta.algorithmParams["observer_height"] = std::to_string(observerHeight);
    meta.algorithmParams["target_height"] = std::to_string(targetHeight);
    meta.algorithmParams["max_distance"] = std::to_string(maxDistance);
    gis::core::writeProcessingMetadata(output, meta);
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("MultiViewshed completed", output);
    result.metadata["action"] = "viewshed_multi";
    result.metadata["band"] = std::to_string(band);
    result.metadata["observer_count"] = std::to_string(observerPoints.size());
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

    progress.throwIfCancelled();

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
    // WHY 分开取、分开判，而不是原来的 `if (输入取成功 && 参考取成功) { 一致性校验 }`：
    // 那个写法让"任一边取不到地理参考"整块短路跳过——既不报错也不校验，于是下面
    // `const double pixelArea = std::abs(inputGt[1]*inputGt[5] - inputGt[2]*inputGt[4])` 那行
    // 改用 GDAL 在失败时**同样会写进数组**的默认单位变换（实测 |1×(-1)|=1.0，
    // 不是恒 0），体积按凭空单位出数还回 success：改前 #263 形状 a 实测 `success=true`、
    // `fill_volume=200.000000`、产物 `t19_cf_nogt_output_a.tif` 照样存在（夹具 pixelSize=1.0 ⇒ gt={116,1,0,40,0,-1}）。
    // 像元面积在缺地理参考时无从得知，只能显式失败；两条守卫分别报输入侧/参考侧，
    // 顺序固定为先输入后参考，用例 #263 的两种形状据此钉住归属。
    if (inputDs->GetGeoTransform(inputGt) != CE_None) {
        return gis::framework::Result::fail("input raster geotransform is missing");
    }
    if (referenceDs->GetGeoTransform(referenceGt) != CE_None) {
        return gis::framework::Result::fail("reference raster geotransform is missing");
    }
    for (int i = 0; i < 6; ++i) {
        if (std::abs(inputGt[i] - referenceGt[i]) > 1e-9) {
            return gis::framework::Result::fail("input and reference geotransform must match");
        }
    }

    progress.onMessage("Reading terrain raster bands");
    cv::Mat inputMat = gis::core::gdalBandToMat(inputDs.get(), band);
    cv::Mat referenceMat = gis::core::gdalBandToMat(referenceDs.get(), band);
    progress.throwIfCancelled();
    progress.onProgress(0.45);

    cv::Mat diff = inputMat - referenceMat;

    // 双侧 NoData 掩膜：哨兵值（-9999 之类）不是高程，进 `inputMat - referenceMat` 会得到
    // ±10000 量级的假高差直接进体积累加。两侧各是一条**独立**守卫：只查输入侧则参考侧哨兵漏进来，
    // 只查参考侧则输入侧漏进来，用例 #261 把 NoData 分侧放（A 区仅参考侧、B 区仅输入侧）各杀一条。
    // WHY 判据用 GetNoDataValue 的 found 出参而不是值本身：没声明 NoData 的栅格返回 0.0，
    // 拿它当哨兵比较会把"真的等于 0 的高程"误判成无效。
    // 比较对象先转 float：gdalBandToMat 恒为 CV_32F（src/core/src/opencv_wrapper.cpp:25-27 声明的就是
    // `cv::Mat mat(height, width, CV_32F)` 并按 GDT_Float32 读回），声明值 double -9999.0 转 float 后与读回的位型一致，逐位比较可靠。
    int inputHasNoData = 0;
    const double inputNoData = inputDs->GetRasterBand(band)->GetNoDataValue(&inputHasNoData);
    int referenceHasNoData = 0;
    const double referenceNoData = referenceDs->GetRasterBand(band)->GetNoDataValue(&referenceHasNoData);

    cv::Mat valid = cv::Mat::ones(diff.rows, diff.cols, CV_8U);
    if (inputHasNoData) {
        cv::compare(inputMat, static_cast<float>(inputNoData), valid, cv::CMP_NE);
    }
    if (referenceHasNoData) {
        cv::Mat referenceValid;
        cv::compare(referenceMat, static_cast<float>(referenceNoData), referenceValid, cv::CMP_NE);
        cv::bitwise_and(valid, referenceValid, valid);
    }
    // 无效像元的输出值：输入侧声明了 NoData 就沿用它（matToGdalTiff 会把输入侧的声明抄到输出波段，
    // src/core/src/opencv_wrapper.cpp:51-57，于是这一格在 GIS 软件里真的是"无数据"而不是一个数）；
    // 输入侧没声明时退回 0 并靠 invalid_pixels 计数把损失显式化。
    const float invalidOutputValue = inputHasNoData ? static_cast<float>(inputNoData) : 0.0f;

    const double pixelArea = std::abs(inputGt[1] * inputGt[5] - inputGt[2] * inputGt[4]);
    double fillVolume = 0.0;
    double cutVolume = 0.0;
    int invalidPixels = 0;
    for (int y = 0; y < diff.rows; ++y) {
        const uchar* validRow = valid.ptr<uchar>(y);
        float* diffRow = diff.ptr<float>(y);
        for (int x = 0; x < diff.cols; ++x) {
            if (validRow[x] == 0) {
                diffRow[x] = invalidOutputValue;
                ++invalidPixels;
                continue;
            }
            const double value = diffRow[x];
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
    auto meta = gis::core::buildPluginMetadata(input, inputDs.get(), "terrain.cut_fill");
    meta.algorithmParams["band"] = std::to_string(band);
    meta.algorithmParams["fill_volume"] = std::to_string(fillVolume);
    meta.algorithmParams["cut_volume"] = std::to_string(cutVolume);
    meta.algorithmParams["net_volume"] = std::to_string(fillVolume - cutVolume);
    // invalid_pixels：被排除的像元数。没有这个计数，"30 像元有效 + 70 像元被排除"与
    // "100 像元全都算过但恰好都是 0 差值"输出同一个体积，缺陷又会换个形态静默。
    meta.algorithmParams["invalid_pixels"] = std::to_string(invalidPixels);
    gis::core::writeProcessingMetadata(output, meta);
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("CutFill completed", output);
    result.metadata["action"] = "cut_fill";
    result.metadata["band"] = std::to_string(band);
    result.metadata["fill_volume"] = std::to_string(fillVolume);
    result.metadata["cut_volume"] = std::to_string(cutVolume);
    result.metadata["net_volume"] = std::to_string(fillVolume - cutVolume);
    result.metadata["invalid_pixels"] = std::to_string(invalidPixels);
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

    progress.throwIfCancelled();

    progress.onProgress(0.1);
    auto srcDs = gis::core::openRaster(input, true);
    if (!srcDs) {
        return gis::framework::Result::fail("Cannot open DEM raster: " + input);
    }
    if (!srcDs->GetRasterBand(band)) {
        return gis::framework::Result::fail("Cannot get band " + std::to_string(band));
    }

    // WHY 地理参考在这里就判掉、放在读波段之前：改前这一行是 `srcDs->GetGeoTransform(gt);`，
    // 返回值**整个丢弃**，缺参考的 DEM 于是按 GDAL 失败时填进数组的默认单位变换算出
    // pixelArea=1.0 的面积/体积并报 success（改前 #264 实测静默值 `reservoir_volume=300.000000` 且产物存在）。
    // 与 cut_fill 侧同口径：拿不到像元面积就显式失败，且失败路径不留产物。
    double gt[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    if (srcDs->GetGeoTransform(gt) != CE_None) {
        return gis::framework::Result::fail("input raster geotransform is missing");
    }

    progress.onMessage("Reading terrain raster band");
    cv::Mat elevation = gis::core::gdalBandToMat(srcDs.get(), band);
    progress.throwIfCancelled();
    progress.onProgress(0.45);

    cv::Mat depth = waterLevel - elevation;
    cv::max(depth, 0.0f, depth);

    // NoData 掩膜：哨兵高程会被 `waterLevel - elevation` 变成 ±10000 量级的"假水深"，
    // 而且恒为正 ⇒ 同时污染面积（每个哨兵像元都算成水面）与体积（用例 #262 实测 500750）。
    // 判据同样取 GetNoDataValue 的 found 出参而不是值本身：未声明 NoData 的栅格返回 0.0，
    // 拿 0.0 当哨兵会把真实高程 0 的像元误删。
    int hasNoData = 0;
    const double noDataValue = srcDs->GetRasterBand(band)->GetNoDataValue(&hasNoData);
    cv::Mat valid = cv::Mat::ones(depth.rows, depth.cols, CV_8U);
    if (hasNoData) {
        cv::compare(elevation, static_cast<float>(noDataValue), valid, cv::CMP_NE);
    }
    const float invalidOutputValue = hasNoData ? static_cast<float>(noDataValue) : 0.0f;

    const double pixelArea = std::abs(gt[1] * gt[5] - gt[2] * gt[4]);
    double reservoirArea = 0.0;
    double reservoirVolume = 0.0;
    int invalidPixels = 0;
    for (int y = 0; y < depth.rows; ++y) {
        const uchar* validRow = valid.ptr<uchar>(y);
        float* depthRow = depth.ptr<float>(y);
        for (int x = 0; x < depth.cols; ++x) {
            if (validRow[x] == 0) {
                depthRow[x] = invalidOutputValue;
                ++invalidPixels;
                continue;
            }
            const double value = depthRow[x];
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
    auto meta = gis::core::buildPluginMetadata(input, srcDs.get(), "terrain.reservoir_volume");
    meta.algorithmParams["band"] = std::to_string(band);
    meta.algorithmParams["water_level"] = std::to_string(waterLevel);
    meta.algorithmParams["reservoir_area"] = std::to_string(reservoirArea);
    meta.algorithmParams["reservoir_volume"] = std::to_string(reservoirVolume);
    meta.algorithmParams["invalid_pixels"] = std::to_string(invalidPixels);
    gis::core::writeProcessingMetadata(output, meta);
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok("ReservoirVolume completed", output);
    result.metadata["action"] = "reservoir_volume";
    result.metadata["band"] = std::to_string(band);
    result.metadata["water_level"] = std::to_string(waterLevel);
    result.metadata["reservoir_area"] = std::to_string(reservoirArea);
    result.metadata["reservoir_volume"] = std::to_string(reservoirVolume);
    result.metadata["invalid_pixels"] = std::to_string(invalidPixels);
    return result;
}

} // namespace

std::vector<gis::framework::ParamSpec> TerrainPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "动作", "Terrain analysis action",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"slope", "aspect", "hillshade", "tpi", "curvature", "profile_curvature", "plan_curvature", "tri", "roughness", "fill_sinks", "flow_direction", "flow_accumulation", "stream_extract", "watershed", "profile_extract", "viewshed", "viewshed_multi", "cut_fill", "reservoir_volume"}
        },
        gis::framework::ParamSpec{"input", "输入DEM", "Input DEM path", gis::framework::ParamType::FilePath, true, std::string{}},
        gis::framework::ParamSpec{"reference", "参考DEM", "Reference DEM path used by cut_fill", gis::framework::ParamType::FilePath, false, std::string{}},
        gis::framework::ParamSpec{"output", "输出结果", "Output raster or report path", gis::framework::ParamType::FilePath, true, std::string{}},
        gis::framework::ParamSpec{"band", "波段", "DEM band index", gis::framework::ParamType::Int, false, int{1}, int{1}, int{999}},
        gis::framework::ParamSpec{"z_factor", "Z因子", "Vertical exaggeration factor", gis::framework::ParamType::Double, false, double{1.0}, double{0.001}, double{10000.0}},
        gis::framework::ParamSpec{"azimuth", "方位角", "Light azimuth for hillshade", gis::framework::ParamType::Double, false, double{315.0}, double{0.0}, double{360.0}},
        gis::framework::ParamSpec{"altitude", "高度角", "Light altitude for hillshade", gis::framework::ParamType::Double, false, double{45.0}, double{0.0}, double{90.0}},
        gis::framework::ParamSpec{"accum_threshold", "汇流阈值", "Threshold used by stream extraction", gis::framework::ParamType::Double, false, double{10.0}, double{0.0}, double{1e9}},
        // 只被 fill_sinks 读取（其它 action 传 0.0 即完全等效于未启用），默认 0.0 保证
        // "GUI 全默认提交"这条路径的行为与新增本键之前逐像元一致。
        // WHY 描述要写清两件事（fix round 1 F1/F5 的口径）：
        // (1) 施与集合只有"低于出水面的像元"，即 fill_sinks 真的抬过的那一片水域——曾经写成对
        //     "与出水面等高"的像元也施 ε，于是整片平地逐圈累积成锥体，描述与产品都不符；
        // (2) ε 加在 elevation *= z_factor **之后**的缩放高程上 ⇒ 真实单位下的微梯度是
        //     epsilon / z_factor（z_factor 区间 [0.001,10000] ⇒ 跨 7 个数量级）。本任务不改单位，
        //     但必须让描述把这件事说出去，不许留成无人知晓的隐式语义。
        gis::framework::ParamSpec{"epsilon", "微坡度", "Monotone micro-gradient added inside each filled depression, on the cells strictly below its water surface (terrain above it is never touched); values are post-z_factor, so the real-unit gradient is epsilon / z_factor", gis::framework::ParamType::Double, false, double{0.0}, double{0.0}, double{10.0}},
        gis::framework::ParamSpec{"profile_path", "剖面路径", "Profile points formatted as x1,y1;x2,y2;...", gis::framework::ParamType::String, false, std::string{}},
        gis::framework::ParamSpec{"observer_x", "观察点X", "Observer X coordinate", gis::framework::ParamType::Double, false, double{0.0}, double{-1e15}, double{1e15}},
        gis::framework::ParamSpec{"observer_y", "观察点Y", "Observer Y coordinate", gis::framework::ParamType::Double, false, double{0.0}, double{-1e15}, double{1e15}},
        gis::framework::ParamSpec{"observer_points", "观察点集合", "Multiple observer points formatted as x1,y1;x2,y2;...", gis::framework::ParamType::String, false, std::string{}},
        gis::framework::ParamSpec{"observer_height", "观察高度", "Observer height above terrain", gis::framework::ParamType::Double, false, double{2.0}, double{0.0}, double{10000.0}},
        gis::framework::ParamSpec{"target_height", "目标高度", "Target height above terrain", gis::framework::ParamType::Double, false, double{0.0}, double{0.0}, double{10000.0}},
        gis::framework::ParamSpec{"max_distance", "最大距离", "Maximum analysis distance, 0 means unlimited", gis::framework::ParamType::Double, false, double{0.0}, double{0.0}, double{1e9}},
        // 只被 viewshed 读取：runViewshedMultiProcess 的第 13 实参仍是硬编码 0.0，所以 R-T12-4
        // 裁定它**不进** viewshed_multi 的 visibleKeys（否则就是面板上放一个改了没用的参数）。
        // 默认 0.85714 = GDAL 的地球曲率+大气折射合并系数，与 doViewshed 的代码兜底同值；
        // 0.0 的语义是关闭修正（改前的行为），区间 [0,2] 覆盖到物理上有意义的取值上限。
        gis::framework::ParamSpec{"curvature_coeff", "曲率折射系数",
            "Earth curvature and refraction coefficient (GDAL default 0.85714, 0 disables)",
            gis::framework::ParamType::Double, false, double{0.85714}, double{0.0}, double{2.0}},
        gis::framework::ParamSpec{"water_level", "水位", "Water level used by reservoir volume analysis", gis::framework::ParamType::Double, false, double{0.0}, double{-10000.0}, double{10000.0}},
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
    if (action == "curvature") return doCurvature(params, progress);
    if (action == "profile_curvature") return doProfileCurvature(params, progress);
    if (action == "plan_curvature") return doPlanCurvature(params, progress);
    if (action == "tri") return doTri(params, progress);
    if (action == "roughness") return doRoughness(params, progress);
    if (action == "fill_sinks") return doFillSinks(params, progress);
    if (action == "flow_direction") return doFlowDirection(params, progress);
    if (action == "flow_accumulation") return doFlowAccumulation(params, progress);
    if (action == "stream_extract") return doStreamExtract(params, progress);
    if (action == "watershed") return doWatershed(params, progress);
    if (action == "profile_extract") return doProfileExtract(params, progress);
    if (action == "viewshed") return doViewshed(params, progress);
    if (action == "viewshed_multi") return doViewshedMulti(params, progress);
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

gis::framework::Result TerrainPlugin::doCurvature(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runLocalTerrainProcess(
        "curvature",
        "Curvature",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        progress);
}

gis::framework::Result TerrainPlugin::doProfileCurvature(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runLocalTerrainProcess(
        "profile_curvature",
        "ProfileCurvature",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        progress);
}

gis::framework::Result TerrainPlugin::doPlanCurvature(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runLocalTerrainProcess(
        "plan_curvature",
        "PlanCurvature",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        progress);
}

gis::framework::Result TerrainPlugin::doTri(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runLocalTerrainProcess(
        "tri",
        "TRI",
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
    // WHY 下限钳到 0：epsilon 的语义是"抬升"，负值会把被填的洼地压到出水面之下，
    // 直接违反 fill_sinks 的"只抬不降"不变量（ParamSpec 的 [0,10] 只约束 GUI/CLI 表单，
    // 程序化调用不经它）。
    const double epsilon = std::max(0.0, gis::framework::getParam<double>(params, "epsilon", 0.0));
    return runHydrologyTerrainProcess(
        "fill_sinks",
        "FillSinks",
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<double>(params, "z_factor", 1.0),
        0.0,
        progress,
        epsilon);
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
        // 代码兜底值与 paramSpecs() 里 curvature_coeff 的默认值同值（GDAL 的 0.85714）：
        // CLI 不带该键、GUI 面板未渲染时走的都是这条兜底，两处不同值会让 GUI 与 CLI 提交不同结果。
        gis::framework::getParam<double>(params, "curvature_coeff", 0.85714),
        progress);
}

gis::framework::Result TerrainPlugin::doViewshedMulti(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    return runViewshedMultiProcess(
        gis::framework::getParam<std::string>(params, "input", ""),
        gis::framework::getParam<std::string>(params, "output", ""),
        gis::framework::getParam<int>(params, "band", 1),
        gis::framework::getParam<std::string>(params, "observer_points", ""),
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


