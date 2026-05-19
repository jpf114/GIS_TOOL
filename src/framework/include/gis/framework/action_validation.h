#pragma once

#include <map>
#include <optional>
#include <string>

#include <gis/framework/param_spec.h>

namespace gis::framework {

struct ActionValidationIssue {
    std::string key;
    std::string message;
};

std::optional<ActionValidationIssue> validateActionSpecificParams(
    const std::string& pluginName,
    const std::string& action,
    const std::map<std::string, ParamValue>& params);

} // namespace gis::framework
