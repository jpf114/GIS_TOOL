#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <ctime>

namespace gis::core {

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3
};

struct LogEntry {
    LogLevel level = LogLevel::Info;
    std::string message;
    std::string timestamp;
    std::string source;
};

class Logger {
public:
    using Sink = std::function<void(const LogEntry&)>;

    static Logger& instance();

    void setLevel(LogLevel level);
    LogLevel level() const;

    void addSink(Sink sink);
    void clearSinks();

    void log(LogLevel level, const std::string& message, const std::string& source = "");

    void debug(const std::string& msg, const std::string& src = "");
    void info(const std::string& msg, const std::string& src = "");
    void warn(const std::string& msg, const std::string& src = "");
    void error(const std::string& msg, const std::string& src = "");

    std::vector<LogEntry> recentEntries(size_t maxCount = 100) const;
    bool exportToFile(const std::string& filePath) const;

private:
    Logger() = default;

    LogLevel minLevel_ = LogLevel::Info;
    std::vector<Sink> sinks_;
    mutable std::mutex mutex_;
    std::vector<LogEntry> entries_;

    static constexpr size_t kMaxEntries = 10000;

    static std::string currentTimestamp();
};

} // namespace gis::core
