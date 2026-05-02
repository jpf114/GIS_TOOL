namespace {

static void appendVertices(
    const OGRGeometry* geom,
    std::vector<OGRPoint>& vertices) {
    if (!geom || geom->IsEmpty()) {
        return;
    }

    switch (wkbFlatten(geom->getGeometryType())) {
        case wkbPoint: {
            const auto* point = geom->toPoint();
            if (point) {
                vertices.push_back(*point);
            }
            break;
        }
        case wkbLineString: {
            const auto* line = geom->toLineString();
            if (!line) {
                break;
            }
            for (int i = 0; i < line->getNumPoints(); ++i) {
                OGRPoint point;
                line->getPoint(i, &point);
                vertices.push_back(point);
            }
            break;
        }
        case wkbPolygon: {
            const auto* polygon = geom->toPolygon();
            if (!polygon) {
                break;
            }
            const auto* exterior = polygon->getExteriorRing();
            if (exterior) {
                for (int i = 0; i < exterior->getNumPoints(); ++i) {
                    OGRPoint point;
                    exterior->getPoint(i, &point);
                    vertices.push_back(point);
                }
            }
            for (int ringIndex = 0; ringIndex < polygon->getNumInteriorRings(); ++ringIndex) {
                const auto* interior = polygon->getInteriorRing(ringIndex);
                if (!interior) {
                    continue;
                }
                for (int i = 0; i < interior->getNumPoints(); ++i) {
                    OGRPoint point;
                    interior->getPoint(i, &point);
                    vertices.push_back(point);
                }
            }
            break;
        }
        case wkbMultiPoint:
        case wkbMultiLineString:
        case wkbMultiPolygon:
        case wkbGeometryCollection: {
            const auto* collection = geom->toGeometryCollection();
            if (!collection) {
                break;
            }
            for (int i = 0; i < collection->getNumGeometries(); ++i) {
                appendVertices(collection->getGeometryRef(i), vertices);
            }
            break;
        }
        default:
            break;
    }
}

} // namespace

gis::framework::Result VectorPlugin::doVerticesExtract(
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
    OGRFieldDefn vertexField("vertex_idx", OFTInteger);
    dstLayer->CreateField(&vertexField);

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

        std::vector<OGRPoint> vertices;
        appendVertices(normalized.get(), vertices);
        for (size_t i = 0; i < vertices.size(); ++i) {
            if (!writeGeometryFeature(dstLayer, &vertices[i], [feat, i](OGRFeature* dstFeat) {
                    dstFeat->SetFrom(feat, TRUE);
                    dstFeat->SetFID(OGRNullFID);
                    dstFeat->SetField("source_fid", static_cast<long long>(feat->GetFID()));
                    dstFeat->SetField("vertex_idx", static_cast<int>(i));
                })) {
                OGRFeature::DestroyFeature(feat);
                GDALClose(dstDS);
                GDALClose(srcDS);
                delete srcSRS;
                return gis::framework::Result::fail("Cannot create vertex feature");
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
        "Vertices extract completed: " + std::to_string(outputCount) + " vertices", output);
    result.metadata["feature_count"] = std::to_string(outputCount);
    result.metadata["source_count"] = std::to_string(sourceCount);
    result.metadata["skipped_count"] = std::to_string(skippedCount);
    return result;
}
