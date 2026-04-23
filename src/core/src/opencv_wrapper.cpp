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

    double adfGT[6];
    srcDS->GetGeoTransform(adfGT);
    std::string proj = getSRSWKT(srcDS.get());

    auto dstDS = createRaster(dstPath, mat.cols, mat.rows, 1, GDT_Float32);
    dstDS->SetGeoTransform(adfGT);
    if (!proj.empty()) {
        dstDS->SetProjection(proj.c_str());
    }

    auto* band = dstDS->GetRasterBand(1);
    int cvType = mat.type();
    GDALDataType gdt;

    if (cvType == CV_32F) {
        gdt = GDT_Float32;
        band->RasterIO(GF_Write, 0, 0, mat.cols, mat.rows,
                       const_cast<float*>(mat.ptr<float>()), mat.cols, mat.rows, gdt, 0, 0);
    } else if (cvType == CV_8U) {
        gdt = GDT_Byte;
        band->RasterIO(GF_Write, 0, 0, mat.cols, mat.rows,
                       const_cast<uint8_t*>(mat.ptr<uint8_t>()), mat.cols, mat.rows, gdt, 0, 0);
    } else if (cvType == CV_16U) {
        gdt = GDT_UInt16;
        band->RasterIO(GF_Write, 0, 0, mat.cols, mat.rows,
                       const_cast<uint16_t*>(mat.ptr<uint16_t>()), mat.cols, mat.rows, gdt, 0, 0);
    } else {
        // Fallback: convert to float32
        cv::Mat fmat;
        mat.convertTo(fmat, CV_32F);
        band->RasterIO(GF_Write, 0, 0, fmat.cols, fmat.rows,
                       fmat.ptr<float>(), fmat.cols, fmat.rows, GDT_Float32, 0, 0);
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

} // namespace gis::core
