#include <QApplication>
#include <QTimer>
#include "mainwindow.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/runtime_env.h>

#include <algorithm>
#include <string>

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

    const bool selfTestMode = std::any_of(argv, argv + argc, [](const char* arg) {
        return arg != nullptr && std::string(arg) == "--self-test";
    });

    MainWindow window;
    window.show();

    if (selfTestMode) {
        QTimer::singleShot(300, &app, &QApplication::quit);
    }

    return app.exec();
}
