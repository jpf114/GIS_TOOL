# GIS Tool

基于 `C++17 + GDAL + OpenCV + PROJ + Qt` 的插件式 GIS 工具集，支持命令行和桌面图形界面两种入口。

## 稳定版本

当前稳定版本：`0.1.3`

本版本已完成以下验证：

- `Visual Studio 2022 + C++17 + 全局 vcpkg` 可稳定配置与编译
- `GIS_BUILD_GUI=OFF/ON` 两种模式均可构建
- 标准构建目录为 `build`，标准安装目录为 `install`
- `ctest --test-dir build -C Release --output-on-failure` 为 `125/125` 通过
- `gis-cli.exe --list` 可正常列出全部主插件
- `gis-gui.exe -platform offscreen --self-test` 可正常启动并退出
- GUI 进程级离屏启动检查通过，可稳定进入事件循环
- 真实数据矢量回归 `quick / full` 均已通过
- Windows 下 `gis-cli.exe` 已验证可直接处理中文路径输入
- `vector filter` 已用真实 GeoJSON 数据验证中文属性条件可正常过滤

当前项目状态：

- `core / framework / plugins / cli / gui / tests` 主链路已可稳定使用
- GUI 已具备基础执行、参数配置和结果反馈能力
- 当前版本可作为稳定使用基线继续迭代
- 当前阶段重点从“先跑通”转向“持续维护与后续迭代”

## 项目结构

- `src/core`
  - GDAL / OpenCV 基础封装、运行时环境初始化、通用类型
- `src/framework`
  - 插件接口、参数描述、插件管理器、执行结果模型
- `src/plugins`
  - 六大插件：投影、裁切、匹配、处理、栅格工具、矢量工具
- `src/cli`
  - 命令行入口
- `src/gui`
  - Qt Widgets 图形界面入口
- `tests`
  - 核心能力、框架能力、插件能力测试
- `docs`
  - 后续工作计划

## 已实现插件类型

- `projection`
  - 重投影、坐标系信息查看、坐标转换、赋予坐标系
- `cutting`
  - 裁切、镶嵌、分块、波段合并
- `matching`
  - 特征检测、特征匹配、影像配准、变化检测、ECC 配准、角点检测、拼接
- `processing`
  - 阈值分割、滤波、增强、波段运算、统计、边缘检测、轮廓提取、模板匹配、全色锐化、霍夫、分水岭、K-Means
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
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DGIS_BUILD_GUI=OFF -DGIS_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

构建 GUI、CLI 与测试：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DGIS_BUILD_GUI=ON -DGIS_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --install build --config Debug
```

建议：

- 使用 `build` 作为标准构建与验证目录
- 使用 `install` 作为标准安装目录
- 发布或交付前至少完整执行一次 `build + test + install`

## 使用

列出插件：

```powershell
.\install\bin\gis-cli.exe --list
```

运行一个插件时，形式为：

```powershell
.\install\bin\gis-cli.exe <plugin> <action> --input <path> --output <path>
```

启动 GUI：

```powershell
.\install\bin\gis-gui.exe
```

GUI 当前适合：

- 选择插件与动作
- 自动生成参数面板
- 执行算法并查看进度与结果摘要

## 后续工作

- 后续工作计划见：[后续工作计划](D:/Code/MyProject/GIS_TOOL/docs/后续工作计划.md)

## 说明

- 文档统一使用中文
- 提交信息统一使用中文
