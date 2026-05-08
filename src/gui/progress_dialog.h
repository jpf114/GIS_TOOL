#pragma once
#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class QTextEdit;

class ProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProgressDialog(QWidget* parent = nullptr);

    void setFinished(const QString& message, bool success, bool cancelled);
    void updateProgress(double percent);
    void appendLog(const QString& message);

private:
    QLabel* statusLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QTextEdit* logEdit_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QPushButton* forceQuitButton_ = nullptr;
};
