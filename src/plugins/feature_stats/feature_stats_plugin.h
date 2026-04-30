#pragma once

#include <gis/framework/plugin.h>

namespace gis::plugins {

class FeatureStatsPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "feature_stats"; }
    std::string displayName() const override { return "面域分类统计"; }
    std::string version() const override { return "0.1.0"; }
    std::string description() const override { return "对面要素范围内的多源分类栅格执行优先级统计，输出像元数、面积和占比。"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doRun(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins

