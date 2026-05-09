#pragma once

#include <gis/framework/plugin.h>

namespace gis::plugins {

class RasterMathPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "raster_math"; }
    std::string displayName() const override { return "栅格运算"; }
    std::string version() const override { return "1.1.0"; }
    std::string description() const override { return "栅格重分类、叠加分析与波段运算。"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doBandMath(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doReclassify(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doRasterOverlay(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
