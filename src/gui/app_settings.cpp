#include "app_settings.h"

#include <QByteArray>
#include <QDir>
#include <QLatin1String>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

// ─────────────────────────────────────────────────────────────────────────────
// R-B2b 红线（写给下一个往这里加测试的人，他不会读到计划文档）：
//   **测试代码一律不得 qunsetenv("GIS_TOOL_SETTINGS_INI")**。
//   tests/test_main.cpp 在每个测试进程启动时都设了它；一旦清掉再调 appSettings()，路径就解析到
//   **真人机器上的** %LOCALAPPDATA%，而 migrateLegacySettings() 无条件 setValue(marker)+sync()
//   ⇒ 测试往用户目录写盘，还是跨用例共享的定名文件。
//   所以"断生产路径的形状"只能走纯函数 resolvedSettingsPath(QString())：不读环境变量、
//   不建 QSettings、不落盘。要换注入路径只允许 qputenv 改指到**另一个私有临时文件**。
// ─────────────────────────────────────────────────────────────────────────────
// 落点实测（2026-10-19 Tier-0 探针；原始三行输出与取证细节抄在任务报告里，这里只留结构性结论）：
//   旧落点一 QSettings("GISTool","GISTool")：
//     format = NativeFormat，fileName() = \HKEY_CURRENT_USER\Software\GISTool\GISTool，status = NoError。
//     它的**内容随机器与历史而异**：可能为空，也可能全是 offscreen GUI 用例留下的测试产物路径
//     （`gui_*_offscreen` 那条腿直接 spawn gis-gui.exe，链路上没人设 GIS_TOOL_SETTINGS_INI ⇒ 还是走
//     NativeFormat）。**不得对它做任何假设，也不得把某台机器上读到的内容/条数写进注释当证据。**
//     ⇒ 这条正是 R-B2 必须存在的原因：读它 = 把"开发者机器上碰巧有什么"当测试输入。
//     ⚠ 而且：**NativeFormat 的 QSettings 光是被构造出来就会在 HKCU 下把这一层键建出来**，
//       哪怕整个进程只读不写——一次性探针只做了构造 + fileName() + allKeys()（没有任何
//       setValue/sync），就把台账记录里"已删除且未重建"的这个键重建成了空键（取证见任务报告）。
//       ⇒ R-B2 那道 `if (settingsOverrideActive()) return 0;` 必须挡在**构造两个旧对象之前**：
//         它挡住的不只是"读到用户数据"，还有"在用户注册表里建键"这个副作用本身。
//   旧落点二 默认构造 QSettings()：
//     * 生产 GUI 进程（src/gui/main.cpp:36-37 设了 org/app = "GIS"/"GIS Tool"，且没人改
//       defaultFormat）⇒ NativeFormat，\HKEY_CURRENT_USER\Software\GIS\GIS Tool；
//       这个键是 welcome_dialog 历史上的写入点（src/gui/welcome_dialog.cpp:15 的 kFirstRunKey），
//       里面有什么随机器与历史而异（可能为空、可能有测试产物残留）⇒ 判据只能来自代码，
//       不能来自某台机器上的一次读数。
//       （读它要用等价的 QSettings("GIS","GIS Tool") 两参形式；生产里默认构造解析到的就是同一个位置。）
//     * 测试进程**不是** "落点无效(invalid)"：test_main.cpp 调了 setDefaultFormat(IniFormat) +
//       setPath(IniFormat, UserScope, <按 pid 分道的私有目录>)，org 名又没设，于是 Qt 6 回退用
//       可执行文件名 ⇒ fileName() = <私有目录>/Unknown Organization/gis_tests.ini，
//       status = AccessError，allKeys() 为空。
//       ⇒ 结论不变（导入在测试里从这个源读不到东西），但**理由要按这条写**：它被 harness 的
//         setPath 圈进了本进程私有目录，而不是"落点无效"。注意这只对源二成立，
//         源一是 NativeFormat，harness 的 setPath(IniFormat,…) 管不到它 ⇒ 唯一的闸还是 R-B2。
//   AppConfigLocation：测试进程 = %LOCALAPPDATA%\gis_tests（org 未设 ⇒ Qt 拿应用名顶上），
//     生产 GUI 进程 = %LOCALAPPDATA%\GIS\GIS Tool（R-B1 要的正是这个形状）。
//     ⇒ appSettings() 一律用**显式路径**构造：不受 org/app 名差异影响，也不受 test_main.cpp
//       那两条 setPath() 的全局副作用影响（R-B1 选它的理由）。
// ─────────────────────────────────────────────────────────────────────────────

namespace gis::gui {

namespace {

// R-B4 的实现本体。计数**必须在循环里自己累加**：K-21/自守断言纪律 = 任何"计数"判据不得由
// 两个集合大小的差推断（计划 Step 3 的 ⚠ 明确否掉了 `after - before` 那种写法）。
// WHY 一次 allKeys() 遍历就够、不需要按组递归：allKeys() 已经把组展开成**完整键路径**
// （磁盘/注册表里它是子键 app 下的一个值，allKeys() 给回的是 "app/<值名>" 这种带前缀的完整键，
//  而不是组名 "app" —— 判据来自 QSettings 的键展开规则，不来自某台机器上读到几条）；
// 反过来 beginGroup()/childKeys() 只看当前层，用它还得自己重建前缀。
void copyMissingInto(const QSettings& from, QSettings& to, int& copied) {
    const QStringList fromKeys = from.allKeys();
    for (const QString& key : fromKeys) {
        if (to.contains(key))
            continue;  // R-B4：补缺不覆盖
        to.setValue(key, from.value(key));
        ++copied;
    }
}

} // namespace

int copyMissingKeys(const QSettings& from, QSettings& to) {
    int copied = 0;
    copyMissingInto(from, to, copied);
    return copied;
}

// ─────────────────────────────────────────────────────────────────────────────
// R-B7：生产接线本体。label 与工厂由**同一次调用的同一批参数**展开，
// 所以"用例断 label"断的就是"生产会拿什么参数去构造旧落点"，两者不会各说各话；
// 从这张表里摘掉一条 = 少一个源 = 用例红（见 tests/test_app_settings.cpp 的 AppSettingsLegacySources）。
// 这里不出现任何注册表路径字面量（R-B3）：落点由 QSettings 对象自己按 org/app 解析。
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// 旧落点一：SettingsManager 历史用的两参构造 ⇒ Windows 上是 NativeFormat。
LegacySettingsSource legacySourceFromNames(const QString& organization, const QString& application) {
    return LegacySettingsSource{
        QStringLiteral("QSettings(%1, %2)").arg(organization, application),
        [organization, application] { return std::make_unique<QSettings>(organization, application); }};
}

// 旧落点二：welcome_dialog 历史用的默认构造 ⇒ 跟随进程的 org/app。
LegacySettingsSource legacyDefaultSource() {
    return LegacySettingsSource{QStringLiteral("QSettings()"),
                                [] { return std::make_unique<QSettings>(); }};
}

} // namespace

const QList<LegacySettingsSource>& productionLegacySources() {
    static const QList<LegacySettingsSource> sources{
        legacySourceFromNames(QStringLiteral("GISTool"), QStringLiteral("GISTool")),
        legacyDefaultSource(),
    };
    return sources;
}

int migrateLegacySettingsFrom(QSettings& target, const QList<LegacySettingsSource>& sources) {
    // R-B4：一次性靠标记，不靠运气 —— 标记检查放在 From 这一层，所以生产入口和用例走同一条幂等路径。
    // 标记写在 ini 里而不是注册表里（R-B5：卸载器不删 HKCU，标记留在 ini 才安全）。
    if (target.contains(QLatin1String(kMigrationMarkerKey)))
        return 0;
    int copied = 0;
    for (const LegacySettingsSource& source : sources) {
        // 一条源没接上（工厂为空 / 工厂返回空指针）就跳过它：一次启动不该因为一条断源而崩，
        // 也不该因此不再读后面的源。
        const std::unique_ptr<QSettings> legacy = source.open ? source.open() : nullptr;
        if (!legacy)
            continue;
        copied += copyMissingKeys(*legacy, target);
    }
    target.setValue(QLatin1String(kMigrationMarkerKey), true);
    target.sync();
    return copied;
}

bool settingsOverrideActive() {
    return !qgetenv("GIS_TOOL_SETTINGS_INI").isEmpty();
}

QString resolvedSettingsPath(const QString& overrideValue) {
    if (!overrideValue.isEmpty())
        return overrideValue;  // R-B2：注入态优先级最高
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(dir).filePath(QStringLiteral("settings.ini"));
}

QString appSettingsPath() {
    // 与 SettingsManager(N100) 同法用 fromLocal8Bit：MSVC 下 qgetenv 拿到的是本地码页字节，不是 UTF-8。
    const QByteArray rawOverride = qgetenv("GIS_TOOL_SETTINGS_INI");
    return resolvedSettingsPath(QString::fromLocal8Bit(rawOverride.constData()));
}

int migrateLegacySettings(QSettings& target) {
    // R-B2：注入态（测试/CI）一律不读真实注册表 —— 否则测试结果依赖开发者机器状态，
    // 而且导入会把用户数据写进测试 ini。这条 return 0 是本仓不碰 HKCU 的唯一保证。
    // 门控**必须留在生产入口这一层、且在拿到工厂之前**：NativeFormat 的 QSettings 光构造就会在
    // HKCU 下建键 ⇒ 挡住的既是"读到用户数据"也是"建键"这个副作用本身。
    if (settingsOverrideActive())
        return 0;
    return migrateLegacySettingsFrom(target, productionLegacySources());
}

QSettings appSettings() {
    const QString path = appSettingsPath();
    // Qt 6 的 QSettings 是 Q_DISABLE_COPY 的（QtCore/qsettings.h 里 QSettings 类的 private 段），
    // 所以**不能** `return settings;`（那是具名局部量的 NRVO，C++17 不保证消除、要求可移动）。
    // 这里先具名打开→跑一次性导入→sync 落盘，最后返回一个 **prvalue**：C++17 保证的返回值
    // 初始化把 prvalue 直接构造进调用方的对象，全程不发生拷贝。
    // 第二个对象的可见性来自"同进程内两个同 spec（= 同一个 fileName）的对象共用同一份
    // QSettingsPrivate 缓存"，**不是**来自下面那句 sync() ⇒ 别把它读成"sync 后另一个对象才看得见"
    // 的普适规律。bootstrap.sync() 负责的是内存→磁盘那一段（跨进程、进程被强杀时才有意义）；
    // 本模块现在没有能鉴别它的用例（注入实验见任务报告 F-4），保留它是为了落盘窗口，不是为了让测试红。
    QSettings bootstrap(path, QSettings::IniFormat);
    migrateLegacySettings(bootstrap);
    bootstrap.sync();
    return QSettings(path, QSettings::IniFormat);
}

} // namespace gis::gui
