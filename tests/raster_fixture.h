#pragma once

// 三态栅格测试夹具：CRS / NoData / 数据类型 / 地理变换 一次性写入，读回供数值断言。
//
// 数据缓冲约定（重要）：写入实现只支持 Float32 缓冲（data / bandValues / bandData 都是
// std::vector<float> 或其按波段嵌套形式），创建与写入一律按 GDT_Float32 进行。
// RasterFixtureOptions::dtype 是声明位而非可扩展开关：判定只看 dtypeSupported(options)
// 这一条纯谓词（D5：抽出来是为了能直接用断言锁住，不需要 gtest-spi 包语句），
// writeFloatRaster / writeConstantBandsRaster / writeFloatBandsRaster 三个写入函数
// 在入口调用它，非 GDT_Float32 一律当场 ADD_FAILURE() 并返回空串（不会静默按 Float32
// 写），整型夹具需要后续任务显式实现按 dtype 分支的创建与写入。
//
// 失败约定（重要，D1 更正）：gis::core::openRaster 失败抛 gis::core::GisError、
// gis::core::createRaster 失败同样抛 gis::core::GisError，两者都不返回 nullptr
// （见 src/core/src/gdal_wrapper.cpp）。另外 createRaster 先经
// ensureParentDirectoryForFile 调**会抛**的 fs::create_directories 重载，父目录建不出来时
// 抛的是 std::filesystem_error（GisError 派生自 std::runtime_error，两者同被 std::exception
// 接住，见 D4）。夹具在 openFixtureRaster / createFixtureRaster 内部把异常转成
// ADD_FAILURE()，再走各自的哨兵返回值（写入 → 空串；读取 → NaN / -1 / false）。
// 准确表述是：ADD_FAILURE() 报在**夹具自身那一行**（raster_fixture.h 的行号），
// 但落在调用方那条语句所属的用例里，因此用例仍然被记为失败、异常不会穿过测试体把行号与
// 消息报到别的用例去——"失败一定报在调用方那一行"是不实描述。
//
// 包含关系（D4 收尾）：两个 catch 的类型是 std::exception，故显式 #include <exception>，
// 不再依赖 <string>/<gtest> 的传递包含。本头文件**不**引用 gis::core::GisError 这个名字
// （异常消息只用 e.what()），所以不 include <gis/core/error.h>——上面注释里提到 GisError
// 只是在说明 core 侧的抛出口径，不需要它的声明参与编译。

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
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

// openRaster 的夹具包装：gis::core::openRaster 打不开时抛 GisError（不返回 nullptr），
// 这里把异常就地转成 ADD_FAILURE() 并返回空指针，让调用方继续走自己的哨兵分支。
// 返回空指针**只可能**是本函数 catch 的产物（见文件头的失败约定 D1）：调用方的判空保留，
// 是为了把"夹具内部已经报的失败"安全地转成哨兵返回值，不是为了覆盖"core 返回 nullptr"
// ——那条路径不存在（原注释如此描述属于不实，已更正）。
inline gis::core::GdalDatasetPtr openFixtureRaster(const std::string& path, const char* who) {
    try {
        return gis::core::openRaster(path, true);
    } catch (const std::exception& e) {   // D4：GisError 之外还要接住 std::filesystem_error
        ADD_FAILURE() << who << ": " << e.what() << " path=" << path;
        return {};
    }
}

// createRaster 的夹具包装，同上：写入函数用空串作为失败哨兵。
// catch 的类型必须是 std::exception（D4）：createRaster 内部先调
// ensureParentDirectoryForFile（src/core/src/gdal_wrapper.cpp:14-26），它用的是
// **会抛**的 fs::create_directories 重载，父目录建不出来时抛 std::filesystem_error，
// 只 catch GisError 会让它穿透进测试体。
inline gis::core::GdalDatasetPtr createFixtureRaster(const std::string& path, int width, int height,
                                                     int bands, GDALDataType dtype,
                                                     const char* who) {
    try {
        return gis::core::createRaster(path, width, height, bands, static_cast<int>(dtype));
    } catch (const std::exception& e) {   // D4
        ADD_FAILURE() << who << ": " << e.what() << " path=" << path;
        return {};
    }
}

struct RasterFixtureOptions {
    FixtureCrs crs = FixtureCrs::None;
    bool hasNoData = false;
    double noData = -9999.0;
    GDALDataType dtype = GDT_Float32;  // 只接受 GDT_Float32：其它值在写入函数入口被拒绝
    double originX = 116.0;
    double originY = 40.0;
    double pixelSize = 0.001;
};

// D5：把"本夹具只支持 Float32 缓冲"这条判据抽成无副作用的纯谓词。
// 三个写入函数的守卫都调它，于是这条判据既能被守卫使用、也能在测试里直接断言
// （不需要 gtest-spi 去捕获 ADD_FAILURE），锁住的是判据本身而不是消息文本。
inline bool dtypeSupported(const RasterFixtureOptions& options) {
    return options.dtype == GDT_Float32;
}

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
    // dtype 守卫放在最前：缓冲区是 float，按别的类型创建/写入会得到与调用方预期不符的
    // 栅格，因此宁可当场失败也不静默降级（判据本身是 dtypeSupported，详见文件头注释）。
    if (!dtypeSupported(options)) {
        ADD_FAILURE() << "fixture: dtype not supported (only GDT_Float32)";
        return {};
    }
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
    auto ds = createFixtureRaster(path, width, height, 1, GDT_Float32, "writeFloatRaster");
    if (!ds) {
        // D2：null 只可能来自 createFixtureRaster 的 catch（createRaster 抛异常后返回空指针），
        // 文案不能再归因成"createRaster 返回了 null"——那个函数根本不返回 null。
        ADD_FAILURE() << "writeFloatRaster: createFixtureRaster returned null "
                      << "(gis::core::createRaster threw, already reported by createFixtureRaster) for "
                      << path;
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
    // 同 writeFloatRaster：非 Float32 的 dtype 一律当场拒绝，不静默按 Float32 写。
    if (!dtypeSupported(options)) {
        ADD_FAILURE() << "fixture: dtype not supported (only GDT_Float32)";
        return {};
    }
    if (width <= 0 || height <= 0 || bandValues.empty()) {
        ADD_FAILURE() << "writeConstantBandsRaster: invalid request (" << width << "x" << height
                      << ", bands=" << bandValues.size() << ")";
        return {};
    }
    const auto path = (dir / fileName).string();
    auto ds = createFixtureRaster(path, width, height,
                                  static_cast<int>(bandValues.size()), GDT_Float32,
                                  "writeConstantBandsRaster");
    if (!ds) {
        // D2：归因见 writeFloatRaster 的同位置注释。
        ADD_FAILURE() << "writeConstantBandsRaster: createFixtureRaster returned null "
                      << "(gis::core::createRaster threw, already reported by createFixtureRaster) for "
                      << path;
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

// R-T6-3：按波段 + 按像元给定数据的写入器。
// 现有两个写入器表达不出"同一幅图里不同波段各有自己的像元分布"——writeFloatRaster 只建
// 1 个波段，writeConstantBandsRaster 只能逐波段常量。Task 6 的 interval 重分类用例需要
// band1={0,9}、band2={7,7} 这种**区间网格错位**的数据来制造跨波段污染，故新增本函数。
// 语义：建 bandData.size() 个 Float32 波段，第 i 波段写入 bandData[i] 的 width*height 个像元。
inline std::string writeFloatBandsRaster(const std::filesystem::path& dir, const std::string& fileName,
                                         int width, int height,
                                         const std::vector<std::vector<float>>& bandData,
                                         const RasterFixtureOptions& options = {}) {
    // 入口校验顺序与既有写入器同形：dtype → 尺寸 → 缓冲长度。
    if (!dtypeSupported(options)) {
        ADD_FAILURE() << "fixture: dtype not supported (only GDT_Float32)";
        return {};
    }
    if (width <= 0 || height <= 0) {
        ADD_FAILURE() << "writeFloatBandsRaster: invalid raster size " << width << "x" << height;
        return {};
    }
    if (bandData.empty()) {
        ADD_FAILURE() << "writeFloatBandsRaster: bandData is empty";
        return {};
    }
    const size_t expectedPixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < bandData.size(); ++i) {
        // 任一波段缓冲不足都会让 RasterIO 越界读，所以逐波段校验、一次报清是哪一段。
        if (bandData[i].size() < expectedPixels) {
            ADD_FAILURE() << "writeFloatBandsRaster: band " << i << " data size "
                          << bandData[i].size() << " < width*height " << expectedPixels;
            return {};
        }
    }
    const auto path = (dir / fileName).string();
    auto ds = createFixtureRaster(path, width, height,
                                  static_cast<int>(bandData.size()), GDT_Float32,
                                  "writeFloatBandsRaster");
    if (!ds) {
        // D2 同形文案：null 来自 createFixtureRaster 的 catch，不是 createRaster 的返回值。
        ADD_FAILURE() << "writeFloatBandsRaster: createFixtureRaster returned null "
                      << "(gis::core::createRaster threw, already reported by createFixtureRaster) for "
                      << path;
        return {};
    }
    applyFixtureHeader(ds.get(), options, width, height);
    for (size_t i = 0; i < bandData.size(); ++i) {
        auto* band = ds->GetRasterBand(static_cast<int>(i) + 1);
        if (!band) {
            ADD_FAILURE() << "writeFloatBandsRaster: band " << (i + 1) << " is null for " << path;
            continue;
        }
        band->RasterIO(GF_Write, 0, 0, width, height, const_cast<float*>(bandData[i].data()),
                       width, height, GDT_Float32, 0, 0);
        band->FlushCache();
    }
    return path;
}

inline float readPixel(const std::string& path, int bandIndex, int x, int y) {
    auto ds = openFixtureRaster(path, "readPixel");
    if (!ds) {
        // D2：openRaster 从不返回 null，null 全部来自 openFixtureRaster 的 catch。
        ADD_FAILURE() << "readPixel: openFixtureRaster returned null "
                      << "(gis::core::openRaster threw, already reported by openFixtureRaster) for "
                      << path;
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
    auto ds = openFixtureRaster(path, "readBandCount");
    if (!ds) {
        ADD_FAILURE() << "readBandCount: openFixtureRaster returned null "
                      << "(gis::core::openRaster threw, already reported by openFixtureRaster) for "
                      << path;
        return -1;
    }
    return ds->GetRasterCount();
}

inline std::string readCrsAuthorityCode(const std::string& path) {
    auto ds = openFixtureRaster(path, "readCrsAuthorityCode");
    if (!ds) {
        // 这里必须显式失败：空串同时是"文件里没有 CRS"的合法返回值，不报出来就分不清
        // "没有 CRS"和"根本没打开文件"（详见 test_fixtures.cpp 的 NoCrs 用例）。
        ADD_FAILURE() << "readCrsAuthorityCode: openFixtureRaster returned null "
                      << "(gis::core::openRaster threw, already reported by openFixtureRaster) for "
                      << path;
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
    auto ds = openFixtureRaster(path, "readGeoTransform");
    // 读不到时返回 NaN，避免"打开失败"被误判成"变换是单位矩阵/零矩阵"。
    std::array<double, 6> gt{std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::quiet_NaN()};
    if (!ds) {
        ADD_FAILURE() << "readGeoTransform: openFixtureRaster returned null "
                      << "(gis::core::openRaster threw, already reported by openFixtureRaster) for "
                      << path;
        return gt;
    }
    ds->GetGeoTransform(gt.data());
    return gt;
}

inline bool readNoDataValue(const std::string& path, int bandIndex, double& outNoData) {
    auto ds = openFixtureRaster(path, "readNoDataValue");
    if (!ds) {
        ADD_FAILURE() << "readNoDataValue: openFixtureRaster returned null "
                      << "(gis::core::openRaster threw, already reported by openFixtureRaster) for "
                      << path;
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
