#pragma once

#include <gis/framework/plugin.h>

namespace gis::plugins {

class TerrainPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "terrain"; }
    std::string displayName() const override { return "地形分析"; }
    std::string version() const override { return "1.0.0"; }
    std::string description() const override { return "基于 DEM 生成坡度、坡向、山体阴影与基础地形因子结果。"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doSlope(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doAspect(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doHillshade(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doTpi(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doRoughness(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doFillSinks(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doFlowDirection(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doFlowAccumulation(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doStreamExtract(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doWatershed(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
