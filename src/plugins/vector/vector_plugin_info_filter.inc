// N127：`--where` 的字节白名单必须是 UTF-8 感知的。
// 旧实现逐字节 `std::isalnum(static_cast<unsigned char>(c))`，默认 C locale 下 `>=0x80` 恒为 0
// ⇒ 任何 UTF-8 多字节序列都落 `default: return false`，中文属性值（本仓样例数据全是中文）根本没法过滤。
// 现在 `>=0x80` 的字节只有在构成**合法且规范的 UTF-8 序列**时才放行：
// 非法字节、悬挂 lead、overlong 编码、超出 U+10FFFF、UTF-16 代理区一律照旧拒。
// ⚠ 不许简化成 `if (c >= 0x80) continue` —— 那等于把任意二进制垃圾放进料给 GDAL 的字符串里。
// lead 字节 → 它要求的续接字节数；返回 -1 表示这个字节不能作 lead：
//   0x80..0xBF（落单的续接字节）、0xC0/0xC1（只会产生 overlong）、0xF5..0xFF（非 UTF-8 / >U+10FFFF）。
static int utf8LeadContinuations(unsigned char lead) {
    if (lead >= 0xC2 && lead <= 0xDF) return 1;
    if (lead >= 0xE0 && lead <= 0xEF) return 2;
    if (lead >= 0xF0 && lead <= 0xF4) return 3;
    return -1;
}

// text[index] 开头若是一个完整且规范的 UTF-8 序列，就把 index 推到该序列的**最后一个字节**上
// （调用点的 ++i 正好前进到下一字符），返回 true；否则返回 false 且不改动 index。
// 第二个字节的取值区间同时承担三件事（Unicode 表 3-7 / RFC 3629 的强制形态）：
//   0xE0 后必须 A0..BF ⇒ 排除 overlong（U+0000..U+07FF 的三字节写法）
//   0xED 后必须 80..9F ⇒ 排除 UTF-16 代理区 U+D800..U+DFFF
//   0xF0 后必须 90..BF ⇒ 排除 overlong 的四字节写法
//   0xF4 后必须 80..8F ⇒ 排除 >U+10FFFF
static bool consumeUtf8Sequence(const std::string& text, size_t& index) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    const int need = utf8LeadContinuations(lead);
    if (need < 0) return false;
    // 悬挂 lead：序列在本串结尾处被截断
    if (index + static_cast<size_t>(need) >= text.size()) return false;

    unsigned char secondLow = 0x80;
    unsigned char secondHigh = 0xBF;
    if (lead == 0xE0) secondLow = 0xA0;
    else if (lead == 0xED) secondHigh = 0x9F;
    else if (lead == 0xF0) secondLow = 0x90;
    else if (lead == 0xF4) secondHigh = 0x8F;

    const unsigned char second = static_cast<unsigned char>(text[index + 1]);
    if (second < secondLow || second > secondHigh) return false;
    for (int offset = 2; offset <= need; ++offset) {
        const unsigned char byte = static_cast<unsigned char>(text[index + static_cast<size_t>(offset)]);
        if (byte < 0x80 || byte > 0xBF) return false;  // 续接字节必须是 10xxxxxx
    }
    index += static_cast<size_t>(need);
    return true;
}

static bool isSafeWhereClause(const std::string& where) {
    // 白名单字符校验：仅允许安全字符
    for (size_t i = 0; i < where.size(); ++i) {
        const unsigned char byte = static_cast<unsigned char>(where[i]);
        if (std::isalnum(byte)) continue;
        if (byte >= 0x80) {
            // N127：非 ASCII 只按合法 UTF-8 序列放行，其余（含 0xFF/0xFE、overlong、>U+10FFFF）照拒
            if (!consumeUtf8Sequence(where, i)) return false;
            continue;
        }
        switch (where[i]) {
            case ' ': case '(': case ')': case ',': case '.':
            case '=': case '<': case '>': case '!':
            case '\'': case '_': case '-': case '+': case '*':
            case '%': case '?':
                continue;
            default:
                return false;
        }
    }
    // 检查危险关键字（在安全字符范围内进一步过滤）
    std::string upper = where;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    // 必须以单词边界匹配危险关键字
    static const std::vector<std::string> dangerous = {
        "DROP", "DELETE", "INSERT", "UPDATE", "ALTER", "CREATE",
        "EXEC", "EXECUTE", "SELECT", "UNION", "GRANT", "REVOKE"
    };
    for (const auto& kw : dangerous) {
        size_t pos = 0;
        while ((pos = upper.find(kw, pos)) != std::string::npos) {
            // 检查是否为单词边界（前后不是字母或数字）
            bool leftBoundary = (pos == 0 || !std::isalnum(static_cast<unsigned char>(upper[pos - 1])));
            bool rightBoundary = (pos + kw.size() >= upper.size() || !std::isalnum(static_cast<unsigned char>(upper[pos + kw.size()])));
            if (leftBoundary && rightBoundary) {
                return false;
            }
            pos++;
        }
    }
    // 检查分号（语句分隔符）
    if (where.find(';') != std::string::npos) return false;
    // 检查注释符号
    if (where.find("--") != std::string::npos) return false;
    if (where.find("/*") != std::string::npos) return false;
    return true;
}

gis::framework::Result VectorPlugin::doInfo(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string layerName = gis::framework::getParam<std::string>(params, "layer", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto ds = openVector(input, true);
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

    progress.throwIfCancelled();
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

    progress.throwIfCancelled();

    progress.onProgress(0.1);

    auto srcDS = openVector(input, true);
    if (!srcDS) return gis::framework::Result::fail("Cannot open vector file: " + input);

    OGRLayer* srcLayer = getLayer(srcDS.get(), layerName);
    if (!srcLayer) {
        return gis::framework::Result::fail("Cannot find layer: " + layerName);
    }

    if (!where.empty()) {
        if (!isSafeWhereClause(where)) {
            return gis::framework::Result::fail("WHERE clause contains disallowed characters or keywords");
        }
        srcLayer->SetAttributeFilter(where.c_str());
    }

    bool hasExtent = extentArr[0] != 0 || extentArr[1] != 0 ||
                     extentArr[2] != 0 || extentArr[3] != 0;
    if (hasExtent) {
        srcLayer->SetSpatialFilterRect(extentArr[0], extentArr[1], extentArr[2], extentArr[3]);
    }

    progress.throwIfCancelled();

    progress.onProgress(0.3);

    namespace fs = std::filesystem;
    std::string outFormat = "GeoJSON";
    std::string ext = fs::u8path(output).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".shp") outFormat = "ESRI Shapefile";
    else if (ext == ".gpkg") outFormat = "GPKG";
    else if (ext == ".kml") outFormat = "KML";

    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName(outFormat.c_str());
    if (!drv) {
        return gis::framework::Result::fail("Cannot get driver for format: " + outFormat);
    }

    // 失败契约的入口闸（等价判定"输出不得覆盖输入"）走在任何建目录/建文件之前：建目录归
    // staged.start 自己做，所以这里不再先动文件系统。旧产物的前置删除同样已去掉 —— 它的
    // 对应物是 commit 末尾的 removeStaleOldSidecars，删除只许发生在新件全部落地之后。
    VectorStagedOutput staged;
    std::string stageError;
    if (!staged.start(output, outFormat, { input }, drv, stageError)) {
        return gis::framework::Result::fail(stageError);
    }
    GDALDataset* dstDS = staged.dataset();

    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr;
    std::vector<std::string> layerOptionStorage = buildGpkgDriverIndexedLayerOptions();
    std::vector<char*> layerOptions = buildLayerOptionPointers(layerOptionStorage);
    OGRLayer* dstLayer = dstDS->CreateLayer(
        srcLayer->GetName(),
        srcSRS,
        resultLayerGeometryType(srcLayer->GetGeomType()),
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
    OGRFeature* feat;
    int count = 0;
    while ((feat = srcLayer->GetNextFeature()) != nullptr) {
        OGRFeature* dstFeat = OGRFeature::CreateFeature(dstLayer->GetLayerDefn());
        dstFeat->SetFrom(feat);
        if (dstLayer->CreateFeature(dstFeat) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
            delete srcSRS;
            return gis::framework::Result::fail("Failed to write feature");
        }
        OGRFeature::DestroyFeature(dstFeat); OGRFeature::DestroyFeature(feat);
        count++;
    }

    delete srcSRS;
    progress.throwIfCancelled();

    // 全部写完 ⇒ 关闭数据集 → 逐文件替换目标 → 清旧边车（顺序由 VectorStagedOutput::commit 钉）。
    // 替换失败就报失败：此时旧产物仍在场，本轮临时件由 staged 的析构删干净。
    std::string commitError;
    if (!staged.commit(commitError)) {
        return gis::framework::Result::fail(commitError);
    }

    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(
        "Filter completed: " + std::to_string(count) + " features written", output);
    // N143 家族补全（普查现读见 docs/RELEASE.md 的 C3h-2b 条目）：filter 与 difference/
    // intersect/polygonize/clip 是同一族"写出矢量产物但一个 metadata 键都不发"的动作，
    // 计数只活在上面那句消息里 ⇒ 真实腿只能抠散文。这里 count 数的是**实际写入输出图层的
    // 要素数**（doFilter 的写循环每 CreateFeature 成功一次自增一次），与消息里的数字同源。
    mergeStandardRunMetadata(result, params, "filter");
    result.metadata["feature_count"] = std::to_string(count);
    return result;
}
