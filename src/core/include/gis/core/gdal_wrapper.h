#pragma once
#include <gis/core/types.h>
#include <gis/core/progress.h>
#include <string>
#include <vector>
#include <memory>

// Forward declarations for GDAL types
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

// Parse EPSG or WKT string into OGRSpatialReference.
// Caller must free the returned object.
OGRSpatialReference* parseSRS(const std::string& srs);

std::string getSRSWKT(GDALDataset* ds);
void initGDAL();

} // namespace gis::core
