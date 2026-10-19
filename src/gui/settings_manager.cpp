#include "settings_manager.h"

#include "app_settings.h"

SettingsManager& SettingsManager::instance() {
    static SettingsManager inst;
    return inst;
}

// 落点与迁移规则见 src/gui/app_settings.cpp 的 R-B1/R-B2/R-B4。
// 这里只保留一条本地纪律：settings_ 必须由 appSettings() 的 prvalue **直接初始化**——
// Qt 6 的 QSettings 是 Q_DISABLE_COPY 的，先存具名局部量再拷贝/赋值会直接编译失败。
SettingsManager::SettingsManager(QObject* parent)
    : QObject(parent), settings_(gis::gui::appSettings()) {}

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
