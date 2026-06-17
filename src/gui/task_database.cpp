#include "task_database.h"
#include "task_manager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

TaskDatabase& TaskDatabase::instance() {
    static TaskDatabase inst;
    return inst;
}

TaskDatabase::TaskDatabase(QObject* parent)
    : QObject(parent) {}

QSqlDatabase TaskDatabase::databaseForGroup(const QString& displayGroup) const {
    QString connName = groupConnectionNames_.value(displayGroup);
    if (connName.isEmpty()) {
        return QSqlDatabase();
    }
    return QSqlDatabase::database(connName);
}

QString TaskDatabase::dbPathForGroup(const QString& displayGroup) const {
    // 拒绝包含路径遍历字符的 displayGroup
    if (displayGroup.contains(QLatin1String("..")) ||
        displayGroup.contains(QLatin1Char('/')) ||
        displayGroup.contains(QLatin1Char('\\')) ||
        displayGroup.contains(QLatin1Char(':'))) {
        qWarning() << "Invalid displayGroup (path traversal risk):" << displayGroup;
        return {};
    }
    return baseDbPath_ + QStringLiteral("/gis_tasks_%1.db").arg(displayGroup);
}

bool TaskDatabase::initializeGroup(const QString& displayGroup) {
    if (groupConnectionNames_.contains(displayGroup)) {
        return true;
    }

    if (baseDbPath_.isEmpty()) {
        baseDbPath_ = QCoreApplication::applicationDirPath() + QStringLiteral("/task_data");
        QDir().mkpath(baseDbPath_);
    }

    QString connName = QStringLiteral("tasks_%1").arg(displayGroup);
    QString dbPath = dbPathForGroup(displayGroup);

    if (dbPath.isEmpty()) {
        qWarning() << "Rejected database path for displayGroup:" << displayGroup;
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(dbPath);

    if (!db.open()) {
        qWarning() << "Failed to open database:" << dbPath;
        return false;
    }

    db.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    db.exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    groupConnectionNames_[displayGroup] = connName;
    groupNextIds_[displayGroup] = 1;

    if (!createTables(displayGroup)) {
        return false;
    }

    QSqlQuery query(db);
    query.exec(QStringLiteral("SELECT MAX(CAST(SUBSTR(id, 2) AS INTEGER)) FROM tasks"));
    if (query.next()) {
        bool ok = false;
        int maxId = query.value(0).toInt(&ok);
        if (ok && maxId > 0) {
            groupNextIds_[displayGroup] = maxId + 1;
        }
    }

    checkDatabaseSize(displayGroup);
    return true;
}

void TaskDatabase::closeAll() {
    for (auto it = groupConnectionNames_.constBegin();
         it != groupConnectionNames_.constEnd(); ++it) {
        QSqlDatabase db = QSqlDatabase::database(it.value());
        if (db.isOpen()) {
            db.close();
        }
        QSqlDatabase::removeDatabase(it.value());
    }
    groupConnectionNames_.clear();
    groupNextIds_.clear();
}

bool TaskDatabase::createTables(const QString& displayGroup) {
    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return false;

    QSqlQuery query(db);

    bool ok = query.exec(
        QStringLiteral("CREATE TABLE IF NOT EXISTS tasks ("
            "id TEXT PRIMARY KEY,"
            "plugin_name TEXT NOT NULL,"
            "action_key TEXT NOT NULL,"
            "plugin_display_name TEXT,"
            "action_display_name TEXT,"
            "params TEXT NOT NULL,"
            "status INTEGER NOT NULL DEFAULT 0,"
            "result_msg TEXT,"
            "result_raw TEXT,"
            "output_path TEXT,"
            "start_time TEXT,"
            "end_time TEXT,"
            "duration_ms INTEGER)"));
    if (!ok) {
        qWarning() << "Failed to create tasks table:" << query.lastError().text();
        return false;
    }

    ok = query.exec(
        QStringLiteral("CREATE TABLE IF NOT EXISTS task_logs ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "task_id TEXT NOT NULL,"
            "timestamp TEXT NOT NULL,"
            "level INTEGER NOT NULL DEFAULT 0,"
            "message TEXT NOT NULL,"
            "FOREIGN KEY (task_id) REFERENCES tasks(id) ON DELETE CASCADE)"));
    if (!ok) {
        qWarning() << "Failed to create task_logs table:" << query.lastError().text();
        return false;
    }

    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_task_logs_task_id ON task_logs(task_id)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tasks_status ON tasks(status)"));
    query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tasks_start_time ON tasks(start_time)"));

    QStringList existingColumns;
    QSqlQuery colQuery(db);
    colQuery.exec(QStringLiteral("PRAGMA table_info(tasks)"));
    while (colQuery.next()) {
        existingColumns << colQuery.value(1).toString();
    }
    if (!existingColumns.contains(QStringLiteral("plugin_display_name"))) {
        query.exec(QStringLiteral("ALTER TABLE tasks ADD COLUMN plugin_display_name TEXT"));
    }
    if (!existingColumns.contains(QStringLiteral("action_display_name"))) {
        query.exec(QStringLiteral("ALTER TABLE tasks ADD COLUMN action_display_name TEXT"));
    }
    if (!existingColumns.contains(QStringLiteral("duration_ms"))) {
        query.exec(QStringLiteral("ALTER TABLE tasks ADD COLUMN duration_ms INTEGER"));
    }

    return true;
}

void TaskDatabase::checkDatabaseSize(const QString& displayGroup) {
    qint64 maxSize = 50 * 1024 * 1024;
    if (databaseFileSize(displayGroup) > maxSize) {
        QSqlDatabase db = databaseForGroup(displayGroup);
        QSqlQuery query(db);
        query.exec(QStringLiteral(
            "DELETE FROM tasks WHERE status IN (2, 3, 4) "
            "ORDER BY start_time ASC"));
        query.exec(QStringLiteral("VACUUM"));
    }
}

QString TaskDatabase::insertTask(const QString& displayGroup, const TaskRecord& rec) {
    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return {};

    QString id = QStringLiteral("T%1").arg(groupNextIds_[displayGroup]++, 4, 10, QLatin1Char('0'));

    QJsonObject paramsObj;
    for (const auto& [key, value] : rec.params) {
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

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO tasks (id, plugin_name, action_key, plugin_display_name, action_display_name, params, status, start_time) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(id);
    query.addBindValue(rec.pluginName);
    query.addBindValue(rec.actionKey);
    query.addBindValue(rec.pluginDisplayName);
    query.addBindValue(rec.actionDisplayName);
    query.addBindValue(paramsJson);
    query.addBindValue(static_cast<int>(rec.status));
    query.addBindValue(rec.startTime.toString(Qt::ISODate));

    if (!query.exec()) {
        qWarning() << "Failed to insert task:" << query.lastError().text();
        return {};
    }

    return id;
}

bool TaskDatabase::updateTaskStatus(const QString& displayGroup, const QString& id, int status) {
    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE tasks SET status=? WHERE id=?"));
    query.addBindValue(status);
    query.addBindValue(id);
    return query.exec();
}

bool TaskDatabase::updateTaskResult(const QString& displayGroup, const QString& id, int status,
                                     const QString& resultMsg, const QString& resultRaw,
                                     const QString& outputPath, const QString& endTime,
                                     qint64 durationMs) {
    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE tasks SET status=?, result_msg=?, result_raw=?, output_path=?, end_time=?, duration_ms=? WHERE id=?"));
    query.addBindValue(status);
    query.addBindValue(resultMsg);
    query.addBindValue(resultRaw);
    query.addBindValue(outputPath);
    query.addBindValue(endTime);
    query.addBindValue(durationMs);
    query.addBindValue(id);
    return query.exec();
}

bool TaskDatabase::updateTaskParams(const QString& displayGroup, const QString& id,
                                     const QString& paramsJson, int status, const QString& startTime) {
    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM task_logs WHERE task_id=?"));
    query.addBindValue(id);
    query.exec();

    query.prepare(QStringLiteral(
        "UPDATE tasks SET params=?, status=?, result_msg=NULL, result_raw=NULL, "
        "output_path=NULL, start_time=?, end_time=NULL WHERE id=?"));
    query.addBindValue(paramsJson);
    query.addBindValue(status);
    query.addBindValue(startTime);
    query.addBindValue(id);
    return query.exec();
}

bool TaskDatabase::deleteTasks(const QString& displayGroup, const QStringList& ids) {
    if (ids.isEmpty()) return true;

    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return false;

    QStringList placeholders;
    for (int i = 0; i < ids.size(); ++i) {
        placeholders << QStringLiteral("?");
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM tasks WHERE id IN (%1)")
        .arg(placeholders.join(QStringLiteral(", "))));
    for (const auto& id : ids) {
        query.addBindValue(id);
    }
    return query.exec();
}

bool TaskDatabase::clearHistory(const QString& displayGroup) {
    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return false;

    QSqlQuery query(db);
    query.exec(QStringLiteral("DELETE FROM task_logs WHERE task_id IN "
        "(SELECT id FROM tasks WHERE status NOT IN (0, 1))"));
    return query.exec(QStringLiteral("DELETE FROM tasks WHERE status NOT IN (0, 1)"));
}

int TaskDatabase::appendLog(const QString& displayGroup, const QString& taskId,
                             const QString& message, int level) {
    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return -1;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO task_logs (task_id, timestamp, level, message) VALUES (?, ?, ?, ?)"));
    query.addBindValue(taskId);
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(level);
    query.addBindValue(message);

    if (query.exec()) {
        return query.lastInsertId().toInt();
    }
    return -1;
}

bool TaskDatabase::clearLogsForTask(const QString& displayGroup, const QString& taskId) {
    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return false;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM task_logs WHERE task_id=?"));
    query.addBindValue(taskId);
    return query.exec();
}

bool TaskDatabase::clearAllLogs(const QString& displayGroup) {
    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return false;

    QSqlQuery query(db);
    return query.exec(QStringLiteral("DELETE FROM task_logs"));
}

QList<TaskLogEntry> TaskDatabase::logsForTask(const QString& displayGroup,
                                               const QString& taskId) const {
    QList<TaskLogEntry> result;
    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return result;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id, task_id, timestamp, level, message FROM task_logs "
        "WHERE task_id=? ORDER BY id ASC"));
    query.addBindValue(taskId);

    if (query.exec()) {
        while (query.next()) {
            TaskLogEntry entry;
            entry.id = query.value(0).toInt();
            entry.taskId = query.value(1).toString();
            entry.timestamp = query.value(2).toString();
            entry.level = query.value(3).toInt();
            entry.message = query.value(4).toString();
            result.append(entry);
        }
    }
    return result;
}

static std::map<std::string, gis::framework::ParamValue> parseParamsFromJson(const QString& json) {
    std::map<std::string, gis::framework::ParamValue> params;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return params;

    QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        std::string key = it.key().toStdString();
        const QJsonValue& val = it.value();
        if (val.isString()) {
            params[key] = val.toString().toStdString();
        } else if (val.isDouble()) {
            double d = val.toDouble();
            if (d == static_cast<int>(d)) {
                params[key] = static_cast<int>(d);
            } else {
                params[key] = d;
            }
        } else if (val.isBool()) {
            params[key] = val.toBool();
        } else if (val.isArray()) {
            QJsonArray arr = val.toArray();
            if (arr.size() == 4 && arr[0].isDouble()) {
                bool allDouble = true;
                std::array<double, 4> extent{};
                for (int i = 0; i < 4; ++i) {
                    if (!arr[i].isDouble()) { allDouble = false; break; }
                    extent[i] = arr[i].toDouble();
                }
                if (allDouble) {
                    params[key] = extent;
                    continue;
                }
            }
            std::vector<std::string> vec;
            for (const auto& item : arr) {
                vec.push_back(item.toString().toStdString());
            }
            params[key] = vec;
        }
    }
    return params;
}

static TaskRecord recordFromQuery(const QSqlQuery& query) {
    TaskRecord rec;
    rec.id = query.value(0).toString();
    rec.pluginName = query.value(1).toString();
    rec.actionKey = query.value(2).toString();
    rec.pluginDisplayName = query.value(3).toString();
    rec.actionDisplayName = query.value(4).toString();
    rec.params = parseParamsFromJson(query.value(5).toString());
    rec.status = static_cast<TaskRecord::Status>(query.value(6).toInt());
    rec.result.message = query.value(7).toString().toStdString();
    rec.result.outputPath = query.value(9).toString().toStdString();
    rec.result.success = (rec.status == TaskRecord::Completed);
    rec.result.isCancelled = (rec.status == TaskRecord::Cancelled);
    rec.startTime = QDateTime::fromString(query.value(10).toString(), Qt::ISODate);
    rec.endTime = QDateTime::fromString(query.value(11).toString(), Qt::ISODate);
    rec.durationMs = query.value(12).toLongLong();
    return rec;
}

QList<TaskRecord> TaskDatabase::recentTasks(const QString& displayGroup, int limit) const {
    QList<TaskRecord> result;
    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return result;

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id, plugin_name, action_key, plugin_display_name, action_display_name, "
        "params, status, result_msg, result_raw, output_path, start_time, end_time, duration_ms "
        "FROM tasks ORDER BY start_time DESC LIMIT ?"));
    query.addBindValue(limit);

    if (query.exec()) {
        while (query.next()) {
            result.append(recordFromQuery(query));
        }
    }
    return result;
}

TaskRecord TaskDatabase::findTask(const QString& displayGroup, const QString& id) const {
    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return TaskRecord{};

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT id, plugin_name, action_key, plugin_display_name, action_display_name, "
        "params, status, result_msg, result_raw, output_path, start_time, end_time, duration_ms "
        "FROM tasks WHERE id=?"));
    query.addBindValue(id);

    if (query.exec() && query.next()) {
        return recordFromQuery(query);
    }
    return TaskRecord{};
}

int TaskDatabase::taskCount(const QString& displayGroup) const {
    QSqlDatabase db = databaseForGroup(displayGroup);
    if (!db.isOpen()) return 0;

    QSqlQuery query(db);
    query.exec(QStringLiteral("SELECT COUNT(*) FROM tasks"));
    if (query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

qint64 TaskDatabase::databaseFileSize(const QString& displayGroup) const {
    return QFileInfo(dbPathForGroup(displayGroup)).size();
}
