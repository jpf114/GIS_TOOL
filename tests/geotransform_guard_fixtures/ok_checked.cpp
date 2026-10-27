// geotransform_guard 自证夹具（**不参与编译**，理由同 bad_unchecked.cpp 文件头）。
// 本文件证明守卫**不过严**：五种"确实判了返回值"的写法都必须被认成 CHECKED。
// 少了这一臂，一个"把所有位点都判成 UNCHECKED"的坏解析器照样能过 bad 那一臂（批次 C 的
// "四类缺陷只报得出 2 类"是同族坑，只是方向相反）。
//
// 期望（与 .py 里 SELF_ARMS["ok"] 逐字同源）：
//   occurrences_any = 8   code = 6   comment_line = 1   block_comment = 0   string_only = 1
//   checked = 6   unchecked = 0   violations = 0
// 六形态 = 仓里现存 14 个 CHECKED 位点的写法全集（现读：`== CE_None` / `!= CE_None` /
// `if (` 换行 / `return … != CE_None` / 赋值后比回来），外加一条比较式掉到下一行的续行臂。

// 形态 1：命中行内 `!= CE_None` 即失败（terrain_plugin.cpp:1200、raster_manage_plugin.cpp:277 同形）
void okSiteInlineNotEqual(GDALDataset* ds) {
    double gt[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    if (ds->GetGeoTransform(gt) != CE_None) {
        return failWith("input raster geotransform is missing");
    }
}

// 形态 2：命中行内 `== CE_None` 才用（gdal_wrapper.cpp:73、raster_inspect_plugin.cpp:205 同形）
void okSiteInlineEqual(GDALDataset* ds) {
    double gt[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    if (ds->GetGeoTransform(gt) == CE_None) {
        writeIt(gt);
    }
}

// 形态 3：布尔量 = 比较式（本刀 B/C/D 改完后的形态，比较仍在命中行上）
void okSiteBoolFromCompare(GDALDataset* ds) {
    double gt[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    const bool hasGeoTransform = ds->GetGeoTransform(gt) == CE_None;
    if (hasGeoTransform) {
        dstDS->SetGeoTransform(gt);
    }
}

// 形态 4：`return` 直接把比较式交出去（test_plugins.cpp:1922 同形）
bool okSiteReturnCompare(GDALDataset* ds) {
    double gt[6];
    return ds->GetGeoTransform(gt) != CE_None;
}

// 形态 5：比较式**掉到下一行**（续行），命中行本身没有 CE_* ⇒ 靠跨行语句拼接才认得出
void okSiteContinuedCompare(GDALDataset* ds) {
    double gt[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    if (ds->GetGeoTransform(gt)
        != CE_None) {
        return failWith("missing");
    }
}

// 形态 6（E2 规则的正面对照）：CPLErr 存进变量、在窗口内比回来 ⇒ 判过
void okSiteAssignedThenJudged(GDALDataset* ds) {
    double gt[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    const CPLErr err = ds->GetGeoTransform(gt);
    if (err != CE_None) {
        return failWith("missing");
    }
}

// 干扰项：注释与字符串里的同形态不许进 CODE 数
// ds->GetGeoTransform(inComment);
void okSiteStringDistractor(GDALDataset* ds) {
    logMessage("if (ds->GetGeoTransform(x) == CE_None) { … }");
}
