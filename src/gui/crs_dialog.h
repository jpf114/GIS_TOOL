#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

class CrsDialog : public QDialog {
    Q_OBJECT
public:
    explicit CrsDialog(QWidget* parent = nullptr);

    QString selectedCrs() const { return selectedCrs_; }

private slots:
    void onSearch();
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    QLineEdit* searchEdit_ = nullptr;
    QListWidget* resultList_ = nullptr;
    QString selectedCrs_;
};
