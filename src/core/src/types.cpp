#include <gis/core/types.h>

namespace gis::core {

bool isEpsgCode(const std::string& srs) {
    return srs.size() >= 6 && srs.compare(0, 5, "EPSG:") == 0;
}

int parseEpsgCode(const std::string& srs) {
    if (!isEpsgCode(srs)) return 0;
    try {
        return std::stoi(srs.substr(5));
    } catch (...) {
        return 0;
    }
}

} // namespace gis::core
