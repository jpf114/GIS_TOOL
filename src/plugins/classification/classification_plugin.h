#pragma once

#include <gis/framework/plugin.h>

namespace gis::plugins {

class ClassificationPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "classification"; }
    std::string displayName() const override { return "鍒嗙被缁熻"; }
    std::string version() const override { return "0.1.0"; }
    std::string description() const override {
        return "鎻愪緵鍒嗙被缁熻涓庣洃鐫ｅ垎绫昏兘鍔涳紝褰撳墠鏀寔鍦扮墿鍒嗙被缁熻銆丼VM銆侀殢鏈烘．鏋楀拰鏈€澶т技鐒跺垎绫汇€?;
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

    gis::framework::Result doAccuracyAssessment(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
