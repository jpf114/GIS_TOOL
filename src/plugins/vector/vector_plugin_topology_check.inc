namespace {

struct CachedTopologyFeature {
    GIntBig fid = -1;
    bool empty = false;
    bool invalid = false;
    std::unique_ptr<OGRGeometry> geometry;
};

static double polygonIntersectionArea(const OGRGeometry* lhs, const OGRGeometry* rhs) {
    if (!lhs || !rhs || lhs->IsEmpty() || rhs->IsEmpty()) {
        return 0.0;
    }

    std::unique_ptr<OGRGeometry> intersection(lhs->Intersection(rhs));
    if (!intersection || intersection->IsEmpty()) {
        return 0.0;
    }

    switch (wkbFlatten(intersection->getGeometryType())) {
        case wkbPolygon:
            return intersection->toPolygon()->get_Area();
        case wkbMultiPolygon:
            return intersection->toMultiPolygon()->get_Area();
        default:
            return 0.0;
    }
}

} // namespace

gis::framework::Result VectorPlugin::doTopologyCheck(
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
        return gis::framework::Result::fail("Cannot find layer");
    }

    const auto geomType = wkbFlatten(effectiveLayerGeometryType(srcLayer));
    if (geomType != wkbPolygon && geomType != wkbMultiPolygon) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Topology check currently only supports polygon layers");
    }

    if (detectSrsType(srcLayer->GetSpatialRef()) != "projected") {
        GDALClose(srcDS);
        return failProjectedRequired("Topology check");
    }

    progress.onProgress(0.2);

    std::vector<CachedTopologyFeature> features;
    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        CachedTopologyFeature cached;
        cached.fid = feat->GetFID();

        OGRGeometry* rawGeom = feat->GetGeometryRef();
        cached.empty = (rawGeom == nullptr || rawGeom->IsEmpty());
        if (rawGeom && !rawGeom->IsEmpty()) {
            ScopedQuietGdalErrorHandler quietErrors;
            cached.invalid = !rawGeom->IsValid();
        } else {
            cached.invalid = false;
        }

        cached.geometry.reset(cloneNormalizedGeometry(rawGeom));
        features.push_back(std::move(cached));
        OGRFeature::DestroyFeature(feat);
    }

    progress.onProgress(0.35);

    ensureParentDirectoryForFile(output);
    std::ofstream ofs(fs::u8path(output), std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    ofs << "source_fid,target_fid,issue_type,issue_value\n";
    int issueCount = 0;
    for (const auto& feature : features) {
        if (feature.empty) {
            ofs << feature.fid << ",,empty_geometry,0\n";
            ++issueCount;
        }
        if (feature.invalid) {
            ofs << feature.fid << ",,invalid_geometry,0\n";
            ++issueCount;
        }
    }

    const size_t totalPairs = features.size() > 1 ? (features.size() * (features.size() - 1)) / 2 : 1;
    size_t processedPairs = 0;
    for (size_t i = 0; i < features.size(); ++i) {
        for (size_t j = i + 1; j < features.size(); ++j) {
            const auto& lhs = features[i];
            const auto& rhs = features[j];
            if (!lhs.geometry || !rhs.geometry || lhs.geometry->IsEmpty() || rhs.geometry->IsEmpty()) {
                ++processedPairs;
                progress.onProgress(0.35 + 0.55 * static_cast<double>(processedPairs) / static_cast<double>(totalPairs));
                continue;
            }

            if (lhs.geometry->Equals(rhs.geometry.get())) {
                ofs << lhs.fid << "," << rhs.fid << ",duplicate_geometry,0\n";
                ++issueCount;
            } else if (lhs.geometry->Intersects(rhs.geometry.get())) {
                const double overlapArea = polygonIntersectionArea(lhs.geometry.get(), rhs.geometry.get());
                if (overlapArea > 0.0) {
                    ofs << lhs.fid << "," << rhs.fid << ",overlap," << overlapArea << "\n";
                    ++issueCount;
                }
            }

            ++processedPairs;
            progress.onProgress(0.35 + 0.55 * static_cast<double>(processedPairs) / static_cast<double>(totalPairs));
        }
    }

    ofs.close();
    GDALClose(srcDS);
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Topology check completed: " + std::to_string(issueCount) + " issues", output);
    result.metadata["issue_count"] = std::to_string(issueCount);
    return result;
}
