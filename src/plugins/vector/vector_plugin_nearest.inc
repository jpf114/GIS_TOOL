gis::framework::Result VectorPlugin::doNearest(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    const std::string nearestVector = gis::framework::getParam<std::string>(params, "nearest_vector", "");
    const std::string nearestField = gis::framework::getParam<std::string>(params, "nearest_field", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (nearestVector.empty()) return gis::framework::Result::fail("nearest_vector is required");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    GDALDataset* targetDS = openVector(nearestVector, true);
    if (!targetDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot open nearest vector file: " + nearestVector);
    }

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    OGRLayer* targetLayer = targetDS->GetLayer(0);
    if (!srcLayer || !targetLayer) {
        GDALClose(targetDS);
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), targetLayer->GetSpatialRef())) {
        GDALClose(targetDS);
        GDALClose(srcDS);
        return failCrsMismatch("Nearest");
    }

    if (detectSrsType(srcLayer->GetSpatialRef()) != "projected") {
        GDALClose(targetDS);
        GDALClose(srcDS);
        return failProjectedRequired("Nearest");
    }

    int nearestFieldIndex = -1;
    if (!nearestField.empty()) {
        nearestFieldIndex = targetLayer->GetLayerDefn()->GetFieldIndex(nearestField.c_str());
        if (nearestFieldIndex < 0) {
            GDALClose(targetDS);
            GDALClose(srcDS);
            return gis::framework::Result::fail("nearest_field not found: " + nearestField);
        }
    }

    progress.onProgress(0.2);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";
    else if (ext == ".kml") outFormat = "KML";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) {
        GDALClose(targetDS);
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    ensureParentDirectoryForFile(output);
    removeExistingVectorOutput(output, outFormat);

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        GDALClose(targetDS);
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(),
        srcSRS,
        resultLayerGeometryType(srcLayer->GetGeomType()),
        nullptr);
    if (!dstLayer) {
        GDALClose(dstDS);
        GDALClose(targetDS);
        GDALClose(srcDS);
        delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    OGRFieldDefn nearestDistField("nearest_dist", OFTReal);
    nearestDistField.SetWidth(24);
    nearestDistField.SetPrecision(6);
    dstLayer->CreateField(&nearestDistField);

    OGRFieldDefn nearestFidField("nearest_fid", OFTInteger64);
    dstLayer->CreateField(&nearestFidField);

    if (!nearestField.empty()) {
        OGRFieldDefn nearestValueField("nearest_val", OFTString);
        nearestValueField.SetWidth(254);
        dstLayer->CreateField(&nearestValueField);
    }

    progress.onProgress(0.35);

    srcLayer->ResetReading();
    OGRFeature* srcFeat = nullptr;
    int count = 0;
    const GIntBig rawTotalCount = srcLayer->GetFeatureCount();
    const GIntBig totalCount = rawTotalCount > 0 ? rawTotalCount : 1;
    const bool sameDataset = fs::equivalent(fs::u8path(input), fs::u8path(nearestVector));
    while ((srcFeat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* srcGeom = srcFeat->GetGeometryRef();
        if (!srcGeom || srcGeom->IsEmpty()) {
            OGRFeature::DestroyFeature(srcFeat);
            continue;
        }

        double bestDistance = 1e300;
        GIntBig bestFid = -1;
        std::string bestValue;

        targetLayer->ResetReading();
        OGRFeature* targetFeat = nullptr;
        while ((targetFeat = targetLayer->GetNextFeature()) != nullptr) {
            if (sameDataset && srcFeat->GetFID() == targetFeat->GetFID()) {
                OGRFeature::DestroyFeature(targetFeat);
                continue;
            }

            OGRGeometry* targetGeom = targetFeat->GetGeometryRef();
            if (!targetGeom || targetGeom->IsEmpty()) {
                OGRFeature::DestroyFeature(targetFeat);
                continue;
            }

            const double distance = srcGeom->Distance(targetGeom);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestFid = targetFeat->GetFID();
                bestValue = nearestFieldIndex >= 0 ? targetFeat->GetFieldAsString(nearestFieldIndex) : "";
            }
            OGRFeature::DestroyFeature(targetFeat);
        }

        OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
        dstFeat->SetFrom(srcFeat, TRUE);
        dstFeat->SetFID(OGRNullFID);
        dstFeat->SetGeometry(srcGeom);
        if (bestFid >= 0) {
            dstFeat->SetField("nearest_dist", bestDistance);
            dstFeat->SetField("nearest_fid", bestFid);
            if (!nearestField.empty()) {
                dstFeat->SetField("nearest_val", bestValue.c_str());
            }
        }

        if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(dstFeat);
            OGRFeature::DestroyFeature(srcFeat);
            GDALClose(dstDS);
            GDALClose(targetDS);
            GDALClose(srcDS);
            delete srcSRS;
            return gis::framework::Result::fail("Cannot create nearest analysis feature");
        }

        OGRFeature::DestroyFeature(dstFeat);
        OGRFeature::DestroyFeature(srcFeat);
        ++count;
        progress.onProgress(0.35 + 0.55 * static_cast<double>(count) / static_cast<double>(totalCount));
    }

    GDALClose(dstDS);
    GDALClose(targetDS);
    GDALClose(srcDS);
    delete srcSRS;
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Nearest completed: " + std::to_string(count) + " features", output);
    result.metadata["feature_count"] = std::to_string(count);
    result.metadata["nearest_vector"] = nearestVector;
    return result;
}
