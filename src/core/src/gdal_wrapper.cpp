#include <gis/core/gdal_wrapper.h>
#include <gis/core/error.h>
#include <gis/core/fs_support.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <gdal_pam.h>
#include <filesystem>
#include <mutex>
#include <chrono>

namespace gis::core {

namespace {

// N162/#156：createRaster 是 core 深处的共用出口、拿不到动作名上下文 ⇒ 动作名槽位
// 用兜底标签"栅格数据集写出"（非遗漏是选定形态：插件入口拿得到动作名的点位各自穿真名）。
void ensureParentDirectoryForFile(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto canonicalPath = fs::weakly_canonical(fs::path(path), ec);
    std::string normalizedPath = ec ? path : canonicalPath.string();

    const fs::path fsPath(normalizedPath);
    if (!fsPath.has_parent_path()) {
        return;
    }

    ensureOutputDirectory(fsPath.parent_path(), "栅格数据集写出");
}

} // namespace

void initGDAL() {
    static std::once_flag flag;
    std::call_once(flag, []() { GDALAllRegister(); });
}

void GdalDatasetDeleter::operator()(GDALDataset* ds) const {
    if (ds) GDALClose(ds);
}

GdalDatasetPtr openRaster(const std::string& path, bool readOnly) {
    initGDAL();

    namespace fs = std::filesystem;
    std::error_code ec;
    auto canonicalPath = fs::weakly_canonical(fs::path(path), ec);
    std::string normalizedPath = ec ? path : canonicalPath.string();

    auto* ds = static_cast<GDALDataset*>(
        GDALOpen(normalizedPath.c_str(), readOnly ? GA_ReadOnly : GA_Update));
    if (!ds) {
        throw GisError("Cannot open raster: " + path + " (" + CPLGetLastErrorMsg() + ")");
    }
    // N184：禁掉这份数据集的 PAM（同名 .aux.xml）读回。GDAL 既没有"按次打开"的 PAM 开关
    // （GDAL_OF_* 里没有），配置项也不能只圈住 GDALOpen——实测边车是**懒读**的：
    // 只在 GDALOpen 前后设 GDAL_PAM_ENABLED 时，GetNoDataValue 仍然报出边车里的 -9999，
    // 整进程设该配置项才压得住（那会连写盘一起关掉，不是本条要的）。
    // 所以改走**每数据集**的 GPF_DISABLED，作用域就是这一份句柄。
    if (auto* pam = dynamic_cast<GDALPamDataset*>(ds)) {
        pam->SetPamFlags(pam->GetPamFlags() | GPF_DISABLED);
    }
    return GdalDatasetPtr(ds);
}

GdalDatasetPtr createRaster(const std::string& path, int width, int height,
                            int bands, int gdalType, const std::string& driver) {
    initGDAL();
    ensureParentDirectoryForFile(path);
    auto* drv = GetGDALDriverManager()->GetDriverByName(driver.c_str());
    if (!drv) {
        throw GisError("Cannot get driver: " + driver);
    }
    auto* ds = drv->Create(path.c_str(), width, height, bands,
                           static_cast<GDALDataType>(gdalType), nullptr);
    if (!ds) {
        throw GisError("Cannot create raster: " + path + " (" + CPLGetLastErrorMsg() + ")");
    }
    return GdalDatasetPtr(ds);
}

void copySpatialRef(GDALDataset* src, GDALDataset* dst) {
    double adfGT[6];
    if (src->GetGeoTransform(adfGT) == CE_None) {
        dst->SetGeoTransform(adfGT);
    }
    const char* proj = src->GetProjectionRef();
    if (proj && proj[0] != '\0') {
        dst->SetProjection(proj);
    }
}

std::unique_ptr<OGRSpatialReference> parseSRS(const std::string& srs) {
    auto srsObj = std::make_unique<OGRSpatialReference>();
    if (isEpsgCode(srs)) {
        int epsg = parseEpsgCode(srs);
        if (srsObj->importFromEPSG(epsg) != OGRERR_NONE) {
            throw GisError("Invalid EPSG code: " + srs);
        }
    } else {
        if (srsObj->importFromWkt(srs.c_str()) != OGRERR_NONE) {
            throw GisError("Invalid WKT or EPSG code: " + srs);
        }
    }
    srsObj->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    return srsObj;
}

std::string getSRSWKT(GDALDataset* ds) {
    const char* proj = ds->GetProjectionRef();
    return (proj && proj[0] != '\0') ? std::string(proj) : std::string();
}

BandStats computeBandStats(GDALDataset* ds, int bandIndex) {
    auto* band = ds->GetRasterBand(bandIndex);
    if (!band) {
        throw GisError("Cannot get band " + std::to_string(bandIndex));
    }

    BandStats stats{};
    int bGotMin = 0, bGotMax = 0;
    double minVal = band->GetMinimum(&bGotMin);
    double maxVal = band->GetMaximum(&bGotMax);

    if (!bGotMin || !bGotMax) {
        band->ComputeStatistics(FALSE, &minVal, &maxVal, &stats.mean, &stats.stddev, nullptr, nullptr);
    } else {
        double mean, stddev;
        band->ComputeStatistics(FALSE, &minVal, &maxVal, &mean, &stddev, nullptr, nullptr);
        stats.mean = mean;
        stats.stddev = stddev;
    }

    stats.minVal = minVal;
    stats.maxVal = maxVal;

    int hasNoData = 0;
    stats.noDataValue = band->GetNoDataValue(&hasNoData);
    stats.hasNoData = hasNoData != 0;
    stats.dataType = band->GetRasterDataType();
    switch (stats.dataType) {
        case GDT_Byte:    stats.dataTypeName = "Byte"; break;
        case GDT_UInt16:  stats.dataTypeName = "UInt16"; break;
        case GDT_Int16:   stats.dataTypeName = "Int16"; break;
        case GDT_UInt32:  stats.dataTypeName = "UInt32"; break;
        case GDT_Int32:   stats.dataTypeName = "Int32"; break;
        case GDT_Float32: stats.dataTypeName = "Float32"; break;
        case GDT_Float64: stats.dataTypeName = "Float64"; break;
        default:          stats.dataTypeName = "Unknown"; break;
    }

    return stats;
}

std::vector<HistogramBin> computeHistogram(GDALDataset* ds, int bandIndex, int numBins) {
    auto* band = ds->GetRasterBand(bandIndex);
    if (!band) {
        throw GisError("Cannot get band " + std::to_string(bandIndex));
    }

    double minVal, maxVal;
    band->ComputeStatistics(FALSE, &minVal, &maxVal, nullptr, nullptr, nullptr, nullptr);

    if (minVal >= maxVal) {
        maxVal = minVal + 1.0;
    }

    std::vector<GUIntBig> histogramVec(numBins, 0);
    GUIntBig* histogram = histogramVec.data();
    CPLErr err = band->GetHistogram(minVal, maxVal, numBins, histogram,
                                     TRUE, FALSE, nullptr, nullptr);
    if (err != CE_None) {
        throw GisError("Failed to compute histogram: " + std::string(CPLGetLastErrorMsg()));
    }

    std::vector<HistogramBin> bins;
    bins.reserve(numBins);
    double binWidth = (maxVal - minVal) / numBins;
    for (int i = 0; i < numBins; ++i) {
        bins.push_back({minVal + i * binWidth, minVal + (i + 1) * binWidth, histogram[i]});
    }

    return bins;
}

RasterInfo getRasterInfo(GDALDataset* ds, const std::string& filePath) {
    RasterInfo info{};
    info.filePath = filePath;
    info.driver = ds->GetDriver() ? ds->GetDriver()->GetDescription() : "Unknown";
    info.width = ds->GetRasterXSize();
    info.height = ds->GetRasterYSize();
    info.bandCount = ds->GetRasterCount();
    // 批次 D / Task 8 白名单位点 A（**只加兜底理由注释，行为一律不改**）：这一句照旧丢弃返回值。
    // 为什么不升级成失败：本函数按值返回 RasterInfo，**没有失败通道**，改成抛错就是改可观测契约；
    //   而产品侧消费方现读只有 GUI 栅格预览一处（src/gui/raster_preview_widget.cpp:125，直接构造
    //   rasterInfo_，外层没有 try；它只在这些行里把像元尺寸与包框排进侧栏，:299 已经按
    //   `[1]/[5] 是否非零`决定"分辨率"那一行要不要显示），另一条是 tests/test_core.cpp:326 的
    //   同信息断言（:330 断像元原点 = 120.0，走的是**有**变换的夹具）⇒ 给"没有地理参考的普通影像"
    //   判死刑等于把预览面板整块打挂，那不在本刀批准的射程里（要补的是预览侧的"有无变换"标志，已挂 GUI 刀）。
    // 兜底到底是什么形态（现读 GDAL 3.12.4 源码，别按"取不到就是全 0"想当然）：
    //   上面 `RasterInfo info{};`（:176）把六个槽零初始化，所以最坏只可能是"全 0"或"GDAL 的默认填充值"，
    //   不会是未初始化内存——基类实现**在返回 CE_Failure 之前也先把默认恒等式 (0,1,0,0,0,1) 写进数组**
    //   （gcore/gdaldataset.cpp:1509 的赋值、函数体 :1506-:1512 + gcore/gdal_geotransform.h:47-:62 的成员默认值
    //    xorig=0/xscale=1/xrot=0/yorig=0/yrot=0/yscale=1），只有自定义该方法且失败前不碰数组的驱动才留调用方初值；
    //   ⇒ 于是预览侧对无参考影像可能显示"分辨率 1 × 1"这种伪值：这是**既有形态**，本刀不掩盖也不修，
    //     口径与守卫白名单 tests/geotransform_guard_allowlist.json 的 A 条 fallback/reason 逐字同源。
    ds->GetGeoTransform(info.geoTransform);
    info.crsWKT = getSRSWKT(ds);

    if (!info.crsWKT.empty()) {
        OGRSpatialReference srs;
        if (srs.importFromWkt(info.crsWKT.c_str()) == OGRERR_NONE) {
            const char* authName = srs.GetAuthorityName(nullptr);
            const char* authCode = srs.GetAuthorityCode(nullptr);
            if (authName && authCode) {
                info.crsAuth = std::string(authName) + ":" + std::string(authCode);
            }
        }
    }

    for (int i = 1; i <= info.bandCount; ++i) {
        info.bands.push_back(computeBandStats(ds, i));
    }

    return info;
}

bool setNoDataValue(GDALDataset* ds, int bandIndex, double value) {
    auto* band = ds->GetRasterBand(bandIndex);
    if (!band) return false;
    return band->SetNoDataValue(value) == CE_None;
}

double getNoDataValue(GDALDataset* ds, int bandIndex, bool* hasNoData) {
    auto* band = ds->GetRasterBand(bandIndex);
    if (!band) {
        if (hasNoData) *hasNoData = false;
        return 0.0;
    }
    int hasND = 0;
    double val = band->GetNoDataValue(&hasND);
    if (hasNoData) *hasNoData = (hasND != 0);
    return val;
}

bool buildOverviews(GDALDataset* ds, const std::vector<int>& levels,
                    const std::string& resampling,
                    ProgressReporter* progress) {
    std::vector<int> lvlVec = levels;
    if (lvlVec.empty()) {
        lvlVec = {2, 4, 8, 16, 32};
    }

    std::vector<int> overviewList;
    for (int lvl : lvlVec) {
        if (lvl > 1) overviewList.push_back(lvl);
    }

    if (overviewList.empty()) return false;

    CPLErr err = ds->BuildOverviews(resampling.c_str(),
        static_cast<int>(overviewList.size()), overviewList.data(),
        0, nullptr, nullptr, nullptr);

    return err == CE_None;
}

namespace {

std::string currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &timeT);
#else
    localtime_r(&timeT, &tmBuf);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmBuf);
    return std::string(buf);
}

} // namespace

bool writeProcessingMetadata(GDALDataset* ds, const ProcessingMetadata& metadata) {
    if (!ds) return false;

    char** papszMD = nullptr;
    // N179：键名/时间戳兜底/空值跳过语义收敛到 processingMetadataPairs 这唯一出口，
    // update 路径（本函数）与 raster_manage cog 的翻译期 -dmo 落点共用，不各养一套键名。
    for (const auto& [key, value] : processingMetadataPairs(metadata)) {
        papszMD = CSLSetNameValue(papszMD, key.c_str(), value.c_str());
    }

    CPLErr err = ds->SetMetadata(papszMD, "GIS_PROCESSING");
    CSLDestroy(papszMD);
    ds->FlushCache();
    return err == CE_None;
}

bool writeProcessingMetadata(const std::string& rasterPath, const ProcessingMetadata& metadata) {
    // N179：IGNORE_COG_LAYOUT_BREAK=YES 压制开关随真修复删除。它的唯一真实用户是本工具自己
    // 产出的 COG（update 重开写元数据、把布局作废），那条路已随 cog 改道消失；其余动作的产物
    // 是普通 GTiff（无 COG 结构头，压不压都照开），外部布局完好的 COG 在 openRaster 的
    // GA_Update 打开处本来就会被 GDAL 拒开（两态一致，本函数不是第一道闸）。已破坏的旧 COG
    // 产物（KNOWN_INCOMPATIBLE_EDITION=YES）不进拒开分支，update 路径照常可用，旧产物兼容。
    GDALDataset* raw = static_cast<GDALDataset*>(
        GDALOpenEx(rasterPath.c_str(), GDAL_OF_RASTER | GDAL_OF_UPDATE, nullptr, nullptr, nullptr));
    if (!raw) return false;
    bool ok = writeProcessingMetadata(raw, metadata);
    GDALClose(raw);
    return ok;
}

std::vector<std::pair<std::string, std::string>> processingMetadataPairs(
    const ProcessingMetadata& metadata) {
    std::vector<std::pair<std::string, std::string>> pairs;
    auto addItem = [&pairs](const char* key, const std::string& val) {
        if (!val.empty()) {
            pairs.emplace_back(key, val);
        }
    };

    addItem("GIS_SOURCE_FILE", metadata.sourceFile);
    addItem("GIS_SOURCE_CRS", metadata.sourceCrs);
    addItem("GIS_PROCESSING_ALGORITHM", metadata.processingAlgorithm);
    addItem("GIS_PROCESSING_VERSION", metadata.processingVersion);
    addItem("GIS_PROCESSING_TIME",
            metadata.processingTime.empty() ? currentTimestamp() : metadata.processingTime);

    for (const auto& [k, v] : metadata.algorithmParams) {
        addItem(("GIS_PARAM_" + k).c_str(), v);
    }
    return pairs;
}

ProcessingMetadata readProcessingMetadata(GDALDataset* ds) {
    ProcessingMetadata result;
    if (!ds) return result;

    char** papszMD = ds->GetMetadata("GIS_PROCESSING");
    if (!papszMD) return result;

    auto getItem = [&](const char* key) -> std::string {
        const char* val = CSLFetchNameValue(papszMD, key);
        return val ? std::string(val) : std::string();
    };

    result.sourceFile = getItem("GIS_SOURCE_FILE");
    result.sourceCrs = getItem("GIS_SOURCE_CRS");
    result.processingAlgorithm = getItem("GIS_PROCESSING_ALGORITHM");
    result.processingVersion = getItem("GIS_PROCESSING_VERSION");
    result.processingTime = getItem("GIS_PROCESSING_TIME");

    int count = CSLCount(papszMD);
    for (int i = 0; i < count; ++i) {
        std::string entry(papszMD[i]);
        auto pos = entry.find('=');
        if (pos == std::string::npos) continue;
        std::string key = entry.substr(0, pos);
        if (key.rfind("GIS_PARAM_", 0) == 0) {
            std::string paramKey = key.substr(10);
            result.algorithmParams[paramKey] = entry.substr(pos + 1);
        }
    }

    return result;
}

} // namespace gis::core
