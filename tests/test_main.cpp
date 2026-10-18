#include <gtest/gtest.h>
#include <QApplication>
#include <QSettings>
#include <QByteArray>
#include <gis/core/runtime_env.h>
#include <gis/core/gdal_wrapper.h>

#include "test_support.h"

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    }
    QApplication app(argc, argv);
    // 21-FixA part2 I-6：机器级兜底，不是约定。
    // WHY 必须放在 main() 里做重定向，而不是靠每条用例自己调一次：
    //   tests 里**从没有**人调 setOrganizationName/setApplicationName，所以
    //     src/gui/settings_manager.cpp:10 的 QSettings("GISTool","GISTool")
    //     src/gui/welcome_dialog.cpp:18 的默认构造 QSettings
    //   在没有重定向时都会解析到**用户真实的 %APPDATA%**（前者 %APPDATA%\GISTool\GISTool.ini）。
    //   旧写法把这件事交给 13 处手写的 configureSettingsForTest() + 一个 static bool
    //   ⇒ 新用例忘记调用就是"绿着把配置写进开发者机器"，而且没有任何断言会发现。
    //   现在：一个进程只有一条路径可走（这里），test_mainwindow.cpp 再断言落点确实如此，
    //   删掉下面这一行会立刻让 13 处调用里的断言变红（探针式双向：机制 + 结果各钉一次）。
    {
        const auto settingsDir = gis::tests::privateSettingsDir();
        const QString settingsPath = QString::fromStdString(settingsDir.string());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsPath);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsPath);
    }
    gis::core::initRuntimeEnvironment();
    gis::core::initGDAL();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
