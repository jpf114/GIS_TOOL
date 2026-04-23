#include <gis/core/opencv_wrapper.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <opencv2/opencv.hpp>

namespace gis::core {

cv::Mat gdalBandToMat(GDALDataset* ds, int bandIndex) {
    auto* band = ds->GetRasterBand(bandIndex);
    if (!band) {
        throw GisError("Cannot get band " + std::to_string(bandIndex));
    }
    int width = ds->GetRasterXSize();
    int height = ds->GetRasterYSize();

    cv::Mat mat(height, width, CV_32F);
    band->RasterIO(GF_Read, 0, 0, width, height,
                   mat.ptr<float>(), width, height, GDT_Float32, 0, 0);
    return mat;
}

void matToGdalTiff(const cv::Mat& mat, const std::string& srcPath,
                   const std::string& dstPath, int bandIndex) {
    auto srcDS = openRaster(srcPath, true);
    matToGdalTiff(mat, srcDS.get(), dstPath, bandIndex);
}

void matToGdalTiff(const cv::Mat& mat, GDALDataset* srcDS,
                   const std::string& dstPath, int bandIndex) {
    double adfGT[6];
    srcDS->GetGeoTransform(adfGT);
    std::string proj = getSRSWKT(srcDS);

    auto dstDS = createRaster(dstPath, mat.cols, mat.rows, 1, GDT_Float32);
    dstDS->SetGeoTransform(adfGT);
    if (!proj.empty()) {
        dstDS->SetProjection(proj.c_str());
    }

    auto* band = dstDS->GetRasterBand(1);

    int srcHasNoData = 0;
    if (bandIndex >= 1 && bandIndex <= srcDS->GetRasterCount()) {
        double srcNoData = srcDS->GetRasterBand(bandIndex)->GetNoDataValue(&srcHasNoData);
        if (srcHasNoData) {
            band->SetNoDataValue(srcNoData);
        }
    }

    int cvType = mat.type();

    if (cvType == CV_32F) {
        band->RasterIO(GF_Write, 0, 0, mat.cols, mat.rows,
                       const_cast<float*>(mat.ptr<float>()), mat.cols, mat.rows, GDT_Float32, 0, 0);
    } else if (cvType == CV_8U) {
        band->RasterIO(GF_Write, 0, 0, mat.cols, mat.rows,
                       const_cast<uint8_t*>(mat.ptr<uint8_t>()), mat.cols, mat.rows, GDT_Byte, 0, 0);
    } else if (cvType == CV_16U) {
        band->RasterIO(GF_Write, 0, 0, mat.cols, mat.rows,
                       const_cast<uint16_t*>(mat.ptr<uint16_t>()), mat.cols, mat.rows, GDT_UInt16, 0, 0);
    } else {
        cv::Mat fmat;
        mat.convertTo(fmat, CV_32F);
        band->RasterIO(GF_Write, 0, 0, fmat.cols, fmat.rows,
                       fmat.ptr<float>(), fmat.cols, fmat.rows, GDT_Float32, 0, 0);
    }
}

void matsToGdalTiff(const std::vector<cv::Mat>& mats, GDALDataset* srcDS,
                    const std::string& dstPath) {
    if (mats.empty()) return;

    int width = mats[0].cols;
    int height = mats[0].rows;
    int numBands = static_cast<int>(mats.size());

    double adfGT[6];
    srcDS->GetGeoTransform(adfGT);
    std::string proj = getSRSWKT(srcDS);

    auto dstDS = createRaster(dstPath, width, height, numBands, GDT_Float32);
    dstDS->SetGeoTransform(adfGT);
    if (!proj.empty()) {
        dstDS->SetProjection(proj.c_str());
    }

    for (int i = 0; i < numBands; ++i) {
        auto* band = dstDS->GetRasterBand(i + 1);

        int srcBandIdx = (i < srcDS->GetRasterCount()) ? (i + 1) : 1;
        int srcHasNoData = 0;
        double srcNoData = srcDS->GetRasterBand(srcBandIdx)->GetNoDataValue(&srcHasNoData);
        if (srcHasNoData) {
            band->SetNoDataValue(srcNoData);
        }

        cv::Mat fmat;
        if (mats[i].type() != CV_32F) {
            mats[i].convertTo(fmat, CV_32F);
        } else {
            fmat = mats[i];
        }
        band->RasterIO(GF_Write, 0, 0, width, height,
                       fmat.ptr<float>(), width, height, GDT_Float32, 0, 0);
    }
}

int gdalTypeToCvType(int gdalType) {
    switch (gdalType) {
        case GDT_Byte:    return CV_8U;
        case GDT_UInt16:  return CV_16U;
        case GDT_Int16:   return CV_16S;
        case GDT_Int32:   return CV_32S;
        case GDT_Float32: return CV_32F;
        case GDT_Float64: return CV_64F;
        default:          return CV_32F;
    }
}

cv::Mat readBandAsMat(const std::string& path, int bandIndex) {
    auto ds = openRaster(path, true);
    if (!ds) {
        throw GisError("Cannot open raster: " + path);
    }
    return gdalBandToMat(ds.get(), bandIndex);
}

cv::Mat toUint8(const cv::Mat& mat) {
    if (mat.type() == CV_8U) return mat.clone();

    double minVal, maxVal;
    cv::minMaxLoc(mat, &minVal, &maxVal);

    cv::Mat u8;
    if (maxVal - minVal < 1e-10) {
        u8 = cv::Mat::zeros(mat.size(), CV_8U);
    } else {
        cv::normalize(mat, u8, 0, 255, cv::NORM_MINMAX, CV_8U);
    }
    return u8;
}

} // namespace gis::core
