gis::framework::Result VectorPlugin::doInteriorPoint(
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
        return gis::framework::Result::fail("Interior point currently only supports polygon layers");
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
        wkbPoint,
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
    int count = 0;
    int skippedCount = 0;
    const GIntBig totalCount = std::max<GIntBig>(1, srcLayer->GetFeatureCount());
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        std::unique_ptr<OGRGeometry> normalized(cloneNormalizedGeometry(feat->GetGeometryRef()));
        if (!normalized || normalized->IsEmpty()) {
            ++skippedCount;
            OGRFeature::DestroyFeature(feat);
            progress.throwIfCancelled();
            progress.onProgress(0.35 + 0.55 * static_cast<double>(count + skippedCount) /
                static_cast<double>(totalCount));
            continue;
        }

        // N117：本动作的语义是"取一个落在几何内部的代表点"，不是面积重心。
        // Centroid() 在凹面/多部件上会给出**形外**的点（实测：两条宽 1 的臂组成的 L 形
        // (0,0)-(10,0)-(10,1)-(1,1)-(1,10)-(0,10) 给出 (2.8684,2.8684)，而该点既不在横臂带
        // y<=1 也不在竖臂带 x<=1 上）。这里走 OGR_G_PointOnSurface 而不是 OGRSurface::PointOnSurface，
        // 因为输入图层的几何类型可以是 wkbUnknown（混合层实测含面也含线）：C API 内部会按类型分派，
        // 线要素照样拿到"线上一点"，不会因基类没有该虚函数而被跳过。
        OGRPoint point;
        // 句柄转换一律显式 reinterpret_cast：OGRGeometryH 在本仓 GDAL 头里是 void*，
        // 靠隐式转换会在 Debug 配置报 C2664（同一份代码 Release 编得过、Debug 编不过，
        // 该差异未解释，登记为 N119）。GDAL 自身的 FROM_H/TO_H 也是同样的显式转换。
        OGRGeometryH hRep = normalized
            ? OGR_G_PointOnSurface(reinterpret_cast<OGRGeometryH>(normalized.get()))
            : nullptr;
        std::unique_ptr<OGRGeometry, void (*)(OGRGeometry *)> repGuard(
            reinterpret_cast<OGRGeometry *>(hRep),
            [](OGRGeometry *g) { OGR_G_DestroyGeometry(reinterpret_cast<OGRGeometryH>(g)); });
        const OGRPoint* surfacePoint =
            (repGuard && repGuard->getGeometryType() == wkbPoint) ? repGuard->toPoint() : nullptr;
        if (surfacePoint) {
            point = *surfacePoint;
        }
        if (!surfacePoint || point.IsEmpty()) {
            ++skippedCount;
            OGRFeature::DestroyFeature(feat);
            progress.throwIfCancelled();
            progress.onProgress(0.35 + 0.55 * static_cast<double>(count + skippedCount) /
                static_cast<double>(totalCount));
            continue;
        }

        if (!writeFeatureWithCopiedFields(dstLayer, feat, &point)) {
            OGRFeature::DestroyFeature(feat);
            delete srcSRS;
            return gis::framework::Result::fail("Cannot create interior point feature");
        }

        OGRFeature::DestroyFeature(feat);
        ++count;
        progress.throwIfCancelled();
        progress.onProgress(0.35 + 0.55 * static_cast<double>(count + skippedCount) /
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
        "Interior point completed: " + std::to_string(count) + " features", output);
    result.metadata["feature_count"] = std::to_string(count);
    result.metadata["skipped_count"] = std::to_string(skippedCount);
    return result;
}
