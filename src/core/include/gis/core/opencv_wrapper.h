#pragma once
#include <string>
#include <vector>
#include <memory>

class GDALDataset;

namespace cv { class Mat; }

namespace gis::core {

cv::Mat gdalBandToMat(GDALDataset* ds, int bandIndex = 1);

void matToGdalTiff(const cv::Mat& mat, const std::string& srcPath,
                   const std::string& dstPath, int bandIndex = 1);

void matToGdalTiff(const cv::Mat& mat, GDALDataset* srcDS,
                   const std::string& dstPath, int bandIndex = 1);

void matsToGdalTiff(const std::vector<cv::Mat>& mats, GDALDataset* srcDS,
                    const std::string& dstPath);

int gdalTypeToCvType(int gdalType);

cv::Mat readBandAsMat(const std::string& path, int bandIndex = 1);

cv::Mat toUint8(const cv::Mat& mat);

} // namespace gis::core
