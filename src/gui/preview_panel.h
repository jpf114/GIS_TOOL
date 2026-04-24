#pragma once

#include <QWidget>

class QLabel;
class QStackedWidget;

class PreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPanel(QWidget* parent = nullptr);

    void clearPreview();
    void showPath(const std::string& path);

private:
    void setPlaceholder(const QString& title, const QString& message);
    void setSummary(const QString& title, const QString& summary, const QString& pathText);
    void showRasterPreview(const std::string& path);
    void showVectorPreview(const std::string& path);
    void showUnsupportedPreview(const std::string& path);

    QLabel* titleLabel_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QLabel* placeholderLabel_ = nullptr;
    QLabel* imageLabel_ = nullptr;
    QStackedWidget* stackedWidget_ = nullptr;
};
