#pragma once
#include <gis/framework/plugin.h>

namespace gis::plugins {

class CuttingPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "cutting"; }
    std::string displayName() const override { return "影像裁切与镶嵌"; }
    std::string version() const override { return "1.0.0"; }
    std::string description() const override { return "影像裁切、镶嵌拼接、分块切割、波段合并"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doClip(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doMosaic(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doSplit(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doMergeBands(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
