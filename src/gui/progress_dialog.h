#pragma once
#include <QDialog>
#include <memory>

class QProgressBar;
class QTextEdit;
class QPushButton;
class QLabel;
class QtProgressReporter;

class ProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProgressDialog(QtProgressReporter* reporter, QWidget* parent = nullptr);

    void setFinished(const QString& message, bool success, bool cancelled = false);

private slots:
    void onCancel();
    void onProgressChanged(double percent);
    void onMessageLogged(const QString& msg);

private:
    QLabel* statusLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QTextEdit* logEdit_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QtProgressReporter* reporter_ = nullptr;
};
