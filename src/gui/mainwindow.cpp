#include "mainwindow.h"

#include "execute_worker.h"
#include "gui_data_support.h"
#include "param_widget.h"
#include "preview_panel.h"
#include "progress_dialog.h"
#include "qt_progress_reporter.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QThread>
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

void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("GIS Tool"));
    resize(1440, 860);

    auto* centralWidget = new QWidget;
    setCentralWidget(centralWidget);

    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto* algorithmFrame = new QFrame;
    algorithmFrame->setObjectName(QStringLiteral("algorithmFrame"));
    auto* algorithmLayout = new QVBoxLayout(algorithmFrame);
    algorithmLayout->setContentsMargins(16, 12, 16, 12);
    algorithmLayout->setSpacing(8);

    auto* algorithmTitle = new QLabel(QStringLiteral("算法工作台"));
    algorithmTitle->setStyleSheet("font-size: 20px; font-weight: 700;");
    auto* algorithmSubtitle = new QLabel(
        QStringLiteral("顶部选择算法，左侧管理数据，中间查看预览，右侧填写参数并执行。"));
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

    algorithmLayout->addWidget(algorithmTitle);
    algorithmLayout->addWidget(algorithmSubtitle);
    algorithmLayout->addWidget(pluginTabs_);

    auto* workspaceSplitter = new QSplitter(Qt::Horizontal);

    auto* dataPanel = new QFrame;
    dataPanel->setObjectName(QStringLiteral("dataPanel"));
    auto* dataLayout = new QVBoxLayout(dataPanel);
    dataLayout->setContentsMargins(14, 14, 14, 14);
    dataLayout->setSpacing(10);

    auto* dataTitle = new QLabel(QStringLiteral("数据"));
    dataTitle->setStyleSheet("font-size: 18px; font-weight: 600;");
    auto* dataHint = new QLabel(
        QStringLiteral("添加栅格或矢量后，选中即可预览，并自动回填到当前算法的输入参数。"));
    dataHint->setWordWrap(true);
    dataHint->setStyleSheet("color: #5f6b7a;");

    auto* dataButtonLayout = new QHBoxLayout;
    auto* addRasterBtn = new QPushButton(QStringLiteral("添加栅格"));
    auto* addVectorBtn = new QPushButton(QStringLiteral("添加矢量"));
    auto* removeDataBtn = new QPushButton(QStringLiteral("移除"));
    connect(addRasterBtn, &QPushButton::clicked, this, &MainWindow::onAddRasterData);
    connect(addVectorBtn, &QPushButton::clicked, this, &MainWindow::onAddVectorData);
    connect(removeDataBtn, &QPushButton::clicked, this, &MainWindow::onRemoveSelectedData);
    dataButtonLayout->addWidget(addRasterBtn);
    dataButtonLayout->addWidget(addVectorBtn);
    dataButtonLayout->addWidget(removeDataBtn);

    dataList_ = new QListWidget;
    dataList_->setAlternatingRowColors(true);
    connect(dataList_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem*, QListWidgetItem*) { onDataSelectionChanged(); });

    dataLayout->addWidget(dataTitle);
    dataLayout->addWidget(dataHint);
    dataLayout->addLayout(dataButtonLayout);
    dataLayout->addWidget(dataList_, 1);

    previewPanel_ = new PreviewPanel;

    auto* rightPanel = new QFrame;
    rightPanel->setObjectName(QStringLiteral("rightPanel"));
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(14, 14, 14, 14);
    rightLayout->setSpacing(10);

    pluginTitleLabel_ = new QLabel(QStringLiteral("请选择算法"));
    pluginTitleLabel_->setStyleSheet("font-size: 18px; font-weight: 700;");
    pluginDescriptionLabel_ = new QLabel(QStringLiteral("当前算法说明会显示在这里。"));
    pluginDescriptionLabel_->setWordWrap(true);
    pluginDescriptionLabel_->setStyleSheet("color: #5f6b7a;");

    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    paramWidget_ = new ParamWidget;
    scrollArea->setWidget(paramWidget_);

    auto* executeBtn = new QPushButton(QStringLiteral("执行当前算法"));
    executeBtn->setMinimumHeight(40);
    executeBtn->setStyleSheet(
        "QPushButton { background: #1f5f8b; color: white; border-radius: 8px; font-size: 14px; }"
        "QPushButton:hover { background: #194c6f; }");
    connect(executeBtn, &QPushButton::clicked, this, &MainWindow::onExecute);

    auto* resultGroup = new QGroupBox(QStringLiteral("执行结果"));
    auto* resultLayout = new QVBoxLayout(resultGroup);
    resultSummaryLabel_ = new QLabel(QStringLiteral("执行后会在这里展示结果摘要。"));
    resultSummaryLabel_->setWordWrap(true);
    resultLayout->addWidget(resultSummaryLabel_);

    rightLayout->addWidget(pluginTitleLabel_);
    rightLayout->addWidget(pluginDescriptionLabel_);
    rightLayout->addWidget(scrollArea, 1);
    rightLayout->addWidget(executeBtn);
    rightLayout->addWidget(resultGroup);

    workspaceSplitter->addWidget(dataPanel);
    workspaceSplitter->addWidget(previewPanel_);
    workspaceSplitter->addWidget(rightPanel);
    workspaceSplitter->setStretchFactor(0, 2);
    workspaceSplitter->setStretchFactor(1, 5);
    workspaceSplitter->setStretchFactor(2, 3);

    mainLayout->addWidget(algorithmFrame);
    mainLayout->addWidget(workspaceSplitter, 1);

    centralWidget->setStyleSheet(
        "QFrame#algorithmFrame, QFrame#dataPanel, QFrame#rightPanel {"
        "  background: #f8fbfd;"
        "  border: 1px solid #dce6ee;"
        "  border-radius: 10px;"
        "}");

    statusBar()->showMessage(QStringLiteral("就绪"));
}

void MainWindow::loadPlugins() {
    namespace fs = std::filesystem;

    std::string pluginsDir;
    auto exePath = fs::canonical(fs::path(QApplication::applicationFilePath().toStdWString()).parent_path());
    pluginsDir = (exePath / "plugins").string();

    pluginManager_.loadFromDirectory(pluginsDir);

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
        int index = pluginTabs_->addTab(QString::fromUtf8(plugin->displayName()));
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
}

void MainWindow::onExecute() {
    if (!currentPlugin_) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先选择一个算法"));
        return;
    }

    auto params = paramWidget_->collectParams();

    reporter_->reset();

    auto* worker = new ExecuteWorker;
    worker->setup(currentPlugin_, params, reporter_);

    auto* thread = new QThread;
    worker->moveToThread(thread);

    auto* progressDialog = new ProgressDialog(reporter_);

    connect(thread, &QThread::started, worker, &ExecuteWorker::run);
    connect(worker, &ExecuteWorker::finished, this, [this, progressDialog](const gis::framework::Result& result) {
        const QString msg = QString::fromUtf8(result.message);
        progressDialog->setFinished(msg, result.success);
        resultSummaryLabel_->setText(buildResultSummary(result));

        if (result.success) {
            if (!result.outputPath.empty() && std::filesystem::exists(result.outputPath)) {
                addDataPath(QString::fromUtf8(result.outputPath), true);
            }
            statusBar()->showMessage(QStringLiteral("执行成功: ") + msg);
        } else {
            statusBar()->showMessage(QStringLiteral("执行失败: ") + msg);
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
    for (const auto& path : paths) {
        addDataPath(path, false);
    }
    if (!paths.isEmpty()) {
        dataList_->setCurrentRow(dataList_->count() - 1);
    }
}

void MainWindow::onAddVectorData() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("选择矢量数据"),
        QString(),
        QStringLiteral("Vector (*.shp *.geojson *.json *.gpkg *.kml *.csv)"));
    for (const auto& path : paths) {
        addDataPath(path, false);
    }
    if (!paths.isEmpty()) {
        dataList_->setCurrentRow(dataList_->count() - 1);
    }
}

void MainWindow::onRemoveSelectedData() {
    delete dataList_->takeItem(dataList_->currentRow());
    if (dataList_->count() == 0) {
        previewPanel_->clearPreview();
    }
}

void MainWindow::onDataSelectionChanged() {
    auto* item = dataList_->currentItem();
    if (!item) {
        previewPanel_->clearPreview();
        return;
    }

    const QString path = item->data(Qt::UserRole).toString();
    previewPanel_->showPath(path.toUtf8().constData());
    syncCurrentDataToParams();
}

void MainWindow::addDataPath(const QString& path, bool makeCurrent) {
    if (path.isEmpty()) {
        return;
    }

    for (int i = 0; i < dataList_->count(); ++i) {
        if (dataList_->item(i)->data(Qt::UserRole).toString() == path) {
            if (makeCurrent) {
                dataList_->setCurrentRow(i);
            }
            return;
        }
    }

    const auto kind = gis::gui::detectDataKind(path.toUtf8().constData());
    const QString displayText = QStringLiteral("[%1] %2")
        .arg(QString::fromUtf8(gis::gui::dataKindDisplayName(kind)))
        .arg(QFileInfo(path).fileName());

    auto* item = new QListWidgetItem(displayText, dataList_);
    item->setToolTip(path);
    item->setData(Qt::UserRole, path);
    item->setData(Qt::UserRole + 1, static_cast<int>(kind));

    if (makeCurrent) {
        dataList_->setCurrentItem(item);
    }
}

void MainWindow::syncCurrentDataToParams() {
    if (!currentPlugin_ || !dataList_->currentItem()) {
        return;
    }

    const QString path = dataList_->currentItem()->data(Qt::UserRole).toString();
    if (paramWidget_->hasParam("input")) {
        paramWidget_->setStringValue("input", path.toUtf8().constData());
    }
}

QString MainWindow::buildResultSummary(const gis::framework::Result& result) const {
    QString summary = result.success ? QStringLiteral("状态: 成功\n")
                                     : QStringLiteral("状态: 失败\n");
    summary += QStringLiteral("消息: ") + QString::fromUtf8(result.message);

    if (!result.outputPath.empty()) {
        summary += QStringLiteral("\n输出: ") + QString::fromUtf8(result.outputPath);
    }

    if (!result.metadata.empty()) {
        summary += QStringLiteral("\n元数据:");
        for (const auto& [key, value] : result.metadata) {
            summary += QStringLiteral("\n- ")
                + QString::fromStdString(key)
                + QStringLiteral(": ")
                + QString::fromStdString(value);
        }
    }

    return summary;
}
