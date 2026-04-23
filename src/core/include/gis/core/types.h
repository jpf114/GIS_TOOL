#pragma once

#include <array>
#include <string>

namespace gis::core {

struct Extent {
    double xmin = 0;
    double ymin = 0;
    double xmax = 0;
    double ymax = 0;

    bool isValid() const { return xmin < xmax && ymin < ymax; }
    double width() const { return xmax - xmin; }
    double height() const { return ymax - ymin; }
};

struct CRSInfo {
    std::string wkt;
    int epsg = 0;
    bool isValid() const { return !wkt.empty() || epsg > 0; }
};

bool isEpsgCode(const std::string& srs);
int parseEpsgCode(const std::string& srs);

} // namespace gis::core
