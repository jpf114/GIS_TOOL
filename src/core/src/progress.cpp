#include <gis/core/progress.h>
#include <iostream>
#include <iomanip>

namespace gis::core {

void CliProgress::onProgress(double percent) {
    int barWidth = 50;
    int pos = static_cast<int>(barWidth * percent);
    std::cerr << "\r[";
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cerr << "=";
        else if (i == pos) std::cerr << ">";
        else std::cerr << " ";
    }
    std::cerr << "] " << std::fixed << std::setprecision(1)
              << (percent * 100.0) << "%" << std::flush;
    if (percent >= 1.0) std::cerr << std::endl;
}

void CliProgress::onMessage(const std::string& msg) {
    std::cerr << msg << std::endl;
}

} // namespace gis::core
