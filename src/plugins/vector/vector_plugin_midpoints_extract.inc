namespace {

struct MidpointRecord {
    OGRPoint point;
};

static bool interpolateLineMidpoint(const OGRLineString* line, OGRPoint& midpoint) {
    if (!line || line->getNumPoints() <= 1) {
        return false;
    }

    const double totalLength = line->get_Length();
    if (totalLength <= 0.0) {
        line->StartPoint(&midpoint);
        return true;
    }

    const double target = totalLength * 0.5;
    double accumulated = 0.0;
    for (int i = 1; i < line->getNumPoints(); ++i) {
        OGRPoint startPoint;
        OGRPoint endPoint;
        line->getPoint(i - 1, &startPoint);
        line->getPoint(i, &endPoint);
        const double dx = endPoint.getX() - startPoint.getX();
        const double dy = endPoint.getY() - startPoint.getY();
        const double segmentLength = std::sqrt(dx * dx + dy * dy);
        if (segmentLength <= 0.0) {
            continue;
        }

        if (accumulated + segmentLength >= target) {
            const double remain = target - accumulated;
            const double ratio = remain / segmentLength;
            midpoint.setX(startPoint.getX() + dx * ratio);
            midpoint.setY(startPoint.getY() + dy * ratio);
            midpoint.setZ(startPoint.getZ() + (endPoint.getZ() - startPoint.getZ()) * ratio);
            return true;
        }
        accumulated += segmentLength;
    }

    line->EndPoint(&midpoint);
    return true;
}

static void appendMidpoints(
    const OGRGeometry* geom,
    std::vector<MidpointRecord>& midpoints) {
    if (!geom || geom->IsEmpty()) {
        return;
    }

    switch (wkbFlatten(geom->getGeometryType())) {
        case wkbLineString: {
            const auto* line = geom->toLineString();
            if (!line || line->getNumPoints() <= 1) {
                break;
            }
            const double target = line->get_Length() * 0.5;
            OGRPoint midpoint;
            if (interpolateLineMidpoint(line, midpoint)) {
                MidpointRecord record;
                record.point = midpoint;
                midpoints.push_back(record);
            }
            break;
        }
        case wkbMultiLineString:
        case wkbGeometryCollection: {
            const auto* collection = geom->toGeometryCollection();
            if (!collection) {
                break;
            }
            for (int i = 0; i < collection->getNumGeometries(); ++i) {
                appendMidpoints(collection->getGeometryRef(i), midpoints);
            }
            break;
        }
        default:
            break;
    }
}

} // namespace

gis::framework::Result VectorPlugin::doMidpointsExtract(
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

    const auto geomType = wkbFlatten(effectiveLayerGeometryType(srcLayer));
    if (geomType != wkbLineString && geomType != wkbMultiLineString) {
        GDALClose(srcDS);
        return gis::framework::Result::fail("Midpoints extract currently only supports line layers");
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
        wkbPoint,
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
    OGRFieldDefn sourceField("source_fid", OFTInteger64);
    dstLayer->CreateField(&sourceField);

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

        std::vector<MidpointRecord> midpoints;
        appendMidpoints(normalized.get(), midpoints);
        for (const auto& midpoint : midpoints) {
            if (!writeGeometryFeature(dstLayer, const_cast<OGRPoint*>(&midpoint.point), [feat](OGRFeature* dstFeat) {
                    dstFeat->SetFrom(feat, TRUE);
                    dstFeat->SetFID(OGRNullFID);
                    dstFeat->SetField("source_fid", static_cast<long long>(feat->GetFID()));
                })) {
                OGRFeature::DestroyFeature(feat);
                GDALClose(dstDS);
                GDALClose(srcDS);
                delete srcSRS;
                return gis::framework::Result::fail("Cannot create midpoint feature");
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
        "Midpoints extract completed: " + std::to_string(outputCount) + " midpoints", output);
    result.metadata["feature_count"] = std::to_string(outputCount);
    result.metadata["source_count"] = std::to_string(sourceCount);
    result.metadata["skipped_count"] = std::to_string(skippedCount);
    return result;
}
