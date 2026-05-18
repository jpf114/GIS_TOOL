#include <gis/core/block_processor.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/progress.h>

#include <gdal_priv.h>

#include <algorithm>
#include <cmath>

namespace gis::core {

RasterBlockProcessor::RasterBlockProcessor(const Config& config)
    : config_(config) {}

std::vector<RasterBlockProcessor::Block>
RasterBlockProcessor::computeBlocks(int width, int height, int blockSize,
                                     bool overlap, int overlapSize) {
    std::vector<Block> blocks;

    if (blockSize <= 0) blockSize = 256;
    if (overlap && overlapSize < 0) overlapSize = 0;

    int step = overlap ? std::max(1, blockSize - overlapSize) : blockSize;

    for (int yOff = 0; yOff < height; yOff += step) {
        for (int xOff = 0; xOff < width; xOff += step) {
            Block blk;
            blk.xOff = xOff;
            blk.yOff = yOff;

            if (overlap) {
                int xStart = std::max(0, xOff - overlapSize);
                int yStart = std::max(0, yOff - overlapSize);
                int xEnd = std::min(width, xOff + blockSize + overlapSize);
                int yEnd = std::min(height, yOff + blockSize + overlapSize);
                blk.xOff = xStart;
                blk.yOff = yStart;
                blk.xSize = xEnd - xStart;
                blk.ySize = yEnd - yStart;
            } else {
                blk.xSize = std::min(blockSize, width - xOff);
                blk.ySize = std::min(blockSize, height - yOff);
            }

            blocks.push_back(blk);
        }
    }

    return blocks;
}

void RasterBlockProcessor::process(const std::string& inputPath,
                                    const std::string& outputPath,
                                    const BlockFn& fn,
                                    ProgressReporter& progress) {
    auto srcDs = openRaster(inputPath, true);
    if (!srcDs) {
        throw std::runtime_error("Cannot open input raster: " + inputPath);
    }

    int width = srcDs->GetRasterXSize();
    int height = srcDs->GetRasterYSize();
    int bandCount = srcDs->GetRasterCount();

    auto blocks = computeBlocks(width, height, config_.blockSize,
                                config_.overlap, config_.overlapSize);
    if (blocks.empty()) {
        throw std::runtime_error("No blocks to process");
    }

    double geoTransform[6] = {};
    srcDs->GetGeoTransform(geoTransform);

    char* projWkt = nullptr;
    srcDs->GetProjectionRef();

    auto dstDs = createRaster(outputPath, width, height, bandCount,
                               srcDs->GetRasterBand(1)->GetRasterDataType());
    if (!dstDs) {
        throw std::runtime_error("Cannot create output raster: " + outputPath);
    }

    dstDs->SetGeoTransform(geoTransform);

    const char* srcProj = srcDs->GetProjectionRef();
    if (srcProj && srcProj[0] != '\0') {
        dstDs->SetProjection(srcProj);
    }

    for (int b = 1; b <= bandCount; ++b) {
        auto* srcBand = srcDs->GetRasterBand(b);
        auto* dstBand = dstDs->GetRasterBand(b);
        if (!srcBand || !dstBand) continue;

        int hasNoData = 0;
        double noDataVal = srcBand->GetNoDataValue(&hasNoData);
        if (hasNoData) {
            dstBand->SetNoDataValue(noDataVal);
        }
    }

    dstDs->FlushCache();

    int totalBlocks = static_cast<int>(blocks.size()) * bandCount;
    int completedBlocks = 0;

    for (int b = 1; b <= bandCount; ++b) {
        auto* srcBand = srcDs->GetRasterBand(b);
        auto* dstBand = dstDs->GetRasterBand(b);
        if (!srcBand || !dstBand) continue;

        for (const auto& block : blocks) {
            progress.throwIfCancelled();

            fn(block, srcBand, dstBand);

            completedBlocks++;
            double prog = static_cast<double>(completedBlocks) / static_cast<double>(totalBlocks);
            progress.onProgress(prog);
        }
    }

    dstDs->FlushCache();
    progress.onProgress(1.0);
}

} // namespace gis::core
