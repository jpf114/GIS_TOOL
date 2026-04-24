#include <gtest/gtest.h>

#include "../src/gui/gui_data_support.h"

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
        "当前预览: 矢量 | 缩放: 摘要模式 | 模式: 属性摘要");
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
