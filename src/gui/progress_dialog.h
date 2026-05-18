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

    void setBatchMode(int totalCount);
    void updateBatchProgress(int completedCount, int failedCount, const QString& currentFile);
    void setBatchFinished(int total, int succeeded, int failed);

    void appendLog(const QString& message);

private:
    QLabel* statusLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QProgressBar* batchProgressBar_ = nullptr;
    QLabel* batchProgressLabel_ = nullptr;
    QTextEdit* logEdit_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QPushButton* forceQuitButton_ = nullptr;

    int batchTotal_ = 0;
};
