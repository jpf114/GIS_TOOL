#include <gis/core/opencv_wrapper.h>
#include <gis/core/gdal_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace gis::core {

cv::Mat gdalBandToMat(GDALDataset* ds, int bandIndex) {
    auto* band = ds->GetRasterBand(bandIndex);
    if (!band) {
        throw GisError("Cannot get band " + std::to_string(bandIndex));
    }
    int width = ds->GetRasterXSize();
    int height = ds->GetRasterYSize();

    constexpr size_t kMaxPixels = 50000 * 50000;
    if (static_cast<size_t>(width) * static_cast<size_t>(height) > kMaxPixels) {
        throw GisError("Raster too large for in-memory processing (" +
                       std::to_string(width) + "x" + std::to_string(height) +
                       "). Maximum supported: 50000x50000 pixels.");
    }

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
    double adfGT[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    // 批次 D / Task 8（位点 B）：GetGeoTransform 的**返回值必须判**，取不到就不往产物写。
    // WHY 不改成失败：本函数有 40+ 调用点，"无参考输入照常出图"是既有契约
    //   （tests/test_plugins.cpp:1969 断不失败、:1982 断产物存在），拒绝执行会断整条链。
    // WHY 不能继续无条件 SetGeoTransform（改前形态）：GDAL 失败时**基类仍会把恒等阵
    //   (0,1,0,0,0,1) 写进数组**（gcore/gdaldataset.cpp:1506-1512），而自定义 GetGeoTransform
    //   且失败前不碰数组的驱动（实测 PNM）会让数组**保留调用方的初值**——改前这里是
    //   `double adfGT[6];`（完全没有初始化），Debug 下即 MSVC 栈填充 0xCCCCCCCCCCCCCCCC
    //   = -9.25596e+61，被原样持久化进交付 GeoTIFF。两种伪造成品的实测读数在
    //   docs/RELEASE.md 批次 D 节与 tests/test_plugins.cpp 的同名新增用例注释里。
    // 数组显式零初始化只是把"误读数组"的未定义性去掉；**判据只看返回值**，不看数组内容。
    const bool hasGeoTransform = srcDS->GetGeoTransform(adfGT) == CE_None;
    std::string proj = getSRSWKT(srcDS);

    auto dstDS = createRaster(dstPath, mat.cols, mat.rows, 1, GDT_Float32);
    if (hasGeoTransform) {
        dstDS->SetGeoTransform(adfGT);
    }
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

    double adfGT[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    // 批次 D / Task 8（位点 C，与上面位点 B 同形态、同处置）：判返回值，仅 CE_None 才写进产物。
    // 本函数是**多波段**写出支，调用方含 processing pansharpen（processing_plugin.cpp:894）——
    // 那条的无变换契约由 PansharpenNoGeoTransformSkipsExtentCheck 钉在"不失败 + 产物照常生成"上，
    // 所以这里同样**不许**改成硬失败，只把伪造的变换式从产物里去掉。
    const bool hasGeoTransform = srcDS->GetGeoTransform(adfGT) == CE_None;
    std::string proj = getSRSWKT(srcDS);

    auto dstDS = createRaster(dstPath, width, height, numBands, GDT_Float32);
    if (hasGeoTransform) {
        dstDS->SetGeoTransform(adfGT);
    }
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
