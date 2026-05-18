#include "stats_preview_widget.h"

#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVBoxLayout>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

StatsPreviewWidget::StatsPreviewWidget(QWidget* parent)
    : QWidget(parent) {
    buildUi();
}

void StatsPreviewWidget::buildUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    tabWidget_ = new QTabWidget;
    tabWidget_->setObjectName(QStringLiteral("pagePanel"));

    tableWidget_ = new QTableWidget;
    tableWidget_->setAlternatingRowColors(true);
    tableWidget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget_->horizontalHeader()->setStretchLastSection(true);
    tableWidget_->verticalHeader()->setDefaultSectionSize(28);
    tabWidget_->addTab(tableWidget_, QStringLiteral("表格视图"));

    rawEdit_ = new QPlainTextEdit;
    rawEdit_->setReadOnly(true);
    rawEdit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    rawEdit_->setStyleSheet(
        QStringLiteral("QPlainTextEdit {"
                        "  font-family: 'Cascadia Code', 'Consolas', monospace;"
                        "  font-size: 12px; padding: 10px;"
                        "  background: #1E2A36; color: #D4DAE2;"
                        "  border: 1px solid #E2E5EA; border-radius: 6px;"
                        "}"));
    tabWidget_->addTab(rawEdit_, QStringLiteral("原始文本"));

    mainLayout->addWidget(tabWidget_);
}

void StatsPreviewWidget::loadStats(const QString& content, const QString& filePath) {
    clear();

    rawEdit_->setPlainText(content);

    if (!tryParseJson(content)) {
        if (!tryParseCsv(content)) {
            tableWidget_->setRowCount(1);
            tableWidget_->setColumnCount(2);
            tableWidget_->setHorizontalHeaderLabels(
                QStringList() << QStringLiteral("键") << QStringLiteral("值"));
            tableWidget_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("内容")));
            tableWidget_->setItem(0, 1, new QTableWidgetItem(content.left(500)));
        }
    }

    tabWidget_->setCurrentIndex(0);
}

void StatsPreviewWidget::clear() {
    tableWidget_->clear();
    tableWidget_->setRowCount(0);
    tableWidget_->setColumnCount(0);
    rawEdit_->clear();
}

bool StatsPreviewWidget::tryParseCsv(const QString& content) {
    QStringList lines = content.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() < 2) return false;

    QChar sep = QLatin1Char(',');
    int commaCount = lines[0].count(QLatin1Char(','));
    int tabCount = lines[0].count(QLatin1Char('\t'));
    int semiCount = lines[0].count(QLatin1Char(';'));
    if (tabCount > commaCount && tabCount > semiCount) {
        sep = QLatin1Char('\t');
    } else if (semiCount > commaCount) {
        sep = QLatin1Char(';');
    }

    QStringList headers = lines[0].split(sep);
    if (headers.size() < 2) return false;

    tableWidget_->setColumnCount(headers.size());
    tableWidget_->setHorizontalHeaderLabels(headers);

    for (int i = 1; i < lines.size(); ++i) {
        QStringList fields = lines[i].split(sep);
        int row = tableWidget_->rowCount();
        tableWidget_->insertRow(row);
        for (int col = 0; col < std::min(fields.size(), headers.size()); ++col) {
            tableWidget_->setItem(row, col, new QTableWidgetItem(fields[col].trimmed()));
        }
    }

    return true;
}

bool StatsPreviewWidget::tryParseJson(const QString& content) {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) return false;

    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        tableWidget_->setRowCount(obj.size());
        tableWidget_->setColumnCount(2);
        tableWidget_->setHorizontalHeaderLabels(
            QStringList() << QStringLiteral("键") << QStringLiteral("值"));

        int row = 0;
        for (auto it = obj.begin(); it != obj.end(); ++it, ++row) {
            tableWidget_->setItem(row, 0, new QTableWidgetItem(it.key()));
            if (it.value().isObject() || it.value().isArray()) {
                tableWidget_->setItem(row, 1,
                    new QTableWidgetItem(QString::fromUtf8(
                        QJsonDocument(it.value().toObject()).toJson(QJsonDocument::Compact))));
            } else {
                tableWidget_->setItem(row, 1,
                    new QTableWidgetItem(it.value().toVariant().toString()));
            }
        }
        return true;
    }

    if (doc.isArray()) {
        QJsonArray arr = doc.array();
        if (arr.isEmpty()) return true;

        if (arr.first().isObject()) {
            QJsonObject firstObj = arr.first().toObject();
            QStringList headers;
            for (auto it = firstObj.begin(); it != firstObj.end(); ++it) {
                headers << it.key();
            }
            tableWidget_->setColumnCount(headers.size());
            tableWidget_->setHorizontalHeaderLabels(headers);

            for (int i = 0; i < arr.size(); ++i) {
                QJsonObject obj = arr[i].toObject();
                int row = tableWidget_->rowCount();
                tableWidget_->insertRow(row);
                for (int col = 0; col < headers.size(); ++col) {
                    QJsonValue val = obj.value(headers[col]);
                    tableWidget_->setItem(row, col,
                        new QTableWidgetItem(val.toVariant().toString()));
                }
            }
            return true;
        }

        tableWidget_->setRowCount(arr.size());
        tableWidget_->setColumnCount(1);
        tableWidget_->setHorizontalHeaderLabels(
            QStringList() << QStringLiteral("值"));
        for (int i = 0; i < arr.size(); ++i) {
            tableWidget_->setItem(i, 0,
                new QTableWidgetItem(arr[i].toVariant().toString()));
        }
        return true;
    }

    return false;
}
