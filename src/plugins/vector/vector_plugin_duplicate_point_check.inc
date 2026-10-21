gis::framework::Result VectorPlugin::doDuplicatePointCheck(
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
    if (geomType != wkbPoint && geomType != wkbMultiPoint) {
        return gis::framework::Result::fail("Duplicate point check currently only supports point layers");
    }

    progress.throwIfCancelled();

    progress.onProgress(0.25);

    ensureParentDirectoryForFile(output);
    std::ofstream ofs(fs::u8path(output), std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    ofs << "source_fid,duplicate_fid,x,y\n";

    std::unordered_map<std::string, GIntBig> firstPointByKey;
    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    int issueCount = 0;
    int scannedCount = 0;
    // 显式跳过计数：图层级类型闸放行了点类图层，但**要素级**几何仍可能不是 Point/MultiPoint
    // （混合几何图层、或 MakeValid() 返回 GeometryCollection 等）。这类几何一律计数并上报，
    // 不允许静默 else 跳过。
    int skippedGeometryCount = 0;
    const GIntBig totalCount = std::max<GIntBig>(1, srcLayer->GetFeatureCount());
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        std::unique_ptr<OGRGeometry> normalized(cloneNormalizedGeometry(feat->GetGeometryRef()));

        // 上方 :25 的图层闸对 wkbPoint 与 wkbMultiPoint **都放行**，所以要素几何未必是 OGRPoint。
        // OGRGeometry::toPoint() 的原文（ogr_geometry.h:649-655）是
        //     /** Down-cast to OGRPoint*. Implies prior checking that
        //      *  wkbFlatten(getGeometryType()) == wkbPoint. */
        //     inline OGRPoint *toPoint() { return cpl::down_cast<OGRPoint *>(this); }
        // 它内部只有 static_cast：**既不校验类型，也永不返回 nullptr**。因此旧写法
        //     const OGRPoint* point = normalized ? normalized->toPoint() : nullptr;
        //     if (point && !point->IsEmpty()) { … }
        // 里的 "normalized ?" 与 "point &&" **一个都挡不住任何东西**——别把现状误读成"nullptr 会被
        // 安全跳过"。对 MultiPoint 调 toPoint() 是未定义行为；CPLAssert 只在 GDAL 自身定义 DEBUG 时
        // 才展开（cpl_error.h:321），本仓两个配置都没定义 DEBUG，所以没有任何断言会替我们兜住误用。
        // 实测（Task 20 §2）：Debug 不崩、CSV 只剩表头（漏检）；Release 不崩、把 OGRMultiPoint 的
        // 字节当 x,y 读出的非规格化垃圾值写进 CSV，且同一二进制连跑三次结果不同。
        // ⇒ 唯一正确形状是按 wkbFlatten(getGeometryType()) 分派，下面两处降型都已先核过类型。
        std::vector<const OGRPoint*> points;
        if (normalized) {
            const OGRwkbGeometryType flatType = wkbFlatten(normalized->getGeometryType());
            if (flatType == wkbPoint) {
                // 单点快路径：指针只借用，所有权仍在 normalized（勿 delete）。
                points.push_back(normalized->toPoint());
            } else if (flatType == wkbMultiPoint) {
                const OGRMultiPoint* multi = normalized->toMultiPoint();
                for (int i = 0; i < multi->getNumGeometries(); ++i) {
                    const OGRGeometry* part = multi->getGeometryRef(i);
                    if (part && wkbFlatten(part->getGeometryType()) == wkbPoint) {
                        points.push_back(part->toPoint());
                    } else {
                        // MultiPoint 里的非点子几何同样显式计数（OGRMultiPoint 正常不会持有非点子几何，
                        // 这条是防御臂，当前几何路径不可达）。
                        ++skippedGeometryCount;
                    }
                }
            } else {
                ++skippedGeometryCount;   // 非点几何：不参与判定，但必须被计入上报
            }
        }

        for (const OGRPoint* point : points) {
            if (point->IsEmpty()) {
                continue;   // 既有语义保留：EMPTY 点既不判重、也不计入 skipped
            }
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
        progress.throwIfCancelled();
        progress.onProgress(0.25 + 0.7 * static_cast<double>(scannedCount) /
            static_cast<double>(totalCount));
    }

    ofs.close();
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Duplicate point check completed: " + std::to_string(issueCount) + " duplicates", output);
    result.metadata["issue_count"] = std::to_string(issueCount);
    result.metadata["scanned_count"] = std::to_string(scannedCount);
    result.metadata["skipped_geometry_count"] = std::to_string(skippedGeometryCount);
    return result;
}
