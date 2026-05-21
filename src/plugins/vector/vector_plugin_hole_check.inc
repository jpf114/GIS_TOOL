gis::framework::Result VectorPlugin::doHoleCheck(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS.get(), layerName);
    if (!srcLayer) {
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    const auto geomType = wkbFlatten(effectiveLayerGeometryType(srcLayer));
    if (geomType != wkbPolygon && geomType != wkbMultiPolygon) {
        return gis::framework::Result::fail("Hole check currently only supports polygon layers");
    }

    progress.throwIfCancelled();

    progress.onProgress(0.25);

    ensureParentDirectoryForFile(output);
    std::ofstream ofs(fs::u8path(output), std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    ofs << "source_fid,geom_type,hole_count\n";

    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    int issueCount = 0;
    int scannedCount = 0;
    const GIntBig totalCount = std::max<GIntBig>(1, srcLayer->GetFeatureCount());
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        const int holeCount = geometryHoleCount(geom);
        if (holeCount > 0) {
            ofs << feat->GetFID() << ","
                << geometryTypeName(geom ? geom->getGeometryType() : wkbUnknown) << ","
                << holeCount << "\n";
            ++issueCount;
        }

        OGRFeature::DestroyFeature(feat);
        ++scannedCount;
        progress.throwIfCancelled();
        progress.onProgress(0.25 + 0.7 * static_cast<double>(scannedCount) /
            static_cast<double>(totalCount));
    }

    ofs.close();
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Hole check completed: " + std::to_string(issueCount) + " polygon features with holes", output);
    result.metadata["issue_count"] = std::to_string(issueCount);
    result.metadata["scanned_count"] = std::to_string(scannedCount);
    return result;
}
