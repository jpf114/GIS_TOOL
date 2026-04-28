#include "nav_panel.h"
#include "style_constants.h"

#include <gis/framework/plugin.h>

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

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
    sidebarLayout->setContentsMargins(16, 16, 16, 16);
    sidebarLayout->setSpacing(14);

    sidebarLayout_ = sidebarLayout;

    auto* topCard = new QFrame;
    topCard->setObjectName(QStringLiteral("sidebarTopCard"));
    auto* topLayout = new QVBoxLayout(topCard);
    topLayout->setContentsMargins(16, 16, 16, 16);
    topLayout->setSpacing(6);

    auto* eyebrowLabel = new QLabel(QStringLiteral("GIS TOOLKIT"));
    eyebrowLabel->setObjectName(QStringLiteral("sidebarEyebrow"));
    topLayout->addWidget(eyebrowLabel);

    titleLabel_ = new QLabel(QStringLiteral("GIS 工具台"));
    titleLabel_->setObjectName(QStringLiteral("sidebarTitle"));
    topLayout->addWidget(titleLabel_);

    auto* descLabel = new QLabel(QStringLiteral("按主功能与子功能组织常用算法，保持参数配置和执行反馈都在同一界面完成。"));
    descLabel->setObjectName(QStringLiteral("sidebarDesc"));
    descLabel->setWordWrap(true);
    topLayout->addWidget(descLabel);

    sidebarLayout->addWidget(topCard);

    scrollArea_ = new QScrollArea;
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);

    auto* middleContainer = new QWidget;
    auto* middleLayout = new QVBoxLayout(middleContainer);
    middleLayout->setContentsMargins(0, 0, 4, 0);
    middleLayout->setSpacing(12);

    auto* sectionLabel = new QLabel(QStringLiteral("主功能分组"));
    sectionLabel->setObjectName(QStringLiteral("sidebarSection"));
    middleLayout->addWidget(sectionLabel);

    auto* pluginContainer = new QWidget;
    pluginLayout_ = new QVBoxLayout(pluginContainer);
    pluginLayout_->setContentsMargins(0, 0, 0, 0);
    pluginLayout_->setSpacing(8);
    middleLayout->addWidget(pluginContainer);

    auto* separator = new QFrame;
    separator->setObjectName(QStringLiteral("sidebarDivider"));
    middleLayout->addWidget(separator);

    subFunctionHeader_ = new QLabel(QStringLiteral("子功能"));
    subFunctionHeader_->setObjectName(QStringLiteral("subFunctionHeader"));
    subFunctionHeader_->hide();
    middleLayout->addWidget(subFunctionHeader_);

    auto* subFunctionContainer = new QWidget;
    subFunctionLayout_ = new QVBoxLayout(subFunctionContainer);
    subFunctionLayout_->setContentsMargins(0, 0, 0, 0);
    subFunctionLayout_->setSpacing(8);
    middleLayout->addWidget(subFunctionContainer);
    middleLayout->addStretch();

    scrollArea_->setWidget(middleContainer);
    sidebarLayout->addWidget(scrollArea_, 1);

    auto* footerCard = new QFrame;
    footerCard->setObjectName(QStringLiteral("sidebarFooterCard"));
    auto* footerLayout = new QVBoxLayout(footerCard);
    footerLayout->setContentsMargins(14, 14, 14, 14);
    footerLayout->setSpacing(4);

    auto* footerTitle = new QLabel(QStringLiteral("更多工具"));
    footerTitle->setObjectName(QStringLiteral("sidebarFooterTitle"));
    footerLayout->addWidget(footerTitle);

    auto* footerDesc = new QLabel(QStringLiteral("当前界面优先服务算法执行与调试，后续可以继续补充批处理、预览和结果检查能力。"));
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
    pluginButtonMap_.clear();
    currentPluginButton_ = nullptr;

    for (auto* plugin : plugins) {
        auto* btn = new QPushButton;
        btn->setObjectName(QStringLiteral("navItem"));
        btn->setCheckable(true);
        btn->setText(QString::fromUtf8(plugin->displayName()));
        btn->setToolTip(QString::fromUtf8(plugin->description()));

        connect(btn, &QPushButton::clicked, this, [this, name = plugin->name()]() {
            onPluginButtonClicked(name);
        });

        pluginLayout_->addWidget(btn);
        pluginButtonMap_[btn] = plugin->name();
    }
}

void NavPanel::clearSubFunctions() {
    QLayoutItem* item = nullptr;
    while ((item = subFunctionLayout_->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    subFunctionButtonMap_.clear();
    currentSubFunctionButton_ = nullptr;
    subFunctionHeader_->hide();
}

void NavPanel::setSubFunctions(const std::vector<std::string>& actions,
                                const std::vector<std::string>& displayNames) {
    clearSubFunctions();
    subFunctionHeader_->show();

    for (size_t i = 0; i < actions.size(); ++i) {
        auto* btn = new QPushButton;
        btn->setObjectName(QStringLiteral("subNavItem"));
        btn->setCheckable(true);
        btn->setText(QString::fromUtf8(i < displayNames.size() ? displayNames[i] : actions[i]));

        connect(btn, &QPushButton::clicked, this, [this, action = actions[i]]() {
            onSubFunctionButtonClicked(action);
        });

        subFunctionLayout_->addWidget(btn);
        subFunctionButtonMap_[btn] = actions[i];
    }
}

void NavPanel::setCurrentPluginSelection(const std::string& pluginName) {
    if (currentPluginButton_) {
        currentPluginButton_->setChecked(false);
        currentPluginButton_ = nullptr;
    }

    for (auto it = pluginButtonMap_.begin(); it != pluginButtonMap_.end(); ++it) {
        if (it->second == pluginName) {
            currentPluginButton_ = it->first;
            currentPluginButton_->setChecked(true);
            if (scrollArea_) {
                scrollArea_->ensureWidgetVisible(currentPluginButton_, 0, 32);
            }
            break;
        }
    }
}

void NavPanel::setCurrentSubFunctionSelection(const std::string& actionKey) {
    if (currentSubFunctionButton_) {
        currentSubFunctionButton_->setChecked(false);
        currentSubFunctionButton_ = nullptr;
    }

    for (auto it = subFunctionButtonMap_.begin(); it != subFunctionButtonMap_.end(); ++it) {
        if (it->second == actionKey) {
            currentSubFunctionButton_ = it->first;
            currentSubFunctionButton_->setChecked(true);
            if (scrollArea_) {
                scrollArea_->ensureWidgetVisible(currentSubFunctionButton_, 0, 48);
            }
            break;
        }
    }
}

void NavPanel::onPluginButtonClicked(const std::string& pluginName) {
    setCurrentPluginSelection(pluginName);
    emit pluginSelected(pluginName);
}

void NavPanel::onSubFunctionButtonClicked(const std::string& actionKey) {
    setCurrentSubFunctionSelection(actionKey);
    emit subFunctionSelected(actionKey);
}
