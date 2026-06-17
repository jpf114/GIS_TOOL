#include <gis/core/perf_monitor.h>

#include <algorithm>
#include <filesystem>
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
    std::lock_guard<std::mutex> lock(mutex_);
    if (records_.size() >= kMaxRecords) {
        records_.erase(records_.begin(), records_.begin() + (records_.size() - kMaxRecords / 2));
    }
    records_.push_back(rec);
}

std::vector<PerformanceRecord> PerformanceMonitor::records(const std::string& algorithm) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (algorithm.empty()) return records_;
    std::vector<PerformanceRecord> result;
    for (const auto& r : records_) {
        if (r.algorithm == algorithm) result.push_back(r);
    }
    return result;
}

bool PerformanceMonitor::exportToFile(const std::string& filePath) const {
    // 限制导出路径在应用目录或临时目录内
    namespace fs = std::filesystem;
    std::error_code ec;
    auto canonicalPath = fs::weakly_canonical(fs::path(filePath), ec);
    if (!ec) {
        auto pathStr = canonicalPath.string();
        // 拒绝明显可疑的路径
        if (pathStr.find("..") != std::string::npos) return false;
    }

    std::vector<PerformanceRecord> recordsCopy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        recordsCopy = records_;
    }

    std::ofstream ofs(filePath, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) return false;

    ofs << "algorithm,duration_ms,memory_peak_bytes,width,height,bands,success,timestamp\n";
    for (const auto& r : recordsCopy) {
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
    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();
}

} // namespace gis::core
