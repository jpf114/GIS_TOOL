# classification 算法移植计划

## 目标

把 `D:\Code\MyProject\FeatureStatistics` 的核心分类统计算法移植到 `GIS_TOOL`，并整理成更符合当前产品结构的插件形态：

- 主功能插件：`classification`
- 当前子功能：`feature_stats`
- GUI 展示：
  - 主功能：`分类统计`
  - 子功能：`地物分类统计`

## 原则

- 只移植算法和必要数据结构
- 不移植 `FeatureStatistics` 的独立工程壳
- 优先复用 `GIS_TOOL` 的插件、参数、结果和测试体系
- 结构上保持可扩展，便于后续继续增加其他分类统计子功能

## 当前结构

### 主功能

- 插件名：`classification`
- 显示名：`分类统计`

### 子功能

- `feature_stats`
  - 显示名：`地物分类统计`
  - 作用：按面要素范围对多源分类栅格执行优先级统计，并输出统计结果

## 已完成阶段

### 阶段 1：算法主链移植

已完成：

1. 矢量读取与属性提取
2. 栅格读取与检查
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

### 阶段 2：结果输出

已完成：

- 统计表输出 `json`
- 统计表输出 `csv`
- 分类面输出 `vector_output`
- 分类栅格输出 `raster_output`

状态：已完成

### 阶段 3：工程接入

已完成：

- 顶层 CMake 接入 `src/plugins/classification`
- CLI 插件依赖接入
- tests 插件依赖接入
- GUI 插件依赖接入
- 插件命名从 `feature_stats` 重构为 `classification`
- 子功能命名从 `run` 重构为 `feature_stats`

状态：已完成

### 阶段 4：GUI 结构对齐

已完成：

- 新增主功能 `分类统计`
- 子功能显示为 `地物分类统计`
- 增加该主功能的侧边栏图标
- 增加 `feature_stats` 子功能文案配置
- 增加相关参数中文说明

状态：已完成

### 阶段 5：测试修正

已完成：

- 插件查找从 `feature_stats` 改为 `classification`
- 执行动作从 `run` 改为 `feature_stats`
- 保持原有核心回归测试有效
- 保持附加输出测试有效

当前覆盖：

- `FeatureStatsPluginParams`
- `FeatureStatsRunResolvesProjectedTargetSrsAndFinestGrid`
- `FeatureStatsRunRejectsGeographicInputsWithoutTargetEpsg`
- `FeatureStatsRunWritesPriorityStatisticsJson`
- `FeatureStatsRunWritesVectorAndRasterOutputs`

状态：已完成

## 当前参数

当前子功能 `feature_stats` 使用以下参数：

- `action`
  - 固定为 `feature_stats`
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
  - 可选分类面输出路径，建议 `.gpkg`
- `raster_output`
  - 可选分类栅格输出路径，建议 `.tif`

## 下一步

下一步不再调整主结构，优先做两类工作：

1. 编译并验证 GUI 接入后的完整链路
2. 后续如需扩展，继续在 `classification` 下增加新的分类统计子功能，而不是再新增平级插件

## 当前结论

现在的结构已经从“单一算法插件”整理成了“分类统计主功能 + 地物分类统计子功能”的产品结构，和现有 GUI 的插件 / action 模型是一致的。
