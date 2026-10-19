// 批次 B / Task B1：gis::gui::appSettings() 单一落点 + "补缺不覆盖"的旧设置导入。
//
// R-B2b 红线（本文件与任何测试代码都适用）：**一律不得 qunsetenv("GIS_TOOL_SETTINGS_INI")**。
//   tests/test_main.cpp 在每个测试进程里都设了这个变量；一旦清掉，路径就解析成
//   **真人机器上的** %LOCALAPPDATA%，而 migrateLegacySettings() 会 setValue(marker)+sync()
//   ⇒ 测试往用户目录写盘。要断"生产路径形状"只能走纯函数 resolvedSettingsPath(QString())。
//
// Qt 6 事实（构树实测，决定了下面 helper 的形状）：qsettings.h 末尾有 Q_DISABLE_COPY(QSettings)
//   ⇒ QSettings **既不可拷贝也不可移动** ⇒ 任何函数都不能"返回一个已打开的 QSettings 对象"，
//   只能返回**路径**，由调用方在自己的栈上原地构造（C++17 保证的返回值初始化才允许 prvalue 返回）。
#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <initializer_list>
#include <utility>

#include "../src/gui/app_settings.h"  // src/gui 的头文件不在 include 路径上（同 tests/test_coverage_meta.cpp:5 的写法）

namespace {

// 在 dir 里落一个 ini 文件并返回它的**路径**（不能返回 QSettings 对象，理由见文件头）。
QString writeIni(const QTemporaryDir& dir, const QString& name,
                 std::initializer_list<std::pair<const char*, const char*>> kv) {
    const QString path = dir.filePath(name);
    QSettings s(path, QSettings::IniFormat);
    for (const auto& [k, v] : kv)
        s.setValue(QString::fromUtf8(k), QString::fromUtf8(v));
    s.sync();
    return path;
}

} // namespace

// R-B4 的正面命题：源里有、目标里没有的键才被复制，已有键保持原值。
TEST(AppSettingsMigration, CopiesOnlyMissingKeys) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QSettings from(writeIni(dir, QStringLiteral("legacy.ini"),
                            {{"paths/lastInputDir", "L:/in"}, {"recent/files", "a.tif"}}),
                   QSettings::IniFormat);
    QSettings to(writeIni(dir, QStringLiteral("target.ini"), {{"paths/lastInputDir", "M:/keepme"}}),
                 QSettings::IniFormat);

    const int copied = gis::gui::copyMissingKeys(from, to);
    to.sync();
    EXPECT_EQ(copied, 1);
    EXPECT_EQ(to.value("paths/lastInputDir").toString(), "M:/keepme") << "已有键必须不被覆盖（R-B4）";
    EXPECT_EQ(to.value("recent/files").toString(), "a.tif");
}

TEST(AppSettingsMigration, IsIdempotent) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QSettings from(writeIni(dir, QStringLiteral("legacy.ini"), {{"defaults/outputSrs", "EPSG:3857"}}),
                   QSettings::IniFormat);
    QSettings to(writeIni(dir, QStringLiteral("target.ini"), {}), QSettings::IniFormat);

    EXPECT_EQ(gis::gui::copyMissingKeys(from, to), 1);
    to.sync();
    EXPECT_EQ(gis::gui::copyMissingKeys(from, to), 0) << "第二次必须一条都不复制";
}

// 嵌套组与字符串列表这两个 Qt 特有形态必须原样带过去（recent/files 在生产里就是 QStringList）。
TEST(AppSettingsMigration, CopiesNestedGroupsAndStringLists) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString legacyPath;
    {
        QSettings from(dir.filePath("legacy.ini"), QSettings::IniFormat);
        from.setValue("recent/files", QStringList{"a.tif", "b.tif"});
        from.beginGroup("app");
        from.setValue("first_run_complete", true);
        from.endGroup();
        from.sync();
        legacyPath = from.fileName();
    }
    QSettings from(legacyPath, QSettings::IniFormat);
    QSettings to(dir.filePath("target.ini"), QSettings::IniFormat);

    EXPECT_EQ(gis::gui::copyMissingKeys(from, to), 2);
    to.sync();
    EXPECT_EQ(to.value("app/first_run_complete").toBool(), true);
    EXPECT_EQ(to.value("recent/files").toStringList(), (QStringList{"a.tif", "b.tif"}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6：appSettings() 自身的三条臂（注入态兜底 / 生产形状 / 注入优先且跳过旧落点）。
TEST(AppSettingsPath, TestHarnessAlwaysOverrides) {
    // 机器级兜底（R-B2b）：测试进程一启动就必须带着注入路径。
    // 删掉 tests/test_main.cpp 里那条 qputenv 会立刻让这条红，而不是"悄悄写到真人 %LOCALAPPDATA%"。
    EXPECT_TRUE(gis::gui::settingsOverrideActive())
        << "测试进程未注入 GIS_TOOL_SETTINGS_INI ⇒ appSettings() 会写开发者的真实配置目录";
    EXPECT_TRUE(gis::gui::appSettingsPath().endsWith(QStringLiteral(".ini")));
}

TEST(AppSettingsPath, ProductionShapeWithoutTouchingDisk) {
    // 断"生产路径形状"只能走纯函数（R-B2b）；在这里构造 QSettings 就会在真人机器上落盘。
    const QString p = gis::gui::resolvedSettingsPath(QString());
    const QString cfg = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    EXPECT_FALSE(p.isEmpty());
    EXPECT_TRUE(p.startsWith(cfg)) << "path=" << p.toStdString() << " cfg=" << cfg.toStdString();
    EXPECT_EQ(QFileInfo(p).fileName(), QStringLiteral("settings.ini"));
    EXPECT_EQ(QSettings(p, QSettings::IniFormat).format(), QSettings::IniFormat)
        << "生产落点必须是 IniFormat；NativeFormat 意味着又写回注册表";
}

TEST(AppSettingsPath, OverrideWinsAndSkipsLegacyRead) {
    // 本用例把注入路径从 harness 的 SettingsManager.ini **改指到另一个私有临时文件**，
    // 这是安全的，因为 tests/CMakeLists.txt:169 用的是 gtest_discover_tests(DISCOVERY_MODE PRE_TEST)
    // ⇒ ctest 侧**一条用例一个进程**（本仓每条 gtest 用例都被独立拉起一次），
    // 在这里 qputenv 不会泄漏到别的用例。要强调的红线是：任何用例都**不得 qunsetenv**（R-B2b），
    // 清掉变量才会把落点还给真人的 %LOCALAPPDATA%。
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString injected = dir.filePath(QStringLiteral("injected.ini"));
    qputenv("GIS_TOOL_SETTINGS_INI", injected.toLocal8Bit());  // 只改指到别的私有文件，绝不 qunsetenv
    {
        QSettings s = gis::gui::appSettings();
        s.setValue("probe/x", 1);
        s.sync();
    }
    const QSettings s = gis::gui::appSettings();
    EXPECT_EQ(s.allKeys(), (QStringList{QStringLiteral("probe/x")}))
        << "注入态除显式写入的键外必须为空 ⇒ 非空说明从真实旧落点读进了用户数据";
    EXPECT_FALSE(s.contains(QLatin1String(gis::gui::kMigrationMarkerKey)))
        << "注入态不得写迁移标记（R-B2）";
}
