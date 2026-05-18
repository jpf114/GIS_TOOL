#pragma once

#include <QMainWindow>
#include <QString>
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
class QTabWidget;
class QCheckBox;
class QLineEdit;
class QStackedWidget;
class TaskCenterPage;
class ResultPreviewPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();
    void selectPluginByName(const std::string& pluginName);
    void selectActionByKey(const std::string& actionKey);
    bool setParamValue(const std::string& key, const std::string& value);
    void triggerExecute();
    bool lastExecutionSuccess() const;
    bool lastExecutionCancelled() const;
    QString lastExecutionMessage() const;
    QString lastExecutionRawMessage() const;

signals:
    void executionFinished(bool success);

private slots:
    void onPluginSelected(const std::string& pluginName);
    void onSubFunctionSelected(const std::string& pluginName,
                               const std::string& actionKey);
    void onExecute();
    void onParamValuesChanged();
    void onRerunTask(const QString& taskId);
    void onEditTask(const QString& taskId);
    void onDeleteTasks(const QStringList& taskIds);
    void onClearHistory();
    void onClearLogsForTask(const QString& taskId);
    void onClearAllLogs();
    void onTaskRunnerFinished(const QString& displayGroup,
                              const QString& taskId,
                              bool success,
                              bool cancelled);

private:
    void loadPlugins();
    void setupUi();
    void syncDerivedParams();

    std::vector<gis::framework::ParamSpec> effectiveParamSpecs() const;
    std::map<std::string, gis::framework::ParamValue> collectExecutionParams() const;
    void refreshExecuteButtonState();
    void refreshParamValidationState();
    void runPluginWithParams(const std::map<std::string, gis::framework::ParamValue>& params);
    void runPluginWithParams(const std::map<std::string, gis::framework::ParamValue>& params,
                             bool skipOverwritePrompt);
    void resetDerivedParamTracking();
    void resetResultPreviewState();
    void showResultPreview(const QString& outputPath,
                           const std::map<std::string, std::string>& metadata);
    void updateBatchCount();
    QStringList scanBatchFiles() const;

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    NavPanel* navPanel_ = nullptr;
    QLabel* functionIconLabel_ = nullptr;
    QLabel* functionTitleLabel_ = nullptr;
    QLabel* functionDescLabel_ = nullptr;
    QLabel* functionMetaLabel_ = nullptr;
    ParamWidget* paramWidget_ = nullptr;
    QPushButton* executeButton_ = nullptr;
    QLabel* resultSummaryLabel_ = nullptr;
    QPushButton* viewResultButton_ = nullptr;
    QStackedWidget* paramPreviewStack_ = nullptr;
    ResultPreviewPage* resultPreviewPage_ = nullptr;
    QLabel* statusAlgorithmLabel_ = nullptr;
    QLabel* statusPluginCountLabel_ = nullptr;
    QLabel* statusSubFunctionCountLabel_ = nullptr;
    QProgressBar* statusProgressBar_ = nullptr;

    QTabWidget* tabWidget_ = nullptr;
    TaskCenterPage* taskCenterPage_ = nullptr;
    QString currentEditingTaskId_;

    QCheckBox* batchCheckBox_ = nullptr;
    QLineEdit* batchDirEdit_ = nullptr;
    QPushButton* batchDirButton_ = nullptr;
    QLineEdit* batchFilterEdit_ = nullptr;
    QLabel* batchCountLabel_ = nullptr;

    QMap<QString, QString> pendingResultTaskIds_;

    int batchTotalCount_ = 0;
    int batchCompletedCount_ = 0;
    int batchFailedCount_ = 0;
    QStringList batchTaskIds_;
    bool isBatchMode_ = false;

    gis::framework::PluginManager pluginManager_;
    gis::framework::IGisPlugin* currentPlugin_ = nullptr;
    std::string currentDisplayGroupKey_;
    QString currentActionKey_;
    bool isSyncingParams_ = false;
    std::string lastAutoOutputPath_;
    std::string lastAutoVectorOutputPath_;
    std::string lastAutoRasterOutputPath_;
    std::string lastAutoExpressionValue_;
    std::string lastAutoLayerName_;
    std::optional<std::array<double, 4>> lastAutoExtent_;
    bool lastExecutionSuccess_ = false;
    bool lastExecutionCancelled_ = false;
    QString lastExecutionMessage_;
    QString lastExecutionRawMessage_;
};
