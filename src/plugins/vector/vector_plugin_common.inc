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

// 带属性的叠加要素表：union 路径 A 要把"相交块"写成"这一条叠加要素自己的字段"，
// 只有几何 + 包串的 OverlayGeometryEntry 撑不起这件事（属性表全丢是 N128 的第二半）。
struct OverlayFeatureEntry {
    std::unique_ptr<OGRFeature> feature;   // 几何已标准化
    OGREnvelope envelope{};
};

// 取舍规则必须逐字镜像 collectNormalizedGeometries：同样用 cloneNormalizedGeometry 标准化，
// 标准化结果为空或 IsEmpty() 的要素跳过，其余按图层读取顺序压入，包串从标准化后的几何取。
// WHY 要逐字：两张表跳过规则一旦不一致，"各分块面积之和 == 并集面积"这类不变量就会凭空多/少一块。
static std::vector<OverlayFeatureEntry> collectNormalizedFeatures(OGRLayer* layer) {
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
    // N122：窄串构造 fs::path 在 Windows 按 ANSI 解，而这里的 output 是 UTF-8 ⇒ 中文路径下
    // exists / directory_iterator / remove 三件事全部静默失效（判成"不存在"），旧产物的
    // .dbf/.shx/.prj 边车不会被删，GDAL 再按 UTF-8 写出新 .shp ⇒ 用户拿到"新几何 + 旧属性表"。
    // 同文件的 ensureParentDirectoryForFile 早就是这个写法（fs::u8path），这里对齐它。
    const fs::path outputPath = fs::u8path(output);
    if (!fs::exists(outputPath)) {
        return;
    }

    if (format == "ESRI Shapefile") {
        for (const auto& entry : fs::directory_iterator(outputPath.parent_path())) {
            if (entry.path().stem() == outputPath.stem()) {
                fs::remove(entry.path());
            }
        }
        return;
    }

    fs::remove(outputPath);
}

static void ensureParentDirectoryForFile(const std::string& output) {
    const fs::path outputPath = fs::u8path(output);
    if (!outputPath.has_parent_path()) {
        return;
    }

    fs::create_directories(outputPath.parent_path());
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
// ⚠ 现读已接 16 个写出点（C3n-1 的 boundary / centroid / vertices_extract，C3n-2a 的 8 个
//   "无事务 + 单输出"点：convex_hull / envelope / endpoints_extract / midpoints_extract /
//   interior_point / singlepart / sliver_remove / filter，以及 C3n-2b 的 geom_metrics / nearest /
//   spatial_join / repair / simplify），其余 8 个仍在直接写目标。
//   "现在是半迁移态"这件事由用例侧的结构守卫承重（J5：已迁移 == 16 / 待迁移名单 == 8 逐条点名），
//   所以这里不许写成"矢量输出已全部按契约走"那种会被当成收口结论的话。
// ⚠ 迁移进本类的点**不再**调 removeExistingVectorOutput：旧代码那一句"在建数据集之前先把旧产物
//   与同 stem 边车删掉"的对应物是 commit 末尾的 removeStaleOldSidecars ⇒ 删除被挪到新件全部
//   落地之后（反过来就会交出".shp 是新的、.dbf 没了"这种比旧态更坏的产物）。
//   其余 8 个点由 C3n-2b..e 逐点搬。
class VectorStagedOutput {
public:
    VectorStagedOutput() = default;
    VectorStagedOutput(const VectorStagedOutput&) = delete;
    VectorStagedOutput& operator=(const VectorStagedOutput&) = delete;
    ~VectorStagedOutput() { discardStagedFiles(); }

    // 建临时件、把它交给驱动建数据集。
    // protectedInputs 是"绝不许被输出覆盖掉"的输入清单。**等价闸走在任何建目录/建文件之前**
    //（工单 §3 J4：命中的时点是"删除/创建之前"，不是"删完了再道歉"）。
    // 已迁移的 16 个动作里 14 个是单输入 ⇒ 传一个；nearest / spatial_join 是双输入
    // （C3n-2b 已接线，两个输入都进保护清单 ⇒ 用例 VectorDoubleInputOutputMustNotOverwriteSecondInput
    // 专打"输出 == 第二个输入"）。后续铺到 union / clip / difference / intersect 那批双输入动作时
    // 同样要**两个都传**。
    // 返回 false 时 error 是一句能直接给用户的失败原因（口径随同插件既有的硬失败写法）。
    bool start(const std::string& output, const std::string& format,
               const std::vector<std::string>& protectedInputs, GDALDriver* drv,
               std::string& error) {
        format_ = format;
        target_ = fs::u8path(output);
        protectedInputs_.clear();
        for (const auto& input : protectedInputs) {
            if (!input.empty()) {
                protectedInputs_.push_back(fs::u8path(input));
            }
        }
        if (target_.empty() || target_.filename().empty()) {
            error = "output is required";
            return false;
        }

        for (const auto& input : protectedInputs_) {
            std::error_code ec;
            // 判"同一个文件"要用**等价**而不是字符串相等：`dir\.\x.gpkg` 与 `dir\x.gpkg` 是
            // 同一个文件，Windows 上大小写不同的拼法也是同一个文件 ⇒ 拿 `==` 当闸会漏
            //（J4 的 dotslash 臂专打这一点）。输出本来不存在时 equivalent 走 ec 分支返回 false
            // ⇒ 不会误伤"输出还没建"这条常规路径。
            if (fs::equivalent(input, target_, ec)) {
                error = "Output must not be the same file as input: " + output +
                    " (input: " + input.u8string() + ")";
                return false;
            }
        }

        ensureParentDirectoryForFile(output);

        // 进程号挡跨进程撞名，进程内自增序号挡同进程两个动作撞名（GUI 的任务队列与测试
        // 都会在一个进程里并发跑多个动作）。
        static std::atomic<unsigned long> sequence{ 0 };
        const unsigned long seq = sequence.fetch_add(1) + 1;
        stagedPrefix_ = target_.filename().stem().u8string() + ".gis-tmp-" +
            stagedProcessTag() + "-" + std::to_string(seq);
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

    static std::string stagedProcessTag() {
#ifdef _WIN32
        return std::to_string(static_cast<unsigned long>(GetCurrentProcessId()));
#else
        return std::to_string(static_cast<unsigned long>(::getpid()));
#endif
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

    // 同卷替换。**这里不走 std::filesystem::rename**：本机 MSVC STL（工具集 14.44.35207，
    // crt/src/stl/filesystem.cpp:722）把它实现成
    //   MoveFileExW(_Source, _Target, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)
    // ⇒ "目标已存在"确实是覆盖成功不报错（这正是替换语义），但带着 COPY_ALLOWED 后
    // **跨卷会悄悄退化成 copy+delete**，"同卷原子替换"就兑不了 ⇒ 自己发一次不带
    // COPY_ALLOWED 的 MoveFileExW：跨卷直接失败，而不是半路变成复制。
    // 非 Windows 分支跟着本仓既有的 #ifdef _WIN32 形态走，用 rename(2) 本身：POSIX 的 rename
    // 同卷原子、跨卷报 EXDEV，本来就没有退化路径。
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
        for (const auto& input : protectedInputs_) {
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
    std::vector<fs::path> protectedInputs_;
    gis::core::GdalDatasetPtr dataset_;
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
