#pragma once
#include <string>
#include <vector>
#include <memory>

class GDALDataset;

namespace cv { class Mat; }

namespace gis::core {

// 两个读波段的入口（gdalBandToMat / readBandAsMat）只有两种出口：返回非空 Mat，或抛 GisError
// （打不开、取不到波段、尺寸超限）。WHY 不返回空 Mat：0x0 数据集在 GDAL 创建侧就被拒
// （实测 `gdal_create -outsize 0 0` 报 "Attempt to create 0x0 dataset is illegal"），
// 所以调用方的 `if (mat.empty())` 是够不着的死支——批次 D 的 N187 据此删掉了 processing 里 15 处判空 fail。
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
