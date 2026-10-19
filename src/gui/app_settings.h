#pragma once
#include <QSettings>
#include <QString>

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
int migrateLegacySettings(QSettings& target);

} // namespace gis::gui
