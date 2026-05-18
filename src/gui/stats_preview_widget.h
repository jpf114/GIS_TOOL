#pragma once

#include <QWidget>
#include <QString>

class QTableWidget;
class QPlainTextEdit;
class QTabWidget;

class StatsPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit StatsPreviewWidget(QWidget* parent = nullptr);

    void loadStats(const QString& content, const QString& filePath);
    void clear();

private:
    void buildUi();
    bool tryParseCsv(const QString& content);
    bool tryParseJson(const QString& content);

    QTabWidget* tabWidget_ = nullptr;
    QTableWidget* tableWidget_ = nullptr;
    QPlainTextEdit* rawEdit_ = nullptr;
};
