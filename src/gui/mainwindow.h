#pragma once

#include <QMainWindow>
#include <gis/framework/plugin_manager.h>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <set>
#include <vector>

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
    void selectPluginByName(const std::string& pluginName);
    void selectActionByKey(const std::string& actionKey);
    bool setParamValue(const std::string& key, const std::string& value);
    void triggerExecute();

signals:
    void executionFinished(bool success);

private slots:
    void onPluginSelected(const std::string& pluginName);
    void onSubFunctionSelected(const std::string& actionKey);
    void onExecute();
    void onParamValuesChanged();

private:
    void loadPlugins();
    void setupUi();
    void syncDerivedParams();

    std::vector<gis::framework::ParamSpec> effectiveParamSpecs() const;
    std::map<std::string, gis::framework::ParamValue> collectExecutionParams() const;
    void refreshExecuteButtonState();
    void refreshParamValidationState();
    void runPluginWithParams(const std::map<std::string, gis::framework::ParamValue>& params);
    void resetDerivedParamTracking();

    static const std::map<std::string, std::map<std::string, std::set<std::string>>>& actionParamVisibilityMap();
    static std::set<std::string> visibleParamsForAction(
        const std::string& pluginName,
        const std::string& actionKey);

    static QString actionDescription(const std::string& pluginName,
                                     const QString& actionKey);

    NavPanel* navPanel_ = nullptr;
    QLabel* functionIconLabel_ = nullptr;
    QLabel* functionTitleLabel_ = nullptr;
    QLabel* functionDescLabel_ = nullptr;
    QLabel* functionMetaLabel_ = nullptr;
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
    std::string lastAutoOutputPath_;
    std::string lastAutoLayerName_;
    std::optional<std::array<double, 4>> lastAutoExtent_;
};
