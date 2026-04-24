#include "mainwindow.h"

#include "execute_worker.h"
#include "gui_data_support.h"
#include "param_widget.h"
#include "preview_panel.h"
#include "progress_dialog.h"
#include "qt_progress_reporter.h"

#include <QApplication>
#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
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

void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("GIS Tool"));
    resize(1460, 880);

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
        QStringLiteral("顶部选择算法，左侧管理输入/输出数据，中间查看预览，右侧填写参数并执行。"));
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

    auto* dataTitle = new QLabel(QStringLiteral("数据图层"));
    dataTitle->setStyleSheet("font-size: 18px; font-weight: 600;");
    auto* dataHint = new QLabel(
        QStringLiteral("输入数据和算法输出会分组显示。双击可重新定位预览，右键可切换为输入或输出。"));
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
    dataLayout->addWidget(dataTree_, 1);

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
                addDataPath(QString::fromUtf8(result.outputPath), true, true);
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
        addDataPath(path, false, false);
    }
    if (!paths.isEmpty()) {
        dataTree_->setCurrentItem(inputGroupItem_->child(inputGroupItem_->childCount() - 1));
    }
}

void MainWindow::onAddVectorData() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        QStringLiteral("选择矢量数据"),
        QString(),
        QStringLiteral("Vector (*.shp *.geojson *.json *.gpkg *.kml *.csv)"));
    for (const auto& path : paths) {
        addDataPath(path, false, false);
    }
    if (!paths.isEmpty()) {
        dataTree_->setCurrentItem(inputGroupItem_->child(inputGroupItem_->childCount() - 1));
    }
}

void MainWindow::onRemoveSelectedData() {
    auto* item = selectedDataItem();
    if (!item) {
        return;
    }

    delete item;
    if (!selectedDataItem()) {
        previewPanel_->clearPreview();
    }
}

void MainWindow::onDataSelectionChanged() {
    auto* item = selectedDataItem();
    if (!item) {
        previewPanel_->clearPreview();
        return;
    }

    const QString path = item->data(0, Qt::UserRole).toString();
    previewPanel_->showPath(path.toUtf8().constData());
    syncCurrentDataToParams();
}

void MainWindow::onDataItemDoubleClicked(QTreeWidgetItem* item, int) {
    if (!item || item == inputGroupItem_ || item == outputGroupItem_) {
        return;
    }

    dataTree_->setCurrentItem(item);
    previewPanel_->showPath(item->data(0, Qt::UserRole).toString().toUtf8().constData());
    previewPanel_->refitPreview();
}

void MainWindow::showDataContextMenu(const QPoint& pos) {
    auto* item = dataTree_->itemAt(pos);
    if (!item || item == inputGroupItem_ || item == outputGroupItem_) {
        return;
    }

    QMenu menu(this);
    auto* setInputAction = menu.addAction(QStringLiteral("设为输入数据"));
    auto* setOutputAction = menu.addAction(QStringLiteral("设为结果数据"));
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
    } else if (selectedAction == removeAction) {
        delete item;
        if (!selectedDataItem()) {
            previewPanel_->clearPreview();
        }
    }
}

void MainWindow::addDataPath(const QString& path, bool makeCurrent, bool isOutput) {
    if (path.isEmpty() || containsPath(path)) {
        if (makeCurrent && !path.isEmpty()) {
            for (auto* group : {inputGroupItem_, outputGroupItem_}) {
                for (int i = 0; i < group->childCount(); ++i) {
                    if (group->child(i)->data(0, Qt::UserRole).toString() == path) {
                        dataTree_->setCurrentItem(group->child(i));
                        return;
                    }
                }
            }
        }
        return;
    }

    const auto kind = gis::gui::detectDataKind(path.toUtf8().constData());
    auto* parent = isOutput ? outputGroupItem_ : inputGroupItem_;
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, QString::fromUtf8(
        gis::gui::buildDataDisplayLabel(path.toStdString(), kind, isOutput)));
    item->setToolTip(0, path);
    item->setData(0, Qt::UserRole, path);
    item->setData(0, Qt::UserRole + 1, static_cast<int>(kind));
    item->setData(0, Qt::UserRole + 2, isOutput);
    parent->setExpanded(true);

    if (makeCurrent) {
        dataTree_->setCurrentItem(item);
    }
}

void MainWindow::syncCurrentDataToParams() {
    auto* item = selectedDataItem();
    if (!currentPlugin_ || !item) {
        return;
    }

    const bool isOutput = item->data(0, Qt::UserRole + 2).toBool();
    if (isOutput) {
        return;
    }

    const QString path = item->data(0, Qt::UserRole).toString();
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

    const QString path = item->data(0, Qt::UserRole).toString();
    const auto kind = static_cast<gis::gui::DataKind>(item->data(0, Qt::UserRole + 1).toInt());

    currentParent->removeChild(item);
    targetParent->addChild(item);
    item->setText(0, QString::fromUtf8(
        gis::gui::buildDataDisplayLabel(path.toStdString(), kind, isOutput)));
    item->setData(0, Qt::UserRole + 2, isOutput);
    targetParent->setExpanded(true);
    dataTree_->setCurrentItem(item);

    if (!isOutput) {
        syncCurrentDataToParams();
    }
}
