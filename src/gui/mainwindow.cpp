#include "mainwindow.h"
#include "param_widget.h"
#include "qt_progress_reporter.h"
#include "execute_worker.h"
#include "progress_dialog.h"
#include <QApplication>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QPushButton>
#include <QLabel>
#include <QStatusBar>
#include <QMessageBox>
#include <QThread>
#include <QScrollArea>
#include <filesystem>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    reporter_ = new QtProgressReporter(this);
    setupUi();
    loadPlugins();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("GIS Tool"));
    resize(900, 600);

    auto* centralWidget = new QWidget;
    setCentralWidget(centralWidget);

    auto* mainLayout = new QHBoxLayout(centralWidget);

    auto* splitter = new QSplitter(Qt::Horizontal);

    pluginList_ = new QListWidget;
    pluginList_->setMaximumWidth(220);
    connect(pluginList_, &QListWidget::currentRowChanged,
            this, &MainWindow::onPluginSelected);
    splitter->addWidget(pluginList_);

    auto* rightPanel = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPanel);

    auto* scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    paramWidget_ = new ParamWidget;
    scrollArea->setWidget(paramWidget_);
    rightLayout->addWidget(scrollArea, 1);

    auto* executeBtn = new QPushButton(QStringLiteral("执行"));
    executeBtn->setMinimumHeight(40);
    executeBtn->setStyleSheet("QPushButton { font-size: 14px; }");
    connect(executeBtn, &QPushButton::clicked, this, &MainWindow::onExecute);
    rightLayout->addWidget(executeBtn);

    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);

    mainLayout->addWidget(splitter);

    statusBar()->showMessage(QStringLiteral("就绪"));
}

void MainWindow::loadPlugins() {
    namespace fs = std::filesystem;

    std::string pluginsDir;
    auto exePath = fs::canonical(fs::path(QApplication::applicationFilePath().toStdWString()).parent_path());
    pluginsDir = (exePath / "plugins").string();

    pluginManager_.loadFromDirectory(pluginsDir);

    pluginList_->clear();
    for (auto* plugin : pluginManager_.plugins()) {
        QString text = QString::fromUtf8(plugin->displayName()) +
                       " (" + QString::fromStdString(plugin->name()) + ")";
        auto* item = new QListWidgetItem(text, pluginList_);
        item->setData(Qt::UserRole, QString::fromStdString(plugin->name()));
    }

    if (pluginManager_.plugins().empty()) {
        statusBar()->showMessage(QStringLiteral("未找到插件，请检查 plugins 目录"));
    } else {
        statusBar()->showMessage(
            QStringLiteral("已加载 %1 个插件").arg(pluginManager_.plugins().size()));
    }
}

void MainWindow::onPluginSelected(int row) {
    if (row < 0) {
        currentPlugin_ = nullptr;
        paramWidget_->clear();
        return;
    }

    auto* item = pluginList_->item(row);
    QString name = item->data(Qt::UserRole).toString();
    currentPlugin_ = pluginManager_.find(name.toStdString());

    if (currentPlugin_) {
        paramWidget_->setParamSpecs(currentPlugin_->paramSpecs());
        statusBar()->showMessage(
            QString::fromUtf8(currentPlugin_->description()));
    }
}

void MainWindow::onExecute() {
    if (!currentPlugin_) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先选择一个插件"));
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
        QString msg = QString::fromUtf8(result.message);
        progressDialog->setFinished(msg, result.success);
        if (result.success) {
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
