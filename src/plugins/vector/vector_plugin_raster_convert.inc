gis::framework::Result VectorPlugin::doRasterize(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    double resolution = gis::framework::getParam<double>(params, "resolution", 1.0);
    std::string attribute = gis::framework::getParam<std::string>(params, "attribute", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (resolution <= 0) return gis::framework::Result::fail("resolution must be positive");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* layer = getLayer(srcDS, layerName);
    if (!layer) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    OGREnvelope extent;
    if (layer->GetExtent(&extent, TRUE) != OGRERR_NONE) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot get layer extent");
    }

    int xSize = static_cast<int>(std::ceil((extent.MaxX - extent.MinX) / resolution));
    int ySize = static_cast<int>(std::ceil((extent.MaxY - extent.MinY) / resolution));
    if (xSize <= 0 || ySize <= 0) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Invalid raster dimensions");
    }

    progress.onProgress(0.2);

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    ensureParentDirectoryForFile(output);
    GDALDataset* dstDS = drv->Create(output.c_str(), xSize, ySize, 1, GDT_Float32, nullptr);
    if (!dstDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output raster: " + output);
    }

    double adfGT[6];
    adfGT[0] = extent.MinX; adfGT[1] = resolution; adfGT[2] = 0;
    adfGT[3] = extent.MaxY; adfGT[4] = 0;          adfGT[5] = -resolution;
    dstDS->SetGeoTransform(adfGT);

    const OGRSpatialReference* srs = layer->GetSpatialRef();
    if (srs) {
        char* wkt = nullptr;
        srs->exportToWkt(&wkt);
        if (wkt) { dstDS->SetProjection(wkt); CPLFree(wkt); }
    }

    GDALRasterBand* band = dstDS->GetRasterBand(1);
    band->Fill(0);
    band->SetNoDataValue(0);

    progress.onProgress(0.3);

    std::vector<std::string> argStorage;
    if (!attribute.empty()) {
        argStorage.push_back("-a");
        argStorage.push_back(attribute);
    } else {
        argStorage.push_back("-burn");
        argStorage.push_back("1");
    }

    std::vector<const char*> rasterizeArgs;
    for (auto& a : argStorage) rasterizeArgs.push_back(a.c_str());
    rasterizeArgs.push_back(nullptr);

    GDALRasterizeOptions* rasterizeOpts = GDALRasterizeOptionsNew(
        const_cast<char**>(rasterizeArgs.data()), nullptr);
    if (!rasterizeOpts) {
        GDALClose(dstDS); GDALClose(srcDS);
        return gis::framework::Result::fail("Failed to create rasterize options");
    }

    int errCode = 0;
    GDALDatasetH dstHandle = GDALRasterize(output.c_str(),
        static_cast<GDALDatasetH>(dstDS),
        static_cast<GDALDatasetH>(srcDS),
        rasterizeOpts, &errCode);
    GDALRasterizeOptionsFree(rasterizeOpts);

    if (!dstHandle || errCode) {
        GDALClose(dstDS); GDALClose(srcDS);
        return gis::framework::Result::fail("Rasterize failed: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALClose(dstHandle); GDALClose(srcDS);
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Rasterize completed: " + std::to_string(xSize) + "x" + std::to_string(ySize) + " raster", output);
}

gis::framework::Result VectorPlugin::doPolygonize(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);

    auto srcDS = gis::core::openRaster(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open raster file: " + input);

    GDALRasterBand* rasterBand = srcDS->GetRasterBand(band);
    if (!rasterBand) return gis::framework::Result::fail("Cannot get band " + std::to_string(band));

    progress.onProgress(0.2);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) fs::remove(entry.path());
        }
    }

    ensureParentDirectoryForFile(output);
    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) return gis::framework::Result::fail("Cannot create output file: " + output);

    OGRSpatialReference* srs = nullptr;
    const char* pszWKT = srcDS->GetProjectionRef();
    if (pszWKT && pszWKT[0] != '\0') {
        srs = new OGRSpatialReference();
        srs->importFromWkt(pszWKT);
    }

    OGRLayer* dstLayer = dstDS->CreateLayer("polygonized", srs, wkbPolygon, nullptr);
    if (!dstLayer) {
        GDALClose(dstDS); if (srs) delete srs;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    OGRFieldDefn fieldDefn("DN", OFTInteger);
    dstLayer->CreateField(&fieldDefn);

    progress.onProgress(0.3);

    const CPLErr polygonizeErr = GDALPolygonize(
        GDALRasterBand::ToHandle(rasterBand),
        nullptr,
        OGRLayer::ToHandle(dstLayer),
        0,
        nullptr,
        nullptr,
        nullptr);
    if (polygonizeErr != CE_None) {
        if (srs) delete srs;
        GDALClose(dstDS);
        return gis::framework::Result::fail(
            "Polygonize failed: " + std::string(CPLGetLastErrorMsg()));
    }

    GIntBig featureCount = 0;
    dstLayer->ResetReading();
    while (auto* feature = dstLayer->GetNextFeature()) {
        ++featureCount;
        OGRFeature::DestroyFeature(feature);
    }
    dstLayer->ResetReading();

    if (srs) delete srs;
    GDALClose(dstDS);
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Polygonize completed: " + std::to_string(featureCount) + " polygons", output);
}

gis::framework::Result VectorPlugin::doConvert(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string format = gis::framework::getParam<std::string>(params, "format", "GeoJSON");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    if (!srcLayer) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    GDALDriver* drv = getDriverForFormat(format);
    if (!drv) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Unsupported format: " + format);
    }

    namespace fs = std::filesystem;
    if (format == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) fs::remove(entry.path());
        }
    }

    ensureParentDirectoryForFile(output);
    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    OGRLayer* dstLayer = dstDS->CreateLayer(srcLayer->GetName(), srcSRS, srcLayer->GetGeomType(), nullptr);
    if (!dstLayer) {
        GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    progress.onProgress(0.3);

    srcLayer->ResetReading();
    OGRFeature* feat;
    int count = 0;
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
        dstFeat->SetFrom(feat);
        if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
            GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
            return gis::framework::Result::fail("Failed to write feature");
        }
        OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
        count++;
    }

    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Convert completed: " + std::to_string(count) + " features converted to " + format, output);
}
