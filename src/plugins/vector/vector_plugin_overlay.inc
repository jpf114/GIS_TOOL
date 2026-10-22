gis::framework::Result VectorPlugin::doUnion(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string overlayVector = gis::framework::getParam<std::string>(params, "overlay_vector", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (overlayVector.empty()) return gis::framework::Result::fail("overlay_vector is required for union");

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open input: " + input);

    auto overlayDS = openVector(overlayVector, true);
    if (!overlayDS) { return gis::framework::Result::fail("Cannot open overlay: " + overlayVector); }

    OGRLayer* srcLayer = getLayer(srcDS.get(), layerName);
    OGRLayer* overlayLayer = overlayDS->GetLayer(0);
    if (!srcLayer || !overlayLayer) { return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), overlayLayer->GetSpatialRef())) { return failCrsMismatch("Union");
    }

    const OGRwkbGeometryType srcEffectiveType = effectiveLayerGeometryType(srcLayer);
    auto overlayEntries = collectNormalizedGeometries(overlayLayer);
    const bool usePracticalMixedUnion =
        isLinearGeometryType(srcEffectiveType) && detectOverlayGeometryDimensionRank(overlayEntries) == 2;

    if (overlayEntries.empty()) { return failInvalidGeometry("Union");
    }

    if (usePracticalMixedUnion && shouldRejectHighlyOverlappedMixedUnion(overlayEntries)) { return gis::framework::Result::fail(
            "Union detected heavily overlapped polygon overlays for mixed-dimension input. "
            "Please dissolve or simplify the overlay polygons first, or use clip/difference instead.");
    }

    progress.throwIfCancelled();

    progress.onProgress(0.3);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::u8path(output).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { return gis::framework::Result::fail("Cannot get driver"); }

    // 窄串构造 fs::path 在 Windows 按 ANSI 解 ⇒ UTF-8 中文路径会被判成"不存在"，旧产物边车永远删不掉（N122）。
    const fs::path outputPath = fs::u8path(output);
    if (outFormat == "ESRI Shapefile" && fs::exists(outputPath)) {
        for (const auto& entry : fs::directory_iterator(outputPath.parent_path())) {
            if (entry.path().stem() == outputPath.stem()) fs::remove(entry.path());
        }
    }

    auto dstDS = gis::core::GdalDatasetPtr(drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr));
    if (!dstDS) { return gis::framework::Result::fail("Cannot create output"); }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    const auto outputGeomType = usePracticalMixedUnion
        ? resultLayerGeometryType(srcEffectiveType)
        : wkbUnknown;
    const bool useTransactions = outFormat == "GPKG";
    std::vector<std::string> layerOptionStorage = buildGpkgFastWriteLayerOptions(useTransactions);
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);
    OGRLayer* dstLayer = dstDS->CreateLayer(
        "union",
        srcSRS,
        outputGeomType,
        layerOptions.empty() ? nullptr : layerOptions.data());
    if (!dstLayer) {
        delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    // 建表分两条路：线 × 面（路径 B）本刀仍只建 `source` 一个字段，属性补齐在下一刀，这里不许顺手扩；
    // 路径 A 按"源字段原序 → 叠加字段（同名改名）→ source"建整表，并把两侧的下标表就地算好，
    // 后面写要素一律按下标，不在循环里按字段名查。
    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    auto* overlayLayerDefn = overlayLayer->GetLayerDefn();
    std::vector<int> srcFieldMap;
    std::vector<int> overlayFieldMap;
    std::vector<std::string> takenFieldNames;
    int sourceFieldIdx = -1;
    OGRFieldDefn sourceFieldDefn("source", OFTString);

    // GDAL 的 OGR_F_FoundUniqueName 不是公开 API（安装头文件里查不到），改名规则只能本仓实现，
    // 且这条规则已同步写进算法文档：与输出表已有字段名做大小写不敏感比较，
    // 冲突则依次试 <name>_2、<name>_3 … 直到唯一。
    auto uniqueFieldName = [](const std::vector<std::string>& taken, const std::string& base) {
        auto conflicts = [&taken](const std::string& candidate) {
            auto sameIgnoringAsciiCase = [](const std::string& lhs, const std::string& rhs) {
                if (lhs.size() != rhs.size()) {
                    return false;
                }
                for (size_t i = 0; i < lhs.size(); ++i) {
                    if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
                        std::tolower(static_cast<unsigned char>(rhs[i]))) {
                        return false;
                    }
                }
                return true;
            };
            for (const auto& existing : taken) {
                if (sameIgnoringAsciiCase(existing, candidate)) {
                    return true;
                }
            }
            return false;
        };

        if (!conflicts(base)) {
            return base;
        }
        for (int suffix = 2; suffix <= 9999; ++suffix) {
            const std::string candidate = base + "_" + std::to_string(suffix);
            if (!conflicts(candidate)) {
                return candidate;
            }
        }
        // 撞满 9998 个同名才走到这里：此时 CreateField 会失败，调用方按下标表把该字段整列跳过，
        // 宁可少一列也不让输出表出现两个同名列。
        return base;
    };

    const auto appendFieldDefn = [&](const OGRFieldDefn* srcDefn, std::string forcedName) -> int {
        OGRFieldDefn mergedFieldDefn(srcDefn);
        if (!forcedName.empty()) {
            mergedFieldDefn.SetName(forcedName.c_str());
        }
        if (dstLayer->CreateField(&mergedFieldDefn) != OGRERR_NONE) {
            return -1;
        }
        takenFieldNames.push_back(mergedFieldDefn.GetNameRef());
        return dstLayer->GetLayerDefn()->GetFieldCount() - 1;
    };

    if (usePracticalMixedUnion) {
        sourceFieldIdx = appendFieldDefn(&sourceFieldDefn, "");
    } else {
        srcFieldMap.assign(srcLayerDefn->GetFieldCount(), -1);
        for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
            srcFieldMap[i] = appendFieldDefn(srcLayerDefn->GetFieldDefn(i), "");
        }

        overlayFieldMap.assign(overlayLayerDefn->GetFieldCount(), -1);
        for (int i = 0; i < overlayLayerDefn->GetFieldCount(); ++i) {
            const std::string mergedName =
                uniqueFieldName(takenFieldNames, overlayLayerDefn->GetFieldDefn(i)->GetNameRef());
            overlayFieldMap[i] = appendFieldDefn(overlayLayerDefn->GetFieldDefn(i), mergedName);
        }

        sourceFieldIdx = appendFieldDefn(
            &sourceFieldDefn, uniqueFieldName(takenFieldNames, sourceFieldDefn.GetNameRef()));
    }

    if (sourceFieldIdx < 0) {
        delete srcSRS;
        return gis::framework::Result::fail("Cannot create union source field");
    }

    progress.throwIfCancelled();

    progress.onProgress(0.4);

    int count = 0;
    // 三类分块各写了多少条：只有真的写出去一条才计数，失败返回不计数。
    // 不变量：union_total_pieces == input + overlap + overlay == count。
    int inputCount = 0;
    int overlapCount = 0;
    int overlayCount = 0;
    // 叠加独有块这一轮因为几何运算失败而**没写出去**的块数（缺口，宁缺不重）。
    // 只有路径 A 的叠加独有块轮会增长，线 × 面路径恒为 0 ⇒ 正常批次看不到这个键。
    int skippedOverlayPieces = 0;
    const int transactionBatchSize = useTransactions ? 20000 : 1000;
    if (useTransactions) {
        dstLayer->StartTransaction();
    }

    if (usePracticalMixedUnion) {
        // 路径 B（线 × 面）：写出的内容与今天逐字一致，只在计数处多记一笔 overlapCount。
        srcLayer->ResetReading();
        OGRFeature* feat = nullptr;
        while ((feat = srcLayer->GetNextFeature()) != nullptr) {
            OGRGeometry* geom = feat->GetGeometryRef();
            if (geom) {
                const auto candidates = collectCandidateOverlayEntries(overlayEntries, geom);
                auto lineParts = splitLineByOverlayEntries(geom, candidates);

                for (size_t i = 0; i < lineParts.size(); ++i) {
                    OGRGeometry* part = lineParts[i].geometry;
                    if (!writeUnionFeature(dstLayer, part, "split")) {
                        for (size_t j = i; j < lineParts.size(); ++j) {
                            delete lineParts[j].geometry;
                        }
                        OGRFeature::DestroyFeature(feat);
                        if (useTransactions) {
                            dstLayer->RollbackTransaction();
                        }
                        delete srcSRS;
                        return gis::framework::Result::fail("Failed to write union feature");
                    }
                    delete part;
                    lineParts[i].geometry = nullptr;
                    count++;
                    // 归类说明：这条路径写的不是"相交块"，但它的 source 值既不是 input 也不是 overlay，
                    // 按"实际写入的 source 值归类"只能落进 overlap 这一桶；下一刀补属性时别把它读成真相交块。
                    overlapCount++;
                    if (useTransactions && count % transactionBatchSize == 0) {
                        dstLayer->CommitTransaction();
                        dstLayer->StartTransaction();
                    }
                }
            }
            OGRFeature::DestroyFeature(feat);
        }
    } else {
        // 路径 A：真并集。三段互不重叠的分块（源独有 / 相交 / 叠加独有）拼出"源 ∪ 叠加"的全部范围。
        // 叠加侧改用带属性的要素表；纯几何表到这里只剩维度判定和高度重叠闸用过它，
        // 显式释放以免两张表同时驻留内存。
        std::vector<OverlayFeatureEntry> overlayFeatures = collectNormalizedFeatures(overlayLayer);
        overlayEntries.clear();

        // 源图层游标本刀要被消费两次（先源遍历，再叠加独有块遍历时做 Difference），先一次性缓存源要素，
        // 第二个循环不许回读源图层。缓存刻意保持"原样几何"不做标准化：本刀只补"叠加侧丢几何 + 属性表"，
        // 源侧口径一旦改成标准化，修复失败的源要素会被整条跳过，凭空少一块面积，那是另一回事。
        std::vector<std::unique_ptr<OGRFeature>> srcFeatures;
        std::vector<OGREnvelope> srcEnvelopes;
        srcLayer->ResetReading();
        for (OGRFeature* rawFeat = srcLayer->GetNextFeature();
             rawFeat != nullptr; rawFeat = srcLayer->GetNextFeature()) {
            std::unique_ptr<OGRFeature> feat(rawFeat);
            OGREnvelope envelope{};
            OGRGeometry* featGeom = feat->GetGeometryRef();
            if (featGeom) {
                featGeom->getEnvelope(&envelope);
            }
            srcEnvelopes.push_back(envelope);
            srcFeatures.push_back(std::move(feat));
        }

        // 包串粗筛沿用 envelopesIntersect（与 collectCandidateOverlayEntries 同一把尺），
        // 不许为每条源要素重新读叠加图层。
        auto featureCandidates = [&overlayFeatures](const OGREnvelope& target) {
            std::vector<const OverlayFeatureEntry*> candidates;
            for (const auto& entry : overlayFeatures) {
                if (envelopesIntersect(entry.envelope, target)) {
                    candidates.push_back(&entry);
                }
            }
            return candidates;
        };

        // 把一组几何并成一块，所有权移交调用方。空集合返回 nullptr，
        // 调用方必须自己走"对面没有几何"的分支，不许拿 nullptr 去调 Difference（doDifference 里就是现例）。
        auto mergeGeometryList = [](const std::vector<const OGRGeometry*>& geoms) -> OGRGeometry* {
            std::vector<OGRGeometry*> clones;
            clones.reserve(geoms.size());
            for (const OGRGeometry* geom : geoms) {
                clones.push_back(geom->clone());
            }
            return unionGeometryList(clones);
        };

        // 叠加独有块要用源几何表：级联并集与"并集失败时逐条差集"两条路都从同一份候选出发，
        // 口径必须一致，否则回退分支减的几何和主路径不是同一批。
        auto featureSourceCandidates = [&srcFeatures, &srcEnvelopes](const OGREnvelope& target) {
            std::vector<const OGRGeometry*> candidates;
            for (size_t i = 0; i < srcFeatures.size(); ++i) {
                const OGRGeometry* geom = srcFeatures[i]->GetGeometryRef();
                if (!geom || geom->IsEmpty() || !envelopesIntersect(srcEnvelopes[i], target)) {
                    continue;
                }
                candidates.push_back(geom);
            }
            return candidates;
        };

        // 属性移交只按下标写，两侧都走 copyFeatureFieldsOnly（SetFieldsFrom，不碰几何）。
        // WHY 不许先 SetFrom(源) 再 SetFrom(叠加)：OGRFeature::SetFrom 按字段名匹配，
        // 两边同名时第二次会把第一次的值静默覆盖掉（name 会变成叠加侧的值）。
        // 下标表在建表时就算好，NULL 字段由 SetFieldsFrom 保持未赋值（GPKG 落盘即 NULL）。
        auto writeUnionPiece = [&](OGRGeometry* geom, const OGRFeature* srcFeat,
                                   const OGRFeature* overlayFeat, const char* sourceValue) {
            return writeGeometryFeature(dstLayer, geom, [&](OGRFeature* dstFeat) {
                if (srcFeat) {
                    copyFeatureFieldsOnly(dstFeat, srcFeat, srcFieldMap);
                }
                if (overlayFeat) {
                    copyFeatureFieldsOnly(dstFeat, overlayFeat, overlayFieldMap);
                }
                dstFeat->SetField(sourceFieldIdx, sourceValue);
            });
        };

        for (const auto& feat : srcFeatures) {
            OGRGeometry* geom = feat->GetGeometryRef();
            if (!geom || geom->IsEmpty()) {
                continue;   // 几何为 NULL 的源要素今天就不写，保持不写
            }

            OGREnvelope envelope{};
            geom->getEnvelope(&envelope);
            const auto candidates = featureCandidates(envelope);

            // consumed = 这条源要素已经写出去的相交块并集。叠加面互相重叠时，每条叠加要素的相交块
            // 必须先减掉它再写 —— 不减就把同一块源面积写两次，"分块面积之和 == 并集面积"当场破。
            std::unique_ptr<OGRGeometry> consumed;
            for (const auto* candidate : candidates) {
                const OGRGeometry* overlayGeom = candidate->feature->GetGeometryRef();
                if (!overlayGeom || overlayGeom->IsEmpty()) {
                    continue;
                }

                std::unique_ptr<OGRGeometry> intersection(geom->Intersection(overlayGeom));
                if (!intersection || intersection->IsEmpty()) {
                    continue;
                }

                std::unique_ptr<OGRGeometry> piece(consumed
                    ? intersection->Difference(consumed.get())
                    : intersection->clone());
                if (piece && !piece->IsEmpty()) {
                    if (!writeUnionPiece(piece.get(), feat.get(), candidate->feature.get(), "both")) {
                        if (useTransactions) {
                            dstLayer->RollbackTransaction();
                        }
                        delete srcSRS;
                        return gis::framework::Result::fail("Failed to write union feature");
                    }
                    count++;
                    overlapCount++;
                    if (useTransactions && count % transactionBatchSize == 0) {
                        dstLayer->CommitTransaction();
                        dstLayer->StartTransaction();
                    }
                }

                // consumed 单调增长；Union 失败时保留旧值（宁少并不也不多写一块）。
                // 注意这条失败现在同时波及源独有块的减法基（它就取 consumed）：少并一块 ⇒ 那块会把已写出的
                // both 面积再算进"源独有"里。旧写法减的是候选并集，看似不受这里影响，但它自己的级联并集
                // 一旦失败就退回"整块克隆写出"，重叠得更彻底。两条都只在 GEOS Union 失败时出现，
                // 今天没有用例能触发（要靠注入 GEOS 失败），故此处保持"少并"这个既有方向。
                OGRGeometry* merged = consumed
                    ? consumed->Union(intersection.get())
                    : intersection->clone();
                if (merged) {
                    consumed.reset(merged);   // unique_ptr 负责释放上一轮的 consumed
                }
            }

            // 源独有块 = geom − consumed。减法基取"本轮已写出的相交块并集"，不取"全部候选叠加几何的并"：
            // 两者可证等价 —— consumed 恒等于 union(candidates ∩ geom) = geom ∩ union(candidates)，
            // 所以 geom − consumed == geom − union(candidates)；等价之外还更强：候选并集那一次
            // GEOS 级联 Union 失败时 consumed 照样在手里，不必退回"整块克隆写出"去和已写出的 both 块重叠
            //（"分块互不重叠、面积可加"是本动作对输出的契约，回退路径也不许破它）。
            // 减不动（Difference 返回 nullptr）时宁可留缺口，也不写重叠块。
            std::unique_ptr<OGRGeometry> remainder(
                consumed ? geom->Difference(consumed.get()) : geom->clone());
            if (remainder && !remainder->IsEmpty()) {
                if (!writeUnionPiece(remainder.get(), feat.get(), nullptr, "input")) {
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete srcSRS;
                    return gis::framework::Result::fail("Failed to write union feature");
                }
                count++;
                inputCount++;
                if (useTransactions && count % transactionBatchSize == 0) {
                    dstLayer->CommitTransaction();
                    dstLayer->StartTransaction();
                }
            }
        }

        progress.throwIfCancelled();

        progress.onProgress(0.7);

        // 叠加独有块：N128 丢掉的就是这一段 —— 叠加图层里不与任何源要素相交的几何，从今天起要写出来。
        // consumedOverlay 与源侧的 consumed 同理：叠加面互叠时，后一条只写没被写过的那部分。
        std::unique_ptr<OGRGeometry> consumedOverlay;
        for (const auto& entry : overlayFeatures) {
            OGRGeometry* overlayGeom = entry.feature->GetGeometryRef();
            if (!overlayGeom || overlayGeom->IsEmpty()) {
                continue;
            }

            const auto sourceCandidates = featureSourceCandidates(entry.envelope);
            std::unique_ptr<OGRGeometry> srcUnion(mergeGeometryList(sourceCandidates));
            std::unique_ptr<OGRGeometry> raw;
            if (srcUnion) {
                raw.reset(overlayGeom->Difference(srcUnion.get()));
            } else if (sourceCandidates.empty()) {
                // 包串粗筛一条源几何都没命中 ⇒ 对面确实没有几何可减，整块写出才是并集。
                raw.reset(overlayGeom->clone());
            } else {
                // 候选非空但**级联并集**失败。级联并集失败不等于逐条差集失败 ⇒ 从整块出发逐条 Difference，
                // 单条失败只跳过那一条候选、继续减剩下的；不许直接 clone() 整块写出（那会把
                // 已经写过的源面积再算成叠加独有块，"互不重叠、面积可加"这条不变量当场破）。
                raw.reset(overlayGeom->clone());
                bool trimmed = false;
                for (const OGRGeometry* candidate : sourceCandidates) {
                    OGRGeometry* step = raw->Difference(candidate);
                    if (!step) {
                        continue;
                    }
                    raw.reset(step);
                    trimmed = true;
                }
                if (!trimmed) {
                    // 一条都减不动 ⇒ 这块和源侧面积的关系无法确定，宁缺不重：跳过并记缺口。
                    skippedOverlayPieces++;
                    continue;
                }
            }
            if (!raw) {
                // 级联并集成功、差集自己失败：同样是缺口（不是"这块整个落在源里"），记账后跳过。
                skippedOverlayPieces++;
                continue;
            }
            if (raw->IsEmpty()) {
                continue;   // 没有叠加独有面积（全被源盖住），不算缺口
            }

            std::unique_ptr<OGRGeometry> piece(consumedOverlay
                ? raw->Difference(consumedOverlay.get())
                : raw->clone());
            if (piece && !piece->IsEmpty()) {
                if (!writeUnionPiece(piece.get(), nullptr, entry.feature.get(), "overlay")) {
                    if (useTransactions) {
                        dstLayer->RollbackTransaction();
                    }
                    delete srcSRS;
                    return gis::framework::Result::fail("Failed to write union feature");
                }
                count++;
                overlayCount++;
                if (useTransactions && count % transactionBatchSize == 0) {
                    dstLayer->CommitTransaction();
                    dstLayer->StartTransaction();
                }
            }

            OGRGeometry* merged = consumedOverlay
                ? consumedOverlay->Union(raw.get())
                : raw->clone();
            if (merged) {
                consumedOverlay.reset(merged);
            }
        }
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
        addSpatialIndexIfNeeded(dstDS.get(), dstLayer, useTransactions, "union");
    }
    delete srcSRS;
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    // 成功消息串逐字不许改：真实数据回归腿按正则 "Union completed: (\d+) features" 取数。
    auto result = gis::framework::Result::ok(
        "Union completed: " + std::to_string(count) + " features", output);
    // 分块记账（本动作今天不调 mergeStandardRunMetadata，overlay 三动作口径一致，不在本刀扩大范围）。
    result.metadata["union_input_pieces"] = std::to_string(inputCount);
    result.metadata["union_overlap_pieces"] = std::to_string(overlapCount);
    result.metadata["union_overlay_pieces"] = std::to_string(overlayCount);
    result.metadata["union_total_pieces"] = std::to_string(count);
    // 缺口计数只在 >0 时才出现：正常批次不该有这个键，出现即说明"分块面积之和 < 并集面积"，
    // 这一条不变量靠它兜住，不许为了凑数把减不动的块整块写出去。
    if (skippedOverlayPieces > 0) {
        result.metadata["union_skipped_overlay_pieces"] = std::to_string(skippedOverlayPieces);
    }
    return result;
}

gis::framework::Result VectorPlugin::doDifference(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string overlayVector = gis::framework::getParam<std::string>(params, "overlay_vector", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (overlayVector.empty()) return gis::framework::Result::fail("overlay_vector is required for difference");

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open input: " + input);

    auto overlayDS = openVector(overlayVector, true);
    if (!overlayDS) { return gis::framework::Result::fail("Cannot open overlay: " + overlayVector); }

    OGRLayer* srcLayer = getLayer(srcDS.get(), layerName);
    OGRLayer* overlayLayer = overlayDS->GetLayer(0);
    if (!srcLayer || !overlayLayer) { return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), overlayLayer->GetSpatialRef())) { return failCrsMismatch("Difference");
    }

    const OGRwkbGeometryType srcEffectiveType = effectiveLayerGeometryType(srcLayer);
    auto overlayEntries = collectNormalizedGeometries(overlayLayer);
    const bool useLinearPolygonSegmentation =
        isLinearGeometryType(srcEffectiveType) && detectOverlayGeometryDimensionRank(overlayEntries) == 2;

    if (overlayEntries.empty()) { return failInvalidGeometry("Difference");
    }

    std::unique_ptr<OGRGeometry> mergedOverlayGeometry;
    if (overlayEntries.size() <= 128) {
        mergedOverlayGeometry.reset(buildMergedOverlayGeometry(overlayEntries));
    }

    progress.throwIfCancelled();

    progress.onProgress(0.3);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::u8path(output).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { return gis::framework::Result::fail("Cannot get driver"); }

    // 窄串构造 fs::path 在 Windows 按 ANSI 解 ⇒ UTF-8 中文路径会被判成"不存在"，旧产物边车永远删不掉（N122）。
    const fs::path outputPath = fs::u8path(output);
    if (outFormat == "ESRI Shapefile" && fs::exists(outputPath)) {
        for (const auto& entry : fs::directory_iterator(outputPath.parent_path())) {
            if (entry.path().stem() == outputPath.stem()) fs::remove(entry.path());
        }
    }

    auto dstDS = gis::core::GdalDatasetPtr(drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr));
    if (!dstDS) { return gis::framework::Result::fail("Cannot create output"); }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    const bool useTransactions = outFormat == "GPKG";
    std::vector<std::string> layerOptionStorage = buildGpkgFastWriteLayerOptions(useTransactions);
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);
    OGRLayer* dstLayer = dstDS->CreateLayer(
        "difference",
        srcSRS,
        resultLayerGeometryType(srcEffectiveType),
        layerOptions.empty() ? nullptr : layerOptions.data());
    if (!dstLayer) {
        delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    progress.throwIfCancelled();

    progress.onProgress(0.4);

    srcLayer->ResetReading();
    const std::vector<int> fieldMap = dstLayer->GetLayerDefn()->ComputeMapForSetFrom(srcLayerDefn, true);
    OGRFeature* feat = nullptr;
    int count = 0;
    const int transactionBatchSize = useTransactions ? 20000 : 1000;
    if (useTransactions) {
        dstLayer->StartTransaction();
    }
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (geom) {
            if (useLinearPolygonSegmentation) {
                const auto candidates = collectCandidateOverlayEntries(overlayEntries, geom);
                if (candidates.empty()) {
                    if (writeFeatureWithCopiedFields(dstLayer, feat, geom->clone(), &fieldMap)) {
                        count++;
                        if (useTransactions && count % transactionBatchSize == 0) {
                            dstLayer->CommitTransaction();
                            dstLayer->StartTransaction();
                        }
                    }
                    OGRFeature::DestroyFeature(feat);
                    continue;
                }

                auto segments = splitLineByOverlayEntries(geom, candidates);
                std::unique_ptr<OGRGeometry> diffGeom(buildMergedSegmentGeometry(segments, false));
                for (auto& segment : segments) {
                    delete segment.geometry;
                }

                if (diffGeom && !diffGeom->IsEmpty()) {
                    if (writeFeatureWithCopiedFields(dstLayer, feat, diffGeom.get(), &fieldMap)) {
                        count++;
                        if (useTransactions && count % transactionBatchSize == 0) {
                            dstLayer->CommitTransaction();
                            dstLayer->StartTransaction();
                        }
                    }
                }
                OGRFeature::DestroyFeature(feat);
                continue;
            }

            OGRGeometry* overlayGeometry = mergedOverlayGeometry.get();
            std::unique_ptr<OGRGeometry> candidateOverlay;
            if (!overlayGeometry) {
                candidateOverlay.reset(buildCandidateOverlayGeometry(overlayEntries, geom));
                overlayGeometry = candidateOverlay.get();
            }
            // writeFeatureWithCopiedFieldsOwned 一律接管传入几何的所有权：用 unique_ptr 一次性移交，
            // 写入失败后再 delete 同一裸指针会二次释放并破坏进程堆。
            std::unique_ptr<OGRGeometry> diffGeom;
            if (!overlayGeometry || !geom->Intersects(overlayGeometry)) {
                diffGeom.reset(geom->clone());
            } else if (!overlayGeometry->Contains(geom)) {
                diffGeom.reset(geom->Difference(overlayGeometry));
            }
            if (diffGeom && !diffGeom->IsEmpty() &&
                writeFeatureWithCopiedFieldsOwned(dstLayer, feat, std::move(diffGeom), &fieldMap)) {
                count++;
                if (useTransactions && count % transactionBatchSize == 0) {
                    dstLayer->CommitTransaction();
                    dstLayer->StartTransaction();
                }
            }
        }
        OGRFeature::DestroyFeature(feat);
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
        addSpatialIndexIfNeeded(dstDS.get(), dstLayer, useTransactions, "difference");
    }
    delete srcSRS;
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Difference completed: " + std::to_string(count) + " features", output);
}

gis::framework::Result VectorPlugin::doIntersect(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");
    std::string overlayVector = gis::framework::getParam<std::string>(params, "overlay_vector", "");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (overlayVector.empty()) return gis::framework::Result::fail("overlay_vector is required for intersect");

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open input: " + input);

    auto overlayDS = openVector(overlayVector, true);
    if (!overlayDS) { return gis::framework::Result::fail("Cannot open overlay: " + overlayVector); }

    OGRLayer* srcLayer = getLayer(srcDS.get(), layerName);
    OGRLayer* overlayLayer = overlayDS->GetLayer(0);
    if (!srcLayer || !overlayLayer) { return gis::framework::Result::fail("Cannot find required layers");
    }

    if (!areSameCrs(srcLayer->GetSpatialRef(), overlayLayer->GetSpatialRef())) { return failCrsMismatch("Intersect");
    }

    const OGRwkbGeometryType srcEffectiveType = effectiveLayerGeometryType(srcLayer);
    auto overlayEntries = collectNormalizedGeometries(overlayLayer);
    const bool useLinearPolygonSegmentation =
        isLinearGeometryType(srcEffectiveType) && detectOverlayGeometryDimensionRank(overlayEntries) == 2;

    if (overlayEntries.empty()) { return failInvalidGeometry("Intersect");
    }

    std::unique_ptr<OGRGeometry> mergedOverlayGeometry;
    if (overlayEntries.size() <= 128) {
        mergedOverlayGeometry.reset(buildMergedOverlayGeometry(overlayEntries));
    }

    progress.throwIfCancelled();

    progress.onProgress(0.3);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::u8path(output).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) { return gis::framework::Result::fail("Cannot get driver"); }

    // 窄串构造 fs::path 在 Windows 按 ANSI 解 ⇒ UTF-8 中文路径会被判成"不存在"，旧产物边车永远删不掉（N122）。
    const fs::path outputPath = fs::u8path(output);
    if (outFormat == "ESRI Shapefile" && fs::exists(outputPath)) {
        for (const auto& entry : fs::directory_iterator(outputPath.parent_path())) {
            if (entry.path().stem() == outputPath.stem()) fs::remove(entry.path());
        }
    }

    auto dstDS = gis::core::GdalDatasetPtr(drv->Create(output.c_str(), 0, 0, 0, GDT_Unknown, nullptr));
    if (!dstDS) { return gis::framework::Result::fail("Cannot create output"); }

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    const bool useTransactions = outFormat == "GPKG";
    std::vector<std::string> layerOptionStorage = buildGpkgFastWriteLayerOptions(useTransactions);
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);
    OGRLayer* dstLayer = dstDS->CreateLayer(
        "intersect",
        srcSRS,
        resultLayerGeometryType(srcEffectiveType),
        layerOptions.empty() ? nullptr : layerOptions.data());
    if (!dstLayer) {
        delete srcSRS;
        return gis::framework::Result::fail("Cannot create output layer");
    }

    auto* srcLayerDefn = srcLayer->GetLayerDefn();
    for (int i = 0; i < srcLayerDefn->GetFieldCount(); ++i) {
        OGRFieldDefn fieldDefn(srcLayerDefn->GetFieldDefn(i));
        dstLayer->CreateField(&fieldDefn);
    }

    progress.throwIfCancelled();

    progress.onProgress(0.4);

    srcLayer->ResetReading();
    const std::vector<int> fieldMap = dstLayer->GetLayerDefn()->ComputeMapForSetFrom(srcLayerDefn, true);
    OGRFeature* feat = nullptr;
    int count = 0;
    const int transactionBatchSize = useTransactions ? 20000 : 1000;
    if (useTransactions) {
        dstLayer->StartTransaction();
    }
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRGeometry* geom = feat->GetGeometryRef();
        if (geom) {
            if (useLinearPolygonSegmentation) {
                const auto candidates = collectCandidateOverlayEntries(overlayEntries, geom);
                if (candidates.empty()) {
                    OGRFeature::DestroyFeature(feat);
                    continue;
                }

                auto segments = splitLineByOverlayEntries(geom, candidates);
                std::unique_ptr<OGRGeometry> intersectGeom(buildMergedSegmentGeometry(segments, true));
                for (auto& segment : segments) {
                    delete segment.geometry;
                }

                if (intersectGeom && !intersectGeom->IsEmpty()) {
                    if (writeFeatureWithCopiedFieldsOwned(dstLayer, feat, std::move(intersectGeom), &fieldMap)) {
                        count++;
                        if (useTransactions && count % transactionBatchSize == 0) {
                            dstLayer->CommitTransaction();
                            dstLayer->StartTransaction();
                        }
                    }
                }
                OGRFeature::DestroyFeature(feat);
                continue;
            }

            OGRGeometry* overlayGeometry = mergedOverlayGeometry.get();
            std::unique_ptr<OGRGeometry> candidateOverlay;
            if (!overlayGeometry) {
                candidateOverlay.reset(buildCandidateOverlayGeometry(overlayEntries, geom));
                overlayGeometry = candidateOverlay.get();
            }
            if (overlayGeometry && geom->Intersects(overlayGeometry)) {
                std::unique_ptr<OGRGeometry> intersectGeom(geom->Intersection(overlayGeometry));
                if (intersectGeom && !intersectGeom->IsEmpty()) {
                    if (writeFeatureWithCopiedFieldsOwned(dstLayer, feat, std::move(intersectGeom), &fieldMap)) {
                        count++;
                        if (useTransactions && count % transactionBatchSize == 0) {
                            dstLayer->CommitTransaction();
                            dstLayer->StartTransaction();
                        }
                    }
                }
            }
        }
        OGRFeature::DestroyFeature(feat);
    }

    if (useTransactions) {
        dstLayer->CommitTransaction();
        addSpatialIndexIfNeeded(dstDS.get(), dstLayer, useTransactions, "intersect");
    }
    delete srcSRS;
    progress.throwIfCancelled();
    progress.onProgress(1.0);

    return gis::framework::Result::ok(
        "Intersect completed: " + std::to_string(count) + " features", output);
}
