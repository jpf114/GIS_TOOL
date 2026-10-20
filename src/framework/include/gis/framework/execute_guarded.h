#pragma once

#include <gis/framework/plugin.h>
#include <gis/framework/result.h>
#include <gis/core/progress.h>

#include <map>

namespace gis::framework {

/**
 * 插件调用边界的**唯一**异常兜获点（Task B15 / N66）。
 *
 * WHY 必须有这样一个函数：三处 `plugin->execute(...)`（GUI execute_worker、CLI main、
 * WorkflowRunner）原先各自写一份 try/catch，接住的层次还不一样 —— GUI 三件套齐全、
 * CLI 没有 `catch (...)`、工作流只有 `catch (std::exception)`。同一次抛出于是有三种结局：
 * GUI 安全转失败 / CLI 直接终止进程（非 `std` 派生）/ 工作流把"用户取消"记成失败
 * （`CancelledException` 与 `GisError` 都派生自 `std::runtime_error`，只 catch
 * `std::exception` 会把取消一起吞成普通失败）。
 *
 * 语义固定（捕获顺序不可换：`CancelledException` 派生自 `std::exception`，
 * 一旦先 catch `std::exception` 取消就永远到不了自己的分支）：
 *   1. `gis::core::CancelledException` ⇒ `Result::cancelled()`（`isCancelled=true`）
 *   2. `std::exception`               ⇒ `Result::fail(e.what())`，原样保留插件的 what()
 *   3. `...`                          ⇒ `Result::fail("Unknown exception (non-std)")`
 *
 * 调用方**只**负责各自的上下文文案（GUI `Exception: `、CLI `Error:`/`Cancelled.` + rc=2、
 * 工作流 `Step n (plugin.action) ...`），不要在这里做格式化，也不要再加第二份 try/catch。
 *
 * @param threwException 可选出参：`true` 表示返回的 Result 来自**异常**（第 2/3 条），
 *        `false` 表示插件正常返回（可能是 ok、fail 或 cancelled）。调用方据此决定
 *        "异常前缀"要不要拼 —— 不做它就只能靠嗅探消息文本来区分，那正是本片要消灭的形态。
 *        取消本身用 `Result::isCancelled` 判，不依赖这个 flag。
 */
Result executeGuarded(IGisPlugin& plugin,
                      const std::map<std::string, ParamValue>& params,
                      gis::core::ProgressReporter& reporter,
                      bool* threwException = nullptr);

} // namespace gis::framework
