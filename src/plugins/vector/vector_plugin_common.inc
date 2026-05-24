static void mergeStandardRunMetadata(
    gis::framework::Result& result,
    const std::map<std::string, gis::framework::ParamValue>& params,
    const char* action) {
    std::map<std::string, std::string> fields;
    fields[gis::framework::ResultMetadataKeys::kAction] = action;
    const auto input = gis::framework::getParam<std::string>(params, "input", "");
    const auto output = gis::framework::getParam<std::string>(params, "output", "");
    if (!input.empty()) {
        fields[gis::framework::ResultMetadataKeys::kInput] = input;
    }
    if (!output.empty()) {
        fields[gis::framework::ResultMetadataKeys::kOutput] = output;
    }
    gis::framework::mergeResultMetadata(result.metadata, fields);
}

static gis::core::GdalDatasetPtr openVector(const std::string& path, bool readOnly = true) {
    const std::string resolvedPath = resolveVectorInputPath(path);
    return gis::core::GdalDatasetPtr(
        static_cast<GDALDataset*>(GDALOpenEx(
            resolvedPath.c_str(),
            (readOnly ? GDAL_OF_READONLY : GDAL_OF_UPDATE) | GDAL_OF_VECTOR,
            nullptr, nullptr, nullptr)));
}

static OGRLayer* getLayer(GDALDataset* ds, const std::string& layerName) {
    if (!layerName.empty()) {
        if (auto* layer = ds->GetLayerByName(layerName.c_str())) {
            return layer;
        }
        if (ds->GetLayerCount() == 1) {
            return ds->GetLayer(0);
        }
        return nullptr;
    }
    return ds->GetLayer(0);
}

static std::string geometryTypeName(OGRwkbGeometryType type) {
    switch (wkbFlatten(type)) {
        case wkbPoint:           return "Point";
        case wkbMultiPoint:      return "MultiPoint";
        case wkbLineString:      return "LineString";
        case wkbMultiLineString: return "MultiLineString";
        case wkbPolygon:         return "Polygon";
        case wkbMultiPolygon:    return "MultiPolygon";
        case wkbGeometryCollection: return "GeometryCollection";
        default:                 return "Unknown";
    }
}

static GDALDriver* getDriverForFormat(const std::string& format) {
    if (format == "GeoJSON")         return GetGDALDriverManager()->GetDriverByName("GeoJSON");
    if (format == "ESRI Shapefile")  return GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
    if (format == "GPKG")            return GetGDALDriverManager()->GetDriverByName("GPKG");
    if (format == "KML")             return GetGDALDriverManager()->GetDriverByName("KML");
    if (format == "CSV")             return GetGDALDriverManager()->GetDriverByName("CSV");
    return nullptr;
}

static std::string detectSrsType(const OGRSpatialReference* srs) {
    if (!srs) {
        return "unknown";
    }
    if (srs->IsGeographic()) {
        return "geographic";
    }
    if (srs->IsProjected()) {
        return "projected";
    }
    return "unknown";
}

static bool areSameCrs(const OGRSpatialReference* lhs, const OGRSpatialReference* rhs) {
    if (!lhs || !rhs) {
        return lhs == rhs;
    }
    return lhs->IsSame(rhs);
}

static gis::framework::Result failProjectedRequired(const std::string& action) {
    return gis::framework::Result::fail(
        action + " requires a projected coordinate system. Please reproject the input layer before processing.");
}

static gis::framework::Result failCrsMismatch(const std::string& action) {
    return gis::framework::Result::fail(
        action + " requires input layers to use the same CRS.");
}

static gis::framework::Result failInvalidGeometry(const std::string& action) {
    return gis::framework::Result::fail(
        action + " requires valid overlay geometries and automatic repair failed.");
}

class ScopedQuietGdalErrorHandler {
public:
    ScopedQuietGdalErrorHandler() {
        CPLPushErrorHandler(CPLQuietErrorHandler);
    }

    ~ScopedQuietGdalErrorHandler() {
        CPLPopErrorHandler();
    }
};

static OGRGeometry* cloneNormalizedGeometry(const OGRGeometry* geom) {
    if (!geom) {
        return nullptr;
    }

    OGRGeometry* cloned = geom->clone();
    if (!cloned || cloned->IsEmpty()) {
        return cloned;
    }

    bool isValid = false;
    {
        ScopedQuietGdalErrorHandler quietErrors;
        isValid = cloned->IsValid();
    }
    if (isValid) {
        return cloned;
    }

    OGRGeometry* fixed = nullptr;
    {
        ScopedQuietGdalErrorHandler quietErrors;
        fixed = cloned->MakeValid();
    }
    delete cloned;
    if (!fixed || fixed->IsEmpty()) {
        delete fixed;
        return nullptr;
    }

    return fixed;
}

static OGRGeometry* buildNormalizedUnion(OGRLayer* layer) {
    if (!layer) {
        return nullptr;
    }

    std::vector<OGRGeometry*> geoms;
    layer->ResetReading();
    OGRFeature* feat = nullptr;
    while ((feat = layer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = cloneNormalizedGeometry(feat->GetGeometryRef());
        if (geom) {
            geoms.push_back(geom);
        }
        OGRFeature::DestroyFeature(feat);
    }

    if (geoms.empty()) {
        return nullptr;
    }

    OGRGeometry* merged = geoms[0];
    for (size_t i = 1; i < geoms.size(); ++i) {
        OGRGeometry* next = merged ? merged->Union(geoms[i]) : nullptr;
        delete merged;
        delete geoms[i];
        if (!next || next->IsEmpty()) {
            delete next;
            return nullptr;
        }
        merged = next;
    }

    return merged;
}

struct OverlayGeometryEntry {
    std::unique_ptr<OGRGeometry> geometry;
    OGREnvelope envelope{};
};

static OGRGeometry* unionGeometryList(std::vector<OGRGeometry*>& geoms);

static std::vector<OverlayGeometryEntry> collectNormalizedGeometries(OGRLayer* layer) {
    std::vector<OverlayGeometryEntry> entries;
    if (!layer) {
        return entries;
    }

    layer->ResetReading();
    OGRFeature* feat = nullptr;
    while ((feat = layer->GetNextFeature()) != nullptr) {
        std::unique_ptr<OGRGeometry> geom(cloneNormalizedGeometry(feat->GetGeometryRef()));
        if (geom && !geom->IsEmpty()) {
            OverlayGeometryEntry entry;
            geom->getEnvelope(&entry.envelope);
            entry.geometry = std::move(geom);
            entries.push_back(std::move(entry));
        }
        OGRFeature::DestroyFeature(feat);
    }

    return entries;
}

static bool envelopesIntersect(const OGREnvelope& lhs, const OGREnvelope& rhs) {
    return !(lhs.MaxX < rhs.MinX || lhs.MinX > rhs.MaxX || lhs.MaxY < rhs.MinY || lhs.MinY > rhs.MaxY);
}

static double computeEnvelopeOverlapRatio(const std::vector<OverlayGeometryEntry>& entries) {
    if (entries.size() < 2) {
        return 0.0;
    }

    size_t overlapPairs = 0;
    size_t totalPairs = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        for (size_t j = i + 1; j < entries.size(); ++j) {
            ++totalPairs;
            if (envelopesIntersect(entries[i].envelope, entries[j].envelope)) {
                ++overlapPairs;
            }
        }
    }

    return totalPairs == 0 ? 0.0 : static_cast<double>(overlapPairs) / static_cast<double>(totalPairs);
}

static OGRGeometry* buildCandidateOverlayGeometry(
    const std::vector<OverlayGeometryEntry>& entries,
    const OGRGeometry* target) {
    if (!target) {
        return nullptr;
    }

    OGREnvelope targetEnvelope;
    target->getEnvelope(&targetEnvelope);

    OGRGeometry* merged = nullptr;
    for (const auto& entry : entries) {
        if (!envelopesIntersect(entry.envelope, targetEnvelope)) {
            continue;
        }

        if (!merged) {
            merged = entry.geometry->clone();
            continue;
        }

        OGRGeometry* next = merged->Union(entry.geometry.get());
        delete merged;
        merged = next;
        if (!merged || merged->IsEmpty()) {
            delete merged;
            return nullptr;
        }
    }

    return merged;
}

static OGRGeometry* buildMergedOverlayGeometry(
    const std::vector<OverlayGeometryEntry>& entries) {
    std::vector<OGRGeometry*> geometries;
    geometries.reserve(entries.size());
    for (const auto& entry : entries) {
        if (!entry.geometry || entry.geometry->IsEmpty()) {
            continue;
        }
        geometries.push_back(entry.geometry->clone());
    }
    return unionGeometryList(geometries);
}

static std::vector<const OverlayGeometryEntry*> collectCandidateOverlayEntries(
    const std::vector<OverlayGeometryEntry>& entries,
    const OGRGeometry* target) {
    std::vector<const OverlayGeometryEntry*> candidates;
    if (!target) {
        return candidates;
    }

    OGREnvelope targetEnvelope;
    target->getEnvelope(&targetEnvelope);
    for (const auto& entry : entries) {
        if (envelopesIntersect(entry.envelope, targetEnvelope)) {
            candidates.push_back(&entry);
        }
    }
    return candidates;
}

static void removeExistingVectorOutput(const std::string& output, const std::string& format) {
    namespace fs = std::filesystem;
    if (!fs::exists(output)) {
        return;
    }

    if (format == "ESRI Shapefile") {
        for (const auto& entry : fs::directory_iterator(fs::path(output).parent_path())) {
            if (entry.path().stem() == fs::path(output).stem()) {
                fs::remove(entry.path());
            }
        }
        return;
    }

    fs::remove(output);
}

static void ensureParentDirectoryForFile(const std::string& output) {
    const fs::path outputPath = fs::u8path(output);
    if (!outputPath.has_parent_path()) {
        return;
    }

    fs::create_directories(outputPath.parent_path());
}

static std::string escapeSqlLiteral(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        escaped.push_back(ch);
        if (ch == '\'') {
            escaped.push_back('\'');
        }
    }
    return escaped;
}

static std::vector<std::string> buildGpkgFastWriteLayerOptions(bool useTransactions) {
    std::vector<std::string> options;
    if (useTransactions) {
        options.emplace_back("SPATIAL_INDEX=NO");
    }
    return options;
}

static std::vector<char*> buildLayerOptionPointers(std::vector<std::string>& options) {
    std::vector<char*> pointers;
    for (auto& option : options) {
        pointers.push_back(option.data());
    }
    pointers.push_back(nullptr);
    return pointers;
}

static void ensureGpkgExtensionsTable(GDALDataset* dataset) {
    if (!dataset) {
        return;
    }

    const char* createExtensionsSql =
        "CREATE TABLE IF NOT EXISTS gpkg_extensions ("
        "table_name TEXT,"
        "column_name TEXT,"
        "extension_name TEXT NOT NULL,"
        "definition TEXT NOT NULL,"
        "scope TEXT NOT NULL,"
        "CONSTRAINT ge_tce UNIQUE (table_name, column_name, extension_name)"
        ")";
    if (OGRLayer* sqlResult = dataset->ExecuteSQL(createExtensionsSql, nullptr, "SQLITE")) {
        dataset->ReleaseResultSet(sqlResult);
    }
}

static void addSpatialIndexIfNeeded(
    GDALDataset* dataset,
    OGRLayer* layer,
    bool useTransactions,
    const std::string& layerName) {
    if (!dataset || !layer || !useTransactions) {
        return;
    }

    const char* geometryColumn = layer->GetGeometryColumn();
    const std::string geometryColumnName =
        (geometryColumn && *geometryColumn) ? geometryColumn : "geom";
    ensureGpkgExtensionsTable(dataset);
    const std::string addSpatialIndexSql =
        "SELECT gpkgAddSpatialIndex('" + escapeSqlLiteral(layerName) +
        "', '" + escapeSqlLiteral(geometryColumnName) + "')";
    if (OGRLayer* sqlResult = dataset->ExecuteSQL(addSpatialIndexSql.c_str(), nullptr, "SQLITE")) {
        dataset->ReleaseResultSet(sqlResult);
    }
}

static OGRwkbGeometryType resultLayerGeometryType(OGRwkbGeometryType srcType) {
    switch (wkbFlatten(srcType)) {
        case wkbPoint:
        case wkbMultiPoint:
            return wkbMultiPoint;
        case wkbLineString:
        case wkbMultiLineString:
            return wkbMultiLineString;
        case wkbPolygon:
        case wkbMultiPolygon:
            return wkbMultiPolygon;
        default:
            return wkbUnknown;
    }
}

static int geometryDimensionRank(OGRwkbGeometryType type) {
    switch (wkbFlatten(type)) {
        case wkbPoint:
        case wkbMultiPoint:
            return 0;
        case wkbLineString:
        case wkbMultiLineString:
            return 1;
        case wkbPolygon:
        case wkbMultiPolygon:
            return 2;
        default:
            return -1;
    }
}

static bool isLinearGeometryType(OGRwkbGeometryType type) {
    const auto flatType = wkbFlatten(type);
    return flatType == wkbLineString || flatType == wkbMultiLineString;
}

static OGRwkbGeometryType effectiveLayerGeometryType(OGRLayer* layer) {
    if (!layer) {
        return wkbUnknown;
    }

    const OGRwkbGeometryType declaredType = layer->GetGeomType();
    if (geometryDimensionRank(declaredType) >= 0) {
        return declaredType;
    }

    layer->ResetReading();
    OGRFeature* feature = nullptr;
    while ((feature = layer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feature->GetGeometryRef();
        if (geom && !geom->IsEmpty()) {
            const OGRwkbGeometryType detectedType = geom->getGeometryType();
            OGRFeature::DestroyFeature(feature);
            layer->ResetReading();
            return detectedType;
        }
        OGRFeature::DestroyFeature(feature);
    }
    layer->ResetReading();
    return declaredType;
}

static void collectLinearGeometryParts(const OGRGeometry* geom, std::vector<OGRGeometry*>& parts) {
    if (!geom || geom->IsEmpty()) {
        return;
    }

    const auto flatType = wkbFlatten(geom->getGeometryType());
    if (flatType == wkbLineString) {
        parts.push_back(geom->clone());
        return;
    }

    if (flatType == wkbMultiLineString) {
        const auto* multiLine = geom->toMultiLineString();
        for (int i = 0; i < multiLine->getNumGeometries(); ++i) {
            parts.push_back(multiLine->getGeometryRef(i)->clone());
        }
        return;
    }

    if (flatType == wkbGeometryCollection) {
        const auto* collection = geom->toGeometryCollection();
        for (int i = 0; i < collection->getNumGeometries(); ++i) {
            collectLinearGeometryParts(collection->getGeometryRef(i), parts);
        }
    }
}

// --- Cached feature structs for topology/overlap checks ---

struct CachedPolygonFeature {
    GIntBig fid = 0;
    std::unique_ptr<OGRGeometry> geometry;
};

struct CachedTopologyFeature {
    GIntBig fid = 0;
    bool empty = false;
    bool invalid = false;
    std::unique_ptr<OGRGeometry> geometry;
};

static double polygonIntersectionArea(const OGRGeometry* lhs, const OGRGeometry* rhs) {
    if (!lhs || !rhs) return 0.0;
    std::unique_ptr<OGRGeometry> intersection(lhs->Intersection(rhs));
    if (!intersection || intersection->IsEmpty()) return 0.0;
    const auto flatType = wkbFlatten(intersection->getGeometryType());
    if (flatType == wkbPolygon) return intersection->toPolygon()->get_Area();
    if (flatType == wkbMultiPolygon) return intersection->toMultiPolygon()->get_Area();
    if (flatType == wkbGeometryCollection) {
        double total = 0.0;
        const auto* collection = intersection->toGeometryCollection();
        for (int i = 0; i < collection->getNumGeometries(); ++i) {
            const auto* sub = collection->getGeometryRef(i);
            const auto subType = wkbFlatten(sub->getGeometryType());
            if (subType == wkbPolygon) total += sub->toPolygon()->get_Area();
            else if (subType == wkbMultiPolygon) total += sub->toMultiPolygon()->get_Area();
        }
        return total;
    }
    return 0.0;
}

// --- Multipart/singlepart helpers ---

static int geometryPartCount(const OGRGeometry* geom) {
    if (!geom || geom->IsEmpty()) return 0;
    const auto flatType = wkbFlatten(geom->getGeometryType());
    switch (flatType) {
        case wkbMultiPoint:       return geom->toMultiPoint()->getNumGeometries();
        case wkbMultiLineString:  return geom->toMultiLineString()->getNumGeometries();
        case wkbMultiPolygon:     return geom->toMultiPolygon()->getNumGeometries();
        case wkbGeometryCollection: return geom->toGeometryCollection()->getNumGeometries();
        default:                  return 1;
    }
}

static bool isMultipartGeometryType(OGRwkbGeometryType type) {
    const auto flatType = wkbFlatten(type);
    return flatType == wkbMultiPoint || flatType == wkbMultiLineString ||
           flatType == wkbMultiPolygon || flatType == wkbGeometryCollection;
}

static OGRwkbGeometryType singlepartLayerGeometryType(OGRwkbGeometryType srcType) {
    switch (wkbFlatten(srcType)) {
        case wkbPoint:            return wkbPoint;
        case wkbMultiPoint:       return wkbPoint;
        case wkbLineString:       return wkbLineString;
        case wkbMultiLineString:  return wkbLineString;
        case wkbPolygon:          return wkbPolygon;
        case wkbMultiPolygon:     return wkbPolygon;
        default:                  return wkbUnknown;
    }
}

static void collectSinglepartGeometries(const OGRGeometry* geom, std::vector<std::unique_ptr<OGRGeometry>>& parts) {
    if (!geom || geom->IsEmpty()) return;
    const auto flatType = wkbFlatten(geom->getGeometryType());
    switch (flatType) {
        case wkbPoint:
        case wkbLineString:
        case wkbPolygon:
            parts.push_back(std::unique_ptr<OGRGeometry>(geom->clone()));
            break;
        case wkbMultiPoint: {
            const auto* multi = geom->toMultiPoint();
            for (int i = 0; i < multi->getNumGeometries(); ++i)
                parts.push_back(std::unique_ptr<OGRGeometry>(multi->getGeometryRef(i)->clone()));
            break;
        }
        case wkbMultiLineString: {
            const auto* multi = geom->toMultiLineString();
            for (int i = 0; i < multi->getNumGeometries(); ++i)
                parts.push_back(std::unique_ptr<OGRGeometry>(multi->getGeometryRef(i)->clone()));
            break;
        }
        case wkbMultiPolygon: {
            const auto* multi = geom->toMultiPolygon();
            for (int i = 0; i < multi->getNumGeometries(); ++i)
                parts.push_back(std::unique_ptr<OGRGeometry>(multi->getGeometryRef(i)->clone()));
            break;
        }
        case wkbGeometryCollection: {
            const auto* collection = geom->toGeometryCollection();
            for (int i = 0; i < collection->getNumGeometries(); ++i)
                collectSinglepartGeometries(collection->getGeometryRef(i), parts);
            break;
        }
        default:
            break;
    }
}

// --- Vertex extraction helpers ---

static void appendVertices(const OGRGeometry* geom, std::vector<OGRPoint>& vertices) {
    if (!geom || geom->IsEmpty()) return;
    const auto flatType = wkbFlatten(geom->getGeometryType());
    switch (flatType) {
        case wkbPoint: {
            const auto* pt = geom->toPoint();
            vertices.push_back(OGRPoint(pt->getX(), pt->getY(), pt->getZ()));
            break;
        }
        case wkbLineString: {
            const auto* line = geom->toLineString();
            for (int i = 0; i < line->getNumPoints(); ++i)
                vertices.push_back(OGRPoint(line->getX(i), line->getY(i), line->getZ(i)));
            break;
        }
        case wkbPolygon: {
            const auto* poly = geom->toPolygon();
            for (int r = 0; r < poly->getNumInteriorRings() + 1; ++r) {
                const auto* ring = r == 0 ? poly->getExteriorRing() : poly->getInteriorRing(r - 1);
                if (!ring) continue;
                for (int i = 0; i < ring->getNumPoints(); ++i)
                    vertices.push_back(OGRPoint(ring->getX(i), ring->getY(i), ring->getZ(i)));
            }
            break;
        }
        case wkbMultiPoint:
        case wkbMultiLineString:
        case wkbMultiPolygon:
        case wkbGeometryCollection: {
            const auto* collection = geom->toGeometryCollection();
            for (int i = 0; i < collection->getNumGeometries(); ++i)
                appendVertices(collection->getGeometryRef(i), vertices);
            break;
        }
        default:
            break;
    }
}

// --- Duplicate point check helpers ---

static std::string pointKey(const OGRPoint* point) {
    if (!point) return "";
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(10) << point->getX() << "," << point->getY();
    return oss.str();
}

// --- Hole check helpers ---

static int geometryHoleCount(const OGRGeometry* geom) {
    if (!geom || geom->IsEmpty()) return 0;
    const auto flatType = wkbFlatten(geom->getGeometryType());
    if (flatType == wkbPolygon) return geom->toPolygon()->getNumInteriorRings();
    if (flatType == wkbMultiPolygon) {
        int total = 0;
        const auto* multi = geom->toMultiPolygon();
        for (int i = 0; i < multi->getNumGeometries(); ++i)
            total += multi->getGeometryRef(i)->toPolygon()->getNumInteriorRings();
        return total;
    }
    if (flatType == wkbGeometryCollection) {
        int total = 0;
        const auto* collection = geom->toGeometryCollection();
        for (int i = 0; i < collection->getNumGeometries(); ++i)
            total += geometryHoleCount(collection->getGeometryRef(i));
        return total;
    }
    return 0;
}

// --- Dangling endpoint check helpers ---

struct DanglingEndpointRecord {
    GIntBig fid = 0;
    std::string endpointType;
    OGRPoint point;
};

static void appendDanglingEndpoints(const OGRGeometry* geom, std::vector<DanglingEndpointRecord>& endpoints, GIntBig fid) {
    if (!geom || geom->IsEmpty()) return;
    const auto flatType = wkbFlatten(geom->getGeometryType());
    if (flatType == wkbLineString) {
        const auto* line = geom->toLineString();
        if (line->getNumPoints() >= 2) {
            endpoints.push_back({fid, "start", OGRPoint(line->getX(0), line->getY(0))});
            endpoints.push_back({fid, "end", OGRPoint(line->getX(line->getNumPoints() - 1), line->getY(line->getNumPoints() - 1))});
        }
    } else if (flatType == wkbMultiLineString || flatType == wkbGeometryCollection) {
        const auto* collection = geom->toGeometryCollection();
        for (int i = 0; i < collection->getNumGeometries(); ++i)
            appendDanglingEndpoints(collection->getGeometryRef(i), endpoints, fid);
    }
}

static std::string danglingEndpointKey(const OGRPoint& point) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(10) << point.getX() << "," << point.getY();
    return oss.str();
}

// --- Adjacency helpers ---

static double sharedBoundaryLength(const OGRGeometry* lhs, const OGRGeometry* rhs) {
    if (!lhs || !rhs) return 0.0;
    std::unique_ptr<OGRGeometry> intersection(lhs->Intersection(rhs));
    if (!intersection || intersection->IsEmpty()) return 0.0;
    const auto flatType = wkbFlatten(intersection->getGeometryType());
    double total = 0.0;
    if (flatType == wkbLineString) {
        total = intersection->toLineString()->get_Length();
    } else if (flatType == wkbMultiLineString) {
        const auto* multi = intersection->toMultiLineString();
        for (int i = 0; i < multi->getNumGeometries(); ++i)
            total += multi->getGeometryRef(i)->toLineString()->get_Length();
    } else if (flatType == wkbGeometryCollection) {
        const auto* collection = intersection->toGeometryCollection();
        for (int i = 0; i < collection->getNumGeometries(); ++i) {
            const auto* sub = collection->getGeometryRef(i);
            const auto subType = wkbFlatten(sub->getGeometryType());
            if (subType == wkbLineString) total += sub->toLineString()->get_Length();
            else if (subType == wkbMultiLineString) {
                const auto* multi = sub->toMultiLineString();
                for (int j = 0; j < multi->getNumGeometries(); ++j)
                    total += multi->getGeometryRef(j)->toLineString()->get_Length();
            }
        }
    }
    return total;
}

// --- Sliver remove helpers ---

static std::unique_ptr<OGRGeometry> buildSliverFilteredGeometry(
    const OGRGeometry* geom, double minArea, int& removedCount) {
    removedCount = 0;
    if (!geom || geom->IsEmpty()) return nullptr;
    const auto flatType = wkbFlatten(geom->getGeometryType());
    if (flatType == wkbPolygon) {
        const auto* poly = geom->toPolygon();
        if (!poly->getExteriorRing() || poly->getExteriorRing()->IsEmpty()) return nullptr;
        const double area = poly->get_Area();
        if (area < minArea) { ++removedCount; return nullptr; }
        // Rebuild without interior rings that are too small (slivers)
        auto result = std::make_unique<OGRPolygon>();
        result->addRing(poly->getExteriorRing());
        for (int i = 0; i < poly->getNumInteriorRings(); ++i) {
            result->addRing(poly->getInteriorRing(i));
        }
        return result;
    }
    if (flatType == wkbMultiPolygon) {
        const auto* multi = geom->toMultiPolygon();
        auto result = std::make_unique<OGRMultiPolygon>();
        for (int i = 0; i < multi->getNumGeometries(); ++i) {
            int subRemoved = 0;
            auto sub = buildSliverFilteredGeometry(multi->getGeometryRef(i), minArea, subRemoved);
            removedCount += subRemoved;
            if (sub && !sub->IsEmpty()) result->addGeometry(sub.release());
        }
        if (result->IsEmpty()) return nullptr;
        return result;
    }
    // For other geometry types, return as-is
    return std::unique_ptr<OGRGeometry>(geom->clone());
}
