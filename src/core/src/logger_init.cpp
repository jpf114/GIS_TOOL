#include <gis/core/logger_init.h>
#include <gis/core/logger.h>

#include <atomic>
#include <iostream>

namespace gis::core {

namespace {

std::atomic<bool> g_loggingInitialized{false};

const char* levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERROR";
    default: return "LOG";
    }
}

void defaultStderrSink(const LogEntry& entry) {
    std::cerr << "[" << entry.timestamp << "] "
              << "[" << levelName(entry.level) << "] ";
    if (!entry.source.empty()) {
        std::cerr << "[" << entry.source << "] ";
    }
    std::cerr << entry.message << std::endl;
}

} // namespace

void initDefaultLogging() {
    bool expected = false;
    if (!g_loggingInitialized.compare_exchange_strong(expected, true)) {
        return;
    }

    Logger::instance().setLevel(LogLevel::Info);
    Logger::instance().addSink(defaultStderrSink);
}

} // namespace gis::core
