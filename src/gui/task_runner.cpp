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

    QueuedTask qt;
    qt.plugin = plugin;
    qt.displayGroup = displayGroup;
    qt.pluginName = pluginName;
    qt.actionKey = actionKey;
    qt.params = params;
    qt.pluginDisplayName = pluginDisplayName;
    qt.actionDisplayName = actionDisplayName;
    qt.taskId = taskId;

    queue_.enqueue(qt);
    emit queueChanged(queue_.size());

    processQueue();
    return taskId;
}

void TaskRunner::processQueue() {
    if (!runningTaskId_.isEmpty()) return;
    if (queue_.isEmpty()) return;

    QueuedTask task = queue_.dequeue();
    emit queueChanged(queue_.size());
    startTask(task);
}

void TaskRunner::startTask(const QueuedTask& task) {
    auto reporter = std::make_unique<QtProgressReporter>(task.taskId);
    auto* reporterPtr = reporter.get();

    auto ctx = std::make_shared<TaskContext>();
    ctx->taskId = task.taskId;
    ctx->displayGroup = task.displayGroup;
    ctx->reporter = std::move(reporter);
    activeTasks_[task.taskId] = ctx;
    runningTaskId_ = task.taskId;

    auto* worker = new ExecuteWorker;
    worker->setup(task.plugin, task.params, reporterPtr);

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

    connect(thread, &QThread::started, this, [this, dg = task.displayGroup, tid = task.taskId]() {
        TaskManager::instance().updateTaskStatus(dg, tid, TaskRecord::Running);
        emit taskStarted(dg, tid);
    });
    connect(thread, &QThread::started, worker, &ExecuteWorker::run);

    connect(worker, &ExecuteWorker::finished, this,
            [this, dg = task.displayGroup, tid = task.taskId, task](const gis::framework::Result& result) {
        if (!result.success && !result.isCancelled && task.retryCount < QueuedTask::kMaxRetries) {
            activeTasks_.remove(tid);
            if (runningTaskId_ == tid) {
                runningTaskId_.clear();
            }

            QueuedTask retryTask = task;
            retryTask.retryCount++;
            auto retryId = TaskManager::instance().submitTask(
                dg, task.pluginName, task.actionKey, task.params,
                task.pluginDisplayName, task.actionDisplayName);
            if (!retryId.isEmpty()) {
                retryTask.taskId = retryId;
                queue_.prepend(retryTask);
                emit queueChanged(queue_.size());
                TaskManager::instance().appendLog(dg, retryId,
                    QStringLiteral("鑷姩閲嶈瘯 (绗?%1 娆?").arg(retryTask.retryCount));
            }
            processQueue();
            return;
        }

        TaskManager::instance().finishTask(dg, tid, result);
        emit taskFinished(dg, tid, result.success, result.isCancelled);
        activeTasks_.remove(tid);
        if (runningTaskId_ == tid) {
            runningTaskId_.clear();
        }
        processQueue();
    });

    connect(worker, &ExecuteWorker::finished, thread, &QThread::quit);
    connect(worker, &ExecuteWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
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

int TaskRunner::queuedCount() const {
    return queue_.size();
}
