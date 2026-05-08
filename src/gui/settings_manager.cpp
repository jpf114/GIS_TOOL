#include "settings_manager.h"

SettingsManager& SettingsManager::instance() {
    static SettingsManager inst;
    return inst;
}

SettingsManager::SettingsManager(QObject* parent)
    : QObject(parent),
      settings_(QStringLiteral("GISTool"), QStringLiteral("GISTool")) {}

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
