namespace {

static int geometryHoleCount(const OGRGeometry* geom) {
    if (!geom || geom->IsEmpty()) {
        return 0;
    }

    switch (wkbFlatten(geom->getGeometryType())) {
        case wkbPolygon: {
            const auto* polygon = geom->toPolygon();
            return polygon ? polygon->getNumInteriorRings() : 0;
        }
        case wkbMultiPolygon: {
            const auto* multiPolygon = geom->toMultiPolygon();
            if (!multiPolygon) {
                return 0;
            }

            int holeCount = 0;
            for (int i = 0; i < multiPolygon->getNumGeometries(); ++i) {
                const auto* polygon = multiPolygon->getGeometryRef(i)->toPolygon();
                if (polygon) {
                    holeCount += polygon->getNumInteriorRings();
                }
            }
            return holeCount;
        }
        default:
            return 0;
    }
}

} // namespace

gis::framework::Result VectorPlugin::doHoleCheck(
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
    if (geomType != wkbPolygon && geomType != wkbMultiPolygon) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Hole check currently only supports polygon layers");
    }

    progress.onProgress(0.25);

    ensureParentDirectoryForFile(output);
    std::ofstream ofs(fs::u8path(output), std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        GDALClose(srcDS);
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
        progress.onProgress(0.25 + 0.7 * static_cast<double>(scannedCount) /
            static_cast<double>(totalCount));
    }

    ofs.close();
    GDALClose(srcDS);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Hole check completed: " + std::to_string(issueCount) + " polygon features with holes", output);
    result.metadata["issue_count"] = std::to_string(issueCount);
    result.metadata["scanned_count"] = std::to_string(scannedCount);
    return result;
}
