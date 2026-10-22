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
    if (detectSrsType(srcSRSRef) == "geographic") { return failProjectedRequired("Dissolve");
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
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { return gis::framework::Result::fail("Cannot get driver"); }

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) fs::remove(entry.path());
        }
    }

    auto dstDS = gis::core::GdalDatasetPtr(drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr));
    if (!dstDS) { return gis::framework::Result::fail("Cannot create output"); }

    OGRSpatialReference* srcSRS = srcSRSRef ? srcSRSRef->Clone() : nullptr;
    const bool useTransactions = outFormat == "GPKG";
    std::vector<std::string> layerOptionStorage = buildGpkgFastWriteLayerOptions(useTransactions);
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

    if (!dissolveField.empty()) {
        for (auto& [key, geoms] : groups) {
            if (geoms.empty()) continue;

            OGRGeometry* result = unionGeometryList(geoms);

            if (result && !result->IsEmpty()) {
                if (!writeGeometryFeatureOwned(dstLayer, std::unique_ptr<OGRGeometry>(result), [&](OGRFeature* feature) {
                        feature->SetField(dissolveField.c_str(), key.c_str());
                    })) {
                    result = nullptr;
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete result;
                    delete srcSRS;
                    return gis::framework::Result::fail("Failed to write dissolve feature");
                }
                result = nullptr;
                count++;
            }
            delete result;
        }
    } else {
        if (!allGeoms.empty()) {
            OGRGeometry* result = unionGeometryList(allGeoms);

            if (result && !result->IsEmpty()) {
                if (!writeGeometryFeatureOwned(dstLayer, std::unique_ptr<OGRGeometry>(result))) {
                    result = nullptr;
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete result;
                    delete srcSRS;
                    return gis::framework::Result::fail("Failed to write dissolve feature");
                }
                result = nullptr;
                count++;
            }
            delete result;
        }
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
        addSpatialIndexIfNeeded(dstDS.get(), dstLayer, useTransactions, "dissolved");
    }
    delete srcSRS;
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Dissolve completed: " + std::to_string(count) + " features", output);
    // N129 顺带补齐本动作的输出契约：dissolve 此前一个 metadata 键都不发（同插件 20+ 动作都发），
    // 结果"写出多少要素"只能从消息字符串里抠。feature_count 与 geom_metrics/boundary 等同名同义。
    mergeStandardRunMetadata(result, params, "dissolve");
    result.metadata["feature_count"] = std::to_string(count);
    return result;
}
