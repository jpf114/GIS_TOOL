#include <gtest/gtest.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string>

#include "../src/gui/gui_data_support.h"
#include "test_support.h"

namespace fs = std::filesystem;

namespace {

struct DatasetCloser {
    void operator()(GDALDataset* ds) const {
        if (ds) {
            GDALClose(ds);
        }
    }
};

fs::path guiSupportTestDir() {
    return gis::tests::defaultTestOutputDir("test_gui_support_output");
}

std::string exportWktFromEpsg(int epsg) {
    OGRSpatialReference srs;
    srs.importFromEPSG(epsg);
    char* wkt = nullptr;
    srs.exportToWkt(&wkt);
    std::string result = wkt ? wkt : "";
    CPLFree(wkt);
    return result;
}

} // namespace

TEST(GuiSupportTest, DetectRasterDataKind) {
    EXPECT_EQ(gis::gui::detectDataKind("demo.TIF"), gis::gui::DataKind::Raster);
    EXPECT_EQ(gis::gui::detectDataKind("preview.jpeg"), gis::gui::DataKind::Raster);
}

TEST(GuiSupportTest, DetectVectorDataKind) {
    EXPECT_EQ(gis::gui::detectDataKind("roads.SHP"), gis::gui::DataKind::Vector);
    EXPECT_EQ(gis::gui::detectDataKind("china.geojson"), gis::gui::DataKind::Vector);
}

TEST(GuiSupportTest, DetectUnknownDataKind) {
    EXPECT_EQ(gis::gui::detectDataKind("notes.txt"), gis::gui::DataKind::Unknown);
    EXPECT_FALSE(gis::gui::canPreviewData("notes.txt"));
}

TEST(GuiSupportTest, DataKindDisplayNameIsChinese) {
    EXPECT_EQ(gis::gui::dataKindDisplayName(gis::gui::DataKind::Raster), "栅格");
    EXPECT_EQ(gis::gui::dataKindDisplayName(gis::gui::DataKind::Vector), "矢量");
    EXPECT_EQ(gis::gui::dataKindDisplayName(gis::gui::DataKind::Unknown), "未知");
}

TEST(GuiSupportTest, BuildDataDisplayLabelIncludesRoleAndName) {
    EXPECT_EQ(
        gis::gui::buildDataDisplayLabel("D:/data/image.tif", gis::gui::DataKind::Raster, false),
        "[栅格][输入] image.tif");
    EXPECT_EQ(
        gis::gui::buildDataDisplayLabel("D:/data/result.geojson", gis::gui::DataKind::Vector, true),
        "[矢量][输出] result.geojson");
}

TEST(GuiSupportTest, BuildDataDisplayLabelIncludesActiveState) {
    EXPECT_EQ(
        gis::gui::buildDataDisplayLabel("D:/data/image.tif", gis::gui::DataKind::Raster, false, true),
        "[栅格][输入][当前] image.tif");
    EXPECT_EQ(
        gis::gui::buildDataDisplayLabel("D:/data/result.geojson", gis::gui::DataKind::Vector, true, false),
        "[矢量][输出] result.geojson");
}

TEST(GuiSupportTest, BuildPreviewStatusTextShowsKindScaleAndMode) {
    EXPECT_EQ(
        gis::gui::buildPreviewStatusText(gis::gui::DataKind::Raster, 1.25, true, false),
        "当前预览: 栅格 | 缩放: 125% | 模式: 适配视图");
    EXPECT_EQ(
        gis::gui::buildPreviewStatusText(gis::gui::DataKind::Raster, 2.0, false, true),
        "当前预览: 栅格 | 缩放: 200% | 模式: 拖拽浏览");
    EXPECT_EQ(
        gis::gui::buildPreviewStatusText(gis::gui::DataKind::Vector, 1.0, false, false),
        "当前预览: 矢量 | 缩放: 100% | 模式: 手动缩放");
    EXPECT_EQ(
        gis::gui::buildPreviewStatusText(gis::gui::DataKind::Vector, 1.0, true, false),
        "当前预览: 矢量 | 缩放: 100% | 模式: 适配视图");
}

TEST(GuiSupportTest, ZoomScaleIsClampedAndStepped) {
    EXPECT_DOUBLE_EQ(gis::gui::zoomInScale(1.0), 1.25);
    EXPECT_DOUBLE_EQ(gis::gui::zoomOutScale(1.0), 0.8);
    EXPECT_DOUBLE_EQ(gis::gui::zoomInScale(8.0), 8.0);
    EXPECT_DOUBLE_EQ(gis::gui::zoomOutScale(0.1), 0.1);
}

TEST(GuiSupportTest, FitScaleUsesViewportBounds) {
    EXPECT_DOUBLE_EQ(gis::gui::fitScaleForSize(800, 400, 400, 200), 0.5);
    EXPECT_DOUBLE_EQ(gis::gui::fitScaleForSize(400, 800, 200, 400), 0.5);
    EXPECT_DOUBLE_EQ(gis::gui::fitScaleForSize(200, 100, 800, 600), 1.0);
}

TEST(GuiSupportTest, BuildSuggestedOutputPathUsesPluginAndActionSuffix) {
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "processing", "threshold"),
        "D:/data/image_processing_threshold.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/roads.shp", "vector", "buffer"),
        "D:/data/roads_vector_buffer.shp");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/scene.tif", "", ""),
        "D:/data/scene_result.tif");
}

TEST(GuiSupportTest, InspectRasterAutoFillInfoReadsCrsAndExtent) {
    GDALAllRegister();
    gis::tests::ensureDirectory(guiSupportTestDir());

    const fs::path rasterPath = guiSupportTestDir() / "autofill_raster.tif";
    auto* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    ASSERT_NE(driver, nullptr);

    std::unique_ptr<GDALDataset, DatasetCloser> ds(
        driver->Create(rasterPath.string().c_str(), 20, 10, 1, GDT_Byte, nullptr));
    ASSERT_NE(ds, nullptr);

    const double geotransform[6] = {100.0, 2.0, 0.0, 50.0, 0.0, -3.0};
    ASSERT_EQ(ds->SetGeoTransform(const_cast<double*>(geotransform)), CE_None);
    const std::string wkt = exportWktFromEpsg(3857);
    ASSERT_EQ(ds->SetProjection(wkt.c_str()), CE_None);
    ds.reset();

    const auto info = gis::gui::inspectDataForAutoFill(rasterPath.string());
    EXPECT_EQ(info.crs, std::string("EPSG:3857"));
    EXPECT_TRUE(info.hasExtent);
    EXPECT_EQ(info.layerName, std::string());
    EXPECT_TRUE(info.extent == (std::array<double, 4>{100.0, 20.0, 140.0, 50.0}));
}

TEST(GuiSupportTest, InspectVectorAutoFillInfoReadsLayerCrsAndExtent) {
    GDALAllRegister();
    gis::tests::ensureDirectory(guiSupportTestDir());

    const fs::path vectorPath = guiSupportTestDir() / "autofill_vector.gpkg";
    auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    ASSERT_NE(driver, nullptr);

    std::unique_ptr<GDALDataset, DatasetCloser> ds(
        driver->Create(vectorPath.string().c_str(), 0, 0, 0, GDT_Unknown, nullptr));
    ASSERT_NE(ds, nullptr);

    OGRSpatialReference srs;
    srs.importFromEPSG(4326);
    auto* layer = ds->CreateLayer("roads", &srs, wkbLineString, nullptr);
    ASSERT_NE(layer, nullptr);

    OGRFeatureDefn* defn = layer->GetLayerDefn();
    std::unique_ptr<OGRFeature> feature(OGRFeature::CreateFeature(defn));
    auto geometry = std::make_unique<OGRLineString>();
    geometry->addPoint(120.0, 30.0);
    geometry->addPoint(121.5, 31.5);
    ASSERT_EQ(feature->SetGeometry(geometry.get()), OGRERR_NONE);
    ASSERT_EQ(layer->CreateFeature(feature.get()), OGRERR_NONE);
    ds.reset();

    const auto info = gis::gui::inspectDataForAutoFill(vectorPath.string());
    EXPECT_EQ(info.crs, std::string("EPSG:4326"));
    EXPECT_TRUE(info.hasExtent);
    EXPECT_EQ(info.layerName, std::string("roads"));
    EXPECT_TRUE(info.extent == (std::array<double, 4>{120.0, 30.0, 121.5, 31.5}));
}

TEST(GuiSupportTest, BuildResultSummaryTextUsesChineseLabels) {
    gis::framework::Result result;
    result.success = true;
    result.message = "处理完成";
    result.outputPath = "D:/data/out.tif";
    result.metadata["rows"] = "256";
    result.metadata["cols"] = "512";

    const std::string summary = gis::gui::buildResultSummaryText(result);
    EXPECT_NE(summary.find("状态: 成功"), std::string::npos);
    EXPECT_NE(summary.find("消息: 处理完成"), std::string::npos);
    EXPECT_NE(summary.find("输出: D:/data/out.tif"), std::string::npos);
    EXPECT_NE(summary.find("元数据:"), std::string::npos);
    EXPECT_NE(summary.find("- rows: 256"), std::string::npos);
    EXPECT_NE(summary.find("- cols: 512"), std::string::npos);
}

TEST(GuiSupportTest, ValidateExecutionParamsRejectsMissingRequiredPath) {
    std::vector<gis::framework::ParamSpec> specs;
    specs.push_back(gis::framework::ParamSpec{
        "input", "输入文件", "", gis::framework::ParamType::FilePath, true
    });

    std::map<std::string, gis::framework::ParamValue> params;
    params["input"] = std::string();

    EXPECT_EQ(
        gis::gui::validateExecutionParams(specs, params),
        "参数“输入文件”不能为空");
}

TEST(GuiSupportTest, ValidateExecutionParamsRejectsOutOfRangeNumber) {
    gis::framework::ParamSpec spec{
        "threshold", "阈值", "", gis::framework::ParamType::Double, true
    };
    spec.minValue = 0.0;
    spec.maxValue = 1.0;

    std::vector<gis::framework::ParamSpec> specs{spec};
    std::map<std::string, gis::framework::ParamValue> params;
    params["threshold"] = 2.5;

    EXPECT_EQ(
        gis::gui::validateExecutionParams(specs, params),
        "参数“阈值”超出范围 [0, 1]");
}

TEST(GuiSupportTest, ValidateExecutionParamsAcceptsValidValues) {
    gis::framework::ParamSpec inputSpec{
        "input", "输入文件", "", gis::framework::ParamType::FilePath, true
    };
    gis::framework::ParamSpec thresholdSpec{
        "threshold", "阈值", "", gis::framework::ParamType::Double, true
    };
    thresholdSpec.minValue = 0.0;
    thresholdSpec.maxValue = 1.0;

    std::vector<gis::framework::ParamSpec> specs{inputSpec, thresholdSpec};
    std::map<std::string, gis::framework::ParamValue> params;
    params["input"] = std::string("D:/data/image.tif");
    params["threshold"] = 0.75;

    EXPECT_TRUE(gis::gui::validateExecutionParams(specs, params).empty());
}

TEST(GuiSupportTest, FindFirstInvalidParamKeyReturnsMissingRequiredField) {
    std::vector<gis::framework::ParamSpec> specs;
    specs.push_back(gis::framework::ParamSpec{
        "input", "输入文件", "", gis::framework::ParamType::FilePath, true
    });
    specs.push_back(gis::framework::ParamSpec{
        "output", "输出文件", "", gis::framework::ParamType::FilePath, true
    });

    std::map<std::string, gis::framework::ParamValue> params;
    params["input"] = std::string("D:/data/image.tif");
    params["output"] = std::string();

    EXPECT_EQ(
        gis::gui::findFirstInvalidParamKey(specs, params),
        "output");
}

TEST(GuiSupportTest, CollectBindableParamOptionsFiltersByTypeRoleAndKind) {
    std::vector<gis::framework::ParamSpec> specs = {
        {"input", "输入文件", "", gis::framework::ParamType::FilePath, true},
        {"output", "输出文件", "", gis::framework::ParamType::FilePath, true},
        {"reference", "参考影像", "", gis::framework::ParamType::FilePath, true},
        {"clip_vector", "裁切矢量", "", gis::framework::ParamType::FilePath, true},
        {"threshold", "阈值", "", gis::framework::ParamType::Double, true}
    };

    const auto rasterOptions = gis::gui::collectBindableParamOptions(specs, gis::gui::DataKind::Raster);
    ASSERT_EQ(rasterOptions.size(), 1);
    EXPECT_EQ(rasterOptions[0].key, "reference");
    EXPECT_EQ(rasterOptions[0].displayName, "参考影像");

    const auto vectorOptions = gis::gui::collectBindableParamOptions(specs, gis::gui::DataKind::Vector);
    ASSERT_EQ(vectorOptions.size(), 1);
    EXPECT_EQ(vectorOptions[0].key, "clip_vector");
    EXPECT_EQ(vectorOptions[0].displayName, "裁切矢量");
}

TEST(GuiSupportTest, CollectBindableParamOptionsKeepsGenericFileParams) {
    std::vector<gis::framework::ParamSpec> specs = {
        {"template_file", "模板文件", "", gis::framework::ParamType::FilePath, true},
        {"pan_file", "全色影像", "", gis::framework::ParamType::FilePath, true}
    };

    const auto rasterOptions = gis::gui::collectBindableParamOptions(specs, gis::gui::DataKind::Raster);
    ASSERT_EQ(rasterOptions.size(), 2);
    EXPECT_EQ(rasterOptions[0].key, "template_file");
    EXPECT_EQ(rasterOptions[1].key, "pan_file");
}
