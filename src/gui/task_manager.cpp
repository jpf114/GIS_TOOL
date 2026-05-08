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
    : QObject(parent) {
    QString dbPath = QCoreApplication::applicationDirPath()
        + QStringLiteral("/gis_tasks.db");
    TaskDatabase::instance().initialize(dbPath);
}

QString TaskManager::submitTask(
    const QString& pluginName,
    const QString& actionKey,
    const std::map<std::string, gis::framework::ParamValue>& params,
    const QString& pluginDisplayName,
    const QString& actionDisplayName) {

    TaskRecord rec;
    rec.pluginName = pluginName;
    rec.actionKey = actionKey;
    rec.pluginDisplayName = pluginDisplayName.isEmpty() ? pluginName : pluginDisplayName;
    rec.actionDisplayName = actionDisplayName.isEmpty() ? actionKey : actionDisplayName;
    rec.params = params;
    rec.startTime = QDateTime::currentDateTime();
    rec.status = TaskRecord::Pending;

    QString id = TaskDatabase::instance().insertTask(rec);
    if (!id.isEmpty()) {
        emit taskSubmitted(id);
    }
    return id;
}

void TaskManager::updateAndRerunTask(
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

    TaskDatabase::instance().updateTaskParams(id, paramsJson,
        static_cast<int>(TaskRecord::Pending), startTime);
    emit taskSubmitted(id);
}

void TaskManager::updateTaskStatus(const QString& id, TaskRecord::Status status) {
    TaskDatabase::instance().updateTaskStatus(id, static_cast<int>(status));
    if (status == TaskRecord::Running) {
        emit taskStarted(id);
    }
}

void TaskManager::finishTask(const QString& id, const gis::framework::Result& result) {
    int status;
    if (result.success) status = TaskRecord::Completed;
    else if (result.isCancelled) status = TaskRecord::Cancelled;
    else status = TaskRecord::Failed;

    QString endTime = QDateTime::currentDateTime().toString(Qt::ISODate);

    TaskDatabase::instance().updateTaskResult(
        id, status,
        QString::fromUtf8(result.message),
        QString::fromUtf8(result.message),
        QString::fromUtf8(result.outputPath),
        endTime);

    emit taskFinished(id);
}

void TaskManager::deleteTasks(const QStringList& ids) {
    TaskDatabase::instance().deleteTasks(ids);
}

void TaskManager::clearHistory() {
    TaskDatabase::instance().clearHistory();
}

void TaskManager::appendLog(const QString& taskId, const QString& message, int level) {
    TaskDatabase::instance().appendLog(taskId, message, level);
    emit logAppended(taskId, message);
}

QList<TaskLogEntry> TaskManager::logsForTask(const QString& taskId) const {
    return TaskDatabase::instance().logsForTask(taskId);
}

const TaskRecord TaskManager::findTask(const QString& id) const {
    return TaskDatabase::instance().findTask(id);
}

QList<TaskRecord> TaskManager::recentTasks(int limit) const {
    return TaskDatabase::instance().recentTasks(limit);
}

int TaskManager::taskCount() const {
    return TaskDatabase::instance().taskCount();
}
