#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFile>
#include <QPixmap>
#include <QTimer>
#include "mainwindow.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/runtime_env.h>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

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
        const QStringList candidateFontFiles = {
            QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
            QStringLiteral("C:/Windows/Fonts/msyhbd.ttc"),
            QStringLiteral("C:/Windows/Fonts/msyh.ttf"),
            QStringLiteral("C:/Windows/Fonts/Deng.ttf"),
            QStringLiteral("C:/Windows/Fonts/simhei.ttf"),
            QStringLiteral("C:/Windows/Fonts/simsun.ttc")
        };
        for (const QString& fontFile : candidateFontFiles) {
            if (QFile::exists(fontFile)) {
                QFontDatabase::addApplicationFont(fontFile);
            }
        }

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
                uiFont.setPointSize(10);
                uiFont.setStyleStrategy(QFont::PreferAntialias);
                uiFont.setHintingPreference(QFont::PreferFullHinting);
                app.setFont(uiFont);
                app.setStyleSheet(QStringLiteral("* { font-family: \"%1\"; }").arg(family));
                break;
            }
        }
    }

    const bool selfTestMode = std::any_of(argv, argv + argc, [](const char* arg) {
        return arg != nullptr && std::string(arg) == "--self-test";
    });
    std::optional<QString> screenshotPath;
    std::optional<std::string> selectedPlugin;
    std::optional<std::string> selectedAction;
    std::vector<std::pair<std::string, std::string>> paramAssignments;
    const bool autoExecute = std::any_of(argv, argv + argc, [](const char* arg) {
        return arg != nullptr && std::string(arg) == "--auto-execute";
    });
    const bool quitOnFinish = std::any_of(argv, argv + argc, [](const char* arg) {
        return arg != nullptr && std::string(arg) == "--quit-on-finish";
    });
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::string(argv[i]) == "--screenshot" && (i + 1) < argc && argv[i + 1] != nullptr) {
            screenshotPath = QString::fromLocal8Bit(argv[i + 1]);
            ++i;
            continue;
        }
        if (argv[i] != nullptr && std::string(argv[i]) == "--select-plugin" && (i + 1) < argc && argv[i + 1] != nullptr) {
            selectedPlugin = argv[i + 1];
            ++i;
            continue;
        }
        if (argv[i] != nullptr && std::string(argv[i]) == "--select-action" && (i + 1) < argc && argv[i + 1] != nullptr) {
            selectedAction = argv[i + 1];
            ++i;
            continue;
        }
        if (argv[i] != nullptr && std::string(argv[i]) == "--set-param" && (i + 1) < argc && argv[i + 1] != nullptr) {
            const std::string assignment = argv[i + 1];
            const size_t pos = assignment.find('=');
            if (pos != std::string::npos && pos > 0) {
                paramAssignments.emplace_back(
                    assignment.substr(0, pos),
                    assignment.substr(pos + 1));
            }
            ++i;
        }
    }

    MainWindow window;
    if (selectedPlugin.has_value()) {
        window.selectPluginByName(selectedPlugin.value());
    }
    if (selectedAction.has_value()) {
        window.selectActionByKey(selectedAction.value());
    }
    for (const auto& [key, value] : paramAssignments) {
        window.setParamValue(key, value);
    }
    window.show();

    if (screenshotPath.has_value() && !autoExecute) {
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

    if (autoExecute) {
        QTimer::singleShot(200, &app, [&window]() {
            window.triggerExecute();
        });
    }

    if (autoExecute && (quitOnFinish || screenshotPath.has_value())) {
        QObject::connect(&window, &MainWindow::executionFinished, &app,
            [&app, &window, screenshotPath, quitOnFinish](bool) {
                if (screenshotPath.has_value()) {
                    const QFileInfo info(screenshotPath.value());
                    if (!info.absoluteDir().exists()) {
                        info.absoluteDir().mkpath(QStringLiteral("."));
                    }
                    window.grab().save(screenshotPath.value());
                }
                if (quitOnFinish || screenshotPath.has_value()) {
                    QTimer::singleShot(150, &app, &QApplication::quit);
                }
            });
    }

    if (selfTestMode) {
        QTimer::singleShot(300, &app, &QApplication::quit);
    }

    return app.exec();
}
