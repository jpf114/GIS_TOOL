#include "projection_plugin.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/error.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>
#include <gdal_utils.h>
#include <sstream>
#include <cmath>
#include <iomanip>
#include <memory>

namespace gis::plugins {

std::vector<gis::framework::ParamSpec> ProjectionPlugin::paramSpecs() const {
    return {
        gis::framework::ParamSpec{
            "action", "子功能", "选择要执行的子功能",
            gis::framework::ParamType::Enum, true, std::string{},
            int{0}, int{0},
            {"reproject", "info", "transform", "assign_srs"}
        },
        gis::framework::ParamSpec{
            "input", "输入文件", "输入影像文件路径",
            gis::framework::ParamType::FilePath, true, std::string{}
        },
        gis::framework::ParamSpec{
            "output", "输出文件", "输出影像文件路径",
            gis::framework::ParamType::FilePath, false, std::string{}
        },
        gis::framework::ParamSpec{
            "dst_srs", "目标坐标系", "目标坐标系(EPSG代号或WKT字符串)",
            gis::framework::ParamType::CRS, false, std::string{}
        },
        gis::framework::ParamSpec{
            "src_srs", "源坐标系", "源坐标系(EPSG代号或WKT字符串)，覆盖影像自带坐标系",
            gis::framework::ParamType::CRS, false, std::string{}
        },
        gis::framework::ParamSpec{
            "resample", "重采样方法", "重采样算法",
            gis::framework::ParamType::Enum, false, std::string{"nearest"},
            int{0}, int{0},
            {"nearest", "bilinear", "cubic", "cubicspline", "lanczos", "average", "mode"}
        },
        gis::framework::ParamSpec{
            "x", "X坐标", "待转换的X坐标(经度)",
            gis::framework::ParamType::Double, false, double{0}
        },
        gis::framework::ParamSpec{
            "y", "Y坐标", "待转换的Y坐标(纬度)",
            gis::framework::ParamType::Double, false, double{0}
        },
        gis::framework::ParamSpec{
            "srs", "坐标系", "指定坐标系(EPSG代号或WKT字符串)，用于assign_srs",
            gis::framework::ParamType::CRS, false, std::string{}
        },
    };
}

gis::framework::Result ProjectionPlugin::execute(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string action = gis::framework::getParam<std::string>(params, "action", "");

    if (action == "reproject")  return doReproject(params, progress);
    if (action == "info")       return doInfo(params, progress);
    if (action == "transform")  return doTransform(params, progress);
    if (action == "assign_srs") return doAssignSRS(params, progress);

    return gis::framework::Result::fail("Unknown action: " + action);
}

gis::framework::Result ProjectionPlugin::doReproject(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input  = gis::framework::getParam<std::string>(params, "input", "");
    std::string output = gis::framework::getParam<std::string>(params, "output", "");
    std::string dstSrs = gis::framework::getParam<std::string>(params, "dst_srs", "");
    std::string srcSrs = gis::framework::getParam<std::string>(params, "src_srs", "");
    std::string resample = gis::framework::getParam<std::string>(params, "resample", "nearest");

    if (input.empty())  return gis::framework::Result::fail("input is required");
    if (output.empty()) return gis::framework::Result::fail("output is required");
    if (dstSrs.empty()) return gis::framework::Result::fail("dst_srs is required");

    progress.onMessage("Opening source dataset: " + input);
    progress.onProgress(0.0);

    auto srcDS = gis::core::openRaster(input, true);

    progress.onMessage("Setting up warp options...");

    std::vector<std::string> argStorage;
    argStorage.push_back("-t_srs");
    argStorage.push_back(dstSrs);

    if (!srcSrs.empty()) {
        argStorage.push_back("-s_srs");
        argStorage.push_back(srcSrs);
    }

    argStorage.push_back("-r");
    argStorage.push_back(resample);

    std::vector<const char*> warpArgs;
    for (auto& a : argStorage) {
        warpArgs.push_back(a.c_str());
    }
    warpArgs.push_back(nullptr);

    GDALWarpAppOptions* warpOpts = GDALWarpAppOptionsNew(
        const_cast<char**>(warpArgs.data()), nullptr);
    if (!warpOpts) {
        return gis::framework::Result::fail("Failed to create warp options: " + std::string(CPLGetLastErrorMsg()));
    }

    progress.onProgress(0.2);
    progress.onMessage("Reprojecting...");

    GDALDatasetH srcHandle = static_cast<GDALDatasetH>(srcDS.get());
    int errCode = 0;
    GDALDatasetH dstHandle = GDALWarp(output.c_str(), nullptr,
        1, &srcHandle, warpOpts, &errCode);

    GDALWarpAppOptionsFree(warpOpts);

    if (!dstHandle || errCode) {
        return gis::framework::Result::fail(
            "Reprojection failed: " + std::string(CPLGetLastErrorMsg()));
    }

    GDALClose(dstHandle);
    progress.onProgress(1.0);
    progress.onMessage("Reprojection completed.");

    return gis::framework::Result::ok("Reprojection completed successfully", output);
}

gis::framework::Result ProjectionPlugin::doInfo(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    if (input.empty()) return gis::framework::Result::fail("input is required");

    auto ds = gis::core::openRaster(input, true);
    progress.onProgress(0.3);

    std::ostringstream oss;
    oss << "File: " << input << "\n";
    oss << "Size: " << ds->GetRasterXSize() << " x " << ds->GetRasterYSize()
        << " x " << ds->GetRasterCount() << " bands\n";

    double adfGT[6];
    if (ds->GetGeoTransform(adfGT) == CE_None) {
        oss << "GeoTransform:\n";
        oss << "  Origin: (" << adfGT[0] << ", " << adfGT[3] << ")\n";
        oss << "  Pixel Size: (" << adfGT[1] << ", " << adfGT[5] << ")\n";
        oss << "  Rotation: (" << adfGT[2] << ", " << adfGT[4] << ")\n";
    } else {
        oss << "GeoTransform: (none)\n";
    }

    std::string wkt = gis::core::getSRSWKT(ds.get());
    if (!wkt.empty()) {
        OGRSpatialReference srs;
        char* wktC = const_cast<char*>(wkt.c_str());
        if (srs.importFromWkt(&wktC) == OGRERR_NONE) {
            char* prettyWkt = nullptr;
            srs.exportToPrettyWkt(&prettyWkt, false);
            if (prettyWkt) {
                oss << "CRS:\n" << prettyWkt << "\n";
                CPLFree(prettyWkt);
            }
            const char* authName = srs.GetAuthorityName(nullptr);
            const char* authCode = srs.GetAuthorityCode(nullptr);
            if (authName && authCode) {
                oss << "Authority: " << authName << ":" << authCode << "\n";
            }
        }
    } else {
        oss << "CRS: (none)\n";
    }

    progress.onProgress(1.0);

    auto result = gis::framework::Result::ok(oss.str());
    result.metadata["width"]  = std::to_string(ds->GetRasterXSize());
    result.metadata["height"] = std::to_string(ds->GetRasterYSize());
    result.metadata["bands"]  = std::to_string(ds->GetRasterCount());
    result.metadata["wkt"]    = wkt;
    return result;
}

gis::framework::Result ProjectionPlugin::doTransform(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string srcSrs = gis::framework::getParam<std::string>(params, "src_srs", "EPSG:4326");
    std::string dstSrs = gis::framework::getParam<std::string>(params, "dst_srs", "");
    double x = gis::framework::getParam<double>(params, "x", 0.0);
    double y = gis::framework::getParam<double>(params, "y", 0.0);

    if (dstSrs.empty()) return gis::framework::Result::fail("dst_srs is required for transform");

    progress.onProgress(0.2);

    auto* srcSRS = gis::core::parseSRS(srcSrs);
    auto* dstSRS = gis::core::parseSRS(dstSrs);

    auto ct = std::unique_ptr<OGRCoordinateTransformation>(
        OGRCreateCoordinateTransformation(srcSRS, dstSRS));
    delete srcSRS;
    delete dstSRS;

    if (!ct) {
        return gis::framework::Result::fail("Failed to create coordinate transformation");
    }

    double tx = x, ty = y;
    if (!ct->Transform(1, &tx, &ty)) {
        return gis::framework::Result::fail("Coordinate transformation failed");
    }

    progress.onProgress(1.0);

    std::ostringstream oss;
    oss << std::setprecision(10) << "(" << x << ", " << y << ") -> (" << tx << ", " << ty << ")";

    auto result = gis::framework::Result::ok(oss.str());
    result.metadata["src_x"] = std::to_string(x);
    result.metadata["src_y"] = std::to_string(y);
    result.metadata["dst_x"] = std::to_string(tx);
    result.metadata["dst_y"] = std::to_string(ty);
    return result;
}

gis::framework::Result ProjectionPlugin::doAssignSRS(
    const std::map<std::string, gis::framework::ParamValue>& params,
    gis::core::ProgressReporter& progress) {

    std::string input = gis::framework::getParam<std::string>(params, "input", "");
    std::string srs   = gis::framework::getParam<std::string>(params, "srs", "");

    if (input.empty()) return gis::framework::Result::fail("input is required");
    if (srs.empty())   return gis::framework::Result::fail("srs is required for assign_srs");

    progress.onProgress(0.2);

    auto* srsObj = gis::core::parseSRS(srs);
    char* wktOut = nullptr;
    srsObj->exportToWkt(&wktOut);
    delete srsObj;

    if (!wktOut) {
        return gis::framework::Result::fail("Failed to export SRS to WKT");
    }

    progress.onProgress(0.4);

    auto ds = gis::core::openRaster(input, false);
    if (ds->SetProjection(wktOut) != CE_None) {
        CPLFree(wktOut);
        return gis::framework::Result::fail("Failed to set projection: " + std::string(CPLGetLastErrorMsg()));
    }

    ds.reset();
    CPLFree(wktOut);

    progress.onProgress(1.0);
    return gis::framework::Result::ok("SRS assigned successfully", input);
}

} // namespace gis::plugins

GIS_PLUGIN_EXPORT(gis::plugins::ProjectionPlugin)
