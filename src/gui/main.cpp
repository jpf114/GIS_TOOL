#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFont>
#include <QFontDatabase>
#include <QFile>
#include <QImage>
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

namespace {

// 批次 B / B7（N98）内容层：grab() 之后由**渲染进程自己**数不同颜色数，连同帧宽高一起写进
//   既有 status 文件，让 driver 第一次能把"窗口真的画出来了"当断言判，而不是按平台名跳过。
// WHY 做在 gis-gui 里而不是 CMake 侧：CMake 没有 zlib，逐扫描线还原 PNG filter 不现实；
//   可行路径只剩"每用例 spawn 一次外部解码器"，那才是把解码开销搬进每条 gui_ 截图腿的地方。
// WHY 三个值一起报：自报口径单独看是自证循环 ⇒ driver 必须拿它跟 IHDR 独立解出的宽高交叉比对
//   （结构层见 tests/gui_regression_helpers.cmake 的 gis_gui_parse_png_ihdr_size），对不上即红。
// WHY 全量像元而非抽样：整帧去重只是进程内一次遍历，成本远小于为解码再 spawn 一个外部解释器；
//   而且抽样子集的颜色数不等于全帧的颜色数 ⇒ 阈值口径就没法说清了。
struct ScreenshotStats {
    int width = 0;
    int height = 0;
    int distinctColors = 0;
};

ScreenshotStats collectScreenshotStats(const QPixmap& pixmap)
{
    ScreenshotStats stats;
    if (pixmap.isNull()) {
        return stats;  // 0/0/0：结构层与内容层都会据此判红，这里不重复报错、也不假装成功
    }
    const QImage image = pixmap.toImage();
    stats.width = image.width();
    stats.height = image.height();

    // 逐像元取 QRgb（QImage::pixel 内部按格式还原 ⇒ 不必先转 Format_ARGB32，
    //   也就不踩"这版 Qt 有没有 convertedToFormat"的接口差异）。
    std::vector<QRgb> pixels;
    pixels.reserve(static_cast<size_t>(image.width()) * static_cast<size_t>(image.height()));
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            pixels.push_back(image.pixel(x, y));
        }
    }
    std::sort(pixels.begin(), pixels.end());
    const auto distinctEnd = std::unique(pixels.begin(), pixels.end());
    stats.distinctColors = static_cast<int>(std::distance(pixels.begin(), distinctEnd));
    return stats;
}

void insertScreenshotStats(QJsonObject& status, const QPixmap& pixmap)
{
    const ScreenshotStats stats = collectScreenshotStats(pixmap);
    status.insert(QStringLiteral("screenshot_width"), stats.width);
    status.insert(QStringLiteral("screenshot_height"), stats.height);
    status.insert(QStringLiteral("screenshot_distinct_colors"), stats.distinctColors);
}

}  // namespace

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

    // N101 + N102 机器级落点守卫：判据放在**被 spawn 的真 GUI 进程**里，一处覆盖全部 driver 腿 ——
    // 放测试侧就退化成"每个 driver 记得检查"，而 Global Constraints 明令不许靠约定。
    // WHY：gui_* 那批用例走 -DGUI_PATH=$<TARGET_FILE:gis-gui>，driver 另起独立进程，父进程的环境
    //   是唯一能把落点告诉它的通道；通道断了（载体被改/漏 include/ctest 直接 spawn）就等于
    //   每条用例往真实用户配置写一遍设置，并把"保存自定义指数预设"写进用户的 AppConfigLocation
    //   （custom_index_preset_store.cpp 是它的第二个写者，覆盖变量是 GIS_CUSTOM_INDEX_PRESET_FILE）。
    //   所以"回归驱动的运行"必须**两个落点变量都注入**，否则宁可非零退出，也不许它悄悄污染用户状态。
    //   --self-test 也算回归驱动：gui_smoke_startup 就是靠它跑的。
    // 预设变量的判据与写者侧对齐（resolvePresetFilePath 把"trim 后非空"当覆盖生效）：
    //   守卫若只查"非空"，一串空格的注入会骗过守卫、让 store 回退到用户目录 ⇒ 这里按 store 语义查。
    // settings 侧（GIS_TOOL_SETTINGS_INI）**故意**不 trim ⇒ 两个变量的判据不对称，这是已知且有意：
    //   settingsOverrideActive() 的读者是"要不要构造 NativeFormat 旧源"，判据要**宽松**（非空即算有人在
    //   注入，宁可少迁移一次也不去 HKCU 建键）；store 侧的读者是"这个路径能不能用"，判据要**严格**。
    //   两处相反的失败方向没法共用一个函数体 ⇒ 别"顺手改一致"，完整理由写在 settingsOverrideActive()
    //   的定义处（src/gui/app_settings.cpp）。
    // 退出码 3 与既有码可区分（0 正常 / app.exit(2) 参数应用失败），driver 侧都有
    //   RESULT_VARIABLE + "非 0 即 FATAL_ERROR" 的判据，gui_smoke_startup 由 ctest 直接读退出码。
    // 覆盖面现跑命令（两棵树都照抄，只换目录与配置名）：
    //   ctest --test-dir build/release -C Release -N -R "^gui_" | tail -n 1
    //   ctest --test-dir build/debug   -C Debug   -N -R "^gui_" | tail -n 1
    if (selfTestMode || screenshotPath.has_value() || statusFilePath.has_value()) {
        const bool presetOverrideActive =
            !qEnvironmentVariable("GIS_CUSTOM_INDEX_PRESET_FILE").trimmed().isEmpty();
        if (!gis::gui::settingsOverrideActive() || !presetOverrideActive) {
            qCritical("--self-test/--screenshot/--status 运行下 GIS_TOOL_SETTINGS_INI 或 "
                      "GIS_CUSTOM_INDEX_PRESET_FILE 未注入 ⇒ 会写真实用户配置（N101/N102）");
            return 3;
        }
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

            // B7 内容层的落点顺序：先抓图，再写 status ⇒ 统计值能在 status 落盘前插进去。
            //   这条腿判的是"参数应用失败"（退出码 2），但窗口照样画得出来 ⇒ 截图判据对它同样成立，
            //   而它断言的失败口径（success=false / raw_message / 不产出文件）一条都没被改动。
            std::optional<QPixmap> capturedScreenshot;
            if (screenshotPath.has_value()) {
                capturedScreenshot = window.grab();
            }

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

                if (capturedScreenshot.has_value()) {
                    insertScreenshotStats(status, *capturedScreenshot);
                }

                QSaveFile statusFile(statusFilePath.value());
                if (statusFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    statusFile.write(QJsonDocument(status).toJson(QJsonDocument::Indented));
                    statusFile.commit();
                }
            }

            if (screenshotPath.has_value() && capturedScreenshot.has_value()) {
                const QFileInfo info(screenshotPath.value());
                if (!info.absoluteDir().exists()) {
                    info.absoluteDir().mkpath(QStringLiteral("."));
                }
                capturedScreenshot->save(screenshotPath.value());
            }

            app.exit(2);
        });
    }

    // 这条静止态支路**不报内容统计**：它没有 status 通道可写（B7 的内容层就搭在既有 status 文件上，
    //   另开一条旁路文件只会多一个没人交叉比对的自报口径）。
    // ⚠ 别说成"结构层是它唯一能拿到的判定"（B7 复审纠正）：按下条命令现跑，走这条路（带 --screenshot
    //   却不带 --auto-execute）的 driver 数是 **0** ⇒ 今天没有任何截图判据落在这里；而真有腿走进来，
    //   它也会红在"status 缺键"（helpers 的通道闸与缺键闸都在结构层之前），不是红在"只判了结构"。
    //   将来真要接这条腿，必须连内容层一起设计，不许只留结构层就当作有覆盖。
    //   现跑命令（带 --screenshot 却不带 --auto-execute 的 driver；注释行不会被数进去）：
    //     for f in $(grep -rlE -- "--screenshot" tests/run_*.cmake); do
    //         grep -qE -- "--auto-execute" "$f" || echo "$f"; done | wc -l
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
                // B7 内容层：抓取时机保持原位（执行完成之后），只是把 grab() 提到 status 之前，
                //   好把帧宽高与不同颜色数一起写进**同一个** status 文件 ⇒ 全部 gui_ 截图腿由此拿到数据源。
                std::optional<QPixmap> capturedScreenshot;
                if (screenshotPath.has_value()) {
                    capturedScreenshot = window.grab();
                }

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

                    if (capturedScreenshot.has_value()) {
                        insertScreenshotStats(status, *capturedScreenshot);
                    }

                    QSaveFile statusFile(statusFilePath.value());
                    if (statusFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        statusFile.write(QJsonDocument(status).toJson(QJsonDocument::Indented));
                        statusFile.commit();
                    }
                }
                if (screenshotPath.has_value() && capturedScreenshot.has_value()) {
                    const QFileInfo info(screenshotPath.value());
                    if (!info.absoluteDir().exists()) {
                        info.absoluteDir().mkpath(QStringLiteral("."));
                    }
                    capturedScreenshot->save(screenshotPath.value());
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
