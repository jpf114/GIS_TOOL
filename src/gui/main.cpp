#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFont>
#include <QFontDatabase>
#include <QFile>
#include <QPixmap>
#include <QSaveFile>
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

    const QStringList arguments = QCoreApplication::arguments();
    const bool selfTestMode = arguments.contains(QStringLiteral("--self-test"));
    std::optional<QString> screenshotPath;
    std::optional<QString> statusFilePath;
    std::optional<std::string> selectedPlugin;
    std::optional<std::string> selectedAction;
    std::vector<std::pair<std::string, std::string>> paramAssignments;
    const bool autoExecute = arguments.contains(QStringLiteral("--auto-execute"));
    const bool quitOnFinish = arguments.contains(QStringLiteral("--quit-on-finish"));
    for (int i = 1; i < arguments.size(); ++i) {
        const QString arg = arguments.at(i);
        if (arg == QStringLiteral("--screenshot") && (i + 1) < arguments.size()) {
            screenshotPath = arguments.at(i + 1);
            ++i;
            continue;
        }
        if (arg == QStringLiteral("--status-file") && (i + 1) < arguments.size()) {
            statusFilePath = arguments.at(i + 1);
            ++i;
            continue;
        }
        if (arg == QStringLiteral("--select-plugin") && (i + 1) < arguments.size()) {
            selectedPlugin = arguments.at(i + 1).toUtf8().toStdString();
            ++i;
            continue;
        }
        if (arg == QStringLiteral("--select-action") && (i + 1) < arguments.size()) {
            selectedAction = arguments.at(i + 1).toUtf8().toStdString();
            ++i;
            continue;
        }
        if (arg == QStringLiteral("--set-param") && (i + 1) < arguments.size()) {
            const QString assignment = arguments.at(i + 1);
            const int pos = assignment.indexOf('=');
            if (pos >= 0) {
                paramAssignments.emplace_back(
                    assignment.left(pos).toUtf8().toStdString(),
                    assignment.mid(pos + 1).toUtf8().toStdString());
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

    if (autoExecute && (quitOnFinish || screenshotPath.has_value() || statusFilePath.has_value())) {
        QObject::connect(&window, &MainWindow::executionFinished, &app,
            [&app, &window, screenshotPath, statusFilePath, quitOnFinish](bool) {
                if (statusFilePath.has_value()) {
                    const QFileInfo info(statusFilePath.value());
                    if (!info.absoluteDir().exists()) {
                        info.absoluteDir().mkpath(QStringLiteral("."));
                    }

                    QJsonObject status;
                    status.insert(QStringLiteral("success"), window.lastExecutionSuccess());
                    status.insert(QStringLiteral("cancelled"), window.lastExecutionCancelled());
                    status.insert(QStringLiteral("message"), window.lastExecutionMessage());
                    status.insert(QStringLiteral("raw_message"), window.lastExecutionRawMessage());

                    QSaveFile statusFile(statusFilePath.value());
                    if (statusFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        statusFile.write(QJsonDocument(status).toJson(QJsonDocument::Indented));
                        statusFile.commit();
                    }
                }
                if (screenshotPath.has_value()) {
                    const QFileInfo info(screenshotPath.value());
                    if (!info.absoluteDir().exists()) {
                        info.absoluteDir().mkpath(QStringLiteral("."));
                    }
                    window.grab().save(screenshotPath.value());
                }
                if (quitOnFinish || screenshotPath.has_value() || statusFilePath.has_value()) {
                    QTimer::singleShot(150, &app, &QApplication::quit);
                }
            });
    }

    if (selfTestMode) {
        QTimer::singleShot(300, &app, &QApplication::quit);
    }

    return app.exec();
}
