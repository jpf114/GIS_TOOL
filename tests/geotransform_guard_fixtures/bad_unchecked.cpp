// geotransform_guard 自证夹具（**不参与编译**：tests/CMakeLists.txt 的 gis_tests 源列表里没有本目录，
// 现读 tests/CMakeLists.txt:3-42 是逐文件显式列名、没有 GLOB）。
// 本文件的唯一职责是让 tests/check_geotransform_guard.py 的解析器在它上面**必须报得出**声明的缺陷，
// 报不出就说明守卫本身是死的，守卫不许去判仓库。
//
// 期望（与 .py 里 SELF_ARMS["bad"] 逐字同源，改这里必须同步改那里，两边不一致守卫就红）：
//   occurrences_any = 7   code = 3   comment_line = 1   block_comment = 1   string_only = 2
//   checked = 0   unchecked = 3   violations（空白名单时）= 3
// 四类缺陷族各埋至少一行（批次 C 教训：docs_match_cli 第一版只埋 1 行、四类缺陷只报得出 2 类）：
//   (1) 裸调用不判返回值        (2) 行注释里的同形态
//   (3) 块注释里的同形态        (4) 字符串字面量里的同形态
//   (5) 赋值了但从不比回来（E2 规则的反面）

void badSiteBareCall(GDALDataset* srcDS) {
    double adfGT[6];
    srcDS->GetGeoTransform(adfGT);          // 缺陷 (1)：返回值整个丢弃（= 本刀 B/C 的改前形态）
    dstDS->SetGeoTransform(adfGT);
}

void badSiteAssignedButNeverJudged(GDALDataset* srcDS) {
    double gt[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    const CPLErr gtErr = srcDS->GetGeoTransform(gt);   // 缺陷 (5)：存进变量却再没和 CE_* 比过 ⇒ 不算"判了"
    dstDS->SetGeoTransform(gt);
    (void)gtErr;
}

void badSiteWarpedBare(GDALDataset* warpedDS) {
    double wgt[6] = {};
    warpedDS->GetGeoTransform(wgt);         // 缺陷 (1')：第二处裸调用（白名单只该挡住点名那一处）
}

// srcDS->GetGeoTransform(inLineComment);   缺陷 (2)：行注释里的同形态，不许进 CODE 数
/* 缺陷 (3)：块注释里的同形态
   srcDS->GetGeoTransform(inBlockComment);
   这一行也必须留在块注释里 */

void badSiteStringOnly(GDALDataset* srcDS) {
    logMessage("srcDS->GetGeoTransform(inString);");      // 缺陷 (4)：字符串字面量，不是调用位点
    logMessage("second literal srcDS->GetGeoTransform(inString2); and no call here");
}
