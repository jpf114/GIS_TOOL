#include "qt_progress_reporter.h"

QtProgressReporter::QtProgressReporter(QObject* parent)
    : QObject(parent) {}

void QtProgressReporter::onProgress(double percent) {
    emit progressChanged(percent);
}

void QtProgressReporter::onMessage(const std::string& msg) {
    emit messageLogged(QString::fromUtf8(msg.c_str()));
}

bool QtProgressReporter::isCancelled() const {
    return m_cancelled.load(std::memory_order_relaxed);
}

void QtProgressReporter::cancel() {
    m_cancelled.store(true, std::memory_order_relaxed);
}

void QtProgressReporter::reset() {
    m_cancelled.store(false, std::memory_order_relaxed);
}