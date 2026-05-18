#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

class GDALDataset;
class GDALRasterBand;

namespace gis::core {

class ProgressReporter;

class RasterBlockProcessor {
public:
    struct Block {
        int xOff = 0;
        int yOff = 0;
        int xSize = 0;
        int ySize = 0;
    };

    struct Config {
        int blockSize = 256;
        bool overlap = false;
        int overlapSize = 0;
    };

    explicit RasterBlockProcessor(const Config& config = Config{});

    using BlockFn = std::function<void(
        const Block& block,
        GDALRasterBand* srcBand,
        GDALRasterBand* dstBand)>;

    void process(const std::string& inputPath,
                 const std::string& outputPath,
                 const BlockFn& fn,
                 ProgressReporter& progress);

    static std::vector<Block> computeBlocks(
        int width, int height, int blockSize,
        bool overlap = false, int overlapSize = 0);

    Config config() const { return config_; }

private:
    Config config_;
};

} // namespace gis::core
