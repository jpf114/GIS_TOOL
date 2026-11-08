gis::framework::Result VectorPlugin::doAdjacency(
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
        return gis::framework::Result::fail("Adjacency currently only supports polygon layers");
    }

    if (detectSrsType(srcLayer->GetSpatialRef()) != "projected") {
        return failProjectedRequired("Adjacency");
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

    // 批次 D / D0-C3：CSV 产物走"等价闸 -> 同目录临时件 -> 写完 -> 关闭 -> 替换"（CsvStagedOutput）。
    // 判定顺序不许倒：闸必须在**任何建目录/建文件之前**命中；本动作只有一个输入 => 保护清单就一个。
    // 批次 D / D0-C4（N157）已落地：GPKG:"<路径>":<图层> 这类带驱动前缀的数据源拼法**同样受本闸保护**
    //（比较前先取串背后的那个文件路径，见 common.inc 的归一函数与它的甲案边界）。
    CsvStagedOutput csv;
    std::string stagedError;
    if (!csv.start(output, { input }, stagedError, "vector adjacency")) {
        return gis::framework::Result::fail(stagedError);
    }
    std::ostream& ofs = csv.stream();

    ofs << "source_fid,target_fid,shared_length\n";
    int relationCount = 0;
    const size_t totalPairs = features.size() > 1 ? (features.size() * (features.size() - 1)) / 2 : 1;
    size_t processedPairs = 0;
    for (size_t i = 0; i < features.size(); ++i) {
        for (size_t j = i + 1; j < features.size(); ++j) {
            const auto& lhs = features[i];
            const auto& rhs = features[j];
            if (!lhs.geometry->Touches(rhs.geometry.get())) {
                ++processedPairs;
                progress.throwIfCancelled();
                progress.onProgress(0.35 + 0.55 * static_cast<double>(processedPairs) / static_cast<double>(totalPairs));
                continue;
            }

            const double length = sharedBoundaryLength(lhs.geometry.get(), rhs.geometry.get());
            ofs << lhs.fid << "," << rhs.fid << "," << length << "\n";
            ++relationCount;
            ++processedPairs;
            progress.throwIfCancelled();
            progress.onProgress(0.35 + 0.55 * static_cast<double>(processedPairs) / static_cast<double>(totalPairs));
        }
    }

    progress.throwIfCancelled();
    progress.onProgress(1.0);

    // 收口：关流 -> 把临时件同卷替换到目标名。失败 => 报失败，旧产物仍在场、临时件由析构删干净。
    if (!csv.commit(stagedError)) {
        return gis::framework::Result::fail(stagedError);
    }

    auto result = gis::framework::Result::ok(
        "Adjacency completed: " + std::to_string(relationCount) + " relations", output);
    result.metadata["relation_count"] = std::to_string(relationCount);
    return result;
}
