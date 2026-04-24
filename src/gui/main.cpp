#include <QApplication>
#include "mainwindow.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/runtime_env.h>

int main(int argc, char* argv[])
{
    gis::core::initRuntimeEnvironment();
    // Initialize GDAL (must be done before any GDAL operations)
    gis::core::initGDAL();

    QApplication app(argc, argv);
    app.setApplicationName("GIS Tool");
    app.setOrganizationName("GIS");

    // High DPI support
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    MainWindow window;
    window.show();
    return app.exec();
}
