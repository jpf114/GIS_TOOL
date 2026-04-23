#pragma once
#include <gis/framework/plugin.h>

namespace gis::plugins {

class ProcessingPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "processing"; }
    std::string displayName() const override { return "影像处理与分析"; }
    std::string version() const override { return "1.0.0"; }
    std::string description() const override { return "阈值分割、空间滤波、影像增强、波段运算、统计信息"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doThreshold(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doFilter(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doEnhance(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doBandMath(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doStats(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
