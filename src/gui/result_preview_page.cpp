#include "result_preview_page.h"
#include "raster_preview_widget.h"
#include "vector_preview_widget.h"
#include "stats_preview_widget.h"
#include "style_constants.h"
#include "gui_data_support.h"

#include <QDesktopServices>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <map>

ResultPreviewPage::ResultPreviewPage(QWidget* parent)
    : QWidget(parent) {
    buildUi();
}

void ResultPreviewPage::buildUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    auto* headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(12);

    auto* backBtn = new QPushButton(QStringLiteral("← 返回参数"));
    backBtn->setObjectName(QStringLiteral("secondaryButton"));
    backBtn->setFixedWidth(120);
    connect(backBtn, &QPushButton::clicked, this, &ResultPreviewPage::backToParamsRequested);
    headerLayout->addWidget(backBtn);

    auto* titleLabel = new QLabel(QStringLiteral("结果预览"));
    titleLabel->setObjectName(QStringLiteral("heroTitle"));
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    openExternalButton_ = new QPushButton(QStringLiteral("在外部打开"));
    openExternalButton_->setObjectName(QStringLiteral("secondaryButton"));
    openExternalButton_->setFixedWidth(120);
    openExternalButton_->setVisible(false);
    connect(openExternalButton_, &QPushButton::clicked, this, [this]() {
        if (!currentPath_.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(currentPath_));
        }
    });
    headerLayout->addWidget(openExternalButton_);

    mainLayout->addLayout(headerLayout);

    pathLabel_ = new QLabel;
    pathLabel_->setObjectName(QStringLiteral("heroDesc"));
    pathLabel_->setWordWrap(true);
    pathLabel_->setText(QStringLiteral("无结果"));
    mainLayout->addWidget(pathLabel_);

    messageLabel_ = new QLabel;
    messageLabel_->setObjectName(QStringLiteral("execSummary"));
    messageLabel_->setWordWrap(true);
    messageLabel_->setVisible(false);
    mainLayout->addWidget(messageLabel_);

    stackedWidget_ = new QStackedWidget;
    rasterPreview_ = new RasterPreviewWidget;
    vectorPreview_ = new VectorPreviewWidget;
    statsPreview_ = new StatsPreviewWidget;

    stackedWidget_->addWidget(rasterPreview_);
    stackedWidget_->addWidget(vectorPreview_);
    stackedWidget_->addWidget(statsPreview_);

    auto* placeholderLabel = new QLabel(QStringLiteral("无可预览的结果"));
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 14px;").arg(gis::style::Color::kTextMuted));
    stackedWidget_->addWidget(placeholderLabel);

    mainLayout->addWidget(stackedWidget_, 1);
}

void ResultPreviewPage::showResult(const QString& outputPath,
                                    const QString& message,
                                    const std::map<std::string, std::string>& metadata) {
    currentPath_ = outputPath;

    if (!outputPath.isEmpty()) {
        pathLabel_->setText(QStringLiteral("输出路径: %1").arg(outputPath));
        openExternalButton_->setVisible(true);
    } else {
        pathLabel_->setText(QStringLiteral("无文件输出"));
        openExternalButton_->setVisible(false);
    }

    if (!message.isEmpty()) {
        messageLabel_->setText(message);
        messageLabel_->setVisible(true);
    } else {
        messageLabel_->setVisible(false);
    }

    detectAndShow(outputPath);

    for (const auto& [key, value] : metadata) {
        if (key == "json_path" || key == "csv_path" || key == "stats_path") {
            QString statsPath = QString::fromStdString(value);
            if (!statsPath.isEmpty()) {
                QFile f(statsPath);
                if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    QString content = QString::fromUtf8(f.readAll());
                    stackedWidget_->setCurrentWidget(statsPreview_);
                    statsPreview_->loadStats(content, statsPath);
                    return;
                }
            }
        }
    }
}

void ResultPreviewPage::detectAndShow(const QString& path) {
    if (path.isEmpty()) {
        stackedWidget_->setCurrentIndex(3);
        return;
    }

    auto kind = gis::gui::detectDataKind(path.toStdString());

    if (kind == gis::gui::DataKind::Raster) {
        rasterPreview_->loadRaster(path);
        stackedWidget_->setCurrentWidget(rasterPreview_);
    } else if (kind == gis::gui::DataKind::Vector) {
        vectorPreview_->loadVector(path);
        stackedWidget_->setCurrentWidget(vectorPreview_);
    } else {
        QString lowerPath = path.toLower();
        if (lowerPath.endsWith(QStringLiteral(".json")) ||
            lowerPath.endsWith(QStringLiteral(".csv")) ||
            lowerPath.endsWith(QStringLiteral(".txt"))) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString content = QString::fromUtf8(f.readAll());
                statsPreview_->loadStats(content, path);
                stackedWidget_->setCurrentWidget(statsPreview_);
                return;
            }
        }
        stackedWidget_->setCurrentIndex(3);
    }
}

void ResultPreviewPage::clear() {
    currentPath_.clear();
    rasterPreview_->clear();
    vectorPreview_->clear();
    statsPreview_->clear();
    pathLabel_->setText(QStringLiteral("无结果"));
    messageLabel_->setVisible(false);
    openExternalButton_->setVisible(false);
    stackedWidget_->setCurrentIndex(3);
}
