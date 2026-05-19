#pragma once
#include <gis/framework/plugin.h>

namespace gis::plugins {

class MatchingPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "matching"; }
    std::string displayName() const override { return "特征匹配与配准"; }
    std::string version() const override { return "1.2.0"; }
    std::string description() const override { return "提供特征提取、特征匹配、影像配准、变化检测、ECC 配准、角点检测与影像拼接。"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doDetect(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doMatch(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doRegister(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doChange(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doEccRegister(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doCornerDetect(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doStitch(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
