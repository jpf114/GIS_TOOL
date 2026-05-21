#include <gis/core/logger.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace gis::core {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    minLevel_ = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return minLevel_;
}

void Logger::addSink(Sink sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::clearSinks() {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.clear();
}

void Logger::log(LogLevel level, const std::string& message, const std::string& source) {
    if (level < minLevel_) return;

    LogEntry entry;
    entry.level = level;
    entry.message = message;
    entry.timestamp = currentTimestamp();
    entry.source = source;

    std::vector<Sink> sinksCopy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back(entry);
        if (entries_.size() > kMaxEntries) {
            entries_.erase(entries_.begin(), entries_.begin() + (entries_.size() - kMaxEntries));
        }
        sinksCopy = sinks_;  // 在锁内复制
    }

    for (auto& sink : sinksCopy) {  // 使用复制后的列表
        sink(entry);
    }
}

void Logger::debug(const std::string& msg, const std::string& src) { log(LogLevel::Debug, msg, src); }
void Logger::info(const std::string& msg, const std::string& src) { log(LogLevel::Info, msg, src); }
void Logger::warn(const std::string& msg, const std::string& src) { log(LogLevel::Warn, msg, src); }
void Logger::error(const std::string& msg, const std::string& src) { log(LogLevel::Error, msg, src); }

std::vector<LogEntry> Logger::recentEntries(size_t maxCount) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.size() <= maxCount) return entries_;
    return std::vector<LogEntry>(entries_.end() - maxCount, entries_.end());
}

bool Logger::exportToFile(const std::string& filePath) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ofstream ofs(filePath, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) return false;

    const char* levelNames[] = {"DEBUG", "INFO", "WARN", "ERROR"};

    for (const auto& entry : entries_) {
        ofs << "[" << entry.timestamp << "] "
            << "[" << levelNames[static_cast<int>(entry.level)] << "] ";
        if (!entry.source.empty()) {
            ofs << "[" << entry.source << "] ";
        }
        ofs << entry.message << "\n";
    }

    return true;
}

std::string Logger::currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &timeT);
#else
    localtime_r(&timeT, &tmBuf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

} // namespace gis::core
