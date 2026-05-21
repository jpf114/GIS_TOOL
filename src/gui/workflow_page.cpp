#include "workflow_page.h"
#include "style_constants.h"

#include <gis/framework/workflow.h>
#include <gis/framework/plugin_manager.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QFileDialog>

WorkflowPage::WorkflowPage(gis::framework::PluginManager& pluginManager,
                           QWidget* parent)
    : QWidget(parent), pluginManager_(pluginManager) {
    buildUi();
}

void WorkflowPage::buildUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    auto* headerCard = new QFrame;
    headerCard->setObjectName(QStringLiteral("card"));
    auto* headerLayout = new QVBoxLayout(headerCard);
    headerLayout->setContentsMargins(18, 14, 18, 14);
    headerLayout->setSpacing(8);

    auto* titleRow = new QHBoxLayout;
    auto* titleLabel = new QLabel(QStringLiteral("工作流"));
    titleLabel->setObjectName(QStringLiteral("cardTitle"));
    titleRow->addWidget(titleLabel);
    titleRow->addStretch();
    headerLayout->addLayout(titleRow);

    auto* presetRow = new QHBoxLayout;
    auto* presetLabel = new QLabel(QStringLiteral("选择预设:"));
    presetLabel->setObjectName(QStringLiteral("cardDesc"));
    presetRow->addWidget(presetLabel);

    presetCombo_ = new QComboBox;
    presetCombo_->setMinimumWidth(250);
    presetRow->addWidget(presetCombo_);
    presetRow->addStretch();
    headerLayout->addLayout(presetRow);

    descLabel_ = new QLabel;
    descLabel_->setObjectName(QStringLiteral("cardDesc"));
    descLabel_->setWordWrap(true);
    descLabel_->setText(QStringLiteral("请选择一个预设工作流"));
    headerLayout->addWidget(descLabel_);

    stepsLabel_ = new QLabel;
    stepsLabel_->setObjectName(QStringLiteral("cardDesc"));
    stepsLabel_->setWordWrap(true);
    stepsLabel_->setVisible(false);
    headerLayout->addWidget(stepsLabel_);

    mainLayout->addWidget(headerCard);

    auto* execCard = new QFrame;
    execCard->setObjectName(QStringLiteral("card"));
    auto* execLayout = new QVBoxLayout(execCard);
    execLayout->setContentsMargins(18, 14, 18, 14);
    execLayout->setSpacing(8);

    auto* execRow = new QHBoxLayout;
    executeButton_ = new QPushButton(QStringLiteral("执行工作流"));
    executeButton_->setObjectName(QStringLiteral("primaryButton"));
    executeButton_->setEnabled(false);
    execRow->addWidget(executeButton_);
    execRow->addStretch();
    execLayout->addLayout(execRow);

    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    execLayout->addWidget(progressBar_);

    mainLayout->addWidget(execCard);

    auto* logCard = new QFrame;
    logCard->setObjectName(QStringLiteral("card"));
    auto* logLayout = new QVBoxLayout(logCard);
    logLayout->setContentsMargins(18, 14, 18, 14);
    logLayout->setSpacing(8);

    auto* logTitle = new QLabel(QStringLiteral("执行日志"));
    logTitle->setObjectName(QStringLiteral("cardTitle"));
    logLayout->addWidget(logTitle);

    logEdit_ = new QTextEdit;
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumHeight(200);
    logEdit_->setObjectName(QStringLiteral("logTerminal"));
    logLayout->addWidget(logEdit_);

    mainLayout->addWidget(logCard);
    mainLayout->addStretch();

    presets_ = gis::framework::WorkflowPresets::allPresets();
    for (const auto& wf : presets_) {
        presetCombo_->addItem(QString::fromStdString(wf.name));
    }

    connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WorkflowPage::onPresetSelected);
    connect(executeButton_, &QPushButton::clicked,
            this, &WorkflowPage::onExecuteClicked);
}

void WorkflowPage::onPresetSelected(int index) {
    currentPresetIndex_ = index;
    if (index < 0 || index >= static_cast<int>(presets_.size())) {
        descLabel_->setText(QStringLiteral("请选择一个预设工作流"));
        stepsLabel_->setVisible(false);
        executeButton_->setEnabled(false);
        return;
    }

    const auto& wf = presets_[static_cast<size_t>(index)];
    descLabel_->setText(QString::fromStdString(wf.description));

    QString stepsText = QStringLiteral("步骤:\n");
    for (size_t i = 0; i < wf.steps.size(); ++i) {
        const auto& step = wf.steps[i];
        stepsText += QStringLiteral("  %1. %2.%3\n")
                         .arg(i + 1)
                         .arg(QString::fromStdString(step.pluginName))
                         .arg(QString::fromStdString(step.actionKey));
    }
    stepsLabel_->setText(stepsText);
    stepsLabel_->setVisible(true);
    executeButton_->setEnabled(true);
}

void WorkflowPage::onExecuteClicked() {
    if (currentPresetIndex_ < 0 || workflowRunning_) return;

    const auto& wf = presets_[static_cast<size_t>(currentPresetIndex_)];

    logEdit_->clear();
    progressBar_->setValue(0);
    executeButton_->setEnabled(false);
    workflowRunning_ = true;

    logEdit_->append(QStringLiteral("开始执行工作流: %1").arg(QString::fromStdString(wf.name)));

    std::map<std::string, std::string> initialParams;
    auto wfCopy = wf;

    workflowThread_ = QThread::create([this, wfCopy, initialParams]() {
        gis::framework::WorkflowRunner runner(pluginManager_);
        workflowResult_ = runner.execute(wfCopy, initialParams);
        QMetaObject::invokeMethod(this, &WorkflowPage::onWorkflowFinished);
    });

    connect(workflowThread_, &QThread::finished, workflowThread_, &QThread::deleteLater);
    workflowThread_->start();
}

void WorkflowPage::onWorkflowFinished() {
    workflowRunning_ = false;
    const auto& result = workflowResult_;

    progressBar_->setValue(result.success ? 100 : (result.totalSteps > 0 ? result.completedSteps * 100 / result.totalSteps : 0));

    for (const auto& msg : result.stepMessages) {
        logEdit_->append(QString::fromStdString(msg));
    }

    if (result.success) {
        logEdit_->append(QStringLiteral("✓ 工作流执行成功"));
    } else {
        logEdit_->append(QStringLiteral("✖ 工作流执行失败: %1").arg(QString::fromStdString(result.message)));
    }

    executeButton_->setEnabled(true);
    emit workflowFinished(result.success, QString::fromStdString(result.message));
}

void WorkflowPage::updateStepDisplay() {
}
