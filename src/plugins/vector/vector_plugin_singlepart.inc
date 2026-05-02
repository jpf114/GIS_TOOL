namespace {

static void collectSinglepartGeometries(const OGRGeometry* geom, std::vector<std::unique_ptr<OGRGeometry>>& parts) {
    if (!geom || geom->IsEmpty()) {
        return;
    }

    const auto flatType = wkbFlatten(geom->getGeometryType());
    if (flatType == wkbMultiPoint || flatType == wkbMultiLineString ||
        flatType == wkbMultiPolygon || flatType == wkbGeometryCollection) {
        const auto* collection = geom->toGeometryCollection();
        for (int i = 0; i < collection->getNumGeometries(); ++i) {
            collectSinglepartGeometries(collection->getGeometryRef(i), parts);
        }
        return;
    }

    parts.emplace_back(geom->clone());
}

static OGRwkbGeometryType singlepartLayerGeometryType(OGRwkbGeometryType srcType) {
    switch (wkbFlatten(srcType)) {
        case wkbMultiPoint:
        case wkbPoint:
            return wkbPoint;
        case wkbMultiLineString:
        case wkbLineString:
            return wkbLineString;
        case wkbMultiPolygon:
        case wkbPolygon:
            return wkbPolygon;
        default:
            return wkbUnknown;
    }
}

} // namespace

gis::framework::Result VectorPlugin::doSinglepart(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    const std::string input = gis::framework::getParam<std::string>(params, "input", "");
    const std::string output = gis::framework::getParam<std::string>(params, "output", "");
    const std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    if (!srcLayer) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    const OGRwkbGeometryType srcType = effectiveLayerGeometryType(srcLayer);
    const OGRwkbGeometryType dstType = singlepartLayerGeometryType(srcType);
    if (dstType == wkbUnknown) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Singlepart currently only supports point, line and polygon geometries");
    }

    progress.onProgress(0.2);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";
    else if (ext == ".kml") outFormat = "KML";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    ensureParentDirectoryForFile(output);
    removeExistingVectorOutput(output, outFormat);

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    const bool useTransactions = outFormat == "GPKG";
    std::vector<std::string> layerOptionStorage = buildGpkgFastWriteLayerOptions(useTransactions);
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(),
        srcSRS,
        dstType,
        layerOptions.empty() ? nullptr : layerOptions.data());
    if (!dstLayer) {
        GDALClose(dstDS);
        GDALClose(srcDS);
        delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    progress.onProgress(0.35);

    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    int outputCount = 0;
    int sourceCount = 0;
    int skippedCount = 0;
    const GIntBig totalCount = std::max<GIntBig>(1, srcLayer->GetFeatureCount());
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        std::unique_ptr<OGRGeometry> normalized(cloneNormalizedGeometry(feat->GetGeometryRef()));
        if (!normalized || normalized->IsEmpty()) {
            ++skippedCount;
            OGRFeature::DestroyFeature(feat);
            ++sourceCount;
            progress.onProgress(0.35 + 0.55 * static_cast<double>(sourceCount) /
                static_cast<double>(totalCount));
            continue;
        }

        std::vector<std::unique_ptr<OGRGeometry>> parts;
        collectSinglepartGeometries(normalized.get(), parts);
        for (auto& part : parts) {
            if (!writeFeatureWithCopiedFieldsOwned(dstLayer, feat, std::move(part))) {
                OGRFeature::DestroyFeature(feat);
                GDALClose(dstDS);
                GDALClose(srcDS);
                delete srcSRS;
                return gis::framework::Result::fail("Cannot create singlepart feature");
            }
            ++outputCount;
        }

        OGRFeature::DestroyFeature(feat);
        ++sourceCount;
        progress.onProgress(0.35 + 0.55 * static_cast<double>(sourceCount) /
            static_cast<double>(totalCount));
    }

    GDALClose(dstDS);
    GDALClose(srcDS);
    delete srcSRS;
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Singlepart completed: " + std::to_string(outputCount) + " features", output);
    result.metadata["feature_count"] = std::to_string(outputCount);
    result.metadata["source_count"] = std::to_string(sourceCount);
    result.metadata["skipped_count"] = std::to_string(skippedCount);
    return result;
}
