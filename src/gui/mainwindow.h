#pragma once

#include <QMainWindow>
#include <gis/framework/plugin_manager.h>
#include <map>
#include <string>

class QLabel;
class ParamWidget;
class NavPanel;
class QProgressBar;
class QPushButton;
class QtProgressReporter;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onPluginSelected(const std::string& pluginName);
    void onSubFunctionSelected(const std::string& actionKey);
    void onExecute();
    void onParamValuesChanged();

private:
    void loadPlugins();
    void setupUi();
    std::vector<gis::framework::ParamSpec> effectiveParamSpecs() const;
    std::map<std::string, gis::framework::ParamValue> collectExecutionParams() const;
    void refreshExecuteButtonState();
    void refreshParamValidationState();
    void runPluginWithParams(const std::map<std::string, gis::framework::ParamValue>& params);

    NavPanel* navPanel_ = nullptr;
    QLabel* functionTitleLabel_ = nullptr;
    QLabel* functionDescLabel_ = nullptr;
    ParamWidget* paramWidget_ = nullptr;
    QPushButton* executeButton_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QLabel* resultSummaryLabel_ = nullptr;
    QLabel* statusAlgorithmLabel_ = nullptr;
    QLabel* statusPluginCountLabel_ = nullptr;
    QLabel* statusExecutionLabel_ = nullptr;
    QProgressBar* statusProgressBar_ = nullptr;
    QtProgressReporter* reporter_ = nullptr;

    gis::framework::PluginManager pluginManager_;
    gis::framework::IGisPlugin* currentPlugin_ = nullptr;
    QString currentActionKey_;
    bool isSyncingParams_ = false;
};
