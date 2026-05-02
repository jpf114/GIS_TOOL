namespace {

static void appendPointsForOrientation(const OGRGeometry* geom, std::vector<OGRPoint>& points) {
    if (!geom || geom->IsEmpty()) {
        return;
    }

    switch (wkbFlatten(geom->getGeometryType())) {
        case wkbPoint: {
            const auto* point = geom->toPoint();
            if (point) {
                points.push_back(*point);
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
                points.push_back(point);
            }
            break;
        }
        case wkbPolygon: {
            const auto* polygon = geom->toPolygon();
            if (!polygon) {
                break;
            }
            const auto* ring = polygon->getExteriorRing();
            if (!ring) {
                break;
            }
            for (int i = 0; i < ring->getNumPoints(); ++i) {
                OGRPoint point;
                ring->getPoint(i, &point);
                points.push_back(point);
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
                appendPointsForOrientation(collection->getGeometryRef(i), points);
            }
            break;
        }
        default:
            break;
    }
}

static double computeOrientationDegrees(const OGRGeometry* geom) {
    std::vector<OGRPoint> points;
    appendPointsForOrientation(geom, points);
    if (points.size() < 2) {
        return 0.0;
    }

    double meanX = 0.0;
    double meanY = 0.0;
    for (const auto& point : points) {
        meanX += point.getX();
        meanY += point.getY();
    }
    meanX /= static_cast<double>(points.size());
    meanY /= static_cast<double>(points.size());

    double sxx = 0.0;
    double syy = 0.0;
    double sxy = 0.0;
    for (const auto& point : points) {
        const double dx = point.getX() - meanX;
        const double dy = point.getY() - meanY;
        sxx += dx * dx;
        syy += dy * dy;
        sxy += dx * dy;
    }

    const double angleRad = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
    double angleDeg = angleRad * 180.0 / M_PI;
    if (angleDeg < 0.0) {
        angleDeg += 180.0;
    }
    return angleDeg;
}

static double computeGeometryArea(const OGRGeometry* geom) {
    if (!geom || geom->IsEmpty()) {
        return 0.0;
    }

    switch (wkbFlatten(geom->getGeometryType())) {
        case wkbPolygon:
            return geom->toPolygon()->get_Area();
        case wkbMultiPolygon:
            return geom->toMultiPolygon()->get_Area();
        default:
            return 0.0;
    }
}

static double computeGeometryLength(const OGRGeometry* geom) {
    if (!geom || geom->IsEmpty()) {
        return 0.0;
    }

    switch (wkbFlatten(geom->getGeometryType())) {
        case wkbLineString:
            return geom->toLineString()->get_Length();
        case wkbMultiLineString:
            return geom->toMultiLineString()->get_Length();
        case wkbPolygon:
        case wkbMultiPolygon: {
            std::unique_ptr<OGRGeometry> boundary(geom->Boundary());
            return computeGeometryLength(boundary.get());
        }
        default:
            return 0.0;
    }
}

} // namespace

gis::framework::Result VectorPlugin::doGeomMetrics(
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
        return gis::framework::Result::fail("Cannot find layer");
    }

    if (detectSrsType(srcLayer->GetSpatialRef()) != "projected") {
        GDALClose(srcDS);
        return failProjectedRequired("GeomMetrics");
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
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(),
        srcSRS,
        resultLayerGeometryType(srcLayer->GetGeomType()),
        nullptr);
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

    OGRFieldDefn areaField("geom_area", OFTReal);
    areaField.SetWidth(24);
    areaField.SetPrecision(6);
    dstLayer->CreateField(&areaField);

    OGRFieldDefn lengthField("geom_length", OFTReal);
    lengthField.SetWidth(24);
    lengthField.SetPrecision(6);
    dstLayer->CreateField(&lengthField);

    OGRFieldDefn compactnessField("compactness", OFTReal);
    compactnessField.SetWidth(24);
    compactnessField.SetPrecision(6);
    dstLayer->CreateField(&compactnessField);

    OGRFieldDefn circularityField("circularity", OFTReal);
    circularityField.SetWidth(24);
    circularityField.SetPrecision(6);
    dstLayer->CreateField(&circularityField);

    OGRFieldDefn orientationField("orientation", OFTReal);
    orientationField.SetWidth(24);
    orientationField.SetPrecision(6);
    dstLayer->CreateField(&orientationField);

    progress.onProgress(0.35);

    srcLayer->ResetReading();
    OGRFeature* feat = nullptr;
    int count = 0;
    const GIntBig totalCount = std::max<GIntBig>(1, srcLayer->GetFeatureCount());
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
        dstFeat->SetFrom(feat);

        OGRGeometry* geom = feat->GetGeometryRef();
        if (geom) {
            const auto flatType = wkbFlatten(geom->getGeometryType());
            double area = 0.0;
            double length = 0.0;
            double compactness = 0.0;
            double circularity = 0.0;
            if (flatType == wkbPolygon || flatType == wkbMultiPolygon) {
                area = computeGeometryArea(geom);
                length = computeGeometryLength(geom);
                if (length > 0.0) {
                    compactness = 4.0 * M_PI * area / (length * length);
                    circularity = compactness;
                }
            } else if (flatType == wkbLineString || flatType == wkbMultiLineString) {
                length = computeGeometryLength(geom);
            }
            const double orientation = computeOrientationDegrees(geom);

            dstFeat->SetField("geom_area", area);
            dstFeat->SetField("geom_length", length);
            dstFeat->SetField("compactness", compactness);
            dstFeat->SetField("circularity", circularity);
            dstFeat->SetField("orientation", orientation);
        }

        if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(dstFeat);
            OGRFeature::DestroyFeature(feat);
            GDALClose(dstDS);
            GDALClose(srcDS);
            delete srcSRS;
            return gis::framework::Result::fail("Cannot create geometry metrics feature");
        }

        OGRFeature::DestroyFeature(dstFeat);
        OGRFeature::DestroyFeature(feat);
        ++count;
        progress.onProgress(0.35 + 0.55 * static_cast<double>(count) / static_cast<double>(totalCount));
    }

    GDALClose(dstDS);
    GDALClose(srcDS);
    delete srcSRS;
    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "GeomMetrics completed: " + std::to_string(count) + " features", output);
    result.metadata["feature_count"] = std::to_string(count);
    return result;
}
