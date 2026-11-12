# 更新日志

本文件记录 GIS TOOL 的版本变更。格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)。

## [Unreleased]

### 修复

- JS 单引号串转义阻断 `</script>` 注入；`polygonize` 取消路径 RAII
- 工作流页析构等待；warp 失败清理改为非抛出

### 变更

- OpenCV 组件声明、许可年份、依赖版本快照入仓；删除无调用点瓦片辅助函数
- `.gitignore` 补齐生成物忽略；空间连接取消检查下沉到候选对
- JS 转义等价改用 `\x3C`，避开 `source_mojibake_guard` 的 `R5_ESCRESIDUE` 误报

### 文档

- 建立 RELEASE 当前状态单一事实源，8 组过期声明改准
- 算法口径名实同步，125 篇正文一次性对撞（任务名沿用阶段三「98 篇」口径）

### 验收

- 新增 `docs_prose_claims` 守卫
- 新增 `real_data_true_raster` / `real_data_true_matching` 两条真实数据腿：从 `D:\Code\Data` 真实影像裁窗，
  用 GDAL 官方 CLI 独立造数、`gdalinfo`/numpy 独立推导期望值，并配三条「改坏产品必须判红」的判别臂
- D4 双树全量 ctest 与两树 `cmake --install` 结论见 RELEASE 当前状态节

## [1.0.0] - 2026-05-24

### 新增

- Windows 插件式 GIS/遥感算法工作台（CLI + GUI）
- 13 个算法插件（当时的 NSIS 安装包已于 2026-10-21 停用）
- GUI 任务中心、批处理、结果预览、预设工作流
- 发布时的 CTest 数量（现值见 `docs/RELEASE.md` 当前状态节）；`debug_only` 用例在 Release 下不注册

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
