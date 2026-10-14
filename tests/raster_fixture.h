#pragma once

// 三态栅格测试夹具：CRS / NoData / 数据类型 / 地理变换 一次性写入，读回供数值断言。
//
// 数据缓冲约定（重要）：当前写入实现仅支持 Float32 缓冲（data / bandValues 都是
// std::vector<float>），创建与写入一律按 GDT_Float32 进行；RasterFixtureOptions::dtype
// 字段保留用于后续任务扩展（整型夹具由后续任务实现，届时按 dtype 分支创建与写入）。
// 后续任务不会传入非 GDT_Float32 的 dtype。

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <gis/core/gdal_wrapper.h>

namespace gis::tests {

enum class FixtureCrs { None, Epsg4326, Epsg3857 };

// 读取失败时返回的哨兵值：故意使用 NaN，任何数值断言（EXPECT_FLOAT_EQ / EXPECT_EQ 等）
// 都不会与合法栅格值相等，因此不会把"打不开文件"伪装成"读到 0"。
inline float unreadablePixelValue() {
    return std::numeric_limits<float>::quiet_NaN();
}

struct RasterFixtureOptions {
    FixtureCrs crs = FixtureCrs::None;
    bool hasNoData = false;
    double noData = -9999.0;
    GDALDataType dtype = GDT_Float32;  // 当前仅支持 GDT_Float32，见文件头注释
    double originX = 116.0;
    double originY = 40.0;
    double pixelSize = 0.001;
};

inline int epsgFor(FixtureCrs crs) {
    switch (crs) {
        case FixtureCrs::Epsg4326: return 4326;
        case FixtureCrs::Epsg3857: return 3857;
        default: return 0;
    }
}

inline void applyFixtureHeader(GDALDataset* ds, const RasterFixtureOptions& options,
                               int width, int height) {
    if (!ds) {
        ADD_FAILURE() << "applyFixtureHeader: dataset is null (creation failed)";
        return;
    }
    double gt[6] = {options.originX, options.pixelSize, 0.0,
                    options.originY, 0.0, -options.pixelSize};
    ds->SetGeoTransform(gt);
    const int epsg = epsgFor(options.crs);
    if (epsg > 0) {
        OGRSpatialReference srs;
        srs.importFromEPSG(epsg);
        char* wkt = nullptr;
        srs.exportToWkt(&wkt);
        ds->SetProjection(wkt ? wkt : "");
        CPLFree(wkt);
    }
    if (options.hasNoData) {
        for (int b = 1; b <= ds->GetRasterCount(); ++b) {
            auto* band = ds->GetRasterBand(b);
            if (!band) {
                ADD_FAILURE() << "applyFixtureHeader: band " << b << " is null";
                continue;
            }
            band->SetNoDataValue(options.noData);
        }
    }
    (void)width;
    (void)height;
}

inline std::string writeFloatRaster(const std::filesystem::path& dir, const std::string& fileName,
                                    int width, int height, const std::vector<float>& data,
                                    const RasterFixtureOptions& options = {}) {
    const size_t expectedPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (width <= 0 || height <= 0) {
        ADD_FAILURE() << "writeFloatRaster: invalid raster size " << width << "x" << height;
        return {};
    }
    // data 必须覆盖整幅栅格，否则 RasterIO 会越界读缓冲。
    if (data.size() < expectedPixels) {
        ADD_FAILURE() << "writeFloatRaster: data size " << data.size()
                      << " < width*height " << expectedPixels;
        return {};
    }
    const auto path = (dir / fileName).string();
    auto ds = gis::core::createRaster(path, width, height, 1, GDT_Float32);
    if (!ds) {
        ADD_FAILURE() << "writeFloatRaster: cannot create " << path;
        return {};
    }
    applyFixtureHeader(ds.get(), options, width, height);
    auto* band = ds->GetRasterBand(1);
    if (!band) {
        ADD_FAILURE() << "writeFloatRaster: band 1 is null for " << path;
        return {};
    }
    band->RasterIO(GF_Write, 0, 0, width, height, const_cast<float*>(data.data()),
                   width, height, GDT_Float32, 0, 0);
    band->FlushCache();
    return path;
}

inline std::string writeConstantBandsRaster(const std::filesystem::path& dir,
                                            const std::string& fileName,
                                            int width, int height,
                                            const std::vector<float>& bandValues,
                                            const RasterFixtureOptions& options = {}) {
    if (width <= 0 || height <= 0 || bandValues.empty()) {
        ADD_FAILURE() << "writeConstantBandsRaster: invalid request (" << width << "x" << height
                      << ", bands=" << bandValues.size() << ")";
        return {};
    }
    const auto path = (dir / fileName).string();
    auto ds = gis::core::createRaster(path, width, height,
                                      static_cast<int>(bandValues.size()), GDT_Float32);
    if (!ds) {
        ADD_FAILURE() << "writeConstantBandsRaster: cannot create " << path;
        return {};
    }
    applyFixtureHeader(ds.get(), options, width, height);
    std::vector<float> row(static_cast<size_t>(width), 0.0f);
    for (size_t i = 0; i < bandValues.size(); ++i) {
        std::fill(row.begin(), row.end(), bandValues[i]);
        auto* band = ds->GetRasterBand(static_cast<int>(i) + 1);
        if (!band) {
            ADD_FAILURE() << "writeConstantBandsRaster: band " << (i + 1) << " is null";
            continue;
        }
        for (int y = 0; y < height; ++y) {
            band->RasterIO(GF_Write, 0, y, width, 1, row.data(), width, 1, GDT_Float32, 0, 0);
        }
        band->FlushCache();
    }
    return path;
}

inline float readPixel(const std::string& path, int bandIndex, int x, int y) {
    auto ds = gis::core::openRaster(path, true);
    if (!ds) {
        ADD_FAILURE() << "readPixel: cannot open " << path;
        return unreadablePixelValue();
    }
    auto* band = ds->GetRasterBand(bandIndex);
    if (!band) {
        ADD_FAILURE() << "readPixel: band " << bandIndex << " is null in " << path;
        return unreadablePixelValue();
    }
    float value = unreadablePixelValue();
    if (band->RasterIO(GF_Read, x, y, 1, 1, &value, 1, 1, GDT_Float32, 0, 0) != CE_None) {
        ADD_FAILURE() << "readPixel: RasterIO failed for " << path << " band " << bandIndex;
        return unreadablePixelValue();
    }
    return value;
}

inline int readBandCount(const std::string& path) {
    auto ds = gis::core::openRaster(path, true);
    if (!ds) {
        ADD_FAILURE() << "readBandCount: cannot open " << path;
        return -1;
    }
    return ds->GetRasterCount();
}

inline std::string readCrsAuthorityCode(const std::string& path) {
    auto ds = gis::core::openRaster(path, true);
    if (!ds) {
        ADD_FAILURE() << "readCrsAuthorityCode: cannot open " << path;
        return {};
    }
    auto* srs = ds->GetSpatialRef();
    if (!srs) return {};
    const char* code = srs->GetAuthorityCode(nullptr);
    if (!code) return {};
    std::string auth = srs->GetAuthorityName(nullptr) ? srs->GetAuthorityName(nullptr) : "";
    if (auth.empty()) auth = "EPSG";
    return auth + ":" + code;
}

inline std::array<double, 6> readGeoTransform(const std::string& path) {
    auto ds = gis::core::openRaster(path, true);
    // 读不到时返回 NaN，避免"打开失败"被误判成"变换是单位矩阵/零矩阵"。
    std::array<double, 6> gt{std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN()};
    if (!ds) {
        ADD_FAILURE() << "readGeoTransform: cannot open " << path;
        return gt;
    }
    ds->GetGeoTransform(gt.data());
    return gt;
}

inline bool readNoDataValue(const std::string& path, int bandIndex, double& outNoData) {
    auto ds = gis::core::openRaster(path, true);
    if (!ds) {
        ADD_FAILURE() << "readNoDataValue: cannot open " << path;
        return false;
    }
    auto* band = ds->GetRasterBand(bandIndex);
    if (!band) {
        ADD_FAILURE() << "readNoDataValue: band " << bandIndex << " is null in " << path;
        return false;
    }
    int found = 0;
    outNoData = band->GetNoDataValue(&found);
    return found != 0;
}

} // namespace gis::tests
