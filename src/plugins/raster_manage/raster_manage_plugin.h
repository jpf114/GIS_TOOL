#pragma once
#include <gis/framework/plugin.h>

namespace gis::plugins {

class RasterManagePlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "raster_manage"; }
    std::string displayName() const override { return "栅格维护"; }
    std::string version() const override { return "1.1.0"; }
    std::string description() const override { return "金字塔构建、NoData 维护、COG 生成、分区统计与欧氏距离。"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doBuildOverviews(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doBuildCog(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doSetNoData(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doZonalStats(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doProximity(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
