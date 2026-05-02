#include "vector_plugin.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <gdal_alg.h>
#include <gdal_utils.h>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace gis::plugins {

namespace fs = std::filesystem;

namespace {

bool containsNonAscii(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch >= 0x80;
    });
}

bool isShapefilePath(const std::string& path) {
    fs::path fsPath = fs::u8path(path);
    std::string ext = fsPath.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".shp";
}

#ifdef _WIN32
std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int sizeNeeded = MultiByteToWideChar(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        nullptr, 0);
    if (sizeNeeded <= 0) {
        return {};
    }

    std::wstring wide(sizeNeeded, L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
        wide.data(), sizeNeeded);
    return wide;
}

std::string makeAsciiShapefileMirror(const std::string& sourcePath) {
    const fs::path sourceFsPath(utf8ToWide(sourcePath));
    if (sourceFsPath.empty() || !fs::exists(sourceFsPath)) {
        return sourcePath;
    }

    std::error_code ec;
    const auto sourceFileSize = fs::file_size(sourceFsPath, ec);
    const auto sourceWriteTime = fs::last_write_time(sourceFsPath, ec);
    const auto cacheKey = std::to_string(std::hash<std::string>{}(sourcePath)) + "_" +
        std::to_string(static_cast<unsigned long long>(sourceFileSize)) + "_" +
        std::to_string(sourceWriteTime.time_since_epoch().count());
    const fs::path tempDir = fs::temp_directory_path() / "gis_tool_shapefile_utf8" / cacheKey;
    fs::create_directories(tempDir, ec);

    const fs::path tempBase = tempDir / "input.shp";
    if (fs::exists(tempBase)) {
        return tempBase.string();
    }

    const std::vector<std::wstring> sidecarExts = {
        L".shp", L".shx", L".dbf", L".prj", L".cpg", L".qix", L".fix", L".sbn", L".sbx"
    };

    auto copyOrLinkFile = [](const fs::path& src, const fs::path& dst) {
        std::error_code localEc;
        fs::remove(dst, localEc);
        localEc.clear();
        fs::create_hard_link(src, dst, localEc);
        if (!localEc) {
            return true;
        }

        localEc.clear();
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, localEc);
        return !localEc;
    };

    for (const auto& ext : sidecarExts) {
        fs::path src = sourceFsPath;
        src.replace_extension(ext);
        if (!fs::exists(src)) {
            continue;
        }

        fs::path dst = tempBase;
        dst.replace_extension(ext);
        if (!copyOrLinkFile(src, dst)) {
            return sourcePath;
        }
    }

    if (!fs::exists(tempBase)) {
        return sourcePath;
    }

    return tempBase.string();
}
#endif

std::string resolveVectorInputPath(const std::string& path) {
#ifdef _WIN32
    if (containsNonAscii(path) && isShapefilePath(path)) {
        return makeAsciiShapefileMirror(path);
    }
#endif
    return path;
}

} // namespace

std::vector<gis::framework::ParamSpec> VectorPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"info", "filter", "buffer", "clip", "rasterize", "polygonize", "convert", "union", "difference", "dissolve", "simplify", "repair", "geom_metrics", "nearest", "adjacency", "overlap_check", "topology_check", "convex_hull", "centroid", "envelope", "boundary", "multipart_check"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入矢量/栅格文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出文件", "输出文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "layer", "图层名", "操作的图层名(默认第一个图层)",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "where", "属性过滤", "SQL WHERE 条件表达式，如 population > 10000",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "extent", "空间范围", "空间过滤范围(xmin,ymin,xmax,ymax)",
            gis::framework::ParamType::Extent, false, std::array<double,4>{0,0,0,0}
        },
        gis::framework::ParamSpec{
            "distance", "缓冲距离", "缓冲区分析的距离(地图单位)",
            gis::framework::ParamType::Double, false, double{100.0}
        },
        gis::framework::ParamSpec{
            "clip_vector", "裁切矢量", "用于裁切的矢量文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "resolution", "栅格分辨率", "矢量转栅格时的像元大小",
            gis::framework::ParamType::Double, false, double{1.0}
        },
        gis::framework::ParamSpec{
            "attribute", "属性字段", "栅格化时写入的属性字段名(不指定则写入 1)",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "band", "波段序号", "栅格转矢量时使用的波段序号(从 1 开始)",
            gis::framework::ParamType::Int, false, int{1}
        },
        gis::framework::ParamSpec{
            "format", "输出格式", "格式转换的目标格式(GeoJSON/ESRI Shapefile/GPKG/KML/CSV)",
            gis::framework::ParamType::Enum, false, std::string{"GeoJSON"},
            int{0}, int{0},
            {"GeoJSON", "ESRI Shapefile", "GPKG", "KML", "CSV"}
        },
        gis::framework::ParamSpec{
            "overlay_vector", "叠加矢量", "并集/差集操作的第二个矢量文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "nearest_vector", "目标图层", "最近邻分析使用的目标矢量文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "nearest_field", "目标字段", "可选，将最近邻要素的该字段值写入结果",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "dissolve_field", "融合字段", "融合操作按该字段值合并相邻多边形(不指定则全部合并)",
            gis::framework::ParamType::String, false, std::string{}
        },
        gis::framework::ParamSpec{
            "tolerance", "简化容差", "Douglas-Peucker 简化容差，单位与图层坐标单位一致",
            gis::framework::ParamType::Double, false, double{10.0}
        },
    };
}

gis::framework::Result VectorPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string action = gis::framework::getParam<std::string>(params, "action", "");

    if (action == "info")       return doInfo(params, progress);
    if (action == "filter")     return doFilter(params, progress);
    if (action == "buffer")     return doBuffer(params, progress);
    if (action == "clip")       return doClip(params, progress);
    if (action == "rasterize")  return doRasterize(params, progress);
    if (action == "polygonize") return doPolygonize(params, progress);
    if (action == "convert")    return doConvert(params, progress);
    if (action == "union")      return doUnion(params, progress);
    if (action == "difference") return doDifference(params, progress);
    if (action == "dissolve")   return doDissolve(params, progress);
    if (action == "simplify")   return doSimplify(params, progress);
    if (action == "repair")     return doRepair(params, progress);
    if (action == "geom_metrics") return doGeomMetrics(params, progress);
    if (action == "nearest")    return doNearest(params, progress);
    if (action == "adjacency")  return doAdjacency(params, progress);
    if (action == "overlap_check") return doOverlapCheck(params, progress);
    if (action == "topology_check") return doTopologyCheck(params, progress);
    if (action == "convex_hull") return doConvexHull(params, progress);
    if (action == "centroid")   return doCentroid(params, progress);
    if (action == "envelope")   return doEnvelope(params, progress);
    if (action == "boundary")   return doBoundary(params, progress);
    if (action == "multipart_check") return doMultipartCheck(params, progress);

    return gis::framework::Result::fail("Unknown action: " + action);
}

#include "vector_plugin_common.inc"

#include "vector_plugin_overlay_support.inc"

#include "vector_plugin_info_filter.inc"

#include "vector_plugin_buffer_clip.inc"

#include "vector_plugin_raster_convert.inc"

#include "vector_plugin_overlay.inc"

#include "vector_plugin_dissolve.inc"

#include "vector_plugin_simplify.inc"

#include "vector_plugin_repair.inc"

#include "vector_plugin_geom_metrics.inc"

#include "vector_plugin_nearest.inc"

#include "vector_plugin_adjacency.inc"

#include "vector_plugin_overlap_check.inc"

#include "vector_plugin_topology_check.inc"

#include "vector_plugin_convex_hull.inc"

#include "vector_plugin_centroid.inc"

#include "vector_plugin_envelope.inc"

#include "vector_plugin_boundary.inc"

#include "vector_plugin_multipart_check.inc"

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::VectorPlugin)

