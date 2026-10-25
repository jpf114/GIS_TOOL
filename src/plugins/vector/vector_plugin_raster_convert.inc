gis::framework::Result VectorPlugin::doRasterize(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    double resolution = gis::framework::getParam<double>(params, "resolution", 1.0);
    std::string attribute = gis::framework::getParam<std::string>(params, "attribute", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (resolution <= 0) return gis::framework::Result::fail("resolution must be positive");

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* layer = getLayer(srcDS.get(), layerName);
    if (!layer) { return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    // N131：点名了 attribute 就必须存在，且要在建出输出文件之前查出来。
    // 旧实现把它原样交给 GDAL 的 -a，烧写阶段才报 "Failed to find field <值> on layer <图层名>"：
    // 那时 drv->Create 已经把输出 tif 建好并 Fill(0) 过 ⇒ 失败后留一张全 0 的半成品
    //（而 0 恰是本动作的 NoData，打开看不出异常），消息也是 GDAL 内部口径、没点出参数叫 attribute。
    // 现在与 dissolve_field / join_field not found 同族文案，失败即返回、磁盘上不留东西。
    // attribute 留空仍是合法的"烧常数 1"，不走这条分支。
    // WHY 按"点名的图层"查：与下面 -a 的取域范围、以及 layer 参数自己的口径保持一致；
    // 大小写也与 GDAL 的 GetFieldIndex 同为敏感，不做额外的宽松匹配。
    if (!attribute.empty()) {
        const int attributeIndex = layer->GetLayerDefn()->GetFieldIndex(attribute.c_str());
        if (attributeIndex < 0) {
            return gis::framework::Result::fail("attribute not found: " + attribute);
        }

        // N132：字段**存在**但类型不是数值 ⇒ 同样必须在这里拒掉，不能交给 GDAL 的 -a。
        // 双臂字节级实测（4×4 Float32 产物尾 64 字节，同一段几何）：
        //   --attribute=label（label 是 OFTString，值 "road_a"/"road_b"）⇒ 16 个 00 00 00 00，
        //     而动作仍报 Success，产物 histogram 报 "no valid pixels found in sampling"
        //     （0 恰是本动作的 NoData ⇒ 整幅都是 NoData，打开看不出异常）；
        //     同输入不传 attribute（burn=1）⇒ 16 个 00 00 80 3f，即 1.0。
        // GDAL 的 -a 对文本域走的是 GetFieldAsDouble（内部 CPLAtof），"3" 这种**看着像数值**
        // 的文本会被静默转换成 3.0 烧进去 —— 那是同一条支上的另一种静默，同样没有依据。
        // 所以这里按**类型**一刀切：只有 OFTInteger / OFTInteger64 / OFTReal 允许被点名当烧值，
        // 文本、日期、时间、二进制、各 List 型一律硬失败。
        // 不退化成"非数值就当 burn=1"：那只是把"静默烧 0"换成"静默烧 1"，用户点名的字段
        // 依旧没有被用上，产物照旧自称为那个字段烧写的结果。
        const OGRFieldType attributeType =
            layer->GetLayerDefn()->GetFieldDefn(attributeIndex)->GetType();
        if (attributeType != OFTInteger && attributeType != OFTInteger64 &&
            attributeType != OFTReal) {
            return gis::framework::Result::fail("attribute field is not numeric: " + attribute);
        }
    }

    OGREnvelope extent;
    if (layer->GetExtent(&extent, TRUE) != OGRERR_NONE) { return gis::framework::Result::fail("Cannot get layer extent");
    }

    int xSize = static_cast<int>(std::ceil((extent.MaxX - extent.MinX) / resolution));
    int ySize = static_cast<int>(std::ceil((extent.MaxY - extent.MinY) / resolution));
    if (xSize <= 0 || ySize <= 0) { return gis::framework::Result::fail("Invalid raster dimensions");
    }
    const long long maxPixels = 500000000LL;
    if (static_cast<long long>(xSize) * static_cast<long long>(ySize) > maxPixels) { return gis::framework::Result::fail(
            "Output raster too large (" + std::to_string(xSize) + "x" + std::to_string(ySize) +
            " = " + std::to_string(static_cast<long long>(xSize) * ySize) +
            " pixels). Please increase resolution or reduce extent.");
    }

    progress.throwIfCancelled();

    progress.onProgress(0.2);

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    ensureParentDirectoryForFile(output);
    auto dstDS = gis::core::GdalDatasetPtr(drv->Create(output.c_str(), xSize, ySize, 1, GDT_Float32, nullptr));
    if (!dstDS) { return gis::framework::Result::fail("Cannot create output raster: " + output);
    }

    double adfGT[6];
    adfGT[0] = extent.MinX; adfGT[1] = resolution; adfGT[2] = 0;
    adfGT[3] = extent.MaxY; adfGT[4] = 0;          adfGT[5] = -resolution;
    dstDS->SetGeoTransform(adfGT);

    const OGRSpatialReference* srs = layer->GetSpatialRef();
    if (srs) {
        char* wkt = nullptr;
        srs->exportToWkt(&wkt);
        if (wkt) { dstDS->SetProjection(wkt); CPLFree(wkt); }
    }

    GDALRasterBand* band = dstDS->GetRasterBand(1);
    band->Fill(0);
    band->SetNoDataValue(0);

    progress.throwIfCancelled();

    progress.onProgress(0.3);

    // N134：只把**点名的那一层**递交给烧写 ⇒ layer 参数从此真正限制烧写范围。
    // 改前三处口径不对称：属性字段校验与外包范围取的都是点名层（都在本段之前），烧写却吃整个 srcDS。
    // ⚠ 改前的真实症状比"未点名的图层也一起烧"更糟：本机 GDAL 3.12.4 的 utils 版 GDALRasterize
    //   在既没给 -l 也没给 -sql 时要求源数据集只有一个图层，否则在开工处就硬失败 ——
    //   "Rasterize failed: Neither -sql nor -l are specified, but the source dataset has not one single layer."
    //   ⇒ 多图层的输入改前**根本烧不出来**（双图层夹具的实测症状就是这句失败，不是"全烧"）。
    //   换成按图层句柄数组调算法层 API 之后，行为变更的方向是：
    //   烧写面收窄（只烧点名层）+ 输入面放宽（多图层输入从"整条动作失败"变成可用）。
    // layer 留空 ⇒ 第一层，沿用 getLayer 的既有语义（vector_plugin_common.inc:27-38），不是"全部图层"。
    //
    // ⚠ GDALRasterizeLayers 吃的是 CPL 的 NAME=VALUE 选项表，**不是** `gdal rasterize` 的命令行 token。
    //   本机 GDAL 3.12.4 源码 alg/gdalrasterize.cpp:1642-1658 把两件事写死在那里：
    //     烧值字段 = CSLFetchNameValue(papszOptions, "ATTRIBUTE")；取不到 ATTRIBUTE 才会去读
    //     padfLayerBurnValues + iLayer * nBandCount。
    //   ⇒ 把 "-a" "<字段名>" 这种 argv token 原样递进去，取不到的是 ATTRIBUTE 这个名字、
    //     于是落到"读烧值数组"那一支，而我们在点字段那一支根本没有数组 ⇒ 不能这么给。
    //     两条合法分支各按各自的形态给：
    //     点了字段 ⇒ 只给 ATTRIBUTE=<字段名>（该分支不读烧值数组，给 nullptr）；
    //     留空     ⇒ 不给 ATTRIBUTE，给 nLayerCount * nBandCount = 1 个烧值 1.0，与改前 -burn 1 同口径。
    // N131/N132 的字段校验按**同一层**查过存在性与类型，所以这里落不进
    // "ATTRIBUTE 指到的字段在该层不存在"那一支 —— GDAL 对该支只发 CE_Warning 后跳过整层并返回 CE_None，
    // 静默烧出一幅全 0，正是 N131 已经堵掉的那种伪装。
    char** rasterizeOptions = nullptr;      // CSL 列表句柄，由 CSLSetNameValue 自己分配
    std::vector<double> burnValues;
    if (!attribute.empty()) {
        rasterizeOptions = CSLSetNameValue(rasterizeOptions, "ATTRIBUTE", attribute.c_str());
    } else {
        burnValues.assign(1, 1.0);
    }

    const int bandList[1] = {1};                            // 本动作固定单波段
    OGRLayerH burnLayers[1] = {OGRLayer::ToHandle(layer)};  // 只有 getLayer 选出来的那一层
    const CPLErr rasterizeErr = GDALRasterizeLayers(
        static_cast<GDALDatasetH>(dstDS.get()),
        1, const_cast<int*>(bandList),
        1, burnLayers,
        nullptr,    // 变换留空：GDAL 按输出栅格自己的地理变换建（点面坐标与栅格同一套）
        nullptr,
        burnValues.empty() ? nullptr : burnValues.data(),
        rasterizeOptions,
        nullptr, nullptr);
    CSLDestroy(rasterizeOptions);

    if (rasterizeErr != CE_None) {
        return gis::framework::Result::fail("Rasterize failed: " + std::string(CPLGetLastErrorMsg()));
    }
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Rasterize completed: " + std::to_string(xSize) + "x" + std::to_string(ySize) + " raster", output);

    // N131 的第二半：产物要能自证"烧成了多大、烧了哪些要素、烧的是哪个字段"。
    // 此前 rasterize 的 Result::ok 一个 metadata 键都不发，这三件事只能打开 tif 反推
    //（而"打开一看全是 0"正是本缺陷的伪装形态）。
    mergeStandardRunMetadata(result, params, "rasterize");
    result.metadata["raster_width"] = std::to_string(xSize);
    result.metadata["raster_height"] = std::to_string(ySize);

    // N134 的记账面：这个键从"输入数据集全部图层求和"收紧为**只数被烧的那一层**。
    // 全层求和是旧烧写范围（整个 srcDS）的产物；范围收窄之后继续按全层求和，就等于让元数据
    // 自称"烧了没被烧的要素"——那是本缺陷的另一面（产物无法自证），不是旧口径的延续。
    // 单图层输入不受影响（点名层就是唯一被烧的层），所以真实腿 rasterize_bbox 仍是 1。
    GIntBig burnedFeatureCount = layer->GetFeatureCount();
    if (burnedFeatureCount < 0) {
        // 少数驱动不报缓存数 ⇒ 现读遍历，不许把 -1 当"0 个要素"写进元数据
        burnedFeatureCount = 0;
        layer->ResetReading();
        while (auto* feat = layer->GetNextFeature()) {
            ++burnedFeatureCount;
            OGRFeature::DestroyFeature(feat);
        }
    }
    result.metadata["source_feature_count"] = std::to_string(burnedFeatureCount);

    // 烧值来源二选一：点了字段就回显字段名，留空就回显常数 1（与本动作的两条合法分支一一对应）
    if (!attribute.empty()) {
        result.metadata["burn_field"] = attribute;
    } else {
        result.metadata["burn_value"] = "1";
    }

    return result;
}

gis::framework::Result VectorPlugin::doPolygonize(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    int band = gis::framework::getParam<int>(params, "band", 1);

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto srcDS = gis::core::openRaster(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open raster file: " + input);

    GDALRasterBand* rasterBand = srcDS->GetRasterBand(band);
    if (!rasterBand) return gis::framework::Result::fail("Cannot get band " + std::to_string(band));

    progress.throwIfCancelled();

    progress.onProgress(0.2);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::u8path(output).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);

    // 失败契约的入口闸（等价判定"输出不得覆盖输入"）走在任何建目录/建文件之前：建目录归
    // staged.start 自己做，所以这里不再先动文件系统。旧产物的前置删除（含 Shapefile 同 stem
    // 边车循环，N122 那条）同样已去掉 —— 它的对应物是 commit 末尾的 removeStaleOldSidecars，
    // 删除只许发生在新件全部落地之后。
    // 输入是栅格：它同样属于"绝不许被输出覆盖"的对象（输出扩展名写错成 .tif 时也一样）。
    VectorStagedOutput staged;
    std::string stageError;
    if (!staged.start(output, outFormat, { input }, drv, stageError)) {
        return gis::framework::Result::fail(stageError);
    }
    GDALDataset* dstDS = staged.dataset();

    OGRSpatialReference* srs = nullptr;
    const char* pszWKT = srcDS->GetProjectionRef();
    if (pszWKT && pszWKT[0] != '\0') {
        srs = new OGRSpatialReference();
        srs->importFromWkt(pszWKT);
    }

    OGRLayer* dstLayer = dstDS->CreateLayer("polygonized", srs, wkbPolygon, nullptr);
    if (!dstLayer) { if (srs) delete srs;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    OGRFieldDefn fieldDefn("DN", OFTInteger);
    dstLayer->CreateField(&fieldDefn);

    progress.throwIfCancelled();

    progress.onProgress(0.3);

    const CPLErr polygonizeErr = GDALPolygonize(
        GDALRasterBand::ToHandle(rasterBand),
        nullptr,
        OGRLayer::ToHandle(dstLayer),
        0,
        nullptr,
        nullptr,
        nullptr);
    if (polygonizeErr != CE_None) {
        if (srs) delete srs;
        return gis::framework::Result::fail(
            "Polygonize failed: " + std::string(CPLGetLastErrorMsg()));
    }

    GIntBig featureCount = 0;
    dstLayer->ResetReading();
    while (auto* feature = dstLayer->GetNextFeature()) {
        ++featureCount;
        OGRFeature::DestroyFeature(feature);
    }
    dstLayer->ResetReading();

    if (srs) delete srs;
    progress.throwIfCancelled();

    // 全部写完 ⇒ 关闭数据集 → 逐文件替换目标 → 清旧边车（顺序由 VectorStagedOutput::commit 钉）。
    // 替换失败就报失败：此时旧产物仍在场，本轮临时件由 staged 的析构删干净。
    std::string commitError;
    if (!staged.commit(commitError)) {
        return gis::framework::Result::fail(commitError);
    }

    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Polygonize completed: " + std::to_string(featureCount) + " polygons", output);
    // N143：polygonize 与 difference/intersect 同族，此前零 metadata。这里的 feature_count 是上面
    // 那段 while **重扫输出图层**数出来的面数（扫的是待提交的临时件图层本身，不是 GDALPolygonize
    // 的返回码，也没有再开一次文件），所以它与产物同一体 ⇒ 真实腿可以直接读这个键，
    // 不必再正则抠消息。
    mergeStandardRunMetadata(result, params, "polygonize");
    result.metadata["feature_count"] = std::to_string(featureCount);
    return result;
}

gis::framework::Result VectorPlugin::doConvert(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string format = gis::framework::getParam<std::string>(params, "format", "GeoJSON");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS.get(), layerName);
    if (!srcLayer) { return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    GDALDriver* drv = getDriverForFormat(format);
    if (!drv) { return gis::framework::Result::fail("Unsupported format: " + format);
    }

    namespace fs = std::filesystem;
    // 失败契约的入口闸（等价判定"输出不得覆盖输入"）走在任何建目录/建文件之前：建目录归
    // staged.start 自己做，所以这里不再先动文件系统。旧产物的前置删除（含 Shapefile 同 stem
    // 边车循环，N122 那条）同样已去掉 —— 它的对应物是 commit 末尾的 removeStaleOldSidecars，
    // 删除只许发生在新件全部落地之后。
    // 注意 doConvert 的格式来自参数 format（不是从扩展名推出来的），staged.start 的第三个
    // 参数因此必须传 format，否则 Shapefile 的边车落点会按扩展名之外的格式走错。
    VectorStagedOutput staged;
    std::string stageError;
    if (!staged.start(output, format, { input }, drv, stageError)) {
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

    progress.onProgress(0.3);

    srcLayer->ResetReading();
    OGRFeature* feat;
    int count = 0;
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
        dstFeat->SetFrom(feat);
        if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
            delete srcSRS;
            return gis::framework::Result::fail("Failed to write feature");
        }
        OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
        count++;
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
        "Convert completed: " + std::to_string(count) + " features converted to " + format, output);
    mergeStandardRunMetadata(result, params, "convert");
    result.metadata["feature_count"] = std::to_string(count);
    result.metadata["output_format"] = format;
    return result;
}
