#pragma once
#include <string>
#include <stdexcept>

namespace gis::core {

class CancelledException : public std::runtime_error {
public:
    explicit CancelledException(const std::string& msg = "鎿嶄綔宸插彇娑?)
        : std::runtime_error(msg) {}
};

class ProgressReporter {
public:
    virtual ~ProgressReporter() = default;
    virtual void onProgress(double percent) = 0;
    virtual void onMessage(const std::string& msg) = 0;
    virtual bool isCancelled() const = 0;

    void throwIfCancelled() const {
        if (isCancelled()) {
            throw CancelledException();
        }
    }
};

class CliProgress : public ProgressReporter {
public:
    void onProgress(double percent) override;
    void onMessage(const std::string& msg) override;
    bool isCancelled() const override { return false; }
};

class NullProgressReporter : public ProgressReporter {
public:
    void onProgress(double) override {}
    void onMessage(const std::string&) override {}
    bool isCancelled() const override { return false; }
};

} // namespace gis::core
