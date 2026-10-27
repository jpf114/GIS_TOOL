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

    // 批次 D / D0-C3：CSV 产物走"等价闸 -> 同目录临时件 -> 写完 -> 关闭 -> 替换"（CsvStagedOutput）。
    // 判定顺序不许倒：闸必须在**任何建目录/建文件之前**命中；本动作只有一个输入 => 保护清单就一个。
    // 批次 D / D0-C4（N157）已落地：GPKG:"<路径>":<图层> 这类带驱动前缀的数据源拼法**同样受本闸保护**
    //（比较前先取串背后的那个文件路径，见 common.inc 的归一函数与它的甲案边界）。
    CsvStagedOutput csv;
    std::string stagedError;
    if (!csv.start(output, { input }, stagedError)) {
        return gis::framework::Result::fail(stagedError);
    }
    std::ostream& ofs = csv.stream();

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

    progress.throwIfCancelled();
    progress.onProgress(1.0);

    // 收口：关流 -> 把临时件同卷替换到目标名。失败 => 报失败，旧产物仍在场、临时件由析构删干净。
    if (!csv.commit(stagedError)) {
        return gis::framework::Result::fail(stagedError);
    }

    auto result = gis::framework::Result::ok(
        "Hole check completed: " + std::to_string(issueCount) + " polygon features with holes", output);
    result.metadata["issue_count"] = std::to_string(issueCount);
    result.metadata["scanned_count"] = std::to_string(scannedCount);
    return result;
}
