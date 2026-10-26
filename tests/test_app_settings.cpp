// 批次 B / Task B1：gis::gui::appSettings() 单一落点 + "补缺不覆盖"的旧设置导入。
//
// R-B2b 红线（本文件与任何测试代码都适用）：**一律不得 qunsetenv("GIS_TOOL_SETTINGS_INI")**。
//   tests/test_main.cpp 在每个测试进程里都设了这个变量；一旦清掉，路径就解析成
//   **真人机器上的** %LOCALAPPDATA%，而 migrateLegacySettings() 会 setValue(marker)+sync()
//   ⇒ 测试往用户目录写盘。要断"生产路径形状"只能走纯函数 resolvedSettingsPath(QString())。
//
// Qt 6 事实（构树实测 + B3a 的三版 / B3c 的四版最小编译探针，决定了下面 helper 与源的形状）：
//   qsettings.h:184 有 Q_DISABLE_COPY(QSettings) ⇒ QSettings **既不可拷贝也不可移动**，于是
//     * 具名局部量再包一层 unique_ptr ⇒ C2280；std::optional<QSettings> ⇒ C2440（没有可用转换）；
//     * 按值返回 **prvalue** 编得过（appSettings() 用的就是它），但要求每个 arm 写成一条 return
//       表达式，且表达不了"这条源没接上"；
//     * 只有把所有权放堆上（std::unique_ptr<QSettings>），"读法自由"与"没接上"才都能表达
//       —— 所以下面的注入源是 (kind, 参数) 的数据，open() 由它推导并返回 unique_ptr。
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

using Source = gis::gui::LegacySettingsSource;
using Kind = gis::gui::LegacySettingsSource::Kind;

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

// 一条"由私有 ini 后备"的旧落点源（R-B7 的注入面）：只给 Kind + 路径，label 与构造由生产实现
// 的同一份推导给出 ⇒ 测试侧不写任何身份字符串，写不出"label 说 A、构造做 B"的源（B3c/Q1）。
Source iniSource(const QString& path) {
    return Source{Kind::IniPath, path, QString()};
}

// 一条"表里有、但没接上任何落点"的源：错接线臂的形状。
// 为什么不再用"把 std::function 置空 / 让它返回空指针"表达：那要求源自己带一个可被替换的工厂，
// 而那份工厂正是 Q1 要拿掉的第二个存储（工厂与 label 各说各话）。Kind::Unwired 让"没接上"
// 回到同一份存储里，open() 仍然给回空指针 ⇒ 迁移要跳过的行为一点没变。
Source unwiredSource() {
    return Source{Kind::Unwired, QString(), QString()};
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
// 也不会去读它（migrateLegacySettingsFrom 不含 R-B2 门控；门控本体在 migrateLegacySettingsGated，
// 它的两臂在下面 GateActiveSkipsSourceThatReallyHasData / GateInactiveImportsThatSameSource 两条里跑）。
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

// 一条源没接上（Kind::Unwired ⇒ open() 给回空指针）时不得把整次启动崩掉，也不得顶掉其余好源。
TEST(AppSettingsMigration, SkipsMiswiredSourcesWithoutAborting) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString legacyPath =
        writeIni(dir, QStringLiteral("old.ini"), {{"defaults/outputSrs", "EPSG:3857"}});
    QSettings target(dir.filePath(QStringLiteral("target.ini")), QSettings::IniFormat);

    EXPECT_EQ(gis::gui::migrateLegacySettingsFrom(
                  target, {unwiredSource(), unwiredSource(), iniSource(legacyPath)}),
              1)
        << "断掉的两条既不能崩，也不能顶掉第三条好源";
    EXPECT_EQ(target.value(QLatin1String("defaults/outputSrs")).toString(), "EPSG:3857");
    // 好源打开成功过 ⇒ 标记必须照打（"有源没接上"不等于"一无所知"，见 AllBrokenSources…那条）。
    EXPECT_TRUE(target.contains(QLatin1String(gis::gui::kMigrationMarkerKey)));
}

// B3c/Q3：全断源（一条都没打开成功）时**不得**打一次性标记。
// WHY 这条判据值得单独存在：标记的语义是"旧落点已被消费、别再导入"，而一无所知的启动照打标记
// = 把一次性迁移机会永久烧掉，且烧得毫无痕迹（下次直接早退，用户那份旧设置再也进不来）。
// 第二个作用域是"烧掉之后再也救不回来"的正面表达：如果第一次把标记打了，这里必然读到 0。
TEST(AppSettingsMigration, AllBrokenSourcesLeaveTheMarkerUnstamped) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString targetPath = dir.filePath(QStringLiteral("target.ini"));
    const QString legacyPath =
        writeIni(dir, QStringLiteral("old.ini"), {{"defaults/outputSrs", "EPSG:4326"}});
    {
        QSettings target(targetPath, QSettings::IniFormat);
        EXPECT_EQ(gis::gui::migrateLegacySettingsFrom(target, {unwiredSource(), unwiredSource()}), 0)
            << "一条都没打开成功 ⇒ 不该有任何复制";
        EXPECT_FALSE(target.contains(QLatin1String(gis::gui::kMigrationMarkerKey)))
            << "全断源的启动照打标记 ⇒ 迁移机会被永久烧掉（B3c/Q3 要消灭的就是这个）";
    }
    {   // 同一条目标、同一条源：这次接上了 ⇒ 必须真的搬进来（标记没被上一次烧掉）。
        QSettings target(targetPath, QSettings::IniFormat);
        EXPECT_EQ(gis::gui::migrateLegacySettingsFrom(target, {iniSource(legacyPath)}), 1)
            << "上一次全断源不得烧掉这次迁移：旧键必须还能进来";
        EXPECT_EQ(target.value(QLatin1String("defaults/outputSrs")).toString(), "EPSG:4326");
        EXPECT_TRUE(target.contains(QLatin1String(gis::gui::kMigrationMarkerKey)))
            << "读到过源之后才打标记（与 From 的意图一致）";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// B3c/Q2：门控参数化之后才有的两臂 —— "门控挡得住一条**确有数据**的源"。
// 原来门控读的是环境变量，而任何测试进程里 GIS_TOOL_SETTINGS_INI 都为真（tests/test_main.cpp）
// 且不得 qunsetenv（R-B2b）⇒ 门控的**效果**在过去没有任何用例能观察。
// 注意这两臂喂的是同一条带数据的源：只有"true 挡不住 / false 也不导"这种实现才同时红两条。
TEST(AppSettingsMigration, GateActiveSkipsSourceThatReallyHasData) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString legacyPath = writeIni(dir, QStringLiteral("old.ini"),
                                        {{"app/first_run_complete", "true"},
                                         {"defaults/outputSrs", "EPSG:4326"}});
    QSettings target(dir.filePath(QStringLiteral("target.ini")), QSettings::IniFormat);

    EXPECT_EQ(gis::gui::migrateLegacySettingsGated(target, {iniSource(legacyPath)}, true), 0)
        << "overrideActive=true ⇒ 一条源都不许打开（既挡读用户数据，也挡构造 NativeFormat 的建键副作用）";
    EXPECT_FALSE(target.contains(QLatin1String("defaults/outputSrs")))
        << "门控没挡住确有数据的源 ⇒ 注入态会把开发者机器上的东西搬进测试 ini";
    EXPECT_FALSE(target.contains(QLatin1String(gis::gui::kMigrationMarkerKey)))
        << "被门控挡住的启动不得打一次性标记（标记归 From 那层，见头文件意图）";
}

TEST(AppSettingsMigration, GateInactiveImportsThatSameSource) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString legacyPath = writeIni(dir, QStringLiteral("old.ini"),
                                        {{"app/first_run_complete", "true"},
                                         {"defaults/outputSrs", "EPSG:4326"}});
    QSettings target(dir.filePath(QStringLiteral("target.ini")), QSettings::IniFormat);

    // 与上一条唯一的差别就是 overrideActive=false ⇒ 两条合起来才把"门控不是恒返回 0"钉住。
    EXPECT_EQ(gis::gui::migrateLegacySettingsGated(target, {iniSource(legacyPath)}, false), 2)
        << "overrideActive=false ⇒ 必须原样落到 From";
    EXPECT_EQ(target.value(QLatin1String("defaults/outputSrs")).toString(), "EPSG:4326");
    EXPECT_TRUE(target.contains(QLatin1String(gis::gui::kMigrationMarkerKey)));
}

// 生产接线本体：migrateLegacySettings() 只从这里取源，所以摘掉一条 ⇒ 这条红，
// 而不是"迁移少搬了一个落点却没有任何用例知道"（R-B7 判别性验收要的就是这一条）。
// ⚠ 这里**不调用 open()**：两个生产源在 Windows 上都是 NativeFormat，光构造就会在 HKCU 下建键
//   （落点实测见 src/gui/app_settings.cpp 顶部）⇒ 判据只能取"身份数据"这种不构造的形态；
//   读 label() 是安全的（它只投影 (kind, 参数) 这份存储，不构造任何东西）。
//   注入态下 open() 现在会先撞上 B3d 的运行期拒绝，但那条拒绝**不记在本用例的账上**：
//   它由 ProductionArmsRefuseToOpenWhileOverrideActive 专门判 ⇒ 别把两条读成同一条判据。
// ⚠ 这条钉的是**表**，不是 migrateLegacySettings() 那一行的调用点：R-B2 门控在任何测试进程里都先
//   return 0，所以"调用点只交一条源"这种写法没有任何用例能观察到（两次注入都复现过，登记为已知判据缺口）。
//   想观察就得让用例真去构造 NativeFormat 源 ⇒ 读用户注册表当测试输入 + 在 HKCU 建键，两条都撞红线。
TEST(AppSettingsLegacySources, ProductionWiringIsTheTwoLegacyLandingPoints) {
    const QList<Source>& sources = gis::gui::productionLegacySources();
    QStringList labels;
    for (const Source& source : sources)
        labels << source.label();
    EXPECT_EQ(labels, (QStringList{QStringLiteral("QSettings(organization=GISTool, application=GISTool)"),
                                   QStringLiteral("QSettings()")}))
        << "生产旧落点的身份与条数就是迁移的输入面；label 由 (kind, 参数) 推导，断它=断接线"
        << " 实际=" << labels.join(QStringLiteral(" | ")).toStdString();
    // label 之外再直接断一次存储本身：label 的推导规则由 LabelPinsTheConstructionKind… 钉，
    // 这里钉的是"生产表里就是这两条"，两者缺一就能被"改推导"或"改表"绕过。
    ASSERT_EQ(sources.size(), 2);
    EXPECT_EQ(sources[0].kind, Kind::NamedOrgApp) << "源一：" << sources[0].label().toStdString();
    EXPECT_EQ(sources[0].first, QStringLiteral("GISTool"));
    EXPECT_EQ(sources[0].second, QStringLiteral("GISTool"));
    EXPECT_EQ(sources[1].kind, Kind::ProcessDefault) << "源二：" << sources[1].label().toStdString();
}

// B3c/Q1：label 必须锁住**构造形态**，不能只锁参数。
// 四份源共用同一批参数 ("GISTool","GISTool")，只有 Kind 不同 ⇒ 四条 label 必须两两不同；
// 任何"label 由参数展开、形态另存在别处"的写法（HEAD 的形状）在这里立刻红。
// 为什么测试里可以构造这些源：LegacySettingsSource 是 (kind, 参数) 的数据，
// 构造它**不会**构造 QSettings ⇒ 读 label()/比 kind 都不碰注册表。
TEST(AppSettingsLegacySources, LabelPinsTheConstructionKindNotJustTheArguments) {
    const Source named{Kind::NamedOrgApp, QStringLiteral("GISTool"), QStringLiteral("GISTool")};
    const Source ini{Kind::IniPath, QStringLiteral("GISTool"), QStringLiteral("GISTool")};
    const Source processDefault{Kind::ProcessDefault, QStringLiteral("GISTool"),
                                QStringLiteral("GISTool")};
    const Source unwired{Kind::Unwired, QStringLiteral("GISTool"), QStringLiteral("GISTool")};
    EXPECT_EQ(named.label(), QStringLiteral("QSettings(organization=GISTool, application=GISTool)"));
    EXPECT_EQ(ini.label(), QStringLiteral("QSettings(path=GISTool, QSettings::IniFormat)"));
    EXPECT_EQ(processDefault.label(), QStringLiteral("QSettings()"));
    EXPECT_EQ(unwired.label(), QStringLiteral("QSettings(<unwired>)"));
    EXPECT_NE(named.label(), ini.label()) << "同一批参数、不同构造形态 ⇒ 身份必须不同，否则改形态没人看得见";
    EXPECT_NE(named.label(), processDefault.label());
    EXPECT_NE(named.label(), unwired.label());
    // 默认构造取 Unwired = 失败关闭：漏填 Kind 的源不打开任何落点，而不是猜一个。
    EXPECT_EQ(Source{}.kind, Kind::Unwired);
}

// open() 的推导里唯一能被**安全构造**的形态（私有 ini）必须真的落到 label 说的那个文件上：
// 这条把"Kind 说 IniFormat、构造却做了别的"这一类错误按在可测的那一侧。
// ⚠ 覆盖面是有意不对称的：NamedOrgApp / ProcessDefault 两条 arm 的**构造结果**不可测
//   （一构造就在 HKCU 建键），能测的只有"注入态下它拒绝构造"——那条判据在下面的
//   ProductionArmsRefuseToOpenWhileOverrideActive；两条臂各自的差别登记在任务报告里（G2 型缺口）。
TEST(AppSettingsLegacySources, IniPathSourceOpensTheFileItsLabelNames) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString legacyPath =
        writeIni(dir, QStringLiteral("old.ini"), {{"defaults/outputSrs", "EPSG:4326"}});
    const Source source = iniSource(legacyPath);
    const std::unique_ptr<QSettings> opened = source.open();
    ASSERT_TRUE(opened != nullptr);
    EXPECT_EQ(QDir::toNativeSeparators(opened->fileName()), QDir::toNativeSeparators(legacyPath))
        << "open() 的落点必须逐字等于 label 里那个路径（同一份存储的两个读者不能各说各话）";
    EXPECT_EQ(opened->format(), QSettings::IniFormat) << "Kind::IniPath 必须走显式 IniFormat";
    EXPECT_TRUE(opened->allKeys().contains(QLatin1String("defaults/outputSrs")));
    EXPECT_TRUE(source.label().contains(QFileInfo(legacyPath).fileName()))
        << " 实际 label=" << source.label().toStdString();
    // 没接上的那条必须给回空指针（迁移靠它跳过断源，见 SkipsMiswiredSourcesWithoutAborting）。
    EXPECT_TRUE(unwiredSource().open() == nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// B3d：**禁令本身**第一次拥有用例 —— 两条生产臂在注入态下 open() 必须给回空指针。
// WHY 这条可以安全地调用生产源的 open()：拒绝写在 QSettings 构造**之前** ⇒ 既不读用户数据，
//   也不会在 HKCU 下建键，所以"证明它被拒"不再需要先污染才看得见。
//   形态为什么从断言换成拒绝：B3c/Q4 用的 Q_ASSERT_X 被 QT_NO_DEBUG 编掉，而交付与 CI 都跑 Release
//   （取证口径=扫两棵配置树的产物，不是读源码）⇒ 禁令在真正跑起来的那棵配置树里没有牙。
// 判别性（验收口径）：摘掉 app_settings.cpp 两个 arm 里那句 `if (settingsOverrideActive())` 的早退
//   ⇒ 本用例红在循环体的 EXPECT_TRUE 上（源被真构造出来 ⇒ 指针非空；NamedOrgApp 那条还会顺手建键）。
// 失效形态与 open() 的注释同一口径：静默返回空**会藏住误调**，但 From 那层以"打开成功几条"为条件
//   才打一次性标记（B3c/Q3）⇒ 被全部拒绝的启动不写标记，状态可恢复、不永久烧掉迁移。
TEST(AppSettingsLegacySources, ProductionArmsRefuseToOpenWhileOverrideActive) {
    // 机器级前置（不是装饰性的恒真检查）：本用例的全部安全性来自"当前是注入态"。前置不成立时必须先
    // 红在这里、直接返回，而不是往下把两个 NativeFormat 落点构造出来。摘掉 tests/test_main.cpp 里
    // 那条 qputenv 就能让它红（既有同族判据：AppSettingsPath.TestHarnessAlwaysOverrides）。
    ASSERT_TRUE(gis::gui::settingsOverrideActive())
        << "注入态前置不成立 ⇒ 不许再往下走，往下就是真去构造生产落点";
    const QList<Source>& sources = gis::gui::productionLegacySources();
    ASSERT_EQ(sources.size(), 2) << "表的内容归 ProductionWiringIsTheTwoLegacyLandingPoints，这里只取那两条臂";
    for (const Source& source : sources) {
        EXPECT_TRUE(source.open() == nullptr)
            << "注入态下这条臂必须拒绝构造（禁令的执行点，B3d）：" << source.label().toStdString();
    }
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
    //     src/gui/app_settings.cpp 顶部"旧落点一"那段实测与任务报告的注册表 lastWriteTime）。
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

// ── C18 / N110 销账：健康新机器那一格（源打开成功但 0 键）此前只有注释在承诺，没有判据 ──────
// migrateLegacySettingsFrom 里那句"读到源但源里没键（健康的新机器）仍然算打开成功 ⇒ 标记照打，
// 所以这里不会退化成每次启动重跑迁移"（app_settings.cpp:180-182）之前两头都没用例沾：
//   AllBrokenSourcesLeaveTheMarkerUnstamped 判的是 opened==0 那一侧；
//   ImportsFromInjectedSources… 喂的是**有键**的源（copied>=1）。
// ⇒ `opened>=1 && copied==0` 这一格是空的，把 `if (opened == 0)` 改写成 `if (copied == 0)`
//   是一个**今天全绿**的存活变异，而它的用户可见后果是：每台新机器每次启动都重跑一遍迁移
//   （并且永远不打标记 ⇒ 那条一次性标记形同不存在）。
TEST(AppSettingsMigration, EmptyButOpenableSourceStillStampsTheMarker) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString legacyPath = writeIni(dir, QStringLiteral("old.ini"), {});  // 存在、可打开、0 键
    const QString targetPath = dir.filePath(QStringLiteral("target.ini"));
    {
        QSettings target(targetPath, QSettings::IniFormat);
        EXPECT_EQ(gis::gui::migrateLegacySettingsFrom(target, {iniSource(legacyPath)}), 0)
            << "空源不该搬来任何东西";
        EXPECT_TRUE(target.contains(QLatin1String(gis::gui::kMigrationMarkerKey)))
            << "打开成功但 0 键 = 健康的新机器 ⇒ 已经消费过就得打标记，否则每次启动重跑迁移";
        // 计划里"退出后只写预期键"那一格：这次启动除了标记以外不许留下任何键。
        EXPECT_EQ(target.allKeys(),
                  (QStringList{QString::fromLatin1(gis::gui::kMigrationMarkerKey)}))
            << "新机器第一次启动的用户配置里，除迁移标记外不该多出任何键";
    }
    {   // 标记已烧 ⇒ 第二次直接早退，且不再往用户配置里添东西。
        QSettings target(targetPath, QSettings::IniFormat);
        EXPECT_EQ(gis::gui::migrateLegacySettingsFrom(target, {iniSource(legacyPath)}), 0);
        EXPECT_EQ(target.allKeys().size(), 1)
            << "第二次启动不许再写键（早退路径也要守住'只写预期键'）";
    }
}

// 旧落点**根本不存在**（新机器上更常见的形态）必须与"存在但空"落在同一格：
// QSettings(IniFormat) 对不存在的路径照样构造成功、读到 0 键 ⇒ opened>=1、标记照打。
// 单独一条而不是并到上一条：如果哪天有人给 open() 加"文件不存在就返回空指针"的"体贴"逻辑，
// 这条会红，而并在一起的那条只会安静地换个分支跑过去。
TEST(AppSettingsMigration, MissingLegacyFileIsConsumedNotBroken) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString absent = dir.filePath(QStringLiteral("never_written.ini"));
    ASSERT_FALSE(QFileInfo::exists(absent));
    QSettings target(dir.filePath(QStringLiteral("target.ini")), QSettings::IniFormat);

    EXPECT_EQ(gis::gui::migrateLegacySettingsFrom(target, {iniSource(absent)}), 0);
    EXPECT_TRUE(target.contains(QLatin1String(gis::gui::kMigrationMarkerKey)))
        << "'文件不存在'与'文件在但空'是同一格；判成全断源就等于每次启动都重跑迁移";
}
