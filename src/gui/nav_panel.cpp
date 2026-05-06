#include "nav_panel.h"
#include "style_constants.h"
#include "icon_manager.h"

#include <gis/framework/plugin.h>
#include <gis/core/runtime_env.h>

#include <QApplication>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <filesystem>

namespace {

constexpr const char* kRasterToolsGroupName = "raster_tools";

bool isRasterToolsMember(const std::string& pluginName) {
    return pluginName == "raster_math"
        || pluginName == "raster_inspect"
        || pluginName == "raster_manage"
        || pluginName == "raster_render";
}

QString collapsedPluginText(const std::string& pluginName, const QString& displayName) {
    Q_UNUSED(pluginName);
    return QStringLiteral("%1   >").arg(displayName);
}

QString expandedPluginText(const std::string& pluginName, const QString& displayName) {
    Q_UNUSED(pluginName);
    return QStringLiteral("%1   v").arg(displayName);
}

QString subFunctionText(const QString& displayName, bool active) {
    Q_UNUSED(active);
    return displayName;
}

QIcon makeSidebarIcon(const std::string& kind, const QColor& bg, const QColor& fg) {
    Q_UNUSED(bg);
    auto& mgr = gis::gui::IconManager::instance();
    if (mgr.hasPluginIcon(kind)) {
        return mgr.iconForPlugin(kind, fg);
    }

    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawRoundedRect(QRectF(0.5, 0.5, 17, 17), 5, 5);
    QPen pen(fg);
    pen.setWidthF(1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawEllipse(QRectF(6, 6, 6, 6));
    return QIcon(pixmap);
}

QIcon makeSubFunctionIcon(const std::string& pluginName, const std::string& actionKey, bool active) {
    const QColor stroke = active ? QColor("#FFFFFF") : QColor("#9AA8B8");
    auto& mgr = gis::gui::IconManager::instance();
    if (mgr.hasActionIcon(pluginName, actionKey)) {
        return mgr.iconForAction(actionKey, stroke);
    }

    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(stroke);
    pen.setWidthF(1.2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(4.6, 4.6, 6.8, 6.8));
    return QIcon(pixmap);
}

}

NavPanel::NavPanel(QWidget* parent)
    : QWidget(parent) {
    setupUi();
}

void NavPanel::setupUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    sidebarFrame_ = new QFrame;
    sidebarFrame_->setObjectName(QStringLiteral("sidebar"));
    sidebarFrame_->setStyleSheet(gis::style::sidebarStyleSheet());
    sidebarFrame_->setFixedWidth(gis::style::Size::kSidebarWidth);
    sidebarFrame_->setMinimumWidth(gis::style::Size::kSidebarMinWidth);

    auto* sidebarLayout = new QVBoxLayout(sidebarFrame_);
    sidebarLayout->setContentsMargins(14, 10, 14, 10);
    sidebarLayout->setSpacing(8);
    sidebarLayout_ = sidebarLayout;

    auto* topCard = new QFrame;
    topCard->setObjectName(QStringLiteral("sidebarTopCard"));
    auto* topLayout = new QVBoxLayout(topCard);
    topLayout->setContentsMargins(4, 6, 4, 6);
    topLayout->setSpacing(3);

    auto* eyebrowLabel = new QLabel(QStringLiteral("GIS TOOLKIT"));
    eyebrowLabel->setObjectName(QStringLiteral("sidebarEyebrow"));
    topLayout->addWidget(eyebrowLabel);

    titleLabel_ = new QLabel(QStringLiteral("GIS 工具台"));
    titleLabel_->setObjectName(QStringLiteral("sidebarTitle"));
    topLayout->addWidget(titleLabel_);

    auto* descLabel = new QLabel(QStringLiteral("点击主功能后在原位展开子功能，参数配置与执行反馈集中在同一界面。"));
    descLabel->setObjectName(QStringLiteral("sidebarDesc"));
    descLabel->setWordWrap(true);
    topLayout->addWidget(descLabel);
    sidebarLayout->addWidget(topCard);

    scrollArea_ = new QScrollArea;
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* middleContainer = new QWidget;
    middleContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* middleLayout = new QVBoxLayout(middleContainer);
    middleLayout->setContentsMargins(0, 0, 4, 0);
    middleLayout->setSpacing(10);

    auto* sectionLabel = new QLabel(QStringLiteral("功能分类"));
    sectionLabel->setObjectName(QStringLiteral("sidebarSection"));
    middleLayout->addWidget(sectionLabel);

    auto* pluginContainer = new QWidget;
    pluginContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    pluginLayout_ = new QVBoxLayout(pluginContainer);
    pluginLayout_->setContentsMargins(0, 0, 0, 0);
    pluginLayout_->setSpacing(6);
    middleLayout->addWidget(pluginContainer);

    auto* separator = new QFrame;
    separator->setObjectName(QStringLiteral("sidebarDivider"));
    middleLayout->addWidget(separator);

    auto* moreLabel = new QLabel(QStringLiteral("更多工具"));
    moreLabel->setObjectName(QStringLiteral("sidebarSection"));
    middleLayout->addWidget(moreLabel);

    middleLayout->addStretch();
    scrollArea_->setWidget(middleContainer);
    sidebarLayout->addWidget(scrollArea_, 1);

    auto* footerCard = new QFrame;
    footerCard->setObjectName(QStringLiteral("sidebarFooterCard"));
    auto* footerLayout = new QVBoxLayout(footerCard);
    footerLayout->setContentsMargins(4, 6, 4, 4);
    footerLayout->setSpacing(3);

    auto* footerTitle = new QLabel(QStringLiteral("更多工具"));
    footerTitle->setObjectName(QStringLiteral("sidebarFooterTitle"));
    footerLayout->addWidget(footerTitle);

    auto* footerDesc = new QLabel(QStringLiteral("当前先聚焦算法执行，后续可继续补结果预览、批处理和检查能力。"));
    footerDesc->setObjectName(QStringLiteral("sidebarFooterDesc"));
    footerDesc->setWordWrap(true);
    footerLayout->addWidget(footerDesc);
    sidebarLayout->addWidget(footerCard);

    rootLayout->addWidget(sidebarFrame_);
}

void NavPanel::setPlugins(const std::vector<gis::framework::IGisPlugin*>& plugins) {
    QLayoutItem* item = nullptr;
    while ((item = pluginLayout_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    pluginGroupMap_.clear();
    pluginSubContainerMap_.clear();
    pluginSubLayoutMap_.clear();
    pluginDisplayNameMap_.clear();
    pluginGroupKeyMap_.clear();
    groupMembersMap_.clear();
    pluginButtonMap_.clear();
    subFunctionButtonMap_.clear();
    subFunctionDisplayNameMap_.clear();
    currentPluginButton_ = nullptr;
    currentSubFunctionButton_ = nullptr;

    bool rasterToolsCreated = false;
    for (auto* plugin : plugins) {
        const std::string pluginName = plugin->name();
        if (isRasterToolsMember(pluginName)) {
            pluginGroupKeyMap_[pluginName] = kRasterToolsGroupName;
            groupMembersMap_[kRasterToolsGroupName].push_back(pluginName);
            if (rasterToolsCreated) {
                continue;
            }
            rasterToolsCreated = true;
        } else {
            pluginGroupKeyMap_[pluginName] = pluginName;
            groupMembersMap_[pluginName].push_back(pluginName);
        }

        const std::string displayGroupName = isRasterToolsMember(pluginName)
            ? std::string{kRasterToolsGroupName}
            : pluginName;
        const QString displayName = isRasterToolsMember(pluginName)
            ? QStringLiteral("栅格工具")
            : QString::fromUtf8(plugin->displayName());
        const std::string iconKind = isRasterToolsMember(pluginName)
            ? std::string{kRasterToolsGroupName}
            : pluginName;

        auto* groupWidget = new QWidget;
        groupWidget->setStyleSheet(QStringLiteral("background: transparent;"));
        auto* groupLayout = new QVBoxLayout(groupWidget);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(2);

        auto* btn = new QPushButton;
        btn->setObjectName(QStringLiteral("navItem"));
        btn->setCheckable(true);
        btn->setText(collapsedPluginText(displayGroupName, displayName));
        btn->setIcon(makeSidebarIcon(iconKind, QColor("#2F7CF6"), QColor("#FFFFFF")));
        btn->setIconSize(QSize(18, 18));
        connect(btn, &QPushButton::clicked, this, [this, name = displayGroupName]() {
            onPluginButtonClicked(name);
        });
        groupLayout->addWidget(btn);

        auto* subContainer = new QWidget;
        subContainer->setObjectName(QStringLiteral("subFunctionContainer"));
        subContainer->setStyleSheet(QStringLiteral("background: transparent;"));
        auto* subLayout = new QVBoxLayout(subContainer);
        subLayout->setContentsMargins(10, 0, 0, 0);
        subLayout->setSpacing(4);
        subContainer->hide();
        groupLayout->addWidget(subContainer);

        pluginLayout_->addWidget(groupWidget);
        pluginGroupMap_[displayGroupName] = groupWidget;
        pluginSubContainerMap_[displayGroupName] = subContainer;
        pluginSubLayoutMap_[displayGroupName] = subLayout;
        pluginDisplayNameMap_[displayGroupName] = displayName;
        pluginButtonMap_[btn] = displayGroupName;
    }
}

void NavPanel::clearSubFunctions() {
    for (auto& entry : pluginSubLayoutMap_) {
        QLayoutItem* item = nullptr;
        while ((item = entry.second->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
    }
    subFunctionButtonMap_.clear();
    subFunctionDisplayNameMap_.clear();
    currentSubFunctionButton_ = nullptr;
}

void NavPanel::setSubFunctions(const std::vector<SubFunctionItem>& items) {
    if (!currentPluginButton_) {
        clearSubFunctions();
        return;
    }

    const auto pluginIt = pluginButtonMap_.find(currentPluginButton_);
    if (pluginIt == pluginButtonMap_.end()) {
        clearSubFunctions();
        return;
    }

    const auto subLayoutIt = pluginSubLayoutMap_.find(pluginIt->second);
    const auto subContainerIt = pluginSubContainerMap_.find(pluginIt->second);
    if (subLayoutIt == pluginSubLayoutMap_.end() || subContainerIt == pluginSubContainerMap_.end()) {
        clearSubFunctions();
        return;
    }

    clearSubFunctions();

    for (const auto& item : items) {
        const QString displayText = QString::fromUtf8(
            item.displayName.empty() ? item.actionKey : item.displayName);

        auto* btn = new QPushButton;
        btn->setObjectName(QStringLiteral("subNavItem"));
        btn->setCheckable(true);
        btn->setText(subFunctionText(displayText, false));
        btn->setIcon(makeSubFunctionIcon(item.pluginName, item.actionKey, false));
        btn->setIconSize(QSize(16, 16));

        connect(btn, &QPushButton::clicked, this, [this, item]() {
            onSubFunctionButtonClicked(item.pluginName, item.actionKey);
        });

        subLayoutIt->second->addWidget(btn);
        subFunctionButtonMap_[btn] = item;
        subFunctionDisplayNameMap_[btn] = displayText;
    }

    subContainerIt->second->setVisible(!items.empty());
}

void NavPanel::setCurrentPluginSelection(const std::string& pluginName) {
    const std::string displayGroupName = displayGroupForPlugin(pluginName);
    if (pluginName.empty()) {
        for (auto& entry : pluginButtonMap_) {
            entry.first->setChecked(false);
            const QString displayName = pluginDisplayNameMap_[entry.second];
            entry.first->setText(collapsedPluginText(entry.second, displayName));
            const auto subContainerIt = pluginSubContainerMap_.find(entry.second);
            if (subContainerIt != pluginSubContainerMap_.end()) {
                subContainerIt->second->setVisible(false);
            }
        }
        currentPluginButton_ = nullptr;
        return;
    }

    for (auto& entry : pluginButtonMap_) {
        const bool active = entry.second == displayGroupName;
        entry.first->setChecked(active);
        const QString displayName = pluginDisplayNameMap_[entry.second];
        entry.first->setText(active
            ? expandedPluginText(entry.second, displayName)
            : collapsedPluginText(entry.second, displayName));

        const auto subContainerIt = pluginSubContainerMap_.find(entry.second);
        if (subContainerIt != pluginSubContainerMap_.end()) {
            subContainerIt->second->setVisible(active && subContainerIt->second->layout() && subContainerIt->second->layout()->count() > 0);
        }

        if (active) {
            currentPluginButton_ = entry.first;
        }
    }

    if (scrollArea_ && currentPluginButton_) {
        scrollArea_->ensureWidgetVisible(currentPluginButton_, 0, 24);
    }
}

void NavPanel::setCurrentSubFunctionSelection(const std::string& pluginName,
                                              const std::string& actionKey) {
    if (actionKey.empty()) {
        for (auto& entry : subFunctionButtonMap_) {
            entry.first->setChecked(false);
            const QString displayName = subFunctionDisplayNameMap_[entry.first];
            entry.first->setText(subFunctionText(displayName, false));
            entry.first->setIcon(makeSubFunctionIcon(entry.second.pluginName, entry.second.actionKey, false));
        }
        currentSubFunctionButton_ = nullptr;
        return;
    }

    for (auto& entry : subFunctionButtonMap_) {
        const bool pluginMatched = pluginName.empty() || entry.second.pluginName == pluginName;
        const bool active = pluginMatched && entry.second.actionKey == actionKey;
        entry.first->setChecked(active);
        const QString displayName = subFunctionDisplayNameMap_[entry.first];
        entry.first->setText(subFunctionText(displayName, active));
        entry.first->setIcon(makeSubFunctionIcon(entry.second.pluginName, entry.second.actionKey, active));
        if (active) {
            currentSubFunctionButton_ = entry.first;
        }
    }

    if (scrollArea_ && currentSubFunctionButton_) {
        scrollArea_->ensureWidgetVisible(currentSubFunctionButton_, 0, 36);
    }
}

void NavPanel::onPluginButtonClicked(const std::string& pluginName) {
    auto* clickedButton = qobject_cast<QPushButton*>(sender());
    if (clickedButton) {
        const auto clickedIt = pluginButtonMap_.find(clickedButton);
        if (clickedIt != pluginButtonMap_.end() &&
            clickedIt->second == pluginName &&
            !clickedButton->isChecked()) {
            setCurrentPluginSelection(std::string{});
            setCurrentSubFunctionSelection(std::string{}, std::string{});
            emit pluginSelected(std::string{});
            return;
        }
    }

    setCurrentPluginSelection(pluginName);
    emit pluginSelected(pluginName);
}

void NavPanel::onSubFunctionButtonClicked(const std::string& pluginName,
                                          const std::string& actionKey) {
    setCurrentPluginSelection(pluginName);
    setCurrentSubFunctionSelection(pluginName, actionKey);
    emit subFunctionSelected(pluginName, actionKey);
}

std::string NavPanel::displayGroupForPlugin(const std::string& pluginName) const {
    if (pluginName.empty()) {
        return {};
    }
    if (pluginGroupMap_.find(pluginName) != pluginGroupMap_.end()) {
        return pluginName;
    }
    const auto it = pluginGroupKeyMap_.find(pluginName);
    if (it != pluginGroupKeyMap_.end()) {
        return it->second;
    }
    return pluginName;
}
