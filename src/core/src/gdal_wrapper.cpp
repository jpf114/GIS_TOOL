#include <gis/core/gdal_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <filesystem>

namespace gis::core {

namespace {

void ensureParentDirectoryForFile(const std::string& path) {
    const std::filesystem::path fsPath(path);
    if (!fsPath.has_parent_path()) {
        return;
    }

    std::filesystem::create_directories(fsPath.parent_path());
}

} // namespace

void initGDAL() {
    static bool initialized = false;
    if (!initialized) {
        GDALAllRegister();
        initialized = true;
    }
}

void GdalDatasetDeleter::operator()(GDALDataset* ds) const {
    if (ds) GDALClose(ds);
}

GdalDatasetPtr openRaster(const std::string& path, bool readOnly) {
    initGDAL();
    auto* ds = static_cast<GDALDataset*>(
        GDALOpen(path.c_str(), readOnly ? GA_ReadOnly : GA_Update));
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

OGRSpatialReference* parseSRS(const std::string& srs) {
    auto* srsObj = new OGRSpatialReference();
    if (isEpsgCode(srs)) {
        int epsg = parseEpsgCode(srs);
        if (srsObj->importFromEPSG(epsg) != OGRERR_NONE) {
            delete srsObj;
            throw GisError("Invalid EPSG code: " + srs);
        }
    } else {
        // Treat as WKT
        if (srsObj->importFromWkt(srs.c_str()) != OGRERR_NONE) {
            delete srsObj;
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

    GUIntBig* histogram = new GUIntBig[numBins]();
    CPLErr err = band->GetHistogram(minVal, maxVal, numBins, histogram,
                                     TRUE, FALSE, nullptr, nullptr);
    if (err != CE_None) {
        delete[] histogram;
        throw GisError("Failed to compute histogram: " + std::string(CPLGetLastErrorMsg()));
    }

    std::vector<HistogramBin> bins;
    bins.reserve(numBins);
    double binWidth = (maxVal - minVal) / numBins;
    for (int i = 0; i < numBins; ++i) {
        bins.push_back({minVal + i * binWidth, minVal + (i + 1) * binWidth, histogram[i]});
    }

    delete[] histogram;
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

} // namespace gis::core
