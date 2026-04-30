#pragma once

#include <gis/framework/plugin.h>

namespace gis::plugins {

class ClassificationPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "classification"; }
    std::string displayName() const override { return "分类统计"; }
    std::string version() const override { return "0.1.0"; }
    std::string description() const override { return "提供面向分类结果的统计分析能力，当前支持地物分类统计。"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doFeatureStats(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
