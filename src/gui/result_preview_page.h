#pragma once

#include <QWidget>
#include <QString>

class RasterPreviewWidget;
class VectorPreviewWidget;
class StatsPreviewWidget;
class QLabel;
class QPushButton;
class QStackedWidget;

class ResultPreviewPage : public QWidget {
    Q_OBJECT
public:
    explicit ResultPreviewPage(QWidget* parent = nullptr);

    void showResult(const QString& outputPath,
                    const QString& message = QString(),
                    const std::map<std::string, std::string>& metadata = {});
    void clear();

signals:
    void backToParamsRequested();

private:
    void buildUi();
    void detectAndShow(const QString& path);

    RasterPreviewWidget* rasterPreview_ = nullptr;
    VectorPreviewWidget* vectorPreview_ = nullptr;
    StatsPreviewWidget* statsPreview_ = nullptr;
    QStackedWidget* stackedWidget_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QPushButton* openExternalButton_ = nullptr;
    QLabel* messageLabel_ = nullptr;

    QString currentPath_;
};
