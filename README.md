# GIS Tool





基于 `C++17 + GDAL + OpenCV + PROJ + Qt` 的插件式 GIS / 遥感算法工作台，提供 CLI 和 GUI 双入口，当前仅支持 Windows。


## 当前状态


- 当前版本：`v1.0.0`


- 标准构建目录：
  - `build/debug`


  - `build/release`


- 默认安装与交付形态：`Release`


- 依赖管理：全局 vcpkg + 仓库 [vcpkg.json](vcpkg.json) 依赖清单


- 当前产品边界：聚焦算法工作台，不包含地图展示平台能力





### 最近一次验收记录（2026-05-24，发布前审查收口）

- 切片预览：HTML 转义 + 本地 Leaflet（去除 CDN）
- 核心 Logger 接入 CLI/GUI 进度消息链
- `ctest --test-dir build/release -C Release`：**475/475** 通过
- `ctest --test-dir build/debug -C Debug`：**475/475** 通过
- `real_raster/matching/vector_regression`（quick）通过
- `cmake --install` + install smoke 通过

### 历史验收记录（2026-05-24）

- 修复 `PluginTest.VectorDanglingEndpointCheckExecution`：`dangling_endpoint_check` CSV 的 `endpoint_type` 与测试约定对齐为 `0/1`（start/end）
- 修复 GUI 离屏回归：默认 Qt 平台与 `qoffscreen.dll` 对齐；修复 `gis_gui_assert_screenshot_if_supported` 无限递归；`geom_metrics`/`nearest` 输出扩展名校验与插件一致
- `ctest --test-dir build/release -C Release`：**472/472** 通过（含 GUI 离屏回归；`debug_only` 用例在 Release 下自动跳过）
- `ctest --test-dir build/release -C Release -R "CoreTest|FrameworkTest|PluginTest|GuiSupportTest|MainWindowTest"`：`280/280` 通过
- `cmake --build build/release --config Release --target real_raster_regression real_matching_regression real_vector_regression`（quick）均通过
- `cmake --install build/release --config Release --prefix install` 成功；`install/bin/gis-cli.exe --list`、`gis-gui -platform offscreen --self-test` 通过
- `ctest --test-dir build/debug -C Debug`：**472/472** 通过（2026-05-24，后续增至 **475/475**）
- NSIS：`installer/gis-toolkit-1.0.0-win64-setup.exe` 本地构建成功（见 [docs/RELEASE.md](docs/RELEASE.md)）

> 更早的验收明细已收敛到 [CHANGELOG.md](CHANGELOG.md) 与 [docs/RELEASE.md](docs/RELEASE.md)；下文仅保留近期摘要。

### 历史验收记录（2026-05-23）

- `tests/data` 最小 GeoJSON 夹具已入库，CLI 验证通过：
  - `vector buffer`（`gui_vector_projected_input.geojson`）→ `tmp/smoke-buffer.gpkg`
  - `vector convert`（`gui_vector_input.geojson`）→ `tmp/convert-out.geojson`
- `ctest --test-dir build/debug -C Debug -R "CoreTest|FrameworkTest"`：`42/42` 通过
- `ctest --test-dir build/debug -C Debug -R "gui_smoke_startup"`：`1/1` 通过
- `ctest -N` 当前发现 `463` 个测试（较此前 `405` 有增加）
- Debug 全量 `ctest` 与部分 GUI 离屏回归（如 `gui_vector_convert_offscreen`）在本机仍可能出现 Access violation，需结合 Release 构建与 CI 结果继续验收
- `ctest -R "MainWindowTest|GuiSupportTest.BuildTaskExecutionFeedback|FrameworkTest.MergeResultMetadata"`：`19/19` 通过（2026-05-23）

### 历史验收记录（2026-05-15）


- `cmake --build build/debug --config Debug --target gis_tests` 通过


- `ctest --test-dir build/debug -C Debug -N` 发现 `405` 个测试
- `ctest --test-dir build/debug -C Debug -R "CoreTest|FrameworkTest" --output-on-failure`：`45/45` 通过


- `ctest --test-dir build/debug -C Debug -R "gui_smoke_startup" --output-on-failure`：`1/1` 通过


- `build/debug/src/cli/Debug/gis-cli.exe --list` 正常，可列出 13 个插件
- `build/debug/src/gui/Debug/gis-gui.exe -platform minimal --self-test` 正常


- Debug 全量 `ctest --test-dir build/debug -C Debug --output-on-failure` 本地执行超过 5 分钟，未在当轮跑完；发布前仍需完整执行


- GUI 的任务队列批量处理时统计、错误提示等主链能力可用


- `cmake --build build/release --config Release --target real_matching_regression` 通过


- `cmake --build build/release --config Release --target real_matching_regression_full` 通过


- `cmake --build build/release --config Release --target real_raster_regression` 通过


- `cmake --build build/release --config Release --target real_raster_regression_full` 通过


- `cmake --build build/release --config Release --target real_vector_regression` 通过


- `cmake --build build/release --config Release --target real_vector_regression_full` 通过


- `tmp/` 当前为空





## 主要能力


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





## 插件与能力映射


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





## GUI 归并说明





- GUI 左侧导航已将 `raster_manage / raster_inspect / raster_render / raster_math` 归并为单独"栅格工具"入口
- `classification` 和 GUI 中统一以分类统计作为主项，具体动作作为子功能呈现
- GUI 和 CLI 最终都落到同一套插件执行链路


## 当前已补齐的重点能力





### processing





- `gabor_filter`


- `glcm_texture`


- `mean_shift_filter`


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


- `percentile_stretch`


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


以上重点能力当前都已经贯通到以下层级：


- 底层算法 / 插件实现


- CLI 调用链路


- GUI 接入


- GUI 参数校验


- 插件测试


- GUI support 测试


- GUI 离屏回归


- 真实数据专项回归


- Release 安装与启动验收


## 构建


前置条件：

- 已设置 `VCPKG_ROOT` 环境变量
- 已通过 `vcpkg.json` 将依赖预装到全局 vcpkg（CMake 使用 `VCPKG_MANIFEST_MODE OFF`）

```powershell
vcpkg install --triplet x64-windows --x-manifest-root=$PWD
```


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


- 日常开发验证使用`build/debug`


- 默认安装与交付使用`build/release`


- 发布前至少执行一次`Debug 全量测试 + Release 构建安装`


## 测试分层入口

### 日常开发（最小验证）

```powershell
ctest --test-dir build/debug -C Debug -R "CoreTest|FrameworkTest" --output-on-failure
ctest --test-dir build/debug -C Debug -R "gui_smoke_startup" --output-on-failure
```

### 提交前（功能回归）

```powershell
ctest --test-dir build/debug -C Debug -R "CoreTest|FrameworkTest|PluginTest|GuiSupportTest|MainWindowTest" --output-on-failure
```

### 发布前（全量验收）

见下方「发布前标准验收基线」。


## 发布前标准验收基线


发布前至少完整执行一次以下命令：





```powershell


ctest --test-dir build/debug -C Debug --output-on-failure


ctest --test-dir build/release -C Release --output-on-failure


cmake --build build/release --config Release --target real_raster_regression


cmake --build build/release --config Release --target real_matching_regression


cmake --build build/release --config Release --target real_vector_regression


```





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





当前已确认过的专项包括：





- `matching`


  - `detect / corner / match / register / change`


  - Release 额外覆盖：`ecc_register / stitch`


- `projection`


  - `info / transform / assign_srs / reproject`


- `cutting`


  - `clip / mosaic / split / merge_bands`


- `processing`


  - `pansharpen / gabor_filter / glcm_texture / mean_shift_filter / skeleton / connected_components`


- `classification`


  - `feature_stats / svm_classify / random_forest_classify / max_likelihood_classify`


  - `full` 额外覆盖：`feature_stats_csv`


- `georef`


  - `dos_correction / radiometric_calibration / gcp_register / cosine_correction / minnaert_correction / c_correction / percentile_stretch / rpc_orthorectify`


- `spindex`


  - `ndvi / ndmi / evi / evi2 / savi / osavi / gndvi / ndwi / mndwi / ndbi / bsi / arvi / nbr / awei / ui / bi / custom_index`


- `terrain`


  - 当前已建立关键动作的真实数据回归链路


- `vector`


  - 当前已建立主链动作的真实数据回归链路





当前 `real_raster_regression` 的重点验收口径如下：





- `classification.feature_stats`


  - `quick`：验收`json / vector_output / raster_output`


  - `full`：额外验收`csv`


  - 补充校验 `actual_srs` 和 `__summary__`


- `classification.svm_classify / random_forest_classify / max_likelihood_classify`


  - 验证输出尺寸 `24 x 12 x 1`


  - 验证输出类型 `Float32`


  - 验证类别范围 `1~2`


- `projection.info / transform / assign_srs / reproject`


  - 验证尺寸、EPSG 编码、坐标转换结果与重投影输出
- `cutting.clip / mosaic / split / merge_bands`


  - 验证输出尺寸、瓦片数量波段数量与关键统计值
- `processing.pansharpen`


  - 固定验证 `pan_method=simple_mean`


- `processing.gabor_filter / glcm_texture / mean_shift_filter`


  - 验证输出尺寸 `32 x 32 x 1`


  - 验证输出类型 `Float32`


- `processing.skeleton / connected_components`


  - 验证输出尺寸 `64 x 64 x 1`


  - `skeleton` 校验最大值`1.0`


  - `connected_components` 校验最大标签数`4`


- `georef`


  - 8 个动作统一校验输出尺寸、输出类型CRS 或关键统计值
- `spindex`


  - 固定验证主流指数输出


  - `custom_index` 使用 `preset=ndvi_alias / ndmi_alias` 作为稳定验收入口


- `terrain`


  - 当前额外校验 `slope / profile_extract / viewshed_multi` 等关键结果


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





- [文档索引](./docs/README.md)


- [当前发布说明](./docs/v1.0.0发布说明.md)


- [算法说明总览](./docs/算法说明/总览.md)


- [架构设计文档](./docs/架构设计文档.md)


- [用户手册](./docs/用户手册.md)


- [历史归档说明](./docs/archive/README.md)


- [贡献指南](./CONTRIBUTING.md)


- [更新日志](./CHANGELOG.md)


- [安全策略](./SECURITY.md)





## 说明





- 仓库当前主文档统一使用中文


- 提交信息当前统一使用中文


- 当前阶段优先保证简单可维护、可回归


- `pointcloud` 当前不计入已完成主模块，相关依赖条件尚未满足





