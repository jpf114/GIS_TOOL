#pragma once
#include <QMainWindow>
#include <gis/framework/plugin_manager.h>
#include <memory>

class QListWidget;
class ParamWidget;
class QtProgressReporter;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onPluginSelected(int row);
    void onExecute();

private:
    void loadPlugins();
    void setupUi();

    QListWidget* pluginList_ = nullptr;
    ParamWidget* paramWidget_ = nullptr;
    QtProgressReporter* reporter_ = nullptr;

    gis::framework::PluginManager pluginManager_;
    gis::framework::IGisPlugin* currentPlugin_ = nullptr;
};
