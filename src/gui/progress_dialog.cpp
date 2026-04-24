#include "progress_dialog.h"
#include "qt_progress_reporter.h"
#include <QVBoxLayout>
#include <QProgressBar>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>

ProgressDialog::ProgressDialog(QtProgressReporter* reporter, QWidget* parent)
    : QDialog(parent), reporter_(reporter) {
    setWindowTitle(QStringLiteral("执行中..."));
    setMinimumWidth(450);
    setWindowFlags(windowFlags() & ~Qt::WindowCloseButtonHint);

    auto* layout = new QVBoxLayout(this);

    auto* statusLabel = new QLabel(QStringLiteral("正在执行，请稍候..."));
    layout->addWidget(statusLabel);

    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    layout->addWidget(progressBar_);

    logEdit_ = new QTextEdit;
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumHeight(150);
    layout->addWidget(logEdit_);

    auto* btnBox = new QDialogButtonBox;
    cancelButton_ = new QPushButton(QStringLiteral("取消"));
    btnBox->addButton(cancelButton_, QDialogButtonBox::ActionRole);
    layout->addWidget(btnBox);

    connect(cancelButton_, &QPushButton::clicked, this, &ProgressDialog::onCancel);
    connect(reporter_, &QtProgressReporter::progressChanged, this, &ProgressDialog::onProgressChanged);
    connect(reporter_, &QtProgressReporter::messageLogged, this, &ProgressDialog::onMessageLogged);
}

void ProgressDialog::setFinished(const QString& message, bool success) {
    cancelButton_->setEnabled(false);
    progressBar_->setValue(100);

    if (success) {
        setWindowTitle(QStringLiteral("执行完成"));
        logEdit_->append(QStringLiteral("<b style='color:green;'>%1</b>").arg(message));
    } else {
        setWindowTitle(QStringLiteral("执行失败"));
        logEdit_->append(QStringLiteral("<b style='color:red;'>%1</b>").arg(message));
    }

    auto* closeBtn = new QPushButton(QStringLiteral("关闭"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto* btnBox = findChild<QDialogButtonBox*>();
    if (btnBox) {
        btnBox->clear();
        btnBox->addButton(closeBtn, QDialogButtonBox::AcceptRole);
    }
}

void ProgressDialog::onCancel() {
    if (reporter_) {
        reporter_->cancel();
    }
    cancelButton_->setEnabled(false);
    cancelButton_->setText(QStringLiteral("正在取消..."));
    logEdit_->append(QStringLiteral("<i>已请求取消...</i>"));
}

void ProgressDialog::onProgressChanged(double percent) {
    progressBar_->setValue(static_cast<int>(percent * 100));
}

void ProgressDialog::onMessageLogged(const QString& msg) {
    logEdit_->append(msg);
}
