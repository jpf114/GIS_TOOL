#pragma once
#include <gis/core/progress.h>
#include <QObject>
#include <atomic>

class QtProgressReporter : public QObject, public gis::core::ProgressReporter {
    Q_OBJECT
public:
    explicit QtProgressReporter(QObject* parent = nullptr);

    // ProgressReporter interface
    void onProgress(double percent) override;
    void onMessage(const std::string& msg) override;
    bool isCancelled() const override;

    // Called from the GUI thread to request cancellation
    void cancel();
    void reset();

signals:
    void progressChanged(double percent);
    void messageLogged(const QString& msg);

private:
    std::atomic<bool> m_cancelled{false};
};