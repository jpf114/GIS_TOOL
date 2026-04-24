#include "execute_worker.h"
#include "qt_progress_reporter.h"
#include <QThread>
#include <exception>

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
    try {
        if (plugin_ && reporter_) {
            result = plugin_->execute(params_, *reporter_);
        } else {
            result = gis::framework::Result::fail("No plugin or reporter configured");
        }
    } catch (const std::exception& e) {
        result = gis::framework::Result::fail(std::string("Exception: ") + e.what());
    } catch (...) {
        result = gis::framework::Result::fail("Unknown exception occurred");
    }
    emit finished(result);
}
