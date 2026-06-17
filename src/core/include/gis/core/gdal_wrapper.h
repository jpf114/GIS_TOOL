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

/** Deleter for GDALDataset unique_ptr. Calls GDALClose on the dataset. */
struct GdalDatasetDeleter {
    void operator()(GDALDataset* ds) const;
};

/** Owning pointer to a GDALDataset with automatic cleanup. */
using GdalDatasetPtr = std::unique_ptr<GDALDataset, GdalDatasetDeleter>;

/**
 * Open a raster dataset from disk.
 * @param path File path to the raster.
 * @param readOnly True to open in read-only mode (default).
 * @return Owning pointer to the dataset, or nullptr on failure.
 */
GdalDatasetPtr openRaster(const std::string& path, bool readOnly = true);

/**
 * Create a new raster dataset on disk.
 * @param path Output file path.
 * @param width Raster width in pixels.
 * @param height Raster height in pixels.
 * @param bands Number of raster bands.
 * @param gdalType GDAL data type code (e.g., GDT_Byte, GDT_Float32).
 * @param driver GDAL driver name (default "GTiff").
 * @return Owning pointer to the new dataset, or nullptr on failure.
 */
GdalDatasetPtr createRaster(const std::string& path, int width, int height,
                            int bands, int gdalType, const std::string& driver = "GTiff");

/** Copy spatial reference and geotransform from src to dst dataset. */
void copySpatialRef(GDALDataset* src, GDALDataset* dst);

/**
 * Parse a spatial reference string (WKT or EPSG) into an OGRSpatialReference.
 * @param srs WKT string or EPSG code string (e.g., "EPSG:4326").
 * @return Owning pointer to the parsed SRS, or nullptr on failure.
 */
std::unique_ptr<OGRSpatialReference> parseSRS(const std::string& srs);

/** Get the WKT representation of a dataset's spatial reference. */
std::string getSRSWKT(GDALDataset* ds);

/** Initialize the GDAL library (register all drivers). Call once at startup. */
void initGDAL();

/** Statistics for a single raster band. */
struct BandStats {
    double minVal;       ///< Minimum pixel value
    double maxVal;       ///< Maximum pixel value
    double mean;         ///< Mean pixel value
    double stddev;       ///< Standard deviation of pixel values
    double noDataValue;  ///< No-data value for the band
    bool hasNoData;      ///< Whether a no-data value is defined
    int dataType;        ///< GDAL data type code
    std::string dataTypeName; ///< Human-readable data type name
};

/** A single bin in a histogram. */
struct HistogramBin {
    double minVal; ///< Lower bound of the bin (inclusive)
    double maxVal; ///< Upper bound of the bin (exclusive)
    uint64_t count; ///< Number of pixels in this bin
};

/** Comprehensive metadata for a raster dataset. */
struct RasterInfo {
    std::string filePath;       ///< File path of the raster
    std::string driver;         ///< GDAL driver used to open the file
    int width;                  ///< Raster width in pixels
    int height;                 ///< Raster height in pixels
    int bandCount;              ///< Number of raster bands
    double geoTransform[6];     ///< Affine geotransform coefficients
    std::string crsWKT;         ///< WKT of the coordinate reference system
    std::string crsAuth;        ///< Authority identifier (e.g., "EPSG:4326")
    std::vector<BandStats> bands; ///< Per-band statistics
};

/**
 * Compute statistics for a raster band.
 * @param ds The dataset to read from.
 * @param bandIndex 1-based band index (default 1).
 */
BandStats computeBandStats(GDALDataset* ds, int bandIndex = 1);

/**
 * Compute a histogram for a raster band.
 * @param ds The dataset to read from.
 * @param bandIndex 1-based band index (default 1).
 * @param numBins Number of histogram bins (default 256).
 */
std::vector<HistogramBin> computeHistogram(GDALDataset* ds, int bandIndex = 1, int numBins = 256);

/**
 * Gather comprehensive metadata for a raster dataset.
 * @param ds The dataset to inspect.
 * @param filePath Optional file path to include in the result.
 */
RasterInfo getRasterInfo(GDALDataset* ds, const std::string& filePath = "");

/** Set the no-data value for a specific band. Returns true on success. */
bool setNoDataValue(GDALDataset* ds, int bandIndex, double value);

/**
 * Get the no-data value for a specific band.
 * @param hasNoData Optional output: whether a no-data value is defined.
 */
double getNoDataValue(GDALDataset* ds, int bandIndex = 1, bool* hasNoData = nullptr);

/**
 * Build overview pyramids for a raster dataset.
 * @param ds The dataset to build overviews for.
 * @param levels Overview levels (e.g., {2, 4, 8}).
 * @param resampling Resampling method (default "NEAREST").
 * @param progress Optional progress reporter.
 * @return True on success.
 */
bool buildOverviews(GDALDataset* ds, const std::vector<int>& levels,
                    const std::string& resampling = "NEAREST",
                    ProgressReporter* progress = nullptr);

/** Metadata describing how a raster was processed, stored as GDAL metadata. */
struct ProcessingMetadata {
    std::string sourceFile;             ///< Original source file path
    std::string sourceCrs;              ///< CRS of the source raster
    std::string processingAlgorithm;    ///< Algorithm name used for processing
    std::string processingVersion;      ///< Version of the processing algorithm
    std::string processingTime;         ///< Timestamp of processing
    std::map<std::string, std::string> algorithmParams; ///< Algorithm-specific parameters
};

/** Write processing metadata to a raster file by path. Returns true on success. */
bool writeProcessingMetadata(const std::string& rasterPath, const ProcessingMetadata& metadata);
/** Write processing metadata to an open dataset. Returns true on success. */
bool writeProcessingMetadata(GDALDataset* ds, const ProcessingMetadata& metadata);
/** Read processing metadata from an open dataset. */
ProcessingMetadata readProcessingMetadata(GDALDataset* ds);

} // namespace gis::core
