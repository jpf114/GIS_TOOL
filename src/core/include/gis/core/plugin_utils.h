#pragma once

#include <gis/core/gdal_wrapper.h>
#include <algorithm>
#include <cctype>
#include <map>
#include <string>

class GDALDataset;

namespace gis::core {

/** Trim whitespace from both ends of a string. */
inline std::string trimString(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

/** Convert string to lowercase. */
inline std::string toLowerString(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

/** Build standard processing metadata for a plugin result. */
ProcessingMetadata buildPluginMetadata(const std::string& input, GDALDataset* srcDs,
                                        const std::string& algorithm);

/**
 * Evaluate a simple arithmetic expression with band variable substitution.
 *
 * Security measures:
 *   - Expression length limited to kMaxExpressionLength (1024) characters
 *   - Only digits, operators (+-*/), parentheses, dots, and spaces are allowed after substitution
 *   - Operator nesting depth limited to kMaxExpressionDepth (64)
 *   - Division by near-zero returns 0.0 instead of infinity
 *
 * @param expr The expression string (e.g., "B1+B2", "(B4-B1)/(B4+B1)")
 * @param bandValues Map of variable names (e.g., "B1") to their numeric values
 * @return The evaluated result, or 0.0 on error
 */
double evalExpression(const std::string& expr,
                      const std::map<std::string, double>& bandValues);

/// Maximum allowed expression length in characters.
static constexpr size_t kMaxExpressionLength = 1024;

/// Maximum allowed operator nesting depth.
static constexpr size_t kMaxExpressionDepth = 64;

} // namespace gis::core
