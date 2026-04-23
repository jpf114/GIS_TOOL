#pragma once
#include <gis/core/types.h>
#include <gis/core/progress.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

class GDALDataset;
class OGRSpatialReference;

namespace gis::core {

struct GdalDatasetDeleter {
    void operator()(GDALDataset* ds) const;
};

using GdalDatasetPtr = std::unique_ptr<GDALDataset, GdalDatasetDeleter>;

GdalDatasetPtr openRaster(const std::string& path, bool readOnly = true);
GdalDatasetPtr createRaster(const std::string& path, int width, int height,
                            int bands, int gdalType, const std::string& driver = "GTiff");

void copySpatialRef(GDALDataset* src, GDALDataset* dst);

OGRSpatialReference* parseSRS(const std::string& srs);

std::string getSRSWKT(GDALDataset* ds);
void initGDAL();

struct BandStats {
    double minVal;
    double maxVal;
    double mean;
    double stddev;
    double noDataValue;
    bool hasNoData;
    int dataType;
    std::string dataTypeName;
};

struct HistogramBin {
    double minVal;
    double maxVal;
    uint64_t count;
};

struct RasterInfo {
    std::string filePath;
    std::string driver;
    int width;
    int height;
    int bandCount;
    double geoTransform[6];
    std::string crsWKT;
    std::string crsAuth;
    std::vector<BandStats> bands;
};

BandStats computeBandStats(GDALDataset* ds, int bandIndex = 1);
std::vector<HistogramBin> computeHistogram(GDALDataset* ds, int bandIndex = 1, int numBins = 256);
RasterInfo getRasterInfo(GDALDataset* ds, const std::string& filePath = "");
bool setNoDataValue(GDALDataset* ds, int bandIndex, double value);
double getNoDataValue(GDALDataset* ds, int bandIndex = 1, bool* hasNoData = nullptr);
bool buildOverviews(GDALDataset* ds, const std::vector<int>& levels,
                    const std::string& resampling = "NEAREST",
                    ProgressReporter* progress = nullptr);

} // namespace gis::core
