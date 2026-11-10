gis::framework::Result VectorPlugin::doSpatialJoin(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    const std::string joinVector = gis::framework::getParam<std::string>(params, "join_vector", "");
    const std::string joinField = gis::framework::getParam<std::string>(params, "join_field", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (joinVector.empty()) return gis::framework::Result::fail("join_vector is required");

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    auto joinDS = openVector(joinVector, true);
    if (!joinDS) {
        return gis::framework::Result::fail("Cannot open join vector file: " + joinVector);
    }

    OGRLayer* srcLayer = getLayer(srcDS.get(), layerName);
    OGRLayer* joinLayer = joinDS->GetLayer(0);
    if (!srcLayer || !joinLayer) {
        return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), joinLayer->GetSpatialRef())) {
        return failCrsMismatch("Spatial join");
    }

    int joinFieldIndex = -1;
    if (!joinField.empty()) {
        joinFieldIndex = joinLayer->GetLayerDefn()->GetFieldIndex(joinField.c_str());
        if (joinFieldIndex < 0) {
            return gis::framework::Result::fail("join_field not found: " + joinField);
        }
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
    // 双输入：`--join_vector` 指到输出同样会毁数据 ⇒ 两个输入都进保护清单（下面循环里的
    // sameDataset 判的是"输入 vs 第二个输入"，管不到"输出 vs 第二个输入"）。
    VectorStagedOutput staged;
    std::string stageError;
    if (!staged.start(output, outFormat, { input, joinVector }, drv, stageError, "vector spatial_join")) {
        return gis::framework::Result::fail(stageError);
    }
    GDALDataset* dstDS = staged.dataset();

    // srcSRS RAII 托管（OGRSpatialReferenceReleaser）：取消/早退路径由析构兜底，不再手工释放。
    std::unique_ptr<OGRSpatialReference, OGRSpatialReferenceReleaser> srcSRS(
        srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr);
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(),
        srcSRS.get(),
        resultLayerGeometryType(effectiveLayerGeometryType(srcLayer)),
        nullptr);
    if (!dstLayer) {
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    OGRFieldDefn joinCountField("join_count", OFTInteger);
    dstLayer->CreateField(&joinCountField);

    if (!joinField.empty()) {
        OGRFieldDefn joinValueField("join_value", OFTString);
        joinValueField.SetWidth(254);
        dstLayer->CreateField(&joinValueField);
    }

    progress.throwIfCancelled();

    progress.onProgress(0.35);

    srcLayer->ResetReading();
    OGRFeature* srcFeat = nullptr;
    int count = 0;
    const GIntBig rawTotalCount = srcLayer->GetFeatureCount();
    const GIntBig totalCount = rawTotalCount > 0 ? rawTotalCount : 1;
    // N156（用户 2026-10-27 拍：一行修，与 nearest.inc 成对改）：路径等价判定必须走**带 error_code
    // 的重载**。`GPKG:"<路径>":<图层>` 是 openVector（GDALOpenEx）**接受**的合法 GDAL 数据源串，
    // 但对 std::filesystem 而言引号是非法字符 ⇒ 无 ec 的那版抛 filesystem_error，CLI rc=1 并把
    // Windows 的 std 原文（"equivalent: The filename, directory name, or volume label syntax is
    // incorrect."）吐给用户。这里吞掉异常、按"不是同一个文件"处理，与同仓 common.inc 的
    // `if (fs::equivalent(input, target_, ec))` 两处写法一致。
    // ⚠ 不解析 `GPKG:` 前缀取真路径再比 —— 那属未批的 `--join_layer` 那笔账。
    std::error_code sameDatasetEc;
    const bool sameDataset = fs::equivalent(fs::u8path(input), fs::u8path(joinVector), sameDatasetEc);
    while ((srcFeat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* srcGeom = srcFeat->GetGeometryRef();
        if (!srcGeom || srcGeom->IsEmpty()) {
            OGRFeature::DestroyFeature(srcFeat);
            continue;
        }

        int joinCount = 0;
        std::vector<std::string> joinValues;
        std::unordered_set<std::string> seenValues;

        joinLayer->ResetReading();
        OGRFeature* joinFeat = nullptr;
        while ((joinFeat = joinLayer->GetNextFeature()) != nullptr) {
            if (sameDataset && srcFeat->GetFID() == joinFeat->GetFID()) {
                OGRFeature::DestroyFeature(joinFeat);
                continue;
            }

            OGRGeometry* joinGeom = joinFeat->GetGeometryRef();
            if (!joinGeom || joinGeom->IsEmpty()) {
                OGRFeature::DestroyFeature(joinFeat);
                continue;
            }

            if (srcGeom->Intersects(joinGeom)) {
                ++joinCount;
                if (joinFieldIndex >= 0) {
                    const std::string value = joinFeat->GetFieldAsString(joinFieldIndex);
                    if (!value.empty() && seenValues.insert(value).second) {
                        joinValues.push_back(value);
                    }
                }
            }
            OGRFeature::DestroyFeature(joinFeat);
            progress.throwIfCancelled();
        }

        OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
        dstFeat->SetFrom(srcFeat, TRUE);
        dstFeat->SetFID(OGRNullFID);
        dstFeat->SetGeometry(srcGeom);
        dstFeat->SetField("join_count", joinCount);
        if (!joinField.empty()) {
            std::string joinedValues;
            for (size_t i = 0; i < joinValues.size(); ++i) {
                if (i > 0) {
                    joinedValues += ";";
                }
                joinedValues += joinValues[i];
            }
            dstFeat->SetField("join_value", joinedValues.c_str());
        }

        if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(dstFeat);
            OGRFeature::DestroyFeature(srcFeat);
            return gis::framework::Result::fail("Cannot create spatial join feature");
        }

        OGRFeature::DestroyFeature(dstFeat);
        OGRFeature::DestroyFeature(srcFeat);
        ++count;
        progress.throwIfCancelled();
        progress.onProgress(0.35 + 0.55 * static_cast<double>(count) / static_cast<double>(totalCount));
    }

    progress.throwIfCancelled();

    // 全部写完 ⇒ 关闭数据集 → 逐文件替换目标 → 清旧边车（顺序由 VectorStagedOutput::commit 钉）。
    // 替换失败就报失败：此时旧产物仍在场，本轮临时件由 staged 的析构删干净。
    std::string commitError;
    if (!staged.commit(commitError)) {
        return gis::framework::Result::fail(commitError);
    }

    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Spatial join completed: " + std::to_string(count) + " features", output);
    result.metadata["feature_count"] = std::to_string(count);
    result.metadata["join_vector"] = joinVector;
    return result;
}
