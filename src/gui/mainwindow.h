#pragma once

#include <QMainWindow>
#include "gui_data_support.h"
#include <gis/framework/plugin_manager.h>
#include <map>

class QLabel;
class ParamWidget;
class PreviewPanel;
class QCheckBox;
class QPushButton;
class QTabBar;
class QTreeWidget;
class QTreeWidgetItem;
class QtProgressReporter;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onPluginSelected(int index);
    void onExecute();
    void onBuildQuickPreview();
    void onRunQuickPreview();
    void onAddRasterData();
    void onAddVectorData();
    void onRemoveSelectedData();
    void onDataSelectionChanged();
    void onDataItemDoubleClicked(QTreeWidgetItem* item, int column);
    void showDataContextMenu(const QPoint& pos);
    void onParamValuesChanged();

private:
    void loadPlugins();
    void setupUi();
    void addDataPath(const QString& path,
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

    QTabBar* pluginTabs_ = nullptr;
    QTreeWidget* dataTree_ = nullptr;
    QTreeWidgetItem* inputGroupItem_ = nullptr;
    QTreeWidgetItem* outputGroupItem_ = nullptr;
    QLabel* pluginTitleLabel_ = nullptr;
    QLabel* pluginDescriptionLabel_ = nullptr;
    QLabel* paramValidationLabel_ = nullptr;
    QLabel* resultSummaryLabel_ = nullptr;
    QPushButton* executeButton_ = nullptr;
    QPushButton* quickPreviewButton_ = nullptr;
    QPushButton* quickRunButton_ = nullptr;
    QCheckBox* quickRunCheckBox_ = nullptr;
    ParamWidget* paramWidget_ = nullptr;
    PreviewPanel* previewPanel_ = nullptr;
    QtProgressReporter* reporter_ = nullptr;
    QString lastSuggestedOutputPath_;
    QString latestOutputPath_;
    bool isSyncingParams_ = false;

    gis::framework::PluginManager pluginManager_;
    gis::framework::IGisPlugin* currentPlugin_ = nullptr;
    std::map<int, std::string> pluginTabMap_;
};
