#include <gtest/gtest.h>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include <QPushButton>
#include <QCoreApplication>
#include <QLabel>
#include <QTextEdit>
#include <QTreeWidget>

#include "../src/gui/custom_index_preset_store.h"
#include "../src/gui/gui_data_support.h"
#include "../src/gui/task_center_page.h"
#include "../src/gui/task_manager.h"
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

QString uniqueTaskGroupName(const char* suffix) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return QStringLiteral("gui_support_%1_%2").arg(QString::fromUtf8(suffix)).arg(now);
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

TEST(GuiSupportTest, BuildGroupSelectionTextUsesRasterToolsSharedCopy) {
    const auto text = gis::gui::buildGroupSelectionText(
        "raster_tools",
        "ignored",
        "ignored",
        4);

    EXPECT_EQ(text.title, QStringLiteral("栅格工具"));
    EXPECT_TRUE(text.description.contains(QStringLiteral("栅格")));
    EXPECT_EQ(text.metaText, QStringLiteral("当前主功能：栅格工具  |  子功能数：4"));
    EXPECT_EQ(text.statusText, QStringLiteral("当前主功能：栅格工具"));
}

TEST(GuiSupportTest, BuildActionSelectionTextFallsBackToPluginDescription) {
    const auto text = gis::gui::buildActionSelectionText(
        "terrain",
        "terrain",
        "地形分析",
        "地形分析插件描述",
        "unknown_action");

    EXPECT_EQ(text.title, QStringLiteral("unknown_action"));
    EXPECT_EQ(text.description, QStringLiteral("地形分析插件描述"));
    EXPECT_EQ(text.metaText, QStringLiteral("当前主功能：地形分析  |  当前子功能：unknown_action"));
    EXPECT_EQ(text.statusText, QStringLiteral("当前子功能：unknown_action"));
}

TEST(GuiSupportTest, BuildActionSelectionTextUsesGroupTitleForRasterTools) {
    const auto text = gis::gui::buildActionSelectionText(
        "raster_tools",
        "raster_inspect",
        "栅格渲染",
        "渲染插件描述",
        "info");

    EXPECT_EQ(text.title, QStringLiteral("栅格信息"));
    EXPECT_EQ(text.metaText, QStringLiteral("当前主功能：栅格工具  |  当前子功能：栅格信息"));
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
            "D:/data/image.tif", "raster_render", "histogram_match"),
        "D:/data/image_raster_render_histogram_match.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "raster_manage", "cog"),
        "D:/data/image_raster_manage_cog.tif");
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
            "D:/data/scene.tif", "classification", "svm_classify"),
        "D:/data/scene_classification_svm_classify.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/scene.tif", "classification", "random_forest_classify"),
        "D:/data/scene_classification_random_forest_classify.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/scene.tif", "classification", "max_likelihood_classify"),
        "D:/data/scene_classification_max_likelihood_classify.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "processing", "skeleton"),
        "D:/data/image_processing_skeleton.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "processing", "connected_components"),
        "D:/data/image_processing_connected_components.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "processing", "gabor_filter"),
        "D:/data/image_processing_gabor_filter.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "processing", "glcm_texture"),
        "D:/data/image_processing_glcm_texture.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "processing", "mean_shift_filter"),
        "D:/data/image_processing_mean_shift_filter.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "georef", "dos_correction"),
        "D:/data/image_georef_dos_correction.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "georef", "radiometric_calibration"),
        "D:/data/image_georef_radiometric_calibration.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "georef", "gcp_register"),
        "D:/data/image_georef_gcp_register.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "georef", "cosine_correction"),
        "D:/data/image_georef_cosine_correction.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "georef", "minnaert_correction"),
        "D:/data/image_georef_minnaert_correction.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "georef", "c_correction"),
        "D:/data/image_georef_c_correction.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "georef", "percentile_stretch"),
        "D:/data/image_georef_percentile_stretch.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/image.tif", "georef", "rpc_orthorectify"),
        "D:/data/image_georef_rpc_orthorectify.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/dem.tif", "terrain", "slope"),
        "D:/data/dem_terrain_slope.tif");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/dem.tif", "terrain", "curvature"),
        "D:/data/dem_terrain_curvature.tif");
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
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/roads.gpkg", "vector", "dangling_endpoint_check"),
        "D:/data/roads_vector_dangling_endpoint_check.csv");
    EXPECT_EQ(
        gis::gui::buildSuggestedOutputPath(
            "D:/data/parcels.gpkg", "vector", "sliver_remove"),
        "D:/data/parcels_vector_sliver_remove.gpkg");
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

TEST(GuiSupportTest, MultiFileTextPickerRulesStayCentralized) {
    EXPECT_TRUE(
        gis::gui::usesMultiFileTextPicker("classification", "feature_stats", "rasters"));
    EXPECT_TRUE(
        gis::gui::usesMultiFileTextPicker("cutting", "merge_bands", "bands"));
    EXPECT_FALSE(
        gis::gui::usesMultiFileTextPicker("cutting", "merge_bands", "input"));
    EXPECT_FALSE(
        gis::gui::usesMultiFileTextPicker("matching", "stitch", "input"));

    const std::string filter =
        gis::gui::multiFileTextPickerFilter("classification", "feature_stats", "rasters");
    EXPECT_NE(filter.find("*.tif"), std::string::npos);
    EXPECT_NE(filter.find("*.img"), std::string::npos);
    EXPECT_NE(filter.find("所有文件"), std::string::npos);
}

TEST(GuiSupportTest, TaskManagerPersistsExecutionLifecycle) {
    const QString group = uniqueTaskGroupName("lifecycle");
    auto& taskManager = TaskManager::instance();
    taskManager.initializeGroup(group);

    std::map<std::string, gis::framework::ParamValue> params;
    params["input"] = std::string("D:/data/roads.shp");
    params["output"] = std::string("D:/data/roads_buffer.gpkg");

    const QString taskId = taskManager.submitTask(
        group, QStringLiteral("vector"), QStringLiteral("buffer"), params,
        QStringLiteral("矢量工具"), QStringLiteral("缓冲区"));
    ASSERT_FALSE(taskId.isEmpty());

    taskManager.updateTaskStatus(group, taskId, TaskRecord::Running);
    taskManager.appendLog(group, taskId, QStringLiteral("开始执行"), 0);

    gis::framework::Result result;
    result.success = true;
    result.outputPath = "D:/data/roads_buffer.gpkg";
    result.message = "缓冲区处理完成";
    taskManager.finishTask(group, taskId, result);

    const TaskRecord record = taskManager.findTask(group, taskId);
    EXPECT_EQ(record.id, taskId);
    EXPECT_EQ(record.status, TaskRecord::Completed);
    EXPECT_EQ(record.pluginName, QStringLiteral("vector"));
    EXPECT_EQ(record.actionKey, QStringLiteral("buffer"));
    EXPECT_EQ(record.actionDisplayName, QStringLiteral("缓冲区"));
    EXPECT_GT(record.durationMs, 0);
    EXPECT_EQ(std::get<std::string>(record.params.at("output")), "D:/data/roads_buffer.gpkg");

    const auto logs = taskManager.logsForTask(group, taskId);
    ASSERT_EQ(logs.size(), 1);
    EXPECT_EQ(logs.front().message, QStringLiteral("开始执行"));

    const auto recent = taskManager.recentTasks(group, 10);
    ASSERT_FALSE(recent.isEmpty());
    EXPECT_EQ(recent.front().id, taskId);
}

TEST(GuiSupportTest, TaskManagerRerunResetsParamsAndClearsLogs) {
    const QString group = uniqueTaskGroupName("rerun");
    auto& taskManager = TaskManager::instance();
    taskManager.initializeGroup(group);

    std::map<std::string, gis::framework::ParamValue> params;
    params["input"] = std::string("D:/data/a.tif");
    params["output"] = std::string("D:/data/out1.tif");

    const QString taskId = taskManager.submitTask(
        group, QStringLiteral("processing"), QStringLiteral("filter"), params,
        QStringLiteral("处理工具"), QStringLiteral("空间滤波"));
    ASSERT_FALSE(taskId.isEmpty());

    taskManager.appendLog(group, taskId, QStringLiteral("旧日志"), 1);

    std::map<std::string, gis::framework::ParamValue> newParams;
    newParams["input"] = std::string("D:/data/b.tif");
    newParams["output"] = std::string("D:/data/out2.tif");
    taskManager.updateAndRerunTask(group, taskId, newParams);

    const TaskRecord record = taskManager.findTask(group, taskId);
    EXPECT_EQ(record.status, TaskRecord::Pending);
    EXPECT_EQ(std::get<std::string>(record.params.at("input")), "D:/data/b.tif");
    EXPECT_EQ(std::get<std::string>(record.params.at("output")), "D:/data/out2.tif");
    EXPECT_TRUE(taskManager.logsForTask(group, taskId).isEmpty());
}

TEST(GuiSupportTest, TaskCenterPageRefreshesCompletedTaskAndShowsLogs) {
    const QString group = uniqueTaskGroupName("task_center_completed");
    auto& taskManager = TaskManager::instance();
    taskManager.initializeGroup(group);

    std::map<std::string, gis::framework::ParamValue> params;
    params["input"] = std::string("D:/data/roads.shp");
    params["output"] = std::string("D:/data/roads_buffer.gpkg");

    const QString taskId = taskManager.submitTask(
        group, QStringLiteral("vector"), QStringLiteral("buffer"), params,
        QStringLiteral("矢量工具"), QStringLiteral("缓冲区"));
    ASSERT_FALSE(taskId.isEmpty());
    taskManager.appendLog(group, taskId, QStringLiteral("准备执行"), 0);

    gis::framework::Result result;
    result.success = true;
    result.outputPath = "D:/data/roads_buffer.gpkg";
    result.message = "缓冲区处理完成";
    taskManager.finishTask(group, taskId, result);

    TaskCenterPage page;
    page.setCurrentGroup(group);

    auto* taskTree = page.findChild<QTreeWidget*>(QStringLiteral("taskTree"));
    auto* rerunButton = page.findChild<QPushButton*>(QStringLiteral("rerunButton"));
    auto* logDisplay = page.findChild<QTextEdit*>(QStringLiteral("logTerminal"));
    ASSERT_NE(taskTree, nullptr);
    ASSERT_NE(rerunButton, nullptr);
    ASSERT_NE(logDisplay, nullptr);
    ASSERT_EQ(taskTree->topLevelItemCount(), 1);

    auto* item = taskTree->topLevelItem(0);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->data(0, Qt::UserRole).toString(), taskId);

    taskTree->setCurrentItem(item);
    QCoreApplication::processEvents();

    EXPECT_TRUE(rerunButton->isEnabled());
    EXPECT_NE(logDisplay->toPlainText().indexOf(QStringLiteral("准备执行")), -1);
}

TEST(GuiSupportTest, TaskCenterPageOnlyAllowsCompletedTaskRerun) {
    const QString group = uniqueTaskGroupName("task_center_status");
    auto& taskManager = TaskManager::instance();
    taskManager.initializeGroup(group);

    std::map<std::string, gis::framework::ParamValue> pendingParams;
    pendingParams["input"] = std::string("D:/data/a.tif");
    pendingParams["output"] = std::string("D:/data/out_pending.tif");
    const QString pendingTaskId = taskManager.submitTask(
        group, QStringLiteral("processing"), QStringLiteral("filter"), pendingParams,
        QStringLiteral("处理工具"), QStringLiteral("空间滤波"));
    ASSERT_FALSE(pendingTaskId.isEmpty());

    std::map<std::string, gis::framework::ParamValue> completedParams;
    completedParams["input"] = std::string("D:/data/b.tif");
    completedParams["output"] = std::string("D:/data/out_done.tif");
    const QString completedTaskId = taskManager.submitTask(
        group, QStringLiteral("processing"), QStringLiteral("filter"), completedParams,
        QStringLiteral("处理工具"), QStringLiteral("空间滤波"));
    ASSERT_FALSE(completedTaskId.isEmpty());

    gis::framework::Result result;
    result.success = true;
    result.outputPath = "D:/data/out_done.tif";
    result.message = "空间滤波完成";
    taskManager.finishTask(group, completedTaskId, result);

    TaskCenterPage page;
    page.setCurrentGroup(group);

    auto* taskTree = page.findChild<QTreeWidget*>(QStringLiteral("taskTree"));
    auto* rerunButton = page.findChild<QPushButton*>(QStringLiteral("rerunButton"));
    ASSERT_NE(taskTree, nullptr);
    ASSERT_NE(rerunButton, nullptr);
    ASSERT_EQ(taskTree->topLevelItemCount(), 2);

    QTreeWidgetItem* pendingItem = nullptr;
    QTreeWidgetItem* completedItem = nullptr;
    for (int i = 0; i < taskTree->topLevelItemCount(); ++i) {
        auto* item = taskTree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == pendingTaskId) {
            pendingItem = item;
        } else if (item->data(0, Qt::UserRole).toString() == completedTaskId) {
            completedItem = item;
        }
    }

    ASSERT_NE(pendingItem, nullptr);
    ASSERT_NE(completedItem, nullptr);

    taskTree->setCurrentItem(pendingItem);
    QCoreApplication::processEvents();
    EXPECT_FALSE(rerunButton->isEnabled());

    taskTree->setCurrentItem(completedItem);
    QCoreApplication::processEvents();
    EXPECT_TRUE(rerunButton->isEnabled());
}

TEST(GuiSupportTest, TaskCenterPageRerunButtonEmitsSelectedTaskId) {
    const QString group = uniqueTaskGroupName("task_center_rerun_signal");
    auto& taskManager = TaskManager::instance();
    taskManager.initializeGroup(group);

    std::map<std::string, gis::framework::ParamValue> params;
    params["input"] = std::string("D:/data/in.tif");
    params["output"] = std::string("D:/data/out.tif");
    const QString taskId = taskManager.submitTask(
        group, QStringLiteral("processing"), QStringLiteral("filter"), params,
        QStringLiteral("处理工具"), QStringLiteral("空间滤波"));
    ASSERT_FALSE(taskId.isEmpty());

    gis::framework::Result result;
    result.success = true;
    result.outputPath = "D:/data/out.tif";
    result.message = "空间滤波完成";
    taskManager.finishTask(group, taskId, result);

    TaskCenterPage page;
    page.setCurrentGroup(group);

    auto* taskTree = page.findChild<QTreeWidget*>(QStringLiteral("taskTree"));
    auto* rerunButton = page.findChild<QPushButton*>(QStringLiteral("rerunButton"));
    ASSERT_NE(taskTree, nullptr);
    ASSERT_NE(rerunButton, nullptr);
    ASSERT_EQ(taskTree->topLevelItemCount(), 1);

    QString emittedTaskId;
    QObject::connect(&page, &TaskCenterPage::rerunTaskRequested,
                     [&emittedTaskId](const QString& id) { emittedTaskId = id; });

    taskTree->setCurrentItem(taskTree->topLevelItem(0));
    QCoreApplication::processEvents();
    rerunButton->click();

    EXPECT_EQ(emittedTaskId, taskId);
}

TEST(GuiSupportTest, TaskCenterPageClearLogButtonUsesTaskScopeAfterSelection) {
    const QString group = uniqueTaskGroupName("task_center_clear_log_signal");
    auto& taskManager = TaskManager::instance();
    taskManager.initializeGroup(group);

    std::map<std::string, gis::framework::ParamValue> params;
    params["input"] = std::string("D:/data/roads.shp");
    params["output"] = std::string("D:/data/roads_buffer.gpkg");
    const QString taskId = taskManager.submitTask(
        group, QStringLiteral("vector"), QStringLiteral("buffer"), params,
        QStringLiteral("矢量工具"), QStringLiteral("缓冲区"));
    ASSERT_FALSE(taskId.isEmpty());
    taskManager.appendLog(group, taskId, QStringLiteral("准备执行"), 0);

    TaskCenterPage page;
    page.setCurrentGroup(group);

    auto* taskTree = page.findChild<QTreeWidget*>(QStringLiteral("taskTree"));
    auto* clearLogButton = page.findChild<QPushButton*>(QStringLiteral("clearLogButton"));
    auto* logTaskLabel = page.findChild<QLabel*>(QStringLiteral("logTaskLabel"));
    ASSERT_NE(taskTree, nullptr);
    ASSERT_NE(clearLogButton, nullptr);
    ASSERT_NE(logTaskLabel, nullptr);

    int clearAllCount = 0;
    QString clearedTaskId;
    QObject::connect(&page, &TaskCenterPage::clearAllLogsRequested,
                     [&clearAllCount]() { ++clearAllCount; });
    QObject::connect(&page, &TaskCenterPage::clearLogsRequested,
                     [&clearedTaskId](const QString& id) { clearedTaskId = id; });

    clearLogButton->click();
    EXPECT_EQ(clearAllCount, 1);
    EXPECT_TRUE(clearedTaskId.isEmpty());

    taskTree->setCurrentItem(taskTree->topLevelItem(0));
    QCoreApplication::processEvents();
    clearLogButton->click();

    EXPECT_EQ(clearedTaskId, taskId);
    EXPECT_NE(logTaskLabel->text().indexOf(taskId), -1);
}

TEST(GuiSupportTest, TaskCenterPageDoubleClickOnlyEditsFinishedTask) {
    const QString group = uniqueTaskGroupName("task_center_edit_signal");
    auto& taskManager = TaskManager::instance();
    taskManager.initializeGroup(group);

    std::map<std::string, gis::framework::ParamValue> pendingParams;
    pendingParams["input"] = std::string("D:/data/pending.tif");
    pendingParams["output"] = std::string("D:/data/pending_out.tif");
    const QString pendingTaskId = taskManager.submitTask(
        group, QStringLiteral("processing"), QStringLiteral("filter"), pendingParams,
        QStringLiteral("处理工具"), QStringLiteral("空间滤波"));
    ASSERT_FALSE(pendingTaskId.isEmpty());

    std::map<std::string, gis::framework::ParamValue> finishedParams;
    finishedParams["input"] = std::string("D:/data/finished.tif");
    finishedParams["output"] = std::string("D:/data/finished_out.tif");
    const QString finishedTaskId = taskManager.submitTask(
        group, QStringLiteral("processing"), QStringLiteral("filter"), finishedParams,
        QStringLiteral("处理工具"), QStringLiteral("空间滤波"));
    ASSERT_FALSE(finishedTaskId.isEmpty());

    gis::framework::Result result;
    result.success = true;
    result.outputPath = "D:/data/finished_out.tif";
    result.message = "空间滤波完成";
    taskManager.finishTask(group, finishedTaskId, result);

    TaskCenterPage page;
    page.setCurrentGroup(group);

    auto* taskTree = page.findChild<QTreeWidget*>(QStringLiteral("taskTree"));
    ASSERT_NE(taskTree, nullptr);
    ASSERT_EQ(taskTree->topLevelItemCount(), 2);

    QTreeWidgetItem* pendingItem = nullptr;
    QTreeWidgetItem* finishedItem = nullptr;
    for (int i = 0; i < taskTree->topLevelItemCount(); ++i) {
        auto* item = taskTree->topLevelItem(i);
        const QString taskId = item->data(0, Qt::UserRole).toString();
        if (taskId == pendingTaskId) {
            pendingItem = item;
        } else if (taskId == finishedTaskId) {
            finishedItem = item;
        }
    }
    ASSERT_NE(pendingItem, nullptr);
    ASSERT_NE(finishedItem, nullptr);

    QString editedTaskId;
    QObject::connect(&page, &TaskCenterPage::editTaskRequested,
                     [&editedTaskId](const QString& id) { editedTaskId = id; });

    QMetaObject::invokeMethod(
        &page, "onItemDoubleClicked", Qt::DirectConnection,
        Q_ARG(QTreeWidgetItem*, pendingItem), Q_ARG(int, 0));
    EXPECT_TRUE(editedTaskId.isEmpty());

    QMetaObject::invokeMethod(
        &page, "onItemDoubleClicked", Qt::DirectConnection,
        Q_ARG(QTreeWidgetItem*, finishedItem), Q_ARG(int, 0));
    EXPECT_EQ(editedTaskId, finishedTaskId);
}

TEST(GuiSupportTest, TaskCenterPageRemoveTaskRowsUpdatesCountLabel) {
    TaskCenterPage page;
    page.addTaskRow(QStringLiteral("T0001"), QStringLiteral("缓冲区"), TaskRecord::Pending,
                    QStringLiteral("2026-05-11 12:00:00"));
    page.addTaskRow(QStringLiteral("T0002"), QStringLiteral("空间滤波"), TaskRecord::Completed,
                    QStringLiteral("2026-05-11 12:01:00"));

    auto* taskTree = page.findChild<QTreeWidget*>(QStringLiteral("taskTree"));
    auto* countLabel = page.findChild<QLabel*>(QStringLiteral("taskCountLabel"));
    ASSERT_NE(taskTree, nullptr);
    ASSERT_NE(countLabel, nullptr);
    ASSERT_EQ(taskTree->topLevelItemCount(), 2);
    EXPECT_NE(countLabel->text().indexOf(QStringLiteral("2")), -1);

    page.removeTaskRows(QStringList{QStringLiteral("T0001")});
    ASSERT_EQ(taskTree->topLevelItemCount(), 1);
    EXPECT_NE(countLabel->text().indexOf(QStringLiteral("1")), -1);

    page.removeTaskRows(QStringList{QStringLiteral("T0002")});
    ASSERT_EQ(taskTree->topLevelItemCount(), 0);
    EXPECT_NE(countLabel->text().indexOf(QStringLiteral("0")), -1);
}

TEST(GuiSupportTest, TaskCenterPageSetLogForTaskAndClearLogDisplayWorkTogether) {
    TaskCenterPage page;

    auto* logDisplay = page.findChild<QTextEdit*>(QStringLiteral("logTerminal"));
    auto* logTaskLabel = page.findChild<QLabel*>(QStringLiteral("logTaskLabel"));
    ASSERT_NE(logDisplay, nullptr);
    ASSERT_NE(logTaskLabel, nullptr);

    page.setLogForTask(
        QStringLiteral("T0099"),
        QStringList{QStringLiteral("第一行日志"), QStringLiteral("第二行日志")});
    EXPECT_NE(logDisplay->toPlainText().indexOf(QStringLiteral("第一行日志")), -1);
    EXPECT_NE(logDisplay->toPlainText().indexOf(QStringLiteral("第二行日志")), -1);

    page.clearLogDisplay();
    EXPECT_TRUE(logDisplay->toPlainText().isEmpty());
    EXPECT_TRUE(logTaskLabel->text().isEmpty());
}

TEST(GuiSupportTest, TaskCenterPageSwitchingGroupRefreshesListAndClearsOldLogs) {
    auto& taskManager = TaskManager::instance();
    const QString firstGroup = uniqueTaskGroupName("task_center_group_a");
    const QString secondGroup = uniqueTaskGroupName("task_center_group_b");
    taskManager.initializeGroup(firstGroup);
    taskManager.initializeGroup(secondGroup);

    std::map<std::string, gis::framework::ParamValue> firstParams;
    firstParams["input"] = std::string("D:/data/a.tif");
    firstParams["output"] = std::string("D:/data/a_out.tif");
    const QString firstTaskId = taskManager.submitTask(
        firstGroup, QStringLiteral("processing"), QStringLiteral("filter"), firstParams,
        QStringLiteral("处理工具"), QStringLiteral("空间滤波"));
    ASSERT_FALSE(firstTaskId.isEmpty());
    taskManager.appendLog(firstGroup, firstTaskId, QStringLiteral("第一组日志"), 0);

    std::map<std::string, gis::framework::ParamValue> secondParams;
    secondParams["input"] = std::string("D:/data/b.tif");
    secondParams["output"] = std::string("D:/data/b_out.tif");
    const QString secondTaskId = taskManager.submitTask(
        secondGroup, QStringLiteral("vector"), QStringLiteral("buffer"), secondParams,
        QStringLiteral("矢量工具"), QStringLiteral("缓冲区"));
    ASSERT_FALSE(secondTaskId.isEmpty());

    TaskCenterPage page;
    auto* taskTree = page.findChild<QTreeWidget*>(QStringLiteral("taskTree"));
    auto* logDisplay = page.findChild<QTextEdit*>(QStringLiteral("logTerminal"));
    auto* countLabel = page.findChild<QLabel*>(QStringLiteral("taskCountLabel"));
    ASSERT_NE(taskTree, nullptr);
    ASSERT_NE(logDisplay, nullptr);
    ASSERT_NE(countLabel, nullptr);

    page.setCurrentGroup(firstGroup);
    ASSERT_EQ(taskTree->topLevelItemCount(), 1);
    taskTree->setCurrentItem(taskTree->topLevelItem(0));
    QCoreApplication::processEvents();
    EXPECT_NE(logDisplay->toPlainText().indexOf(QStringLiteral("第一组日志")), -1);
    EXPECT_NE(countLabel->text().indexOf(QStringLiteral("1")), -1);

    page.setCurrentGroup(secondGroup);
    ASSERT_EQ(taskTree->topLevelItemCount(), 1);
    EXPECT_EQ(taskTree->topLevelItem(0)->data(0, Qt::UserRole).toString(), secondTaskId);
    EXPECT_TRUE(logDisplay->toPlainText().isEmpty());
    EXPECT_NE(countLabel->text().indexOf(QStringLiteral("1")), -1);
}

TEST(GuiSupportTest, TaskCenterPageUpdateTaskProgressUpdatesPercentText) {
    TaskCenterPage page;
    page.addTaskRow(QStringLiteral("T0101"), QStringLiteral("空间滤波"), TaskRecord::Running,
                    QStringLiteral("2026-05-11 12:10:00"));

    auto* taskTree = page.findChild<QTreeWidget*>(QStringLiteral("taskTree"));
    ASSERT_NE(taskTree, nullptr);
    ASSERT_EQ(taskTree->topLevelItemCount(), 1);

    page.updateTaskProgress(QStringLiteral("T0101"), 0.426);
    EXPECT_EQ(taskTree->topLevelItem(0)->text(3), QStringLiteral("42%"));

    page.updateTaskProgress(QStringLiteral("T0101"), 1.4);
    EXPECT_EQ(taskTree->topLevelItem(0)->text(3), QStringLiteral("100%"));
}

TEST(GuiSupportTest, TaskCenterPageUpdateTaskRowSetsCompletedStatusAndDuration) {
    TaskCenterPage page;
    page.addTaskRow(QStringLiteral("T0102"), QStringLiteral("缓冲区"), TaskRecord::Running,
                    QStringLiteral("2026-05-11 12:00:00"));

    auto* taskTree = page.findChild<QTreeWidget*>(QStringLiteral("taskTree"));
    ASSERT_NE(taskTree, nullptr);
    ASSERT_EQ(taskTree->topLevelItemCount(), 1);

    page.updateTaskRow(QStringLiteral("T0102"), TaskRecord::Completed,
                       QStringLiteral("2026-05-11 12:00:05"), 5200);

    auto* item = taskTree->topLevelItem(0);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->text(0), QStringLiteral("已完成"));
    EXPECT_EQ(item->text(3), QStringLiteral("100%"));
    EXPECT_NE(item->text(4).indexOf(QStringLiteral("5.2")), -1);
}

TEST(GuiSupportTest, TaskCenterPageAppendLogOnlyUpdatesCurrentTaskView) {
    TaskCenterPage page;
    page.setLogForTask(QStringLiteral("T0103"), QStringList{QStringLiteral("初始日志")});

    auto* logDisplay = page.findChild<QTextEdit*>(QStringLiteral("logTerminal"));
    ASSERT_NE(logDisplay, nullptr);

    page.appendLog(QStringLiteral("T0104"), QStringLiteral("别的任务日志"));
    EXPECT_EQ(logDisplay->toPlainText().indexOf(QStringLiteral("别的任务日志")), -1);

    page.appendLog(QStringLiteral("T0103"), QStringLiteral("完成"));
    EXPECT_NE(logDisplay->toPlainText().indexOf(QStringLiteral("初始日志")), -1);
    EXPECT_NE(logDisplay->toPlainText().indexOf(QStringLiteral("完成")), -1);
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

TEST(GuiSupportTest, ComputeDerivedParamSyncResultAggregatesAutoUpdates) {
    gis::gui::DerivedParamTracking tracking;
    tracking.outputPath = "D:/data/old_vector_buffer.gpkg";
    tracking.layerName = "old_layer";
    tracking.extent = std::array<double, 4>{1.0, 2.0, 3.0, 4.0};

    gis::gui::DataAutoFillInfo inputInfo;
    inputInfo.layerName = "roads";
    inputInfo.extent = {100.0, 20.0, 110.0, 30.0};
    inputInfo.hasExtent = true;

    const auto result = gis::gui::computeDerivedParamSyncResult(
        "vector",
        "buffer",
        "D:/data/source.geojson",
        "",
        true,
        "D:/data/old_vector_buffer.gpkg",
        false,
        "",
        false,
        "",
        false,
        "",
        "",
        true,
        "old_layer",
        true,
        std::array<double, 4>{1.0, 2.0, 3.0, 4.0},
        "D:/data/source.gpkg",
        inputInfo,
        tracking);

    EXPECT_TRUE(result.outputUpdate.shouldApply);
    EXPECT_EQ(result.outputUpdate.value, "D:/data/source_vector_buffer.gpkg");
    EXPECT_TRUE(result.shouldApplyLayer);
    EXPECT_EQ(result.layerValue, "roads");
    EXPECT_TRUE(result.shouldApplyExtent);
    EXPECT_TRUE(result.extent == (std::array<double, 4>{100.0, 20.0, 110.0, 30.0}));
    EXPECT_EQ(result.tracking.outputPath, "D:/data/source_vector_buffer.gpkg");
    EXPECT_EQ(result.tracking.layerName, "roads");
    EXPECT_TRUE(result.tracking.extent == (std::array<double, 4>{100.0, 20.0, 110.0, 30.0}));
}

TEST(GuiSupportTest, ComputeDerivedParamSyncResultPreservesManualValues) {
    gis::gui::DerivedParamTracking tracking;
    tracking.outputPath = "D:/data/source_vector_buffer.gpkg";
    tracking.layerName = "roads";
    tracking.extent = std::array<double, 4>{100.0, 20.0, 110.0, 30.0};

    gis::gui::DataAutoFillInfo inputInfo;
    inputInfo.layerName = "buildings";
    inputInfo.extent = {200.0, 40.0, 210.0, 50.0};
    inputInfo.hasExtent = true;

    const auto result = gis::gui::computeDerivedParamSyncResult(
        "vector",
        "buffer",
        "D:/data/changed.geojson",
        "",
        true,
        "D:/data/manual_output.gpkg",
        false,
        "",
        false,
        "",
        false,
        "",
        "",
        true,
        "manual_layer",
        true,
        std::array<double, 4>{9.0, 9.0, 9.0, 9.0},
        "D:/data/changed.gpkg",
        inputInfo,
        tracking);

    EXPECT_FALSE(result.outputUpdate.shouldApply);
    EXPECT_EQ(result.outputUpdate.value, "D:/data/changed_vector_buffer.gpkg");
    EXPECT_FALSE(result.shouldApplyLayer);
    EXPECT_FALSE(result.shouldApplyExtent);
    EXPECT_EQ(result.tracking.outputPath, "D:/data/changed_vector_buffer.gpkg");
    EXPECT_EQ(result.tracking.layerName, "buildings");
    EXPECT_TRUE(result.tracking.extent == (std::array<double, 4>{100.0, 20.0, 110.0, 30.0}));
}

TEST(GuiSupportTest, CollectBatchFilesUsesDefaultAndCustomFilters) {
    gis::tests::ensureDirectory(guiSupportTestDir());
    const fs::path batchDir = guiSupportTestDir() / "batch_inputs";
    gis::tests::ensureDirectory(batchDir);

    std::ofstream(batchDir / "a.tif").put('\n');
    std::ofstream(batchDir / "b.img").put('\n');
    std::ofstream(batchDir / "c.txt").put('\n');

    const QStringList defaultFiles = gis::gui::collectBatchFiles(
        QString::fromStdString(batchDir.string()),
        QString());
    ASSERT_EQ(defaultFiles.size(), 1);
    EXPECT_TRUE(defaultFiles.front().endsWith(QStringLiteral("a.tif")));

    const QStringList customFiles = gis::gui::collectBatchFiles(
        QString::fromStdString(batchDir.string()),
        QStringLiteral("*.tif *.img"));
    ASSERT_EQ(customFiles.size(), 2);
    EXPECT_TRUE(customFiles[0].endsWith(QStringLiteral("a.tif")));
    EXPECT_TRUE(customFiles[1].endsWith(QStringLiteral("b.img")));
}

TEST(GuiSupportTest, BuildBatchCountTextReflectsMatchCount) {
    EXPECT_EQ(gis::gui::buildBatchCountText(0), QStringLiteral("未找到匹配文件"));
    EXPECT_EQ(gis::gui::buildBatchCountText(3), QStringLiteral("匹配 3 个文件"));
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

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidClassificationSvmValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["training_csv"] = std::string("D:/data/samples.json");

    auto issue = gis::gui::validateActionSpecificParams("classification", "svm_classify", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "training_csv");

    params["training_csv"] = std::string("D:/data/samples.csv");
    params["output"] = std::string("D:/data/result.json");
    issue = gis::gui::validateActionSpecificParams("classification", "svm_classify", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "output");

    params["output"] = std::string("D:/data/result.tif");
    params["bands"] = std::string("1,0,3");
    issue = gis::gui::validateActionSpecificParams("classification", "svm_classify", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "bands");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidGeorefDosValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["band"] = 0;

    auto issue = gis::gui::validateActionSpecificParams("georef", "dos_correction", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "band");

    params["band"] = 1;
    params["output"] = std::string("D:/data/result.json");
    issue = gis::gui::validateActionSpecificParams("georef", "dos_correction", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "output");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidGeorefRadiometricValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["band"] = 0;

    auto issue = gis::gui::validateActionSpecificParams("georef", "radiometric_calibration", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "band");

    params["band"] = 1;
    params["output"] = std::string("D:/data/result.json");
    issue = gis::gui::validateActionSpecificParams("georef", "radiometric_calibration", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "output");

    params["output"] = std::string("D:/data/result.tif");
    params["metadata_file"] = std::string("D:/data/meta.csv");
    issue = gis::gui::validateActionSpecificParams("georef", "radiometric_calibration", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "metadata_file");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidGeorefGcpRegisterValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["gcp_file"] = std::string("D:/data/points.json");

    auto issue = gis::gui::validateActionSpecificParams("georef", "gcp_register", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "gcp_file");

    params["gcp_file"] = std::string("D:/data/points.csv");
    params["output"] = std::string("D:/data/result.json");
    issue = gis::gui::validateActionSpecificParams("georef", "gcp_register", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "output");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidGeorefCosineValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["band"] = 0;

    auto issue = gis::gui::validateActionSpecificParams("georef", "cosine_correction", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "band");

    params["band"] = 1;
    params["sun_zenith_deg"] = 95.0;
    issue = gis::gui::validateActionSpecificParams("georef", "cosine_correction", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "sun_zenith_deg");

    params["sun_zenith_deg"] = 30.0;
    params["sun_azimuth_deg"] = 400.0;
    issue = gis::gui::validateActionSpecificParams("georef", "cosine_correction", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "sun_azimuth_deg");

    params["sun_azimuth_deg"] = 180.0;
    params["output"] = std::string("D:/data/result.json");
    issue = gis::gui::validateActionSpecificParams("georef", "cosine_correction", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "output");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidGeorefMinnaertValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["band"] = 0;

    auto issue = gis::gui::validateActionSpecificParams("georef", "minnaert_correction", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "band");

    params["band"] = 1;
    params["minnaert_k"] = 0.0;
    issue = gis::gui::validateActionSpecificParams("georef", "minnaert_correction", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "minnaert_k");

    params["minnaert_k"] = 0.5;
    params["sun_zenith_deg"] = 95.0;
    issue = gis::gui::validateActionSpecificParams("georef", "minnaert_correction", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "sun_zenith_deg");

    params["sun_zenith_deg"] = 30.0;
    params["sun_azimuth_deg"] = 400.0;
    issue = gis::gui::validateActionSpecificParams("georef", "minnaert_correction", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "sun_azimuth_deg");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidGeorefCValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["band"] = 0;

    auto issue = gis::gui::validateActionSpecificParams("georef", "c_correction", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "band");

    params["band"] = 1;
    params["c_value"] = -0.1;
    issue = gis::gui::validateActionSpecificParams("georef", "c_correction", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "c_value");

    params["c_value"] = 0.1;
    params["sun_zenith_deg"] = 95.0;
    issue = gis::gui::validateActionSpecificParams("georef", "c_correction", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "sun_zenith_deg");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidGeorefQuacValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["dark_percentile"] = 100.0;

    auto issue = gis::gui::validateActionSpecificParams("georef", "percentile_stretch", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "dark_percentile");

    params["dark_percentile"] = 10.0;
    params["bright_percentile"] = 10.0;
    issue = gis::gui::validateActionSpecificParams("georef", "percentile_stretch", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "bright_percentile");

    params["bright_percentile"] = 101.0;
    issue = gis::gui::validateActionSpecificParams("georef", "percentile_stretch", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "bright_percentile");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidGeorefRpcValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["output"] = std::string("D:/data/result.json");

    auto issue = gis::gui::validateActionSpecificParams("georef", "rpc_orthorectify", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "output");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsEvenKernelSizeForProcessingFilter) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["kernel_size"] = 4;

    const auto issue = gis::gui::validateActionSpecificParams("processing", "filter", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "kernel_size");
    EXPECT_EQ(issue->message, "参数“核大小”建议填写奇数，例如 3、5、7");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidProcessingGaborValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["kernel_size"] = 4;

    auto issue = gis::gui::validateActionSpecificParams("processing", "gabor_filter", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "kernel_size");

    params["kernel_size"] = 5;
    params["sigma"] = 0.0;
    issue = gis::gui::validateActionSpecificParams("processing", "gabor_filter", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "sigma");

    params.erase("sigma");
    params["gabor_lambda"] = 0.0;
    issue = gis::gui::validateActionSpecificParams("processing", "gabor_filter", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "gabor_lambda");

    params.erase("gabor_lambda");
    params["gabor_gamma"] = 0.0;
    issue = gis::gui::validateActionSpecificParams("processing", "gabor_filter", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "gabor_gamma");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidProcessingGlcmValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["kernel_size"] = 4;

    auto issue = gis::gui::validateActionSpecificParams("processing", "glcm_texture", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "kernel_size");

    params["kernel_size"] = 5;
    params["glcm_levels"] = 1;
    issue = gis::gui::validateActionSpecificParams("processing", "glcm_texture", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "glcm_levels");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidProcessingMeanShiftValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["spatial_radius"] = 0.0;

    auto issue = gis::gui::validateActionSpecificParams("processing", "mean_shift_filter", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "spatial_radius");

    params.erase("spatial_radius");
    params["color_radius"] = 0.0;
    issue = gis::gui::validateActionSpecificParams("processing", "mean_shift_filter", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "color_radius");

    params.erase("color_radius");
    params["pyramid_level"] = -1;
    issue = gis::gui::validateActionSpecificParams("processing", "mean_shift_filter", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "pyramid_level");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidRasterManageCogOutputExtension) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["output"] = std::string("D:/data/output.png");

    const auto issue = gis::gui::validateActionSpecificParams("raster_manage", "cog", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "output");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidVectorSimplifyTolerance) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["tolerance"] = 0.0;

    const auto issue = gis::gui::validateActionSpecificParams("vector", "simplify", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "tolerance");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidVectorSliverRemoveValues) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["output"] = std::string("D:/data/output.csv");

    auto issue = gis::gui::validateActionSpecificParams("vector", "sliver_remove", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "output");

    params["output"] = std::string("D:/data/output.gpkg");
    params["min_area"] = 0.0;

    issue = gis::gui::validateActionSpecificParams("vector", "sliver_remove", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "min_area");
}

TEST(GuiSupportTest, ValidateActionSpecificParamsRejectsInvalidVectorSpatialJoinOutputExtension) {
    std::map<std::string, gis::framework::ParamValue> params;
    params["output"] = std::string("D:/data/output.csv");

    const auto issue = gis::gui::validateActionSpecificParams("vector", "spatial_join", params);
    ASSERT_TRUE(issue.has_value());
    EXPECT_EQ(issue->key, "output");
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
    EXPECT_EQ(invalid.statusText, "待修正");
    EXPECT_EQ(invalid.statusObjectName, "statusBadgeWarning");

    const auto ready = gis::gui::buildExecuteButtonState(true, "");
    EXPECT_TRUE(ready.enabled);
    EXPECT_EQ(ready.tooltip, "参数已就绪，可以执行当前功能");
    EXPECT_EQ(ready.statusText, "可执行");
    EXPECT_EQ(ready.statusObjectName, "statusBadgeReady");
}

TEST(GuiSupportTest, RasterToolsGroupingRulesStayCentralized) {
    EXPECT_EQ(gis::gui::rasterToolsGroupKey(), "raster_tools");
    EXPECT_TRUE(gis::gui::isRasterToolsMember("raster_manage"));
    EXPECT_TRUE(gis::gui::isRasterToolsMember("raster_inspect"));
    EXPECT_TRUE(gis::gui::isRasterToolsMember("raster_render"));
    EXPECT_TRUE(gis::gui::isRasterToolsMember("raster_math"));
    EXPECT_FALSE(gis::gui::isRasterToolsMember("projection"));
    EXPECT_EQ(gis::gui::displayGroupForPlugin("raster_manage"), "raster_tools");
    EXPECT_EQ(gis::gui::displayGroupForPlugin("projection"), "projection");
}

TEST(GuiSupportTest, RasterToolsPluginOrderStaysStable) {
    const std::vector<std::string> expected = {
        "raster_manage",
        "raster_inspect",
        "raster_render",
        "raster_math",
    };
    EXPECT_EQ(gis::gui::rasterToolsPluginNames(), expected);
}

TEST(GuiSupportTest, RasterToolsGroupDisplayTextStaysCentralized) {
    EXPECT_EQ(gis::gui::rasterToolsGroupDisplayName(), QStringLiteral("栅格工具"));
    EXPECT_EQ(
        gis::gui::rasterToolsGroupDescription(),
        QStringLiteral("集中提供栅格管理、检查、渲染与波段运算相关子功能。"));
}

TEST(GuiSupportTest, ActionDisplayNameUsesConfiguredValueOrFallsBackToActionKey) {
    EXPECT_EQ(
        gis::gui::actionDisplayName("processing", "gabor_filter"),
        QStringLiteral("Gabor 滤波"));
    EXPECT_EQ(
        gis::gui::actionDisplayName("classification", "feature_stats"),
        QStringLiteral("地物分类统计"));
    EXPECT_EQ(
        gis::gui::actionDisplayName("projection", "reproject"),
        QStringLiteral("重投影"));
    EXPECT_EQ(
        gis::gui::actionDisplayName("vector", "buffer"),
        QStringLiteral("缓冲区"));
    EXPECT_NE(
        gis::gui::actionDisplayName("processing", "skeleton"),
        QStringLiteral("skeleton"));
    EXPECT_NE(
        gis::gui::actionDisplayName("processing", "watershed"),
        QStringLiteral("watershed"));
    EXPECT_NE(
        gis::gui::actionDisplayName("processing", "contour"),
        QStringLiteral("contour"));
    EXPECT_NE(
        gis::gui::actionDisplayName("processing", "edge"),
        QStringLiteral("edge"));
    EXPECT_NE(
        gis::gui::actionDisplayName("processing", "template_match"),
        QStringLiteral("template_match"));
    EXPECT_NE(
        gis::gui::actionDisplayName("processing", "enhance"),
        QStringLiteral("enhance"));
    EXPECT_NE(
        gis::gui::actionDisplayName("processing", "filter"),
        QStringLiteral("filter"));
    EXPECT_NE(
        gis::gui::actionDisplayName("processing", "threshold"),
        QStringLiteral("threshold"));
    EXPECT_NE(
        gis::gui::actionDisplayName("processing", "stats"),
        QStringLiteral("stats"));
    EXPECT_NE(
        gis::gui::actionDisplayName("processing", "gabor_filter"),
        QStringLiteral("gabor_filter"));
    EXPECT_NE(
        gis::gui::actionDisplayName("projection", "reproject"),
        QStringLiteral("reproject"));
    EXPECT_NE(
        gis::gui::actionDisplayName("vector", "buffer"),
        QStringLiteral("buffer"));
    EXPECT_NE(
        gis::gui::actionDisplayName("projection", "info"),
        QStringLiteral("info"));
    EXPECT_NE(
        gis::gui::actionDisplayName("projection", "transform"),
        QStringLiteral("transform"));
    EXPECT_NE(
        gis::gui::actionDisplayName("projection", "assign_srs"),
        QStringLiteral("assign_srs"));
    EXPECT_NE(
        gis::gui::actionDisplayName("vector", "clip"),
        QStringLiteral("clip"));
    EXPECT_NE(
        gis::gui::actionDisplayName("vector", "filter"),
        QStringLiteral("filter"));
    EXPECT_NE(
        gis::gui::actionDisplayName("raster_inspect", "info"),
        QStringLiteral("info"));
    EXPECT_NE(
        gis::gui::actionDisplayName("raster_inspect", "histogram"),
        QStringLiteral("histogram"));
    EXPECT_NE(
        gis::gui::actionDisplayName("matching", "detect"),
        QStringLiteral("detect"));
    EXPECT_NE(
        gis::gui::actionDisplayName("matching", "match"),
        QStringLiteral("match"));
    EXPECT_NE(
        gis::gui::actionDisplayName("matching", "register"),
        QStringLiteral("register"));
    EXPECT_NE(
        gis::gui::actionDisplayName("matching", "change"),
        QStringLiteral("change"));
    EXPECT_NE(
        gis::gui::actionDisplayName("matching", "ecc_register"),
        QStringLiteral("ecc_register"));
    EXPECT_NE(
        gis::gui::actionDisplayName("matching", "corner"),
        QStringLiteral("corner"));
    EXPECT_NE(
        gis::gui::actionDisplayName("matching", "stitch"),
        QStringLiteral("stitch"));
    EXPECT_NE(
        gis::gui::actionDisplayName("raster_manage", "overviews"),
        QStringLiteral("overviews"));
    EXPECT_NE(
        gis::gui::actionDisplayName("raster_manage", "cog"),
        QStringLiteral("cog"));
    EXPECT_NE(
        gis::gui::actionDisplayName("raster_manage", "nodata"),
        QStringLiteral("nodata"));
    EXPECT_EQ(
        gis::gui::actionDisplayName("processing", "unknown_action"),
        QStringLiteral("unknown_action"));
}

TEST(GuiSupportTest, ActionDescriptionUsesConfiguredValueOrFallsBackToEmpty) {
    EXPECT_EQ(
        gis::gui::actionDescription("processing", "gabor_filter"),
        QStringLiteral("按给定方向和尺度提取纹理响应。"));
    EXPECT_EQ(
        gis::gui::actionDescription("classification", "feature_stats"),
        QStringLiteral("按面要素范围对多源分类栅格执行优先级统计，可输出统计表、分类面和分类栅格。"));
    EXPECT_FALSE(
        gis::gui::actionDescription("processing", "skeleton").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("processing", "watershed").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("processing", "contour").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("processing", "edge").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("processing", "template_match").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("processing", "enhance").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("processing", "filter").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("processing", "threshold").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("processing", "stats").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("processing", "gabor_filter").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("projection", "info").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("projection", "transform").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("projection", "assign_srs").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("vector", "clip").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("vector", "filter").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("raster_inspect", "info").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("raster_inspect", "histogram").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("matching", "detect").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("matching", "match").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("matching", "register").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("matching", "change").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("matching", "ecc_register").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("matching", "corner").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("matching", "stitch").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("raster_manage", "overviews").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("raster_manage", "cog").isEmpty());
    EXPECT_FALSE(
        gis::gui::actionDescription("raster_manage", "nodata").isEmpty());
    EXPECT_TRUE(
        gis::gui::actionDescription("processing", "unknown_action").isEmpty());
}

TEST(GuiSupportTest, ActionUiConfigLookupReturnsVisibleAndRequiredKeys) {
    const auto* skeletonConfig =
        gis::gui::findActionUiConfig("processing", "skeleton");
    ASSERT_NE(skeletonConfig, nullptr);
    EXPECT_TRUE(skeletonConfig->visibleKeys.count("band") > 0);
    EXPECT_TRUE(skeletonConfig->requiredKeys.count("output") > 0);

    const auto* watershedConfig =
        gis::gui::findActionUiConfig("processing", "watershed");
    ASSERT_NE(watershedConfig, nullptr);
    EXPECT_TRUE(watershedConfig->visibleKeys.count("marker_input") > 0);
    EXPECT_TRUE(watershedConfig->requiredKeys.count("output") > 0);

    const auto* contourConfig =
        gis::gui::findActionUiConfig("processing", "contour");
    ASSERT_NE(contourConfig, nullptr);
    EXPECT_TRUE(contourConfig->visibleKeys.count("min_area") > 0);
    EXPECT_TRUE(contourConfig->requiredKeys.count("output") > 0);

    const auto* edgeConfig =
        gis::gui::findActionUiConfig("processing", "edge");
    ASSERT_NE(edgeConfig, nullptr);
    EXPECT_TRUE(edgeConfig->visibleKeys.count("edge_method") > 0);
    EXPECT_TRUE(edgeConfig->visibleKeys.count("low_threshold") > 0);
    EXPECT_TRUE(edgeConfig->visibleKeys.count("sobel_dx") > 0);
    EXPECT_TRUE(edgeConfig->requiredKeys.count("output") > 0);

    const auto* templateMatchConfig =
        gis::gui::findActionUiConfig("processing", "template_match");
    ASSERT_NE(templateMatchConfig, nullptr);
    EXPECT_TRUE(templateMatchConfig->visibleKeys.count("template_file") > 0);
    EXPECT_TRUE(templateMatchConfig->visibleKeys.count("match_method") > 0);
    EXPECT_TRUE(templateMatchConfig->requiredKeys.count("template_file") > 0);

    const auto* enhanceConfig = gis::gui::findActionUiConfig("processing", "enhance");
    ASSERT_NE(enhanceConfig, nullptr);
    EXPECT_TRUE(enhanceConfig->visibleKeys.count("enhance_type") > 0);
    EXPECT_TRUE(enhanceConfig->visibleKeys.count("clip_limit") > 0);
    EXPECT_TRUE(enhanceConfig->requiredKeys.count("output") > 0);

    const auto* filterConfig = gis::gui::findActionUiConfig("processing", "filter");
    ASSERT_NE(filterConfig, nullptr);
    EXPECT_TRUE(filterConfig->visibleKeys.count("filter_type") > 0);
    EXPECT_TRUE(filterConfig->visibleKeys.count("kernel_size") > 0);
    EXPECT_TRUE(filterConfig->requiredKeys.count("output") > 0);

    const auto* thresholdConfig = gis::gui::findActionUiConfig("processing", "threshold");
    ASSERT_NE(thresholdConfig, nullptr);
    EXPECT_TRUE(thresholdConfig->visibleKeys.count("threshold_value") > 0);
    EXPECT_TRUE(thresholdConfig->visibleKeys.count("max_value") > 0);
    EXPECT_TRUE(thresholdConfig->requiredKeys.count("output") > 0);

    const auto* statsConfig = gis::gui::findActionUiConfig("processing", "stats");
    ASSERT_NE(statsConfig, nullptr);
    EXPECT_TRUE(statsConfig->visibleKeys.count("input") > 0);
    EXPECT_TRUE(statsConfig->visibleKeys.count("band") > 0);
    EXPECT_TRUE(statsConfig->requiredKeys.count("input") > 0);

    const auto* config = gis::gui::findActionUiConfig("processing", "gabor_filter");
    ASSERT_NE(config, nullptr);
    EXPECT_NE(config->displayName, QStringLiteral("gabor_filter"));
    EXPECT_FALSE(config->description.isEmpty());
    EXPECT_TRUE(config->visibleKeys.count("input") > 0);
    EXPECT_TRUE(config->visibleKeys.count("output") > 0);
    EXPECT_TRUE(config->requiredKeys.count("input") > 0);

    const auto* vectorConfig = gis::gui::findActionUiConfig("vector", "buffer");
    ASSERT_NE(vectorConfig, nullptr);
    EXPECT_TRUE(vectorConfig->visibleKeys.count("distance") > 0);

    const auto* projectionConfig = gis::gui::findActionUiConfig("projection", "reproject");
    ASSERT_NE(projectionConfig, nullptr);
    EXPECT_TRUE(projectionConfig->requiredKeys.count("dst_srs") > 0);

    const auto* projectionInfoConfig = gis::gui::findActionUiConfig("projection", "info");
    ASSERT_NE(projectionInfoConfig, nullptr);
    EXPECT_TRUE(projectionInfoConfig->visibleKeys.count("input") > 0);
    EXPECT_TRUE(projectionInfoConfig->requiredKeys.count("input") > 0);

    const auto* vectorClipConfig = gis::gui::findActionUiConfig("vector", "clip");
    ASSERT_NE(vectorClipConfig, nullptr);
    EXPECT_TRUE(vectorClipConfig->visibleKeys.count("clip_vector") > 0);
    EXPECT_TRUE(vectorClipConfig->requiredKeys.count("clip_vector") > 0);

    const auto* projectionTransformConfig =
        gis::gui::findActionUiConfig("projection", "transform");
    ASSERT_NE(projectionTransformConfig, nullptr);
    EXPECT_TRUE(projectionTransformConfig->visibleKeys.count("x") > 0);
    EXPECT_TRUE(projectionTransformConfig->visibleKeys.count("y") > 0);
    EXPECT_TRUE(projectionTransformConfig->requiredKeys.count("dst_srs") > 0);

    const auto* vectorFilterConfig = gis::gui::findActionUiConfig("vector", "filter");
    ASSERT_NE(vectorFilterConfig, nullptr);
    EXPECT_TRUE(vectorFilterConfig->visibleKeys.count("where") > 0);
    EXPECT_TRUE(vectorFilterConfig->visibleKeys.count("extent") > 0);

    const auto* projectionAssignConfig =
        gis::gui::findActionUiConfig("projection", "assign_srs");
    ASSERT_NE(projectionAssignConfig, nullptr);
    EXPECT_TRUE(projectionAssignConfig->visibleKeys.count("srs") > 0);
    EXPECT_TRUE(projectionAssignConfig->requiredKeys.count("srs") > 0);

    const auto* rasterInfoConfig =
        gis::gui::findActionUiConfig("raster_inspect", "info");
    ASSERT_NE(rasterInfoConfig, nullptr);
    EXPECT_TRUE(rasterInfoConfig->requiredKeys.count("input") > 0);

    const auto* rasterHistogramConfig =
        gis::gui::findActionUiConfig("raster_inspect", "histogram");
    ASSERT_NE(rasterHistogramConfig, nullptr);
    EXPECT_TRUE(rasterHistogramConfig->visibleKeys.count("bins") > 0);
    EXPECT_TRUE(rasterHistogramConfig->visibleKeys.count("band") > 0);

    const auto* matchingDetectConfig =
        gis::gui::findActionUiConfig("matching", "detect");
    ASSERT_NE(matchingDetectConfig, nullptr);
    EXPECT_TRUE(matchingDetectConfig->visibleKeys.count("method") > 0);
    EXPECT_TRUE(matchingDetectConfig->requiredKeys.count("input") > 0);

    const auto* rasterOverviewsConfig =
        gis::gui::findActionUiConfig("raster_manage", "overviews");
    ASSERT_NE(rasterOverviewsConfig, nullptr);
    EXPECT_TRUE(rasterOverviewsConfig->visibleKeys.count("levels") > 0);
    EXPECT_TRUE(rasterOverviewsConfig->visibleKeys.count("resample") > 0);
    EXPECT_TRUE(rasterOverviewsConfig->requiredKeys.count("input") > 0);

    const auto* rasterCogConfig =
        gis::gui::findActionUiConfig("raster_manage", "cog");
    ASSERT_NE(rasterCogConfig, nullptr);
    EXPECT_TRUE(rasterCogConfig->requiredKeys.count("output") > 0);

    const auto* matchingMatchConfig =
        gis::gui::findActionUiConfig("matching", "match");
    ASSERT_NE(matchingMatchConfig, nullptr);
    EXPECT_TRUE(matchingMatchConfig->visibleKeys.count("reference") > 0);
    EXPECT_TRUE(matchingMatchConfig->visibleKeys.count("ratio_test") > 0);
    EXPECT_TRUE(matchingMatchConfig->requiredKeys.count("reference") > 0);

    const auto* rasterNodataConfig =
        gis::gui::findActionUiConfig("raster_manage", "nodata");
    ASSERT_NE(rasterNodataConfig, nullptr);
    EXPECT_TRUE(rasterNodataConfig->visibleKeys.count("nodata_value") > 0);
    EXPECT_TRUE(rasterNodataConfig->requiredKeys.count("input") > 0);

    const auto* matchingRegisterConfig =
        gis::gui::findActionUiConfig("matching", "register");
    ASSERT_NE(matchingRegisterConfig, nullptr);
    EXPECT_TRUE(matchingRegisterConfig->visibleKeys.count("transform") > 0);
    EXPECT_TRUE(matchingRegisterConfig->visibleKeys.count("resample") > 0);
    EXPECT_TRUE(matchingRegisterConfig->requiredKeys.count("output") > 0);

    const auto* matchingChangeConfig =
        gis::gui::findActionUiConfig("matching", "change");
    ASSERT_NE(matchingChangeConfig, nullptr);
    EXPECT_TRUE(matchingChangeConfig->visibleKeys.count("change_method") > 0);
    EXPECT_TRUE(matchingChangeConfig->visibleKeys.count("threshold") > 0);
    EXPECT_TRUE(matchingChangeConfig->requiredKeys.count("reference") > 0);

    const auto* matchingEccConfig =
        gis::gui::findActionUiConfig("matching", "ecc_register");
    ASSERT_NE(matchingEccConfig, nullptr);
    EXPECT_TRUE(matchingEccConfig->visibleKeys.count("ecc_motion") > 0);
    EXPECT_TRUE(matchingEccConfig->visibleKeys.count("ecc_iterations") > 0);
    EXPECT_TRUE(matchingEccConfig->requiredKeys.count("output") > 0);

    const auto* matchingCornerConfig =
        gis::gui::findActionUiConfig("matching", "corner");
    ASSERT_NE(matchingCornerConfig, nullptr);
    EXPECT_TRUE(matchingCornerConfig->visibleKeys.count("corner_method") > 0);
    EXPECT_TRUE(matchingCornerConfig->visibleKeys.count("max_corners") > 0);
    EXPECT_TRUE(matchingCornerConfig->requiredKeys.count("input") > 0);

    const auto* matchingStitchConfig =
        gis::gui::findActionUiConfig("matching", "stitch");
    ASSERT_NE(matchingStitchConfig, nullptr);
    EXPECT_TRUE(matchingStitchConfig->visibleKeys.count("stitch_confidence") > 0);
    EXPECT_TRUE(matchingStitchConfig->requiredKeys.count("output") > 0);

}

TEST(GuiSupportTest, ParamTextLookupReturnsCommonAndActionSpecificText) {
    const auto* commonText = gis::gui::findCommonParamText("input");
    ASSERT_NE(commonText, nullptr);
    EXPECT_FALSE(commonText->displayName.isEmpty());
    EXPECT_FALSE(commonText->description.isEmpty());
    EXPECT_EQ(commonText->displayName, QStringLiteral("输入文件"));
    EXPECT_EQ(commonText->description, QStringLiteral("输入数据路径；多文件场景请使用英文逗号分隔。"));

    const auto* referenceText = gis::gui::findCommonParamText("reference");
    ASSERT_NE(referenceText, nullptr);
    EXPECT_FALSE(referenceText->displayName.isEmpty());
    EXPECT_FALSE(referenceText->description.isEmpty());
    EXPECT_EQ(referenceText->displayName, QStringLiteral("参考文件"));

    const auto* dstSrsText = gis::gui::findCommonParamText("dst_srs");
    ASSERT_NE(dstSrsText, nullptr);
    EXPECT_FALSE(dstSrsText->displayName.isEmpty());
    EXPECT_FALSE(dstSrsText->description.isEmpty());
    EXPECT_EQ(dstSrsText->displayName, QStringLiteral("目标坐标系"));

    const auto* layerText = gis::gui::findCommonParamText("layer");
    ASSERT_NE(layerText, nullptr);
    EXPECT_FALSE(layerText->displayName.isEmpty());
    EXPECT_FALSE(layerText->description.isEmpty());

    const auto* clipVectorText = gis::gui::findCommonParamText("clip_vector");
    ASSERT_NE(clipVectorText, nullptr);
    EXPECT_FALSE(clipVectorText->displayName.isEmpty());
    EXPECT_FALSE(clipVectorText->description.isEmpty());

    const auto* whereText = gis::gui::findCommonParamText("where");
    ASSERT_NE(whereText, nullptr);
    EXPECT_FALSE(whereText->displayName.isEmpty());
    EXPECT_FALSE(whereText->description.isEmpty());

    const auto* xText = gis::gui::findCommonParamText("x");
    ASSERT_NE(xText, nullptr);
    EXPECT_FALSE(xText->displayName.isEmpty());
    EXPECT_FALSE(xText->description.isEmpty());

    const auto* yText = gis::gui::findCommonParamText("y");
    ASSERT_NE(yText, nullptr);
    EXPECT_FALSE(yText->displayName.isEmpty());
    EXPECT_FALSE(yText->description.isEmpty());

    const auto* srsText = gis::gui::findCommonParamText("srs");
    ASSERT_NE(srsText, nullptr);
    EXPECT_FALSE(srsText->displayName.isEmpty());
    EXPECT_FALSE(srsText->description.isEmpty());

    const auto* binsText = gis::gui::findCommonParamText("bins");
    ASSERT_NE(binsText, nullptr);
    EXPECT_FALSE(binsText->displayName.isEmpty());
    EXPECT_FALSE(binsText->description.isEmpty());

    const auto* srcSrsText = gis::gui::findCommonParamText("src_srs");
    ASSERT_NE(srcSrsText, nullptr);
    EXPECT_FALSE(srcSrsText->displayName.isEmpty());
    EXPECT_FALSE(srcSrsText->description.isEmpty());

    const auto* resampleText = gis::gui::findCommonParamText("resample");
    ASSERT_NE(resampleText, nullptr);
    EXPECT_FALSE(resampleText->displayName.isEmpty());
    EXPECT_FALSE(resampleText->description.isEmpty());

    const auto* bandText = gis::gui::findCommonParamText("band");
    ASSERT_NE(bandText, nullptr);
    EXPECT_FALSE(bandText->displayName.isEmpty());
    EXPECT_FALSE(bandText->description.isEmpty());

    const auto* methodText = gis::gui::findCommonParamText("method");
    ASSERT_NE(methodText, nullptr);
    EXPECT_FALSE(methodText->displayName.isEmpty());
    EXPECT_FALSE(methodText->description.isEmpty());

    const auto* matchMethodText = gis::gui::findCommonParamText("match_method");
    ASSERT_NE(matchMethodText, nullptr);
    EXPECT_FALSE(matchMethodText->displayName.isEmpty());
    EXPECT_FALSE(matchMethodText->description.isEmpty());

    const auto* nodataValueText = gis::gui::findCommonParamText("nodata_value");
    ASSERT_NE(nodataValueText, nullptr);
    EXPECT_FALSE(nodataValueText->displayName.isEmpty());
    EXPECT_FALSE(nodataValueText->description.isEmpty());

    const auto* changeMethodText = gis::gui::findCommonParamText("change_method");
    ASSERT_NE(changeMethodText, nullptr);
    EXPECT_FALSE(changeMethodText->displayName.isEmpty());
    EXPECT_FALSE(changeMethodText->description.isEmpty());

    const auto* eccMotionText = gis::gui::findCommonParamText("ecc_motion");
    ASSERT_NE(eccMotionText, nullptr);
    EXPECT_FALSE(eccMotionText->displayName.isEmpty());
    EXPECT_FALSE(eccMotionText->description.isEmpty());

    const auto* stitchConfidenceText = gis::gui::findCommonParamText("stitch_confidence");
    ASSERT_NE(stitchConfidenceText, nullptr);
    EXPECT_FALSE(stitchConfidenceText->displayName.isEmpty());
    EXPECT_FALSE(stitchConfidenceText->description.isEmpty());

    const auto* thresholdValueText = gis::gui::findCommonParamText("threshold_value");
    ASSERT_NE(thresholdValueText, nullptr);
    EXPECT_FALSE(thresholdValueText->displayName.isEmpty());
    EXPECT_FALSE(thresholdValueText->description.isEmpty());

    const auto* maxValueText = gis::gui::findCommonParamText("max_value");
    ASSERT_NE(maxValueText, nullptr);
    EXPECT_FALSE(maxValueText->displayName.isEmpty());
    EXPECT_FALSE(maxValueText->description.isEmpty());

    const auto* filterTypeText = gis::gui::findCommonParamText("filter_type");
    ASSERT_NE(filterTypeText, nullptr);
    EXPECT_FALSE(filterTypeText->displayName.isEmpty());
    EXPECT_FALSE(filterTypeText->description.isEmpty());

    const auto* kernelSizeText = gis::gui::findCommonParamText("kernel_size");
    ASSERT_NE(kernelSizeText, nullptr);
    EXPECT_FALSE(kernelSizeText->displayName.isEmpty());
    EXPECT_FALSE(kernelSizeText->description.isEmpty());

    const auto* sigmaText = gis::gui::findCommonParamText("sigma");
    ASSERT_NE(sigmaText, nullptr);
    EXPECT_FALSE(sigmaText->displayName.isEmpty());
    EXPECT_FALSE(sigmaText->description.isEmpty());

    const auto* enhanceTypeText = gis::gui::findCommonParamText("enhance_type");
    ASSERT_NE(enhanceTypeText, nullptr);
    EXPECT_FALSE(enhanceTypeText->displayName.isEmpty());
    EXPECT_FALSE(enhanceTypeText->description.isEmpty());

    const auto* clipLimitText = gis::gui::findCommonParamText("clip_limit");
    ASSERT_NE(clipLimitText, nullptr);
    EXPECT_FALSE(clipLimitText->displayName.isEmpty());
    EXPECT_FALSE(clipLimitText->description.isEmpty());

    const auto* gammaText = gis::gui::findCommonParamText("gamma");
    ASSERT_NE(gammaText, nullptr);
    EXPECT_FALSE(gammaText->displayName.isEmpty());
    EXPECT_FALSE(gammaText->description.isEmpty());

    const auto* edgeMethodText = gis::gui::findCommonParamText("edge_method");
    ASSERT_NE(edgeMethodText, nullptr);
    EXPECT_FALSE(edgeMethodText->displayName.isEmpty());
    EXPECT_FALSE(edgeMethodText->description.isEmpty());

    const auto* lowThresholdText = gis::gui::findCommonParamText("low_threshold");
    ASSERT_NE(lowThresholdText, nullptr);
    EXPECT_FALSE(lowThresholdText->displayName.isEmpty());
    EXPECT_FALSE(lowThresholdText->description.isEmpty());

    const auto* highThresholdText = gis::gui::findCommonParamText("high_threshold");
    ASSERT_NE(highThresholdText, nullptr);
    EXPECT_FALSE(highThresholdText->displayName.isEmpty());
    EXPECT_FALSE(highThresholdText->description.isEmpty());

    const auto* sobelDxText = gis::gui::findCommonParamText("sobel_dx");
    ASSERT_NE(sobelDxText, nullptr);
    EXPECT_FALSE(sobelDxText->displayName.isEmpty());
    EXPECT_FALSE(sobelDxText->description.isEmpty());

    const auto* sobelDyText = gis::gui::findCommonParamText("sobel_dy");
    ASSERT_NE(sobelDyText, nullptr);
    EXPECT_FALSE(sobelDyText->displayName.isEmpty());
    EXPECT_FALSE(sobelDyText->description.isEmpty());

    const auto* minAreaText = gis::gui::findCommonParamText("min_area");
    ASSERT_NE(minAreaText, nullptr);
    EXPECT_FALSE(minAreaText->displayName.isEmpty());
    EXPECT_FALSE(minAreaText->description.isEmpty());

    const auto* levelsText = gis::gui::findCommonParamText("levels");
    ASSERT_NE(levelsText, nullptr);
    EXPECT_FALSE(levelsText->displayName.isEmpty());
    EXPECT_FALSE(levelsText->description.isEmpty());

    const auto* actionText = gis::gui::findActionSpecificParamText(
        "classification", "feature_stats", "vector_output");
    ASSERT_NE(actionText, nullptr);
    EXPECT_EQ(actionText->displayName, QStringLiteral("分类面输出"));
    EXPECT_EQ(actionText->description, QStringLiteral("可选，输出分类面结果，当前仅支持 .gpkg。"));

    const auto* splitOutputText =
        gis::gui::findActionSpecificParamText("cutting", "split", "output");
    ASSERT_NE(splitOutputText, nullptr);
    EXPECT_EQ(splitOutputText->displayName, QStringLiteral("输出目录"));
    EXPECT_EQ(splitOutputText->description, QStringLiteral("分块输出目录，图块会自动命名为 tile_x_y.tif。"));

    const auto* templateFileText =
        gis::gui::findActionSpecificParamText("processing", "template_match", "template_file");
    ASSERT_NE(templateFileText, nullptr);
    EXPECT_EQ(templateFileText->displayName, QStringLiteral("模板文件"));
    EXPECT_EQ(templateFileText->description, QStringLiteral("模板影像路径，尺寸需小于等于输入影像。"));

    const auto* reprojectInputText =
        gis::gui::findActionSpecificParamText("projection", "reproject", "input");
    ASSERT_NE(reprojectInputText, nullptr);
    EXPECT_EQ(reprojectInputText->displayName, QStringLiteral("输入文件"));
    EXPECT_EQ(reprojectInputText->description, QStringLiteral("支持栅格或矢量数据，输出格式由输出后缀决定。"));

    const auto* mergeBandsInputText =
        gis::gui::findActionSpecificParamText("cutting", "merge_bands", "input");
    ASSERT_NE(mergeBandsInputText, nullptr);
    EXPECT_EQ(mergeBandsInputText->displayName, QStringLiteral("输入文件"));
    EXPECT_EQ(mergeBandsInputText->description, QStringLiteral("可填写一个或多个单波段栅格路径，使用英文逗号分隔。"));

    const auto* mergeBandsBandsText =
        gis::gui::findActionSpecificParamText("cutting", "merge_bands", "bands");
    ASSERT_NE(mergeBandsBandsText, nullptr);
    EXPECT_EQ(mergeBandsBandsText->displayName, QStringLiteral("波段列表"));
    EXPECT_EQ(mergeBandsBandsText->description, QStringLiteral("补充更多单波段栅格路径，使用英文逗号分隔。"));

    EXPECT_EQ(
        gis::gui::findActionSpecificParamText("processing", "gabor_filter", "missing_param"),
        nullptr);
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


