gis::framework::Result VectorPlugin::doUnion(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string overlayVector = gis::framework::getParam<std::string>(params, "overlay_vector", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (overlayVector.empty()) return gis::framework::Result::fail("overlay_vector is required for union");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open input: " + input);

    GDALDataset* overlayDS = openVector(overlayVector, true);
    if (!overlayDS) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot open overlay: " + overlayVector); }

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    OGRLayer* overlayLayer = overlayDS->GetLayer(0);
    if (!srcLayer || !overlayLayer) {
        GDALClose(overlayDS); GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), overlayLayer->GetSpatialRef())) {
        GDALClose(overlayDS); GDALClose(srcDS);
        return failCrsMismatch("Union");
    }

    const OGRwkbGeometryType srcEffectiveType = effectiveLayerGeometryType(srcLayer);
    auto overlayEntries = collectNormalizedGeometries(overlayLayer);
    const bool usePracticalMixedUnion =
        isLinearGeometryType(srcEffectiveType) && detectOverlayGeometryDimensionRank(overlayEntries) == 2;
    GDALClose(overlayDS);

    if (overlayEntries.empty()) {
        GDALClose(srcDS);
        return failInvalidGeometry("Union");
    }

    if (usePracticalMixedUnion && shouldRejectHighlyOverlappedMixedUnion(overlayEntries)) {
        GDALClose(srcDS);
        return gis::framework::Result::fail(
            "Union detected heavily overlapped polygon overlays for mixed-dimension input. "
            "Please dissolve or simplify the overlay polygons first, or use clip/difference instead.");
    }

    progress.onProgress(0.3);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot get driver"); }

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) fs::remove(entry.path());
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot create output"); }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    const auto outputGeomType = usePracticalMixedUnion
        ? resultLayerGeometryType(srcEffectiveType)
        : wkbUnknown;
    const bool useTransactions = outFormat == "GPKG";
    std::vector<std::string> layerOptionStorage = buildGpkgFastWriteLayerOptions(useTransactions);
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);
    OGRLayer* dstLayer = dstDS->CreateLayer(
        "union",
        srcSRS,
        outputGeomType,
        layerOptions.empty() ? nullptr : layerOptions.data());
    if (!dstLayer) {
        GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    OGRFieldDefn fieldDefn("source", OFTString);
    dstLayer->CreateField(&fieldDefn);

    progress.onProgress(0.4);

    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    int count = 0;
    const int transactionBatchSize = useTransactions ? 20000 : 1000;
    if (useTransactions) {
        dstLayer->StartTransaction();
    }
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (usePracticalMixedUnion && geom) {
            const auto candidates = collectCandidateOverlayEntries(overlayEntries, geom);
            auto lineParts = splitLineByOverlayEntries(geom, candidates);

            for (size_t i = 0; i < lineParts.size(); ++i) {
                OGRGeometry* part = lineParts[i].geometry;
                if (!writeUnionFeature(dstLayer, part, "split")) {
                    for (size_t j = i; j < lineParts.size(); ++j) {
                        delete lineParts[j].geometry;
                    }
                    OGRFeature::DestroyFeature(feat);
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
                    return gis::framework::Result::fail("Failed to write union feature");
                }
                delete part;
                lineParts[i].geometry = nullptr;
                count++;
                if (useTransactions && count % transactionBatchSize == 0) {
                    dstLayer->CommitTransaction();
                    dstLayer->StartTransaction();
                }
            }
        } else {
            std::unique_ptr<OGRGeometry> candidateOverlay(buildCandidateOverlayGeometry(overlayEntries, geom));
            if (geom && candidateOverlay && geom->Intersects(candidateOverlay.get())) {
            std::unique_ptr<OGRGeometry> unionGeom(geom->Union(candidateOverlay.get()));
            if (unionGeom && !unionGeom->IsEmpty()) {
                if (!writeUnionFeatureOwned(
                        dstLayer, std::move(unionGeom), "union")) {
                    OGRFeature::DestroyFeature(feat);
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
                    return gis::framework::Result::fail("Failed to write union feature");
                }
                count++;
                if (useTransactions && count % transactionBatchSize == 0) {
                    dstLayer->CommitTransaction();
                    dstLayer->StartTransaction();
                }
            }
            } else if (geom) {
                OGRGeometry* cloned = geom->clone();
                if (!writeUnionFeature(dstLayer, cloned, "input")) {
                    OGRFeature::DestroyFeature(feat);
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete cloned;
                    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
                    return gis::framework::Result::fail("Failed to write union feature");
                }
                delete cloned;
                count++;
                if (useTransactions && count % transactionBatchSize == 0) {
                    dstLayer->CommitTransaction();
                    dstLayer->StartTransaction();
                }
            }
        }
        OGRFeature::DestroyFeature(feat);
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
        addSpatialIndexIfNeeded(dstDS, dstLayer, useTransactions, "union");
    }
    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Union completed: " + std::to_string(count) + " features", output);
}

gis::framework::Result VectorPlugin::doDifference(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string overlayVector = gis::framework::getParam<std::string>(params, "overlay_vector", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (overlayVector.empty()) return gis::framework::Result::fail("overlay_vector is required for difference");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open input: " + input);

    GDALDataset* overlayDS = openVector(overlayVector, true);
    if (!overlayDS) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot open overlay: " + overlayVector); }

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    OGRLayer* overlayLayer = overlayDS->GetLayer(0);
    if (!srcLayer || !overlayLayer) {
        GDALClose(overlayDS); GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), overlayLayer->GetSpatialRef())) {
        GDALClose(overlayDS); GDALClose(srcDS);
        return failCrsMismatch("Difference");
    }

    const OGRwkbGeometryType srcEffectiveType = effectiveLayerGeometryType(srcLayer);
    auto overlayEntries = collectNormalizedGeometries(overlayLayer);
    const bool useLinearPolygonSegmentation =
        isLinearGeometryType(srcEffectiveType) && detectOverlayGeometryDimensionRank(overlayEntries) == 2;
    GDALClose(overlayDS);

    if (overlayEntries.empty()) {
        GDALClose(srcDS);
        return failInvalidGeometry("Difference");
    }

    std::unique_ptr<OGRGeometry> mergedOverlayGeometry;
    if (overlayEntries.size() <= 128) {
        mergedOverlayGeometry.reset(buildMergedOverlayGeometry(overlayEntries));
    }

    progress.onProgress(0.3);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot get driver"); }

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) fs::remove(entry.path());
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot create output"); }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    const bool useTransactions = outFormat == "GPKG";
    std::vector<std::string> layerOptionStorage = buildGpkgFastWriteLayerOptions(useTransactions);
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);
    OGRLayer* dstLayer = dstDS->CreateLayer(
        "difference",
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
    OGRFeature* feat = nullptr;
    int count = 0;
    const int transactionBatchSize = useTransactions ? 20000 : 1000;
    if (useTransactions) {
        dstLayer->StartTransaction();
    }
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (geom) {
            if (useLinearPolygonSegmentation) {
                const auto candidates = collectCandidateOverlayEntries(overlayEntries, geom);
                if (candidates.empty()) {
                    if (writeFeatureWithCopiedFields(dstLayer, feat, geom->clone(), &fieldMap)) {
                        count++;
                        if (useTransactions && count % transactionBatchSize == 0) {
                            dstLayer->CommitTransaction();
                            dstLayer->StartTransaction();
                        }
                    }
                    OGRFeature::DestroyFeature(feat);
                    continue;
                }

                auto segments = splitLineByOverlayEntries(geom, candidates);
                std::unique_ptr<OGRGeometry> diffGeom(buildMergedSegmentGeometry(segments, false));
                for (auto& segment : segments) {
                    delete segment.geometry;
                }

                if (diffGeom && !diffGeom->IsEmpty()) {
                    if (writeFeatureWithCopiedFields(dstLayer, feat, diffGeom.get(), &fieldMap)) {
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

            OGRGeometry* overlayGeometry = mergedOverlayGeometry.get();
            std::unique_ptr<OGRGeometry> candidateOverlay;
            if (!overlayGeometry) {
                candidateOverlay.reset(buildCandidateOverlayGeometry(overlayEntries, geom));
                overlayGeometry = candidateOverlay.get();
            }
            OGRGeometry* diffGeom = nullptr;
            if (!overlayGeometry || !geom->Intersects(overlayGeometry)) {
                diffGeom = geom->clone();
            } else if (!overlayGeometry->Contains(geom)) {
                diffGeom = geom->Difference(overlayGeometry);
            }
            if (diffGeom && !diffGeom->IsEmpty()) {
                if (writeFeatureWithCopiedFieldsOwned(
                        dstLayer, feat, std::unique_ptr<OGRGeometry>(diffGeom), &fieldMap)) {
                    diffGeom = nullptr;
                    count++;
                    if (useTransactions && count % transactionBatchSize == 0) {
                        dstLayer->CommitTransaction();
                        dstLayer->StartTransaction();
                    }
                }
            }
            delete diffGeom;
        }
        OGRFeature::DestroyFeature(feat);
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
        addSpatialIndexIfNeeded(dstDS, dstLayer, useTransactions, "difference");
    }
    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Difference completed: " + std::to_string(count) + " features", output);
}

gis::framework::Result VectorPlugin::doIntersect(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string overlayVector = gis::framework::getParam<std::string>(params, "overlay_vector", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (overlayVector.empty()) return gis::framework::Result::fail("overlay_vector is required for intersect");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open input: " + input);

    GDALDataset* overlayDS = openVector(overlayVector, true);
    if (!overlayDS) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot open overlay: " + overlayVector); }

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    OGRLayer* overlayLayer = overlayDS->GetLayer(0);
    if (!srcLayer || !overlayLayer) {
        GDALClose(overlayDS); GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), overlayLayer->GetSpatialRef())) {
        GDALClose(overlayDS); GDALClose(srcDS);
        return failCrsMismatch("Intersect");
    }

    const OGRwkbGeometryType srcEffectiveType = effectiveLayerGeometryType(srcLayer);
    auto overlayEntries = collectNormalizedGeometries(overlayLayer);
    const bool useLinearPolygonSegmentation =
        isLinearGeometryType(srcEffectiveType) && detectOverlayGeometryDimensionRank(overlayEntries) == 2;
    GDALClose(overlayDS);

    if (overlayEntries.empty()) {
        GDALClose(srcDS);
        return failInvalidGeometry("Intersect");
    }

    std::unique_ptr<OGRGeometry> mergedOverlayGeometry;
    if (overlayEntries.size() <= 128) {
        mergedOverlayGeometry.reset(buildMergedOverlayGeometry(overlayEntries));
    }

    progress.onProgress(0.3);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot get driver"); }

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) fs::remove(entry.path());
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot create output"); }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    const bool useTransactions = outFormat == "GPKG";
    std::vector<std::string> layerOptionStorage = buildGpkgFastWriteLayerOptions(useTransactions);
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);
    OGRLayer* dstLayer = dstDS->CreateLayer(
        "intersect",
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
    OGRFeature* feat = nullptr;
    int count = 0;
    const int transactionBatchSize = useTransactions ? 20000 : 1000;
    if (useTransactions) {
        dstLayer->StartTransaction();
    }
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (geom) {
            if (useLinearPolygonSegmentation) {
                const auto candidates = collectCandidateOverlayEntries(overlayEntries, geom);
                if (candidates.empty()) {
                    OGRFeature::DestroyFeature(feat);
                    continue;
                }

                auto segments = splitLineByOverlayEntries(geom, candidates);
                std::unique_ptr<OGRGeometry> intersectGeom(buildMergedSegmentGeometry(segments, true));
                for (auto& segment : segments) {
                    delete segment.geometry;
                }

                if (intersectGeom && !intersectGeom->IsEmpty()) {
                    if (writeFeatureWithCopiedFieldsOwned(dstLayer, feat, std::move(intersectGeom), &fieldMap)) {
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

            OGRGeometry* overlayGeometry = mergedOverlayGeometry.get();
            std::unique_ptr<OGRGeometry> candidateOverlay;
            if (!overlayGeometry) {
                candidateOverlay.reset(buildCandidateOverlayGeometry(overlayEntries, geom));
                overlayGeometry = candidateOverlay.get();
            }
            if (overlayGeometry && geom->Intersects(overlayGeometry)) {
                std::unique_ptr<OGRGeometry> intersectGeom(geom->Intersection(overlayGeometry));
                if (intersectGeom && !intersectGeom->IsEmpty()) {
                    if (writeFeatureWithCopiedFieldsOwned(dstLayer, feat, std::move(intersectGeom), &fieldMap)) {
                        count++;
                        if (useTransactions && count % transactionBatchSize == 0) {
                            dstLayer->CommitTransaction();
                            dstLayer->StartTransaction();
                        }
                    }
                }
            }
        }
        OGRFeature::DestroyFeature(feat);
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
        addSpatialIndexIfNeeded(dstDS, dstLayer, useTransactions, "intersect");
    }
    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Intersect completed: " + std::to_string(count) + " features", output);
}
