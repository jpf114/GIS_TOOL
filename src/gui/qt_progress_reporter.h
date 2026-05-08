#pragma once
#include <gis/core/progress.h>
#include <QObject>
#include <atomic>
#include <chrono>

class QtProgressReporter : public QObject, public gis::core::ProgressReporter {
    Q_OBJECT
public:
    explicit QtProgressReporter(const QString& taskId, QObject* parent = nullptr);

    void onProgress(double percent) override;
    void onMessage(const std::string& msg) override;
    bool isCancelled() const override;

    void cancel();
    void reset();

    QString taskId() const { return taskId_; }

signals:
    void progressChanged(const QString& taskId, double percent);
    void messageLogged(const QString& taskId, const QString& msg);

private:
    bool shouldEmitProgress(double percent) const;

    std::atomic<bool> m_cancelled{false};
    double m_lastProgressValue{-1.0};
    std::chrono::steady_clock::time_point m_lastProgressTime{};
    bool m_firstProgress{true};
    QString taskId_;

    static constexpr double kProgressDelta = 0.01;
    static constexpr int kProgressIntervalMs = 50;
};
