#include "execute_worker.h"
#include "qt_progress_reporter.h"
#include <gis/framework/execute_guarded.h>
#include <QThread>
#include <string>

ExecuteWorker::ExecuteWorker(QObject* parent)
    : QObject(parent) {}

void ExecuteWorker::setup(gis::framework::IGisPlugin* plugin,
                           const std::map<std::string, gis::framework::ParamValue>& params,
                           QtProgressReporter* reporter) {
    plugin_ = plugin;
    params_ = params;
    reporter_ = reporter;
}

void ExecuteWorker::run() {
    gis::framework::Result result;
    if (plugin_ && reporter_) {
        // Task B15 / N66：捕获层次收口到 framework 的 executeGuarded（一处 catch 三件套），
        // 本侧只剩自己的上下文文案。threwException 用来把"异常"与"插件正常返回的失败"
        // 分开 —— 只有前者才该带 `Exception: ` 前缀（生产里绝大多数失败是插件自己报的）。
        bool threwException = false;
        result = gis::framework::executeGuarded(*plugin_, params_, *reporter_, &threwException);
        if (threwException) {
            result.message = std::string("Exception: ") + result.message;
        } else if (reporter_->isCancelled() && !result.isCancelled) {
            // 插件正常返回但用户已按取消：GUI 的取消状态归 reporter 所有，仍然以它为准。
            result = gis::framework::Result::cancelled();
        }
    } else {
        result = gis::framework::Result::fail("No plugin or reporter configured");
    }
    emit finished(result);
}
