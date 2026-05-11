#include <gtest/gtest.h>
#include <QApplication>
#include <QByteArray>
#include <gis/core/runtime_env.h>
#include <gis/core/gdal_wrapper.h>

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    }
    QApplication app(argc, argv);
    gis::core::initRuntimeEnvironment();
    gis::core::initGDAL();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
