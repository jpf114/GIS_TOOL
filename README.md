# GIS Tool

基于 `C++17 + GDAL + OpenCV + PROJ + Qt` 的插件式 GIS 工具集，提供 CLI 和 GUI 两种入口。

## 当前状态

- 当前版本：`v0.1.3`
- 标准构建目录：
  - `build/debug`
  - `build/release`
- 默认安装与交付：`Release`
- 依赖统一使用全局 `vcpkg`，当前项目约定路径为 `D:\Develop\vcpkg`

## 当前主功能

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

当前底层插件与主功能映射：

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

GUI 当前已完成的主功能归并：

- 左侧导航已将 `raster_manage / raster_inspect / raster_render / raster_math` 合并为单一“栅格工具”主项
- 子功能点击时会自动切回对应真实插件，底层参数与执行链路保持不变
- `classification` 主项统一使用“分类统计”，监督分类动作继续作为其子功能

## 本轮新增并已接入的功能

### processing

- `gabor_filter` Gabor 滤波
- `glcm_texture` GLCM 纹理
- `mean_shift_segment` Mean Shift 分割
- `skeleton` 骨架提取
- `connected_components` 连通组件

### classification

- `svm_classify` SVM 分类
- `random_forest_classify` 随机森林分类
- `max_likelihood_classify` 最大似然分类

### georef

- `dos_correction` DOS 大气校正
- `radiometric_calibration` 辐射定标
- `gcp_register` 控制点配准
- `cosine_correction` 余弦校正
- `minnaert_correction` Minnaert 校正
- `c_correction` C 校正
- `quac_correction` QUAC 大气校正
- `rpc_orthorectify` RPC 正射校正

### raster_tools

- `band_math` 波段运算
- `cog` COG 生成
- `histogram_match` 直方图匹配

### vector

- `intersect` 交集
- `simplify` 简化
- `repair` 修复
- `geom_metrics` 几何属性
- `nearest` 最近邻
- `spatial_join` 空间连接
- `adjacency` 邻接关系
- `overlap_check` 重叠检查
- `topology_check` 拓扑检查
- `convex_hull` 凸包
- `centroid` 质心
- `envelope` 外包矩形
- `boundary` 边界提取
- `multipart_check` 多部件检查
- `singlepart` 转单部件
- `vertices_extract` 顶点提取
- `endpoints_extract` 端点提取
- `midpoints_extract` 中点提取
- `interior_point` 内部点
- `duplicate_point_check` 重复点检查
- `hole_check` 孔洞检查
- `dangling_endpoint_check` 悬挂端点检查
- `sliver_remove` 碎片面消除

## 完整性说明

以上新增功能都按当前项目既有模式补齐到以下层级：

- 底层算法实现
- 插件参数定义
- CLI 调用链路
- GUI 动作接入
- GUI 参数面板说明
- GUI 建议输出路径
- GUI 参数校验
- 插件测试
- GUI 支撑测试
- GUI 离屏回归测试
- 算法说明文档

说明：

- CLI 使用统一插件驱动入口，不为每个算法单独维护一套命令程序
- 自动化测试以合成数据和离屏 GUI 回归为主，适合当前阶段持续迭代验证

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

### 真实数据专项回归

`Debug`：

```powershell
cmake --build build/debug --config Debug --target real_matching_regression
cmake --build build/debug --config Debug --target real_matching_regression_full
cmake --build build/debug --config Debug --target real_raster_regression
cmake --build build/debug --config Debug --target real_raster_regression_full
```

`Release`：

```powershell
cmake --build build/release --config Release --target real_matching_regression
cmake --build build/release --config Release --target real_matching_regression_full
cmake --build build/release --config Release --target real_raster_regression
cmake --build build/release --config Release --target real_raster_regression_full
```

当前已经确认通过的专项包括：

- `matching`：`detect / corner / match / register / change`
- `matching` Release 追加：`ecc_register / stitch`
- `processing`：`pansharpen / gabor_filter / glcm_texture / mean_shift_segment`
- `classification`：`feature_stats`，以及 `full` 模式下的 `feature_stats_csv`
- `spindex`：`ndvi / ndmi / evi / evi2 / savi / osavi / gndvi / ndwi / mndwi / ndbi / bsi / arvi / nbr / awei / ui / bi / custom_index`
- `terrain`：当前已实现动作的真实数据回归链路

当前 `real_raster_regression` 中这 3 类重点模块的专项验收范围为：

- `classification.feature_stats`
  - `quick`：验证 `json / vector_output / raster_output`
  - `full`：在 `quick` 基础上追加 `csv` 输出
  - 当前已额外校验 `actual_srs` 与 `__summary__` 汇总记录
- `processing.pansharpen`
  - 当前固定验证 `pan_method=simple_mean`
  - 输入由真实样例或辅助程序生成的多光谱 / 全色配对数据提供
- `processing.gabor_filter / glcm_texture / mean_shift_segment`
  - 已纳入 Debug / Release `real_raster_regression quick`
  - 当前固定验证输出尺寸 `32 x 32 x 1` 与输出类型 `Float32`
  - 当前已额外校验输出为 `30 x 30 x 3`，并校验三波段统计值
- `spindex`
  - 当前固定验证 `ndvi / evi / savi / gndvi / ndwi / mndwi / ndbi / arvi / nbr / awei / ui / bi / custom_index`
  - `custom_index` 当前使用 `preset=ndvi_alias / ndmi_alias` 作为稳定验收入口
  - 当前已额外校验 `ndvi` 输出尺寸与均值
- `terrain`
  - 当前已额外校验 `slope / profile_extract / viewshed_multi` 的关键结构或统计结果

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

## 算法文档

算法说明总览见 [docs/算法说明/总览.md](/D:/Develop/GIS/GIS_TOOL/docs/算法说明/总览.md)。

## 说明

- 文档统一使用中文
- 提交信息统一使用中文
- 当前项目优先保持简单、可维护、可持续扩展
