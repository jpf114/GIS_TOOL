namespace {

static int geometryPartCount(const OGRGeometry* geom) {
    if (!geom || geom->IsEmpty()) {
        return 0;
    }

    const auto flatType = wkbFlatten(geom->getGeometryType());
    if (flatType == wkbMultiPoint) {
        return geom->toMultiPoint()->getNumGeometries();
    }
    if (flatType == wkbMultiLineString) {
        return geom->toMultiLineString()->getNumGeometries();
    }
    if (flatType == wkbMultiPolygon) {
        return geom->toMultiPolygon()->getNumGeometries();
    }
    if (flatType == wkbGeometryCollection) {
        return geom->toGeometryCollection()->getNumGeometries();
    }
    return 1;
}

static bool isMultipartGeometryType(OGRwkbGeometryType type) {
    const auto flatType = wkbFlatten(type);
    return flatType == wkbMultiPoint || flatType == wkbMultiLineString ||
        flatType == wkbMultiPolygon || flatType == wkbGeometryCollection;
}

} // namespace

gis::framework::Result VectorPlugin::doMultipartCheck(
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

    progress.onProgress(0.25);

    ensureParentDirectoryForFile(output);
    std::ofstream ofs(fs::u8path(output), std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    ofs << "source_fid,geom_type,part_count\n";

    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    int issueCount = 0;
    int scannedCount = 0;
    const GIntBig totalCount = std::max<GIntBig>(1, srcLayer->GetFeatureCount());
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        const int partCount = geometryPartCount(geom);
        if (partCount > 1 || (geom && isMultipartGeometryType(geom->getGeometryType()))) {
            ofs << feat->GetFID() << ","
                << geometryTypeName(geom ? geom->getGeometryType() : wkbUnknown) << ","
                << partCount << "\n";
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
        "Multipart check completed: " + std::to_string(issueCount) + " multipart features", output);
    result.metadata["issue_count"] = std::to_string(issueCount);
    result.metadata["scanned_count"] = std::to_string(scannedCount);
    return result;
}
