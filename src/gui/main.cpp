#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QPixmap>
#include <QTimer>
#include "mainwindow.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/runtime_env.h>

#include <algorithm>
#include <optional>
#include <string>

int main(int argc, char* argv[])
{
    gis::core::initRuntimeEnvironment();
    // Initialize GDAL (must be done before any GDAL operations)
    gis::core::initGDAL();

    // Must be set before QApplication/QGuiApplication is constructed.
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName("GIS Tool");
    app.setOrganizationName("GIS");
    {
        const QStringList preferredFamilies = {
            QStringLiteral("Microsoft YaHei UI"),
            QStringLiteral("Microsoft YaHei"),
            QStringLiteral("Noto Sans SC"),
            QStringLiteral("Noto Sans CJK SC"),
            QStringLiteral("Source Han Sans SC"),
            QStringLiteral("DengXian"),
            QStringLiteral("SimHei"),
            QStringLiteral("SimSun"),
            QStringLiteral("SimSun")
        };
        const QFontDatabase fontDb;
        QFont uiFont = app.font();
        for (const QString& family : preferredFamilies) {
            if (fontDb.families().contains(family)) {
                uiFont.setFamily(family);
                uiFont.setStyleStrategy(QFont::PreferAntialias);
                uiFont.setHintingPreference(QFont::PreferFullHinting);
                app.setFont(uiFont);
                break;
            }
        }
    }

    const bool selfTestMode = std::any_of(argv, argv + argc, [](const char* arg) {
        return arg != nullptr && std::string(arg) == "--self-test";
    });
    std::optional<QString> screenshotPath;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::string(argv[i]) == "--screenshot" && (i + 1) < argc && argv[i + 1] != nullptr) {
            screenshotPath = QString::fromLocal8Bit(argv[i + 1]);
            break;
        }
    }

    MainWindow window;
    window.show();

    if (screenshotPath.has_value()) {
        const QString targetPath = screenshotPath.value();
        QTimer::singleShot(500, &app, [&app, &window, targetPath]() {
            const QFileInfo info(targetPath);
            if (!info.absoluteDir().exists()) {
                info.absoluteDir().mkpath(QStringLiteral("."));
            }
            window.grab().save(targetPath);
            app.quit();
        });
    }

    if (selfTestMode) {
        QTimer::singleShot(300, &app, &QApplication::quit);
    }

    return app.exec();
}
