// 批次 B / Task B1：gis::gui::appSettings() 单一落点 + "补缺不覆盖"的旧设置导入。
//
// R-B2b 红线（本文件与任何测试代码都适用）：**一律不得 qunsetenv("GIS_TOOL_SETTINGS_INI")**。
//   tests/test_main.cpp 在每个测试进程里都设了这个变量；一旦清掉，路径就解析成
//   **真人机器上的** %LOCALAPPDATA%，而 migrateLegacySettings() 会 setValue(marker)+sync()
//   ⇒ 测试往用户目录写盘。要断"生产路径形状"只能走纯函数 resolvedSettingsPath(QString())。
//
// Qt 6 事实（构树实测 + B3a 的三版最小编译探针，决定了下面 helper 与工厂的形状）：
//   qsettings.h:184 有 Q_DISABLE_COPY(QSettings) ⇒ QSettings **既不可拷贝也不可移动**，
//   所以"返回一个已打开的 QSettings 对象"只有两条路：
//     * 按值返回 **prvalue**（C++17 保证的返回值初始化直接构造进调用方的对象）—— appSettings() 用的就是它；
//       写成"具名局部量再 return"立刻编不过（探针实测 C2280）。
//     * 把对象放到堆上，返回 std::unique_ptr<QSettings> —— 旧落点工厂（R-B7 的注入面）用的就是它，
//       因为工厂要在调用点之后才被选中，prvalue 那条路要求每个工厂都写成一条 return 表达式。
//   给已存在的 QSettings 引用"原地赋值/swap"这条路**不通**：赋值是 deleted，Qt 6.11 的 QSettings 也没有 swap。
#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <initializer_list>
#include <memory>
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

// 一条"由私有 ini 后备"的旧落点源（R-B7 的注入面）。路径按值捕获，工厂每次新开对象，
// 与生产接线里"每条源自己决定用什么参数构造 QSettings"是同一个形状。
gis::gui::LegacySettingsSource iniSource(const QString& path) {
    gis::gui::LegacySettingsSource source;
    source.label = QStringLiteral("ini:%1").arg(QFileInfo(path).fileName());
    source.open = [path] { return std::make_unique<QSettings>(path, QSettings::IniFormat); };
    return source;
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
// R-B7：迁移**内容**的真判据 —— 旧落点由参数注入，所以这几条不需要开发者机器上有注册表键，
// 也不会去读它（migrateLegacySettingsFrom 不碰 GIS_TOOL_SETTINGS_INI 门控，门控在生产入口那一层）。
TEST(AppSettingsMigration, ImportsFromInjectedSourcesAndStampsMarker) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString targetPath = dir.filePath(QStringLiteral("target.ini"));
    const QString legacyPath = writeIni(dir, QStringLiteral("old.ini"),
                                        {{"app/first_run_complete", "true"},
                                         {"defaults/outputSrs", "EPSG:4326"}});
    {
        QSettings target(targetPath, QSettings::IniFormat);
        const int imported = gis::gui::migrateLegacySettingsFrom(target, {iniSource(legacyPath)});
        EXPECT_EQ(imported, 2) << "两条都要被搬过来，计数由循环自己累加";
        EXPECT_TRUE(target.value(QLatin1String("app/first_run_complete")).toBool());
        EXPECT_EQ(target.value(QLatin1String("defaults/outputSrs")).toString(), "EPSG:4326");
        EXPECT_TRUE(target.contains(QLatin1String(gis::gui::kMigrationMarkerKey)))
            << "一次性标记必须由 From 这一层打（生产入口只多一道 R-B2 门控）";
    }
    {   // 第二次必须一条都不导，而且幂等要靠**标记**、不能靠"目标里恰好还留着那些键"：
        //     这里先把一条已导入的键从目标里删掉，没有标记早退的话它会被源**复活**。
        QSettings target(targetPath, QSettings::IniFormat);
        target.remove(QStringLiteral("defaults/outputSrs"));
        target.sync();
        ASSERT_FALSE(target.contains(QStringLiteral("defaults/outputSrs")));
        EXPECT_EQ(gis::gui::migrateLegacySettingsFrom(target, {iniSource(legacyPath)}), 0)
            << "标记已在 ⇒ 同一个目标第二次不再导入";
        EXPECT_FALSE(target.contains(QStringLiteral("defaults/outputSrs")))
            << "旧键被源复活 ⇒ 一次性标记（R-B4）没起作用，用户删掉的设置下次启动又长回来";
    }
}

TEST(AppSettingsMigration, DoesNotOverwriteExistingKeysFromInjectedSource) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString legacyPath = writeIni(dir, QStringLiteral("old.ini"),
                                        {{"paths/lastInputDir", "L:/in"}, {"recent/files", "a.tif"}});
    const QString targetPath =
        writeIni(dir, QStringLiteral("target.ini"), {{"paths/lastInputDir", "M:/keepme"}});
    QSettings target(targetPath, QSettings::IniFormat);

    EXPECT_EQ(gis::gui::migrateLegacySettingsFrom(target, {iniSource(legacyPath)}), 1)
        << "两条源值里只有目标缺的那条算复制";
    EXPECT_EQ(target.value(QLatin1String("paths/lastInputDir")).toString(), "M:/keepme")
        << "已有键必须不被覆盖（R-B4）";
    EXPECT_EQ(target.value(QLatin1String("recent/files")).toString(), "a.tif");
}

// 生产给的是**两个**源；这条钉住"From 会把 sources 逐条读完"。
// 少了它，实现退化成"只看第一个源"时上面几条（都只喂一条源）依然全绿。
TEST(AppSettingsMigration, ReadsEverySourceInTheList) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString firstPath = writeIni(dir, QStringLiteral("one.ini"),
                                       {{"defaults/outputSrs", "EPSG:4326"}});
    const QString secondPath = writeIni(dir, QStringLiteral("two.ini"),
                                        {{"app/first_run_complete", "true"}});
    QSettings target(dir.filePath(QStringLiteral("target.ini")), QSettings::IniFormat);

    EXPECT_EQ(gis::gui::migrateLegacySettingsFrom(target, {iniSource(firstPath), iniSource(secondPath)}), 2);
    EXPECT_EQ(target.value(QLatin1String("defaults/outputSrs")).toString(), "EPSG:4326") << "第一条源没读到";
    EXPECT_TRUE(target.value(QLatin1String("app/first_run_complete")).toBool()) << "第二条源没读到";
}

// 一条源没接上（工厂为空，或工厂返回空指针）时不得把整次启动崩掉，也不得顶掉其余好源。
TEST(AppSettingsMigration, SkipsMiswiredSourcesWithoutAborting) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString legacyPath =
        writeIni(dir, QStringLiteral("old.ini"), {{"defaults/outputSrs", "EPSG:3857"}});
    gis::gui::LegacySettingsSource nullFactory = iniSource(legacyPath);
    nullFactory.open = nullptr;
    gis::gui::LegacySettingsSource nullObject = iniSource(legacyPath);
    nullObject.open = [] { return std::unique_ptr<QSettings>(); };
    QSettings target(dir.filePath(QStringLiteral("target.ini")), QSettings::IniFormat);

    EXPECT_EQ(gis::gui::migrateLegacySettingsFrom(
                  target, {nullFactory, nullObject, iniSource(legacyPath)}),
              1)
        << "断掉的两条既不能崩，也不能顶掉第三条好源";
    EXPECT_EQ(target.value(QLatin1String("defaults/outputSrs")).toString(), "EPSG:3857");
}

// 生产接线本体：migrateLegacySettings() 只从这里取源，所以摘掉一条 ⇒ 这条红，
// 而不是"迁移少搬了一个落点却没有任何用例知道"（R-B7 判别性验收要的就是这一条）。
// ⚠ 这里**不调用 open()**：两个生产源在 Windows 上都是 NativeFormat，光构造就会在 HKCU 下建键
//   （落点实测见 src/gui/app_settings.cpp 顶部）⇒ 判据只能取"身份 + 工厂是否接上"这种不构造的形态。
// ⚠ 这条钉的是**表**，不是 migrateLegacySettings() 那一行的调用点：R-B2 门控在任何测试进程里都先
//   return 0，所以"调用点只交一条源"这种写法没有任何用例能观察到（B3a 报告 INJ-2 实测为绿，已登记）。
//   想观察就得让用例真去构造 NativeFormat 源 ⇒ 读用户注册表当测试输入 + 在 HKCU 建键，两条都撞红线。
TEST(AppSettingsLegacySources, ProductionWiringIsTheTwoLegacyLandingPoints) {
    const QList<gis::gui::LegacySettingsSource>& sources = gis::gui::productionLegacySources();
    QStringList labels;
    int wired = 0;
    for (const gis::gui::LegacySettingsSource& source : sources) {
        labels << source.label;
        if (source.open)
            ++wired;
    }
    EXPECT_EQ(labels, (QStringList{QStringLiteral("QSettings(GISTool, GISTool)"),
                                   QStringLiteral("QSettings()")}))
        << "生产旧落点的身份与条数就是迁移的输入面；label 由构造参数展开，断它=断接线"
        << " 实际=" << labels.join(QStringLiteral(" | ")).toStdString();
    EXPECT_EQ(wired, labels.size()) << "有身份却没有工厂的条目等于该源根本没接上";
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
    // 断"生产路径形状"只能走纯函数 resolvedSettingsPath(QString())（R-B2b 红线）。
    // 纪律：**这里不许构造任何指向生产路径的 QSettings 对象**。理由要说准，而且"构造会不会写盘"
    // 这件事**按 format 分岔**，不能混成一句（否则下一个读者会以为"构造永远无害"）：
    //   * **IniFormat**：构造本身不写盘（写盘要 setValue()+sync()，见任务报告的探针 #3），但一旦有人
    //     顺手补一句 setValue，这个落点就是真人机器上的 %LOCALAPPDATA% ⇒ 污染用户目录，
    //     还是跨用例共享的定名文件。
    //   * **NativeFormat**：光构造就会在 HKCU 下把这一层键建出来，哪怕全程只读（取证见
    //     src/gui/app_settings.cpp:26-30 与任务报告 §2 的注册表 lastWriteTime）。
    // 至于"生产落点是不是注册表"：本用例判不了（format() 返回的就是刚传给构造的参数，与被测实现
    // 无关，任何实现下都不可能红 —— 那条恒真断言已在 B1 修复轮删掉），
    // 所以也不要把它抄回来；这条判据由 B2 在 tests/test_mainwindow.cpp 的落点守卫块断
    // （那里 appSettings().format() 是**被测实现自己选的**构造参数，才有鉴别力）。
    const QString p = gis::gui::resolvedSettingsPath(QString());
    const QString cfg = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    EXPECT_FALSE(p.isEmpty());
    // 承重的是**文件名那一行**；下面 startsWith 是半自反的（cfg 取自同一次 QStandardPaths 调用，
    // 与实现无关，永远为真），留着只为把"前缀来自本进程"这个形状说清楚，别误当它有鉴别力。
    EXPECT_TRUE(p.startsWith(cfg)) << "path=" << p.toStdString() << " cfg=" << cfg.toStdString();
    EXPECT_EQ(QFileInfo(p).fileName(), QStringLiteral("settings.ini"));
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
    // F-1（评审严重项）：override 的"最高优先"必须由**真实对象的落点**钉住，不能只靠"注入文件里
    // 除我写的键外为空"。后者在退化成"appSettings() 永远吃 AppConfigLocation"时仍然全绿
    // ——写与读落在同一个路径上，键集合当然干净，而落点已变成非 pid 私有、跨用例共享的定名文件。
    // 先断纯函数、再断 appSettings() 这个真实对象返回的 fileName()（先例：tests/test_mainwindow.cpp:93-99
    // 直测 SettingsManager 的落点）；只有后者能鉴别"实现忽略了 override"。
    EXPECT_TRUE(gis::gui::appSettingsPath().endsWith(QStringLiteral("injected.ini")))
        << "override 未生效：appSettingsPath()=" << gis::gui::appSettingsPath().toStdString();
    EXPECT_EQ(gis::gui::appSettings().fileName(), injected)
        << "落点必须逐字等于注入路径（先例：tests/test_mainwindow.cpp:93-99 直测真实对象落点）"
        << " 实际=" << gis::gui::appSettings().fileName().toStdString();
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
