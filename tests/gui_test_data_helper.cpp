#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>
#include <gis/core/runtime_env.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void ensureParentDir(const std::string& path) {
    const fs::path fsPath(path);
    if (fsPath.has_parent_path()) {
        fs::create_directories(fsPath.parent_path());
    }
}

int makeClassRaster(const std::string& outputPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(outputPath);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* ds = driver->Create(outputPath.c_str(), 6, 6, 1, GDT_Int32, nullptr);
    if (!ds) {
        std::cerr << "Failed to create raster: " << outputPath << "\n";
        return 1;
    }

    double geotransform[6] = {
        12935000.0, 50.0, 0.0,
        4852300.0, 0.0, -50.0
    };
    ds->SetGeoTransform(geotransform);

    OGRSpatialReference srs;
    if (srs.importFromEPSG(3857) != OGRERR_NONE) {
        GDALClose(ds);
        std::cerr << "Failed to create EPSG:3857 SRS\n";
        return 1;
    }
    char* wkt = nullptr;
    srs.exportToWkt(&wkt);
    if (wkt) {
        ds->SetProjection(wkt);
        CPLFree(wkt);
    }

    GDALRasterBand* band = ds->GetRasterBand(1);
    if (!band) {
        GDALClose(ds);
        std::cerr << "Failed to get raster band\n";
        return 1;
    }
    band->SetNoDataValue(0);

    std::vector<int> values(36, 1);
    if (band->RasterIO(
            GF_Write, 0, 0, 6, 6,
            values.data(), 6, 6, GDT_Int32,
            0, 0, nullptr) != CE_None) {
        GDALClose(ds);
        std::cerr << "Failed to write class raster values\n";
        return 1;
    }

    GDALClose(ds);
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: gui_test_data_helper class-raster <output>\n";
        return 1;
    }

    const std::string command = argv[1];
    if (command == "class-raster") {
        return makeClassRaster(argv[2]);
    }

    std::cerr << "Unknown command: " << command << "\n";
    return 1;
}
