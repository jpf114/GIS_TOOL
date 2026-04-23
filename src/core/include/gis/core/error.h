#pragma once
#include <stdexcept>
#include <string>

namespace gis::core {

class GisError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace gis::core
