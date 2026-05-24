# 更新日志

本文件记录 GIS TOOL 的版本变更。格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)。

## [1.0.0] - 2026-05-24

### 新增

- Windows 插件式 GIS/遥感算法工作台（CLI + GUI）
- 13 个算法插件与 NSIS 安装脚本
- GUI 任务中心、批处理、结果预览、预设工作流
- 475 项 CTest（含 GUI 离屏回归；`debug_only` 用例在 Release 下自动跳过）

### 变更

- 依赖管理口径统一为全局 vcpkg + 仓库 `vcpkg.json` 清单
- Workflow / Logger / PerformanceMonitor 能力定级与文档同步
- GUI 导航目录函数外提至 `gui_data_support`，精简 `MainWindow`
- CLI / GUI 元数据输出统一为 `orderedMetadataEntries` 顺序
- 任务执行反馈、任务状态展示与 `ResultMetadataKeys` 收敛到支持层
- README 补充日常 / 提交前 / 发布前三套测试入口

### 修复

- 切片预览 HTML：转义 `title`/`copyright`，并改用本地 Leaflet 资源（去除 unpkg CDN）
- 接入核心 `Logger` 到 CLI/GUI 进度消息路径
- 统一插件 CMake 列表（`cmake/PluginTargets.cmake`）
- 修正 `.gitignore` 导致 `tests/data` 夹具无法入库的问题
- 补充 GUI 回归所需最小 GeoJSON 测试夹具
- GUI 离屏回归默认 Qt 平台与 `qoffscreen.dll` 对齐；修复 screenshot 断言无限递归
- `geom_metrics` / `nearest` 输出扩展名校验与插件能力一致
- `dangling_endpoint_check` CSV `endpoint_type` 输出 `0/1`

### 已知限制

详见 [docs/v1.0.0发布说明.md](docs/v1.0.0发布说明.md)。

[1.0.0]: https://github.com/jpf114/GIS_TOOL/releases/tag/v1.0.0
