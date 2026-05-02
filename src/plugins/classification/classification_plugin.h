#pragma once

#include <gis/framework/plugin.h>

namespace gis::plugins {

class ClassificationPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "classification"; }
    std::string displayName() const override { return "分类统计"; }
    std::string version() const override { return "0.1.0"; }
    std::string description() const override {
        return "提供地物分类统计与监督分类能力，当前支持分类统计、SVM、随机森林和最大似然分类。";
    }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doFeatureStats(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doSvmClassify(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doRandomForestClassify(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doMaxLikelihoodClassify(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
