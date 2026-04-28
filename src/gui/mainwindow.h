#pragma once

#include <QMainWindow>
#include "gui_data_support.h"
#include <gis/framework/plugin_manager.h>
#include <map>

class QLabel;
class ParamWidget;
class PreviewPanel;
class QCheckBox;
class QFrame;
class QDragEnterEvent;
class QDropEvent;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QToolButton;
class QTabBar;
class QTreeWidget;
class QTreeWidgetItem;
class QtProgressReporter;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onPluginSelected(int index);
    void onSubFunctionSelected(int index);
    void onExecute();
    void onBuildQuickPreview();
    void onRunQuickPreview();
    void onAddRasterData();
    void onAddVectorData();
    void onAddDataDirectory();
    void onRemoveSelectedData();
    void onDataSelectionChanged();
    void onDataItemDoubleClicked(QTreeWidgetItem* item, int column);
    void showDataContextMenu(const QPoint& pos);
    void onParamValuesChanged();
    void onUseSelectedAsInput();
    void onUseSelectedAsOutput();
    void onBindSelectedToInputParam();

private:
    enum class ContextPanelMode {
        Auto,
        InfoLocked,
        ParamsLocked,
    };

    void loadPlugins();
    void setupUi();
    void rebuildSubFunctionTabs();
    std::vector<gis::framework::ParamSpec> effectiveParamSpecs() const;
    std::map<std::string, gis::framework::ParamValue> collectExecutionParams() const;
    void setCurrentActionValue(const QString& actionKey);
    QString displayTextForAction(const QString& actionKey) const;
    void refreshContextPanelForDataSelection();
    void refreshContextPanelForActionSelection();
    void updateDataInfoPanel(const QString& path,
                             gis::gui::DataKind kind,
                             gis::gui::DataOrigin origin);
    void clearDataInfoPanel();
    void refreshContextTabSelection(bool preferParams);
    void setSidebarVisible(bool visible);
    void updateSidebarToggleText();
    bool addDataPath(const QString& path,
                     bool makeCurrent = true,
                     gis::gui::DataOrigin origin = gis::gui::DataOrigin::Input);
    void openDataPath(const QString& path, bool refitPreview = false);
    void syncCurrentDataToParams();
    void applyAutoFillFromPath(const QString& path);
    void bindDataPathToParam(const QString& path, const std::string& key);
    QString buildResultSummary(const gis::framework::Result& result, const QString& resultType) const;
    QString currentSelectedDataPath() const;
    QString currentInputReferencePath() const;
    QString currentOutputReferencePath() const;
    QString currentActionValue() const;
    QString buildSuggestedOutputPathFor(const QString& inputPath) const;
    QTreeWidgetItem* selectedDataItem() const;
    bool containsPath(const QString& path) const;
    void moveDataItemToRole(QTreeWidgetItem* item, bool isOutput);
    void openDataPathInExplorer(const QString& path);
    void copyDataPathToClipboard(const QString& path);
    void refreshDataTreeVisualState();
    void refreshPreviewCompareTargets();
    void updateDataItemPresentation(QTreeWidgetItem* item, bool isActive);
    void refreshSuggestedOutputFromCurrentData();
    void runPluginWithParams(const std::map<std::string, gis::framework::ParamValue>& params,
                             const QString& statusPrefix,
                             const QString& resultType,
                             gis::gui::DataOrigin outputOrigin,
                             bool syncOutputBackToForm);
    void refreshExecuteButtonState();
    void refreshQuickPreviewButtonState();
    void refreshQuickRunButtonState();
    void refreshParamValidationState();
    void refreshDataActionButtonsState();

    QTabBar* pluginTabs_ = nullptr;
    QTabBar* subFunctionTabs_ = nullptr;
    QTreeWidget* dataTree_ = nullptr;
    QTreeWidgetItem* inputGroupItem_ = nullptr;
    QTreeWidgetItem* outputGroupItem_ = nullptr;
    QLabel* pluginTitleLabel_ = nullptr;
    QLabel* pluginDescriptionLabel_ = nullptr;
    QLabel* subFunctionLabel_ = nullptr;
    QLabel* paramValidationLabel_ = nullptr;
    QLabel* resultSummaryLabel_ = nullptr;
    QLabel* dataInfoTitleLabel_ = nullptr;
    QLabel* dataInfoPathLabel_ = nullptr;
    QLabel* dataInfoMetaLabel_ = nullptr;
    QLabel* statusAlgorithmLabel_ = nullptr;
    QLabel* statusPluginCountLabel_ = nullptr;
    QLabel* statusExecutionLabel_ = nullptr;
    QPlainTextEdit* dataInfoSummaryEdit_ = nullptr;
    QProgressBar* statusProgressBar_ = nullptr;
    QPushButton* executeButton_ = nullptr;
    QPushButton* quickPreviewButton_ = nullptr;
    QPushButton* quickRunButton_ = nullptr;
    QToolButton* useAsInputButton_ = nullptr;
    QToolButton* useAsOutputButton_ = nullptr;
    QToolButton* bindInputButton_ = nullptr;
    QCheckBox* quickRunCheckBox_ = nullptr;
    QToolButton* sidebarToggleButton_ = nullptr;
    ParamWidget* paramWidget_ = nullptr;
    PreviewPanel* previewPanel_ = nullptr;
    QTabWidget* contextTabWidget_ = nullptr;
    QFrame* sidebarPanel_ = nullptr;
    QtProgressReporter* reporter_ = nullptr;
    QString lastSuggestedOutputPath_;
    QString latestOutputPath_;
    QString currentActionKey_;
    bool isSyncingParams_ = false;
    bool isSyncingContextTabs_ = false;
    bool isSidebarVisible_ = true;
    ContextPanelMode contextPanelMode_ = ContextPanelMode::Auto;

    gis::framework::PluginManager pluginManager_;
    gis::framework::IGisPlugin* currentPlugin_ = nullptr;
    std::map<int, std::string> pluginTabMap_;
    std::map<int, QString> subFunctionTabMap_;
};
