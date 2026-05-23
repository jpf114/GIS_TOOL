#include "task_center_page.h"
#include "task_manager.h"
#include "style_constants.h"
#include "gui_data_support.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QMenu>
#include <QMessageBox>
#include <QTime>
#include <QDateTime>
#include <QHeaderView>
#include <algorithm>

TaskCenterPage::TaskCenterPage(QWidget* parent)
    : QWidget(parent) {
    setupUi();
}

void TaskCenterPage::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    splitter_ = new QSplitter(Qt::Vertical);
    splitter_->setChildrenCollapsible(false);
    splitter_->setHandleWidth(1);

    auto* taskListCard = new QFrame;
    taskListCard->setObjectName(QStringLiteral("card"));
    auto* taskListLayout = new QVBoxLayout(taskListCard);
    taskListLayout->setContentsMargins(
        gis::style::Size::kCardPadding, gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding, gis::style::Size::kCardPadding);
    taskListLayout->setSpacing(gis::style::Size::kCardSpacing);

    auto* taskHeaderLayout = new QHBoxLayout;
    taskHeaderLayout->setSpacing(12);

    auto* taskTitleLabel = new QLabel(QStringLiteral("任务列表"));
    taskTitleLabel->setObjectName(QStringLiteral("cardTitle"));
    taskHeaderLayout->addWidget(taskTitleLabel);

    taskCountLabel_ = new QLabel(QStringLiteral("共 0 个任务"));
    taskCountLabel_->setObjectName(QStringLiteral("taskCountLabel"));
    taskHeaderLayout->addWidget(taskCountLabel_);
    taskHeaderLayout->addStretch();

    clearHistoryButton_ = new QPushButton(QStringLiteral("清空历史"));
    clearHistoryButton_->setObjectName(QStringLiteral("clearHistoryButton"));
    connect(clearHistoryButton_, &QPushButton::clicked, this, [this]() {
        emit clearHistoryRequested();
    });
    taskHeaderLayout->addWidget(clearHistoryButton_);

    taskListLayout->addLayout(taskHeaderLayout);

    taskTree_ = new QTreeWidget;
    taskTree_->setObjectName(QStringLiteral("taskTree"));
    taskTree_->setAlternatingRowColors(true);
    taskTree_->setRootIsDecorated(false);
    taskTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    taskTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    taskTree_->setHeaderLabels(QStringList()
        << QStringLiteral("状态")
        << QStringLiteral("子功能")
        << QStringLiteral("开始时间")
        << QStringLiteral("进度")
        << QStringLiteral("耗时"));
    taskTree_->header()->setStretchLastSection(true);
    taskTree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    taskTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    taskTree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    taskTree_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    taskTree_->setColumnWidth(0, 80);
    taskTree_->setColumnWidth(1, 160);
    taskTree_->setColumnWidth(2, 150);
    taskTree_->setColumnWidth(3, 80);

    connect(taskTree_, &QTreeWidget::currentItemChanged,
            this, &TaskCenterPage::onCurrentItemChanged);
    connect(taskTree_, &QTreeWidget::itemDoubleClicked,
            this, &TaskCenterPage::onItemDoubleClicked);
    connect(taskTree_, &QTreeWidget::customContextMenuRequested,
            this, &TaskCenterPage::onCustomContextMenu);

    taskListLayout->addWidget(taskTree_);

    splitter_->addWidget(taskListCard);

    auto* logCard = new QFrame;
    logCard->setObjectName(QStringLiteral("card"));
    auto* logLayout = new QVBoxLayout(logCard);
    logLayout->setContentsMargins(
        gis::style::Size::kCardPadding, gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding, gis::style::Size::kCardPadding);
    logLayout->setSpacing(gis::style::Size::kCardSpacing);

    auto* logHeaderLayout = new QHBoxLayout;
    logHeaderLayout->setSpacing(12);

    auto* logTitleLabel = new QLabel(QStringLiteral("执行日志"));
    logTitleLabel->setObjectName(QStringLiteral("cardTitle"));
    logHeaderLayout->addWidget(logTitleLabel);

    logTaskLabel_ = new QLabel;
    logTaskLabel_->setObjectName(QStringLiteral("logTaskLabel"));
    logHeaderLayout->addWidget(logTaskLabel_);
    logHeaderLayout->addStretch();

    clearLogButton_ = new QPushButton(QStringLiteral("清空日志"));
    clearLogButton_->setObjectName(QStringLiteral("clearLogButton"));
    connect(clearLogButton_, &QPushButton::clicked, this, [this]() {
        if (currentLogTaskId_.isEmpty()) {
            emit clearAllLogsRequested();
        } else {
            emit clearLogsRequested(currentLogTaskId_);
        }
    });
    logHeaderLayout->addWidget(clearLogButton_);

    logLayout->addLayout(logHeaderLayout);

    logDisplay_ = new QTextEdit;
    logDisplay_->setObjectName(QStringLiteral("logTerminal"));
    logDisplay_->setReadOnly(true);
    logLayout->addWidget(logDisplay_);

    rerunButton_ = new QPushButton(QStringLiteral("重新执行"));
    rerunButton_->setObjectName(QStringLiteral("rerunButton"));
    rerunButton_->setEnabled(false);
    connect(rerunButton_, &QPushButton::clicked, this, [this]() {
        auto* current = taskTree_->currentItem();
        if (current) {
            QString taskId = current->data(0, Qt::UserRole).toString();
            if (!taskId.isEmpty()) {
                emit rerunTaskRequested(taskId);
            }
        }
    });
    logLayout->addWidget(rerunButton_);

    splitter_->addWidget(logCard);
    splitter_->setStretchFactor(0, 3);
    splitter_->setStretchFactor(1, 2);

    mainLayout->addWidget(splitter_);
}

void TaskCenterPage::setCurrentGroup(const QString& displayGroup) {
    if (currentGroup_ == displayGroup) return;
    currentGroup_ = displayGroup;
    refreshAll();
}

void TaskCenterPage::addTaskRow(const QString& taskId, const QString& actionDisplayName,
                                 int status, const QString& startTime) {
    auto* item = new QTreeWidgetItem;
    item->setData(0, Qt::UserRole, taskId);
    item->setText(0, statusText(status));
    item->setText(1, actionDisplayName);
    item->setText(2, startTime);
    item->setText(3, QStringLiteral("-"));
    item->setText(4, QStringLiteral("-"));

    QFont f = item->font(0);
    f.setBold(true);
    item->setFont(0, f);
    item->setForeground(0, QColor(statusColor(status)));

    taskTree_->insertTopLevelItem(0, item);
    taskTree_->setCurrentItem(item);

    int count = taskTree_->topLevelItemCount();
    taskCountLabel_->setText(QStringLiteral("共 %1 个任务").arg(count));
}

void TaskCenterPage::updateTaskRow(const QString& taskId, int status,
                                    const QString& endTime, qint64 durationMs) {
    auto* item = findItemByTaskId(taskId);
    if (!item) return;

    item->setText(0, statusText(status));
    item->setForeground(0, QColor(statusColor(status)));

    if (durationMs > 0) {
        qint64 secs = durationMs / 1000;
        qint64 ms = durationMs % 1000;
        if (secs < 60) {
            double seconds = durationMs / 1000.0;
            item->setText(4, QStringLiteral("%1秒").arg(seconds, 0, 'f', 1));
        } else {
            int mins = static_cast<int>(secs) / 60;
            int remainSecs = static_cast<int>(secs) % 60;
            item->setText(4, QStringLiteral("%1分%2秒").arg(mins).arg(remainSecs));
        }
    } else {
        QString startStr = item->text(2);
        QDateTime startDt = QDateTime::fromString(startStr, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!startDt.isValid()) {
            startDt = QDateTime::fromString(startStr, QStringLiteral("HH:mm:ss"));
        }
        if (!startDt.isValid()) {
            startDt = QDateTime::currentDateTime();
        }
        QDateTime endDt = QDateTime::fromString(endTime, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!endDt.isValid()) {
            endDt = QDateTime::fromString(endTime, QStringLiteral("HH:mm:ss"));
        }
        if (!endDt.isValid()) {
            endDt = QDateTime::currentDateTime();
        }
        qint64 secs = startDt.secsTo(endDt);
        if (secs < 0) secs = 0;
        if (secs < 60) {
            item->setText(4, QStringLiteral("%1秒").arg(secs));
        } else {
            item->setText(4, QStringLiteral("%1分%2秒").arg(secs / 60).arg(secs % 60));
        }
    }
    item->setText(3, status == TaskRecord::Completed ? QStringLiteral("100%") : QStringLiteral("-"));
}

void TaskCenterPage::updateTaskProgress(const QString& taskId, double percent) {
    auto* item = findItemByTaskId(taskId);
    if (!item) return;

    int value = std::clamp(static_cast<int>(percent * 100.0), 0, 100);
    item->setText(3, QStringLiteral("%1%").arg(value));
}

void TaskCenterPage::removeTaskRows(const QStringList& taskIds) {
    for (const auto& id : taskIds) {
        auto* item = findItemByTaskId(id);
        if (item) {
            delete item;
        }
    }
    int count = taskTree_->topLevelItemCount();
    taskCountLabel_->setText(QStringLiteral("共 %1 个任务").arg(count));
}

void TaskCenterPage::refreshAll() {
    taskTree_->clear();
    logDisplay_->clear();
    currentLogTaskId_.clear();

    if (currentGroup_.isEmpty()) {
        taskCountLabel_->setText(QStringLiteral("共 0 个任务"));
        return;
    }

    auto tasks = TaskManager::instance().recentTasks(currentGroup_);
    for (const auto& rec : tasks) {
        auto* item = new QTreeWidgetItem;
        item->setData(0, Qt::UserRole, rec.id);
        item->setText(0, statusText(static_cast<int>(rec.status)));
        item->setText(1, rec.actionDisplayName.isEmpty() ? rec.actionKey : rec.actionDisplayName);
        item->setText(2, rec.startTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));

        if (rec.status == TaskRecord::Running) {
            item->setText(3, QStringLiteral("运行中"));
        } else if (rec.status == TaskRecord::Completed) {
            item->setText(3, QStringLiteral("100%"));
        } else {
            item->setText(3, QStringLiteral("-"));
        }

        if (rec.durationMs > 0) {
            qint64 secs = rec.durationMs / 1000;
            if (secs < 60) {
                double seconds = rec.durationMs / 1000.0;
                item->setText(4, QStringLiteral("%1秒").arg(seconds, 0, 'f', 1));
            } else {
                int mins = static_cast<int>(secs) / 60;
                int remainSecs = static_cast<int>(secs) % 60;
                item->setText(4, QStringLiteral("%1分%2秒").arg(mins).arg(remainSecs));
            }
        } else if (rec.endTime.isValid() && rec.startTime.isValid()) {
            qint64 secs = rec.startTime.secsTo(rec.endTime);
            if (secs < 0) secs = 0;
            if (secs < 60) {
                item->setText(4, QStringLiteral("%1秒").arg(secs));
            } else {
                item->setText(4, QStringLiteral("%1分%2秒").arg(secs / 60).arg(secs % 60));
            }
        } else {
            item->setText(4, QStringLiteral("-"));
        }

        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        item->setForeground(0, QColor(statusColor(static_cast<int>(rec.status))));

        taskTree_->addTopLevelItem(item);
    }

    int count = taskTree_->topLevelItemCount();
    taskCountLabel_->setText(QStringLiteral("共 %1 个任务").arg(count));
}

void TaskCenterPage::appendLog(const QString& taskId, const QString& message) {
    if (taskId == currentLogTaskId_) {
        QString timestamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
        QString html;
        if (message.contains(QStringLiteral("失败")) || message.contains(QStringLiteral("错误")) || message.contains(QStringLiteral("error"))) {
            html = QStringLiteral("<span style='color:#FF6B6B;'>[%1] %2</span>").arg(timestamp, message.toHtmlEscaped());
        } else if (message.contains(QStringLiteral("警告")) || message.contains(QStringLiteral("warning"))) {
            html = QStringLiteral("<span style='color:#FFD93D;'>[%1] %2</span>").arg(timestamp, message.toHtmlEscaped());
        } else if (message.contains(QStringLiteral("成功")) || message.contains(QStringLiteral("完成"))) {
            html = QStringLiteral("<span style='color:#6BCB77;'>[%1] %2</span>").arg(timestamp, message.toHtmlEscaped());
        } else {
            html = QStringLiteral("<span style='color:#8B9BB4;'>[%1]</span> %2").arg(timestamp, message.toHtmlEscaped());
        }
        logDisplay_->append(html);
    }
}

void TaskCenterPage::clearLogDisplay() {
    logDisplay_->clear();
}

void TaskCenterPage::setLogForTask(const QString& taskId, const QStringList& logs) {
    currentLogTaskId_ = taskId;
    logDisplay_->clear();
    for (const auto& msg : logs) {
        logDisplay_->append(msg.toHtmlEscaped());
    }
}

void TaskCenterPage::onCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* /*previous*/) {
    if (!current) {
        rerunButton_->setEnabled(false);
        logTaskLabel_->clear();
        return;
    }

    QString taskId = current->data(0, Qt::UserRole).toString();
    if (taskId.isEmpty()) return;

    currentLogTaskId_ = taskId;

    auto logs = TaskManager::instance().logsForTask(currentGroup_, taskId);
    logDisplay_->clear();
    for (const auto& entry : logs) {
        QString timestamp = QDateTime::fromString(entry.timestamp, Qt::ISODate).toString(QStringLiteral("HH:mm:ss"));
        QString html;
        if (entry.level == 2) {
            html = QStringLiteral("<span style='color:#FF6B6B;'>[%1] %2</span>").arg(timestamp, entry.message.toHtmlEscaped());
        } else if (entry.level == 1) {
            html = QStringLiteral("<span style='color:#FFD93D;'>[%1] %2</span>").arg(timestamp, entry.message.toHtmlEscaped());
        } else {
            html = QStringLiteral("<span style='color:#8B9BB4;'>[%1]</span> %2").arg(timestamp, entry.message.toHtmlEscaped());
        }
        logDisplay_->append(html);
    }

    int status = TaskManager::instance().findTask(currentGroup_, taskId).status;
    bool canRerun = (status == TaskRecord::Completed ||
                     status == TaskRecord::Failed ||
                     status == TaskRecord::Cancelled);
    rerunButton_->setEnabled(canRerun);

    logTaskLabel_->setText(QStringLiteral("任务: %1").arg(taskId));
}

void TaskCenterPage::onItemDoubleClicked(QTreeWidgetItem* item, int /*column*/) {
    if (!item) return;
    QString taskId = item->data(0, Qt::UserRole).toString();
    if (taskId.isEmpty()) return;

    int status = TaskManager::instance().findTask(currentGroup_, taskId).status;
    if (status == TaskRecord::Running || status == TaskRecord::Pending) return;

    emit editTaskRequested(taskId);
}

void TaskCenterPage::onCustomContextMenu(const QPoint& pos) {
    auto* current = taskTree_->itemAt(pos);
    if (!current) return;

    QString taskId = current->data(0, Qt::UserRole).toString();
    if (taskId.isEmpty()) return;

    int status = TaskManager::instance().findTask(currentGroup_, taskId).status;
    bool canRerun = (status == TaskRecord::Completed ||
                     status == TaskRecord::Failed ||
                     status == TaskRecord::Cancelled);

    QMenu menu;
    menu.setStyleSheet(
        QStringLiteral(
            "QMenu { background: white; border: 1px solid %1; border-radius: 6px; padding: 4px 0; }"
            "QMenu::item { padding: 8px 28px; color: %2; font-size: 13px; }"
            "QMenu::item:selected { background: %3; color: %4; }"
            "QMenu::item:disabled { color: %5; }"
            "QMenu::separator { height: 1px; background: %1; margin: 4px 8px; }")
        .arg(gis::style::Color::kCardBorder)
        .arg(gis::style::Color::kTextPrimary)
        .arg(gis::style::Color::kPrimaryLight)
        .arg(gis::style::Color::kPrimary)
        .arg(gis::style::Color::kDisabledText));
    auto* rerunAction = menu.addAction(QStringLiteral("重新执行"));
    rerunAction->setEnabled(canRerun);
    auto* editAction = menu.addAction(QStringLiteral("编辑参数"));
    editAction->setEnabled(canRerun);
    menu.addSeparator();
    auto* deleteAction = menu.addAction(QStringLiteral("删除任务"));
    auto* clearLogAction = menu.addAction(QStringLiteral("清空日志"));

    auto* selected = menu.exec(taskTree_->mapToGlobal(pos));
    if (!selected) return;

    if (selected == rerunAction) {
        emit rerunTaskRequested(taskId);
    } else if (selected == editAction) {
        emit editTaskRequested(taskId);
    } else if (selected == deleteAction) {
        QStringList ids;
        for (auto* item : taskTree_->selectedItems()) {
            QString id = item->data(0, Qt::UserRole).toString();
            if (!id.isEmpty()) ids << id;
        }
        if (!ids.isEmpty()) {
            emit deleteTasksRequested(ids);
        }
    } else if (selected == clearLogAction) {
        emit clearLogsRequested(taskId);
    }
}

QTreeWidgetItem* TaskCenterPage::findItemByTaskId(const QString& taskId) const {
    for (int i = 0; i < taskTree_->topLevelItemCount(); ++i) {
        auto* item = taskTree_->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == taskId) {
            return item;
        }
    }
    return nullptr;
}

QString TaskCenterPage::statusIcon(int status) {
    return gis::gui::taskStatusIcon(static_cast<gis::gui::TaskStatusKind>(status));
}

QString TaskCenterPage::statusText(int status) {
    return gis::gui::taskStatusText(static_cast<gis::gui::TaskStatusKind>(status));
}

QString TaskCenterPage::statusColor(int status) {
    return gis::gui::taskStatusColor(static_cast<gis::gui::TaskStatusKind>(status));
}
