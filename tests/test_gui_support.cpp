#include <gtest/gtest.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include "../src/gui/custom_index_preset_store.h"
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

void setCustomIndexPresetFileForTest(const fs::path& path) {
#ifdef _WIN32
    _putenv_s("GIS_CUSTOM_INDEX_PRESET_FILE", path.string().c_str());
#else
    setenv("GIS_CUSTOM_INDEX_PRESET_FILE", path.string().c_str(), 1);
#endif
}

void clearCustomIndexPresetFileForTest() {
#ifdef _WIN32
    _putenv_s("GIS_CUSTOM_INDEX_PRESET_FILE", "");
#else
    unsetenv("GIS_CUSTOM_INDEX_PRESET_FILE");
#endif
}

struct CustomIndexPresetFileGuard {
    explicit CustomIndexPresetFileGuard(const fs::path& path) : path_(path) {
        setCustomIndexPresetFileForTest(path_);
    }

    ~CustomIndexPresetFileGuard() {
        clearCustomIndexPresetFileForTest();
    }

    fs::path path_;
};

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
            "D:/data/image.tif", "raster_inspect", "histogram"),
        "D:/data/image_raster_inspect_histogram.json");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "raster_render", "colormap"),
        "D:/data/image_raster_render_colormap.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "spindex", "ndvi"),
        "D:/data/image_spindex_ndvi.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "spindex", "ndwi"),
        "D:/data/image_spindex_ndwi.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "spindex", "custom_index"),
        "D:/data/image_spindex_custom_index.tif");
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
            "D:/data/dem.tif", "terrain", "slope"),
        "D:/data/dem_terrain_slope.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/dem.tif", "terrain", "profile_extract"),
        "D:/data/dem_terrain_profile_extract.csv");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/scene.tif", "cutting", "split"),
        "D:/data/scene_cutting_split");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/roads.shp", "vector", "convert"),
        "D:/data/roads_vector_convert.geojson");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/parcels.gpkg", "vector", "adjacency"),
        "D:/data/parcels_vector_adjacency.csv");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/parcels.gpkg", "vector", "overlap_check"),
        "D:/data/parcels_vector_overlap_check.csv");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/parcels.gpkg", "vector", "topology_check"),
        "D:/data/parcels_vector_topology_check.csv");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/parcels.gpkg", "vector", "multipart_check"),
        "D:/data/parcels_vector_multipart_check.csv");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/points.gpkg", "vector", "duplicate_point_check"),
        "D:/data/points_vector_duplicate_point_check.csv");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/parcels.gpkg", "vector", "hole_check"),
        "D:/data/parcels_vector_hole_check.csv");
}

TEST(GuiSupportTest, ComputeDerivedOutputUpdateAppliesSuggestedValueForEmptyOutput) {
    const auto update = gis::gui::computeDerivedOutputUpdate(
        "", "", "D:/data/roads.shp", "vector", "buffer");
    EXPECT_TRUE(update.shouldApply);
    EXPECT_EQ(update.value, "D:/data/roads_vector_buffer.gpkg");
    EXPECT_EQ(update.autoValue, "D:/data/roads_vector_buffer.gpkg");
}

TEST(GuiSupportTest, ComputeDerivedOutputUpdateRespectsManualOutputOverride) {
    const auto update = gis::gui::computeDerivedOutputUpdate(
        "D:/manual/custom.geojson",
        "D:/data/roads_vector_buffer.gpkg",
        "D:/data/roads.shp",
        "vector",
        "buffer");
    EXPECT_FALSE(update.shouldApply);
    EXPECT_EQ(update.value, "D:/data/roads_vector_buffer.gpkg");
    EXPECT_EQ(update.autoValue, "D:/data/roads_vector_buffer.gpkg");
}

TEST(GuiSupportTest, ComputeDerivedOutputUpdateAdjustsVectorConvertSuffixFromFormat) {
    const auto update = gis::gui::computeDerivedOutputUpdate(
        "",
        "",
        "D:/data/roads.shp",
        "vector",
        "convert",
        "output",
        "CSV");
    EXPECT_TRUE(update.shouldApply);
    EXPECT_EQ(update.value, "D:/data/roads_vector_convert.csv");
}

TEST(GuiSupportTest, ComputeDerivedExpressionUpdateAppliesPresetExpression) {
    const auto update = gis::gui::computeDerivedExpressionUpdate(
        "",
        "",
        "spindex",
        "custom_index",
        "ndvi_alias");
    EXPECT_TRUE(update.shouldApply);
    EXPECT_EQ(update.value, "(NIR-RED)/(NIR+RED)");
    EXPECT_EQ(update.autoValue, "(NIR-RED)/(NIR+RED)");
}

TEST(GuiSupportTest, ComputeDerivedExpressionUpdateRespectsManualExpression) {
    const auto update = gis::gui::computeDerivedExpressionUpdate(
        "(B4-B1)/(B4+B1)",
        "(NIR-RED)/(NIR+RED)",
        "spindex",
        "custom_index",
        "ndwi_alias");
    EXPECT_FALSE(update.shouldApply);
    EXPECT_EQ(update.value, "(GREEN-NIR)/(GREEN+NIR)");
}

TEST(GuiSupportTest, CustomIndexUserPresetStoreSavesLoadsAndRemoves) {
    gis::tests::ensureDirectory(guiSupportTestDir());
    const fs::path presetPath = guiSupportTestDir() / "custom_index_presets.json";
    fs::remove(presetPath);
    CustomIndexPresetFileGuard guard(presetPath);

    std::string errorMessage;
    const std::string presetKey = gis::gui::saveCustomIndexUserPreset(
        "土壤测试",
        "(SWIR1-RED)/(SWIR1+RED)",
        &errorMessage);
    ASSERT_FALSE(presetKey.empty()) << errorMessage;
    EXPECT_TRUE(gis::gui::isCustomIndexUserPresetKey(presetKey));

    const auto presets = gis::gui::loadCustomIndexUserPresets();
    ASSERT_EQ(presets.size(), 1u);
    EXPECT_EQ(presets.front().name, "土壤测试");
    EXPECT_EQ(presets.front().expression, "(SWIR1-RED)/(SWIR1+RED)");
    EXPECT_EQ(
        gis::gui::findCustomIndexUserPresetExpression(presetKey),
        "(SWIR1-RED)/(SWIR1+RED)");

    EXPECT_TRUE(gis::gui::removeCustomIndexUserPreset(presetKey, &errorMessage)) << errorMessage;
    EXPECT_TRUE(gis::gui::loadCustomIndexUserPresets().empty());
}

TEST(GuiSupportTest, SpindexCustomIndexPresetValuesIncludeUserPresets) {
    gis::tests::ensureDirectory(guiSupportTestDir());
    const fs::path presetPath = guiSupportTestDir() / "custom_index_presets_for_values.json";
    fs::remove(presetPath);
    CustomIndexPresetFileGuard guard(presetPath);

    std::string errorMessage;
    const std::string presetKey = gis::gui::saveCustomIndexUserPreset(
        "水体测试",
        "(GREEN-SWIR1)/(GREEN+SWIR1)",
        &errorMessage);
    ASSERT_FALSE(presetKey.empty()) << errorMessage;

    const auto values = gis::gui::spindexCustomIndexPresetValues();
    EXPECT_NE(std::find(values.begin(), values.end(), "ndvi_alias"), values.end());
    EXPECT_NE(std::find(values.begin(), values.end(), presetKey), values.end());
    EXPECT_EQ(
        gis::gui::spindexCustomIndexPresetExpression(presetKey),
        "(GREEN-SWIR1)/(GREEN+SWIR1)");
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

    const auto terrainProfileOutputConfig = gis::gui::buildFileParamUiConfig(
        "terrain", "profile_extract", "output", gis::framework::ParamType::FilePath);
    EXPECT_EQ(terrainProfileOutputConfig.suggestedSuffix, ".csv");
    EXPECT_NE(terrainProfileOutputConfig.saveFilter.find("*.csv"), std::string::npos);
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

TEST(GuiSupportTest, ShouldAutoFillLayerValueAppliesForEmptyOrPreviousAutoValue) {
    EXPECT_TRUE(gis::gui::shouldAutoFillLayerValue("", "", "roads"));
    EXPECT_TRUE(gis::gui::shouldAutoFillLayerValue("roads_old", "roads_old", "roads_new"));
    EXPECT_FALSE(gis::gui::shouldAutoFillLayerValue("manual_layer", "roads_old", "roads_new"));
}

TEST(GuiSupportTest, ShouldAutoFillExtentValueAppliesForUnsetZeroOrPreviousAutoValue) {
    EXPECT_TRUE(gis::gui::shouldAutoFillExtentValue(std::nullopt, std::nullopt, true));
    EXPECT_TRUE(gis::gui::shouldAutoFillExtentValue(
        std::array<double, 4>{0.0, 0.0, 0.0, 0.0},
        std::nullopt,
        true));
    EXPECT_TRUE(gis::gui::shouldAutoFillExtentValue(
        std::array<double, 4>{1.0, 2.0, 3.0, 4.0},
        std::array<double, 4>{1.0, 2.0, 3.0, 4.0},
        true));
    EXPECT_FALSE(gis::gui::shouldAutoFillExtentValue(
        std::array<double, 4>{9.0, 9.0, 9.0, 9.0},
        std::array<double, 4>{1.0, 2.0, 3.0, 4.0},
        true));
    EXPECT_FALSE(gis::gui::shouldAutoFillExtentValue(std::nullopt, std::nullopt, false));
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

TEST(GuiSupportTest, ValidateActionSpecificParamsRequiresCutlineOrExtentForClip) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["cutline"] = std::string();
    params["extent"] = std::array<double, 4>{0.0, 0.0, 0.0, 0.0};

    const auto issue = gis::gui::validateActionSpecificParams("cutting", "clip", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "extent");
    EXPECT_EQ(issue->message, "参数“裁切范围”或“裁切矢量”至少填写一个");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsMismatchedVectorConvertOutputExtension) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["output"] = std::string("D:/data/result.shp");
    params["format"] = std::string("GeoJSON");

    const auto issue = gis::gui::validateActionSpecificParams("vector", "convert", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "output");
    EXPECT_EQ(issue->message, "参数“输出文件”应与“输出格式”一致：GeoJSON 应使用 .geojson 或 .json");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsMismatchedFeatureStatsBandsCount) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["rasters"] = std::string("D:/a.tif,D:/b.tif");
    params["bands"] = std::string("1");

    const auto issue = gis::gui::validateActionSpecificParams("classification", "feature_stats", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "bands");
    EXPECT_EQ(issue->message, "参数“波段列表”数量必须与“分类栅格列表”一致");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsEvenKernelSizeForProcessingFilter) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["kernel_size"] = 4;

    const auto issue = gis::gui::validateActionSpecificParams("processing", "filter", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "kernel_size");
    EXPECT_EQ(issue->message, "参数“核大小”建议填写奇数，例如 3、5、7");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidTerrainValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["band"] = 0;
    params["z_factor"] = 0.0;
    params["azimuth"] = 400.0;
    params["altitude"] = 100.0;

    const auto bandIssue = gis::gui::validateActionSpecificParams("terrain", "hillshade", params);
    ASSERT_TRUE(bandIssue.has_value());
    EXPECT_EQ(bandIssue->key, "band");

    params["band"] = 1;
    const auto zIssue = gis::gui::validateActionSpecificParams("terrain", "hillshade", params);
    ASSERT_TRUE(zIssue.has_value());
    EXPECT_EQ(zIssue->key, "z_factor");

    params["z_factor"] = 1.0;
    const auto azimuthIssue = gis::gui::validateActionSpecificParams("terrain", "hillshade", params);
    ASSERT_TRUE(azimuthIssue.has_value());
    EXPECT_EQ(azimuthIssue->key, "azimuth");

    params["azimuth"] = 315.0;
    const auto altitudeIssue = gis::gui::validateActionSpecificParams("terrain", "hillshade", params);
    ASSERT_TRUE(altitudeIssue.has_value());
    EXPECT_EQ(altitudeIssue->key, "altitude");

    params.clear();
    params["band"] = 1;
    params["z_factor"] = 1.0;
    params["accum_threshold"] = 0.0;
    const auto thresholdIssue = gis::gui::validateActionSpecificParams("terrain", "stream_extract", params);
    ASSERT_TRUE(thresholdIssue.has_value());
    EXPECT_EQ(thresholdIssue->key, "accum_threshold");

    params.clear();
    params["band"] = 1;
    params["profile_path"] = std::string("");
    const auto profilePathIssue = gis::gui::validateActionSpecificParams("terrain", "profile_extract", params);
    ASSERT_TRUE(profilePathIssue.has_value());
    EXPECT_EQ(profilePathIssue->key, "profile_path");

    params["profile_path"] = std::string("116.0,40.0;116.1,39.9");
    params["output"] = std::string("D:/data/profile.json");
    const auto profileOutputIssue = gis::gui::validateActionSpecificParams("terrain", "profile_extract", params);
    ASSERT_TRUE(profileOutputIssue.has_value());
    EXPECT_EQ(profileOutputIssue->key, "output");

    params.clear();
    params["observer_height"] = -1.0;
    const auto observerHeightIssue = gis::gui::validateActionSpecificParams("terrain", "viewshed", params);
    ASSERT_TRUE(observerHeightIssue.has_value());
    EXPECT_EQ(observerHeightIssue->key, "observer_height");

    params.clear();
    params["observer_points"] = std::string("");
    const auto observerPointsIssue = gis::gui::validateActionSpecificParams("terrain", "viewshed_multi", params);
    ASSERT_TRUE(observerPointsIssue.has_value());
    EXPECT_EQ(observerPointsIssue->key, "observer_points");

    params.clear();
    params["observer_points"] = std::string("116.0,40.0;116.1,39.9");
    params["target_height"] = -1.0;
    const auto targetHeightIssue = gis::gui::validateActionSpecificParams("terrain", "viewshed_multi", params);
    ASSERT_TRUE(targetHeightIssue.has_value());
    EXPECT_EQ(targetHeightIssue->key, "target_height");

    params.clear();
    params["observer_points"] = std::string("116.0,40.0;116.1,39.9");
    params["max_distance"] = -1.0;
    const auto maxDistanceIssue = gis::gui::validateActionSpecificParams("terrain", "viewshed_multi", params);
    ASSERT_TRUE(maxDistanceIssue.has_value());
    EXPECT_EQ(maxDistanceIssue->key, "max_distance");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsAcceptsValidVectorConvertCombination) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["output"] = std::string("D:/data/result.geojson");
    params["format"] = std::string("GeoJSON");

    const auto issue = gis::gui::validateActionSpecificParams("vector", "convert", params);
    EXPECT_FALSE(issue.has_value());
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

TEST(GuiSupportTest, BuildEffectiveGuiParamSpecsFiltersVisibleAndRequiredKeys) {
    std::vector<gis::framework::ParamSpec> specs = {
        {"input", "输入", "", gis::framework::ParamType::FilePath, true},
        {"output", "输出", "", gis::framework::ParamType::FilePath, true},
        {"method", "方法", "", gis::framework::ParamType::Enum, false},
        {"hidden", "隐藏", "", gis::framework::ParamType::String, true}
    };

    const auto filtered = gis::gui::buildEffectiveGuiParamSpecs(
        "processing",
        "threshold",
        specs,
        {"input", "output", "method"},
        {"input", "output"});

    ASSERT_EQ(filtered.size(), 3u);
    EXPECT_EQ(filtered[0].key, "input");
    EXPECT_TRUE(filtered[0].required);
    EXPECT_EQ(filtered[1].key, "output");
    EXPECT_TRUE(filtered[1].required);
    EXPECT_EQ(filtered[2].key, "method");
    EXPECT_FALSE(filtered[2].required);
    EXPECT_EQ(std::get<std::string>(filtered[2].defaultValue), "otsu");
}

TEST(GuiSupportTest, BuildEffectiveGuiParamSpecsAppliesProjectionTransformDefaultSrs) {
    std::vector<gis::framework::ParamSpec> specs = {
        {"src_srs", "源坐标系", "", gis::framework::ParamType::CRS, false},
        {"dst_srs", "目标坐标系", "", gis::framework::ParamType::CRS, true}
    };

    const auto filtered = gis::gui::buildEffectiveGuiParamSpecs(
        "projection",
        "transform",
        specs,
        {"src_srs", "dst_srs"},
        {"dst_srs"});

    ASSERT_EQ(filtered.size(), 2u);
    EXPECT_EQ(std::get<std::string>(filtered[0].defaultValue), "EPSG:4326");
    EXPECT_FALSE(filtered[0].required);
    EXPECT_TRUE(filtered[1].required);
}

TEST(GuiSupportTest, BuildEffectiveGuiParamSpecsAppliesMatchingRanges) {
    std::vector<gis::framework::ParamSpec> specs = {
        {"ratio_test", "比率阈值", "", gis::framework::ParamType::Double, false},
        {"quality_level", "质量阈值", "", gis::framework::ParamType::Double, false},
        {"min_distance", "最小间距", "", gis::framework::ParamType::Double, false},
        {"stitch_confidence", "拼接置信度", "", gis::framework::ParamType::Double, false}
    };

    const auto filtered = gis::gui::buildEffectiveGuiParamSpecs(
        "matching",
        "corner",
        specs,
        {"ratio_test", "quality_level", "min_distance", "stitch_confidence"},
        {});

    ASSERT_EQ(filtered.size(), 4u);
    EXPECT_DOUBLE_EQ(std::get<double>(filtered[0].minValue), 0.000001);
    EXPECT_DOUBLE_EQ(std::get<double>(filtered[0].maxValue), 1.0);
    EXPECT_DOUBLE_EQ(std::get<double>(filtered[1].minValue), 0.000001);
    EXPECT_DOUBLE_EQ(std::get<double>(filtered[1].maxValue), 1.0);
    EXPECT_DOUBLE_EQ(std::get<double>(filtered[2].minValue), 0.0);
    EXPECT_DOUBLE_EQ(std::get<double>(filtered[3].minValue), 0.0);
    EXPECT_DOUBLE_EQ(std::get<double>(filtered[3].maxValue), 1.0);
}

TEST(GuiSupportTest, BuildEffectiveGuiParamSpecsAppliesUtilityAndProcessingBounds) {
    std::vector<gis::framework::ParamSpec> rasterManageSpecs = {
        {"band", "波段", "", gis::framework::ParamType::Int, false},
        {"red_band", "红光波段", "", gis::framework::ParamType::Int, false},
        {"nir_band", "近红外波段", "", gis::framework::ParamType::Int, false}
    };
    const auto rasterManageFiltered = gis::gui::buildEffectiveGuiParamSpecs(
        "raster_manage",
        "nodata",
        rasterManageSpecs,
        {"band", "red_band", "nir_band"},
        {});

    ASSERT_EQ(rasterManageFiltered.size(), 3u);
    EXPECT_EQ(std::get<int>(rasterManageFiltered[0].defaultValue), 0);
    EXPECT_EQ(std::get<int>(rasterManageFiltered[0].minValue), 0);
    EXPECT_EQ(std::get<int>(rasterManageFiltered[1].minValue), 0);
    EXPECT_EQ(std::get<int>(rasterManageFiltered[2].minValue), 0);

    std::vector<gis::framework::ParamSpec> rasterInspectSpecs = {
        {"bins", "分箱数", "", gis::framework::ParamType::Int, false},
    };
    const auto rasterInspectFiltered = gis::gui::buildEffectiveGuiParamSpecs(
        "raster_inspect",
        "histogram",
        rasterInspectSpecs,
        {"bins"},
        {});
    ASSERT_EQ(rasterInspectFiltered.size(), 1u);
    EXPECT_EQ(std::get<int>(rasterInspectFiltered[0].minValue), 1);

    std::vector<gis::framework::ParamSpec> spindexSpecs = {
        {"blue_band", "蓝波段", "", gis::framework::ParamType::Int, false},
        {"green_band", "绿波段", "", gis::framework::ParamType::Int, false},
        {"red_band", "红光波段", "", gis::framework::ParamType::Int, false},
        {"nir_band", "近红外波段", "", gis::framework::ParamType::Int, false},
        {"swir1_band", "短波红外1波段", "", gis::framework::ParamType::Int, false}
    };
    const auto spindexFiltered = gis::gui::buildEffectiveGuiParamSpecs(
        "spindex",
        "evi",
        spindexSpecs,
        {"blue_band", "green_band", "red_band", "nir_band", "swir1_band"},
        {});

    ASSERT_EQ(spindexFiltered.size(), 5u);
    EXPECT_EQ(std::get<int>(spindexFiltered[0].minValue), 1);
    EXPECT_EQ(std::get<int>(spindexFiltered[1].minValue), 1);
    EXPECT_EQ(std::get<int>(spindexFiltered[2].minValue), 1);
    EXPECT_EQ(std::get<int>(spindexFiltered[3].minValue), 1);
    EXPECT_EQ(std::get<int>(spindexFiltered[4].minValue), 1);

    gis::framework::ParamSpec expressionSpec{
        "expression", "表达式", "", gis::framework::ParamType::String, false
    };
    EXPECT_NE(
        gis::gui::buildTextParamPlaceholder("spindex", "custom_index", expressionSpec)
            .find("(NIR-RED)/(NIR+RED)"),
        std::string::npos);

    std::vector<gis::framework::ParamSpec> vectorSpecs = {
        {"tolerance", "简化容差", "", gis::framework::ParamType::Double, false}
    };
    const auto vectorFiltered = gis::gui::buildEffectiveGuiParamSpecs(
        "vector",
        "simplify",
        vectorSpecs,
        {"tolerance"},
        {"tolerance"});
    ASSERT_EQ(vectorFiltered.size(), 1u);
    EXPECT_DOUBLE_EQ(std::get<double>(vectorFiltered[0].minValue), 0.000001);
    EXPECT_TRUE(vectorFiltered[0].required);

    std::vector<gis::framework::ParamSpec> processingSpecs = {
        {"gamma", "Gamma", "", gis::framework::ParamType::Double, false},
        {"k", "聚类数", "", gis::framework::ParamType::Int, false},
        {"clip_limit", "CLAHE", "", gis::framework::ParamType::Double, false},
        {"kernel_size", "核大小", "", gis::framework::ParamType::Int, false}
    };
    const auto processingFiltered = gis::gui::buildEffectiveGuiParamSpecs(
        "processing",
        "enhance",
        processingSpecs,
        {"gamma", "k", "clip_limit", "kernel_size"},
        {});

    ASSERT_EQ(processingFiltered.size(), 4u);
    EXPECT_DOUBLE_EQ(std::get<double>(processingFiltered[0].minValue), 0.000001);
    EXPECT_EQ(std::get<int>(processingFiltered[1].minValue), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(processingFiltered[2].minValue), 0.0);
    EXPECT_EQ(std::get<int>(processingFiltered[3].minValue), 3);

    std::vector<gis::framework::ParamSpec> terrainSpecs = {
        {"band", "波段", "", gis::framework::ParamType::Int, false},
        {"z_factor", "高程缩放", "", gis::framework::ParamType::Double, false},
        {"azimuth", "方位角", "", gis::framework::ParamType::Double, false},
        {"altitude", "高度角", "", gis::framework::ParamType::Double, false},
        {"accum_threshold", "汇流阈值", "", gis::framework::ParamType::Double, false}
    };
    const auto terrainFiltered = gis::gui::buildEffectiveGuiParamSpecs(
        "terrain",
        "stream_extract",
        terrainSpecs,
        {"band", "z_factor", "azimuth", "altitude", "accum_threshold"},
        {});

    ASSERT_EQ(terrainFiltered.size(), 5u);
    EXPECT_EQ(std::get<int>(terrainFiltered[0].minValue), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(terrainFiltered[1].minValue), 0.000001);
    EXPECT_DOUBLE_EQ(std::get<double>(terrainFiltered[2].minValue), 0.0);
    EXPECT_DOUBLE_EQ(std::get<double>(terrainFiltered[2].maxValue), 360.0);
    EXPECT_DOUBLE_EQ(std::get<double>(terrainFiltered[3].minValue), 0.0);
    EXPECT_DOUBLE_EQ(std::get<double>(terrainFiltered[3].maxValue), 90.0);
    EXPECT_DOUBLE_EQ(std::get<double>(terrainFiltered[4].minValue), 0.000001);
}

TEST(GuiSupportTest, BuildExecuteButtonStateReflectsSelectionAndValidation) {
    const auto noSelection = gis::gui::buildExecuteButtonState(false, "");
    EXPECT_FALSE(noSelection.enabled);
    EXPECT_EQ(noSelection.tooltip, "请先选择主功能和子功能");
    EXPECT_EQ(noSelection.statusText, "就绪");
    EXPECT_EQ(noSelection.statusObjectName, "statusBadgeReady");

    const auto invalid = gis::gui::buildExecuteButtonState(true, "参数“输入文件”不能为空");
    EXPECT_FALSE(invalid.enabled);
    EXPECT_EQ(invalid.tooltip, "参数“输入文件”不能为空");
    EXPECT_EQ(invalid.statusText, "待补充");
    EXPECT_EQ(invalid.statusObjectName, "statusBadgeWarning");

    const auto ready = gis::gui::buildExecuteButtonState(true, "");
    EXPECT_TRUE(ready.enabled);
    EXPECT_EQ(ready.tooltip, "参数已就绪，可以执行当前功能");
    EXPECT_EQ(ready.statusText, "可执行");
    EXPECT_EQ(ready.statusObjectName, "statusBadgeReady");
}

TEST(GuiSupportTest, ResolveHighlightedParamKeyPrefersFrameworkValidationThenActionIssue) {
    std::vector<gis::framework::ParamSpec> specs = {
        {"input", "输入文件", "", gis::framework::ParamType::FilePath, true},
        {"output", "输出文件", "", gis::framework::ParamType::FilePath, true}
    };
    std::map<std::string, gis::framework::ParamValue> params;
    params["input"] = std::string();
    params["output"] = std::string("D:/data/out.tif");

    EXPECT_EQ(
        gis::gui::resolveHighlightedParamKey(
            true,
            specs,
            params,
            std::optional<gis::gui::ActionValidationIssue>{
                gis::gui::ActionValidationIssue{"output", "动作级问题"}
            }),
        "input");

    params["input"] = std::string("D:/data/in.tif");
    EXPECT_EQ(
        gis::gui::resolveHighlightedParamKey(
            true,
            specs,
            params,
            std::optional<gis::gui::ActionValidationIssue>{
                gis::gui::ActionValidationIssue{"output", "动作级问题"}
            }),
        "output");

    EXPECT_TRUE(
        gis::gui::resolveHighlightedParamKey(true, specs, params, std::nullopt).empty());
    EXPECT_TRUE(
        gis::gui::resolveHighlightedParamKey(false, specs, params, std::nullopt).empty());
}


