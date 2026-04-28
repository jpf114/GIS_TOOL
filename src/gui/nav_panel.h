#pragma once

#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScrollArea>
#include <vector>
#include <string>
#include <map>

namespace gis::framework {
class IGisPlugin;
}

class NavPanel : public QWidget {
    Q_OBJECT
public:
    explicit NavPanel(QWidget* parent = nullptr);

    void setPlugins(const std::vector<gis::framework::IGisPlugin*>& plugins);
    void clearSubFunctions();
    void setSubFunctions(const std::vector<std::string>& actions,
                         const std::vector<std::string>& displayNames);
    void setCurrentPluginSelection(const std::string& pluginName);
    void setCurrentSubFunctionSelection(const std::string& actionKey);

signals:
    void pluginSelected(const std::string& pluginName);
    void subFunctionSelected(const std::string& actionKey);

private:
    void setupUi();
    void onPluginButtonClicked(const std::string& pluginName);
    void onSubFunctionButtonClicked(const std::string& actionKey);

    QFrame* sidebarFrame_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* subFunctionHeader_ = nullptr;
    QVBoxLayout* pluginLayout_ = nullptr;
    QVBoxLayout* subFunctionLayout_ = nullptr;
    QVBoxLayout* sidebarLayout_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;

    std::map<QPushButton*, std::string> pluginButtonMap_;
    std::map<QPushButton*, std::string> subFunctionButtonMap_;
    QPushButton* currentPluginButton_ = nullptr;
    QPushButton* currentSubFunctionButton_ = nullptr;
};
