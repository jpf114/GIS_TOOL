gis::framework::Result VectorPlugin::doDissolve(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string dissolveField = gis::framework::getParam<std::string>(params, "dissolve_field", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS.get(), layerName);
    if (!srcLayer) { return gis::framework::Result::fail("Cannot find layer"); }

    const OGRSpatialReference* srcSRSRef = srcLayer->GetSpatialRef();
    // N130：`== "geographic"` 会放过"没有坐标参考"的输入（detectSrsType 返回 "unknown"），
    // 融合后的面积/长度单位无从谈起。用户裁定这一族统一收成"必须投影"。
    if (detectSrsType(srcSRSRef) != "projected") { return failProjectedRequired("Dissolve");
    }

    progress.throwIfCancelled();

    progress.onProgress(0.2);

    // N129：点名了融合字段就必须存在。旧实现在这里允许 GetFieldIndex() 返回 -1，
    // 把这些要素塞进 allGeoms，而下面写出段走的却是 !dissolveField.empty() 那条分支、
    // 遍历的是空的 groups ⇒ 一个要素也不写，仍然返回
    // "Dissolve completed: 0 features" 且 success=true（参数写错静默换成空图层）。
    // 改成硬失败，文案与 spatial_join 的 join_field not found
    // （vector_plugin_spatial_join.inc:38-43）同族；校验放在建输出文件之前 ⇒ 失败不留半成品。
    // dissolve_field 留空仍是合法的"整体融合"，不走这条分支。
    int dissolveFieldIndex = -1;
    if (!dissolveField.empty()) {
        dissolveFieldIndex = srcLayer->GetLayerDefn()->GetFieldIndex(dissolveField.c_str());
        if (dissolveFieldIndex < 0) {
            return gis::framework::Result::fail("dissolve_field not found: " + dissolveField);
        }
    }

    std::map<std::string, std::vector<OGRGeometry*>> groups;
    std::vector<OGRGeometry*> allGeoms;

    srcLayer->ResetReading();
    OGRFeature* feat;
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = cloneNormalizedGeometry(feat->GetGeometryRef());
        if (!geom) { OGRFeature::DestroyFeature(feat); continue; }

        if (!dissolveField.empty()) {
            groups[feat->GetFieldAsString(dissolveFieldIndex)].push_back(geom);
        } else {
            allGeoms.push_back(geom);
        }
        OGRFeature::DestroyFeature(feat);
    }

    progress.throwIfCancelled();

    progress.onProgress(0.4);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::u8path(output).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { return gis::framework::Result::fail("Cannot get driver"); }

    // 失败契约的入口闸（等价判定"输出不得覆盖输入"）走在任何建目录/建文件之前：建目录归
    // staged.start 自己做，所以这里不再先动文件系统。旧产物的前置删除（含 Shapefile 同 stem
    // 边车循环，N122 那条）同样已去掉 —— 它的对应物是 commit 末尾的 removeStaleOldSidecars，
    // 删除只许发生在新件全部落地之后。
    VectorStagedOutput staged;
    std::string stageError;
    if (!staged.start(output, outFormat, { input }, drv, stageError)) {
        return gis::framework::Result::fail(stageError);
    }
    GDALDataset* dstDS = staged.dataset();

    OGRSpatialReference* srcSRS = srcSRSRef ? srcSRSRef->Clone() : nullptr;
    const bool useTransactions = outFormat == "GPKG";
    std::vector<std::string> layerOptionStorage = buildGpkgDriverIndexedLayerOptions();
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);
    OGRLayer* dstLayer = dstDS->CreateLayer(
        "dissolved",
        srcSRS,
        wkbMultiPolygon,
        layerOptions.empty() ? nullptr : layerOptions.data());
    if (!dstLayer) {
        delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    if (!dissolveField.empty()) {
        OGRFieldDefn fieldDefn(dissolveField.c_str(), OFTString);
        dstLayer->CreateField(&fieldDefn);
    }

    progress.throwIfCancelled();

    progress.onProgress(0.5);

    int count = 0;
    if (useTransactions) {
        dstLayer->StartTransaction();
    }

    // 所有权口径（读 unionGeometryRange 现确认后写下来的，不是猜的）：单元素那一组的 `result` 就是
    // `geoms[0]` 本身（base case 返回 `geoms[begin]`，不做 clone），多元素那一组把 left/right 都 delete 掉
    // ⇒ 传进来的每个几何都由这条链消费。而 writeGeometryFeatureOwned 收的是 `std::unique_ptr` 值参 ⇒
    // 调用一旦发生，所有权就已移交（空几何早退那条由形参析构释放）。
    // 所以旧代码那四处 `result = nullptr;` 与其后配套的 `delete result;` 全是空转（`delete nullptr`）：
    // 它们不会造成 double free，但读起来像 use-after-free，且把"谁拥有这块几何"这条唯一的关键事实藏住了
    // ⇒ 改成 unique_ptr + std::move，让移交点在代码上可见。
    if (!dissolveField.empty()) {
        for (auto& [key, geoms] : groups) {
            if (geoms.empty()) continue;

            std::unique_ptr<OGRGeometry> result(unionGeometryList(geoms));

            if (result && !result->IsEmpty()) {
                if (!writeGeometryFeatureOwned(dstLayer, std::move(result), [&](OGRFeature* feature) {
                        feature->SetField(dissolveField.c_str(), key.c_str());
                    })) {
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete srcSRS;
                    return gis::framework::Result::fail("Failed to write dissolve feature");
                }
                count++;
            }
        }
    } else {
        if (!allGeoms.empty()) {
            std::unique_ptr<OGRGeometry> result(unionGeometryList(allGeoms));

            if (result && !result->IsEmpty()) {
                if (!writeGeometryFeatureOwned(dstLayer, std::move(result))) {
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete srcSRS;
                    return gis::framework::Result::fail("Failed to write dissolve feature");
                }
                count++;
            }
        }
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
    }
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
        "Dissolve completed: " + std::to_string(count) + " features", output);
    // N129 顺带补齐本动作的输出契约：dissolve 此前一个 metadata 键都不发（同插件 20+ 动作都发），
    // 结果"写出多少要素"只能从消息字符串里抠。feature_count 与 geom_metrics/boundary 等同名同义。
    mergeStandardRunMetadata(result, params, "dissolve");
    result.metadata["feature_count"] = std::to_string(count);
    return result;
}
