namespace {

static std::string pointKey(const OGRPoint* point) {
    if (!point) {
        return {};
    }
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(8);
    oss << point->getX() << "," << point->getY();
    return oss.str();
}

} // namespace

gis::framework::Result VectorPlugin::doDuplicatePointCheck(
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
    if (geomType != wkbPoint && geomType != wkbMultiPoint) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Duplicate point check currently only supports point layers");
    }

    progress.onProgress(0.25);

    ensureParentDirectoryForFile(output);
    std::ofstream ofs(fs::u8path(output), std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    ofs << "source_fid,duplicate_fid,x,y\n";

    std::unordered_map<std::string, GIntBig> firstPointByKey;
    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    int issueCount = 0;
    int scannedCount = 0;
    const GIntBig totalCount = std::max<GIntBig>(1, srcLayer->GetFeatureCount());
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        std::unique_ptr<OGRGeometry> normalized(cloneNormalizedGeometry(feat->GetGeometryRef()));
        const OGRPoint* point = normalized ? normalized->toPoint() : nullptr;
        if (point && !point->IsEmpty()) {
            const std::string key = pointKey(point);
            const auto it = firstPointByKey.find(key);
            if (it == firstPointByKey.end()) {
                firstPointByKey.emplace(key, feat->GetFID());
            } else {
                ofs << it->second << "," << feat->GetFID()
                    << "," << point->getX()
                    << "," << point->getY() << "\n";
                ++issueCount;
            }
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
        "Duplicate point check completed: " + std::to_string(issueCount) + " duplicates", output);
    result.metadata["issue_count"] = std::to_string(issueCount);
    result.metadata["scanned_count"] = std::to_string(scannedCount);
    return result;
}
