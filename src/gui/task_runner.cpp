#include "task_runner.h"
#include "execute_worker.h"
#include "qt_progress_reporter.h"
#include "task_manager.h"

#include <QThread>

TaskRunner& TaskRunner::instance() {
    static TaskRunner inst;
    return inst;
}

TaskRunner::TaskRunner(QObject* parent)
    : QObject(parent) {}

QString TaskRunner::run(
    gis::framework::IGisPlugin* plugin,
    const QString& displayGroup,
    const QString& pluginName,
    const QString& actionKey,
    const std::map<std::string, gis::framework::ParamValue>& params,
    const QString& pluginDisplayName,
    const QString& actionDisplayName) {

    TaskManager::instance().initializeGroup(displayGroup);

    auto taskId = TaskManager::instance().submitTask(
        displayGroup, pluginName, actionKey, params,
        pluginDisplayName, actionDisplayName);

    if (taskId.isEmpty()) return {};

    auto reporter = std::make_unique<QtProgressReporter>(taskId);
    auto* reporterPtr = reporter.get();

    auto ctx = std::make_shared<TaskContext>();
    ctx->taskId = taskId;
    ctx->displayGroup = displayGroup;
    ctx->reporter = std::move(reporter);
    activeTasks_[taskId] = ctx;
    runningTaskId_ = taskId;

    auto* worker = new ExecuteWorker;
    worker->setup(plugin, params, reporterPtr);

    auto* thread = new QThread;
    worker->moveToThread(thread);

    connect(reporterPtr, &QtProgressReporter::progressChanged,
            this, [this](const QString& tid, double percent) {
        emit taskProgressChanged(tid, percent);
    });

    connect(reporterPtr, &QtProgressReporter::messageLogged,
            this, [this](const QString& tid, const QString& msg) {
        auto it = activeTasks_.find(tid);
        QString dg = (it != activeTasks_.end()) ? it.value()->displayGroup : QString();
        TaskManager::instance().appendLog(dg, tid, msg);
        emit taskLogMessage(dg, tid, msg);
    });

    connect(thread, &QThread::started, this, [this, displayGroup, taskId]() {
        TaskManager::instance().updateTaskStatus(displayGroup, taskId, TaskRecord::Running);
        emit taskStarted(displayGroup, taskId);
    });
    connect(thread, &QThread::started, worker, &ExecuteWorker::run);

    connect(worker, &ExecuteWorker::finished, this,
            [this, displayGroup, taskId](const gis::framework::Result& result) {
        TaskManager::instance().finishTask(displayGroup, taskId, result);
        emit taskFinished(displayGroup, taskId, result.success, result.isCancelled);
        activeTasks_.remove(taskId);
        if (runningTaskId_ == taskId) {
            runningTaskId_.clear();
        }
    });

    connect(worker, &ExecuteWorker::finished, thread, &QThread::quit);
    connect(worker, &ExecuteWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
    return taskId;
}

void TaskRunner::cancelTask(const QString& taskId) {
    auto it = activeTasks_.find(taskId);
    if (it != activeTasks_.end() && it.value()->reporter) {
        it.value()->reporter->cancel();
    }
}

bool TaskRunner::isRunning() const {
    return !runningTaskId_.isEmpty();
}

QString TaskRunner::runningTaskId() const {
    return runningTaskId_;
}
