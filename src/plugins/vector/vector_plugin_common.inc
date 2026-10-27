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

// N153（批次 D，用户 2026-10-27 拍案 A）：未显式给 --format 时按输出扩展名推导格式。
// 返回空串表示"映射不出来"，由调用方决定回落（convert 回落 GeoJSON 并打一行提示）。
// 只认 getDriverForFormat 支持的五种格式；大小写不敏感（Windows 上 .GPKG 很常见）。
static std::string inferVectorFormatFromExtension(const std::string& output) {
    std::string ext = std::filesystem::u8path(output).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (ext == ".gpkg")    return "GPKG";
    if (ext == ".shp")     return "ESRI Shapefile";
    if (ext == ".geojson" || ext == ".json") return "GeoJSON";
    if (ext == ".kml")     return "KML";
    if (ext == ".csv")     return "CSV";
    return std::string();
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

static std::vector<OverlayGeometryEntry> collectNormalizedGeometries(
    OGRLayer* layer, size_t* skippedInputCount = nullptr) {
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
        } else if (skippedInputCount) {
            // N148：标准化后仍为空/非法的输入今天过去无痕丢掉；这里逐条计数，由调用方所在动作
            // 发 `*_skipped_input_count` 附加键（既有键取值一律不动）。
            ++(*skippedInputCount);
        }
        OGRFeature::DestroyFeature(feat);
    }

    return entries;
}

// 带属性的叠加要素表：union 路径 A 要把"相交块"写成"这一条叠加要素自己的字段"，
// 只有几何 + 包串的 OverlayGeometryEntry 撑不起这件事（属性表全丢是 N128 的第二半）。
struct OverlayFeatureEntry {
    std::unique_ptr<OGRFeature> feature;   // 几何已标准化
    OGREnvelope envelope{};
};

// 取舍规则必须逐字镜像 collectNormalizedGeometries：同样用 cloneNormalizedGeometry 标准化，
// 标准化结果为空或 IsEmpty() 的要素跳过，其余按图层读取顺序压入，包串从标准化后的几何取。
// WHY 要逐字：两张表跳过规则一旦不一致，"各分块面积之和 == 并集面积"这类不变量就会凭空多/少一块。
// 判据一致性由守卫用例钉住：PluginTest.VectorOverlayCollectorMirrorPredicateStaysVerbatim（源码扫描）。
// N148：跳过支同样逐字镜像地累加 skippedInputCount，动作侧据此发 `*_skipped_input_count` 附加键。
// ⚠ 覆盖边界（如实登记，不是漏删）：**要素趟这支计数今天是不可达的** —— `collectNormalizedFeatures` 的
// 两个调用点（`vector_plugin_overlay.inc` 路径 A / 路径 B 那两处）都只传图层、不传计数指针，
// 所以 `*_skipped_input_count` 的实际取值只来自几何趟 `collectNormalizedGeometries`。
// 保留这两行的唯一理由是"跳过支逐字镜像"（守卫把两侧含 `if (` 的决策行逐字比对，删掉要素趟这支 ⇒ 守卫必红）。
// 一旦将来有调用点开始传计数指针，union 侧必须防重复计数：几何趟已经取过一次数，两趟各加一遍会把
// 同一条被丢的输入算成 2 条（union 对同一叠加层就是扫两趟）。
static std::vector<OverlayFeatureEntry> collectNormalizedFeatures(
    OGRLayer* layer, size_t* skippedInputCount = nullptr) {
    std::vector<OverlayFeatureEntry> entries;
    if (!layer) {
        return entries;
    }

    layer->ResetReading();
    OGRFeature* feat = nullptr;
    while ((feat = layer->GetNextFeature()) != nullptr) {
        std::unique_ptr<OGRGeometry> geom(cloneNormalizedGeometry(feat->GetGeometryRef()));
        if (geom && !geom->IsEmpty()) {
            OverlayFeatureEntry entry;
            geom->getEnvelope(&entry.envelope);
            // 属性留、几何换成标准化后的那份：SetGeometryDirectly 接管所有权并丢弃原几何。
            feat->SetGeometryDirectly(geom.release());
            entry.feature.reset(feat);
            entries.push_back(std::move(entry));
            continue;
        } else if (skippedInputCount) {
            ++(*skippedInputCount);
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

static void ensureParentDirectoryForFile(const std::string& output) {
    const fs::path outputPath = fs::u8path(output);
    if (!outputPath.has_parent_path()) {
        return;
    }

    fs::create_directories(outputPath.parent_path());
}

// ── 批次 D / D0-C3：临时件**命名**与**替换**两条原语的单一来源 ────────────────────────
// 原来这两件事都长在 VectorStagedOutput 里（私有成员）。CSV 文本产物（7 个检查动作）要走同一条
// 失败契约，但不该自带第二份实现 ⇒ 按"提成文件作用域、分支与文案逐字不动"的方式共用：
//   ① 命名 <目标 stem>.gis-tmp-<进程号>-<进程内自增序号>：<目标目录> 由调用方补，扩展名各接各的。
//      WHY 必须同源：仓库里按 `.gis-tmp-` 这个名字扫残留的既有判据有两处
//      （tests/test_plugins.cpp 的 c3n1ExpectDirectoryUntouched 与 J3 的成功路径臂），
//      换名字 = 新残骸没人抓。
//   ② 进程内序号是**一把**静态计数器（两个类共用）⇒ 同一目录里即便目标 stem 相同
//      （out.csv 与 out.gpkg 的 stem 都是 "out"）也不会撞出同一个临时件名。
static std::string stagedOutputProcessTag() {
#ifdef _WIN32
    return std::to_string(static_cast<unsigned long>(GetCurrentProcessId()));
#else
    return std::to_string(static_cast<unsigned long>(::getpid()));
#endif
}

static unsigned long nextStagedOutputSequence() {
    static std::atomic<unsigned long> sequence{ 0 };
    return sequence.fetch_add(1) + 1;
}

static std::string stagedOutputPrefix(const fs::path& target) {
    // 进程号挡跨进程撞名，进程内自增序号挡同进程两个动作撞名（GUI 的任务队列与测试
    // 都会在一个进程里并发跑多个动作）。
    return target.filename().stem().u8string() + ".gis-tmp-" + stagedOutputProcessTag() + "-" +
        std::to_string(nextStagedOutputSequence());
}

// 同卷替换。**这里不走 std::filesystem::rename**：本机 MSVC STL（工具集 14.44.35207，
// crt/src/stl/filesystem.cpp:722）把它实现成
//   MoveFileExW(_Source, _Target, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)
// ⇒ "目标已存在"确实是覆盖成功不报错（这正是替换语义），但带着 COPY_ALLOWED 后
// **跨卷会悄悄退化成 copy+delete**，"同卷原子替换"就兑不了 ⇒ 自己发一次不带
// COPY_ALLOWED 的 MoveFileExW：跨卷直接失败，而不是半路变成复制。
// 非 Windows 分支跟着本仓既有的 #ifdef _WIN32 形态走，用 rename(2) 本身：POSIX 的 rename
// 同卷原子、跨卷报 EXDEV，本来就没有退化路径。
// ⚠ 本函数是**从 VectorStagedOutput 的私有静态成员原样提到文件作用域**的（批次 D / D0-C3）：
//   分支、返回值、错误文案逐字未改，数据集侧与 CSV 侧共用这一份。
static bool replaceStagedFile(const fs::path& stagedFile, const fs::path& target,
                              std::string& error) {
#ifdef _WIN32
    const std::wstring stagedWide = utf8ToWide(stagedFile.u8string());
    const std::wstring targetWide = utf8ToWide(target.u8string());
    if (stagedWide.empty() || targetWide.empty()) {
        error = "Cannot encode staged output path: " + stagedFile.u8string();
        return false;
    }
    if (MoveFileExW(stagedWide.c_str(), targetWide.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        return true;
    }
    error = "Cannot replace output " + target.u8string() + " with the staged file " +
        stagedFile.u8string() + " (Win32 error " +
        std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
    return false;
#else
    std::error_code ec;
    fs::rename(stagedFile, target, ec);
    if (!ec) {
        return true;
    }
    error = "Cannot replace output " + target.u8string() + " with the staged file " +
        stagedFile.u8string() + "：" + ec.message();
    return false;
#endif
}

// ── 批次 D / D0-C4（N157）：GDAL 数据源串 → 纯路径；三处输出保护闸共用**这一份**───────────
// 病灶：本文件三处"输出不得就是输入"的等价闸判的是 std::filesystem 的等价，而 openVector 明确接受
// GDAL 的**数据源串**当合法输入（`GPKG:"<路径>":<图层>` 这类"驱动名前缀 + 路径 + 图层后缀"的拼法；
// 既有用例 VectorSpatialJoinAcceptsPrefixedGpkgSourceString / VectorNearestAcceptsPrefixedGpkgSourceString
// 钉的就是这种拼法今天能跑 ⇒ 它是合法输入，不是脏数据）。这种串对 filesystem 不是一个路径 ⇒ 带 ec 的
// equivalent 恒得 false ⇒ 闸看不见"输出其实就是输入那个文件本身"（台账 N157 / 工单 D0-C4）。
// 本函数只干一件事：认出带驱动前缀的拼法、取出它背后那个**文件路径**，让三处闸用同一个口径比。
// ⚠ **认不出来就原样返回**，此时本闸不覆盖该形态：归一结果仍不是有效路径 ⇒ equivalent 走 ec 分支
//   判 false ⇒ 放行。这是工单**甲案**的边界，不是遗漏 —— 乙案（认不出形态即拒）会把今天能跑的其它
//   参数化拼法一起变成失败（`/vsizip/…`、`/vsimem/…`、`CSV:/path` 这类），属未批准的新增拒绝面。
//   现在**不覆盖**的形态逐字摆出来：`/vsizip/dir.zip/x.gpkg`、`/vsimem/x.gpkg`（无 ':'，原样返回，
//   本来也不是盘上的一个文件）；连接串形态（`PG:dbname=…`）；任何"':' 前那段不是已注册驱动短名"的写法。
// 识别规则（宁可少认，不可错认 —— 错认会把一条合法裸路径改成别的路径）：
//   ① 串里没有 ':' ⇒ 普通路径，原样返回；
//   ② ':' 前是一个字母且紧跟 '/' 或 '\' ⇒ Windows 盘符（`C:/x.gpkg`、`D:\dir\x.gpkg`），原样返回；
//   ③ ':' 前那段必须与某个**已注册的 GDAL 驱动短名**逐字相同（ASCII 大小写不敏感、且不含路径分隔符）
//     才认作前缀。短名向 GDALDriverManager 现取，不写死名单 ⇒ 驱动增减不必回来改这里；
//   ④ 前缀后的剩余：以 '"' 打头就截到下一个 '"'（GDAL 的引号形态，路径里可带空格）；不带引号时，
//     若末尾还挂着一段不含路径分隔符的 ':' 尾巴（图层名），把它截掉；
//   ⑤ 剥完剩下的必须非空且不含 '"' 才认，否则原样返回。
// ⚠ 本函数**不做**任何等价判定、也不查文件存在性：三处闸各只留一份等价判定（结构守卫
//   PluginTest.VectorStagedOutputNormalizationSharedAcrossAllThreeGates 按出现次数恰为 1 数这件事）。
static fs::path datasetSourceAsPlainPath(const std::string& source) {
    const std::size_t colon = source.find(':');
    if (colon == std::string::npos || colon == 0) {
        return fs::u8path(source);
    }

    // 规则②：单字母 + ':' + 路径分隔符 = Windows 盘符，不是驱动前缀。
    if (colon == 1 && source.size() > 2 && std::isalpha(static_cast<unsigned char>(source[0])) &&
        (source[2] == '/' || source[2] == '\\')) {
        return fs::u8path(source);
    }

    // 规则③：前缀段必须逐字等于（大小写不敏感）某个已注册驱动短名。
    const std::string prefix = source.substr(0, colon);
    if (prefix.find('/') != std::string::npos || prefix.find('\\') != std::string::npos) {
        return fs::u8path(source);
    }
    auto asciiEqualIgnoreCase = [](const std::string& left, const char* right) {
        std::size_t i = 0;
        for (; i < left.size() && right[i] != '\0'; ++i) {
            if (std::tolower(static_cast<unsigned char>(left[i])) !=
                std::tolower(static_cast<unsigned char>(right[i]))) {
                return false;
            }
        }
        return i == left.size() && right[i] == '\0';
    };
    bool isDriverPrefix = false;
    if (auto* driverManager = GetGDALDriverManager()) {
        for (int i = 0; i < driverManager->GetDriverCount() && !isDriverPrefix; ++i) {
            GDALDriver* driver = driverManager->GetDriver(i);
            isDriverPrefix = driver != nullptr && asciiEqualIgnoreCase(prefix, driver->GetDescription());
        }
    }
    if (!isDriverPrefix) {
        return fs::u8path(source);
    }

    // 规则④/⑤：取前缀背后的路径。
    const std::string rest = source.substr(colon + 1);
    std::string candidate;
    if (!rest.empty() && rest.front() == '"') {
        const std::size_t closing = rest.find('"', 1);
        if (closing == std::string::npos) {
            return fs::u8path(source);   // 引号没闭合 ⇒ 不猜，原样返回
        }
        candidate = rest.substr(1, closing - 1);
    } else {
        candidate = rest;
        const std::size_t lastColon = candidate.rfind(':');
        if (lastColon != std::string::npos) {
            const std::string tail = candidate.substr(lastColon + 1);
            const bool tailIsLayerName = !tail.empty() &&
                tail.find('/') == std::string::npos && tail.find('\\') == std::string::npos;
            if (tailIsLayerName) {
                candidate = candidate.substr(0, lastColon);
            }
        }
    }
    if (candidate.empty() || candidate.find('"') != std::string::npos) {
        return fs::u8path(source);
    }
    return fs::u8path(candidate);
}

// ── C3n-1：矢量输出的失败契约载体「同目录临时件 → 全部成功 → 显式关闭 → 替换目标」────────
// 契约出处：用户 2026-10-23 的裁定，原文登记在 docs/RELEASE.md 的 N137 条目。
// 要治的病：矢量动作今天**先截断目标再写**（16 个写出点甚至在驱动建数据集之前就把整个目标
// fs::remove 掉），于是"写到一半失败"交出去的是"旧产物已经没了 + 新产物又不可用"。
// 本类把成功与失败两条路径都改成：
//   ① 只往**同目录**临时件写：<目标目录>/<目标 stem>.gis-tmp-<进程号>-<进程内自增序号><目标扩展名>
//     （不放 build/tmp、不放 %TEMP% ⇒ 那些地方一跨卷，替换就退化成复制，见机制②）；
//   ② 全部写完 ⇒ **先显式关闭数据集**（释放句柄、让驱动把 rtree 与日志收干净）；
//   ③ 再把临时件族逐个替换到目标名（Shapefile 是**逐文件**替换 ⇒ 这一步非原子，见 commit 里
//     那句"诚实说法"，台账里不许写成"Shapefile 原子改名"）；
//   ④ 任何中途失败或异常展开 ⇒ 析构函数把临时件族删干净 ⇒ 旧产物原样存活、不留半截新产物。
// ⚠ 清理义务跟着**对象生命周期**走，不跟着某个 return 语句走（工单 §2.4：24 个点、多点返回，
//   手写清理必漏）。动作里那些 CancelledException 展开也走同一个析构。
// ⚠ 现读已接 24 个写出点（C3n-1 的 boundary / centroid / vertices_extract，C3n-2a 的 8 个
//   "无事务 + 单输出"点：convex_hull / envelope / endpoints_extract / midpoints_extract /
//   interior_point / singlepart / sliver_remove / filter，C3n-2b 的 geom_metrics / nearest /
//   spatial_join / repair / simplify，C3n-2c 的 buffer / clip / dissolve / polygonize /
//   convert，以及 C3n-2d 的 overlay 三点 union / difference / intersect）⇒ 矢量写出点已全部
//   按本契约走，J5 的待迁移名单为空。新增写出点必须自己接进本类，否则 J5 的
//   "扫到的 (文件,函数) 总数 == 24" 与"名单外命中"会同时红。
// ⚠ 迁移进本类的点不再沿用旧 helper（那个"按 stem 删旧产物"的公共函数）的"在建数据集之前
//   先把旧产物与同 stem 边车删掉"：对应物是 commit 末尾的 removeStaleOldSidecars ⇒ 删除被
//   挪到新件全部落地之后（反过来就会交出".shp 是新的、.dbf 没了"这种比旧态更坏的产物）。
//   那个旧 helper 本身在 C3n-2d 已删（调用点 0；overlay 三点从来没调用过它）。
class VectorStagedOutput {
public:
    VectorStagedOutput() = default;
    VectorStagedOutput(const VectorStagedOutput&) = delete;
    VectorStagedOutput& operator=(const VectorStagedOutput&) = delete;
    ~VectorStagedOutput() { discardStagedFiles(); }

    // 建临时件、把它交给驱动建数据集。
    // protectedInputs 是"绝不许被输出覆盖掉"的输入清单。**等价闸走在任何建目录/建文件之前**
    //（工单 §3 J4：命中的时点是"删除/创建之前"，不是"删完了再道歉"）。
    // 已迁移的 21 个动作里 18 个是单输入 ⇒ 传一个；nearest / spatial_join / clip 是双输入
    // （nearest 与 spatial_join 在 C3n-2b 接线、clip 在 C3n-2c 接线，两个输入都进保护清单 ⇒
    // 用例 VectorDoubleInputOutputMustNotOverwriteSecondInput 专打"输出 == 第二个输入"）。
    // 后续铺到 union / difference / intersect 那批双输入动作时同样要**两个都传**。
    // 返回 false 时 error 是一句能直接给用户的失败原因（口径随同插件既有的硬失败写法）。
    bool start(const std::string& output, const std::string& format,
               const std::vector<std::string>& protectedInputs, GDALDriver* drv,
               std::string& error) {
        format_ = format;
        target_ = fs::u8path(output);
        protectedInputs_.clear();
        for (const auto& input : protectedInputs) {
            if (!input.empty()) {
                // 存**用户敲进去的原样拼法**（不是 fs::path）：比较时才归一，
                // 失败文案要回显的也是原样拼法（否则用户看不出自己是带前缀写进来的）。
                protectedInputs_.push_back(input);
            }
        }
        if (target_.empty() || target_.filename().empty()) {
            error = "output is required";
            return false;
        }

        for (const auto& rawInput : protectedInputs_) {
            std::error_code ec;
            // 先归一再比：`GPKG:"x.gpkg":layer` 这类 openVector 接受的合法数据源串背后就是那个文件，
            // 直接拿串当路径比会恒判"不是同一个文件"（N157 的病灶）。归一认不出的形态原样返回，
            // 本闸此时不覆盖它 —— 甲案的边界，见本函数上方注释。
            const fs::path input = datasetSourceAsPlainPath(rawInput);
            // 判"同一个文件"要用**等价**而不是字符串相等：`dir\.\x.gpkg` 与 `dir\x.gpkg` 是
            // 同一个文件，Windows 上大小写不同的拼法也是同一个文件 ⇒ 拿 `==` 当闸会漏
            //（J4 的 dotslash 臂专打这一点）。输出本来不存在时 equivalent 走 ec 分支返回 false
            // ⇒ 不会误伤"输出还没建"这条常规路径。
            if (fs::equivalent(input, target_, ec)) {
                error = "Output must not be the same file as input: " + output +
                    " (input: " + rawInput + ")";
                return false;
            }
        }

        ensureParentDirectoryForFile(output);

        // 命名与序号的单一来源见上面 stagedOutputPrefix()（数据集侧与 CSV 侧共用一把计数器）。
        stagedPrefix_ = stagedOutputPrefix(target_);
        const std::string stagedName = stagedPrefix_ + target_.extension().u8string();
        staged_ = targetDir() / fs::u8path(stagedName);

        // 上次崩溃留的同前缀孤儿件先清掉 ⇒ 否则 commit 会把别人的残骸当成本轮产物搬进目标名。
        // （前缀含进程号 + 序号，正常路径本来就是空的；只有进程重启且序号重上才算得上。）
        for (const auto& orphan : listStagedFamily()) {
            std::error_code ec;
            fs::remove(orphan, ec);
        }

        const std::string stagedUtf8 = staged_.u8string();
        dataset_ = gis::core::GdalDatasetPtr(
            drv->Create(stagedUtf8.c_str(), 0, 0, 0, GDT_Unknown, nullptr));
        if (!dataset_) {
            error = "Cannot create output file: " + output;
            discardStagedFiles();
            return false;
        }
        return true;
    }

    // 动作写的那个数据集：裸指针，所有权在本对象（析构负责关闭）。
    GDALDataset* dataset() const { return dataset_.get(); }

    // 成功路径的收口：**关闭数据集 → 逐文件替换 → 清旧边车**。
    // 返回 false ⇒ 调用方必须把这次动作报成失败；此时旧产物仍在场，临时件族由析构兜住。
    bool commit(std::string& error) {
        // 这一行是本刀最容易做错的地方（工单 §2.3）。今天全插件 0 处显式 Close()，数据集本来
        // 要靠作用域结束才 GDALClose ⇒ 若在还握着句柄时改名，Windows 上替换会撞
        // ERROR_ACCESS_DENIED，GPKG 的 -wal/-journal 也还没收（J3 判的就是产物里不许有它们）。
        dataset_.reset();

        std::vector<std::string> landedExtensions;
        for (const auto& stagedFile : listStagedFamily()) {
            const fs::path landing = landingPathFor(stagedFile);
            if (landing.empty()) {
                continue;   // 同前缀的附属件（例如 SQLite 的 -wal）不是产物，留到下面统一删
            }
            if (!replaceStagedFile(stagedFile, landing, error)) {
                // ⚠ 诚实说法（工单 §2.5）：Shapefile 的替换是**逐文件**的 ⇒ 这一支可能在"已经
                // 搬走几个边车"的中途停下，留下混合态。契约能保证的是"新件完工之前旧产物一直完好"，
                // 不能保证"替换段本身原子"。
                return false;
            }
            landedExtensions.push_back(lowerExtension(stagedFile));
        }

        if (landedExtensions.empty()) {
            error = "Output was not produced: " + target_.u8string();
            return false;
        }

        // 替换完成后同前缀还剩下的文件一律是本轮临时件的附属残骸（GPKG 的 -wal/-journal/-shm
        // 正常已被上面的 close 收掉；还在就说明驱动没收干净）⇒ 删，绝不留给用户。
        for (const auto& leftover : listStagedFamily()) {
            std::error_code ec;
            fs::remove(leftover, ec);
        }

        // 旧边车的清理**只能发生在新件全部完工之后**（工单 §2.5）。这一支就是旧代码里
        // "按同 stem 遍历删边车"那句的对应物，只是搬到了终点。
        removeStaleOldSidecars(landedExtensions);
        return true;
    }

private:
    // 输出所在目录（相对路径 `x.gpkg` 的 parent 是空 path ⇒ 归一成 "."，否则清单扫描会空转）。
    fs::path targetDir() const {
        const fs::path parent = target_.parent_path();
        return parent.empty() ? fs::path(".") : parent;
    }

    static std::string lowerExtension(const fs::path& path) {
        std::string ext = path.extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return ext;
    }

    // 临时件族 = 目标目录里**文件名以本轮前缀打头**的那些普通文件。
    // WHY 按前缀而不是按 stem 相等：Shapefile 的边车是 <前缀>.shp/.shx/.dbf/.prj（stem 相同），
    // 而 GPKG 的 SQLite 附属件是 <前缀>.gpkg-wal / ...-journal（stem 不同，中间多了一段 .gpkg）
    // ⇒ 只按 stem 收会漏掉附属件，于是清理与替换都只完成一半。
    std::vector<fs::path> listStagedFamily() const {
        std::vector<fs::path> found;
        if (stagedPrefix_.empty()) {
            return found;
        }
        const fs::path dir = targetDir();
        std::error_code ec;
        if (!fs::exists(dir, ec) || ec) {
            return found;
        }
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            std::error_code secer;
            if (!it->is_regular_file(secer) || secer) {
                continue;
            }
            const std::string name = it->path().filename().u8string();
            if (name.size() >= stagedPrefix_.size() &&
                name.compare(0, stagedPrefix_.size(), stagedPrefix_) == 0) {
                found.push_back(it->path());
            }
        }
        std::sort(found.begin(), found.end());
        return found;
    }

    // 本轮临时件族里某个文件该落到目标目录的哪个名字；空 path = "它不是产物，不许落"。
    // 交给驱动的那个文件（扩展名与目标一致）永远落到目标名；ESRI Shapefile 的其它同前缀边车
    // 按"<目标 stem> + 各自的扩展名"落（.shp/.shx/.dbf/.prj/... 一一对应）。
    fs::path landingPathFor(const fs::path& stagedFile) const {
        const std::string name = stagedFile.filename().u8string();
        if (name == staged_.filename().u8string()) {
            return target_;
        }
        if (format_ != "ESRI Shapefile") {
            return fs::path();
        }
        return targetDir() / fs::u8path(
            target_.filename().stem().u8string() + stagedFile.extension().u8string());
    }

    // 同卷替换的原语在本文件**文件作用域**的 replaceStagedFile()（批次 D / D0-C3 从本类私有
    // 静态成员原样提出，与 CSV 侧共用一份；WHY 只留一份：两把语义会漂）。

    // 新件全部落地之后，把**旧产物**留下的、这一轮没产出的同名边车清掉。
    // Shapefile：同 stem 但扩展名不在本轮落地集合里的那些（例：旧的 .prj，而这轮没写 .prj）。
    // GPKG：换名之后还能看见 <目标>.gpkg-wal / -journal / -shm，就说明它们属于**上一份产物**
    //（本轮那份是关闭干净才换名的）⇒ 一并清掉，否则用户拿到"新库 + 旧日志"的混合体。
    // 两种形状都不许碰受保护的输入：输出与某个输入"同 stem 不同扩展名"（nearest /
    // spatial_join 那批就有这种拼法）时，按 stem 遍历会把输入自己的边车扫进去 ⇒ 先等价挡一次。
    void removeStaleOldSidecars(const std::vector<std::string>& landedExtensions) {
        if (format_ == "ESRI Shapefile") {
            const fs::path dir = targetDir();
            std::error_code ec;
            if (!fs::exists(dir, ec) || ec) {
                return;
            }
            for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                std::error_code secer;
                if (!it->is_regular_file(secer) || secer) {
                    continue;
                }
                const fs::path path = it->path();
                if (path.stem() != target_.stem()) {
                    continue;
                }
                const std::string ext = lowerExtension(path);
                if (std::find(landedExtensions.begin(), landedExtensions.end(), ext) !=
                    landedExtensions.end()) {
                    continue;
                }
                if (isProtectedInput(path)) {
                    continue;
                }
                std::error_code rmec;
                fs::remove(path, rmec);
            }
            return;
        }

        if (format_ == "GPKG") {
            const std::string base = target_.u8string();
            for (const char* suffix : { "-wal", "-journal", "-shm" }) {
                std::error_code rmec;
                fs::remove(fs::u8path(base + suffix), rmec);
            }
        }
    }

    bool isProtectedInput(const fs::path& path) const {
        std::error_code ec;
        for (const auto& rawInput : protectedInputs_) {
            // 与两处 start() 同一个归一（N157）：按 stem 清旧边车那支扫到的可能是**输入自己**，
            // 输入写成数据源串时，这一格里唯一的保护就是先把串背后的路径取出来再比。
            const fs::path input = datasetSourceAsPlainPath(rawInput);
            if (fs::equivalent(input, path, ec)) {
                return true;
            }
        }
        return false;
    }

    // 失败/异常展开路径的兜底：**先关数据集再删文件**（Windows 上还开着句柄的文件删不掉），
    // 然后把本轮临时件族删干净。已 commit 的调用点走不到这里也执行一遍 —— 它删的是"替换后
    // 仍然挂着的前缀残骸"，与 commit 末尾那条是同一支幂等收尾。
    void discardStagedFiles() {
        dataset_.reset();
        for (const auto& stagedFile : listStagedFamily()) {
            std::error_code ec;
            fs::remove(stagedFile, ec);
        }
    }

    std::string format_;
    std::string stagedPrefix_;
    fs::path target_;
    fs::path staged_;
    // 原样拼法（可含 GDAL 数据源前缀）；比较前由本类三处闸各自过一次归一，见 start()/isProtectedInput。
    std::vector<std::string> protectedInputs_;
    gis::core::GdalDatasetPtr dataset_;
};

// ── 批次 D / D0-C3：CSV 文本产物的失败契约载体「等价闸 → 同目录临时件 → 写完 → 关闭 → 替换」──
// 射程：adjacency / dangling_endpoint_check / duplicate_point_check / hole_check /
// multipart_check / overlap_check / topology_check 这 7 个写 CSV 的检查动作。
// 它们不是**数据集**写出点（没有驱动、没有边车、没有 rtree），所以不接进 VectorStagedOutput，
// 但两件必须同源的事与它同源：临时件**命名**（stagedOutputPrefix）与**替换原语**
//（replaceStagedFile，同卷、不带 COPY_ALLOWED）⇒ 仓库里按 `.gis-tmp-` 扫残留的既有判据
// 一样能扫到这 7 个动作的残骸。
// 治的两个病（改前实测见台账对应节，此处不复述数字）：
//   缺陷 A：`--output` 指回 `--input` 那个矢量文件 ⇒ 旧写法 std::ofstream(output, trunc)
//           把输入就地清空、动作还报成功；
//   缺陷 B：写入中途失败（取消 / 磁盘满 / 写坏）⇒ 目标位置留下上一份产物被截断后的半件，
//           与 C3n 的契约"失败 ⇒ 旧产物仍在场"相反。
// 判定顺序（工单硬要求，不许倒）：参数齐备（动作自己已在前面判）→ 输出与输入等价判定 →
// 建父目录 → 开临时件 → 写 → 替换。清理义务跟着**对象生命周期**走：动作里的
// CancelledException 展开同样经析构删临时件，不靠某个 return 语句。
class CsvStagedOutput {
public:
    CsvStagedOutput() = default;
    CsvStagedOutput(const CsvStagedOutput&) = delete;
    CsvStagedOutput& operator=(const CsvStagedOutput&) = delete;
    ~CsvStagedOutput() { discardStagedFile(); }

    // 建临时件并把写端交给动作。protectedInputs 是"绝不许被输出覆盖掉"的输入清单：
    // 这 7 个动作每个**只有一个输入**（现读 getParam<std::string> 只有 input/output/layer 三键），
    // 所以调用方传 { input } 即可； nearest / spatial_join 那种双输入族不归本类。
    // 返回 false 时 error 是一句能直接给用户的失败原因（文案与数据集侧 staged.start 同源）。
    bool start(const std::string& output, const std::vector<std::string>& protectedInputs,
               std::string& error) {
        target_ = fs::u8path(output);
        if (target_.empty() || target_.filename().empty()) {
            error = "output is required";
            return false;
        }

        for (const std::string& rawInput : protectedInputs) {
            if (rawInput.empty()) {
                continue;
            }
            // 先归一再比（批次 D / D0-C4，N157）：`GPKG:"<路径>":<图层>` 这类 openVector 明确接受的
            // 数据源串，背后就是那个文件 ⇒ 取出来比才看得见"输出就是输入"；认不出的形态原样返回，
            // 本闸此时不覆盖该形态（甲案的边界，不许在这里改成"认不出就拒"）。
            const fs::path input = datasetSourceAsPlainPath(rawInput);
            std::error_code ec;
            // 必须用**三参带 std::error_code** 的 equivalent（N156 的教训）：无 ec 的重载对
            // `GPKG:"<路径>":<图层>` 这类合法 GDAL 数据源串抛 filesystem_error ⇒ std 原文会吐给用户。
            // 判"同一个文件"用等价而不是字符串相等：`dir\.\x.gpkg` 与 `dir\x.gpkg` 是同一个文件、
            // Windows 上大小写不同的拼法也是同一个文件 ⇒ 拿 == 当闸会漏。
            // 输出本来不存在时 equivalent 走 ec 分支返回 false ⇒ 不误伤"输出还没建"这条常规路径。
            if (fs::equivalent(input, target_, ec)) {
                error = "Output must not be the same file as input: " + output +
                    " (input: " + rawInput + ")";
                return false;
            }
        }

        ensureParentDirectoryForFile(output);

        stagedPrefix_ = stagedOutputPrefix(target_);
        staged_ = targetDir() / fs::u8path(stagedPrefix_ + target_.extension().u8string());

        // 上次崩溃留的同名孤儿件先清掉 ⇒ 否则本轮的替换会把别人的残骸搬进目标名。
        // （名字含进程号 + 进程内序号，正常路径本来就是空的。）
        std::error_code orphanEc;
        fs::remove(staged_, orphanEc);

        ofs_.open(staged_, std::ios::binary | std::ios::trunc);
        if (!ofs_.is_open()) {
            error = "Cannot create output file: " + output;
            return false;
        }
        return true;
    }

    // 动作写的那个流：引用，所有权在本对象（析构负责关闭）。
    std::ostream& stream() { return ofs_; }

    // 成功路径的收口：**先关闭流**（把缓冲区的最后一行真的落到盘上）→ 替换目标。
    // 返回 false ⇒ 调用方必须把这次动作报成失败；此时旧产物仍在场，临时件由析构兜住。
    bool commit(std::string& error) {
        ofs_.close();
        // 关闭时才暴露的写失败（缓冲区满时写盘失败要在 close/flush 才判得出来）也算这次动作失败：
        // 这条支在旧写法里交出的正是"目标位置一份被截断的半件 + success"。
        if (ofs_.fail()) {
            error = "Cannot write output file: " + target_.u8string();
            return false;
        }
        return replaceStagedFile(staged_, target_, error);
    }

private:
    // 输出所在目录（相对路径 `x.csv` 的 parent 是空 path ⇒ 归一成 "."，与数据集侧同一个处理）。
    fs::path targetDir() const {
        const fs::path parent = target_.parent_path();
        return parent.empty() ? fs::path(".") : parent;
    }

    // 失败 / 异常展开路径的兜底：**先关流再删文件**（Windows 上还开着句柄的文件删不掉）。
    // 已 commit 的调用点走不到这里也执行一遍 —— 换名成功后这个路径已不存在，删除走 ec 静默失败，
    // 与数据集侧 discardStagedFiles() 的幂等收尾同一个口径。
    void discardStagedFile() {
        if (ofs_.is_open()) {
            ofs_.close();
        }
        if (!staged_.empty()) {
            std::error_code ec;
            fs::remove(staged_, ec);
        }
    }

    std::string stagedPrefix_;
    fs::path target_;
    fs::path staged_;
    std::ofstream ofs_;
};

// WHY vector 的 24 个 CreateLayer 点里，这 17 个共用这一个 builder、且它一个图层选项都不发：
// 不发选项 ⇒ GDAL 走它自己的延迟建索引，在 FlushCache 里建 rtree 并把已经在表里的行一次性回填
// （GDAL 侧的判据就是"选项缺省即建"，`bCreateSpatialIndex = (pszSI == nullptr || CPLTestBool(pszSI))`）。
// 反面写法（"先关掉索引批量写、写完再用 SQL 函数补建索引"）交出的必是 0 行的索引 ——
// 那种补建只造 rtree 表和触发器、不回填既有行，而 GDAL 只看 rtree 表在不在就认定"有索引"，
// 于是按包框查的读端拿到 0 个要素（N138；正是这条被 `PluginTest.VectorSourceNeverDisablesGpkgSpatialIndex` 拦着）。
// 事务批量化和"索引归谁建"是两件不相干的事：buffer_clip / dissolve / overlay 那 6 个点照旧分批
// 提交，索引仍由驱动在 FlushCache 里一次性回填 ⇒ 22 855 要素的 union 产物补索引实测只多约 1 s、
// 文件多约 2.5 MB，"省那 1 s"不是把读端打成 0 条的理由。
static std::vector<std::string> buildGpkgDriverIndexedLayerOptions() {
    return {};
}

static std::vector<char*> buildLayerOptionPointers(std::vector<std::string>& options) {
    std::vector<char*> pointers;
    for (auto& option : options) {
        pointers.push_back(option.data());
    }
    pointers.push_back(nullptr);
    return pointers;
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
