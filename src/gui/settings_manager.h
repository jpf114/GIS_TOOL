#pragma once
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

class SettingsManager : public QObject {
    Q_OBJECT
public:
    static SettingsManager& instance();

    QString lastInputDirectory() const;
    void setLastInputDirectory(const QString& dir);

    QString lastOutputDirectory() const;
    void setLastOutputDirectory(const QString& dir);

    QStringList recentFiles() const;
    void addRecentFile(const QString& path);
    void clearRecentFiles();

    QString defaultOutputSrs() const;
    void setDefaultOutputSrs(const QString& srs);

    int maxRecentFiles() const;

    // 本实例实际落盘的存储位置（批次 B 之后恒为 appSettings() 解析出的 ini 文件路径）。
    // 存在的理由：测试必须能证明"我没写进真实用户状态"（N100），而不是靠约定。
    QString storagePath() const;

signals:
    void recentFilesChanged();

private:
    SettingsManager(QObject* parent = nullptr);
    QSettings settings_;
    static constexpr int kMaxRecentFiles = 10;
};
