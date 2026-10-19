#include <gtest/gtest.h>
#include <QApplication>
#include <QSettings>
#include <QByteArray>
#include <QDir>
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
    // 落点事实（批次 B2 之后：两个生产消费点都走 gis::gui::appSettings()，本文件只负责注入）：
    //   * tests/test_main.cpp 不调 setApplicationName/setOrganizationName（真 GUI 在
    //     src/gui/main.cpp:36-37 调了 "GIS Tool"/"GIS"），所以**默认构造**的 QSettings 在本进程里
    //     不是"invalid（无处可写）"而是 IniFormat + status=AccessError，落到下面 setPath 圈的
    //     <私有目录>/Unknown Organization/gis_tests.ini（org 未设 ⇒ Qt 6 拿可执行文件名顶上）。
    //     安全结论不变（读不到用户数据），但理由要说准，别写成"无处可写"。
    //     批次 B2 之后 welcome_dialog 也不再默认构造了，它和 SettingsManager 一样走 appSettings()。
    //   * setDefaultFormat/setPath 只管**按 IniFormat 解析**的 QSettings；两参构造
    //     QSettings("GISTool","GISTool") 走 NativeFormat，会直接落注册表
    //     HKEY_CURRENT_USER\Software\GISTool —— 那是 SettingsManager 原先的写法，
    //     于是每次跑 MainWindowTest 都在写真实用户注册表（盘上实测：recent/files 里全是测试产物路径）。
    //     ⇒ 现在 SettingsManager 的落点由 appSettings() 决定，而 appSettings() 在 GIS_TOOL_SETTINGS_INI
    //       非空时逐字用那个文件（生产侧不设该变量 ⇒ 落 AppConfigLocation/settings.ini），
    //       断言在 test_mainwindow.cpp（读 SettingsManager::storagePath()，测结果不是测约定）。
    //   * 下面三条 setPath 依旧保留：它们保护的是其余按 IniFormat 解析的 QSettings，并按 pid 分道，
    //     消除 13 个 MainWindowTest 进程共享同一定名 ini 的并发互 trunc。
    {
        const auto settingsDir = gis::tests::privateSettingsDir();
        const QString settingsPath = QString::fromStdString(settingsDir.string());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsPath);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsPath);
        const QByteArray managerIniPath =
            QDir(settingsPath).filePath(QStringLiteral("SettingsManager.ini")).toUtf8();
        qputenv("GIS_TOOL_SETTINGS_INI", managerIniPath);
    }
    gis::core::initRuntimeEnvironment();
    gis::core::initGDAL();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
