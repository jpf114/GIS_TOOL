#include <gis/core/gdal_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>

namespace gis::core {

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

} // namespace gis::core
