gis::framework::Result VectorPlugin::doSimplify(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    const double tolerance = gis::framework::getParam<double>(params, "tolerance", 10.0);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (tolerance <= 0.0) return gis::framework::Result::fail("tolerance must be positive");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    if (!srcLayer) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    if (detectSrsType(srcLayer->GetSpatialRef()) != "projected") {
        GDALClose(srcDS);
        return failProjectedRequired("Simplify");
    }

    progress.onProgress(0.2);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
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
        GDALClose(dstDS);
        GDALClose(srcDS);
        delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    progress.onProgress(0.35);

    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    int count = 0;
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
        dstFeat->SetFrom(feat);

        if (OGRGeometry* geom = feat->GetGeometryRef()) {
            std::unique_ptr<OGRGeometry> simplified(geom->SimplifyPreserveTopology(tolerance));
            if (!simplified || simplified->IsEmpty()) {
                OGRFeature::DestroyFeature(dstFeat);
                OGRFeature::DestroyFeature(feat);
                GDALClose(dstDS);
                GDALClose(srcDS);
                delete srcSRS;
                return gis::framework::Result::fail("Simplify failed for one or more features");
            }
            dstFeat->SetGeometry(simplified.get());
        }

        if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(dstFeat);
            OGRFeature::DestroyFeature(feat);
            GDALClose(dstDS);
            GDALClose(srcDS);
            delete srcSRS;
            return gis::framework::Result::fail("Cannot create simplified feature");
        }

        OGRFeature::DestroyFeature(dstFeat);
        OGRFeature::DestroyFeature(feat);
        ++count;
        progress.onProgress(0.35 + 0.55 * static_cast<double>(count) /
            std::max<GIntBig>(1, srcLayer->GetFeatureCount()));
    }

    GDALClose(dstDS);
    GDALClose(srcDS);
    delete srcSRS;
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Simplify completed: " + std::to_string(count) + " features", output);
    result.metadata["feature_count"] = std::to_string(count);
    result.metadata["tolerance"] = std::to_string(tolerance);
    return result;
}
