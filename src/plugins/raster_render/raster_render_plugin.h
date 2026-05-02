#pragma once

#include <gis/framework/plugin.h>

namespace gis::plugins {

class RasterRenderPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "raster_render"; }
    std::string displayName() const override { return "栅格渲染"; }
    std::string version() const override { return "1.1.0"; }
    std::string description() const override { return "伪彩色渲染与直方图匹配"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doColormap(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doHistogramMatch(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
