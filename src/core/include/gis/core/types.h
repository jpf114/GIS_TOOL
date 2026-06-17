#pragma once

#include <array>
#include <string>

namespace gis::core {

/** Axis-aligned bounding box in geographic or projected coordinates. */
struct Extent {
    double xmin = 0; ///< Minimum X coordinate (west or left)
    double ymin = 0; ///< Minimum Y coordinate (south or bottom)
    double xmax = 0; ///< Maximum X coordinate (east or right)
    double ymax = 0; ///< Maximum Y coordinate (north or top)

    /** Returns true if the extent has positive area (xmin < xmax and ymin < ymax). */
    bool isValid() const { return xmin < xmax && ymin < ymax; }
    /** Returns the width of the extent (xmax - xmin). */
    double width() const { return xmax - xmin; }
    /** Returns the height of the extent (ymax - ymin). */
    double height() const { return ymax - ymin; }
};

/** Coordinate Reference System information. */
struct CRSInfo {
    std::string wkt; ///< Well-Known Text representation of the CRS
    int epsg = 0;    ///< EPSG code (0 if unknown)

    /** Returns true if either WKT or EPSG code is valid. */
    bool isValid() const { return !wkt.empty() || epsg > 0; }
};

/**
 * Check if a string is an EPSG code (e.g., "EPSG:4326").
 * @param srs The string to check.
 * @return True if the string starts with "EPSG:" and has at least one digit after the colon.
 */
bool isEpsgCode(const std::string& srs);

/**
 * Parse the numeric part of an EPSG code string.
 * @param srs A string like "EPSG:4326".
 * @return The numeric EPSG code, or 0 if parsing fails.
 */
int parseEpsgCode(const std::string& srs);

} // namespace gis::core
