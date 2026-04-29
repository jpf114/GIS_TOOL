gis::framework::Result VectorPlugin::doDissolve(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string dissolveField = gis::framework::getParam<std::string>(params, "dissolve_field", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    if (!srcLayer) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot find layer"); }

    const OGRSpatialReference* srcSRSRef = srcLayer->GetSpatialRef();
    if (detectSrsType(srcSRSRef) == "geographic") {
        GDALClose(srcDS);
        return failProjectedRequired("Dissolve");
    }

    progress.onProgress(0.2);

    std::map<std::string, std::vector<OGRGeometry*>> groups;
    std::vector<OGRGeometry*> allGeoms;

    srcLayer->ResetReading();
    OGRFeature* feat;
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = cloneNormalizedGeometry(feat->GetGeometryRef());
        if (!geom) { OGRFeature::DestroyFeature(feat); continue; }

        if (!dissolveField.empty()) {
            int fieldIdx = srcLayer->GetLayerDefn()->GetFieldIndex(dissolveField.c_str());
            if (fieldIdx >= 0) {
                std::string val = feat->GetFieldAsString(fieldIdx);
                groups[val].push_back(geom);
            } else {
                allGeoms.push_back(geom);
            }
        } else {
            allGeoms.push_back(geom);
        }
        OGRFeature::DestroyFeature(feat);
    }

    progress.onProgress(0.4);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::path(output).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot get driver"); }

    if (outFormat == "ESRI Shapefile" && fs::exists(output)) {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) fs::remove(entry.path());
        }
    }

    GDALDataset* dstDS = drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDS) { GDALClose(srcDS); return gis::framework::Result::fail("Cannot create output"); }

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
        GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    if (!dissolveField.empty()) {
        OGRFieldDefn fieldDefn(dissolveField.c_str(), OFTString);
        dstLayer->CreateField(&fieldDefn);
    }

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
                    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
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
                    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
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
        addSpatialIndexIfNeeded(dstDS, dstLayer, useTransactions, "dissolved");
    }
    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Dissolve completed: " + std::to_string(count) + " features", output);
}
