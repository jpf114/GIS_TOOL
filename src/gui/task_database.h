#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QSqlDatabase>
#include <QList>

struct TaskRecord;
struct TaskLogEntry;

class TaskDatabase : public QObject {
    Q_OBJECT
public:
    static TaskDatabase& instance();

    bool initialize(const QString& dbPath);
    void close();

    QString insertTask(const TaskRecord& rec);
    bool updateTaskStatus(const QString& id, int status);
    bool updateTaskResult(const QString& id, int status, const QString& resultMsg,
                          const QString& resultRaw, const QString& outputPath,
                          const QString& endTime);
    bool updateTaskParams(const QString& id, const QString& paramsJson,
                          int status, const QString& startTime);
    bool deleteTasks(const QStringList& ids);
    bool clearHistory();

    int appendLog(const QString& taskId, const QString& message, int level = 0);
    bool clearLogsForTask(const QString& taskId);
    bool clearAllLogs();
    QList<TaskLogEntry> logsForTask(const QString& taskId) const;

    QList<TaskRecord> recentTasks(int limit = 200) const;
    TaskRecord findTask(const QString& id) const;
    int taskCount() const;

    qint64 databaseFileSize() const;

private:
    TaskDatabase(QObject* parent = nullptr);
    bool createTables();
    void checkDatabaseSize();

    QSqlDatabase db_;
    QString dbPath_;
    int nextId_ = 1;
};
