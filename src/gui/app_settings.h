#pragma once
#include <QList>
#include <QSettings>
#include <QString>

#include <functional>
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
// 工厂返回 std::unique_ptr<QSettings>：Qt 6 的 QSettings 在 qsettings.h:184 是 Q_DISABLE_COPY
// ⇒ 既不可拷贝也不可移动。按值返回只有"一条 return + prvalue"那一条路（写成具名局部量就编不过），
// 而"往调用方给的引用上原地赋值/swap"两条都不通（赋值是 deleted，Qt 6.11 的 QSettings 没有 swap）
// ⇒ 把所有权放堆上才能让任何写法都走得通（三版最小编译探针的输出见 B3a 报告）。
struct LegacySettingsSource {
    // 身份字符串：由**构造这条源所用的同一批参数**展开（见 app_settings.cpp 的 legacySourceFromNames），
    // 所以用例断言 label 就是在断言生产接线本身，而不是断言另一份手写的描述。
    // label 不要求唯一，也不参与任何逻辑 ⇒ 打开源之前就能读它。
    QString label;
    std::function<std::unique_ptr<QSettings>()> open;
};

// 生产接线本体：旧设置真实存在的那两个落点。
// ⚠ 用例只许读 label / 个数，**不得调用 open()**：Windows 上这两个源都是 NativeFormat，
//   光构造就会在 HKCU 下把键建出来（落点实测见 app_settings.cpp 顶部）。
const QList<LegacySettingsSource>& productionLegacySources();

// 逐条打开 sources、按 R-B4 "补缺不覆盖"导入，最后打一次性标记。
// 与 migrateLegacySettings() 的区别只在**源从哪来**：这里由参数给，所以测试能喂私有 ini。
// R-B2 的门控**不在**这一层（否则任何用例都观察不到迁移内容）。
int migrateLegacySettingsFrom(QSettings& target, const QList<LegacySettingsSource>& sources);

// 生产入口 = R-B2 门控 + migrateLegacySettingsFrom(target, productionLegacySources())。
// ⚠ 这一行只许"整张表交出去"：门控在任何测试进程里都先 return 0，所以**调用点**没有任何用例能观察到
//   （想观察就得构造 NativeFormat 源 ⇒ 在 HKCU 建键）。判据只能钉在 productionLegacySources() 那张表上，
//   于是"少一个源"必须表现为"表里少一条"，不能表现为这里少交一条。
int migrateLegacySettings(QSettings& target);

} // namespace gis::gui
