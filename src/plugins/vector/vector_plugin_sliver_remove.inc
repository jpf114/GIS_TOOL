gis::framework::Result VectorPlugin::doSliverRemove(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    const double minArea = gis::framework::getParam<double>(params, "min_area", 10.0);

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (minArea <= 0.0) return gis::framework::Result::fail("min_area must be greater than 0");

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
        return gis::framework::Result::fail("Sliver remove currently only supports polygon layers");
    }

    progress.throwIfCancelled();

    progress.onProgress(0.2);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::u8path(output).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";
    else if (ext == ".kml") outFormat = "KML";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) {
        return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    // 失败契约的入口闸（等价判定"输出不得覆盖输入"）走在任何建目录/建文件之前：建目录归
    // staged.start 自己做，所以这里不再先动文件系统。旧产物的前置删除同样已去掉 —— 它的
    // 对应物是 commit 末尾的 removeStaleOldSidecars，删除只许发生在新件全部落地之后。
    VectorStagedOutput staged;
    std::string stageError;
    if (!staged.start(output, outFormat, { input }, drv, stageError)) {
        return gis::framework::Result::fail(stageError);
    }
    GDALDataset* dstDS = staged.dataset();

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    std::vector<std::string> layerOptionStorage = buildGpkgDriverIndexedLayerOptions();
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(),
        srcSRS,
        wkbMultiPolygon,
        layerOptions.empty() ? nullptr : layerOptions.data());
    if (!dstLayer) {
        delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    progress.throwIfCancelled();

    progress.onProgress(0.35);

    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    int featureCount = 0;
    int sourceCount = 0;
    int skippedCount = 0;
    int removedPartCount = 0;
    int removedFeatureCount = 0;
    const GIntBig totalCount = std::max<GIntBig>(1, srcLayer->GetFeatureCount());
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        std::unique_ptr<OGRGeometry> normalized(cloneNormalizedGeometry(feat->GetGeometryRef()));
        if (!normalized || normalized->IsEmpty()) {
            ++skippedCount;
            OGRFeature::DestroyFeature(feat);
            ++sourceCount;
            progress.throwIfCancelled();
            progress.onProgress(0.35 + 0.55 * static_cast<double>(sourceCount) /
                static_cast<double>(totalCount));
            continue;
        }

        int removedForFeature = 0;
        std::unique_ptr<OGRGeometry> filtered =
            buildSliverFilteredGeometry(normalized.get(), minArea, removedForFeature);
        removedPartCount += removedForFeature;
        if (!filtered || filtered->IsEmpty()) {
            ++removedFeatureCount;
            OGRFeature::DestroyFeature(feat);
            ++sourceCount;
            progress.throwIfCancelled();
            progress.onProgress(0.35 + 0.55 * static_cast<double>(sourceCount) /
                static_cast<double>(totalCount));
            continue;
        }

        if (!writeFeatureWithCopiedFieldsOwned(dstLayer, feat, std::move(filtered))) {
            OGRFeature::DestroyFeature(feat);
            delete srcSRS;
            return gis::framework::Result::fail("Cannot create sliver-removed feature");
        }

        OGRFeature::DestroyFeature(feat);
        ++featureCount;
        ++sourceCount;
        progress.throwIfCancelled();
        progress.onProgress(0.35 + 0.55 * static_cast<double>(sourceCount) /
            static_cast<double>(totalCount));
    }

    delete srcSRS;
    progress.throwIfCancelled();

    // 全部写完 ⇒ 关闭数据集 → 逐文件替换目标 → 清旧边车（顺序由 VectorStagedOutput::commit 钉）。
    // 替换失败就报失败：此时旧产物仍在场，本轮临时件由 staged 的析构删干净。
    std::string commitError;
    if (!staged.commit(commitError)) {
        return gis::framework::Result::fail(commitError);
    }

    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Sliver remove completed: " + std::to_string(featureCount) + " features", output);
    result.metadata["feature_count"] = std::to_string(featureCount);
    result.metadata["source_count"] = std::to_string(sourceCount);
    result.metadata["skipped_count"] = std::to_string(skippedCount);
    result.metadata["removed_part_count"] = std::to_string(removedPartCount);
    result.metadata["removed_feature_count"] = std::to_string(removedFeatureCount);
    return result;
}
