#include <gis/core/plugin_utils.h>

namespace gis::core {

ProcessingMetadata buildPluginMetadata(const std::string& input, GDALDataset* srcDs,
                                        const std::string& algorithm) {
    ProcessingMetadata meta;
    meta.sourceFile = input;
    meta.sourceCrs = getSRSWKT(srcDs);
    meta.processingAlgorithm = algorithm;
    return meta;
}

} // namespace gis::core
