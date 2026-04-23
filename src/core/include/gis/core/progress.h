#pragma once
#include <string>

namespace gis::core {

class ProgressReporter {
public:
    virtual ~ProgressReporter() = default;
    virtual void onProgress(double percent) = 0;
    virtual void onMessage(const std::string& msg) = 0;
    virtual bool isCancelled() const = 0;
};

class CliProgress : public ProgressReporter {
public:
    void onProgress(double percent) override;
    void onMessage(const std::string& msg) override;
    bool isCancelled() const override { return false; }
};

} // namespace gis::core
