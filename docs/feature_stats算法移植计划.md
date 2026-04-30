# feature_stats 算法移植计划

## 目标

把 `D:\Code\MyProject\FeatureStatistics` 的核心分类统计算法移植到 `GIS_TOOL`，采用 `GIS_TOOL` 插件体系落地。

本次只做算法移植，不做原工程整体移植。

## 原则

- 只移植算法和必要数据结构
- 优先复用 `GIS_TOOL` 现有插件框架、GDAL 环境、参数体系、结果体系
- 先完成最小可用链路，再逐步补附加输出和 GUI
- 过程允许按实际实现情况修正计划，但每一步都要保持可编译、可验证

## 分阶段计划

### 阶段 1：确定插件边界

目标：

- 新建插件 `feature_stats`
- 输入面矢量、分类映射、多栅格
- 输出统计结果

不移植：

- `FeatureStatistics` 独立 CLI
- `FeatureStatistics` 独立 DLL / 工程壳
- 原项目的配置驱动方式

状态：已完成

### 阶段 2：重建内部任务模型

目标：

- 建立 `RasterTaskInput`
- 建立 `FeatureStatsTask`
- 建立目标坐标系、目标网格、窗口、记录等结构
- 让插件参数直接驱动算法执行

状态：已完成

### 阶段 3：迁移核心统计算法主链

目标：

1. 矢量读取与属性提取
2. 栅格读取与 SRS / 分辨率检查
3. 目标坐标系决策
4. 目标网格决策
5. 要素重投影
6. 要素窗口计算
7. 面掩膜生成
8. 窗口级重采样
9. 多栅格优先级分类
10. 像元数 / 面积 / 占比统计
11. 汇总记录生成

状态：已完成

### 阶段 4：接入 GIS_TOOL 插件体系

目标：

- 接入 `src/plugins/feature_stats`
- 接入顶层 `CMakeLists.txt`
- 接入 CLI 插件发现链路
- 接入测试依赖链路

状态：已完成

### 阶段 5：第一批结果输出

目标：

- `json`
- `csv`

字段：

- `feature_id`
- `feature_name`
- `class_value`
- `class_name`
- `pixel_count`
- `area`
- `ratio`
- `actual_srs`
- `area_unit`

状态：已完成

### 阶段 6：第二批附加输出

目标：

- 分类面输出 `vector_output`
- 分类栅格输出 `raster_output`

当前实现：

- `vector_output` 生成 `.gpkg` 分类面
- `raster_output` 生成 `.tif` 分类栅格
- 输出元数据已回填到插件结果 `metadata`

状态：已完成

### 阶段 7：测试补齐

目标：

1. 参数定义测试
2. 坐标系与网格决策测试
3. 优先级分类统计测试
4. 分类面输出测试
5. 分类栅格输出测试

已完成：

- `FeatureStatsPluginParams`
- `FeatureStatsRunResolvesProjectedTargetSrsAndFinestGrid`
- `FeatureStatsRunRejectsGeographicInputsWithoutTargetEpsg`
- `FeatureStatsRunWritesPriorityStatisticsJson`
- `FeatureStatsRunWritesVectorAndRasterOutputs`

已验证命令：

```powershell
cmake --build build --config Debug --target gis_tests
D:\Code\MyProject\GIS_TOOL\build\tests\Debug\gis_tests.exe --gtest_filter=*FeatureStats*
```

状态：已完成

### 阶段 8：GUI 接入

目标：

- GUI 展示 `feature_stats` 参数
- 补充新增参数 `vector_output` / `raster_output`
- 让 GUI 侧能直接发起统计任务

状态：未开始

## 当前插件参数

- `action`
  - 当前固定为 `run`
- `vector`
  - 输入面矢量路径
- `feature_id_field`
  - 要素 ID 字段名
- `feature_name_field`
  - 要素名称字段名
- `class_map`
  - 分类映射 JSON 路径
- `rasters`
  - 多栅格路径，逗号分隔
- `bands`
  - 波段列表，逗号分隔
- `nodatas`
  - NoData 列表，逗号分隔
- `target_epsg`
  - 可选目标 EPSG
- `output`
  - 统计结果输出路径，支持 `.json` / `.csv`
- `vector_output`
  - 可选分类面输出路径，当前建议 `.gpkg`
- `raster_output`
  - 可选分类栅格输出路径，当前建议 `.tif`

## 当前结论

`FeatureStatistics` 的核心算法已经在 `GIS_TOOL` 内形成独立插件主链，现阶段 CLI 和测试侧已可用。

接下来优先进入 GUI 接入，而不是继续扩展新的算法分支。
