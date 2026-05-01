static bool writeGeometryFeature(
    OGRLayer* layer,
    OGRGeometry* geom,
    const std::function<void(OGRFeature*)>& fillAttributes = {});

static bool writeGeometryFeatureOwned(
    OGRLayer* layer,
    std::unique_ptr<OGRGeometry> geom,
    const std::function<void(OGRFeature*)>& fillAttributes = {});

static std::unique_ptr<OGRGeometry> promoteGeometryToLayerType(
    OGRLayer* layer,
    OGRGeometry* geom) {
    if (!layer || !geom) {
        return nullptr;
    }

    const auto targetType = wkbFlatten(layer->GetGeomType());
    if (targetType == wkbMultiLineString && wkbFlatten(geom->getGeometryType()) == wkbLineString) {
        auto multi = std::make_unique<OGRMultiLineString>();
        multi->addGeometry(geom);
        return multi;
    }

    if (targetType == wkbMultiPolygon && wkbFlatten(geom->getGeometryType()) == wkbPolygon) {
        auto multi = std::make_unique<OGRMultiPolygon>();
        multi->addGeometry(geom);
        return multi;
    }

    return nullptr;
}

static bool writeUnionFeature(OGRLayer* layer, OGRGeometry* geom, const char* sourceValue) {
    return writeGeometryFeature(layer, geom, [sourceValue](OGRFeature* feature) {
        feature->SetField("source", sourceValue);
    });
}

static bool writeUnionFeatureOwned(
    OGRLayer* layer,
    std::unique_ptr<OGRGeometry> geom,
    const char* sourceValue) {
    return writeGeometryFeatureOwned(layer, std::move(geom), [sourceValue](OGRFeature* feature) {
        feature->SetField("source", sourceValue);
    });
}

static bool writeGeometryFeature(
    OGRLayer* layer,
    OGRGeometry* geom,
    const std::function<void(OGRFeature*)>& fillAttributes) {
    if (!layer || !geom || geom->IsEmpty()) {
        return true;
    }

    OGRFeature* dstFeat = OGRFeature::CreateFeature(layer->GetLayerDefn());
    if (fillAttributes) {
        fillAttributes(dstFeat);
    }
    std::unique_ptr<OGRGeometry> promoted = promoteGeometryToLayerType(layer, geom);
    dstFeat->SetGeometry(promoted ? promoted.get() : geom);
    dstFeat->SetFID(OGRNullFID);
    const bool ok = layer->CreateFeature(dstFeat) == OGRERR_NONE;
    OGRFeature::DestroyFeature(dstFeat);
    return ok;
}

static bool writeGeometryFeatureOwned(
    OGRLayer* layer,
    std::unique_ptr<OGRGeometry> geom,
    const std::function<void(OGRFeature*)>& fillAttributes) {
    if (!layer || !geom || geom->IsEmpty()) {
        return true;
    }

    OGRFeature* dstFeat = OGRFeature::CreateFeature(layer->GetLayerDefn());
    if (fillAttributes) {
        fillAttributes(dstFeat);
    }
    std::unique_ptr<OGRGeometry> promoted = promoteGeometryToLayerType(layer, geom.get());
    if (promoted) {
        geom.reset();
        dstFeat->SetGeometryDirectly(promoted.release());
    } else {
        dstFeat->SetGeometryDirectly(geom.release());
    }
    dstFeat->SetFID(OGRNullFID);
    const bool ok = layer->CreateFeature(dstFeat) == OGRERR_NONE;
    OGRFeature::DestroyFeature(dstFeat);
    return ok;
}

static void copyFeatureFieldsOnly(
    OGRFeature* dstFeature,
    const OGRFeature* srcFeature,
    const std::vector<int>& fieldMap) {
    if (!dstFeature || !srcFeature) {
        return;
    }

    if (!fieldMap.empty()) {
        dstFeature->SetFieldsFrom(srcFeature, fieldMap.data(), TRUE);
    }
    dstFeature->SetFID(OGRNullFID);
}

static bool writeFeatureWithCopiedFields(
    OGRLayer* layer,
    const OGRFeature* sourceFeature,
    OGRGeometry* geom,
    const std::vector<int>* fieldMap = nullptr) {
    return writeGeometryFeature(layer, geom, [sourceFeature, fieldMap](OGRFeature* feature) {
        if (sourceFeature) {
            if (!fieldMap) {
                feature->SetFrom(sourceFeature, TRUE);
                feature->SetFID(OGRNullFID);
            } else {
                copyFeatureFieldsOnly(feature, sourceFeature, *fieldMap);
            }
        }
    });
}

static bool writeFeatureWithCopiedFieldsOwned(
    OGRLayer* layer,
    const OGRFeature* sourceFeature,
    std::unique_ptr<OGRGeometry> geom,
    const std::vector<int>* fieldMap = nullptr) {
    return writeGeometryFeatureOwned(layer, std::move(geom), [sourceFeature, fieldMap](OGRFeature* feature) {
        if (sourceFeature) {
            if (!fieldMap) {
                feature->SetFrom(sourceFeature, TRUE);
                feature->SetFID(OGRNullFID);
            } else {
                copyFeatureFieldsOnly(feature, sourceFeature, *fieldMap);
            }
        }
    });
}

static bool isLinePolygonPracticalUnionCase(OGRLayer* srcLayer, OGRLayer* overlayLayer) {
    if (!srcLayer || !overlayLayer) {
        return false;
    }

    return isLinearGeometryType(srcLayer->GetGeomType())
        && geometryDimensionRank(overlayLayer->GetGeomType()) == 2;
}

static int detectOverlayGeometryDimensionRank(const std::vector<OverlayGeometryEntry>& entries) {
    int maxRank = -1;
    for (const auto& entry : entries) {
        if (!entry.geometry) {
            continue;
        }
        maxRank = (std::max)(maxRank, geometryDimensionRank(entry.geometry->getGeometryType()));
    }
    return maxRank;
}

static bool shouldRejectHighlyOverlappedMixedUnion(const std::vector<OverlayGeometryEntry>& entries) {
    if (entries.size() < 3) {
        return false;
    }

    return computeEnvelopeOverlapRatio(entries) >= 0.25;
}

static OGRGeometry* unionGeometryRange(std::vector<OGRGeometry*>& geoms, size_t begin, size_t end) {
    if (begin >= end) {
        return nullptr;
    }
    if (end - begin == 1) {
        return geoms[begin];
    }

    const size_t mid = begin + (end - begin) / 2;
    OGRGeometry* left = unionGeometryRange(geoms, begin, mid);
    OGRGeometry* right = unionGeometryRange(geoms, mid, end);
    if (!left) {
        return right;
    }
    if (!right) {
        return left;
    }

    OGRGeometry* merged = left->Union(right);
    delete left;
    delete right;
    return merged;
}

static OGRGeometry* unionGeometryList(std::vector<OGRGeometry*>& geoms) {
    if (geoms.empty()) {
        return nullptr;
    }
    return unionGeometryRange(geoms, 0, geoms.size());
}

struct SegmentedLinePart {
    OGRGeometry* geometry = nullptr;
    bool overlapsOverlay = false;
};

static std::vector<SegmentedLinePart> splitLineByOverlayEntries(
    const OGRGeometry* geom,
    const std::vector<const OverlayGeometryEntry*>& candidates) {
    std::vector<SegmentedLinePart> segments;
    if (!geom || geom->IsEmpty()) {
        return segments;
    }

    segments.push_back({geom->clone(), false});
    for (const auto* entry : candidates) {
        std::vector<SegmentedLinePart> nextSegments;
        for (auto& segment : segments) {
            if (!segment.geometry || segment.geometry->IsEmpty()) {
                delete segment.geometry;
                continue;
            }

            if (segment.overlapsOverlay || !segment.geometry->Intersects(entry->geometry.get())) {
                nextSegments.push_back(segment);
                continue;
            }

            std::vector<OGRGeometry*> outsideParts;
            collectLinearGeometryParts(segment.geometry->Difference(entry->geometry.get()), outsideParts);
            for (auto* part : outsideParts) {
                nextSegments.push_back({part, false});
            }

            std::vector<OGRGeometry*> insideParts;
            collectLinearGeometryParts(segment.geometry->Intersection(entry->geometry.get()), insideParts);
            for (auto* part : insideParts) {
                nextSegments.push_back({part, true});
            }

            delete segment.geometry;
        }
        segments = std::move(nextSegments);
    }

    return segments;
}

static OGRGeometry* buildMergedSegmentGeometry(
    const std::vector<SegmentedLinePart>& segments,
    bool overlapsOverlay) {
    std::vector<OGRGeometry*> matchedParts;
    for (const auto& segment : segments) {
        if (!segment.geometry || segment.geometry->IsEmpty() || segment.overlapsOverlay != overlapsOverlay) {
            continue;
        }
        matchedParts.push_back(segment.geometry->clone());
    }

    if (matchedParts.empty()) {
        return nullptr;
    }

    if (matchedParts.size() == 1) {
        return matchedParts[0];
    }

    auto* merged = new OGRMultiLineString();
    for (auto* part : matchedParts) {
        merged->addGeometry(part);
        delete part;
    }
    return merged;
}
