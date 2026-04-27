#include "mainwindow.h"

#include "execute_worker.h"
#include "gui_data_support.h"
#include "param_widget.h"
#include "preview_panel.h"
#include "progress_dialog.h"
#include "qt_progress_reporter.h"

#include <gis/core/runtime_env.h>

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <vector>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    reporter_ = new QtProgressReporter(this);
    setupUi();
    loadPlugins();
}

MainWindow::~MainWindow() = default;

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls()) {
        return;
    }

    std::vector<std::string> paths;
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.push_back(url.toLocalFile().toStdString());
        }
    }

    if (!gis::gui::collectSupportedDataPathsRecursively(paths).empty()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls()) {
        return;
    }

    std::vector<std::string> droppedPaths;
    for (const auto& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            droppedPaths.push_back(url.toLocalFile().toStdString());
        }
    }

    const auto supportedPaths = gis::gui::collectSupportedDataPathsRecursively(droppedPaths);
    if (supportedPaths.empty()) {
        statusBar()->showMessage(QStringLiteral("拖入的数据类型暂不支持"));
        return;
    }

    int importedCount = 0;
    for (const auto& path : supportedPaths) {
        if (addDataPath(QString::fromStdString(path), false, gis::gui::DataOrigin::Input)) {
            ++importedCount;
        }
    }

    if (inputGroupItem_->childCount() > 0) {
        dataTree_->setCurrentItem(inputGroupItem_->child(inputGroupItem_->childCount() - 1));
    }

    const int unsupportedCount = static_cast<int>(droppedPaths.size() - supportedPaths.size());
    const int duplicateCount = static_cast<int>(supportedPaths.size()) - importedCount;
    QString message = QStringLiteral("已导入 %1 个数据").arg(importedCount);
    if (duplicateCount > 0) {
        message += QStringLiteral("，跳过 %1 个重复项").arg(duplicateCount);
    }
    if (unsupportedCount > 0) {
        message += QStringLiteral("，忽略 %1 个不支持的文件").arg(unsupportedCount);
    }
    statusBar()->showMessage(message);
    event->acceptProposedAction();
}

void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("GIS 工具台"));
    resize(1460, 880);
    setAcceptDrops(true);

    auto* centralWidget = new QWidget;
    setCentralWidget(centralWidget);

    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto* algorithmFrame = new QFrame;
    algorithmFrame->setObjectName(QStringLiteral("algorithmFrame"));
    auto* algorithmLayout = new QVBoxLayout(algorithmFrame);
    algorithmLayout->setContentsMargins(18, 16, 18, 16);
    algorithmLayout->setSpacing(10);

    auto* algorithmTitle = new QLabel(QStringLiteral("算法工作台"));
    algorithmTitle->setStyleSheet("font-size: 20px; font-weight: 700;");
    auto* algorithmSubtitle = new QLabel(
        QStringLiteral("上方选择算法，左侧管理输入与结果数据，中间预览数据，下方填写参数并查看执行结果。"));
    algorithmSubtitle->setStyleSheet("color: #5f6b7a;");

    pluginTabs_ = new QTabBar;
    pluginTabs_->setExpanding(false);
    pluginTabs_->setUsesScrollButtons(true);
    pluginTabs_->setDrawBase(false);
    pluginTabs_->setDocumentMode(true);
    pluginTabs_->setStyleSheet(
        "QTabBar::tab {"
        "  background: #dfe8ef; color: #243442; border-radius: 8px;"
        "  padding: 10px 18px; margin-right: 8px; min-width: 96px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #1f5f8b; color: white; font-weight: 600;"
        "}");
    connect(pluginTabs_, &QTabBar::currentChanged, this, &MainWindow::onPluginSelected);

    pluginTitleLabel_ = new QLabel(QStringLiteral("请选择算法"));
    pluginTitleLabel_->setStyleSheet("font-size: 18px; font-weight: 700;");
    pluginDescriptionLabel_ = new QLabel(QStringLiteral("当前算法说明会显示在这里。"));
    pluginDescriptionLabel_->setWordWrap(true);
    pluginDescriptionLabel_->setStyleSheet("color: #5f6b7a;");

    executeButton_ = new QPushButton(QStringLiteral("执行当前算法"));
    executeButton_->setMinimumHeight(42);
    executeButton_->setMinimumWidth(180);
    executeButton_->setStyleSheet(
        "QPushButton { background: #1f5f8b; color: white; border-radius: 8px; font-size: 14px; font-weight: 600; }"
        "QPushButton:hover { background: #194c6f; }");
    connect(executeButton_, &QPushButton::clicked, this, &MainWindow::onExecute);

    quickPreviewButton_ = new QPushButton(QStringLiteral("生成8位预览"));
    quickPreviewButton_->setMinimumHeight(42);
    quickPreviewButton_->setMinimumWidth(160);
    connect(quickPreviewButton_, &QPushButton::clicked, this, &MainWindow::onBuildQuickPreview);

    quickRunButton_ = new QPushButton(QStringLiteral("快速试算当前算法"));
    quickRunButton_->setMinimumHeight(42);
    quickRunButton_->setMinimumWidth(180);
    connect(quickRunButton_, &QPushButton::clicked, this, &MainWindow::onRunQuickPreview);

    quickRunCheckBox_ = new QCheckBox(QStringLiteral("执行时使用快速试算"));
    quickRunCheckBox_->setToolTip(QStringLiteral("勾选后，“执行当前算法”会自动使用8位小尺寸预览影像先跑一版"));

    auto* algorithmMetaLayout = new QHBoxLayout;
    algorithmMetaLayout->setSpacing(16);
    auto* algorithmTextLayout = new QVBoxLayout;
    algorithmTextLayout->setSpacing(4);
    algorithmTextLayout->addWidget(pluginTitleLabel_);
    algorithmTextLayout->addWidget(pluginDescriptionLabel_);
    algorithmTextLayout->addWidget(quickRunCheckBox_);
    algorithmMetaLayout->addLayout(algorithmTextLayout, 1);
    algorithmMetaLayout->addWidget(quickPreviewButton_, 0, Qt::AlignTop);
    algorithmMetaLayout->addWidget(quickRunButton_, 0, Qt::AlignTop);
    algorithmMetaLayout->addWidget(executeButton_, 0, Qt::AlignTop);

    algorithmLayout->addWidget(algorithmTitle);
    algorithmLayout->addWidget(algorithmSubtitle);
    algorithmLayout->addWidget(pluginTabs_);
    algorithmLayout->addLayout(algorithmMetaLayout);

    auto* workspaceSplitter = new QSplitter(Qt::Horizontal);

    auto* dataPanel = new QFrame;
    dataPanel->setObjectName(QStringLiteral("dataPanel"));
    auto* dataLayout = new QVBoxLayout(dataPanel);
    dataLayout->setContentsMargins(14, 14, 14, 14);
    dataLayout->setSpacing(10);

    auto* dataTitle = new QLabel(QStringLiteral("数据目录"));
    dataTitle->setStyleSheet("font-size: 18px; font-weight: 600;");
    auto* dataHint = new QLabel(
        QStringLiteral("输入数据和算法结果分组显示。双击可重新定位预览，右键可切换为输入或结果。"));
    dataHint->setWordWrap(true);
    dataHint->setStyleSheet("color: #5f6b7a;");

    auto* dataButtonLayout = new QHBoxLayout;
    auto* addRasterBtn = new QPushButton(QStringLiteral("添加栅格"));
    auto* addVectorBtn = new QPushButton(QStringLiteral("添加矢量"));
    auto* addDirectoryBtn = new QPushButton(QStringLiteral("导入目录"));
    auto* removeDataBtn = new QPushButton(QStringLiteral("移除"));
    connect(addRasterBtn, &QPushButton::clicked, this, &MainWindow::onAddRasterData);
    connect(addVectorBtn, &QPushButton::clicked, this, &MainWindow::onAddVectorData);
    connect(addDirectoryBtn, &QPushButton::clicked, this, &MainWindow::onAddDataDirectory);
    connect(removeDataBtn, &QPushButton::clicked, this, &MainWindow::onRemoveSelectedData);
    dataButtonLayout->addWidget(addRasterBtn);
    dataButtonLayout->addWidget(addVectorBtn);
    dataButtonLayout->addWidget(addDirectoryBtn);
    dataButtonLayout->addWidget(removeDataBtn);

    auto* dataActionLayout = new QHBoxLayout;
    useAsInputButton_ = new QPushButton(QStringLiteral("设为输入"));
    useAsOutputButton_ = new QPushButton(QStringLiteral("设为结果"));
    bindInputButton_ = new QPushButton(QStringLiteral("填入参数"));
    useAsInputButton_->setToolTip(QStringLiteral("把当前选中数据切换到输入数据分组"));
    useAsOutputButton_->setToolTip(QStringLiteral("把当前选中数据切换到结果数据分组"));
    bindInputButton_->setToolTip(QStringLiteral("把当前选中数据填入 input 参数"));
    connect(useAsInputButton_, &QPushButton::clicked, this, &MainWindow::onUseSelectedAsInput);
    connect(useAsOutputButton_, &QPushButton::clicked, this, &MainWindow::onUseSelectedAsOutput);
    connect(bindInputButton_, &QPushButton::clicked, this, &MainWindow::onBindSelectedToInputParam);
    dataActionLayout->addWidget(useAsInputButton_);
    dataActionLayout->addWidget(useAsOutputButton_);
    dataActionLayout->addWidget(bindInputButton_);

    dataTree_ = new QTreeWidget;
    dataTree_->setColumnCount(1);
    dataTree_->setHeaderHidden(true);
    dataTree_->setAlternatingRowColors(true);
    dataTree_->header()->setStretchLastSection(true);
    dataTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dataTree_, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onDataSelectionChanged);
    connect(dataTree_, &QTreeWidget::itemDoubleClicked, this, &MainWindow::onDataItemDoubleClicked);
    connect(dataTree_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showDataContextMenu);

    inputGroupItem_ = new QTreeWidgetItem(QStringList(QStringLiteral("输入数据")));
    outputGroupItem_ = new QTreeWidgetItem(QStringList(QStringLiteral("结果数据")));
    inputGroupItem_->setFlags(inputGroupItem_->flags() & ~Qt::ItemIsSelectable);
    outputGroupItem_->setFlags(outputGroupItem_->flags() & ~Qt::ItemIsSelectable);
    inputGroupItem_->setExpanded(true);
    outputGroupItem_->setExpanded(true);
    dataTree_->addTopLevelItem(inputGroupItem_);
    dataTree_->addTopLevelItem(outputGroupItem_);

    dataLayout->addWidget(dataTitle);
    dataLayout->addWidget(dataHint);
    dataLayout->addLayout(dataButtonLayout);
    dataLayout->addLayout(dataActionLayout);
    dataLayout->addWidget(dataTree_, 1);

    auto* centerPanel = new QFrame;
    centerPanel->setObjectName(QStringLiteral("centerPanel"));
    auto* centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(10);

    auto* centerSplitter = new QSplitter(Qt::Vertical);
    previewPanel_ = new PreviewPanel;
    connect(previewPanel_, &PreviewPanel::requestOpenPath, this, [this](const QString& path) {
        openDataPath(path, true);
    });
    connect(previewPanel_, &PreviewPanel::requestUseAsInput, this, [this](const QString& path) {
        bindDataPathToParam(path, "input");
    });

    auto* paramsPanel = new QFrame;
    paramsPanel->setObjectName(QStringLiteral("paramsPanel"));
    auto* paramsLayout = new QVBoxLayout(paramsPanel);
    paramsLayout->setContentsMargins(14, 14, 14, 14);
    paramsLayout->setSpacing(10);

    auto* paramsTitle = new QLabel(QStringLiteral("参数与结果"));
    paramsTitle->setStyleSheet("font-size: 18px; font-weight: 600;");
    auto* paramsHint = new QLabel(QStringLiteral("当前算法参数会自动生成，执行结果摘要会显示在下方。"));
    paramsHint->setWordWrap(true);
    paramsHint->setStyleSheet("color: #5f6b7a;");

    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    paramWidget_ = new ParamWidget;
    connect(paramWidget_, &ParamWidget::paramsChanged, this, &MainWindow::onParamValuesChanged);
    scrollArea->setWidget(paramWidget_);

    paramValidationLabel_ = new QLabel(QStringLiteral("当前参数已就绪。"));
    paramValidationLabel_->setWordWrap(true);
    paramValidationLabel_->setStyleSheet(
        "background: #eef6ec; color: #245b2b; border: 1px solid #cfe4cc; border-radius: 8px; padding: 8px 10px;");

    auto* resultGroup = new QGroupBox(QStringLiteral("执行结果"));
    auto* resultLayout = new QVBoxLayout(resultGroup);
    resultSummaryLabel_ = new QLabel(QStringLiteral("执行后会在这里显示结果摘要。"));
    resultSummaryLabel_->setWordWrap(true);
    resultLayout->addWidget(resultSummaryLabel_);

    paramsLayout->addWidget(paramsTitle);
    paramsLayout->addWidget(paramsHint);
    paramsLayout->addWidget(paramValidationLabel_);
    paramsLayout->addWidget(scrollArea, 1);
    paramsLayout->addWidget(resultGroup);

    centerSplitter->addWidget(previewPanel_);
    centerSplitter->addWidget(paramsPanel);
    centerSplitter->setStretchFactor(0, 5);
    centerSplitter->setStretchFactor(1, 3);
    centerLayout->addWidget(centerSplitter);

    workspaceSplitter->addWidget(dataPanel);
    workspaceSplitter->addWidget(centerPanel);
    workspaceSplitter->setStretchFactor(0, 2);
    workspaceSplitter->setStretchFactor(1, 7);

    mainLayout->addWidget(algorithmFrame);
    mainLayout->addWidget(workspaceSplitter, 1);

    centralWidget->setStyleSheet(
        "QFrame#algorithmFrame, QFrame#dataPanel, QFrame#centerPanel, QFrame#paramsPanel {"
        "  background: #f8fbfd;"
        "  border: 1px solid #dce6ee;"
        "  border-radius: 10px;"
        "}");

    statusBar()->showMessage(QStringLiteral("就绪"));
    refreshQuickPreviewButtonState();
    refreshQuickRunButtonState();
    refreshExecuteButtonState();
    refreshParamValidationState();
    refreshDataActionButtonsState();
}

void MainWindow::loadPlugins() {
    namespace fs = std::filesystem;

    const auto exePath = fs::canonical(fs::path(QApplication::applicationFilePath().toStdWString()).parent_path());
    const auto pluginsDir = gis::core::findPluginDirectoryFrom(exePath);
    if (!pluginsDir.empty()) {
        pluginManager_.loadFromDirectory(pluginsDir.string());
    }

    static const std::vector<std::string> preferredOrder = {
        "projection", "cutting", "matching", "processing", "utility", "vector"
    };

    while (pluginTabs_->count() > 0) {
        pluginTabs_->removeTab(0);
    }
    pluginTabMap_.clear();

    std::vector<gis::framework::IGisPlugin*> plugins = pluginManager_.plugins();
    std::sort(plugins.begin(), plugins.end(), [&](auto* lhs, auto* rhs) {
        const auto leftIt = std::find(preferredOrder.begin(), preferredOrder.end(), lhs->name());
        const auto rightIt = std::find(preferredOrder.begin(), preferredOrder.end(), rhs->name());
        return leftIt < rightIt;
    });

    for (auto* plugin : plugins) {
        const int index = pluginTabs_->addTab(QString::fromUtf8(plugin->displayName()));
        pluginTabs_->setTabToolTip(index, QString::fromUtf8(plugin->description()));
        pluginTabMap_[index] = plugin->name();
    }

    if (plugins.empty()) {
        statusBar()->showMessage(QStringLiteral("未找到插件，请检查 plugins 目录"));
        return;
    }

    pluginTabs_->setCurrentIndex(0);
    statusBar()->showMessage(QStringLiteral("已加载 %1 个算法插件").arg(plugins.size()));
}

void MainWindow::onPluginSelected(int index) {
    if (index < 0 || pluginTabMap_.count(index) == 0) {
        currentPlugin_ = nullptr;
        paramWidget_->clear();
        lastSuggestedOutputPath_.clear();
        refreshQuickPreviewButtonState();
        refreshQuickRunButtonState();
        refreshParamValidationState();
        refreshExecuteButtonState();
        return;
    }

    currentPlugin_ = pluginManager_.find(pluginTabMap_[index]);
    if (!currentPlugin_) {
        return;
    }

    paramWidget_->setParamSpecs(currentPlugin_->paramSpecs());
    pluginTitleLabel_->setText(QString::fromUtf8(currentPlugin_->displayName()));
    pluginDescriptionLabel_->setText(QString::fromUtf8(currentPlugin_->description()));
    statusBar()->showMessage(QStringLiteral("当前算法: %1").arg(QString::fromStdString(currentPlugin_->name())));
    syncCurrentDataToParams();
    refreshQuickPreviewButtonState();
    refreshQuickRunButtonState();
    refreshParamValidationState();
    refreshExecuteButtonState();
}

void MainWindow::onBuildQuickPreview() {
    const QString inputPath = currentSelectedDataPath();
    if (inputPath.isEmpty() ||
        gis::gui::detectDataKind(inputPath.toStdString()) != gis::gui::DataKind::Raster) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先选择一个栅格输入数据"));
        return;
    }

    const QString outputPath = QString::fromStdString(
        gis::gui::buildQuickPreviewOutputPath(inputPath.toStdString()));
    if (outputPath.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("无法生成预览输出路径"));
        return;
    }

    if (!gis::gui::buildQuickPreviewRaster(
            inputPath.toStdString(),
            outputPath.toStdString(),
            512)) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("快速预览影像生成失败"));
        return;
    }

    addDataPath(outputPath, true, gis::gui::DataOrigin::QuickPreview);
    latestOutputPath_ = outputPath;
    refreshPreviewCompareTargets();
    previewPanel_->showComparePreviewIfAvailable();
    resultSummaryLabel_->setText(QStringLiteral("结果类型: 预览影像\n已生成 8 位快速预览影像\n%1").arg(outputPath));
    statusBar()->showMessage(QStringLiteral("已生成8位快速预览影像"));
    refreshQuickRunButtonState();
}

void MainWindow::onRunQuickPreview() {
    if (!currentPlugin_) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先选择一个算法"));
        return;
    }

    const auto specs = currentPlugin_->paramSpecs();
    const auto params = paramWidget_->collectParams();
    const std::string validationMessage = gis::gui::validateExecutionParams(specs, params);
    if (!validationMessage.empty()) {
        refreshParamValidationState();
        QMessageBox::warning(this, QStringLiteral("参数不完整"),
                             QString::fromUtf8(validationMessage));
        return;
    }

    std::map<std::string, gis::framework::ParamValue> quickParams;
    if (!gis::gui::buildQuickPreviewExecutionParams(
            specs,
            params,
            currentPlugin_->name(),
            currentActionValue().toStdString(),
            quickParams,
            512)) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("快速试算参数准备失败，请确认当前输入包含可生成预览的栅格数据"));
        return;
    }

    runPluginWithParams(
        quickParams, QStringLiteral("快速试算"), QStringLiteral("快速试算结果"), gis::gui::DataOrigin::QuickRun, false);
}

void MainWindow::onExecute() {
    if (!currentPlugin_) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先选择一个算法"));
        return;
    }

    const auto specs = currentPlugin_->paramSpecs();
    auto params = paramWidget_->collectParams();
    const QString inputPath = currentSelectedDataPath();
    if (paramWidget_->hasParam("output")) {
        const std::string currentOutput = paramWidget_->stringValue("output");
        if (currentOutput.empty() && !inputPath.isEmpty()) {
            const QString suggestedOutputPath = buildSuggestedOutputPathFor(inputPath);
            if (!suggestedOutputPath.isEmpty()) {
                paramWidget_->setStringValue("output", suggestedOutputPath.toUtf8().constData());
                params["output"] = suggestedOutputPath.toUtf8().constData();
                lastSuggestedOutputPath_ = suggestedOutputPath;
            }
        }
    }

    const std::string validationMessage = gis::gui::validateExecutionParams(specs, params);
    if (!validationMessage.empty()) {
        refreshParamValidationState();
        QMessageBox::warning(this, QStringLiteral("参数不完整"),
                             QString::fromUtf8(validationMessage));
        statusBar()->showMessage(QStringLiteral("执行已拦截: 请先补全必要参数"));
        return;
    }

    if (quickRunCheckBox_ && quickRunCheckBox_->isChecked()) {
        std::map<std::string, gis::framework::ParamValue> quickParams;
        if (!gis::gui::buildQuickPreviewExecutionParams(
                specs,
                params,
                currentPlugin_->name(),
                currentActionValue().toStdString(),
                quickParams,
                512)) {
            QMessageBox::warning(this, QStringLiteral("提示"),
                                 QStringLiteral("快速试算参数准备失败，请确认当前输入包含可生成预览的栅格数据"));
            return;
        }

        runPluginWithParams(
            quickParams, QStringLiteral("快速试算"), QStringLiteral("快速试算结果"), gis::gui::DataOrigin::QuickRun, false);
        return;
    }

    runPluginWithParams(
        params, QStringLiteral("执行"), QStringLiteral("正式结果"), gis::gui::DataOrigin::Output, true);
}

void MainWindow::runPluginWithParams(
    const std::map<std::string, gis::framework::ParamValue>& params,
    const QString& statusPrefix,
    const QString& resultType,
    gis::gui::DataOrigin outputOrigin,
    bool syncOutputBackToForm) {
    reporter_->reset();

    auto* worker = new ExecuteWorker;
    worker->setup(currentPlugin_, params, reporter_);

    auto* thread = new QThread;
    worker->moveToThread(thread);

    auto* progressDialog = new ProgressDialog(reporter_);

    connect(thread, &QThread::started, worker, &ExecuteWorker::run);
    connect(worker, &ExecuteWorker::finished, this, [this, progressDialog, statusPrefix, resultType, outputOrigin, syncOutputBackToForm](const gis::framework::Result& result) {
        QString message = QString::fromUtf8(result.message);
        const bool cancelled = result.message == "已取消执行";
        if (result.success && !result.outputPath.empty()) {
            message += QStringLiteral("\n结果已写出并加入左侧结果数据区。");
        }
        progressDialog->setFinished(message, result.success, cancelled);
        resultSummaryLabel_->setText(buildResultSummary(result, resultType));

        if (result.success) {
            if (syncOutputBackToForm && paramWidget_->hasParam("output") && !result.outputPath.empty()) {
                paramWidget_->setStringValue("output", result.outputPath);
                lastSuggestedOutputPath_ = QString::fromUtf8(result.outputPath);
            }
            if (!result.outputPath.empty() && std::filesystem::exists(result.outputPath)) {
                latestOutputPath_ = QString::fromUtf8(result.outputPath);
                addDataPath(latestOutputPath_, true, outputOrigin);
                refreshPreviewCompareTargets();
                previewPanel_->showComparePreviewIfAvailable();
            }
            statusBar()->showMessage(statusPrefix + QStringLiteral("成功: ") + message);
        } else if (cancelled) {
            statusBar()->showMessage(statusPrefix + QStringLiteral("已取消"));
        } else {
            statusBar()->showMessage(statusPrefix + QStringLiteral("失败: ") + message);
        }
    });
    connect(worker, &ExecuteWorker::finished, thread, &QThread::quit);
    connect(worker, &ExecuteWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(progressDialog, &QDialog::finished, progressDialog, &QObject::deleteLater);

    thread->start();
    progressDialog->exec();
}

void MainWindow::onAddRasterData() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("选择栅格数据"),
        QString(),
        QStringLiteral("Raster (*.tif *.tiff *.img *.vrt *.png *.jpg *.jpeg *.bmp)"));
    int importedCount = 0;
    for (const auto& path : paths) {
        if (addDataPath(path, false, gis::gui::DataOrigin::Input)) {
            ++importedCount;
        }
    }
    if (!paths.isEmpty() && inputGroupItem_->childCount() > 0) {
        dataTree_->setCurrentItem(inputGroupItem_->child(inputGroupItem_->childCount() - 1));
        const int duplicateCount = paths.size() - importedCount;
        QString message = QStringLiteral("已导入 %1 个栅格数据").arg(importedCount);
        if (duplicateCount > 0) {
            message += QStringLiteral("，跳过 %1 个重复项").arg(duplicateCount);
        }
        statusBar()->showMessage(message);
    } else if (!paths.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("未导入新栅格数据，所选文件均已存在"));
    } else {
        refreshDataTreeVisualState();
    }
}

void MainWindow::onAddVectorData() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("选择矢量数据"),
        QString(),
        QStringLiteral("Vector (*.shp *.geojson *.json *.gpkg *.kml *.csv)"));
    int importedCount = 0;
    for (const auto& path : paths) {
        if (addDataPath(path, false, gis::gui::DataOrigin::Input)) {
            ++importedCount;
        }
    }
    if (!paths.isEmpty() && inputGroupItem_->childCount() > 0) {
        dataTree_->setCurrentItem(inputGroupItem_->child(inputGroupItem_->childCount() - 1));
        const int duplicateCount = paths.size() - importedCount;
        QString message = QStringLiteral("已导入 %1 个矢量数据").arg(importedCount);
        if (duplicateCount > 0) {
            message += QStringLiteral("，跳过 %1 个重复项").arg(duplicateCount);
        }
        statusBar()->showMessage(message);
    } else if (!paths.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("未导入新矢量数据，所选文件均已存在"));
    } else {
        refreshDataTreeVisualState();
    }
}

void MainWindow::onAddDataDirectory() {
    const QString directory = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("选择数据目录"),
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (directory.isEmpty()) {
        refreshDataTreeVisualState();
        return;
    }

    const auto supportedPaths = gis::gui::collectSupportedDataPathsRecursively(
        {directory.toStdString()});
    if (supportedPaths.empty()) {
        statusBar()->showMessage(QStringLiteral("该目录下未发现支持的栅格或矢量数据"));
        return;
    }

    int importedCount = 0;
    for (const auto& path : supportedPaths) {
        if (addDataPath(QString::fromStdString(path), false, gis::gui::DataOrigin::Input)) {
            ++importedCount;
        }
    }

    if (inputGroupItem_->childCount() > 0) {
        dataTree_->setCurrentItem(inputGroupItem_->child(inputGroupItem_->childCount() - 1));
    }

    const int duplicateCount = static_cast<int>(supportedPaths.size()) - importedCount;
    QString message = QStringLiteral("目录导入完成，新增 %1 个数据").arg(importedCount);
    if (duplicateCount > 0) {
        message += QStringLiteral("，跳过 %1 个重复项").arg(duplicateCount);
    }
    statusBar()->showMessage(message);
}

void MainWindow::onRemoveSelectedData() {
    auto* item = selectedDataItem();
    if (!item) {
        return;
    }

    delete item;
    if (!selectedDataItem()) {
        previewPanel_->clearPreview();
        statusBar()->showMessage(QStringLiteral("已移除当前数据"));
    }
    refreshDataTreeVisualState();
}

void MainWindow::onDataSelectionChanged() {
    auto* item = selectedDataItem();
    if (!item) {
        previewPanel_->clearPreview();
        refreshPreviewCompareTargets();
        refreshDataTreeVisualState();
        return;
    }

    const QString path = item->data(0, Qt::UserRole).toString();
    const auto origin = static_cast<gis::gui::DataOrigin>(item->data(0, Qt::UserRole + 2).toInt());
    previewPanel_->setCurrentOrigin(origin);
    previewPanel_->showPath(path.toUtf8().constData());
    syncCurrentDataToParams();
    refreshPreviewCompareTargets();
    refreshDataTreeVisualState();
    statusBar()->showMessage(QStringLiteral("当前数据: %1").arg(path));
    refreshQuickPreviewButtonState();
    refreshQuickRunButtonState();
    refreshParamValidationState();
    refreshExecuteButtonState();
}

void MainWindow::onParamValuesChanged() {
    if (isSyncingParams_) {
        return;
    }

    refreshSuggestedOutputFromCurrentData();
    refreshQuickRunButtonState();
    refreshParamValidationState();
    refreshExecuteButtonState();
}

void MainWindow::onUseSelectedAsInput() {
    auto* item = selectedDataItem();
    if (!item) {
        return;
    }
    moveDataItemToRole(item, false);
    statusBar()->showMessage(QStringLiteral("当前数据已设为输入数据"));
}

void MainWindow::onUseSelectedAsOutput() {
    auto* item = selectedDataItem();
    if (!item) {
        return;
    }
    moveDataItemToRole(item, true);
    statusBar()->showMessage(QStringLiteral("当前数据已设为结果数据"));
}

void MainWindow::onBindSelectedToInputParam() {
    const QString path = currentSelectedDataPath();
    if (path.isEmpty()) {
        return;
    }
    bindDataPathToParam(path, "input");
}

void MainWindow::onDataItemDoubleClicked(QTreeWidgetItem* item, int) {
    if (!item || item == inputGroupItem_ || item == outputGroupItem_) {
        return;
    }

    openDataPath(item->data(0, Qt::UserRole).toString(), true);
}

void MainWindow::showDataContextMenu(const QPoint& pos) {
    auto* item = dataTree_->itemAt(pos);
    if (!item || item == inputGroupItem_ || item == outputGroupItem_) {
        return;
    }

    const QString path = item->data(0, Qt::UserRole).toString();
    const auto kind = static_cast<gis::gui::DataKind>(item->data(0, Qt::UserRole + 1).toInt());

    QMenu menu(this);
    auto* setInputAction = menu.addAction(QStringLiteral("设为输入数据"));
    auto* setOutputAction = menu.addAction(QStringLiteral("设为结果数据"));
    std::map<QAction*, std::string> bindActions;
    if (currentPlugin_) {
        const auto options = gis::gui::collectBindableParamOptions(currentPlugin_->paramSpecs(), kind);
        if (!options.empty()) {
            auto* bindMenu = menu.addMenu(QStringLiteral("填入参数"));
            for (const auto& option : options) {
                auto* action = bindMenu->addAction(QString::fromUtf8(option.displayName));
                action->setToolTip(QString::fromUtf8(option.key));
                bindActions[action] = option.key;
            }
        }
    }
    menu.addSeparator();
    auto* removeAction = menu.addAction(QStringLiteral("移除"));

    QAction* selectedAction = menu.exec(dataTree_->viewport()->mapToGlobal(pos));
    if (!selectedAction) {
        return;
    }

    if (selectedAction == setInputAction) {
        moveDataItemToRole(item, false);
    } else if (selectedAction == setOutputAction) {
        moveDataItemToRole(item, true);
    } else if (bindActions.count(selectedAction) > 0) {
        bindDataPathToParam(path, bindActions[selectedAction]);
    } else if (selectedAction == removeAction) {
        delete item;
        if (!selectedDataItem()) {
            previewPanel_->clearPreview();
        }
        refreshPreviewCompareTargets();
        refreshDataTreeVisualState();
    }
}

bool MainWindow::addDataPath(const QString& path, bool makeCurrent, gis::gui::DataOrigin origin) {
    if (path.isEmpty() || containsPath(path)) {
        if (makeCurrent && !path.isEmpty()) {
            for (auto* group : {inputGroupItem_, outputGroupItem_}) {
                for (int i = 0; i < group->childCount(); ++i) {
                    if (group->child(i)->data(0, Qt::UserRole).toString() == path) {
                        dataTree_->setCurrentItem(group->child(i));
                        return false;
                    }
                }
            }
        }
        refreshDataTreeVisualState();
        return false;
    }

    const auto kind = gis::gui::detectDataKind(path.toUtf8().constData());
    auto* parent = gis::gui::isOutputDataOrigin(origin) ? outputGroupItem_ : inputGroupItem_;
    auto* item = new QTreeWidgetItem(parent);
    item->setToolTip(0, path);
    item->setData(0, Qt::UserRole, path);
    item->setData(0, Qt::UserRole + 1, static_cast<int>(kind));
    item->setData(0, Qt::UserRole + 2, static_cast<int>(origin));
    updateDataItemPresentation(item, false);
    parent->setExpanded(true);

    if (makeCurrent) {
        dataTree_->setCurrentItem(item);
    } else {
        refreshDataTreeVisualState();
    }
    return true;
}

void MainWindow::syncCurrentDataToParams() {
    if (!currentPlugin_) {
        return;
    }

    auto* item = selectedDataItem();
    if (!item) {
        return;
    }

    const auto origin = static_cast<gis::gui::DataOrigin>(item->data(0, Qt::UserRole + 2).toInt());
    if (origin != gis::gui::DataOrigin::Input) {
        return;
    }

    const QString path = currentSelectedDataPath();
    if (path.isEmpty()) {
        return;
    }
    isSyncingParams_ = true;

    if (paramWidget_->hasParam("input")) {
        paramWidget_->setStringValue("input", path.toUtf8().constData());
    }

    refreshSuggestedOutputFromCurrentData();
    applyAutoFillFromPath(path);
    isSyncingParams_ = false;
}

void MainWindow::applyAutoFillFromPath(const QString& path) {
    if (path.isEmpty()) {
        return;
    }

    const auto autoFillInfo = gis::gui::inspectDataForAutoFill(path.toStdString());
    if (paramWidget_->hasParam("layer") && !autoFillInfo.layerName.empty()) {
        paramWidget_->setStringValue("layer", autoFillInfo.layerName);
    }

    if (paramWidget_->hasParam("extent") && autoFillInfo.hasExtent) {
        paramWidget_->setExtentValue("extent", autoFillInfo.extent);
    }

    if (!autoFillInfo.crs.empty()) {
        if (paramWidget_->hasParam("src_srs")) {
            paramWidget_->setStringValue("src_srs", autoFillInfo.crs);
        }
        if (paramWidget_->hasParam("srs")) {
            paramWidget_->setStringValue("srs", autoFillInfo.crs);
        }
    }
}

void MainWindow::bindDataPathToParam(const QString& path, const std::string& key) {
    if (!paramWidget_ || path.isEmpty() || key.empty() || !paramWidget_->hasParam(key)) {
        return;
    }

    isSyncingParams_ = true;
    paramWidget_->setStringValue(key, path.toUtf8().constData());
    if (key == "input") {
        refreshSuggestedOutputFromCurrentData();
    }
    applyAutoFillFromPath(path);
    isSyncingParams_ = false;
    statusBar()->showMessage(
        QStringLiteral("已将当前数据填入参数: %1").arg(QString::fromStdString(key)));
    refreshParamValidationState();
    refreshQuickRunButtonState();
    refreshExecuteButtonState();
    refreshDataActionButtonsState();
}

QString MainWindow::buildResultSummary(const gis::framework::Result& result, const QString& resultType) const {
    QString summary = QString::fromUtf8(gis::gui::buildResultSummaryText(result));
    if (!resultType.isEmpty()) {
        summary = QStringLiteral("结果类型: %1\n").arg(resultType) + summary;
    }
    return summary;
}

QString MainWindow::currentSelectedDataPath() const {
    auto* item = selectedDataItem();
    if (!item) {
        return {};
    }
    return item->data(0, Qt::UserRole).toString();
}

QString MainWindow::currentInputReferencePath() const {
    if (paramWidget_ && paramWidget_->hasParam("input")) {
        const QString path = QString::fromUtf8(paramWidget_->stringValue("input"));
        if (!path.isEmpty()) {
            return path;
        }
    }

    auto* item = selectedDataItem();
    if (!item) {
        return {};
    }

    const auto origin = static_cast<gis::gui::DataOrigin>(item->data(0, Qt::UserRole + 2).toInt());
    return origin == gis::gui::DataOrigin::Input ? item->data(0, Qt::UserRole).toString() : QString();
}

QString MainWindow::currentOutputReferencePath() const {
    auto* item = selectedDataItem();
    if (item) {
        const auto origin = static_cast<gis::gui::DataOrigin>(item->data(0, Qt::UserRole + 2).toInt());
        if (gis::gui::isOutputDataOrigin(origin)) {
            return item->data(0, Qt::UserRole).toString();
        }
    }
    return latestOutputPath_;
}

QString MainWindow::currentActionValue() const {
    if (!paramWidget_ || !paramWidget_->hasParam("action")) {
        return {};
    }
    return QString::fromUtf8(paramWidget_->stringValue("action"));
}

QString MainWindow::buildSuggestedOutputPathFor(const QString& inputPath) const {
    if (!currentPlugin_ || inputPath.isEmpty()) {
        return {};
    }
    return QString::fromStdString(gis::gui::buildSuggestedOutputPath(
        inputPath.toStdString(),
        currentPlugin_->name(),
        currentActionValue().toStdString()));
}

void MainWindow::openDataPath(const QString& path, bool refitPreview) {
    if (path.isEmpty()) {
        return;
    }

    for (auto* group : {inputGroupItem_, outputGroupItem_}) {
        for (int i = 0; i < group->childCount(); ++i) {
            auto* item = group->child(i);
            if (item->data(0, Qt::UserRole).toString() == path) {
                dataTree_->setCurrentItem(item);
                if (refitPreview) {
                    previewPanel_->refitPreview();
                }
                return;
            }
        }
    }

    previewPanel_->showPath(path.toUtf8().constData());
    refreshPreviewCompareTargets();
    if (refitPreview) {
        previewPanel_->refitPreview();
    }
}

void MainWindow::refreshSuggestedOutputFromCurrentData() {
    const QString path = currentSelectedDataPath();
    if (path.isEmpty() || !paramWidget_ || !paramWidget_->hasParam("output")) {
        return;
    }

    const QString currentOutput = QString::fromUtf8(paramWidget_->stringValue("output"));
    const QString suggestedOutput = buildSuggestedOutputPathFor(path);
    if (!suggestedOutput.isEmpty() &&
        (currentOutput.isEmpty() || currentOutput == lastSuggestedOutputPath_)) {
        paramWidget_->setStringValue("output", suggestedOutput.toUtf8().constData());
        lastSuggestedOutputPath_ = suggestedOutput;
    }
}

void MainWindow::refreshPreviewCompareTargets() {
    if (!previewPanel_) {
        return;
    }
    previewPanel_->setCompareTargets(currentInputReferencePath(), currentOutputReferencePath());
}

void MainWindow::refreshExecuteButtonState() {
    if (!executeButton_) {
        return;
    }

    if (!currentPlugin_) {
        executeButton_->setEnabled(false);
        executeButton_->setToolTip(QStringLiteral("请先选择一个算法"));
        return;
    }

    const std::string validationMessage = gis::gui::validateExecutionParams(
        currentPlugin_->paramSpecs(),
        paramWidget_->collectParams());
    if (!validationMessage.empty()) {
        executeButton_->setEnabled(false);
        executeButton_->setToolTip(QString::fromUtf8(validationMessage));
        return;
    }

    executeButton_->setEnabled(true);
    executeButton_->setToolTip(QStringLiteral("参数已就绪，可执行当前算法"));
}

void MainWindow::refreshQuickPreviewButtonState() {
    if (!quickPreviewButton_) {
        return;
    }

    const QString path = currentSelectedDataPath();
    const bool isRaster = !path.isEmpty() &&
        gis::gui::detectDataKind(path.toStdString()) == gis::gui::DataKind::Raster;
    quickPreviewButton_->setEnabled(isRaster);
    quickPreviewButton_->setToolTip(
        isRaster
            ? QStringLiteral("基于当前栅格生成8位小尺寸预览影像")
            : QStringLiteral("请选择一个栅格数据后再生成8位预览"));
}

void MainWindow::refreshQuickRunButtonState() {
    if (!quickRunButton_) {
        return;
    }
    if (!currentPlugin_) {
        quickRunButton_->setEnabled(false);
        quickRunButton_->setToolTip(QStringLiteral("请先选择一个算法"));
        return;
    }

    const auto specs = currentPlugin_->paramSpecs();
    const auto params = paramWidget_->collectParams();
    const std::string validationMessage = gis::gui::validateExecutionParams(specs, params);
    if (!validationMessage.empty()) {
        quickRunButton_->setEnabled(false);
        quickRunButton_->setToolTip(QString::fromUtf8(validationMessage));
        return;
    }

    const bool ok = gis::gui::canBuildQuickPreviewExecution(specs, params);
    quickRunButton_->setEnabled(ok);
    quickRunButton_->setToolTip(
        ok
            ? QStringLiteral("使用8位小尺寸预览影像快速试算当前算法")
            : QStringLiteral("当前参数里需要至少有一个可生成预览的栅格输入"));

    if (quickRunCheckBox_) {
        quickRunCheckBox_->setEnabled(ok);
        if (!ok) {
            quickRunCheckBox_->setChecked(false);
        }
    }
}

void MainWindow::refreshParamValidationState() {
    if (!paramWidget_ || !currentPlugin_) {
        if (paramWidget_) {
            paramWidget_->setHighlightedParam({});
        }
        if (paramValidationLabel_) {
            paramValidationLabel_->setText(QStringLiteral("请先选择一个算法。"));
            paramValidationLabel_->setStyleSheet(
                "background: #f4f6f8; color: #5f6b7a; border: 1px solid #d5dde5; border-radius: 8px; padding: 8px 10px;");
        }
        return;
    }

    const std::string invalidKey = gis::gui::findFirstInvalidParamKey(
        currentPlugin_->paramSpecs(),
        paramWidget_->collectParams());
    paramWidget_->setHighlightedParam(invalidKey);

    if (!paramValidationLabel_) {
        return;
    }

    if (invalidKey.empty()) {
        paramValidationLabel_->setText(QStringLiteral("当前参数已就绪，可以直接执行或快速试算。"));
        paramValidationLabel_->setStyleSheet(
            "background: #eef6ec; color: #245b2b; border: 1px solid #cfe4cc; border-radius: 8px; padding: 8px 10px;");
        return;
    }

    QString displayName = QString::fromUtf8(invalidKey);
    for (const auto& spec : currentPlugin_->paramSpecs()) {
        if (spec.key == invalidKey) {
            displayName = QString::fromUtf8(spec.displayName.empty() ? spec.key : spec.displayName);
            break;
        }
    }

    paramValidationLabel_->setText(
        QStringLiteral("当前还不能执行，请先检查参数：%1").arg(displayName));
    paramValidationLabel_->setStyleSheet(
        "background: #fff4e8; color: #8a4b12; border: 1px solid #f0cfaa; border-radius: 8px; padding: 8px 10px;");
}

QTreeWidgetItem* MainWindow::selectedDataItem() const {
    if (!dataTree_) {
        return nullptr;
    }

    auto* item = dataTree_->currentItem();
    if (!item || item == inputGroupItem_ || item == outputGroupItem_) {
        return nullptr;
    }
    return item;
}

bool MainWindow::containsPath(const QString& path) const {
    for (auto* group : {inputGroupItem_, outputGroupItem_}) {
        for (int i = 0; i < group->childCount(); ++i) {
            if (group->child(i)->data(0, Qt::UserRole).toString() == path) {
                return true;
            }
        }
    }
    return false;
}

void MainWindow::moveDataItemToRole(QTreeWidgetItem* item, bool isOutput) {
    if (!item) {
        return;
    }

    auto* currentParent = item->parent();
    auto* targetParent = isOutput ? outputGroupItem_ : inputGroupItem_;
    if (currentParent == targetParent) {
        return;
    }

    currentParent->removeChild(item);
    targetParent->addChild(item);
    item->setData(0, Qt::UserRole + 2,
                  static_cast<int>(isOutput ? gis::gui::DataOrigin::Output : gis::gui::DataOrigin::Input));
    targetParent->setExpanded(true);
    dataTree_->setCurrentItem(item);

    syncCurrentDataToParams();
    refreshPreviewCompareTargets();
    refreshDataTreeVisualState();
}

void MainWindow::refreshDataTreeVisualState() {
    auto* activeItem = selectedDataItem();
    for (auto* group : {inputGroupItem_, outputGroupItem_}) {
        for (int i = 0; i < group->childCount(); ++i) {
            auto* item = group->child(i);
            updateDataItemPresentation(item, item == activeItem);
        }
    }
    refreshDataActionButtonsState();
}

void MainWindow::updateDataItemPresentation(QTreeWidgetItem* item, bool isActive) {
    if (!item) {
        return;
    }

    const QString path = item->data(0, Qt::UserRole).toString();
    const auto kind = static_cast<gis::gui::DataKind>(item->data(0, Qt::UserRole + 1).toInt());
    const auto origin = static_cast<gis::gui::DataOrigin>(item->data(0, Qt::UserRole + 2).toInt());

    item->setText(0, QString::fromUtf8(
        gis::gui::buildDataDisplayLabel(path.toStdString(), kind, origin, isActive)));

    QFont font = item->font(0);
    font.setBold(isActive);
    item->setFont(0, font);

    if (isActive) {
        item->setBackground(0, QBrush(QColor("#d6ebff")));
        item->setForeground(0, QBrush(QColor("#0f3d62")));
    } else {
        item->setBackground(0, QBrush());
        item->setForeground(0, QBrush());
    }
}

void MainWindow::refreshDataActionButtonsState() {
    auto* item = selectedDataItem();
    const bool hasItem = item != nullptr;
    const bool canBindInput = hasItem && currentPlugin_ && paramWidget_ && paramWidget_->hasParam("input");
    bool isOutput = false;
    if (hasItem) {
        const auto origin = static_cast<gis::gui::DataOrigin>(item->data(0, Qt::UserRole + 2).toInt());
        isOutput = gis::gui::isOutputDataOrigin(origin);
    }

    if (useAsInputButton_) {
        useAsInputButton_->setEnabled(hasItem && isOutput);
    }
    if (useAsOutputButton_) {
        useAsOutputButton_->setEnabled(hasItem && !isOutput);
    }
    if (bindInputButton_) {
        bindInputButton_->setEnabled(canBindInput);
    }
}
