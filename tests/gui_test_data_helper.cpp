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

// R2（批次 D）：raster_manage.overviews + raster_render.colormap 两条腿判据共用的**非方**夹具。
// 为什么不能直接用 makeAnalysisRaster（32x32x3）：它是方阵，而这两个动作的腿判据都要读宽高
// （金字塔的每级概览尺寸、伪彩色 Create(output, cols, rows) 的输出尺寸）——方阵上"宽高写反"是
// 等价变异，改了不红。
// ⚠ 撤回记录（修复轮 1）：这一节的第一版在这里与 docs/RELEASE.md、
//   docs/算法说明/栅格工具/伪彩色.md、tests/test_plugins.cpp、
//   tests/run_real_raster_regression.ps1 都写过"伪彩色走 cv::normalize 到 8 位整型时用的是
//   带百分位裁剪的极值估计，斜坡里每个值只占 1/256 = 0.39% 的像元会被裁掉，于是'极值映射到
//   0/255'这条本来能手推的关系被位移；把两个极值各做厚到 25%，裁剪够不着它们"。这条因果由
//   控制器与复审席各自独立否证，本席位在修复轮 1 复跑同样否证，原文撤回：
//   NORM_MINMAX 到 CV_8U 就是 t=(v-min)*255/(max-min) 的线性映射，链路里没有直方图也没有
//   百分位步骤。本席位复跑读数（本机 cv2 4.13.0）：64x64 的 i%256 斜坡（每桶恰占 0.390625%）
//   归一化后 min 0 / max 255 / unique 256，与精确线性期望不符的格子数 = 0；把两个极值各压到
//   只剩 1 个像元（0.104%）仍然是精确线性。产品二进制的同形状读数在
//   build/tmp/batchD/r2/arms/F1_product_info_ramp64x64.log（绿波段 Min:0 / Max:255 /
//   Mean:127.5 / StdDev:73.9003，等于独立手推的 (256^2-1)/12 开方）。
// 那为什么仍然用三级平台而不是斜坡？真实理由是**可推性**，不是裁剪：平台件只有 3 个档，
//   每档多少个像元能直接数（240/480/240），一阶与二阶矩都是两行闭式；斜坡件有 256 个档，
//   期望要靠求和顺序与浮点累加去凑、每一档还得单独问一次"舍入还是截断"⇒ 手推成本高、
//   残差不定。所以这条是**我选的夹具形状决定的**，不是产品决定的（换回斜坡也能判，只是推不起）。
// 形状与取值（与 tests/test_plugins.cpp 的 createStepLevelRaster 同一张表）：
//   40 x 24 单波段 Float32，x 以 20 分左右、y 以 12 分上下，
//   左上 = 0、右上 = 10、左下 = 51、右下 = 10
//   ⇒ 多重集 {0^240, 10^480, 51^240}，n = 960
//   Mean   = (480*10 + 240*51)/960 = 17040/960 = 17.75
//   E[x^2] = (480*100 + 240*2601)/960 = 672240/960 = 700.25
//   Var    = 700.25 - 17.75^2 = 385.1875，StdDev = sqrt(385.1875)
//   标度 255/(51-0) = 5.0（可精确表示）⇒ 归一化后 0 -> 0、10 -> 50、51 -> 255
int makeStepLevelRaster(const std::string& outputPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(outputPath);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    const int width = 40;
    const int height = 24;
    GDALDataset* ds = driver->Create(outputPath.c_str(), width, height, 1, GDT_Float32, nullptr);
    if (!ds) {
        std::cerr << "Failed to create step level raster: " << outputPath << "\n";
        return 1;
    }

    double geotransform[6] = {
        116.0, 0.001, 0.0,
        40.0, 0.0, -0.001
    };
    ds->SetGeoTransform(geotransform);

    std::vector<float> band1(static_cast<size_t>(width) * static_cast<size_t>(height), 0.0f);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool left = x < (width / 2);
            const bool top = y < (height / 2);
            float value = 10.0f;                // 右上 + 右下 = 10（占 50%）
            if (left && top) value = 0.0f;      // 左上 = 0（下极值，占 25%）
            if (left && !top) value = 51.0f;    // 左下 = 51（上极值，占 25%）
            band1[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = value;
        }
    }

    GDALRasterBand* band = ds->GetRasterBand(1);
    if (!band) {
        GDALClose(ds);
        std::cerr << "Failed to get step level raster band\n";
        return 1;
    }
    if (band->RasterIO(
            GF_Write, 0, 0, width, height,
            band1.data(), width, height, GDT_Float32,
            0, 0, nullptr) != CE_None) {
        GDALClose(ds);
        std::cerr << "Failed to write step level raster band\n";
        return 1;
    }

    GDALClose(ds);
    return 0;
}

int makeProcessingBinaryRaster(const std::string& outputPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(outputPath);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* ds = driver->Create(outputPath.c_str(), 64, 64, 1, GDT_Byte, nullptr);
    if (!ds) {
        std::cerr << "Failed to create processing binary raster: " << outputPath << "\n";
        return 1;
    }

    double geotransform[6] = {
        116.0, 0.0005, 0.0,
        40.0, 0.0, -0.0005
    };
    ds->SetGeoTransform(geotransform);

    std::vector<unsigned char> data(64 * 64, 0);
    auto fillRect = [&](int minX, int minY, int maxX, int maxY, unsigned char value) {
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                data[static_cast<size_t>(y * 64 + x)] = value;
            }
        }
    };

    fillRect(6, 6, 20, 20, 255);
    fillRect(28, 8, 44, 18, 255);
    fillRect(10, 34, 18, 54, 255);
    fillRect(34, 34, 54, 54, 255);
    for (int i = 0; i < 14; ++i) {
        data[static_cast<size_t>((24 + i) * 64 + (48 + i / 2))] = 255;
    }

    GDALRasterBand* band = ds->GetRasterBand(1);
    if (!band) {
        GDALClose(ds);
        std::cerr << "Failed to get processing binary raster band\n";
        return 1;
    }
    band->SetNoDataValue(0);
    if (band->RasterIO(
            GF_Write, 0, 0, 64, 64,
            data.data(), 64, 64, GDT_Byte,
            0, 0, nullptr) != CE_None) {
        GDALClose(ds);
        std::cerr << "Failed to write processing binary raster\n";
        return 1;
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

int makeTerrainSinkRaster(const std::string& outputPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(outputPath);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* ds = driver->Create(outputPath.c_str(), 9, 9, 1, GDT_Float32, nullptr);
    if (!ds) {
        std::cerr << "Failed to create terrain sink raster: " << outputPath << "\n";
        return 1;
    }

    double geotransform[6] = {
        116.0, 0.001, 0.0,
        40.0, 0.0, -0.001
    };
    ds->SetGeoTransform(geotransform);

    std::vector<float> data(9 * 9, 10.0f);
    data[4 * 9 + 4] = 2.0f;

    GDALRasterBand* band = ds->GetRasterBand(1);
    if (!band) {
        GDALClose(ds);
        std::cerr << "Failed to get terrain sink band\n";
        return 1;
    }
    if (band->RasterIO(
            GF_Write, 0, 0, 9, 9,
            data.data(), 9, 9, GDT_Float32,
            0, 0, nullptr) != CE_None) {
        GDALClose(ds);
        std::cerr << "Failed to write terrain sink raster\n";
        return 1;
    }

    GDALClose(ds);
    return 0;
}

int makeTerrainEastDownhillRaster(const std::string& outputPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    ensureParentDir(outputPath);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver) {
        std::cerr << "Missing GTiff driver\n";
        return 1;
    }

    GDALDataset* ds = driver->Create(outputPath.c_str(), 9, 9, 1, GDT_Float32, nullptr);
    if (!ds) {
        std::cerr << "Failed to create east downhill raster: " << outputPath << "\n";
        return 1;
    }

    double geotransform[6] = {
        116.0, 0.001, 0.0,
        40.0, 0.0, -0.001
    };
    ds->SetGeoTransform(geotransform);

    std::vector<float> data(9 * 9, 0.0f);
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 9; ++x) {
            data[y * 9 + x] = static_cast<float>(9 - x);
        }
    }

    GDALRasterBand* band = ds->GetRasterBand(1);
    if (!band) {
        GDALClose(ds);
        std::cerr << "Failed to get east downhill band\n";
        return 1;
    }
    if (band->RasterIO(
            GF_Write, 0, 0, 9, 9,
            data.data(), 9, 9, GDT_Float32,
            0, 0, nullptr) != CE_None) {
        GDALClose(ds);
        std::cerr << "Failed to write east downhill raster\n";
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

int printVectorFeatureCount(const std::string& inputPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();
    OGRRegisterAll();

    GDALDataset* ds = static_cast<GDALDataset*>(
        GDALOpenEx(inputPath.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
    if (!ds) {
        std::cerr << "Failed to open vector dataset: " << inputPath << "\n";
        return 1;
    }

    OGRLayer* layer = ds->GetLayer(0);
    if (!layer) {
        GDALClose(ds);
        std::cerr << "Failed to get first vector layer: " << inputPath << "\n";
        return 1;
    }

    std::cout << layer->GetFeatureCount();
    GDALClose(ds);
    return 0;
}

int printRasterBandCount(const std::string& inputPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(inputPath.c_str(), GA_ReadOnly));
    if (!ds) {
        std::cerr << "Failed to open raster dataset: " << inputPath << "\n";
        return 1;
    }

    std::cout << ds->GetRasterCount();
    GDALClose(ds);
    return 0;
}

int printRasterSize(const std::string& inputPath) {
    gis::core::initRuntimeEnvironment();
    GDALAllRegister();

    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(inputPath.c_str(), GA_ReadOnly));
    if (!ds) {
        std::cerr << "Failed to open raster dataset: " << inputPath << "\n";
        return 1;
    }

    std::cout << ds->GetRasterXSize() << "x" << ds->GetRasterYSize();
    GDALClose(ds);
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
    if (command == "step-level-raster") {
        return makeStepLevelRaster(argv[2]);
    }
    if (command == "processing-binary-raster") {
        return makeProcessingBinaryRaster(argv[2]);
    }
    if (command == "terrain-raster") {
        return makeTerrainRaster(argv[2]);
    }
    if (command == "terrain-sink-raster") {
        return makeTerrainSinkRaster(argv[2]);
    }
    if (command == "terrain-east-downhill-raster") {
        return makeTerrainEastDownhillRaster(argv[2]);
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
    if (command == "vector-feature-count") {
        return printVectorFeatureCount(argv[2]);
    }
    if (command == "raster-band-count") {
        return printRasterBandCount(argv[2]);
    }
    if (command == "raster-size") {
        return printRasterSize(argv[2]);
    }

    std::cerr << "Unknown command: " << command << "\n";
    return 1;
}
