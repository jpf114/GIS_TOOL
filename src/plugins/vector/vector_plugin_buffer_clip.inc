gis::framework::Result VectorPlugin::doBuffer(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {
    const auto startedAt = std::chrono::steady_clock::now();

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    double distance = gis::framework::getParam<double>(params, "distance", 100.0);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS.get(), layerName);
    if (!srcLayer) { return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    const OGRSpatialReference* srcSRSRef = srcLayer->GetSpatialRef();
    const std::string srsType = detectSrsType(srcSRSRef);
    // N130：旧闸只拒 "geographic"，而 detectSrsType 对"没有坐标参考"与"非地理非投影"都返回 "unknown"
    // ⇒ 无 CRS 的输入被放过去，distance 按"度"当"米"算，结果量级全错。用户裁定这一族统一收成"必须投影"。
    if (srsType != "projected") { return failProjectedRequired("Buffer");
    }

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::u8path(output).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";
    else if (ext == ".kml") outFormat = "KML";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
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

    OGRSpatialReference* srcSRS = srcSRSRef ? srcSRSRef->Clone() : nullptr;
    const bool useTransactions = false;
    // 这里不向 CreateLayer 传任何图层选项（buildGpkgDriverIndexedLayerOptions 恒返回空），
    // GeoPackage 空间索引由驱动在 FlushCache 时自行创建。
    std::vector<std::string> layerOptionStorage = buildGpkgDriverIndexedLayerOptions();
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);

    const std::string outputLayerName = std::string(srcLayer->GetName()) + "_buffer";
    OGRLayer* dstLayer = dstDS->CreateLayer(
        outputLayerName.c_str(),
        srcSRS,
        resultLayerGeometryType(wkbPolygon),
        layerOptionStorage.empty() ? nullptr : layerOptions.data());
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

    progress.onProgress(0.3);

    srcLayer->ResetReading();
    auto* dstLayerDefn = dstLayer->GetLayerDefn();
    const std::vector<int> fieldMap = dstLayerDefn->ComputeMapForSetFrom(srcLayerDefn, true);
    OGRFeature* feat;
    int count = 0;
    int total = static_cast<int>(srcLayer->GetFeatureCount(FALSE));
    if (total <= 0) total = 1;
    const int transactionBatchSize = useTransactions ? 20000 : 5000;
    constexpr int progressUpdateInterval = 1000;

    if (useTransactions) {
        dstLayer->StartTransaction();
    }

    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (geom) {
            std::unique_ptr<OGRGeometry> bufferGeom(geom->Buffer(distance));
            if (bufferGeom) {
                OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayerDefn);
                copyFeatureFieldsOnly(dstFeat, feat, fieldMap);
                std::unique_ptr<OGRGeometry> promoted = promoteGeometryToLayerType(dstLayer, bufferGeom.get());
                if (promoted) {
                    dstFeat->SetGeometryDirectly(promoted.release());
                } else {
                    dstFeat->SetGeometryDirectly(bufferGeom.release());
                }
                if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
                    OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete srcSRS;
                    return gis::framework::Result::fail("Failed to write buffer feature");
                }
                OGRFeature::DestroyFeature(dstFeat);
            }
        }
        OGRFeature::DestroyFeature(feat);
        count++;
        if (useTransactions && count % transactionBatchSize == 0) {
            dstLayer->CommitTransaction();
            dstLayer->StartTransaction();
        }
        if (count % progressUpdateInterval == 0) {
            progress.throwIfCancelled();
            progress.onProgress(0.3 + 0.6 * static_cast<double>(count) / total);
        }
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
    }
    dstLayer->SyncToDisk();
    delete srcSRS;
    progress.throwIfCancelled();

    // 工单 §2：事务必须在 commit() 之前收口 —— commit 的第一件事就是 dataset_.reset()，
    // 事务还开着就关数据集会被 GDAL 在关闭时回滚 ⇒ "产物已替换但内容半截"的假绿。
    // 全部写完 ⇒ 关闭数据集 → 逐文件替换目标 → 清旧边车（顺序由 VectorStagedOutput::commit 钉）。
    std::string commitError;
    if (!staged.commit(commitError)) {
        return gis::framework::Result::fail(commitError);
    }

    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Buffer completed: " + std::to_string(count) + " features processed", output);
    mergeStandardRunMetadata(result, params, "buffer");
    result.metadata["feature_count"] = std::to_string(count);
    {
        std::ostringstream oss;
        oss << distance;
        result.metadata["distance"] = oss.str();
    }
    result.metadata["srs_type"] = srsType;
    result.metadata["output_format"] = outFormat;
    result.metadata["elapsed_ms"] = std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt).count());
    return result;
}

gis::framework::Result VectorPlugin::doClip(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string clipVector = gis::framework::getParam<std::string>(params, "clip_vector", "");

    if (input.empty())       return gis::framework::Result::fail("input is required");
    if (output.empty())      return gis::framework::Result::fail("output is required");
    if (clipVector.empty())  return gis::framework::Result::fail("clip_vector is required for clip operation");

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open input vector: " + input);

    auto clipDS = openVector(clipVector, true);
    if (!clipDS) { return gis::framework::Result::fail("Cannot open clip vector: " + clipVector);
    }

    OGRLayer* srcLayer = getLayer(srcDS.get(), layerName);
    OGRLayer* clipLayer = clipDS->GetLayer(0);
    if (!srcLayer || !clipLayer) { return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), clipLayer->GetSpatialRef())) { return failCrsMismatch("Clip");
    }

    const OGRwkbGeometryType srcEffectiveType = effectiveLayerGeometryType(srcLayer);
    auto clipEntries = collectNormalizedGeometries(clipLayer);
    const bool useLinearPolygonSegmentation =
        isLinearGeometryType(srcEffectiveType) && detectOverlayGeometryDimensionRank(clipEntries) == 2;

    if (clipEntries.empty()) { return failInvalidGeometry("Clip");
    }

    std::unique_ptr<OGRGeometry> mergedClipGeometry;
    if (clipEntries.size() <= 128) {
        mergedClipGeometry.reset(buildMergedOverlayGeometry(clipEntries));
    }

    progress.throwIfCancelled();

    progress.onProgress(0.3);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::u8path(output).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";
    else if (ext == ".kml") outFormat = "KML";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    // 失败契约的入口闸（等价判定"输出不得覆盖输入"）走在任何建目录/建文件之前：建目录归
    // staged.start 自己做，所以这里不再先动文件系统。旧产物的前置删除同样已去掉 —— 它的
    // 对应物是 commit 末尾的 removeStaleOldSidecars，删除只许发生在新件全部落地之后。
    // 双输入：`--clip_vector` 指到输出同样会毁数据 ⇒ 两个输入都进保护清单。
    VectorStagedOutput staged;
    std::string stageError;
    if (!staged.start(output, outFormat, { input, clipVector }, drv, stageError)) {
        return gis::framework::Result::fail(stageError);
    }
    GDALDataset* dstDS = staged.dataset();

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    const bool useTransactions = outFormat == "GPKG";
    std::vector<std::string> layerOptionStorage = buildGpkgDriverIndexedLayerOptions();
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(),
        srcSRS,
        resultLayerGeometryType(srcEffectiveType),
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

    progress.onProgress(0.4);

    srcLayer->ResetReading();
    const std::vector<int> fieldMap = dstLayer->GetLayerDefn()->ComputeMapForSetFrom(srcLayerDefn, true);
    OGRFeature* feat;
    int count = 0;
    const int transactionBatchSize = useTransactions ? 20000 : 1000;
    if (useTransactions) {
        dstLayer->StartTransaction();
    }
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (useLinearPolygonSegmentation && geom) {
            const auto candidates = collectCandidateOverlayEntries(clipEntries, geom);
            auto segments = splitLineByOverlayEntries(geom, candidates);
            std::unique_ptr<OGRGeometry> clippedGeom(buildMergedSegmentGeometry(segments, true));
            for (auto& segment : segments) {
                delete segment.geometry;
            }

            if (clippedGeom && !clippedGeom->IsEmpty()) {
                // N139（结构修，用户 2026-10-23 裁定不引入失败注入缝）：写失败不许吞成
                // "少写一条仍报成功"。与 doBuffer 的 `CreateFeature != OGRERR_NONE` 同形 ——
                // 在事务里先回滚，再硬失败返回（临时件族由 staged 析构兜住，旧产物不动）。
                if (!writeFeatureWithCopiedFields(dstLayer, feat, clippedGeom.get(), &fieldMap)) {
                    OGRFeature::DestroyFeature(feat);
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete srcSRS;
                    return gis::framework::Result::fail("Failed to write clip feature");
                }
                count++;
                if (useTransactions && count % transactionBatchSize == 0) {
                    dstLayer->CommitTransaction();
                    dstLayer->StartTransaction();
                }
            }
            OGRFeature::DestroyFeature(feat);
            continue;
        }

        OGRGeometry* overlayGeometry = mergedClipGeometry.get();
        std::unique_ptr<OGRGeometry> candidateOverlay;
        if (!overlayGeometry) {
            candidateOverlay.reset(buildCandidateOverlayGeometry(clipEntries, geom));
            overlayGeometry = candidateOverlay.get();
        }
        if (geom && overlayGeometry && geom->Intersects(overlayGeometry)) {
            // writeFeatureWithCopiedFieldsOwned 一律接管传入几何的所有权（无论写入成功与否，
            // 几何都会在写者内部被销毁）。这里必须用 unique_ptr 一次性移交：
            // 若写入失败后再对同一个裸指针 delete，就是二次释放，直接破坏进程堆。
            std::unique_ptr<OGRGeometry> clipped(overlayGeometry->Contains(geom)
                ? geom->clone()
                : geom->Intersection(overlayGeometry));
            if (clipped && !clipped->IsEmpty()) {
                // N139（结构修，用户 2026-10-23 裁定不引入失败注入缝）：同 doBuffer —— 写失败
                // ⇒ 事务里先回滚，再硬失败返回。writeFeatureWithCopiedFieldsOwned 无论成功失败
                // 都接管几何所有权，所以失败后只销毁源要素，不得再碰 clipped。
                if (!writeFeatureWithCopiedFieldsOwned(dstLayer, feat, std::move(clipped), &fieldMap)) {
                    OGRFeature::DestroyFeature(feat);
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete srcSRS;
                    return gis::framework::Result::fail("Failed to write clip feature");
                }
                count++;
                if (useTransactions && count % transactionBatchSize == 0) {
                    dstLayer->CommitTransaction();
                    dstLayer->StartTransaction();
                }
            }
        }
        OGRFeature::DestroyFeature(feat);
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
    }
    delete srcSRS;
    progress.throwIfCancelled();

    // 工单 §2：事务必须在 commit() 之前收口（理由同 doBuffer）。全部写完 ⇒ 关闭数据集 →
    // 逐文件替换目标 → 清旧边车（顺序由 VectorStagedOutput::commit 钉）。
    std::string commitError;
    if (!staged.commit(commitError)) {
        return gis::framework::Result::fail(commitError);
    }

    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Clip completed: " + std::to_string(count) + " features clipped", output);
}
