#pragma once

#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <cstdint>

namespace gis::core {

struct PerformanceRecord {
    std::string algorithm;
    int64_t durationMs = 0;
    int64_t memoryPeakBytes = 0;
    int rasterWidth = 0;
    int rasterHeight = 0;
    int bandCount = 0;
    std::string timestamp;
    bool success = false;
    std::map<std::string, std::string> extra;
};

class PerformanceMonitor {
public:
    static PerformanceMonitor& instance();

    void record(const PerformanceRecord& rec);
    std::vector<PerformanceRecord> records(const std::string& algorithm = "") const;
    bool exportToFile(const std::string& filePath) const;
    void clear();

    struct ScopeTimer {
        PerformanceRecord rec;
        std::chrono::steady_clock::time_point start;

        explicit ScopeTimer(const std::string& algorithm)
            : rec{}, start(std::chrono::steady_clock::now()) {
            rec.algorithm = algorithm;
        }

        ~ScopeTimer() {
            auto end = std::chrono::steady_clock::now();
            rec.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            PerformanceMonitor::instance().record(rec);
        }
    };

private:
    PerformanceMonitor() = default;
    std::vector<PerformanceRecord> records_;
};

} // namespace gis::core
