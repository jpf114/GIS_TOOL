#pragma once
#include <gis/framework/plugin.h>

namespace gis::plugins {

class UtilityPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "utility"; }
    std::string displayName() const override { return "栅格工具"; }
    std::string version() const override { return "1.0.0"; }
    std::string description() const override { return "金字塔构建、NoData 设置、直方图、栅格信息、色彩映射、NDVI"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doBuildOverviews(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doSetNoData(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doHistogram(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doRasterInfo(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doColormap(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doComputeNdvi(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
