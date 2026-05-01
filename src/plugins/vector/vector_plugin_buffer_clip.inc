gis::framework::Result VectorPlugin::doBuffer(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const auto startedAt = std::chrono::steady_clock::now();

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    double distance = gis::framework::getParam<double>(params, "distance", 100.0);

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

    const OGRSpatialReference* srcSRSRef = srcLayer->GetSpatialRef();
    const std::string srsType = detectSrsType(srcSRSRef);
    if (srsType == "geographic") {
        GDALClose(srcDS);
        return gis::framework::Result::fail(
            "Buffer requires a projected coordinate system. Please reproject the input layer before buffering.");
    }

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";
    else if (ext == ".kml") outFormat = "KML";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    ensureParentDirectoryForFile(output);
    removeExistingVectorOutput(output, outFormat);

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcSRSRef ? srcSRSRef->Clone() : nullptr;
    // Buffer output is user-facing analysis data; prefer the driver's native
    // GeoPackage spatial index creation path over the fragile deferred SQL path.
    const bool useTransactions = false;
    std::vector<std::string> layerOptionStorage = buildGpkgFastWriteLayerOptions(useTransactions);
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);

    const std::string outputLayerName = std::string(srcLayer->GetName()) + "_buffer";
    OGRLayer* dstLayer = dstDS->CreateLayer(
        outputLayerName.c_str(),
        srcSRS,
        resultLayerGeometryType(wkbPolygon),
        layerOptionStorage.empty() ? nullptr : layerOptions.data());
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
    auto* dstLayerDefn = dstLayer->GetLayerDefn();
    const std::vector<int> fieldMap = dstLayerDefn->ComputeMapForSetFrom(srcLayerDefn, true);
    OGRFeature* feat;
    int count = 0;
    int total = static_cast<int>(srcLayer->GetFeatureCount(FALSE));
    if (total <= 0) total = 1;
    const int transactionBatchSize = useTransactions ? 20000 : 5000;
    constexpr int progressUpdateInterval = 1000;

    if (useTransactions) {
        dstLayer->StartTransaction();
    }

    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (geom) {
            OGRGeometry* bufferGeom = geom->Buffer(distance);
            if (bufferGeom) {
                OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayerDefn);
                copyFeatureFieldsOnly(dstFeat, feat, fieldMap);
                std::unique_ptr<OGRGeometry> promoted = promoteGeometryToLayerType(dstLayer, bufferGeom);
                if (promoted) {
                    delete bufferGeom;
                    bufferGeom = nullptr;
                    dstFeat->SetGeometryDirectly(promoted.release());
                } else {
                    dstFeat->SetGeometryDirectly(bufferGeom);
                    bufferGeom = nullptr;
                }
                if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
                    OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete bufferGeom; GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
                    return gis::framework::Result::fail("Failed to write buffer feature");
                }
                OGRFeature::DestroyFeature(dstFeat);
                delete bufferGeom;
            }
        }
        OGRFeature::DestroyFeature(feat);
        count++;
        if (useTransactions && count % transactionBatchSize == 0) {
            dstLayer->CommitTransaction();
            dstLayer->StartTransaction();
        }
        if (count % progressUpdateInterval == 0) {
            progress.onProgress(0.3 + 0.6 * static_cast<double>(count) / total);
        }
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
        addSpatialIndexIfNeeded(dstDS, dstLayer, useTransactions, outputLayerName);
    }
    dstLayer->SyncToDisk();
    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Buffer completed: " + std::to_string(count) + " features processed", output);
    result.metadata["feature_count"] = std::to_string(count);
    {
        std::ostringstream oss;
        oss << distance;
        result.metadata["distance"] = oss.str();
    }
    result.metadata["srs_type"] = srsType;
    result.metadata["output_format"] = outFormat;
    result.metadata["elapsed_ms"] = std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt).count());
    return result;
}

gis::framework::Result VectorPlugin::doClip(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string clipVector = gis::framework::getParam<std::string>(params, "clip_vector", "");

    if (input.empty())       return gis::framework::Result::fail("input is required");
    if (output.empty())      return gis::framework::Result::fail("output is required");
    if (clipVector.empty())  return gis::framework::Result::fail("clip_vector is required for clip operation");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open input vector: " + input);

    GDALDataset* clipDS = openVector(clipVector, true);
    if (!clipDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot open clip vector: " + clipVector);
    }

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    OGRLayer* clipLayer = clipDS->GetLayer(0);
    if (!srcLayer || !clipLayer) {
        GDALClose(clipDS); GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), clipLayer->GetSpatialRef())) {
        GDALClose(clipDS); GDALClose(srcDS);
        return failCrsMismatch("Clip");
    }

    const OGRwkbGeometryType srcEffectiveType = effectiveLayerGeometryType(srcLayer);
    auto clipEntries = collectNormalizedGeometries(clipLayer);
    const bool useLinearPolygonSegmentation =
        isLinearGeometryType(srcEffectiveType) && detectOverlayGeometryDimensionRank(clipEntries) == 2;
    GDALClose(clipDS);

    if (clipEntries.empty()) {
        GDALClose(srcDS);
        return failInvalidGeometry("Clip");
    }

    std::unique_ptr<OGRGeometry> mergedClipGeometry;
    if (clipEntries.size() <= 128) {
        mergedClipGeometry.reset(buildMergedOverlayGeometry(clipEntries));
    }

    progress.onProgress(0.3);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";
    else if (ext == ".kml") outFormat = "KML";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    ensureParentDirectoryForFile(output);
    removeExistingVectorOutput(output, outFormat);

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    const bool useTransactions = outFormat == "GPKG";
    std::vector<std::string> layerOptionStorage = buildGpkgFastWriteLayerOptions(useTransactions);
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(),
        srcSRS,
        resultLayerGeometryType(srcEffectiveType),
        layerOptions.empty() ? nullptr : layerOptions.data());
    if (!dstLayer) {
        GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    progress.onProgress(0.4);

    srcLayer->ResetReading();
    const std::vector<int> fieldMap = dstLayer->GetLayerDefn()->ComputeMapForSetFrom(srcLayerDefn, true);
    OGRFeature* feat;
    int count = 0;
    const int transactionBatchSize = useTransactions ? 20000 : 1000;
    if (useTransactions) {
        dstLayer->StartTransaction();
    }
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (useLinearPolygonSegmentation && geom) {
            const auto candidates = collectCandidateOverlayEntries(clipEntries, geom);
            auto segments = splitLineByOverlayEntries(geom, candidates);
            std::unique_ptr<OGRGeometry> clippedGeom(buildMergedSegmentGeometry(segments, true));
            for (auto& segment : segments) {
                delete segment.geometry;
            }

            if (clippedGeom && !clippedGeom->IsEmpty()) {
                if (writeFeatureWithCopiedFields(dstLayer, feat, clippedGeom.get(), &fieldMap)) {
                    count++;
                    if (useTransactions && count % transactionBatchSize == 0) {
                        dstLayer->CommitTransaction();
                        dstLayer->StartTransaction();
                    }
                }
            }
            OGRFeature::DestroyFeature(feat);
            continue;
        }

        OGRGeometry* overlayGeometry = mergedClipGeometry.get();
        std::unique_ptr<OGRGeometry> candidateOverlay;
        if (!overlayGeometry) {
            candidateOverlay.reset(buildCandidateOverlayGeometry(clipEntries, geom));
            overlayGeometry = candidateOverlay.get();
        }
        if (geom && overlayGeometry && geom->Intersects(overlayGeometry)) {
            OGRGeometry* clipped = overlayGeometry->Contains(geom)
                ? geom->clone()
                : geom->Intersection(overlayGeometry);
            if (clipped && !clipped->IsEmpty()) {
                if (writeFeatureWithCopiedFieldsOwned(
                        dstLayer, feat, std::unique_ptr<OGRGeometry>(clipped), &fieldMap)) {
                    clipped = nullptr;
                    count++;
                    if (useTransactions && count % transactionBatchSize == 0) {
                        dstLayer->CommitTransaction();
                        dstLayer->StartTransaction();
                    }
                }
            }
            delete clipped;
        }
        OGRFeature::DestroyFeature(feat);
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
        addSpatialIndexIfNeeded(dstDS, dstLayer, useTransactions, srcLayer->GetName());
    }
    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Clip completed: " + std::to_string(count) + " features clipped", output);
}
