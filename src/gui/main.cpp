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
#include "welcome_dialog.h"
#include "app_settings.h"
#include <gis/core/gdal_wrapper.h>
#include <gis/core/runtime_env.h>
#include <gis/core/logger_init.h>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    gis::core::initRuntimeEnvironment();
    gis::core::initDefaultLogging();
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
    std::vector<std::string> failedParamAssignments;
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

    // N101 机器级落点守卫：判据放在**被 spawn 的真 GUI 进程**里，一处覆盖 105 个 driver ——
    // 放测试侧就退化成"每个 driver 记得检查"，而 Global Constraints 明令不许靠约定。
    // WHY：gui_* 那批用例走 -DGUI_PATH=$<TARGET_FILE:gis-gui>，driver 另起独立进程，父进程的环境
    //   是唯一能把落点告诉它的通道；通道断了（载体被改/漏 include/ctest 直接 spawn）就等于
    //   每条用例往真实用户配置写一遍设置、并把注册表里的历史值当成用户数据导入。
    //   所以"回归驱动的运行"必须自带落点，否则宁可非零退出，也不许它悄悄污染用户状态。
    //   --self-test 也算回归驱动：gui_smoke_startup 就是靠它跑的。
    // 退出码 3 与既有码可区分（0 正常 / app.exit(2) 参数应用失败），且 105 个 driver 都有
    //   RESULT_VARIABLE + "非 0 即 FATAL_ERROR" 的判据，gui_smoke_startup 由 ctest 直接读退出码。
    if ((selfTestMode || screenshotPath.has_value() || statusFilePath.has_value())
        && !gis::gui::settingsOverrideActive()) {
        qCritical("--self-test/--screenshot/--status 运行下 GIS_TOOL_SETTINGS_INI 未注入"
                  " ⇒ 会写真实用户配置（N101）");
        return 3;
    }

    if (selfTestMode || arguments.contains(QStringLiteral("--auto-execute"))) {
        gis::gui::markFirstRunComplete();
    }

    MainWindow window;
    window.setAutomationMode(autoExecute);
    if (selectedPlugin.has_value()) {
        window.selectPluginByName(selectedPlugin.value());
    }
    if (selectedAction.has_value()) {
        window.selectActionByKey(selectedAction.value());
    }
    for (const auto& [key, value] : paramAssignments) {
        if (!window.setParamValue(key, value)) {
            failedParamAssignments.push_back(key);
        }
    }
    window.show();

    if (autoExecute && !failedParamAssignments.empty()) {
        QTimer::singleShot(100, &app, [&app, &window, screenshotPath, statusFilePath, failedParamAssignments]() {
            const QString failedKeys = QString::fromUtf8(
                [&failedParamAssignments]() {
                    std::string joined;
                    for (size_t i = 0; i < failedParamAssignments.size(); ++i) {
                        if (i > 0) {
                            joined += ",";
                        }
                        joined += failedParamAssignments[i];
                    }
                    return joined;
                }().c_str());

            if (statusFilePath.has_value()) {
                const QFileInfo info(statusFilePath.value());
                if (!info.absoluteDir().exists()) {
                    info.absoluteDir().mkpath(QStringLiteral("."));
                }

                QJsonObject status;
                status.insert(QStringLiteral("success"), false);
                status.insert(QStringLiteral("cancelled"), false);
                status.insert(QStringLiteral("message"),
                    QStringLiteral("浠ヤ笅鍙傛暟鏈兘搴旂敤鍒板綋鍓嶇晫闈細%1").arg(failedKeys));
                status.insert(QStringLiteral("raw_message"),
                    QStringLiteral("failed_to_apply_param:%1").arg(failedKeys));

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

            app.exit(2);
        });
    }

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
