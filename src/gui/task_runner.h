#pragma once
#include <QObject>
#include <QString>
#include <QMap>
#include <memory>
#include <map>
#include <string>
#include <gis/framework/param_spec.h>

namespace gis::framework {
class IGisPlugin;
}

class QtProgressReporter;

struct TaskContext {
    QString taskId;
    QString displayGroup;
    std::unique_ptr<QtProgressReporter> reporter;
};

class TaskRunner : public QObject {
    Q_OBJECT
public:
    static TaskRunner& instance();

    QString run(gis::framework::IGisPlugin* plugin,
                const QString& displayGroup,
                const QString& pluginName,
                const QString& actionKey,
                const std::map<std::string, gis::framework::ParamValue>& params,
                const QString& pluginDisplayName,
                const QString& actionDisplayName);

    void cancelTask(const QString& taskId);
    bool isRunning() const;
    QString runningTaskId() const;

signals:
    void taskStarted(const QString& displayGroup, const QString& taskId);
    void taskFinished(const QString& displayGroup, const QString& taskId,
                      bool success, bool cancelled);
    void taskProgressChanged(const QString& taskId, double percent);
    void taskLogMessage(const QString& displayGroup, const QString& taskId,
                        const QString& msg);

private:
    TaskRunner(QObject* parent = nullptr);

    QMap<QString, std::shared_ptr<TaskContext>> activeTasks_;
    QString runningTaskId_;
};
