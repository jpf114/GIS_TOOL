#include <gtest/gtest.h>
#include <QCoreApplication>
#include <gis/core/runtime_env.h>
#include <gis/core/gdal_wrapper.h>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    gis::core::initRuntimeEnvironment();
    gis::core::initGDAL();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
