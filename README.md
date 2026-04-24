# GIS Tool

基于 `C++17 + GDAL + OpenCV + PROJ + Qt` 的插件式 GIS 工具集，支持命令行和桌面图形界面两种入口。

## 项目简介

当前项目采用统一的插件式架构：

- `src/core`：GDAL / OpenCV 基础封装与通用类型
- `src/framework`：插件接口、参数元数据、插件管理器
- `src/plugins`：各类 GIS/图像处理插件
- `src/cli`：命令行入口
- `src/gui`：Qt 图形界面入口
- `tests`：核心、框架与插件测试
- `docs`：功能说明、架构设计、计划文档

当前已经实现的主要插件类别包括：

- `projection`：投影与坐标系处理
- `cutting`：裁切、分块、拼接
- `matching`：特征匹配、配准、变化检测
- `processing`：阈值、滤波、增强、统计、分割等
- `vector`：矢量数据处理
- `utility`：辅助工具类功能

## 技术栈

- `C++17`
- `CMake`
- `vcpkg`
- `GDAL`
- `OpenCV`
- `PROJ`
- `Qt 6`
- `GoogleTest`

## 当前状态

项目已经完成基础架构与多类插件原型开发，但仍处于工程稳定化阶段，当前优先工作包括：

- 打通默认构建链路
- 清理测试中的环境依赖
- 加强插件生命周期管理
- 补齐协作文档与持续集成

详细路线图见：

- [项目稳定化与迭代路线图](/D:/Code/MyProject/GIS_TOOL/docs/superpowers/plans/2026-04-24-project-stabilization-roadmap.md:1)

## 构建准备

建议在 Windows 环境下使用以下工具：

1. 安装 Visual Studio 2022/2026 C++ 工具链
2. 安装 `CMake`
3. 安装并配置 `vcpkg`
4. 设置环境变量 `VCPKG_ROOT`

项目依赖由 `vcpkg.json` 管理，默认依赖包括：

- `gdal`
- `opencv4`
- `proj`
- `gtest`
- `qtbase`

## 推荐构建方式

### 1. 配置

```powershell
cmake -S . -B build -DGIS_BUILD_GUI=OFF -DGIS_BUILD_TESTS=ON
```

如果需要 GUI：

```powershell
cmake -S . -B build -DGIS_BUILD_GUI=ON -DGIS_BUILD_TESTS=ON
```

### 2. 编译

```powershell
cmake --build build --config Debug
```

### 3. 运行测试

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

## CLI 使用示例

列出插件：

```powershell
.\build\src\cli\Debug\gis-cli.exe --list
```

查看插件帮助：

```powershell
.\build\src\cli\Debug\gis-cli.exe projection --help
```

执行投影转换：

```powershell
.\build\src\cli\Debug\gis-cli.exe projection reproject --input=dem.tif --output=dem_4326.tif --dst_srs=EPSG:4326
```

## GUI 说明

当启用 `GIS_BUILD_GUI=ON` 时会编译 Qt 图形界面。GUI 会根据插件提供的 `ParamSpec` 自动生成参数表单，因此 CLI 与 GUI 使用的是同一套插件定义。

## 文档索引

- `docs/GDAL_OpenCV_Cpp_功能清单.md`
- `docs/superpowers/specs/2026-04-22-gis-tool-plugin-architecture-design.md`
- `docs/superpowers/plans/2026-04-24-project-stabilization-roadmap.md`

## 开发说明

当前建议优先遵循以下顺序推进：

1. 先修复构建与测试基线
2. 再稳定插件框架
3. 再拆分大插件并补测试
4. 最后增强 GUI 与发布流程

## 说明

仓库内新增和维护的文档统一使用中文。
