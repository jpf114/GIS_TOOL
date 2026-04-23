#include <gis/core/types.h>

namespace gis::core {

bool isEpsgCode(const std::string& srs) {
    return srs.size() > 5 && srs.substr(0, 5) == "EPSG:";
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
