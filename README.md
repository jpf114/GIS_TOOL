# GIS Tool

基于 `C++17 + GDAL + OpenCV + PROJ + Qt` 的插件式 GIS 工具集，提供 CLI 和 GUI 两种入口。

## 当前状态

- 当前版本：`v0.1.3`
- 标准构建目录：
  - `build/debug`
  - `build/release`
- 默认安装与交付：`Release`
- 依赖统一复用全局 `vcpkg`
  - 当前项目约定路径：`D:\Develop\vcpkg`

## 主功能

- 投影转换
- 影像裁切与镶嵌
- 特征匹配与配准
- 影像处理与分析
- 分类统计
- 几何校正与辐射处理
- 地形分析
- 光谱指数
- 栅格工具
- 矢量数据处理

## 插件与主功能映射

- `projection` -> 投影转换
- `cutting` -> 影像裁切与镶嵌
- `matching` -> 特征匹配与配准
- `processing` -> 影像处理与分析
- `classification` -> 分类统计
- `georef` -> 几何校正与辐射处理
- `terrain` -> 地形分析
- `spindex` -> 光谱指数
- `raster_manage / raster_inspect / raster_render / raster_math` -> 栅格工具
- `vector` -> 矢量数据处理

## GUI 主功能归并

- GUI 左侧导航已将 `raster_manage / raster_inspect / raster_render / raster_math` 合并为单一“栅格工具”主项
- `classification` 主项统一使用“分类统计”，监督分类动作为其子功能
- GUI 与 CLI 最终都落到同一套插件执行链

## 当前已补齐的重点能力

### processing

- `gabor_filter`
- `glcm_texture`
- `mean_shift_segment`
- `skeleton`
- `connected_components`
- `pansharpen`

### classification

- `feature_stats`
- `svm_classify`
- `random_forest_classify`
- `max_likelihood_classify`

### georef

- `dos_correction`
- `radiometric_calibration`
- `gcp_register`
- `cosine_correction`
- `minnaert_correction`
- `c_correction`
- `quac_correction`
- `rpc_orthorectify`

### projection

- `info`
- `transform`
- `assign_srs`
- `reproject`

### cutting

- `clip`
- `mosaic`
- `split`
- `merge_bands`

### vector

- `intersect`
- `simplify`
- `repair`
- `geom_metrics`
- `nearest`
- `spatial_join`
- `adjacency`
- `overlap_check`
- `topology_check`
- `convex_hull`
- `centroid`
- `envelope`
- `boundary`
- `multipart_check`
- `singlepart`
- `vertices_extract`
- `endpoints_extract`
- `midpoints_extract`
- `interior_point`
- `duplicate_point_check`
- `hole_check`
- `dangling_endpoint_check`
- `sliver_remove`

## 完整性说明

以上重点能力当前都已经按既有项目标准补到以下层级：

- 底层算法 / 插件实现
- CLI 调用链路
- GUI 接入
- GUI 参数校验
- 插件测试
- GUI support 测试
- GUI 离屏回归
- 真实数据专项回归
- Release 安装与启动验证

## 构建

### Debug

```powershell
cmake -S . -B build/debug -G "Visual Studio 17 2022" -A x64 -DGIS_BUILD_GUI=ON -DGIS_BUILD_TESTS=ON
cmake --build build/debug --config Debug
ctest --test-dir build/debug -C Debug --output-on-failure
```

### Release

```powershell
cmake -S . -B build/release -G "Visual Studio 17 2022" -A x64 -DGIS_BUILD_GUI=ON -DGIS_BUILD_TESTS=ON
cmake --build build/release --config Release
cmake --install build/release --config Release
```

约定：

- 日常开发验证使用 `build/debug`
- 默认安装使用 `build/release`
- 发布前至少执行一次 `Debug 全量测试 + Release 编译安装`

## 真实数据专项回归

### Debug

```powershell
cmake --build build/debug --config Debug --target real_matching_regression
cmake --build build/debug --config Debug --target real_matching_regression_full
cmake --build build/debug --config Debug --target real_raster_regression
cmake --build build/debug --config Debug --target real_raster_regression_full
cmake --build build/debug --config Debug --target real_vector_regression
cmake --build build/debug --config Debug --target real_vector_regression_full
```

### Release

```powershell
cmake --build build/release --config Release --target real_matching_regression
cmake --build build/release --config Release --target real_matching_regression_full
cmake --build build/release --config Release --target real_raster_regression
cmake --build build/release --config Release --target real_raster_regression_full
cmake --build build/release --config Release --target real_vector_regression
cmake --build build/release --config Release --target real_vector_regression_full
```

当前已经确认通过的专项包括：

- `matching`
  - `detect / corner / match / register / change`
  - Release 追加：`ecc_register / stitch`
- `projection`
  - `info / transform / assign_srs / reproject`
- `cutting`
  - `clip / mosaic / split / merge_bands`
- `processing`
  - `pansharpen / gabor_filter / glcm_texture / mean_shift_segment / skeleton / connected_components`
- `classification`
  - `feature_stats / svm_classify / random_forest_classify / max_likelihood_classify`
  - `full` 追加：`feature_stats_csv`
- `georef`
  - `dos_correction / radiometric_calibration / gcp_register / cosine_correction / minnaert_correction / c_correction / quac_correction / rpc_orthorectify`
- `spindex`
  - `ndvi / ndmi / evi / evi2 / savi / osavi / gndvi / ndwi / mndwi / ndbi / bsi / arvi / nbr / awei / ui / bi / custom_index`
- `terrain`
  - 当前已实现动作的真实数据回归链路
- `vector`
  - 当前主链动作的真实数据回归链路

当前 `real_raster_regression` 的重点验收口径为：

- `classification.feature_stats`
  - `quick`：验证 `json / vector_output / raster_output`
  - `full`：追加 `csv`
  - 额外校验 `actual_srs` 与 `__summary__`
- `classification.svm_classify / random_forest_classify / max_likelihood_classify`
  - 验证输出尺寸 `24 x 12 x 1`
  - 验证输出类型 `Float32`
  - 验证类别范围 `1~2`
- `projection.info / transform / assign_srs / reproject`
  - 验证尺寸、EPSG 编码、坐标转换结果与重投影输出
- `cutting.clip / mosaic / split / merge_bands`
  - 验证输出尺寸、瓦片数量、波段数量与关键统计值
- `processing.pansharpen`
  - 固定验证 `pan_method=simple_mean`
- `processing.gabor_filter / glcm_texture / mean_shift_segment`
  - 验证输出尺寸 `32 x 32 x 1`
  - 验证输出类型 `Float32`
- `processing.skeleton / connected_components`
  - 验证输出尺寸 `64 x 64 x 1`
  - `skeleton` 校验最大值 `255`
  - `connected_components` 校验最大标签值 `4`
- `georef`
  - 8 个动作固定校验输出尺寸、输出类型、CRS 或关键统计值
- `spindex`
  - 固定验证主流指数输出
  - `custom_index` 使用 `preset=ndvi_alias / ndmi_alias` 作为稳定验收入口
- `terrain`
  - 当前已额外校验 `slope / profile_extract / viewshed_multi` 等关键结果

## 使用

### 列出插件

```powershell
.\install\bin\gis-cli.exe --list
```

### 运行算法

```powershell
.\install\bin\gis-cli.exe <plugin> <action> --input <path> --output <path>
```

### 启动 GUI

```powershell
.\install\bin\gis-gui.exe
```

## 文档

- 算法总览：[docs/算法说明/总览.md](/D:/Develop/GIS/GIS_TOOL/docs/算法说明/总览.md)
- 对齐验证清单：[docs/GUI_CLI_底层对齐验证清单.md](/D:/Develop/GIS/GIS_TOOL/docs/GUI_CLI_底层对齐验证清单.md)
- 后续工作计划：[docs/后续工作计划.md](/D:/Develop/GIS/GIS_TOOL/docs/后续工作计划.md)

## 说明

- 文档统一使用中文
- 提交信息统一使用中文
- 当前阶段优先保证简单、可维护、可回归
- `pointcloud` 当前不计入已完成主模块，相关依赖条件尚未满足
