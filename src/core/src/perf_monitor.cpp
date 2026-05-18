#include <gis/core/perf_monitor.h>

#include <algorithm>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace gis::core {

PerformanceMonitor& PerformanceMonitor::instance() {
    static PerformanceMonitor monitor;
    return monitor;
}

void PerformanceMonitor::record(const PerformanceRecord& rec) {
    records_.push_back(rec);
}

std::vector<PerformanceRecord> PerformanceMonitor::records(const std::string& algorithm) const {
    if (algorithm.empty()) return records_;
    std::vector<PerformanceRecord> result;
    for (const auto& r : records_) {
        if (r.algorithm == algorithm) result.push_back(r);
    }
    return result;
}

bool PerformanceMonitor::exportToFile(const std::string& filePath) const {
    std::ofstream ofs(filePath, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) return false;

    ofs << "algorithm,duration_ms,memory_peak_bytes,width,height,bands,success,timestamp\n";
    for (const auto& r : records_) {
        ofs << r.algorithm << ","
            << r.durationMs << ","
            << r.memoryPeakBytes << ","
            << r.rasterWidth << ","
            << r.rasterHeight << ","
            << r.bandCount << ","
            << (r.success ? "1" : "0") << ","
            << r.timestamp << "\n";
    }
    return true;
}

void PerformanceMonitor::clear() {
    records_.clear();
}

} // namespace gis::core
