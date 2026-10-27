gis::framework::Result VectorPlugin::doRepair(
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

    progress.throwIfCancelled();

    progress.onProgress(0.2);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::u8path(output).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

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
    OGRLayer* dstLayer = dstDS->CreateLayer(srcLayer->GetName(), srcSRS, srcLayer->GetGeomType(), nullptr);
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
    int repairedCount = 0;
    int skippedCount = 0;
    const GIntBig totalCount = std::max<GIntBig>(1, srcLayer->GetFeatureCount());
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
        dstFeat->SetFrom(feat);

        // 所有权照抄先例 vector_plugin_singlepart.inc:85 的**写法**（不只抄判据）：克隆一拿到手就交
        // 给 std::unique_ptr 持有。OGRFeature::SetGeometry 内部做的是克隆，故交出 .get() 后本地仍由
        // 智能指针负责释放 ⇒ EMPTY/null 支路的 continue、以及 CreateFeature 失败支路的提前 return
        // 都不必各自手写 delete（评审席逮到的正是"continue 前漏 delete repaired"这条新引入的泄漏）。
        std::unique_ptr<OGRGeometry> repaired(cloneNormalizedGeometry(feat->GetGeometryRef()));
        // N149 案①（用户 2026-10-27 拍）：cloneNormalizedGeometry 对 EMPTY 返回的是**非空指针的
        // 空克隆**（common.inc 里 `if (!cloned || cloned->IsEmpty()) return cloned;`），所以只判
        // nullptr 会把 EMPTY 当成"修好了"：既计入 repaired_count，又原样写进产物。判据照抄本仓
        // 既有先例 vector_plugin_singlepart.inc 的 `!normalized || normalized->IsEmpty()` ⇒ EMPTY 归 skipped。
        // ⚠ 只改 repair 本地，**不动共享的 cloneNormalizedGeometry**（上抬会波及 dissolve 与
        //   buildNormalizedUnion，那是另一笔账）。
        if (!repaired || repaired->IsEmpty()) {
            ++skippedCount;
            OGRFeature::DestroyFeature(dstFeat);
            OGRFeature::DestroyFeature(feat);
            progress.throwIfCancelled();
            progress.onProgress(0.35 + 0.55 * static_cast<double>(repairedCount + skippedCount) /
                static_cast<double>(totalCount));
            continue;
        }

        dstFeat->SetGeometry(repaired.get());

        if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(dstFeat);
            OGRFeature::DestroyFeature(feat);
            delete srcSRS;
            return gis::framework::Result::fail("Cannot create repaired feature");
        }

        OGRFeature::DestroyFeature(dstFeat);
        OGRFeature::DestroyFeature(feat);
        ++repairedCount;
        progress.throwIfCancelled();
        progress.onProgress(0.35 + 0.55 * static_cast<double>(repairedCount + skippedCount) /
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
        "Repair completed: " + std::to_string(repairedCount) + " features", output);
    result.metadata["repaired_count"] = std::to_string(repairedCount);
    result.metadata["skipped_count"] = std::to_string(skippedCount);
    return result;
}
