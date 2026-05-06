#pragma once
#include <gis/core/progress.h>
#include <QObject>
#include <atomic>
#include <chrono>

class QtProgressReporter : public QObject, public gis::core::ProgressReporter {
    Q_OBJECT
public:
    explicit QtProgressReporter(QObject* parent = nullptr);

    void onProgress(double percent) override;
    void onMessage(const std::string& msg) override;
    bool isCancelled() const override;

    void cancel();
    void reset();

signals:
    void progressChanged(double percent);
    void messageLogged(const QString& msg);

private:
    bool shouldEmitProgress(double percent) const;

    std::atomic<bool> m_cancelled{false};
    double m_lastProgressValue{-1.0};
    std::chrono::steady_clock::time_point m_lastProgressTime{};
    bool m_firstProgress{true};

    static constexpr double kProgressDelta = 0.01;
    static constexpr int kProgressIntervalMs = 50;
};
