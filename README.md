# GIS Tool

基于 `C++17 + GDAL + OpenCV + PROJ + Qt` 的插件式 GIS 工具集，提供命令行和桌面图形界面两种入口。

## 当前版本

当前稳定版本：`v0.1.3`

## 当前验证状态

以下结论已在本仓库当前代码上重新验证：

- `Visual Studio 2022 + C++17 + 全局 vcpkg` 可稳定配置与编译
- `GIS_BUILD_GUI=OFF/ON` 两种模式均可构建
- 标准构建目录为 `build/debug` 与 `build/release`，标准安装目录为 `install`
- `ctest --test-dir build/debug -C Debug --output-on-failure` 为 `201/201` 通过
- `gis-cli.exe --list` 可正常列出全部主插件
- `gis-gui.exe -platform offscreen --self-test` 可正常启动并退出
- GUI 已验证较完整的离屏自动执行链路，覆盖 `vector / projection / utility / classification / processing / matching / cutting` 主功能动作
- 矢量回归 `quick / full` 可运行；存在本地 `data/vector` 时优先使用本地数据，否则自动生成可复现样例数据
- Windows 下 `gis-cli.exe` 已验证可直接处理中文路径输入
- `vector filter` 已用真实 GeoJSON 数据验证中文属性条件可正常过滤
- 当前收口状态与边界说明见 [GUI_CLI_底层对齐验证清单](D:\Code\MyProject\GIS_TOOL\docs\GUI_CLI_底层对齐验证清单.md)

需要特别说明：

- 当前自动化测试覆盖已经较完整，但其中一部分仍属于合成数据测试
- 项目已经具备真实场景使用能力，但还不能等同于“所有模块都经过完整生产数据验证”
- GUI 已具备参数配置、执行和结果反馈能力，但当前自动化验证重点仍是主执行链路，不是完整交互回归

## 项目状态

- `core / framework / plugins / cli / gui / tests` 主链路可稳定使用
- GUI 已接入主要功能模块，参数界面与插件参数定义基本匹配
- 当前版本适合作为持续迭代的可用基线

## 项目结构

- `src/core`
  - GDAL / OpenCV 基础封装、运行时环境初始化、通用类型
- `src/framework`
  - 插件接口、参数描述、插件管理器、执行结果模型
- `src/plugins`
  - 投影、裁切、匹配、处理、分类、工具、矢量等插件
- `src/cli`
  - 命令行入口
- `src/gui`
  - Qt Widgets 图形界面入口
- `tests`
  - 核心能力、框架能力、插件能力、GUI 支撑能力测试

## 已实现插件

- `projection`
  - 重投影、坐标系信息查看、坐标转换、赋予坐标系
- `cutting`
  - 裁切、镶嵌、分块、波段合并
- `matching`
  - 特征检测、特征匹配、影像配准、变化检测、角点检测
- `processing`
  - 阈值分割、滤波、增强、波段运算、统计、边缘检测、轮廓提取、模板匹配、全色锐化、霍夫变换、分水岭、K-Means
- `classification`
  - 地物分类统计
- `utility`
  - 金字塔、NoData、直方图、信息查看、伪彩色、NDVI
- `vector`
  - 信息查看、过滤、缓冲、裁切、栅格化、面矢量化、格式转换、并集、差集、融合

## 环境要求

推荐环境：

- Windows
- Visual Studio 2022
- C++17
- 全局 `vcpkg`
- `VCPKG_ROOT/installed/x64-windows`

推荐已安装依赖：

- `gdal`
- `opencv4`
- `proj`
- `gtest`
- `qtbase`

## 构建

仅构建 CLI 与测试：

```powershell
cmake -S . -B build/debug -G "Visual Studio 17 2022" -A x64 -DGIS_BUILD_GUI=OFF -DGIS_BUILD_TESTS=ON
cmake --build build/debug --config Debug
ctest --test-dir build/debug -C Debug --output-on-failure
```

构建 GUI、CLI 与测试：

```powershell
cmake -S . -B build/debug -G "Visual Studio 17 2022" -A x64 -DGIS_BUILD_GUI=ON -DGIS_BUILD_TESTS=ON
cmake --build build/debug --config Debug
ctest --test-dir build/debug -C Debug --output-on-failure
cmake -S . -B build/release -G "Visual Studio 17 2022" -A x64 -DGIS_BUILD_GUI=ON -DGIS_BUILD_TESTS=ON
cmake --build build/release --config Release
cmake --install build/release --config Release
```

建议：

- 使用 `build/debug` 作为标准 Debug 构建与验证目录
- 使用 `build/release` 作为标准 Release 构建与安装目录
- 使用 `install` 作为标准安装目录
- 建议开发验证使用 `Debug`
- 默认安装与交付使用 `Release`
- 发布前至少完整执行一次 `Debug build + Debug test + Release install`

## 使用

列出插件：

```powershell
.\install\bin\gis-cli.exe --list
```

运行插件：

```powershell
.\install\bin\gis-cli.exe <plugin> <action> --input <path> --output <path>
```

启动 GUI：

```powershell
.\install\bin\gis-gui.exe
```

GUI 当前适合：

- 选择插件与功能
- 自动生成参数面板
- 执行算法并查看进度与结果摘要

## 许可证

本项目采用 `MIT License`，详见 [LICENSE](/D:/Code/MyProject/GIS_TOOL/LICENSE)。

## 说明

- 文档统一使用中文
- 提交信息统一使用中文
