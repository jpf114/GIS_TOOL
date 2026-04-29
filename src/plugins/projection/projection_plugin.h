#pragma once
#include <gis/framework/plugin.h>

namespace gis::plugins {

class ProjectionPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "projection"; }
    std::string displayName() const override { return "投影转换"; }
    std::string version() const override { return "1.0.0"; }
    std::string description() const override { return "坐标系投影转换、重投影、坐标变换、坐标系赋值"; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doReproject(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doInfo(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doTransform(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doAssignSRS(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
