# 更新日志

本文件记录 GIS TOOL 的版本变更。格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)。

## [1.0.0] - 未发布

### 新增

- Windows 插件式 GIS/遥感算法工作台（CLI + GUI）
- 13 个算法插件与 NSIS 安装包
- GUI 任务中心、批处理、结果预览、预设工作流
- 约 405 项 CTest（含 GUI 离屏回归）

### 变更

- 依赖管理口径统一为全局 vcpkg + 仓库 `vcpkg.json` 清单
- Workflow / Logger / PerformanceMonitor 能力定级与文档同步

### 修复

- 修正 `.gitignore` 导致 `tests/data` 夹具无法入库的问题
- 补充 GUI 回归所需最小 GeoJSON 测试夹具

### 已知限制

详见 [docs/v1.0.0发布说明.md](docs/v1.0.0发布说明.md)。

[1.0.0]: https://github.com/your-org/gis-tool/releases/tag/v1.0.0
