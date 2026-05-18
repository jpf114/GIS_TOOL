#pragma once

#include <QWidget>
#include <QString>
#include <map>

class QComboBox;
class QLabel;
class QPushButton;
class QTextEdit;
class QProgressBar;

namespace gis::framework {
class PluginManager;
struct Workflow;
}

class WorkflowPage : public QWidget {
    Q_OBJECT
public:
    explicit WorkflowPage(gis::framework::PluginManager& pluginManager,
                          QWidget* parent = nullptr);

signals:
    void workflowFinished(bool success, const QString& message);

private:
    void buildUi();
    void onPresetSelected(int index);
    void onExecuteClicked();
    void updateStepDisplay();

    gis::framework::PluginManager& pluginManager_;

    QComboBox* presetCombo_ = nullptr;
    QLabel* descLabel_ = nullptr;
    QLabel* stepsLabel_ = nullptr;
    QPushButton* executeButton_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QTextEdit* logEdit_ = nullptr;

    std::vector<gis::framework::Workflow> presets_;
    int currentPresetIndex_ = -1;
};
