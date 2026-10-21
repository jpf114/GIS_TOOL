# GIS Tool

基于 `C++17 + GDAL + OpenCV + PROJ + Qt` 的插件式 GIS / 遥感算法工作台，提供 CLI 和 GUI 双入口，当前仅支持 Windows。

## 当前状态

- 当前版本：`v1.0.0`
- 标准构建目录：`build/debug`、`build/release`
- 默认安装与交付形态：`Release`
- 依赖管理：全局 vcpkg + 仓库 [vcpkg.json](vcpkg.json) 依赖清单
- 当前产品边界：聚焦算法工作台，不包含地图展示平台能力

### 最近验收摘要（2026-10-21）

- `ctest --test-dir build/debug -C Debug -j1`：**693/693** 通过（429.86 s）
- `ctest --test-dir build/release -C Release -j1`：**691/691** 通过（231.79 s）
  （Release 恒少 2 条，是两条只在 Debug 生效的轻量闸门用例，按名字核过；
  ⚠ 本机全量墙钟的 run-to-run 波动有 ±25 s，这两数以当次现读为准，不要拿来论证某个改动变慢）
- 真实数据回归（`data/` 只读，输入是 22836 要素的北京道路）：`real_raster_regression`、
  `real_matching_regression`、`real_vector_regression`（quick 15 条）与 `real_vector_regression_full`（17 条）
  四个目标全部 rc=0。⚠ 这四条是 `add_custom_target`，**不在 ctest 的 693/691 里**（挂账 N112）
- `cmake --install` + 包内导入闭包断言（`install_closure`）通过
- 交付方式：只用 `cmake --install`；NSIS 安装包与 GitHub 云端构建已于 2026-10-21 停用
- 包体：430 个文件 / 192.2 MB（2026-10-21 删掉"把 `bin/*.dll` 再复制一份进 `bin/plugins/`"那段后
  比原先少 78 个文件 / 167.4 MB；本项目自己的代码只占 3.6 MB，其余全是 GDAL/OpenCV/Qt 的传递依赖）

> ⚠ 文档与代码的一致性只**逐条复验了 5 条**（D1~D5，见
> [docs/算法说明/文档漂移清单.md](docs/算法说明/文档漂移清单.md)）：D2/D3/D4 已在本仓库改文档，
> D1 是 CLI 参数契约缺陷（`vector info` 被强制要求 `--output`，留待批次 C），D5 是缺一篇算法说明。
> 其余 120 篇动作说明的参数表**尚未与 `--list/--help` 逐篇机械核对** ⇒ 参数名/类型/默认值以二进制为准。

> 更早的验收明细见 [CHANGELOG.md](CHANGELOG.md) 与 [docs/RELEASE.md](docs/RELEASE.md)。

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

- 日常开发验证使用 `build/debug`
- 默认安装与交付使用 `build/release`
- 发布前至少执行一次 Debug 全量测试 + Release 构建安装

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

- `matching`：detect / corner / match / register / change；Release 额外覆盖 ecc_register / stitch
- `projection`：info / transform / assign_srs / reproject
- `cutting`：clip / mosaic / split / merge_bands
- `processing`：pansharpen / gabor_filter / glcm_texture / mean_shift_filter / skeleton / connected_components
- `classification`：feature_stats / svm_classify / random_forest_classify / max_likelihood_classify
- `georef`：dos_correction / radiometric_calibration / gcp_register / cosine_correction / minnaert_correction / c_correction / percentile_stretch / rpc_orthorectify
- `spindex`：ndvi / ndmi / evi / evi2 / savi / osavi / gndvi / ndwi / mndwi / ndbi / bsi / arvi / nbr / awei / ui / bi / custom_index
- `terrain`：当前已建立关键动作的真实数据回归链路
- `vector`：当前已建立主链动作的真实数据回归链路

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
- [贡献指南](./CONTRIBUTING.md)
- [更新日志](./CHANGELOG.md)
- [安全策略](./SECURITY.md)
- [许可证](./LICENSE)
- [第三方声明](./THIRD_PARTY_NOTICES)

## 说明

- 仓库当前主文档统一使用中文
- 提交信息当前统一使用中文
- 当前阶段优先保证简单可维护、可回归
- `pointcloud` 当前不计入已完成主模块，相关依赖条件尚未满足
