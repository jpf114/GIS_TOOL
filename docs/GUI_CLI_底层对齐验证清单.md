# GUI / CLI / 底层对齐验证清单

## 1. 目的

本文用于收口当前项目在 `GUI / CLI / 底层算法插件` 三层上的对齐状态，给出已经完成的验证证据、当前可确认结论、已知边界，以及后续是否还存在必须继续补测的硬缺口。

结论以 `2026-05-02` 当天在当前工作区重新执行得到的结果为准。

## 2. 当前结论

当前项目已经达到如下状态：

- 底层 `core / framework / plugins` 主链路可用，且全量自动化测试通过。
- `CLI` 插件加载、参数解析、动作执行链路已重新验证通过。
- `GUI` 支撑逻辑、参数校验、离屏自动执行回归已重新验证通过。
- 当前主功能在 `CLI` 与 `GUI` 间已基本对齐，二者共同落到同一套底层插件执行路径。
- 项目当前处于“高强度验证后可控”的状态，可以认为基本工作已经接近收口。

不应表述为：

- “100% 绝对零风险”
- “所有模块都已经过外部真实生产数据完整验证”

## 3. 新鲜验证证据

### 3.1 全量自动化测试

执行命令：

```powershell
ctest --test-dir build/debug -C Debug --output-on-failure
```

结果：

- `337/337` 通过

这组结果覆盖：

- `core` 基础能力
- `framework` 参数与插件管理
- 各插件单元/集成测试
- CLI 相关测试
- GUI support 测试
- GUI offscreen 回归测试

### 3.2 GUI 支撑与离屏回归

已包含在上述全量套件中，当前结论为：

- GUI support 测试通过
- GUI offscreen 回归通过
- GUI 当前自动化覆盖总量为 `172` 项
  - `61` 项 `GuiSupportTest`
  - `112` 项 `gui_` 离屏回归

### 3.3 CLI 插件加载验证

已重新验证：

```powershell
build\debug\src\cli\Debug\gis-cli.exe --list
```

结果：

- 可正常列出全部主插件
- 说明 `CLI -> 插件发现 -> 插件加载` 链路当前可用

### 3.4 真实矢量回归脚本

已重新执行：

```powershell
powershell.exe -ExecutionPolicy Bypass -File tests/run_real_vector_regression.ps1 -CliPath build\debug\src\cli\Debug\gis-cli.exe -WorkspaceRoot . -OutputRoot tmp\vector_regression -Mode quick

powershell.exe -ExecutionPolicy Bypass -File tests/run_real_vector_regression.ps1 -CliPath build\debug\src\cli\Debug\gis-cli.exe -WorkspaceRoot . -OutputRoot tmp\vector_regression_full -Mode full
```

结果：

- `quick` 通过
- `full` 通过

说明：

- 回归脚本会在 `tmp/vector_regression` 或 `tmp/vector_regression_full` 下生成摘要文件
- 这些文件属于验证产物，可按上面的命令随时重新生成，不必纳入仓库跟踪
- 当前 `real_vector_regression` 已不只验证“命令成功 + 文件存在”，还补充了代表性结果断言：
  - `buffer` 校验 `feature_count` 与 `srs_type`
  - `clip / difference / union / intersect / dissolve / polygonize` 校验 CLI 输出中的结果数量
  - `convert` 校验 `GeoJSON FeatureCollection` 结构与要素数
  - `rasterize` 追加校验输出栅格尺寸、坐标系与统计值

## 4. 三层对齐状态

| 层级 | 当前状态 | 主要证据 | 说明 |
| --- | --- | --- | --- |
| 底层 `core` | 通过 | `ctest 337/337` | GDAL/OpenCV/PROJ 包装、运行时环境、基础能力可用 |
| 底层 `framework` | 通过 | `ctest 337/337` | 参数类型、校验、插件管理器、CLI 参数解析链路通过 |
| 插件层 `plugins/*` | 通过 | 插件测试 + GUI/CLI 回归 | 主执行链已验证 |
| `CLI` 入口 | 通过 | `gis-cli --list` + 全量测试 + real vector regression | 可加载插件并执行主要动作 |
| `GUI` 入口 | 通过 | `GuiSupportTest` + `gui_` offscreen 回归 | 参数 UI 支撑与自动执行链路可用 |
| `GUI / CLI / 底层` 对齐 | 基本对齐 | GUI 和 CLI 共用插件执行路径 | 当前没有发现明显“GUI 可配但底层不可跑”或“CLI 可跑但 GUI 不可触达”的主链路缺口 |

## 5. 功能覆盖矩阵

下表描述的是“当前已纳入自动回归并有证据支撑”的状态。

| 模块 | 动作 | 底层/插件测试 | CLI 证据 | GUI 证据 | 结论 |
| --- | --- | --- | --- | --- | --- |
| `vector` | `info` `filter` `buffer` `clip` `rasterize` `polygonize` `convert` `union` `difference` `intersect` `dissolve` `simplify` `repair` `geom_metrics` `nearest` `spatial_join` `adjacency` `overlap_check` `topology_check` `convex_hull` `centroid` `envelope` `boundary` `multipart_check` `singlepart` `vertices_extract` `endpoints_extract` `midpoints_extract` `interior_point` `duplicate_point_check` `hole_check` `dangling_endpoint_check` `sliver_remove` | 有 | 有 | 有 | 主链路已对齐 |
| `projection` | `reproject` `info` `transform` `assign_srs` | 有 | 有 | 有 | 主链路已对齐 |
| `spindex` | `ndvi` `ndwi` `custom_index` | 有 | 有 | 有 | 主链路已对齐 |
| `raster_manage / raster_inspect / raster_render / raster_math` | `info` `histogram` `colormap` `histogram_match` `nodata` `overviews` `band_math` `cog` | 有 | 有 | 有 | GUI 已统一归并到“栅格工具”主项 |
| `classification` | `feature_stats` `svm_classify` `random_forest_classify` `max_likelihood_classify` | 有 | 有 | 有 | 主链路已对齐 |
| `processing` | `threshold` `filter` `enhance` `stats` `edge` `contour` `template_match` `pansharpen` `hough` `watershed` `kmeans` `gabor_filter` `glcm_texture` `mean_shift_segment` `skeleton` `connected_components` | 有 | 有 | 有 | 主链路已对齐 |
| `matching` | `detect` `corner` `match` `change` `register` | 有 | 有 | 有 | 主链路已对齐 |
| `cutting` | `clip` `mosaic` `split` `merge_bands` | 有 | 有 | 有 | 主链路已对齐 |
| `georef` | `dos_correction` `radiometric_calibration` `gcp_register` `cosine_correction` `minnaert_correction` `c_correction` `quac_correction` `rpc_orthorectify` | 有 | 有 | 有 | 主链路已对齐 |
| `terrain` | `slope` `aspect` `hillshade` `tpi` `curvature` `profile_curvature` `plan_curvature` `tri` `roughness` `fill_sinks` `flow_direction` `flow_accumulation` `stream_extract` `watershed` `profile_extract` `viewshed` `viewshed_multi` `cut_fill` `reservoir_volume` | 有 | 有 | 有 | 主链路已对齐 |

补充说明：

- `spindex` 当前已有 `ndvi / ndwi / ndmi / evi2 / bsi / custom_index` 的 GUI 证据，并已纳入真实数据专项的 `ndvi / ndmi / evi / evi2 / savi / gndvi / ndwi / mndwi / ndbi / bsi / arvi / nbr / awei / ui / bi / custom_index`
- `classification.feature_stats` 当前真实数据专项中，`quick` 已覆盖 `json / vector_output / raster_output`，`full` 追加 `csv`
- `processing.pansharpen` 当前真实数据专项固定验证 `pan_method=simple_mean`
- `real_raster_regression` 当前也已补充关键结果断言，而不只是检查输出文件存在：
  - `spindex.ndvi / ndmi / bsi / evi2` 校验关键输出统计
  - `classification.feature_stats` 校验 `actual_srs` 与 `__summary__` 汇总记录
  - `processing.pansharpen` 校验输出为 `30 x 30 x 3`，并校验三波段统计值
  - `terrain.profile_extract / terrain.slope / terrain.viewshed_multi / terrain.profile_curvature / terrain.plan_curvature / terrain.tri` 校验关键结构或统计结果

## 6. GUI 当前回归覆盖说明

GUI 当前已覆盖的离屏回归共 `112` 项，包含：

- `vector`
  - `info`
  - `filter`
  - `buffer`
  - `clip`
  - `rasterize`
  - `polygonize`
  - `convert`
  - `union`
  - `difference`
  - `intersect`
  - `dissolve`
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
- `projection`
  - `reproject`
  - `info`
  - `transform`
  - `assign_srs`
- `spindex`
  - `ndvi`
  - `ndwi`
  - `custom_index`
- `栅格工具`
  - `histogram`
  - `colormap`
  - `histogram_match`
  - `nodata`
  - `overviews`
  - `info`
  - `band_math`
  - `cog`
- `classification`
  - `feature_stats`
  - `svm_classify`
  - `random_forest_classify`
  - `max_likelihood_classify`
- `processing`
  - `threshold`
  - `filter`
  - `enhance`
  - `stats`
  - `edge`
  - `contour`
  - `template_match`
  - `pansharpen`
  - `hough`
  - `watershed`
  - `kmeans`
  - `gabor_filter`
  - `glcm_texture`
  - `mean_shift_segment`
  - `skeleton`
  - `connected_components`
- `matching`
  - `detect`
  - `corner`
  - `match`
  - `change`
  - `register`
  - `ecc_register_debug_failure`
  - `stitch_debug_failure`
- `cutting`
  - `clip`
  - `mosaic`
  - `split`
  - `merge_bands`
- `georef`
  - `dos_correction`
  - `radiometric_calibration`
  - `gcp_register`
  - `cosine_correction`
  - `minnaert_correction`
  - `c_correction`
  - `quac_correction`
  - `rpc_orthorectify`
- `terrain`
  - `slope`
  - `aspect`
  - `hillshade`
  - `tpi`
  - `curvature`
  - `roughness`
  - `fill_sinks`
  - `flow_direction`
  - `flow_accumulation`
  - `watershed`
  - `stream_extract`
  - `profile_extract`
  - `viewshed`
  - `viewshed_multi`
  - `cut_fill`
  - `reservoir_volume`

另外，`112` 项离屏回归总数中还包含以下非“成功动作输出”场景：

- `gui_smoke_startup`
- `gui_invalid_param_fast_fail_offscreen`
- `gui_vector_buffer_geographic_failure_offscreen`
- `gui_matching_ecc_register_debug_failure_offscreen`
- `gui_matching_stitch_debug_failure_offscreen`

## 7. 已知边界与注意事项

### 7.1 Debug 轻量构建中的预期失败项

以下两项当前是“预期失败验证通过”，不是“功能在当前 Debug 轻量构建下成功运行”：

- `gui_matching_ecc_register_debug_failure_offscreen`
- `gui_matching_stitch_debug_failure_offscreen`

这说明：

- 当前回归已验证它们在该构建形态下会按预期快速失败
- 但不能据此表述为这两项在当前 Debug 轻量环境里已成功跑通

### 7.2 真实矢量回归的数据来源边界

本次工作区下不存在 `data/vector` 外部真实矢量数据目录，因此本轮 `real_vector_regression` 使用的是脚本自动生成的可复现样例数据，而不是外部提供的生产实测数据。

因此当前能确认的是：

- `CLI + 插件 + 回归脚本` 执行链已跑通
- 常见矢量操作流程可稳定执行

当前不能据此直接扩大表述为：

- “全部矢量能力都已经用外部真实业务数据完整验证”

### 7.3 关于 GUI 改动范围

本阶段新增或调整的重点主要是：

- GUI 支撑逻辑
- 自动化参数校验与状态回传
- GUI 回归基础设施

当前没有证据表明本轮为了通过回归而改动了底层算法实现本身。

## 8. 本轮收尾时补的关键修正

本轮最后修正了：

- [tests/run_real_vector_regression.ps1](/D:/Develop/GIS/GIS_TOOL/tests/run_real_vector_regression.ps1)
- [tests/run_real_raster_regression.ps1](/D:/Develop/GIS/GIS_TOOL/tests/run_real_raster_regression.ps1)

修正内容：

- 真实矢量回归脚本同步插件产物时，改为按 `gis-cli.exe` 所在配置目录匹配对应插件产物
- 修正脚本中构建根目录回溯层级，避免把错误配置的插件覆盖到 `CLI` 的 `Debug/plugins`

修正后的直接效果：

- `real_vector_regression quick / full` 可重新稳定执行
- `gis-cli --list` 当前可正常列出全部插件
- 对 “CLI / 插件 / 回归脚本” 这条链路的证据更完整

## 9. 是否还存在必须继续啃的硬缺口

以当前目标来看，已经没有明显必须继续啃完才能说“基本可收口”的硬缺口。

当前更像是两类后续工作：

- 进一步引入外部真实业务数据，补更强场景验证
- 按需要继续扩展 GUI 交互层体验，而不是为了补主功能断链

如果只针对“基础算法模块没有明显问题、CLI 没明显问题、GUI 没明显问题、三者已同步”这个目标，当前已经基本达成。

## 10. 收口判断

当前可以使用如下表述：

> 项目当前已经完成一轮比较完整的 GUI / CLI / 底层对齐回归，主功能链路在自动化测试和实际回归脚本下均已重新验证，整体处于可控、可继续使用和继续迭代的状态。

不建议使用如下表述：

> 项目已经绝对安全，所有功能在所有真实数据场景下都没有问题。
