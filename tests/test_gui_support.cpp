#include <gtest/gtest.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
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

TEST(GuiSupportTest, IsSupportedDataPathRecognizesSupportedExtensions) {
    EXPECT_TRUE(gis::gui::isSupportedDataPath("scene.tif"));
    EXPECT_TRUE(gis::gui::isSupportedDataPath("roads.gpkg"));
    EXPECT_FALSE(gis::gui::isSupportedDataPath("report.docx"));
}

TEST(GuiSupportTest, CollectSupportedDataPathsFiltersUnsupportedEntries) {
    const std::vector<std::string> paths = {
        "D:/data/scene.tif",
        "D:/data/readme.txt",
        "D:/data/roads.shp",
        "D:/data/archive.zip"
    };

    const auto supported = gis::gui::collectSupportedDataPaths(paths);
    ASSERT_EQ(supported.size(), 2u);
    EXPECT_EQ(supported[0], "D:/data/scene.tif");
    EXPECT_EQ(supported[1], "D:/data/roads.shp");
}

TEST(GuiSupportTest, CollectSupportedDataPathsRecursivelyFindsFilesInNestedDirectories) {
    gis::tests::ensureDirectory(guiSupportTestDir());

    const fs::path rootDir = guiSupportTestDir() / "recursive_import";
    const fs::path nestedDir = rootDir / "nested";
    gis::tests::ensureDirectory(nestedDir);

    const fs::path rasterPath = rootDir / "scene.tif";
    const fs::path vectorPath = nestedDir / "roads.shp";
    const fs::path textPath = nestedDir / "notes.txt";

    {
        std::ofstream(rasterPath.string()).put('\n');
        std::ofstream(vectorPath.string()).put('\n');
        std::ofstream(textPath.string()).put('\n');
    }

    const auto supported = gis::gui::collectSupportedDataPathsRecursively({rootDir.string()});
    ASSERT_EQ(supported.size(), 2u);
    EXPECT_NE(
        std::find(supported.begin(), supported.end(),
                  (rootDir / "scene.tif").lexically_normal().generic_string()),
        supported.end());
    EXPECT_NE(
        std::find(supported.begin(), supported.end(),
                  (nestedDir / "roads.shp").lexically_normal().generic_string()),
        supported.end());
}

TEST(GuiSupportTest, DataKindDisplayNameIsChinese) {
    EXPECT_EQ(gis::gui::dataKindDisplayName(gis::gui::DataKind::Raster), "栅格");
    EXPECT_EQ(gis::gui::dataKindDisplayName(gis::gui::DataKind::Vector), "矢量");
    EXPECT_EQ(gis::gui::dataKindDisplayName(gis::gui::DataKind::Unknown), "未知");
}

TEST(GuiSupportTest, DataOriginDisplayNameIsChinese) {
    EXPECT_EQ(gis::gui::dataOriginDisplayName(gis::gui::DataOrigin::Input), "输入");
    EXPECT_EQ(gis::gui::dataOriginDisplayName(gis::gui::DataOrigin::Output), "输出结果");
}

TEST(GuiSupportTest, OutputDataOriginIncludesOutputOnly) {
    EXPECT_FALSE(gis::gui::isOutputDataOrigin(gis::gui::DataOrigin::Input));
    EXPECT_TRUE(gis::gui::isOutputDataOrigin(gis::gui::DataOrigin::Output));
}

TEST(GuiSupportTest, BuildDataDisplayLabelIncludesRoleAndName) {
    EXPECT_EQ(
        gis::gui::buildDataDisplayLabel(
            "D:/data/image.tif", gis::gui::DataKind::Raster, gis::gui::DataOrigin::Input),
        "[栅格][输入] image.tif");
    EXPECT_EQ(
        gis::gui::buildDataDisplayLabel(
            "D:/data/result.geojson", gis::gui::DataKind::Vector, gis::gui::DataOrigin::Output),
        "[矢量][输出结果] result.geojson");
}

TEST(GuiSupportTest, BuildDataDisplayLabelIncludesActiveState) {
    EXPECT_EQ(
        gis::gui::buildDataDisplayLabel(
            "D:/data/image.tif", gis::gui::DataKind::Raster, gis::gui::DataOrigin::Input, true),
        "[栅格][输入][当前] image.tif");
    EXPECT_EQ(
        gis::gui::buildDataDisplayLabel(
            "D:/data/result.geojson", gis::gui::DataKind::Vector, gis::gui::DataOrigin::Output, false),
        "[矢量][输出结果] result.geojson");
}

TEST(GuiSupportTest, BuildSuggestedOutputPathUsesPluginAndActionSuffix) {
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "processing", "threshold"),
        "D:/data/image_processing_threshold.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/roads.shp", "vector", "buffer"),
        "D:/data/roads_vector_buffer.gpkg");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/scene.tif", "", ""),
        "D:/data/scene_result.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/a.tif, D:/data/b.tif", "cutting", "mosaic"),
        "D:/data/a_cutting_mosaic.tif");
}

TEST(GuiSupportTest, BuildSuggestedOutputPathUsesActionSpecificSuffixes) {
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "matching", "detect"),
        "D:/data/image_matching_detect.json");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "utility", "histogram"),
        "D:/data/image_utility_histogram.json");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/scene.tif", "classification", "feature_stats", "vector_output"),
        "D:/data/scene_classification_feature_stats.gpkg");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/scene.tif", "classification", "feature_stats", "raster_output"),
        "D:/data/scene_classification_feature_stats.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/scene.tif", "cutting", "split"),
        "D:/data/scene_cutting_split");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/roads.shp", "vector", "convert"),
        "D:/data/roads_vector_convert.geojson");
}

TEST(GuiSupportTest, BuildFileParamUiConfigProvidesSpecializedHints) {
    const auto classMapConfig = gis::gui::buildFileParamUiConfig(
        "classification", "feature_stats", "class_map", gis::framework::ParamType::FilePath);
    EXPECT_NE(classMapConfig.openFilter.find("*.json"), std::string::npos);
    EXPECT_NE(classMapConfig.placeholder.find("JSON"), std::string::npos);

    const auto splitOutputConfig = gis::gui::buildFileParamUiConfig(
        "cutting", "split", "output", gis::framework::ParamType::FilePath);
    EXPECT_TRUE(splitOutputConfig.selectDirectory);
    EXPECT_TRUE(splitOutputConfig.isOutput);

    const auto detectOutputConfig = gis::gui::buildFileParamUiConfig(
        "matching", "detect", "output", gis::framework::ParamType::FilePath);
    EXPECT_EQ(detectOutputConfig.suggestedSuffix, ".json");
    EXPECT_NE(detectOutputConfig.saveFilter.find("*.json"), std::string::npos);

    const auto statsOutputConfig = gis::gui::buildFileParamUiConfig(
        "classification", "feature_stats", "output", gis::framework::ParamType::FilePath);
    EXPECT_NE(statsOutputConfig.saveFilter.find("*.json"), std::string::npos);
    EXPECT_NE(statsOutputConfig.saveFilter.find("*.csv"), std::string::npos);
    EXPECT_NE(statsOutputConfig.placeholder.find(".csv"), std::string::npos);

    const auto classVectorOutputConfig = gis::gui::buildFileParamUiConfig(
        "classification", "feature_stats", "vector_output", gis::framework::ParamType::FilePath);
    EXPECT_NE(classVectorOutputConfig.saveFilter.find("*.gpkg"), std::string::npos);
    EXPECT_EQ(classVectorOutputConfig.saveFilter.find("*.shp"), std::string::npos);
    EXPECT_EQ(classVectorOutputConfig.saveFilter.find("*.csv"), std::string::npos);

    const auto projectionInputConfig = gis::gui::buildFileParamUiConfig(
        "projection", "reproject", "input", gis::framework::ParamType::FilePath);
    EXPECT_NE(projectionInputConfig.openFilter.find("*.tif"), std::string::npos);
    EXPECT_NE(projectionInputConfig.openFilter.find("*.gpkg"), std::string::npos);

    const auto splitDirConfig = gis::gui::buildFileParamUiConfig(
        "cutting", "split", "output", gis::framework::ParamType::FilePath);
    EXPECT_NE(splitDirConfig.placeholder.find("tile_x_y.tif"), std::string::npos);

    const auto mosaicInputConfig = gis::gui::buildFileParamUiConfig(
        "cutting", "mosaic", "input", gis::framework::ParamType::FilePath);
    EXPECT_TRUE(mosaicInputConfig.allowMultiSelect);

    const auto stitchInputConfig = gis::gui::buildFileParamUiConfig(
        "matching", "stitch", "input", gis::framework::ParamType::FilePath);
    EXPECT_TRUE(stitchInputConfig.allowMultiSelect);

    const auto vectorFilterOutputConfig = gis::gui::buildFileParamUiConfig(
        "vector", "filter", "output", gis::framework::ParamType::FilePath);
    EXPECT_NE(vectorFilterOutputConfig.saveFilter.find("*.gpkg"), std::string::npos);
    EXPECT_NE(vectorFilterOutputConfig.saveFilter.find("*.kml"), std::string::npos);
    EXPECT_EQ(vectorFilterOutputConfig.saveFilter.find("*.csv"), std::string::npos);

    const auto polygonizeOutputConfig = gis::gui::buildFileParamUiConfig(
        "vector", "polygonize", "output", gis::framework::ParamType::FilePath);
    EXPECT_NE(polygonizeOutputConfig.saveFilter.find("*.shp"), std::string::npos);
    EXPECT_EQ(polygonizeOutputConfig.saveFilter.find("*.kml"), std::string::npos);

    const auto unionOutputConfig = gis::gui::buildFileParamUiConfig(
        "vector", "union", "output", gis::framework::ParamType::FilePath);
    EXPECT_NE(unionOutputConfig.saveFilter.find("*.gpkg"), std::string::npos);
    EXPECT_EQ(unionOutputConfig.saveFilter.find("*.kml"), std::string::npos);
    EXPECT_EQ(unionOutputConfig.saveFilter.find("*.csv"), std::string::npos);
}

TEST(GuiSupportTest, BuildTextParamPlaceholderProvidesFormatExamples) {
    gis::framework::ParamSpec rastersSpec{
        "rasters", "分类栅格列表", "", gis::framework::ParamType::String, true
    };
    EXPECT_NE(
        gis::gui::buildTextParamPlaceholder("classification", "feature_stats", rastersSpec)
            .find("D:/a.tif,D:/b.tif"),
        std::string::npos);

    gis::framework::ParamSpec nodatasSpec{
        "nodatas", "NoData 列表", "", gis::framework::ParamType::String, false
    };
    EXPECT_NE(
        gis::gui::buildTextParamPlaceholder("classification", "feature_stats", nodatasSpec)
            .find("0,0,255"),
        std::string::npos);

    gis::framework::ParamSpec whereSpec{
        "where", "属性过滤", "", gis::framework::ParamType::String, false
    };
    EXPECT_NE(
        gis::gui::buildTextParamPlaceholder("vector", "filter", whereSpec)
            .find("population > 10000"),
        std::string::npos);
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

TEST(GuiSupportTest, InspectRasterAutoFillInfoUsesFirstPathFromMultiInputString) {
    GDALAllRegister();
    gis::tests::ensureDirectory(guiSupportTestDir());

    const fs::path rasterPath = guiSupportTestDir() / "autofill_multi_raster.tif";
    auto* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    ASSERT_NE(driver, nullptr);

    std::unique_ptr<GDALDataset, DatasetCloser> ds(
        driver->Create(rasterPath.string().c_str(), 12, 8, 1, GDT_Byte, nullptr));
    ASSERT_NE(ds, nullptr);

    const double geotransform[6] = {10.0, 0.5, 0.0, 20.0, 0.0, -0.5};
    ASSERT_EQ(ds->SetGeoTransform(const_cast<double*>(geotransform)), CE_None);
    const std::string wkt = exportWktFromEpsg(4326);
    ASSERT_EQ(ds->SetProjection(wkt.c_str()), CE_None);
    ds.reset();

    const auto info = gis::gui::inspectDataForAutoFill(
        rasterPath.string() + ", D:/data/another.tif");
    EXPECT_EQ(info.crs, std::string("EPSG:4326"));
    EXPECT_TRUE(info.hasExtent);
    EXPECT_TRUE(info.extent == (std::array<double, 4>{10.0, 16.0, 16.0, 20.0}));
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


