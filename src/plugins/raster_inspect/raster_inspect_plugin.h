#pragma once
#include <gis/framework/plugin.h>

namespace gis::plugins {

class RasterInspectPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "raster_inspect"; }
    std::string displayName() const override { return "栅格检查"; }
    std::string version() const override { return "1.0.0"; }
    std::string description() const override { return "栅格信息查看与直方图统计"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doHistogram(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doRasterInfo(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
