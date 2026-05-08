#pragma once
#include <QObject>
#include <QString>
#include <QDateTime>
#include <vector>
#include <map>
#include <string>
#include <gis/framework/result.h>
#include <gis/framework/param_spec.h>

struct TaskLogEntry {
    int id = 0;
    QString taskId;
    QString timestamp;
    int level = 0;
    QString message;
};

struct TaskRecord {
    QString id;
    QString pluginName;
    QString actionKey;
    QString pluginDisplayName;
    QString actionDisplayName;
    std::map<std::string, gis::framework::ParamValue> params;
    gis::framework::Result result;
    QDateTime startTime;
    QDateTime endTime;
    enum Status { Pending, Running, Completed, Cancelled, Failed } status = Pending;
};

class TaskManager : public QObject {
    Q_OBJECT
public:
    static TaskManager& instance();

    QString submitTask(const QString& pluginName,
                       const QString& actionKey,
                       const std::map<std::string, gis::framework::ParamValue>& params,
                       const QString& pluginDisplayName = {},
                       const QString& actionDisplayName = {});
    void updateAndRerunTask(const QString& id,
                            const std::map<std::string, gis::framework::ParamValue>& newParams);
    void updateTaskStatus(const QString& id, TaskRecord::Status status);
    void finishTask(const QString& id, const gis::framework::Result& result);
    void deleteTasks(const QStringList& ids);
    void clearHistory();

    void appendLog(const QString& taskId, const QString& message, int level = 0);
    QList<TaskLogEntry> logsForTask(const QString& taskId) const;

    const TaskRecord findTask(const QString& id) const;
    QList<TaskRecord> recentTasks(int limit = 200) const;
    int taskCount() const;

signals:
    void taskSubmitted(const QString& id);
    void taskStarted(const QString& id);
    void taskFinished(const QString& id);
    void logAppended(const QString& taskId, const QString& message);

private:
    TaskManager(QObject* parent = nullptr);
};
