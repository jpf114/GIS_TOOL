#include "nav_panel.h"
#include "style_constants.h"

#include <gis/framework/plugin.h>

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QFrame>

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
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    sidebarLayout_ = sidebarLayout;

    titleLabel_ = new QLabel(QStringLiteral("GIS \345\267\245\345\205\267\345\217\260"));
    titleLabel_->setObjectName(QStringLiteral("sidebarTitle"));
    sidebarLayout->addWidget(titleLabel_);

    auto* pluginContainer = new QWidget;
    pluginLayout_ = new QVBoxLayout(pluginContainer);
    pluginLayout_->setContentsMargins(0, 0, 0, 0);
    pluginLayout_->setSpacing(0);
    sidebarLayout_->addWidget(pluginContainer);

    sidebarLayout->addSpacing(8);

    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QStringLiteral("background: %1; max-height: 1px; margin: 0 16px;").arg(gis::style::Color::kCardBorder));
    sidebarLayout->addWidget(separator);

    subFunctionHeader_ = new QLabel(QStringLiteral("\345\255\220\345\212\237\350\203\275"));
    subFunctionHeader_->setObjectName(QStringLiteral("subFunctionHeader"));
    subFunctionHeader_->hide();
    sidebarLayout->addWidget(subFunctionHeader_);

    auto* subFunctionContainer = new QWidget;
    subFunctionLayout_ = new QVBoxLayout(subFunctionContainer);
    subFunctionLayout_->setContentsMargins(0, 0, 0, 0);
    subFunctionLayout_->setSpacing(0);
    sidebarLayout_->addWidget(subFunctionContainer);

    sidebarLayout->addStretch();

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

    static const std::vector<std::pair<std::string, QString>> kPluginIcons = {
        {"projection", QStringLiteral("\360\237\227\272")},
        {"cutting",    QStringLiteral("\342\234\202")},
        {"matching",   QStringLiteral("\360\237\224\227")},
        {"processing", QStringLiteral("\342\232\231")},
        {"utility",    QStringLiteral("\360\237\224\247")},
        {"vector",     QStringLiteral("\360\237\223\220")},
    };

    for (auto* plugin : plugins) {
        auto* btn = new QPushButton;
        btn->setObjectName(QStringLiteral("navItem"));
        btn->setCheckable(true);

        QString icon;
        for (const auto& [name, ic] : kPluginIcons) {
            if (plugin->name() == name) {
                icon = ic + QStringLiteral(" ");
                break;
            }
        }
        btn->setText(icon + QString::fromUtf8(plugin->displayName()));
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

void NavPanel::onPluginButtonClicked(const std::string& pluginName) {
    if (currentPluginButton_) {
        currentPluginButton_->setChecked(false);
    }

    for (auto it = pluginButtonMap_.begin(); it != pluginButtonMap_.end(); ++it) {
        if (it->second == pluginName) {
            currentPluginButton_ = it->first;
            currentPluginButton_->setChecked(true);
            break;
        }
    }

    emit pluginSelected(pluginName);
}

void NavPanel::onSubFunctionButtonClicked(const std::string& actionKey) {
    if (currentSubFunctionButton_) {
        currentSubFunctionButton_->setChecked(false);
    }

    for (auto it = subFunctionButtonMap_.begin(); it != subFunctionButtonMap_.end(); ++it) {
        if (it->second == actionKey) {
            currentSubFunctionButton_ = it->first;
            currentSubFunctionButton_->setChecked(true);
            break;
        }
    }

    emit subFunctionSelected(actionKey);
}
