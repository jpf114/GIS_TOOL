#include "progress_dialog.h"
#include "task_runner.h"
#include "style_constants.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <algorithm>

ProgressDialog::ProgressDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("鎵ц涓?.."));
    setMinimumWidth(450);
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);
    setStyleSheet(gis::style::globalStyleSheet());

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    statusLabel_ = new QLabel(QStringLiteral("姝ｅ湪鎵ц锛岃绋嶅€?.."));
    statusLabel_->setObjectName(QStringLiteral("heroMeta"));
    layout->addWidget(statusLabel_);

    batchProgressLabel_ = new QLabel;
    batchProgressLabel_->setObjectName(QStringLiteral("heroMeta"));
    batchProgressLabel_->setVisible(false);
    layout->addWidget(batchProgressLabel_);

    batchProgressBar_ = new QProgressBar;
    batchProgressBar_->setRange(0, 100);
    batchProgressBar_->setValue(0);
    batchProgressBar_->setVisible(false);
    batchProgressBar_->setFormat(QStringLiteral("鏁翠綋杩涘害: %p%"));
    layout->addWidget(batchProgressBar_);

    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    layout->addWidget(progressBar_);

    logEdit_ = new QTextEdit;
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumHeight(150);
    logEdit_->setObjectName(QStringLiteral("logTerminal"));
    layout->addWidget(logEdit_);

    auto* btnBox = new QDialogButtonBox;
    cancelButton_ = new QPushButton(QStringLiteral("鍙栨秷"));
    cancelButton_->setObjectName(QStringLiteral("secondaryButton"));
    forceQuitButton_ = new QPushButton(QStringLiteral("寮哄埗缁堟"));
    forceQuitButton_->setObjectName(QStringLiteral("secondaryButton"));
    forceQuitButton_->setVisible(false);
    btnBox->addButton(cancelButton_, QDialogButtonBox::ActionRole);
    btnBox->addButton(forceQuitButton_, QDialogButtonBox::DestructiveRole);
    layout->addWidget(btnBox);

    connect(cancelButton_, &QPushButton::clicked, this, [this]() {
        QString tid = TaskRunner::instance().runningTaskId();
        if (!tid.isEmpty()) {
            TaskRunner::instance().cancelTask(tid);
        }
        cancelButton_->setEnabled(false);
        statusLabel_->setText(QStringLiteral("姝ｅ湪鍙栨秷..."));
    });

    connect(forceQuitButton_, &QPushButton::clicked, this, &QDialog::reject);
}

void ProgressDialog::setFinished(const QString& message, bool success, bool cancelled) {
    if (cancelled) {
        statusLabel_->setText(QStringLiteral("宸插彇娑?));
    } else if (success) {
        statusLabel_->setText(QStringLiteral("鎵ц瀹屾垚"));
    } else {
        statusLabel_->setText(QStringLiteral("鎵ц澶辫触"));
    }

    progressBar_->setRange(0, 100);
    progressBar_->setValue(success ? 100 : 0);

    cancelButton_->setVisible(false);
    forceQuitButton_->setVisible(false);

    auto* closeBtn = new QPushButton(QStringLiteral("鍏抽棴"));
    closeBtn->setObjectName(QStringLiteral("primaryButton"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto* btnBox = findChild<QDialogButtonBox*>();
    if (btnBox) {
        btnBox->clear();
        btnBox->addButton(closeBtn, QDialogButtonBox::AcceptRole);
    }
}

void ProgressDialog::updateProgress(double percent) {
    int value = std::clamp(static_cast<int>(percent * 100.0), 0, 100);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(value);
}

void ProgressDialog::setBatchMode(int totalCount) {
    batchTotal_ = totalCount;
    batchProgressBar_->setVisible(true);
    batchProgressLabel_->setVisible(true);
    batchProgressBar_->setRange(0, totalCount);
    batchProgressBar_->setValue(0);
    batchProgressLabel_->setText(
        QStringLiteral("鎵归噺澶勭悊: 鍏?%1 涓枃浠?).arg(totalCount));
    progressBar_->setFormat(QStringLiteral("褰撳墠: %p%"));
}

void ProgressDialog::updateBatchProgress(int completedCount, int failedCount, const QString& currentFile) {
    batchProgressBar_->setValue(completedCount);
    int succeeded = completedCount - failedCount;
    batchProgressLabel_->setText(
        QStringLiteral("鎵归噺澶勭悊: %1/%2 瀹屾垚 (鎴愬姛 %3, 澶辫触 %4)")
            .arg(completedCount).arg(batchTotal_).arg(succeeded).arg(failedCount));
    if (!currentFile.isEmpty()) {
        statusLabel_->setText(
            QStringLiteral("姝ｅ湪澶勭悊: %1").arg(currentFile));
    }
}

void ProgressDialog::setBatchFinished(int total, int succeeded, int failed) {
    batchProgressBar_->setValue(total);
    batchProgressLabel_->setText(
        QStringLiteral("鎵归噺澶勭悊瀹屾垚: 鍏?%1 涓? 鎴愬姛 %2, 澶辫触 %3")
            .arg(total).arg(succeeded).arg(failed));

    if (failed == 0) {
        statusLabel_->setText(
            QStringLiteral("鉁?鎵归噺澶勭悊鍏ㄩ儴鎴愬姛 (%1 涓枃浠?").arg(succeeded));
    } else if (succeeded == 0) {
        statusLabel_->setText(
            QStringLiteral("鉁?鎵归噺澶勭悊鍏ㄩ儴澶辫触 (%1 涓枃浠?").arg(failed));
    } else {
        statusLabel_->setText(
            QStringLiteral("鎵归噺澶勭悊瀹屾垚: %1 鎴愬姛, %2 澶辫触").arg(succeeded).arg(failed));
    }

    progressBar_->setRange(0, 100);
    progressBar_->setValue(100);
    progressBar_->setFormat(QStringLiteral("%p%"));

    cancelButton_->setVisible(false);
    forceQuitButton_->setVisible(false);

    auto* closeBtn = new QPushButton(QStringLiteral("鍏抽棴"));
    closeBtn->setObjectName(QStringLiteral("primaryButton"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto* btnBox = findChild<QDialogButtonBox*>();
    if (btnBox) {
        btnBox->clear();
        btnBox->addButton(closeBtn, QDialogButtonBox::AcceptRole);
    }
}

void ProgressDialog::appendLog(const QString& message) {
    logEdit_->append(message.toHtmlEscaped());
}
