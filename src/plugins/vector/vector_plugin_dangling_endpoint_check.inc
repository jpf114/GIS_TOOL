gis::framework::Result VectorPlugin::doDanglingEndpointCheck(
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
    if (geomType != wkbLineString && geomType != wkbMultiLineString) {
        return gis::framework::Result::fail("Dangling endpoint check currently only supports line layers");
    }

    progress.throwIfCancelled();

    progress.onProgress(0.2);

    std::vector<DanglingEndpointRecord> endpoints;
    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    int scannedCount = 0;
    const GIntBig totalCount = std::max<GIntBig>(1, srcLayer->GetFeatureCount());
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        std::unique_ptr<OGRGeometry> normalized(cloneNormalizedGeometry(feat->GetGeometryRef()));
        appendDanglingEndpoints(normalized.get(), endpoints, feat->GetFID());
        OGRFeature::DestroyFeature(feat);
        ++scannedCount;
        progress.throwIfCancelled();
        progress.onProgress(0.2 + 0.35 * static_cast<double>(scannedCount) /
            static_cast<double>(totalCount));
    }

    std::unordered_map<std::string, int> endpointCountByKey;
    for (const auto& endpoint : endpoints) {
        ++endpointCountByKey[danglingEndpointKey(endpoint.point)];
    }

    ensureParentDirectoryForFile(output);
    std::ofstream ofs(fs::u8path(output), std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    ofs << "source_fid,endpoint_type,x,y\n";

    int issueCount = 0;
    const size_t totalEndpoints = endpoints.empty() ? 1 : endpoints.size();
    for (size_t i = 0; i < endpoints.size(); ++i) {
        const auto& endpoint = endpoints[i];
        if (endpointCountByKey[danglingEndpointKey(endpoint.point)] == 1) {
            ofs << endpoint.fid << "," << endpoint.endpointType
                << "," << endpoint.point.getX()
                << "," << endpoint.point.getY() << "\n";
            ++issueCount;
        }
        progress.throwIfCancelled();
        progress.onProgress(0.55 + 0.4 * static_cast<double>(i + 1) /
            static_cast<double>(totalEndpoints));
    }

    ofs.close();
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Dangling endpoint check completed: " + std::to_string(issueCount) + " dangling endpoints", output);
    result.metadata["issue_count"] = std::to_string(issueCount);
    result.metadata["scanned_count"] = std::to_string(scannedCount);
    return result;
}
