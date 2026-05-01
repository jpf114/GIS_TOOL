#include <gis/core/spindex_presets.h>

namespace gis::core {

std::vector<std::string> spindexCustomIndexPresetValues() {
    return {
        "none",
        "ndvi_alias",
        "ndwi_alias",
        "mndwi_alias",
        "ndbi_alias",
        "gndvi_alias",
        "savi_alias",
        "evi_alias"
    };
}

std::string spindexCustomIndexPresetExpression(const std::string& presetKey) {
    if (presetKey == "ndvi_alias") return "(NIR-RED)/(NIR+RED)";
    if (presetKey == "ndwi_alias") return "(GREEN-NIR)/(GREEN+NIR)";
    if (presetKey == "mndwi_alias") return "(GREEN-SWIR1)/(GREEN+SWIR1)";
    if (presetKey == "ndbi_alias") return "(SWIR1-NIR)/(SWIR1+NIR)";
    if (presetKey == "gndvi_alias") return "(NIR-GREEN)/(NIR+GREEN)";
    if (presetKey == "savi_alias") return "((NIR-RED)/(NIR+RED+0.5))*(1+0.5)";
    if (presetKey == "evi_alias") return "2.5*(NIR-RED)/(NIR+6*RED-7.5*BLUE+1)";
    return {};
}

} // namespace gis::core
