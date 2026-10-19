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
    //   旧写法把重定向交给 13 处手写的 configureSettingsForTest() + 一个 static bool，
    //   新用例忘记调用照样跑得绿，没有任何断言会发现"这条用例把配置写进了开发者机器"。
    //   现在一个进程只有一条路径可走（这里），test_mainwindow.cpp 再断言落点确实如此：
    //   机制钉一次、结果断言一次，删掉下面三行会立刻让 14 处调用里的断言变红。
    // 落点事实（本轮实测，纠正上一版注释的误判）：
    //   * tests/test_main.cpp 不调 setApplicationName/setOrganizationName（真 GUI 在
    //     src/gui/main.cpp:36-37 调了 "GIS Tool"/"GIS"），所以 welcome_dialog.cpp:18 的
    //     默认构造 QSettings 在测试进程里是 invalid（无处可写）；
    //   * settings_manager.cpp:10 用的是两参构造 QSettings("GISTool","GISTool")，
    //     它走 NativeFormat ⇒ 实际落注册表 HKEY_CURRENT_USER\Software\GISTool，
    //     不吃下面这两条 setPath（测试侧改不了它，已挂账；不要指望本重定向保护注册表）；
    //   * 因此本重定向保护的是**所有显式/默认按 IniFormat 解析的 QSettings**，
    //     并按 pid 分道，消除 13 个 MainWindowTest 进程共享同一定名 ini 的并发互 trunc。
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
