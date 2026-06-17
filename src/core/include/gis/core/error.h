#pragma once
#include <stdexcept>
#include <string>

namespace gis::core {

/** General-purpose error for GIS operations. Inherits std::runtime_error. */
class GisError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace gis::core
