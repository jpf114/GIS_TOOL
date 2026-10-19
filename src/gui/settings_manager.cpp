#include "settings_manager.h"

#include <QByteArray>

SettingsManager& SettingsManager::instance() {
    static SettingsManager inst;
    return inst;
}

// N100：测试必须能在**不碰真实用户注册表**的前提下跑完 SettingsManager 的全部行为。
// WHY 用环境变量而不是改默认存储位置：两参 QSettings("GISTool","GISTool") 在 Windows 上走
//   NativeFormat ⇒ 落 HKCU\Software\GISTool（实测盘上已有 10 条测试产物路径写在 recent/files 里，
//   因为 MainWindowTest 每次都调 instance()）。直接把它改成 IniFormat 会**改变生产行为的存储位置**
//   ——老用户存在注册表里的"最近文件/路径"会凭空看不到。
//   所以默认分支一字不改，只在显式设了 GIS_TOOL_SETTINGS_INI（测试进程由 tests/test_main.cpp 设）时
//   走那个文件，测试与生产的落点从此可分别验证（见 SettingsManager::storagePath()）。
SettingsManager::SettingsManager(QObject* parent)
    : QObject(parent),
      settings_([]() {
          const QByteArray rawOverride = qgetenv("GIS_TOOL_SETTINGS_INI");
          if (!rawOverride.isEmpty()) {
              return QSettings(QString::fromLocal8Bit(rawOverride.constData()),
                               QSettings::IniFormat);
          }
          return QSettings(QStringLiteral("GISTool"), QStringLiteral("GISTool"));
      }()) {}

QString SettingsManager::storagePath() const {
    return settings_.fileName();
}

QString SettingsManager::lastInputDirectory() const {
    return settings_.value(QStringLiteral("paths/lastInputDir")).toString();
}

void SettingsManager::setLastInputDirectory(const QString& dir) {
    settings_.setValue(QStringLiteral("paths/lastInputDir"), dir);
}

QString SettingsManager::lastOutputDirectory() const {
    return settings_.value(QStringLiteral("paths/lastOutputDir")).toString();
}

void SettingsManager::setLastOutputDirectory(const QString& dir) {
    settings_.setValue(QStringLiteral("paths/lastOutputDir"), dir);
}

QStringList SettingsManager::recentFiles() const {
    return settings_.value(QStringLiteral("recent/files")).toStringList();
}

void SettingsManager::addRecentFile(const QString& path) {
    auto files = recentFiles();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > kMaxRecentFiles)
        files.removeLast();
    settings_.setValue(QStringLiteral("recent/files"), files);
    emit recentFilesChanged();
}

void SettingsManager::clearRecentFiles() {
    settings_.remove(QStringLiteral("recent/files"));
    emit recentFilesChanged();
}

QString SettingsManager::defaultOutputSrs() const {
    return settings_.value(QStringLiteral("defaults/outputSrs")).toString();
}

void SettingsManager::setDefaultOutputSrs(const QString& srs) {
    settings_.setValue(QStringLiteral("defaults/outputSrs"), srs);
}

int SettingsManager::maxRecentFiles() const {
    return kMaxRecentFiles;
}
