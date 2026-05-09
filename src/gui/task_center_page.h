#pragma once
#include <QWidget>
#include <QTreeWidget>
#include <QTextEdit>
#include <QSplitter>
#include "task_manager.h"

class QPushButton;
class QLabel;

class TaskCenterPage : public QWidget {
    Q_OBJECT
public:
    explicit TaskCenterPage(QWidget* parent = nullptr);

    void setCurrentGroup(const QString& displayGroup);
    QString currentGroup() const { return currentGroup_; }

    void addTaskRow(const QString& taskId, const QString& actionDisplayName,
                    int status, const QString& startTime);
    void updateTaskRow(const QString& taskId, int status, const QString& endTime,
                       qint64 durationMs = 0);
    void updateTaskProgress(const QString& taskId, double percent);
    void removeTaskRows(const QStringList& taskIds);
    void refreshAll();
    void appendLog(const QString& taskId, const QString& message);
    void clearLogDisplay();
    void setLogForTask(const QString& taskId, const QStringList& logs);

signals:
    void rerunTaskRequested(const QString& taskId);
    void editTaskRequested(const QString& taskId);
    void deleteTasksRequested(const QStringList& taskIds);
    void clearHistoryRequested();
    void clearLogsRequested(const QString& taskId);
    void clearAllLogsRequested();

private slots:
    void onCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onCustomContextMenu(const QPoint& pos);

private:
    void setupUi();
    QTreeWidgetItem* findItemByTaskId(const QString& taskId) const;
    static QString statusIcon(int status);
    static QString statusText(int status);
    static QString statusColor(int status);

    QTreeWidget* taskTree_ = nullptr;
    QTextEdit* logDisplay_ = nullptr;
    QPushButton* rerunButton_ = nullptr;
    QPushButton* clearHistoryButton_ = nullptr;
    QPushButton* clearLogButton_ = nullptr;
    QLabel* taskCountLabel_ = nullptr;
    QLabel* logTaskLabel_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QString currentLogTaskId_;
    QString currentGroup_;
};
