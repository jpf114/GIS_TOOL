#pragma once

#include <QMainWindow>
#include <gis/framework/plugin_manager.h>
#include <map>

class QLabel;
class ParamWidget;
class PreviewPanel;
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

private:
    void loadPlugins();
    void setupUi();
    void addDataPath(const QString& path, bool makeCurrent = true, bool isOutput = false);
    void syncCurrentDataToParams();
    QString buildResultSummary(const gis::framework::Result& result) const;
    QTreeWidgetItem* selectedDataItem() const;
    bool containsPath(const QString& path) const;

    QTabBar* pluginTabs_ = nullptr;
    QTreeWidget* dataTree_ = nullptr;
    QTreeWidgetItem* inputGroupItem_ = nullptr;
    QTreeWidgetItem* outputGroupItem_ = nullptr;
    QLabel* pluginTitleLabel_ = nullptr;
    QLabel* pluginDescriptionLabel_ = nullptr;
    QLabel* resultSummaryLabel_ = nullptr;
    ParamWidget* paramWidget_ = nullptr;
    PreviewPanel* previewPanel_ = nullptr;
    QtProgressReporter* reporter_ = nullptr;

    gis::framework::PluginManager pluginManager_;
    gis::framework::IGisPlugin* currentPlugin_ = nullptr;
    std::map<int, std::string> pluginTabMap_;
};
