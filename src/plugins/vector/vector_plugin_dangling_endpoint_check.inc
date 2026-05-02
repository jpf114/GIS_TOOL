namespace {

struct DanglingEndpointRecord {
    GIntBig fid = -1;
    int endpointType = 0;
    OGRPoint point;
};

static std::string danglingEndpointKey(const OGRPoint& point) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(8);
    oss << point.getX() << "," << point.getY();
    return oss.str();
}

static void appendDanglingEndpoints(
    const OGRGeometry* geom,
    std::vector<DanglingEndpointRecord>& endpoints,
    GIntBig fid) {
    if (!geom || geom->IsEmpty()) {
        return;
    }

    switch (wkbFlatten(geom->getGeometryType())) {
        case wkbLineString: {
            const auto* line = geom->toLineString();
            if (!line || line->getNumPoints() <= 0) {
                break;
            }

            DanglingEndpointRecord startRecord;
            startRecord.fid = fid;
            startRecord.endpointType = 0;
            line->StartPoint(&startRecord.point);
            endpoints.push_back(startRecord);

            if (line->getNumPoints() > 1) {
                DanglingEndpointRecord endRecord;
                endRecord.fid = fid;
                endRecord.endpointType = 1;
                line->EndPoint(&endRecord.point);
                endpoints.push_back(endRecord);
            }
            break;
        }
        case wkbMultiLineString:
        case wkbGeometryCollection: {
            const auto* collection = geom->toGeometryCollection();
            if (!collection) {
                break;
            }
            for (int i = 0; i < collection->getNumGeometries(); ++i) {
                appendDanglingEndpoints(collection->getGeometryRef(i), endpoints, fid);
            }
            break;
        }
        default:
            break;
    }
}

} // namespace

gis::framework::Result VectorPlugin::doDanglingEndpointCheck(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    if (!srcLayer) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    const auto geomType = wkbFlatten(effectiveLayerGeometryType(srcLayer));
    if (geomType != wkbLineString && geomType != wkbMultiLineString) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Dangling endpoint check currently only supports line layers");
    }

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
        GDALClose(srcDS);
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
        progress.onProgress(0.55 + 0.4 * static_cast<double>(i + 1) /
            static_cast<double>(totalEndpoints));
    }

    ofs.close();
    GDALClose(srcDS);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Dangling endpoint check completed: " + std::to_string(issueCount) + " dangling endpoints", output);
    result.metadata["issue_count"] = std::to_string(issueCount);
    result.metadata["scanned_count"] = std::to_string(scannedCount);
    return result;
}
