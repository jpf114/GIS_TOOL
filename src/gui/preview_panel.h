#pragma once

#include "gui_data_support.h"

#include <QImage>
#include <QPoint>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;

class PreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPanel(QWidget* parent = nullptr);

    void clearPreview();
    void showPath(const std::string& path);
    void refitPreview();
    void setCompareTargets(const QString& inputPath, const QString& outputPath);
    void setCurrentOrigin(gis::gui::DataOrigin origin);
    bool showComparePreviewIfAvailable();

signals:
    void requestOpenPath(const QString& path);
    void requestUseAsInput(const QString& path);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setPlaceholder(const QString& title, const QString& message);
    void setSummary(const QString& title, const QString& summary, const QString& pathText);
    void showComparePreview();
    void showRasterPreview(const std::string& path);
    void showVectorPreview(const std::string& path);
    void showUnsupportedPreview(const std::string& path);
    void updateImageDisplay();
    void updateScaleLabel();
    void setScale(double scale, bool keepFitMode);
    void fitCurrentImage();
    void setZoomControlsEnabled(bool enabled);
    void openCurrentPathDirectory();
    void copyCurrentPath();
    void updateCompareButtons();
    void updateOriginLabel();
    bool hasImagePreview() const;

    QLabel* titleLabel_ = nullptr;
    QLabel* originLabel_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QLabel* placeholderLabel_ = nullptr;
    QLabel* imageLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* scaleLabel_ = nullptr;
    QPushButton* showInputButton_ = nullptr;
    QPushButton* showOutputButton_ = nullptr;
    QPushButton* compareButton_ = nullptr;
    QPushButton* useAsInputButton_ = nullptr;
    QPushButton* openDirButton_ = nullptr;
    QPushButton* copyPathButton_ = nullptr;
    QPushButton* zoomInButton_ = nullptr;
    QPushButton* zoomOutButton_ = nullptr;
    QPushButton* fitButton_ = nullptr;
    QScrollArea* imageScrollArea_ = nullptr;
    QStackedWidget* stackedWidget_ = nullptr;

    QImage currentImage_;
    gis::gui::DataKind currentDataKind_ = gis::gui::DataKind::Unknown;
    double currentScale_ = 1.0;
    bool fitMode_ = true;
    bool isPanning_ = false;
    QPoint lastPanPoint_;
    QString currentPath_;
    QString compareInputPath_;
    QString compareOutputPath_;
    QString currentOriginText_;
};
