#pragma once

#include <QImage>
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

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setPlaceholder(const QString& title, const QString& message);
    void setSummary(const QString& title, const QString& summary, const QString& pathText);
    void showRasterPreview(const std::string& path);
    void showVectorPreview(const std::string& path);
    void showUnsupportedPreview(const std::string& path);
    void updateImageDisplay();
    void updateScaleLabel();
    void setScale(double scale, bool keepFitMode);
    void fitCurrentImage();
    void setZoomControlsEnabled(bool enabled);

    QLabel* titleLabel_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QLabel* placeholderLabel_ = nullptr;
    QLabel* imageLabel_ = nullptr;
    QLabel* scaleLabel_ = nullptr;
    QPushButton* zoomInButton_ = nullptr;
    QPushButton* zoomOutButton_ = nullptr;
    QPushButton* fitButton_ = nullptr;
    QScrollArea* imageScrollArea_ = nullptr;
    QStackedWidget* stackedWidget_ = nullptr;

    QImage currentImage_;
    double currentScale_ = 1.0;
    bool fitMode_ = true;
};
