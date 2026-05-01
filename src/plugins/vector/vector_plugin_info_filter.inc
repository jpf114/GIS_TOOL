gis::framework::Result VectorPlugin::doInfo(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");

    progress.onProgress(0.1);

    GDALDataset* ds = openVector(input, true);
    if (!ds) {
        return gis::framework::Result::fail("Cannot open vector file: " + input);
    }

    std::ostringstream oss;
    oss << "Vector Info: " << input << "\n";
    oss << "  Driver: " << ds->GetDriver()->GetDescription() << "\n";
    oss << "  Layers: " << ds->GetLayerCount() << "\n";

    int layerIdx = 0;
    while (auto* layer = ds->GetLayer(layerIdx)) {
        if (!layerName.empty() && layerName != layer->GetName()) {
            layerIdx++;
            continue;
        }

        oss << "\n  Layer [" << layerIdx << "]: " << layer->GetName() << "\n";

        OGRwkbGeometryType geomType = layer->GetGeomType();
        oss << "    Geometry: " << geometryTypeName(geomType) << "\n";

        OGREnvelope extent;
        if (layer->GetExtent(&extent, TRUE) == OGRERR_NONE) {
            oss << "    Extent:  " << extent.MinX << ", " << extent.MinY
                << " - " << extent.MaxX << ", " << extent.MaxY << "\n";
        }

        const OGRSpatialReference* srs = layer->GetSpatialRef();
        if (srs) {
            const char* authName = srs->GetAuthorityName(nullptr);
            const char* authCode = srs->GetAuthorityCode(nullptr);
            if (authName && authCode) {
                oss << "    CRS:     " << authName << ":" << authCode << "\n";
            } else {
                char* wkt = nullptr;
                srs->exportToWkt(&wkt);
                if (wkt) {
                    std::string wktStr(wkt);
                    if (wktStr.length() > 80) wktStr = wktStr.substr(0, 77) + "...";
                    oss << "    CRS:     " << wktStr << "\n";
                    CPLFree(wkt);
                }
            }
        } else {
            oss << "    CRS:     (none)\n";
        }

        GIntBig featureCount = layer->GetFeatureCount(FALSE);
        oss << "    Features: " << featureCount << "\n";

        auto* layerDefn = layer->GetLayerDefn();
        int fieldCount = layerDefn->GetFieldCount();
        oss << "    Fields:  " << fieldCount << "\n";
        for (int i = 0; i < fieldCount; ++i) {
            auto* fieldDefn = layerDefn->GetFieldDefn(i);
            oss << "      [" << i << "] " << fieldDefn->GetNameRef()
                << " (" << OGRFieldDefn::GetFieldTypeName(fieldDefn->GetType()) << ")\n";
        }

        layerIdx++;
    }

    GDALClose(ds);
    progress.onProgress(1.0);

    return gis::framework::Result::ok(oss.str());
}

gis::framework::Result VectorPlugin::doFilter(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string where = gis::framework::getParam<std::string>(params, "where", "");
    auto extentArr = gis::framework::getParam<std::array<double,4>>(params, "extent", {0,0,0,0});

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (where.empty() && extentArr[0] == 0 && extentArr[1] == 0 &&
        extentArr[2] == 0 && extentArr[3] == 0) {
        return gis::framework::Result::fail("At least one of --where or --extent must be specified");
    }

    progress.onProgress(0.1);

    GDALDataset* srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS, layerName);
    if (!srcLayer) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    if (!where.empty()) srcLayer->SetAttributeFilter(where.c_str());

    bool hasExtent = extentArr[0] != 0 || extentArr[1] != 0 ||
                     extentArr[2] != 0 || extentArr[3] != 0;
    if (hasExtent) {
        srcLayer->SetSpatialFilterRect(extentArr[0], extentArr[1], extentArr[2], extentArr[3]);
    }

    progress.onProgress(0.3);

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
        resultLayerGeometryType(srcLayer->GetGeomType()),
        layerOptions.empty() ? nullptr : layerOptions.data());
    if (!dstLayer) {
        GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    progress.onProgress(0.4);

    srcLayer->ResetReading();
    OGRFeature* feat;
    int count = 0;
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
        dstFeat->SetFrom(feat);
        if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
            GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
            return gis::framework::Result::fail("Failed to write feature");
        }
        OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
        count++;
    }

    GDALClose(dstDS); GDALClose(srcDS); delete srcSRS;
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Filter completed: " + std::to_string(count) + " features written", output);
}
