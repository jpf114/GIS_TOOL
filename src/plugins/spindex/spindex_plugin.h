#pragma once

#include <gis/framework/plugin.h>

namespace gis::plugins {

class SpindexPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "spindex"; }
    std::string displayName() const override { return "光谱指数与专题分析"; }
    std::string version() const override { return "1.2.0"; }
    std::string description() const override {
        return "遥感光谱指数计算与专题分析，当前支持 NDVI、EVI、SAVI、GNDVI、NDWI、MNDWI、NDBI 以及自定义指数表达式。";
    }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doExecuteAction(
        const std::string& action,
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
