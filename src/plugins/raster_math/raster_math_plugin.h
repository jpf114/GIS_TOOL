#pragma once

#include <gis/framework/plugin.h>

namespace gis::plugins {

class RasterMathPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "raster_math"; }
    std::string displayName() const override { return "栅格运算"; }
    std::string version() const override { return "1.0.0"; }
    std::string description() const override { return "按表达式对多波段影像执行栅格运算。"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;
};

} // namespace gis::plugins
