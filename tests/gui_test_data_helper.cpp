#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>
#include <gis/core/runtime_env.h>

#include <filesystem>
#include <fstream>
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

bool assignEpsg3857(GDALDataset* ds) {
    OGRSpatialReference srs;
    if (srs.importFromEPSG(3857) != OGRERR_NONE) {
        return false;
    }
    char* wkt = nullptr;
    if (srs.exportToWkt(&wkt) != OGRERR_NONE || !wkt) {
        return false;
    }
    ds->SetProjection(wkt);
    CPLFree(wkt);
    return true;
}

int makeNdviRaster(const std::string& outputPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(outputPath);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* ds = driver->Create(outputPath.c_str(), 24, 24, 6, GDT_Float32, nullptr);
    if (!ds) {
        std::cerr << "Failed to create NDVI raster: " << outputPath << "\n";
        return 1;
    }

    double geotransform[6] = {
        116.0, 0.0005, 0.0,
        40.0, 0.0, -0.0005
    };
    ds->SetGeoTransform(geotransform);

    std::vector<float> band1(24 * 24, 10.0f);
    std::vector<float> band2(24 * 24, 20.0f);
    std::vector<float> band3(24 * 24, 30.0f);
    std::vector<float> band4(24 * 24, 70.0f);
    std::vector<float> band5(24 * 24, 90.0f);
    std::vector<float> band6(24 * 24, 110.0f);
    const std::vector<std::vector<float>*> bands = {&band1, &band2, &band3, &band4, &band5, &band6};

    for (int i = 0; i < 6; ++i) {
        GDALRasterBand* band = ds->GetRasterBand(i + 1);
        if (!band) {
            GDALClose(ds);
            std::cerr << "Failed to get NDVI raster band\n";
            return 1;
        }
        if (band->RasterIO(
                GF_Write, 0, 0, 24, 24,
                bands[i]->data(), 24, 24, GDT_Float32,
                0, 0, nullptr) != CE_None) {
            GDALClose(ds);
            std::cerr << "Failed to write NDVI raster band\n";
            return 1;
        }
    }

    GDALClose(ds);
    return 0;
}

int makePansharpenInputs(const std::string& msPath, const std::string& panPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(msPath);
    ensureParentDir(panPath);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* msDs = driver->Create(msPath.c_str(), 30, 30, 3, GDT_Float32, nullptr);
    if (!msDs) {
        std::cerr << "Failed to create multispectral raster: " << msPath << "\n";
        return 1;
    }

    GDALDataset* panDs = driver->Create(panPath.c_str(), 30, 30, 1, GDT_Float32, nullptr);
    if (!panDs) {
        GDALClose(msDs);
        std::cerr << "Failed to create panchromatic raster: " << panPath << "\n";
        return 1;
    }

    double geotransform[6] = {
        116.0, 0.0005, 0.0,
        40.0, 0.0, -0.0005
    };
    msDs->SetGeoTransform(geotransform);
    panDs->SetGeoTransform(geotransform);

    std::vector<float> msBand1(30 * 30, 30.0f);
    std::vector<float> msBand2(30 * 30, 50.0f);
    std::vector<float> msBand3(30 * 30, 70.0f);
    std::vector<float> panBand(30 * 30, 120.0f);
    const std::vector<std::vector<float>*> msBands = {&msBand1, &msBand2, &msBand3};

    for (int i = 0; i < 3; ++i) {
        GDALRasterBand* band = msDs->GetRasterBand(i + 1);
        if (!band) {
            GDALClose(msDs);
            GDALClose(panDs);
            std::cerr << "Failed to get multispectral band\n";
            return 1;
        }
        if (band->RasterIO(
                GF_Write, 0, 0, 30, 30,
                msBands[i]->data(), 30, 30, GDT_Float32,
                0, 0, nullptr) != CE_None) {
            GDALClose(msDs);
            GDALClose(panDs);
            std::cerr << "Failed to write multispectral band\n";
            return 1;
        }
    }

    GDALRasterBand* panRasterBand = panDs->GetRasterBand(1);
    if (!panRasterBand) {
        GDALClose(msDs);
        GDALClose(panDs);
        std::cerr << "Failed to get pan band\n";
        return 1;
    }
    if (panRasterBand->RasterIO(
            GF_Write, 0, 0, 30, 30,
            panBand.data(), 30, 30, GDT_Float32,
            0, 0, nullptr) != CE_None) {
        GDALClose(msDs);
        GDALClose(panDs);
        std::cerr << "Failed to write pan band\n";
        return 1;
    }

    GDALClose(msDs);
    GDALClose(panDs);
    return 0;
}

int makeMatchingInputs(const std::string& referencePath, const std::string& changedPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(referencePath);
    ensureParentDir(changedPath);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* refDs = driver->Create(referencePath.c_str(), 96, 96, 1, GDT_Byte, nullptr);
    if (!refDs) {
        std::cerr << "Failed to create matching reference raster: " << referencePath << "\n";
        return 1;
    }

    GDALDataset* changedDs = driver->Create(changedPath.c_str(), 96, 96, 1, GDT_Byte, nullptr);
    if (!changedDs) {
        GDALClose(refDs);
        std::cerr << "Failed to create matching input raster: " << changedPath << "\n";
        return 1;
    }

    double geotransform[6] = {
        116.0, 0.0005, 0.0,
        40.0, 0.0, -0.0005
    };
    refDs->SetGeoTransform(geotransform);
    changedDs->SetGeoTransform(geotransform);

    std::vector<unsigned char> reference(96 * 96, 0);
    for (int y = 8; y < 88; ++y) {
        for (int x = 8; x < 88; ++x) {
            if ((x >= 18 && x <= 34 && y >= 18 && y <= 34) ||
                (x >= 54 && x <= 78 && y >= 20 && y <= 28) ||
                (x >= 52 && x <= 60 && y >= 20 && y <= 70) ||
                ((x % 12) <= 2 && (y % 12) <= 2 && x >= 12 && y >= 12) ||
                ((x + 2 * y) % 19 == 0 && x >= 10 && y >= 10) ||
                (x == y) ||
                (x + y == 95)) {
                reference[y * 96 + x] = 255;
            }
        }
    }

    std::vector<unsigned char> changed = reference;
    for (int y = 36; y < 48; ++y) {
        for (int x = 28; x < 44; ++x) {
            changed[y * 96 + x] = 180;
        }
    }

    GDALRasterBand* refBand = refDs->GetRasterBand(1);
    GDALRasterBand* changedBand = changedDs->GetRasterBand(1);
    if (!refBand || !changedBand) {
        GDALClose(refDs);
        GDALClose(changedDs);
        std::cerr << "Failed to get matching raster band\n";
        return 1;
    }

    if (refBand->RasterIO(
            GF_Write, 0, 0, 96, 96,
            reference.data(), 96, 96, GDT_Byte,
            0, 0, nullptr) != CE_None ||
        changedBand->RasterIO(
            GF_Write, 0, 0, 96, 96,
            changed.data(), 96, 96, GDT_Byte,
            0, 0, nullptr) != CE_None) {
        GDALClose(refDs);
        GDALClose(changedDs);
        std::cerr << "Failed to write matching raster values\n";
        return 1;
    }

    GDALClose(refDs);
    GDALClose(changedDs);
    return 0;
}

int makeMatchingStitchInputs(const std::string& firstPath, const std::string& secondPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(firstPath);
    ensureParentDir(secondPath);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* firstDs = driver->Create(firstPath.c_str(), 96, 96, 1, GDT_Byte, nullptr);
    if (!firstDs) {
        std::cerr << "Failed to create first stitch raster: " << firstPath << "\n";
        return 1;
    }

    GDALDataset* secondDs = driver->Create(secondPath.c_str(), 96, 96, 1, GDT_Byte, nullptr);
    if (!secondDs) {
        GDALClose(firstDs);
        std::cerr << "Failed to create second stitch raster: " << secondPath << "\n";
        return 1;
    }

    double geotransform[6] = {
        116.0, 0.0005, 0.0,
        40.0, 0.0, -0.0005
    };
    firstDs->SetGeoTransform(geotransform);
    secondDs->SetGeoTransform(geotransform);

    std::vector<unsigned char> canvas(128 * 128, 0);
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            unsigned char value = static_cast<unsigned char>((x * 9 + y * 7) % 180);
            if ((x >= 18 && x <= 42 && y >= 18 && y <= 44) ||
                (x >= 70 && x <= 104 && y >= 26 && y <= 38) ||
                (x >= 58 && x <= 66 && y >= 18 && y <= 92) ||
                ((x + y) % 17 == 0) ||
                ((x - 2 * y + 256) % 23 == 0) ||
                (std::abs(x - y) <= 1)) {
                value = 255;
            }
            canvas[y * 128 + x] = value;
        }
    }

    auto extractWindow = [&](int startX, int startY) {
        std::vector<unsigned char> out(96 * 96, 0);
        for (int y = 0; y < 96; ++y) {
            for (int x = 0; x < 96; ++x) {
                out[y * 96 + x] = canvas[(startY + y) * 128 + (startX + x)];
            }
        }
        return out;
    };

    std::vector<unsigned char> first = extractWindow(0, 0);
    std::vector<unsigned char> second = extractWindow(24, 8);

    GDALRasterBand* firstBand = firstDs->GetRasterBand(1);
    GDALRasterBand* secondBand = secondDs->GetRasterBand(1);
    if (!firstBand || !secondBand) {
        GDALClose(firstDs);
        GDALClose(secondDs);
        std::cerr << "Failed to get stitch raster bands\n";
        return 1;
    }

    if (firstBand->RasterIO(
            GF_Write, 0, 0, 96, 96,
            first.data(), 96, 96, GDT_Byte,
            0, 0, nullptr) != CE_None ||
        secondBand->RasterIO(
            GF_Write, 0, 0, 96, 96,
            second.data(), 96, 96, GDT_Byte,
            0, 0, nullptr) != CE_None) {
        GDALClose(firstDs);
        GDALClose(secondDs);
        std::cerr << "Failed to write stitch raster data\n";
        return 1;
    }

    GDALClose(firstDs);
    GDALClose(secondDs);
    return 0;
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

    if (!assignEpsg3857(ds)) {
        GDALClose(ds);
        std::cerr << "Failed to create EPSG:3857 SRS\n";
        return 1;
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

int makeClassificationInputs(const std::string& vectorPath,
                             const std::string& classMapPath,
                             const std::string& rasterPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(vectorPath);
    ensureParentDir(classMapPath);
    ensureParentDir(rasterPath);

    {
        std::ofstream ofs(classMapPath, std::ios::binary);
        if (!ofs.is_open()) {
            std::cerr << "Failed to create class map: " << classMapPath << "\n";
            return 1;
        }
        ofs << "{\n"
            << "  \"1\": \"parcel\"\n"
            << "}\n";
    }

    GDALDriver* vectorDriver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!vectorDriver) {
        std::cerr << "Missing GPKG driver\n";
        return 1;
    }

    GDALDataset* vectorDs = vectorDriver->Create(vectorPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!vectorDs) {
        std::cerr << "Failed to create classification vector: " << vectorPath << "\n";
        return 1;
    }

    OGRSpatialReference srs;
    if (srs.importFromEPSG(3857) != OGRERR_NONE) {
        GDALClose(vectorDs);
        std::cerr << "Failed to create vector EPSG:3857 SRS\n";
        return 1;
    }

    OGRLayer* layer = vectorDs->CreateLayer("features", &srs, wkbPolygon, nullptr);
    if (!layer) {
        GDALClose(vectorDs);
        std::cerr << "Failed to create classification layer\n";
        return 1;
    }

    OGRFieldDefn idField("id", OFTString);
    OGRFieldDefn nameField("name", OFTString);
    if (layer->CreateField(&idField) != OGRERR_NONE ||
        layer->CreateField(&nameField) != OGRERR_NONE) {
        GDALClose(vectorDs);
        std::cerr << "Failed to create classification fields\n";
        return 1;
    }

    OGRFeature* feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
    if (!feature) {
        GDALClose(vectorDs);
        std::cerr << "Failed to create classification feature\n";
        return 1;
    }
    feature->SetField("id", "f1");
    feature->SetField("name", "area1");

    OGRPolygon polygon;
    OGRLinearRing ring;
    ring.addPoint(12935000.0, 4852300.0);
    ring.addPoint(12935300.0, 4852300.0);
    ring.addPoint(12935300.0, 4852000.0);
    ring.addPoint(12935000.0, 4852000.0);
    ring.addPoint(12935000.0, 4852300.0);
    polygon.addRing(&ring);
    feature->SetGeometry(&polygon);

    if (layer->CreateFeature(feature) != OGRERR_NONE) {
        OGRFeature::DestroyFeature(feature);
        GDALClose(vectorDs);
        std::cerr << "Failed to write classification feature\n";
        return 1;
    }

    OGRFeature::DestroyFeature(feature);
    GDALClose(vectorDs);
    return makeClassRaster(rasterPath);
}

int makeAnalysisRaster(const std::string& outputPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(outputPath);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* ds = driver->Create(outputPath.c_str(), 32, 32, 3, GDT_Float32, nullptr);
    if (!ds) {
        std::cerr << "Failed to create analysis raster: " << outputPath << "\n";
        return 1;
    }

    double geotransform[6] = {
        116.0, 0.0005, 0.0,
        40.0, 0.0, -0.0005
    };
    ds->SetGeoTransform(geotransform);

    std::vector<float> band1(32 * 32, 0.0f);
    std::vector<float> band2(32 * 32, 0.0f);
    std::vector<float> band3(32 * 32, 0.0f);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const size_t index = static_cast<size_t>(y * 32 + x);
            band1[index] = static_cast<float>(x + y);
            band2[index] = static_cast<float>(x * 2 - y);
            band3[index] = static_cast<float>((x * y) % 17);
        }
    }

    const std::vector<std::vector<float>*> bands = {&band1, &band2, &band3};
    for (int i = 0; i < 3; ++i) {
        GDALRasterBand* band = ds->GetRasterBand(i + 1);
        if (!band) {
            GDALClose(ds);
            std::cerr << "Failed to get analysis raster band\n";
            return 1;
        }
        if (band->RasterIO(
                GF_Write, 0, 0, 32, 32,
                bands[i]->data(), 32, 32, GDT_Float32,
                0, 0, nullptr) != CE_None) {
            GDALClose(ds);
            std::cerr << "Failed to write analysis raster band\n";
            return 1;
        }
    }

    GDALClose(ds);
    return 0;
}

int makeTerrainRaster(const std::string& outputPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(outputPath);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* ds = driver->Create(outputPath.c_str(), 48, 48, 1, GDT_Float32, nullptr);
    if (!ds) {
        std::cerr << "Failed to create terrain raster: " << outputPath << "\n";
        return 1;
    }

    double geotransform[6] = {
        116.0, 0.0005, 0.0,
        40.0, 0.0, -0.0005
    };
    ds->SetGeoTransform(geotransform);

    std::vector<float> dem(48 * 48, 0.0f);
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 48; ++x) {
            const size_t index = static_cast<size_t>(y * 48 + x);
            dem[index] = static_cast<float>(x * 1.5 + y * 0.8 + ((x * y) % 11) * 0.2);
        }
    }

    GDALRasterBand* band = ds->GetRasterBand(1);
    if (!band) {
        GDALClose(ds);
        std::cerr << "Failed to get terrain raster band\n";
        return 1;
    }
    if (band->RasterIO(
            GF_Write, 0, 0, 48, 48,
            dem.data(), 48, 48, GDT_Float32,
            0, 0, nullptr) != CE_None) {
        GDALClose(ds);
        std::cerr << "Failed to write terrain raster band\n";
        return 1;
    }

    GDALClose(ds);
    return 0;
}

int makeSupervisedClassificationInputs(const std::string& rasterPath, const std::string& csvPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(rasterPath);
    ensureParentDir(csvPath);

    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* ds = driver->Create(rasterPath.c_str(), 24, 12, 2, GDT_Float32, nullptr);
    if (!ds) {
        std::cerr << "Failed to create supervised classification raster: " << rasterPath << "\n";
        return 1;
    }

    double geotransform[6] = {
        116.0, 0.001, 0.0,
        40.0, 0.0, -0.001
    };
    ds->SetGeoTransform(geotransform);

    std::vector<float> band1(24 * 12, 0.0f);
    std::vector<float> band2(24 * 12, 0.0f);
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 24; ++x) {
            const bool leftClass = x < 12;
            band1[y * 24 + x] = leftClass ? 10.0f : 200.0f;
            band2[y * 24 + x] = leftClass ? 20.0f : 180.0f;
        }
    }

    GDALRasterBand* rasterBand1 = ds->GetRasterBand(1);
    GDALRasterBand* rasterBand2 = ds->GetRasterBand(2);
    if (!rasterBand1 || !rasterBand2) {
        GDALClose(ds);
        std::cerr << "Failed to get supervised classification raster bands\n";
        return 1;
    }

    if (rasterBand1->RasterIO(
            GF_Write, 0, 0, 24, 12,
            band1.data(), 24, 12, GDT_Float32,
            0, 0, nullptr) != CE_None ||
        rasterBand2->RasterIO(
            GF_Write, 0, 0, 24, 12,
            band2.data(), 24, 12, GDT_Float32,
            0, 0, nullptr) != CE_None) {
        GDALClose(ds);
        std::cerr << "Failed to write supervised classification raster bands\n";
        return 1;
    }

    GDALClose(ds);

    std::ofstream ofs(csvPath, std::ios::binary);
    if (!ofs) {
        std::cerr << "Failed to create supervised classification CSV: " << csvPath << "\n";
        return 1;
    }
    ofs << "label,b1,b2\n";
    ofs << "1,10,20\n";
    ofs << "1,12,18\n";
    ofs << "2,200,180\n";
    ofs << "2,195,185\n";
    return 0;
}

int makeGeorefGcpInputs(const std::string& rasterPath, const std::string& csvPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(rasterPath);
    ensureParentDir(csvPath);

    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* ds = driver->Create(rasterPath.c_str(), 10, 6, 1, GDT_Float32, nullptr);
    if (!ds) {
        std::cerr << "Failed to create GCP raster: " << rasterPath << "\n";
        return 1;
    }

    double geotransform[6] = {
        120.0, 1.0, 0.0,
        30.0, 0.0, -1.0
    };
    ds->SetGeoTransform(geotransform);

    std::vector<float> data(10 * 6, 7.0f);
    GDALRasterBand* band = ds->GetRasterBand(1);
    if (!band) {
        GDALClose(ds);
        std::cerr << "Failed to get GCP raster band\n";
        return 1;
    }
    if (band->RasterIO(
            GF_Write, 0, 0, 10, 6,
            data.data(), 10, 6, GDT_Float32,
            0, 0, nullptr) != CE_None) {
        GDALClose(ds);
        std::cerr << "Failed to write GCP raster band\n";
        return 1;
    }

    GDALClose(ds);

    std::ofstream ofs(csvPath, std::ios::binary);
    if (!ofs) {
        std::cerr << "Failed to create GCP CSV: " << csvPath << "\n";
        return 1;
    }
    ofs << "pixel_x,pixel_y,map_x,map_y\n";
    ofs << "0,0,120,30\n";
    ofs << "9,0,129,30\n";
    ofs << "0,5,120,25\n";
    ofs << "9,5,129,25\n";
    return 0;
}

int makeGeorefRadiometricInputs(const std::string& rasterPath, const std::string& metadataPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(rasterPath);
    ensureParentDir(metadataPath);

    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* ds = driver->Create(rasterPath.c_str(), 16, 16, 1, GDT_Float32, nullptr);
    if (!ds) {
        std::cerr << "Failed to create radiometric raster: " << rasterPath << "\n";
        return 1;
    }

    std::vector<float> data(16 * 16, 0.0f);
    for (int i = 0; i < 16 * 16; ++i) {
        data[static_cast<size_t>(i)] = static_cast<float>(i % 256);
    }

    GDALRasterBand* band = ds->GetRasterBand(1);
    if (!band) {
        GDALClose(ds);
        std::cerr << "Failed to get radiometric raster band\n";
        return 1;
    }
    if (band->RasterIO(
            GF_Write, 0, 0, 16, 16,
            data.data(), 16, 16, GDT_Float32,
            0, 0, nullptr) != CE_None) {
        GDALClose(ds);
        std::cerr << "Failed to write radiometric raster band\n";
        return 1;
    }

    GDALClose(ds);

    std::ofstream ofs(metadataPath, std::ios::binary);
    if (!ofs) {
        std::cerr << "Failed to create radiometric metadata file: " << metadataPath << "\n";
        return 1;
    }
    ofs << "RADIANCE_MULT_BAND_1 = 3.0\n";
    ofs << "RADIANCE_ADD_BAND_1 = 2.0\n";
    return 0;
}

int makeGeorefTopographicInputs(const std::string& inputPath,
                                const std::string& slopePath,
                                const std::string& aspectPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(inputPath);
    ensureParentDir(slopePath);
    ensureParentDir(aspectPath);

    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    auto writeConstantRaster = [&](const std::string& path, float value) -> bool {
        GDALDataset* ds = driver->Create(path.c_str(), 10, 6, 1, GDT_Float32, nullptr);
        if (!ds) {
            return false;
        }
        std::vector<float> data(10 * 6, value);
        GDALRasterBand* band = ds->GetRasterBand(1);
        const bool ok = band && band->RasterIO(
            GF_Write, 0, 0, 10, 6,
            data.data(), 10, 6, GDT_Float32,
            0, 0, nullptr) == CE_None;
        GDALClose(ds);
        return ok;
    };

    if (!writeConstantRaster(inputPath, 10.0f) ||
        !writeConstantRaster(slopePath, 60.0f) ||
        !writeConstantRaster(aspectPath, 90.0f)) {
        std::cerr << "Failed to create topographic correction rasters\n";
        return 1;
    }

    return 0;
}

int makeGeorefQuacInput(const std::string& rasterPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(rasterPath);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* ds = driver->Create(rasterPath.c_str(), 8, 8, 3, GDT_Float32, nullptr);
    if (!ds) {
        std::cerr << "Failed to create QUAC raster: " << rasterPath << "\n";
        return 1;
    }

    const std::vector<float> values = {20.0f, 60.0f, 100.0f};
    for (int i = 0; i < 3; ++i) {
        std::vector<float> bandData(8 * 8, values[static_cast<size_t>(i)]);
        GDALRasterBand* band = ds->GetRasterBand(i + 1);
        if (!band || band->RasterIO(
                GF_Write, 0, 0, 8, 8,
                bandData.data(), 8, 8, GDT_Float32,
                0, 0, nullptr) != CE_None) {
            GDALClose(ds);
            std::cerr << "Failed to write QUAC raster band\n";
            return 1;
        }
    }

    GDALClose(ds);
    return 0;
}

int makeGeorefRpcInput(const std::string& rasterPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(rasterPath);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* ds = driver->Create(rasterPath.c_str(), 10, 6, 1, GDT_Float32, nullptr);
    if (!ds) {
        std::cerr << "Failed to create RPC raster: " << rasterPath << "\n";
        return 1;
    }

    std::vector<float> data(10 * 6, 9.0f);
    GDALRasterBand* band = ds->GetRasterBand(1);
    if (!band || band->RasterIO(
            GF_Write, 0, 0, 10, 6,
            data.data(), 10, 6, GDT_Float32,
            0, 0, nullptr) != CE_None) {
        GDALClose(ds);
        std::cerr << "Failed to write RPC raster band\n";
        return 1;
    }

    const bool ok =
        ds->SetMetadataItem("ERR_BIAS", "0", "RPC") == CE_None &&
        ds->SetMetadataItem("ERR_RAND", "0", "RPC") == CE_None &&
        ds->SetMetadataItem("LINE_OFF", "2.5", "RPC") == CE_None &&
        ds->SetMetadataItem("SAMP_OFF", "4.5", "RPC") == CE_None &&
        ds->SetMetadataItem("LAT_OFF", "29.9975", "RPC") == CE_None &&
        ds->SetMetadataItem("LONG_OFF", "120.0045", "RPC") == CE_None &&
        ds->SetMetadataItem("HEIGHT_OFF", "0", "RPC") == CE_None &&
        ds->SetMetadataItem("LINE_SCALE", "2.5", "RPC") == CE_None &&
        ds->SetMetadataItem("SAMP_SCALE", "4.5", "RPC") == CE_None &&
        ds->SetMetadataItem("LAT_SCALE", "0.0025", "RPC") == CE_None &&
        ds->SetMetadataItem("LONG_SCALE", "0.0045", "RPC") == CE_None &&
        ds->SetMetadataItem("HEIGHT_SCALE", "1", "RPC") == CE_None &&
        ds->SetMetadataItem(
            "LINE_NUM_COEFF",
            "0 0 -1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", "RPC") == CE_None &&
        ds->SetMetadataItem(
            "LINE_DEN_COEFF",
            "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", "RPC") == CE_None &&
        ds->SetMetadataItem(
            "SAMP_NUM_COEFF",
            "0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", "RPC") == CE_None &&
        ds->SetMetadataItem(
            "SAMP_DEN_COEFF",
            "1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", "RPC") == CE_None;

    GDALClose(ds);

    if (!ok) {
        std::cerr << "Failed to write RPC metadata\n";
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: gui_test_data_helper <command> <args...>\n";
        return 1;
    }

    const std::string command = argv[1];
    if (command == "class-raster") {
        return makeClassRaster(argv[2]);
    }
    if (command == "ndvi-raster") {
        return makeNdviRaster(argv[2]);
    }
    if (command == "pansharpen-inputs") {
        if (argc < 4) {
            std::cerr << "Usage: gui_test_data_helper pansharpen-inputs <ms_output> <pan_output>\n";
            return 1;
        }
        return makePansharpenInputs(argv[2], argv[3]);
    }
    if (command == "matching-inputs") {
        if (argc < 4) {
            std::cerr << "Usage: gui_test_data_helper matching-inputs <reference_output> <changed_output>\n";
            return 1;
        }
        return makeMatchingInputs(argv[2], argv[3]);
    }
    if (command == "matching-stitch-inputs") {
        if (argc < 4) {
            std::cerr << "Usage: gui_test_data_helper matching-stitch-inputs <first_output> <second_output>\n";
            return 1;
        }
        return makeMatchingStitchInputs(argv[2], argv[3]);
    }
    if (command == "classification-inputs") {
        if (argc < 5) {
            std::cerr << "Usage: gui_test_data_helper classification-inputs <vector_output> <class_map_output> <raster_output>\n";
            return 1;
        }
        return makeClassificationInputs(argv[2], argv[3], argv[4]);
    }
    if (command == "analysis-raster") {
        return makeAnalysisRaster(argv[2]);
    }
    if (command == "terrain-raster") {
        return makeTerrainRaster(argv[2]);
    }
    if (command == "classification-supervised-inputs") {
        if (argc < 4) {
            std::cerr << "Usage: gui_test_data_helper classification-supervised-inputs <raster_output> <csv_output>\n";
            return 1;
        }
        return makeSupervisedClassificationInputs(argv[2], argv[3]);
    }
    if (command == "georef-gcp-inputs") {
        if (argc < 4) {
            std::cerr << "Usage: gui_test_data_helper georef-gcp-inputs <raster_output> <csv_output>\n";
            return 1;
        }
        return makeGeorefGcpInputs(argv[2], argv[3]);
    }
    if (command == "georef-radiometric-inputs") {
        if (argc < 4) {
            std::cerr << "Usage: gui_test_data_helper georef-radiometric-inputs <raster_output> <metadata_output>\n";
            return 1;
        }
        return makeGeorefRadiometricInputs(argv[2], argv[3]);
    }
    if (command == "georef-topographic-inputs") {
        if (argc < 5) {
            std::cerr << "Usage: gui_test_data_helper georef-topographic-inputs <input_output> <slope_output> <aspect_output>\n";
            return 1;
        }
        return makeGeorefTopographicInputs(argv[2], argv[3], argv[4]);
    }
    if (command == "georef-quac-input") {
        return makeGeorefQuacInput(argv[2]);
    }
    if (command == "georef-rpc-input") {
        return makeGeorefRpcInput(argv[2]);
    }

    std::cerr << "Unknown command: " << command << "\n";
    return 1;
}
