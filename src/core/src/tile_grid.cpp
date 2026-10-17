#include <gis/core/tile_grid.h>

#include <algorithm>
#include <limits>

namespace gis::core {

namespace {

// EPSG:3857 世界半宽（米）。与 OGC/GeoWebCache 通用取值一致；
// 端到端用例（tests/test_plugins.cpp 的 Tile 几何钉）按同一常量计算期望瓦片集合。
constexpr double kMaxWebMercator = 20037508.342789244;

int safeTileSize(int tileSize) {
    return std::max(1, tileSize);
}

} // namespace

TileGrid mercatorTileGrid(int zoom, int tileSize) {
    TileGrid grid;
    grid.worldMinX = -kMaxWebMercator;
    grid.worldMinY = -kMaxWebMercator;
    grid.worldMaxX = kMaxWebMercator;
    grid.worldMaxY = kMaxWebMercator;

    // 全球边长像元数 = tileSize * 2^z。用 long long 中间量再钳回 int：
    // (zoom=22, tileSize=1024) 这类组合的真值 4.29e9 超出 int 表达上限，
    // 此时取"int 内最大的瓦片对齐网格"，tileWindow/tileWorldRect 一律从
    // globalWidth 推导瓦片数与分辨率，三个函数同源，越界层级不会互相错开。
    // 合法域（z<=20 且 tileSize<=1024，或 tileSize=256 且 z<=22）内该钳位不生效，
    // 网格与真值逐位一致。
    const int ts = safeTileSize(tileSize);
    const int z = std::clamp(zoom, 0, 40);
    const long long count = static_cast<long long>(ts) << z;
    const long long cap = (std::numeric_limits<int>::max() / ts) * static_cast<long long>(ts);
    grid.globalWidth = static_cast<int>(std::min(count, cap));
    grid.globalHeight = grid.globalWidth;
    return grid;
}

TileWindow tileWindow(const TileGrid& grid, int tileSize, int tileX, int tileY) {
    TileWindow win;
    const int ts = safeTileSize(tileSize);
    const int tiles = std::max(1, grid.globalWidth / ts);
    if (tileX < 0 || tileY < 0 || tileX >= tiles || tileY >= tiles) return win;
    win.xOff = tileX * ts;
    win.yOff = tileY * ts;
    win.xSize = std::min(ts, grid.globalWidth - win.xOff);
    win.ySize = std::min(ts, grid.globalHeight - win.yOff);
    win.valid = win.xSize > 0 && win.ySize > 0;
    return win;
}

TileRect tileWorldRect(const TileGrid& grid, int tileSize, int tileX, int tileY) {
    TileRect rect;
    const int ts = safeTileSize(tileSize);
    const int tiles = std::max(1, grid.globalWidth / ts);
    if (tileX < 0 || tileY < 0 || tileX >= tiles || tileY >= tiles) return rect;

    const double resX = (grid.worldMaxX - grid.worldMinX) / static_cast<double>(grid.globalWidth);
    const double resY = (grid.worldMaxY - grid.worldMinY) / static_cast<double>(grid.globalHeight);
    if (!(resX > 0.0) || !(resY > 0.0)) return rect;

    rect.minX = grid.worldMinX + tileX * ts * resX;
    rect.maxX = std::min(grid.worldMaxX, rect.minX + ts * resX);
    // XYZ 行序自上而下：tileY=0 的上边贴 worldMaxY（北在上）。
    rect.maxY = grid.worldMaxY - tileY * ts * resY;
    rect.minY = std::max(grid.worldMinY, rect.maxY - ts * resY);
    rect.valid = rect.maxX > rect.minX && rect.maxY > rect.minY;
    if (!rect.valid) {
        rect = TileRect{};
    }
    return rect;
}

} // namespace gis::core
