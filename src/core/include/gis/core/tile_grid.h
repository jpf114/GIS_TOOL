#pragma once

// XYZ（slippy map）瓦片网格的世界几何纯函数，批次 A Task 17。
//
// 这里只描述"世界网格几何"：层级 → 全球像素尺寸、瓦片索引 → 全球像素窗口、
// 瓦片索引 → EPSG:3857 世界矩形。不含任何 GDAL/OpenCV 依赖，可被插件与
// tests/test_core.cpp 直接纯函数单测（R-T17-7 第 1 点：端到端断言必须能在
// 插件之外独立钉住这条几何，否则等于拿插件自己的换算验插件自己的产物）。
//
// 口径：标准 XYZ/slippy-map —— 世界范围是 Web Mercator 正方形
// [-20037508.342789244, 20037508.342789244]²，层级 z 有 2^z × 2^z 张瓦片，
// 全球边长 tileSize*2^z 像元；瓦片行 y 自上（北）而下（南）递增（xyz 方案；
// tms 的反转归调用方处理，本层不掺混存储方案）。

namespace gis::core {

// 某一层级的世界网格几何（EPSG:3857 单位：米；globalWidth/Height 单位：像元）。
struct TileGrid {
    double worldMinX = 0.0, worldMinY = 0.0, worldMaxX = 0.0, worldMaxY = 0.0;
    int globalWidth = 0, globalHeight = 0;
};

// 瓦片在"全球像素"坐标系里的整数读窗（tileWindow 语义即全球网格上的裁剪窗口）。
struct TileWindow {
    int xOff = 0, yOff = 0, xSize = 0, ySize = 0;
    bool valid = false;
};

// 瓦片索引对应的 EPSG:3857 世界矩形（米）。valid=false 表示索引越界。
struct TileRect {
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    bool valid = false;
};

// z=0 起算的层级网格；zoom<0 按 0、tileSize<1 按 1 处理（纯函数不拒非法输入，
// 但非法值不得产生 0 宽网格或除零）。tileSize*2^z 超出 int 可表达范围时取 int 内
// 最大的瓦片对齐网格；tileWindow/tileWorldRect 都从 globalWidth 推导，三者恒同源。
TileGrid mercatorTileGrid(int zoom, int tileSize);

// 瓦片 (tileX,tileY) 在全球像素网格上的整数窗口；索引越界时 valid=false。
TileWindow tileWindow(const TileGrid& grid, int tileSize, int tileX, int tileY);

// 瓦片 (tileX,tileY) 的 EPSG:3857 世界矩形；索引越界时 valid=false，四边保持默认值。
// y 轴约定：tileY=0 贴 worldMaxY（北在上），与 XYZ 方案一致。
TileRect tileWorldRect(const TileGrid& grid, int tileSize, int tileX, int tileY);

} // namespace gis::core
