# GIS Tool

基于 `C++17 + GDAL + OpenCV + PROJ + Qt` 的插件式 GIS 工具集，支持命令行和桌面图形界面两种入口。

## 当前状态

当前代码基线已经完成以下验证：

- `Visual Studio 2022 + C++17 + 全局 vcpkg` 可稳定配置与编译
- `GIS_BUILD_GUI=OFF/ON` 两种模式均已验证构建通过
- `ctest` 当前为 `51/51` 通过
- `gis-gui.exe` 已验证可成功启动，不闪退

当前项目更准确的状态是：

- `core / framework / plugins / cli` 已具备可用的功能主链路
- `gui` 已具备基础执行界面，但仍属于工具型前端，不是完整 GIS 工作台
- 全项目仍处于“稳定化 + 验收补齐”阶段，不能直接宣称“所有功能都已完整无问题”

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
  - 构建说明、功能与验证说明、历史设计文档

## 已实现插件

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

## 依赖策略

项目遵循以下依赖约束：

- 所有第三方依赖统一安装到全局 `vcpkg`
- 默认复用 `VCPKG_ROOT/installed/x64-windows`
- 不在项目构建目录重复下载和重复安装依赖
- 运行时 DLL、Qt 平台插件、GDAL/PROJ 数据目录从全局 `vcpkg` 拷贝

推荐已安装包：

- `gdal`
- `opencv4`
- `proj`
- `gtest`
- `qtbase`

## 推荐构建命令

仅构建 CLI 与测试：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DGIS_BUILD_GUI=OFF -DGIS_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

构建 GUI、CLI 与测试：

```powershell
cmake -S . -B build-gui -G "Visual Studio 17 2022" -A x64 -DGIS_BUILD_GUI=ON -DGIS_BUILD_TESTS=ON
cmake --build build-gui --config Debug
ctest --test-dir build-gui -C Debug --output-on-failure
```

## 运行方式

列出插件：

```powershell
.\build\src\cli\Debug\gis-cli.exe --list
```

启动 GUI：

```powershell
.\build-gui\src\gui\Debug\gis-gui.exe
```

## 文档索引

- [构建与开发说明](D:/Code/MyProject/GIS_TOOL/docs/构建与开发说明.md)
- [当前功能与真实数据验证指南](D:/Code/MyProject/GIS_TOOL/docs/当前功能与真实数据验证指南.md)
- [项目稳定化与迭代路线图](D:/Code/MyProject/GIS_TOOL/docs/superpowers/plans/2026-04-24-project-stabilization-roadmap.md)

## 说明

- 所有新增和维护文档统一使用中文
- 提交信息统一使用中文
