#pragma once
#include <string>
#include <memory>

class GDALDataset;

namespace cv { class Mat; }

namespace gis::core {

// Read a single band from a GDAL dataset into cv::Mat (float32)
// The caller is responsible for keeping the dataset alive.
cv::Mat gdalBandToMat(GDALDataset* ds, int bandIndex = 1);

// Write cv::Mat back to a new GeoTIFF, copying spatial reference from srcPath
void matToGdalTiff(const cv::Mat& mat, const std::string& srcPath,
                   const std::string& dstPath, int bandIndex = 1);

// Convert GDAL data type to OpenCV type
int gdalTypeToCvType(int gdalType);

} // namespace gis::core
