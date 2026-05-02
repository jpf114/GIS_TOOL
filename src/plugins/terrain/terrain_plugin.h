#pragma once

#include <gis/framework/plugin.h>

namespace gis::plugins {

class TerrainPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "terrain"; }
    std::string displayName() const override { return "地形分析"; }
    std::string version() const override { return "1.0.0"; }
    std::string description() const override {
        return "提供基于 DEM 的地形分析能力，当前支持坡度坡向、阴影、水文分析、视域分析和体积计算。";
    }

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
    gis::framework::Result doCurvature(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doProfileCurvature(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doPlanCurvature(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doTri(
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
    gis::framework::Result doProfileExtract(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doViewshed(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doViewshedMulti(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doCutFill(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
    gis::framework::Result doReservoirVolume(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
