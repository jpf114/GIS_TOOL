#pragma once

#include <QIcon>
#include <QPixmap>
#include <QColor>
#include <string>
#include <map>

QT_BEGIN_NAMESPACE
class QSvgRenderer;
QT_END_NAMESPACE

namespace gis::gui {

enum class IconWeight {
    Regular,
    Bold
};

struct IconSpec {
    std::string svgName;
    IconWeight weight = IconWeight::Regular;
    QColor defaultColor{Qt::white};
};

class IconManager {
public:
    static IconManager& instance();

    void setIconsBasePath(const std::string& path);

    QIcon iconForPlugin(const std::string& pluginName, const QColor& color = QColor("#FFFFFF"));
    QIcon iconForAction(const std::string& actionKey, const QColor& color = QColor("#FFFFFF"));
    QIcon iconForCard(const std::string& cardType, const QColor& color = QColor("#2F7CF6"));

    QPixmap pixmapForPlugin(const std::string& pluginName, int size, const QColor& color = QColor("#FFFFFF"));
    QPixmap pixmapForAction(const std::string& actionKey, int size, const QColor& color = QColor("#FFFFFF"));
    QPixmap pixmapForCard(const std::string& cardType, int size, const QColor& color = QColor("#2F7CF6"));

    bool hasPluginIcon(const std::string& pluginName) const;
    bool hasActionIcon(const std::string& actionKey) const;

private:
    IconManager();
    ~IconManager() = default;
    IconManager(const IconManager&) = delete;
    IconManager& operator=(const IconManager&) = delete;

    void loadMapping();
    QPixmap renderSvg(const std::string& svgPath, int size, const QColor& color);
    std::string svgPathFor(const std::string& svgName, IconWeight weight) const;

    std::string basePath_;
    std::map<std::string, IconSpec> pluginMap_;
    std::map<std::string, IconSpec> actionMap_;
    std::map<std::string, IconSpec> cardMap_;
    std::map<std::string, QPixmap> cache_;
};

} // namespace gis::gui
