# GUI 主链职责盘点（阶段 1-1）

> 更新日期：2026-05-24  
> 目的：明确 `MainWindow` 与 `gui_data_support` 边界，指导后续收口。

## 已外提至 `gui_data_support`

| 职责 | 代表 API |
|------|----------|
| 动作目录与分组展示 | `collectActionCatalogItems`、`resolveGroupedActionPlugin` |
| 插件组计数 / 耗时格式化 | `countDisplayPluginGroups`、`formatDurationText` |
| 任务执行反馈与状态样式 | `buildTaskExecutionFeedback`、`taskStatusIcon/Text/Color` |
| 结果摘要与元数据顺序 | `buildResultSummaryText` + `orderedMetadataEntries` |
| 数据路径类型检测 | `detectDataKind`、`isSupportedDataPath` |

## 仍由 `MainWindow` 承担（窗口级）

| 职责 | 说明 |
|------|------|
| 布局与页面切换 | 导航、参数卡、结果预览、任务中心、工作流页 |
| 执行触发与进度对话框 | `ExecuteWorker`、取消、批处理队列编排 |
| 参数控件绑定 | `ParamWidget` / `ParamCardWidget` 与当前插件上下文 |
| 参数自动联动 | 输出路径、`extent`、`layer` 等（阶段 1-3 待收紧） |
| 设置与欢迎流程 | `SettingsManager`、`WelcomeDialog` |

## 阶段 1-3 后续收口建议

- 将输出路径 / extent / layer 联动规则迁入 `gui_data_support` 可测试函数
- 保持 `MainWindow` 仅负责「何时调用」而非「规则本身」
