namespace {

static std::unique_ptr<OGRGeometry> buildSliverFilteredGeometry(
    const OGRGeometry* geom,
    double minArea,
    int& removedPartCount) {
    if (!geom || geom->IsEmpty()) {
        return nullptr;
    }

    auto filtered = std::make_unique<OGRMultiPolygon>();
    switch (wkbFlatten(geom->getGeometryType())) {
        case wkbPolygon: {
            const auto* polygon = geom->toPolygon();
            if (!polygon) {
                return nullptr;
            }
            if (polygon->get_Area() < minArea) {
                ++removedPartCount;
                return nullptr;
            }
            filtered->addGeometry(polygon);
            break;
        }
        case wkbMultiPolygon: {
            const auto* multiPolygon = geom->toMultiPolygon();
            if (!multiPolygon) {
                return nullptr;
            }
            for (int i = 0; i < multiPolygon->getNumGeometries(); ++i) {
                const auto* polygon = multiPolygon->getGeometryRef(i)->toPolygon();
                if (!polygon) {
                    continue;
                }
                if (polygon->get_Area() < minArea) {
                    ++removedPartCount;
                    continue;
                }
                filtered->addGeometry(polygon);
            }
            break;
        }
        default:
            return nullptr;
    }

    if (filtered->IsEmpty()) {
        return nullptr;
    }
    return filtered;
}

} // namespace

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

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    if (!srcLayer) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    const auto geomType = wkbFlatten(effectiveLayerGeometryType(srcLayer));
    if (geomType != wkbPolygon && geomType != wkbMultiPolygon) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Sliver remove currently only supports polygon layers");
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
        wkbMultiPolygon,
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
            progress.onProgress(0.35 + 0.55 * static_cast<double>(sourceCount) /
                static_cast<double>(totalCount));
            continue;
        }

        if (!writeFeatureWithCopiedFieldsOwned(dstLayer, feat, std::move(filtered))) {
            OGRFeature::DestroyFeature(feat);
            GDALClose(dstDS);
            GDALClose(srcDS);
            delete srcSRS;
            return gis::framework::Result::fail("Cannot create sliver-removed feature");
        }

        OGRFeature::DestroyFeature(feat);
        ++featureCount;
        ++sourceCount;
        progress.onProgress(0.35 + 0.55 * static_cast<double>(sourceCount) /
            static_cast<double>(totalCount));
    }

    GDALClose(dstDS);
    GDALClose(srcDS);
    delete srcSRS;
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
