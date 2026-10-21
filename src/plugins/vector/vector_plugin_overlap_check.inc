gis::framework::Result VectorPlugin::doOverlapCheck(
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
        return gis::framework::Result::fail("Cannot find layer");
    }

    const auto geomType = wkbFlatten(effectiveLayerGeometryType(srcLayer));
    if (geomType != wkbPolygon && geomType != wkbMultiPolygon) {
        return gis::framework::Result::fail("Overlap check currently only supports polygon layers");
    }

    if (detectSrsType(srcLayer->GetSpatialRef()) != "projected") {
        return failProjectedRequired("Overlap check");
    }

    progress.throwIfCancelled();

    progress.onProgress(0.2);

    std::vector<CachedPolygonFeature> features;
    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        std::unique_ptr<OGRGeometry> geom(cloneNormalizedGeometry(feat->GetGeometryRef()));
        if (geom && !geom->IsEmpty()) {
            CachedPolygonFeature cached;
            cached.fid = feat->GetFID();
            cached.geometry = std::move(geom);
            features.push_back(std::move(cached));
        }
        OGRFeature::DestroyFeature(feat);
    }

    progress.throwIfCancelled();

    progress.onProgress(0.35);

    ensureParentDirectoryForFile(output);
    std::ofstream ofs(fs::u8path(output), std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    ofs << "source_fid,target_fid,overlap_area\n";
    int relationCount = 0;
    const size_t totalPairs = features.size() > 1 ? (features.size() * (features.size() - 1)) / 2 : 1;
    size_t processedPairs = 0;
    for (size_t i = 0; i < features.size(); ++i) {
        for (size_t j = i + 1; j < features.size(); ++j) {
            const auto& lhs = features[i];
            const auto& rhs = features[j];
            if (!lhs.geometry->Intersects(rhs.geometry.get())) {
                ++processedPairs;
                progress.throwIfCancelled();
                progress.onProgress(0.35 + 0.55 * static_cast<double>(processedPairs) / static_cast<double>(totalPairs));
                continue;
            }

            // N125 销账（C3d）：交面积口径必须与 topology_check **同源**，所以这里不再自带一份
            // 只认 Polygon / MultiPolygon 的本地 switch，改为复用 common.inc 的 polygonIntersectionArea()
            // （它自己求交、并对 GeometryCollection 累加子面/子多面面积，vector_plugin_common.inc:492-511）。
            // 旧写法的后果：真实省界这类 MultiPolygon 层的交集常常是**混合维度**的
            // GEOMETRYCOLLECTION(POLYGON, LINESTRING)（共面积 + 共边同时发生），旧 switch 落 default 得 0.0
            // ⇒ 下面 `overlapArea > 0.0` 不成立 ⇒ 整行不写，而 topology_check 报得出同一对的面积 ⇒ 自相矛盾。
            // WHY 不许复制第二份面积逻辑：两处各写一遍正是这两个动作口径漂移的成因，单源才是修复。
            const double overlapArea = polygonIntersectionArea(lhs.geometry.get(), rhs.geometry.get());

            if (overlapArea > 0.0) {
                ofs << lhs.fid << "," << rhs.fid << "," << overlapArea << "\n";
                ++relationCount;
            }

            ++processedPairs;
            progress.throwIfCancelled();
            progress.onProgress(0.35 + 0.55 * static_cast<double>(processedPairs) / static_cast<double>(totalPairs));
        }
    }

    ofs.close();
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Overlap check completed: " + std::to_string(relationCount) + " relations", output);
    result.metadata["relation_count"] = std::to_string(relationCount);
    return result;
}
