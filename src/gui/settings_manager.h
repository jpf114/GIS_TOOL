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

signals:
    void recentFilesChanged();

private:
    SettingsManager(QObject* parent = nullptr);
    QSettings settings_;
    static constexpr int kMaxRecentFiles = 10;
};
