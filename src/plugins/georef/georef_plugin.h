#pragma once

#include <gis/framework/plugin.h>

namespace gis::plugins {

class GeorefPlugin : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "georef"; }
    std::string displayName() const override { return "几何校正与辐射处理"; }
    std::string version() const override { return "0.1.0"; }
    std::string description() const override {
        return "提供遥感辐射与几何预处理能力，当前支持 DOS 校正、辐射定标、控制点配准与基础地形校正。";
    }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override;

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress) override;

private:
    gis::framework::Result doDosCorrection(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doRadiometricCalibration(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doGcpRegister(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doCosineCorrection(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doMinnaertCorrection(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doCCorrection(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doQuacCorrection(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);

    gis::framework::Result doRpcOrthorectify(
        const std::map<std::string, gis::framework::ParamValue>& params,
        gis::core::ProgressReporter& progress);
};

} // namespace gis::plugins
