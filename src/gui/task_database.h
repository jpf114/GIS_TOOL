#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QSqlDatabase>
#include <QMap>
#include <QList>

struct TaskRecord;
struct TaskLogEntry;

class TaskDatabase : public QObject {
    Q_OBJECT
public:
    static TaskDatabase& instance();

    bool initializeGroup(const QString& displayGroup);
    void closeAll();

    QString insertTask(const QString& displayGroup, const TaskRecord& rec);
    bool updateTaskStatus(const QString& displayGroup, const QString& id, int status);
    bool updateTaskResult(const QString& displayGroup, const QString& id, int status,
                          const QString& resultMsg, const QString& resultRaw,
                          const QString& outputPath, const QString& endTime);
    bool updateTaskParams(const QString& displayGroup, const QString& id,
                          const QString& paramsJson, int status, const QString& startTime);
    bool deleteTasks(const QString& displayGroup, const QStringList& ids);
    bool clearHistory(const QString& displayGroup);

    int appendLog(const QString& displayGroup, const QString& taskId,
                  const QString& message, int level = 0);
    bool clearLogsForTask(const QString& displayGroup, const QString& taskId);
    bool clearAllLogs(const QString& displayGroup);

    QList<TaskLogEntry> logsForTask(const QString& displayGroup,
                                     const QString& taskId) const;
    QList<TaskRecord> recentTasks(const QString& displayGroup, int limit = 200) const;
    TaskRecord findTask(const QString& displayGroup, const QString& id) const;
    int taskCount(const QString& displayGroup) const;

    qint64 databaseFileSize(const QString& displayGroup) const;

private:
    TaskDatabase(QObject* parent = nullptr);

    QSqlDatabase databaseForGroup(const QString& displayGroup) const;
    bool createTables(const QString& displayGroup);
    void checkDatabaseSize(const QString& displayGroup);
    QString dbPathForGroup(const QString& displayGroup) const;

    QString baseDbPath_;
    QMap<QString, QString> groupConnectionNames_;
    QMap<QString, int> groupNextIds_;
};
