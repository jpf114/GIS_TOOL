#pragma once

#include <string>

namespace gis::core {

/** Escape text for HTML text nodes and attribute values. */
std::string escapeForHtml(const std::string& text);

/** Escape text for inclusion inside a JavaScript single-quoted string literal. */
std::string escapeForJsSingleQuoted(const std::string& text);

} // namespace gis::core
