#include <gis/core/gdal_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <filesystem>
#include <mutex>
#include <chrono>

namespace gis::core {

namespace {

void ensureParentDirectoryForFile(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto canonicalPath = fs::weakly_canonical(fs::path(path), ec);
    std::string normalizedPath = ec ? path : canonicalPath.string();

    const fs::path fsPath(normalizedPath);
    if (!fsPath.has_parent_path()) {
        return;
    }

    fs::create_directories(fsPath.parent_path());
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
    auto addItem = [&](const char* key, const std::string& val) {
        if (!val.empty()) {
            papszMD = CSLSetNameValue(papszMD, key, val.c_str());
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

    CPLErr err = ds->SetMetadata(papszMD, "GIS_PROCESSING");
    CSLDestroy(papszMD);
    ds->FlushCache();
    return err == CE_None;
}

bool writeProcessingMetadata(const std::string& rasterPath, const ProcessingMetadata& metadata) {
    char** openOptions = nullptr;
    openOptions = CSLSetNameValue(openOptions, "IGNORE_COG_LAYOUT_BREAK", "YES");
    GDALDataset* raw = static_cast<GDALDataset*>(
        GDALOpenEx(rasterPath.c_str(),
                   GDAL_OF_RASTER | GDAL_OF_UPDATE,
                   nullptr, openOptions, nullptr));
    CSLDestroy(openOptions);
    if (!raw) return false;
    bool ok = writeProcessingMetadata(raw, metadata);
    GDALClose(raw);
    return ok;
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
