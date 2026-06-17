#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

class GDALDataset;
class GDALRasterBand;

namespace gis::core {

class ProgressReporter;

/** Block-based raster processor for memory-efficient processing of large rasters. */
class RasterBlockProcessor {
public:
    /** Describes a rectangular block within a raster. */
    struct Block {
        int xOff = 0;  ///< X offset (column) of the block in pixels
        int yOff = 0;  ///< Y offset (row) of the block in pixels
        int xSize = 0; ///< Width of the block in pixels
        int ySize = 0; ///< Height of the block in pixels
    };

    /** Configuration for block processing. */
    struct Config {
        int blockSize = 256;   ///< Block size in pixels (both width and height)
        bool overlap = false;  ///< Whether blocks overlap at edges
        int overlapSize = 0;   ///< Overlap size in pixels when overlap is enabled
    };

    explicit RasterBlockProcessor(const Config& config = Config{});

    /** Callback type for processing a single block. */
    using BlockFn = std::function<void(
        const Block& block,
        GDALRasterBand* srcBand,
        GDALRasterBand* dstBand)>;

    /**
     * Process a raster in blocks, applying fn to each block.
     * @param inputPath Path to the input raster.
     * @param outputPath Path for the output raster.
     * @param fn Callback invoked for each block.
     * @param progress Progress reporter.
     */
    void process(const std::string& inputPath,
                 const std::string& outputPath,
                 const BlockFn& fn,
                 ProgressReporter& progress);

    /**
     * Compute block layout for a given raster dimension.
     * @param width Raster width in pixels.
     * @param height Raster height in pixels.
     * @param blockSize Block size in pixels.
     * @param overlap Whether blocks overlap.
     * @param overlapSize Overlap size in pixels.
     * @return Vector of Block descriptors covering the entire raster.
     */
    static std::vector<Block> computeBlocks(
        int width, int height, int blockSize,
        bool overlap = false, int overlapSize = 0);

    Config config() const { return config_; }

private:
    Config config_;
};

} // namespace gis::core
