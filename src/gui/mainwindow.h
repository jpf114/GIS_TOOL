#pragma once

#include <QMainWindow>
#include <gis/framework/plugin_manager.h>
#include <map>

class QLabel;
class ParamWidget;
class PreviewPanel;
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
    void addDataPath(const QString& path, bool makeCurrent = true, bool isOutput = false);
    void syncCurrentDataToParams();
    void applyAutoFillFromPath(const QString& path);
    void bindDataPathToParam(const QString& path, const std::string& key);
    QString buildResultSummary(const gis::framework::Result& result) const;
    QString currentSelectedDataPath() const;
    QString currentActionValue() const;
    QString buildSuggestedOutputPathFor(const QString& inputPath) const;
    QTreeWidgetItem* selectedDataItem() const;
    bool containsPath(const QString& path) const;
    void moveDataItemToRole(QTreeWidgetItem* item, bool isOutput);
    void refreshDataTreeVisualState();
    void updateDataItemPresentation(QTreeWidgetItem* item, bool isActive);
    void refreshSuggestedOutputFromCurrentData();
    void refreshExecuteButtonState();

    QTabBar* pluginTabs_ = nullptr;
    QTreeWidget* dataTree_ = nullptr;
    QTreeWidgetItem* inputGroupItem_ = nullptr;
    QTreeWidgetItem* outputGroupItem_ = nullptr;
    QLabel* pluginTitleLabel_ = nullptr;
    QLabel* pluginDescriptionLabel_ = nullptr;
    QLabel* resultSummaryLabel_ = nullptr;
    QPushButton* executeButton_ = nullptr;
    ParamWidget* paramWidget_ = nullptr;
    PreviewPanel* previewPanel_ = nullptr;
    QtProgressReporter* reporter_ = nullptr;
    QString lastSuggestedOutputPath_;
    bool isSyncingParams_ = false;

    gis::framework::PluginManager pluginManager_;
    gis::framework::IGisPlugin* currentPlugin_ = nullptr;
    std::map<int, std::string> pluginTabMap_;
};
