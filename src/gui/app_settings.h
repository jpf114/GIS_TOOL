#pragma once
#include <QList>
#include <QSettings>
#include <QString>

#include <memory>

namespace gis::gui {

// 批次 B：用户设置的唯一落点。生产 = AppConfigLocation 下的 settings.ini（R-B1），
// 测试/CI = GIS_TOOL_SETTINGS_INI 指定的文件（R-B2，优先级最高）。
QSettings appSettings();
QString appSettingsPath();
bool settingsOverrideActive();

// 纯函数：overrideValue 非空 ⇒ 原样返回；为空 ⇒ 返回 AppConfigLocation 下的 settings.ini。
// 不读环境变量、不建 QSettings 对象、不落盘（存在的理由见 app_settings.cpp 里的 R-B2b 红线）。
QString resolvedSettingsPath(const QString& overrideValue);

// 一次性导入的机器级门控键（R-B4）。
inline constexpr const char* kMigrationMarkerKey = "meta/migrated_from_registry";

// 只复制 to 里不存在的键；返回复制条数。纯函数：不碰注册表、不读环境变量。
int copyMissingKeys(const QSettings& from, QSettings& to);

// R-B7：旧落点做成可注入的源，"迁移到底搬动了什么"因此第一次拥有不依赖开发者机器的判据。
//
// (kind, first, second) 是这份身份的**唯一存储**：label() 与 open() 都由同一份存储推导，
// 头文件与实现里都不再有手写的身份字符串、不再有工厂闭包。
// WHY 锁"构造形态"而不只锁参数（B3c/Q1）：同一批参数在不同形态下落到的介质完全不同 ——
//   两参 QSettings(organization, application) 在 Windows 上就是 NativeFormat，既不吃
//   setDefaultFormat() 也不吃 setPath()（tests/test_mainwindow.cpp:59-62 为此踩过一次）。
//   工厂是闭包、label 是另一份手写格式串时，把工厂换成四参 QSettings(IniFormat, UserScope, org, app)
//   一字不动 label ⇒ 接线用例照绿（这个洞在 HEAD 上实测复现，见 B3c 报告的 INJ-Q1-0）。
//   现在改形态就得改 Kind ⇒ label 必变 ⇒ 用例红。
//
// open() 返回 std::unique_ptr<QSettings>，不是 QSettings、也不是 std::optional<QSettings>：
// Qt 6 的 QSettings 在 qsettings.h:184 是 Q_DISABLE_COPY ⇒ 既不可拷贝也不可移动。四版最小编译探针
// （输出见 B3c 报告 §1）把可选形状收窄到只剩这一个：
//   * 具名局部量再包一层 unique_ptr ⇒ C2280；std::optional<QSettings> ⇒ C2440（没有可用转换）；
//   * 按值返回 prvalue **编得过**，但它要求每个 arm 写成一条 return 表达式，且表达不了"这条源没接上"；
//   * 只有把所有权放堆上，"原地读法"与"没接上"两种形态才都能表达。
struct LegacySettingsSource {
    // QSettings 的构造形态：Kind 决定"打开的是哪种介质、走的是哪个构造重载"，
    // 参数决定"那个落点叫什么"。两者合起来就是这条源的全部身份。
    enum class Kind {
        NamedOrgApp,     // QSettings(organization, application)：两参 ⇒ Windows 上即 NativeFormat
        ProcessDefault,  // QSettings()：跟随进程的 org/app
        IniPath,         // QSettings(path, QSettings::IniFormat)：用例注入面用的私有文件
        Unwired,         // 表里有这一条，但它没接上任何落点 ⇒ open() 给回空指针
    };
    // 默认值取 Unwired = 失败关闭：一条没填 Kind 的源不该在下次启动时悄悄打开某个真实落点。
    Kind kind = Kind::Unwired;
    QString first;   // NamedOrgApp: organization；IniPath: ini 路径；其余 Kind 忽略
    QString second;  // NamedOrgApp: application；其余 Kind 忽略

    // 身份：由 kind + first/second 推导，不读任何外部状态 ⇒ 打开源之前就能读它。
    // label 不要求唯一，也不参与任何逻辑（它只是那份存储的可读投影）。
    QString label() const;

    // 按 kind 构造真正的 QSettings。
    // ⚠ 用例**不得**打开 NamedOrgApp / ProcessDefault 这两条：它们在 Windows 上是注册表落点，
    //   光构造就会在 HKCU 下把键建出来（实测见 app_settings.cpp 顶部）。
    //   这条禁令的执行点在 open() 里那两个 arm 的**第一行**（Q_ASSERT_X），不在本注释里 ——
    //   禁令写在声明处只能防住读了它的人，写在风险处才防住没读它的人（B3c/Q4）。
    std::unique_ptr<QSettings> open() const;
};

// 生产接线本体：旧设置真实存在的那两个落点。
// 只读这张表**绝无副作用**：条目是 (kind, 参数) 的数据，构造 QList 不会构造任何 QSettings
// ——这正是"断接线而不碰注册表"的立论前提（B3a 复审确认过，别把它改回需要构造才能表达身份的形态）。
const QList<LegacySettingsSource>& productionLegacySources();

// 逐条打开 sources、按 R-B4 "补缺不覆盖"导入；**至少打开成功一条**才打一次性标记（B3c/Q3）。
// WHY 标记要以"读到过源"为条件：标记的语义是"旧落点已被消费，别再导入"，而一条都没打开成功时
// 对旧落点一无所知，照打等于把一次性迁移机会永久烧掉、还烧得毫无痕迹（下次直接早退）。
// 反过来"旧落点是空的"**不算**失败（open() 成功、只是读到 0 条键）⇒ 标记照打，
// 所以这条改动不会退化成"每次启动都重跑迁移"。
// 与 migrateLegacySettings() 的区别只在**源从哪来**：这里由参数给，所以测试能喂私有 ini。
// R-B2 的门控**不在**这一层（否则任何用例都观察不到迁移内容）。
int migrateLegacySettingsFrom(QSettings& target, const QList<LegacySettingsSource>& sources);

// R-B2 门控**本体**（B3c/Q2）：overrideActive=true ⇒ 一条源都不打开。
// 挡住的既是"读到用户数据"，也是"构造 NativeFormat 对象就在 HKCU 建键"这个副作用本身。
// WHY 做成参数而不是直接读环境变量：门控原来是 `if (settingsOverrideActive()) return 0;` 一行，
// 而任何测试进程里该函数都为真 ⇒ 这一行的**效果**没有用例能观察（要观察就得 qunsetenv ⇒ 违反 R-B2b）。
// 参数化后"门控挡得住一条**确有数据**的源"这一臂可以用私有 ini 源跑 true/false 两侧。
// ⚠ 硬边界，不许写成"已闭"：它**仍然看不见调用点**。migrateLegacySettings() 在测试进程里无论交出
//   什么表都只会返回 0 ⇒ "调用点只交第一条源"或"把 overrideActive 写死成 true"这类错误依旧无人看守；
//   能红它的用例必须先把门控打开，而打开门控就等于构造 NativeFormat 源（HKCU 建键）。
//   本项的效果只是把无人看守的面从"门控 + 接线"收窄成"一句必须原样委托"。
int migrateLegacySettingsGated(QSettings& target, const QList<LegacySettingsSource>& sources,
                               bool overrideActive);

// 生产入口 = migrateLegacySettingsGated(target, productionLegacySources(), settingsOverrideActive())。
// ⚠ 这一行只许"整张表 + 真门控"原样交出去：不含列表运算（.at()/.first()/.mid()），不把 overrideActive
//   写成常量。判据只能钉在 productionLegacySources() 那张表和 Gated 的两臂上，钉不到这里
//   （为什么钉不到：见上面 migrateLegacySettingsGated 的硬边界）。
int migrateLegacySettings(QSettings& target);

} // namespace gis::gui
