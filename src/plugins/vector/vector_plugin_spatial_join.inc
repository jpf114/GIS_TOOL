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

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    GDALDataset* joinDS = openVector(joinVector, true);
    if (!joinDS) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot open join vector file: " + joinVector);
    }

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    OGRLayer* joinLayer = joinDS->GetLayer(0);
    if (!srcLayer || !joinLayer) {
        GDALClose(joinDS);
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), joinLayer->GetSpatialRef())) {
        GDALClose(joinDS);
        GDALClose(srcDS);
        return failCrsMismatch("Spatial join");
    }

    int joinFieldIndex = -1;
    if (!joinField.empty()) {
        joinFieldIndex = joinLayer->GetLayerDefn()->GetFieldIndex(joinField.c_str());
        if (joinFieldIndex < 0) {
            GDALClose(joinDS);
            GDALClose(srcDS);
            return gis::framework::Result::fail("join_field not found: " + joinField);
        }
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
        GDALClose(joinDS);
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    ensureParentDirectoryForFile(output);
    removeExistingVectorOutput(output, outFormat);

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) {
        GDALClose(joinDS);
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot create output file: " + output);
    }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(),
        srcSRS,
        resultLayerGeometryType(effectiveLayerGeometryType(srcLayer)),
        nullptr);
    if (!dstLayer) {
        GDALClose(dstDS);
        GDALClose(joinDS);
        GDALClose(srcDS);
        delete srcSRS;
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

    progress.onProgress(0.35);

    srcLayer->ResetReading();
    OGRFeature* srcFeat = nullptr;
    int count = 0;
    const GIntBig rawTotalCount = srcLayer->GetFeatureCount();
    const GIntBig totalCount = rawTotalCount > 0 ? rawTotalCount : 1;
    const bool sameDataset = fs::equivalent(fs::u8path(input), fs::u8path(joinVector));
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
            GDALClose(dstDS);
            GDALClose(joinDS);
            GDALClose(srcDS);
            delete srcSRS;
            return gis::framework::Result::fail("Cannot create spatial join feature");
        }

        OGRFeature::DestroyFeature(dstFeat);
        OGRFeature::DestroyFeature(srcFeat);
        ++count;
        progress.onProgress(0.35 + 0.55 * static_cast<double>(count) / static_cast<double>(totalCount));
    }

    GDALClose(dstDS);
    GDALClose(joinDS);
    GDALClose(srcDS);
    delete srcSRS;
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Spatial join completed: " + std::to_string(count) + " features", output);
    result.metadata["feature_count"] = std::to_string(count);
    result.metadata["join_vector"] = joinVector;
    return result;
}
