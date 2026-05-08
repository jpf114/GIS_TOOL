#include "task_manager.h"
#include "task_database.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

TaskManager& TaskManager::instance() {
    static TaskManager inst;
    return inst;
}

TaskManager::TaskManager(QObject* parent)
    : QObject(parent) {}

void TaskManager::initializeGroup(const QString& displayGroup) {
    TaskDatabase::instance().initializeGroup(displayGroup);
}

QString TaskManager::submitTask(
    const QString& displayGroup,
    const QString& pluginName,
    const QString& actionKey,
    const std::map<std::string, gis::framework::ParamValue>& params,
    const QString& pluginDisplayName,
    const QString& actionDisplayName) {

    TaskRecord rec;
    rec.displayGroup = displayGroup;
    rec.pluginName = pluginName;
    rec.actionKey = actionKey;
    rec.pluginDisplayName = pluginDisplayName.isEmpty() ? pluginName : pluginDisplayName;
    rec.actionDisplayName = actionDisplayName.isEmpty() ? actionKey : actionDisplayName;
    rec.params = params;
    rec.startTime = QDateTime::currentDateTime();
    rec.status = TaskRecord::Pending;

    QString id = TaskDatabase::instance().insertTask(displayGroup, rec);
    if (!id.isEmpty()) {
        emit taskSubmitted(displayGroup, id);
    }
    return id;
}

void TaskManager::updateAndRerunTask(
    const QString& displayGroup,
    const QString& id,
    const std::map<std::string, gis::framework::ParamValue>& newParams) {

    QJsonObject paramsObj;
    for (const auto& [key, value] : newParams) {
        std::visit([&paramsObj, &key](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) {
                paramsObj[QString::fromStdString(key)] = QString::fromStdString(v);
            } else if constexpr (std::is_same_v<T, int>) {
                paramsObj[QString::fromStdString(key)] = v;
            } else if constexpr (std::is_same_v<T, double>) {
                paramsObj[QString::fromStdString(key)] = v;
            } else if constexpr (std::is_same_v<T, bool>) {
                paramsObj[QString::fromStdString(key)] = v;
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                QJsonArray arr;
                for (const auto& s : v) arr.append(QString::fromStdString(s));
                paramsObj[QString::fromStdString(key)] = arr;
            } else if constexpr (std::is_same_v<T, std::array<double, 4>>) {
                QJsonArray arr;
                for (double d : v) arr.append(d);
                paramsObj[QString::fromStdString(key)] = arr;
            }
        }, value);
    }

    QString paramsJson = QJsonDocument(paramsObj).toJson(QJsonDocument::Compact);
    QString startTime = QDateTime::currentDateTime().toString(Qt::ISODate);

    TaskDatabase::instance().updateTaskParams(displayGroup, id, paramsJson,
        static_cast<int>(TaskRecord::Pending), startTime);
    emit taskSubmitted(displayGroup, id);
}

void TaskManager::updateTaskStatus(const QString& displayGroup,
                                    const QString& id, TaskRecord::Status status) {
    TaskDatabase::instance().updateTaskStatus(displayGroup, id, static_cast<int>(status));
    if (status == TaskRecord::Running) {
        emit taskStarted(displayGroup, id);
    }
}

void TaskManager::finishTask(const QString& displayGroup, const QString& id,
                              const gis::framework::Result& result) {
    int status;
    if (result.success) status = TaskRecord::Completed;
    else if (result.isCancelled) status = TaskRecord::Cancelled;
    else status = TaskRecord::Failed;

    QString endTime = QDateTime::currentDateTime().toString(Qt::ISODate);

    TaskDatabase::instance().updateTaskResult(
        displayGroup, id, status,
        QString::fromUtf8(result.message),
        QString::fromUtf8(result.message),
        QString::fromUtf8(result.outputPath),
        endTime);

    emit taskFinished(displayGroup, id);
}

void TaskManager::deleteTasks(const QString& displayGroup, const QStringList& ids) {
    TaskDatabase::instance().deleteTasks(displayGroup, ids);
}

void TaskManager::clearHistory(const QString& displayGroup) {
    TaskDatabase::instance().clearHistory(displayGroup);
}

void TaskManager::appendLog(const QString& displayGroup, const QString& taskId,
                             const QString& message, int level) {
    TaskDatabase::instance().appendLog(displayGroup, taskId, message, level);
    emit logAppended(displayGroup, taskId, message);
}

QList<TaskLogEntry> TaskManager::logsForTask(const QString& displayGroup,
                                              const QString& taskId) const {
    return TaskDatabase::instance().logsForTask(displayGroup, taskId);
}

const TaskRecord TaskManager::findTask(const QString& displayGroup,
                                        const QString& id) const {
    return TaskDatabase::instance().findTask(displayGroup, id);
}

QList<TaskRecord> TaskManager::recentTasks(const QString& displayGroup,
                                            int limit) const {
    return TaskDatabase::instance().recentTasks(displayGroup, limit);
}

int TaskManager::taskCount(const QString& displayGroup) const {
    return TaskDatabase::instance().taskCount(displayGroup);
}
