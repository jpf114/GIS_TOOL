#include "mainwindow.h"

#include "execute_worker.h"
#include "nav_panel.h"
#include "param_widget.h"
#include "progress_dialog.h"
#include "qt_progress_reporter.h"
#include "style_constants.h"
#include "gui_data_support.h"

#include <gis/core/runtime_env.h>

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <vector>

namespace {

QString actionDisplayName(const QString& actionKey) {
    static const std::map<QString, QString> kLabels = {
        {QStringLiteral("reproject"), QStringLiteral("\351\207\215\346\212\225\345\275\261")},
        {QStringLiteral("info"), QStringLiteral("\344\277\241\346\201\257\346\237\245\347\234\213")},
        {QStringLiteral("transform"), QStringLiteral("\345\235\220\346\240\207\350\275\254\346\215\242")},
        {QStringLiteral("assign_srs"), QStringLiteral("\350\265\213\344\272\210\345\235\220\346\240\207\347\263\273")},
        {QStringLiteral("clip"), QStringLiteral("\350\243\201\345\210\207")},
        {QStringLiteral("mosaic"), QStringLiteral("\351\225\266\345\265\214")},
        {QStringLiteral("split"), QStringLiteral("\345\210\206\345\235\227")},
        {QStringLiteral("merge_bands"), QStringLiteral("\346\263\242\346\256\265\345\220\210\345\271\266")},
        {QStringLiteral("detect"), QStringLiteral("\347\211\271\345\276\201\346\243\200\346\265\213")},
        {QStringLiteral("match"), QStringLiteral("\347\211\271\345\276\201\345\214\271\351\205\215")},
        {QStringLiteral("register"), QStringLiteral("\345\275\261\345\203\217\351\205\215\345\207\206")},
        {QStringLiteral("change"), QStringLiteral("\345\217\230\345\214\226\346\243\200\346\265\213")},
        {QStringLiteral("ecc_register"), QStringLiteral("ECC \351\205\215\345\207\206")},
        {QStringLiteral("corner"), QStringLiteral("\350\247\222\347\202\271\346\243\200\346\265\213")},
        {QStringLiteral("stitch"), QStringLiteral("\345\233\276\345\203\217\346\213\274\346\216\245")},
        {QStringLiteral("threshold"), QStringLiteral("\351\230\210\345\200\274\345\210\206\345\211\262")},
        {QStringLiteral("filter"), QStringLiteral("\346\273\244\346\263\242")},
        {QStringLiteral("enhance"), QStringLiteral("\345\242\236\345\274\272")},
        {QStringLiteral("band_math"), QStringLiteral("\346\263\242\346\256\265\350\277\220\347\256\227")},
        {QStringLiteral("stats"), QStringLiteral("\347\273\237\350\256\241\344\277\241\346\201\257")},
        {QStringLiteral("edge"), QStringLiteral("\350\276\271\347\274\230\346\243\200\346\265\213")},
        {QStringLiteral("contour"), QStringLiteral("\350\275\256\345\273\223\346\217\220\345\217\226")},
        {QStringLiteral("template_match"), QStringLiteral("\346\250\241\346\235\277\345\214\271\351\205\215")},
        {QStringLiteral("pansharpen"), QStringLiteral("\345\205\250\350\211\262\351\224\220\345\214\226")},
        {QStringLiteral("hough"), QStringLiteral("\351\234\215\345\244\253\345\217\230\346\215\242")},
        {QStringLiteral("watershed"), QStringLiteral("\345\210\206\346\260\264\345\262\255")},
        {QStringLiteral("kmeans"), QStringLiteral("K-Means")},
        {QStringLiteral("overviews"), QStringLiteral("\351\207\221\345\255\227\345\241\224")},
        {QStringLiteral("nodata"), QStringLiteral("NoData \350\256\276\347\275\256")},
        {QStringLiteral("histogram"), QStringLiteral("\347\233\264\346\226\271\345\233\276")},
        {QStringLiteral("colormap"), QStringLiteral("\344\274\252\345\275\251\350\211\262")},
        {QStringLiteral("ndvi"), QStringLiteral("NDVI")},
        {QStringLiteral("buffer"), QStringLiteral("\347\274\223\345\206\262\345\214\272")},
        {QStringLiteral("rasterize"), QStringLiteral("\346\240\205\346\240\274\345\214\226")},
        {QStringLiteral("polygonize"), QStringLiteral("\351\235\242\347\237\242\351\207\217\345\214\226")},
        {QStringLiteral("convert"), QStringLiteral("\346\240\274\345\274\217\350\275\254\346\215\242")},
        {QStringLiteral("union"), QStringLiteral("\345\271\266\351\233\206")},
        {QStringLiteral("difference"), QStringLiteral("\345\267\256\351\233\206")},
        {QStringLiteral("dissolve"), QStringLiteral("\350\236\215\345\220\210")},
        {QStringLiteral("filter"), QStringLiteral("\347\251\272\351\227\264\350\277\207\346\273\244")},
    };

    const auto it = kLabels.find(actionKey);
    if (it != kLabels.end()) {
        return it->second;
    }
    return actionKey;
}

}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    reporter_ = new QtProgressReporter(this);
    setupUi();
    loadPlugins();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("GIS \345\267\245\345\205\267\345\217\260"));
    resize(gis::style::Size::kWindowDefaultWidth, gis::style::Size::kWindowDefaultHeight);
    setMinimumSize(gis::style::Size::kWindowMinWidth, gis::style::Size::kWindowMinHeight);

    auto* centralWidget = new QWidget;
    setCentralWidget(centralWidget);
    centralWidget->setStyleSheet(gis::style::globalStyleSheet());

    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    navPanel_ = new NavPanel;
    connect(navPanel_, &NavPanel::pluginSelected, this, &MainWindow::onPluginSelected);
    connect(navPanel_, &NavPanel::subFunctionSelected, this, &MainWindow::onSubFunctionSelected);

    auto* rightPanel = new QWidget;
    rightPanel->setStyleSheet(QStringLiteral("background: %1;").arg(gis::style::Color::kWindowBg));

    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding);
    rightLayout->setSpacing(gis::style::Size::kCardSpacing);

    auto* titleCard = new QFrame;
    titleCard->setObjectName(QStringLiteral("card"));
    auto* titleLayout = new QVBoxLayout(titleCard);
    titleLayout->setContentsMargins(
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding);
    titleLayout->setSpacing(4);

    functionTitleLabel_ = new QLabel(QStringLiteral("\350\257\267\351\200\211\346\213\251\345\212\237\350\203\275"));
    functionTitleLabel_->setObjectName(QStringLiteral("cardTitle"));
    functionTitleLabel_->setStyleSheet(
        QStringLiteral("font-size: 18px; font-weight: 700; color: %1;").arg(gis::style::Color::kTextPrimary));
    titleLayout->addWidget(functionTitleLabel_);

    functionDescLabel_ = new QLabel(QStringLiteral("\344\273\216\345\267\246\344\276\247\351\200\211\346\213\251\346\217\222\344\273\266\345\222\214\345\255\220\345\212\237\350\203\275\345\220\216\357\274\214\350\277\231\351\207\214\344\274\232\346\230\276\347\244\272\345\212\237\350\203\275\350\257\264\346\230\216\345\222\214\345\217\202\346\225\260\351\205\215\347\275\256\343\200\202"));
    functionDescLabel_->setObjectName(QStringLiteral("cardDesc"));
    functionDescLabel_->setWordWrap(true);
    titleLayout->addWidget(functionDescLabel_);

    rightLayout->addWidget(titleCard);

    auto* paramScrollArea = new QScrollArea;
    paramScrollArea->setWidgetResizable(true);
    paramScrollArea->setFrameShape(QFrame::NoFrame);
    paramScrollArea->setStyleSheet(
        QStringLiteral("QScrollArea { background: transparent; border: none; }"));

    paramWidget_ = new ParamWidget;
    connect(paramWidget_, &ParamWidget::paramsChanged, this, &MainWindow::onParamValuesChanged);
    paramScrollArea->setWidget(paramWidget_);

    rightLayout->addWidget(paramScrollArea, 1);

    auto* executionCard = new QFrame;
    executionCard->setObjectName(QStringLiteral("card"));
    auto* executionLayout = new QVBoxLayout(executionCard);
    executionLayout->setContentsMargins(
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding,
        gis::style::Size::kCardPadding);
    executionLayout->setSpacing(10);

    auto* execHeaderLayout = new QHBoxLayout;
    execHeaderLayout->setSpacing(12);

    auto* execIconLabel = new QLabel(QStringLiteral("\342\226\266"));
    execIconLabel->setFixedSize(20, 20);
    execHeaderLayout->addWidget(execIconLabel);

    auto* execTitleLabel = new QLabel(QStringLiteral("\346\211\247\350\241\214\346\216\247\345\210\266"));
    execTitleLabel->setObjectName(QStringLiteral("cardTitle"));
    execHeaderLayout->addWidget(execTitleLabel);
    execHeaderLayout->addStretch();

    executeButton_ = new QPushButton(QStringLiteral("\346\211\247\350\241\214"));
    executeButton_->setObjectName(QStringLiteral("primaryButton"));
    executeButton_->setEnabled(false);
    connect(executeButton_, &QPushButton::clicked, this, &MainWindow::onExecute);
    execHeaderLayout->addWidget(executeButton_);

    executionLayout->addLayout(execHeaderLayout);

    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(
        QStringLiteral("background: %1; max-height: 1px;").arg(gis::style::Color::kDivider));
    executionLayout->addWidget(separator);

    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setTextVisible(true);
    progressBar_->setFormat(QStringLiteral("%p%"));
    executionLayout->addWidget(progressBar_);

    resultSummaryLabel_ = new QLabel;
    resultSummaryLabel_->setWordWrap(true);
    resultSummaryLabel_->setObjectName(QStringLiteral("cardDesc"));
    resultSummaryLabel_->setMinimumHeight(24);
    resultSummaryLabel_->setText(QStringLiteral("\347\255\211\345\276\205\346\211\247\350\241\214..."));
    executionLayout->addWidget(resultSummaryLabel_);

    rightLayout->addWidget(executionCard);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);
    splitter->addWidget(navPanel_);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({gis::style::Size::kSidebarWidth, gis::style::Size::kWindowDefaultWidth - gis::style::Size::kSidebarWidth});

    mainLayout->addWidget(splitter);

    statusAlgorithmLabel_ = new QLabel(QStringLiteral("\345\275\223\345\211\215\347\256\227\346\263\225\357\274\232\346\234\252\351\200\211\346\213\251"));
    statusPluginCountLabel_ = new QLabel(QStringLiteral("\346\222\255\344\273\266\346\225\260\357\274\2320"));
    statusExecutionLabel_ = new QLabel(QStringLiteral("\346\211\247\350\241\214\347\212\266\346\200\201\357\274\232\345\260\261\347\273\252"));
    statusProgressBar_ = new QProgressBar;
    statusProgressBar_->setRange(0, 100);
    statusProgressBar_->setValue(0);
    statusProgressBar_->setFixedWidth(180);
    statusProgressBar_->setTextVisible(false);
    statusBar()->addPermanentWidget(statusAlgorithmLabel_);
    statusBar()->addPermanentWidget(statusPluginCountLabel_);
    statusBar()->addPermanentWidget(statusExecutionLabel_);
    statusBar()->addPermanentWidget(statusProgressBar_);
    statusBar()->showMessage(QStringLiteral("\345\260\261\347\273\252"));
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

    std::vector<gis::framework::IGisPlugin*> plugins = pluginManager_.plugins();
    std::sort(plugins.begin(), plugins.end(), [&](auto* lhs, auto* rhs) {
        const auto leftIt = std::find(preferredOrder.begin(), preferredOrder.end(), lhs->name());
        const auto rightIt = std::find(preferredOrder.begin(), preferredOrder.end(), rhs->name());
        return leftIt < rightIt;
    });

    navPanel_->setPlugins(plugins);

    if (plugins.empty()) {
        statusBar()->showMessage(QStringLiteral("\346\234\252\346\211\276\345\210\260\346\222\255\344\273\266\357\274\214\350\257\267\346\243\200\346\237\245 plugins \347\233\256\345\275\225"));
        if (statusPluginCountLabel_) {
            statusPluginCountLabel_->setText(QStringLiteral("\346\222\255\344\273\266\346\225\260\357\274\2320"));
        }
        return;
    }

    if (statusPluginCountLabel_) {
        statusPluginCountLabel_->setText(QStringLiteral("\346\222\255\344\273\266\346\225\260\357\274\232%1").arg(plugins.size()));
    }
    statusBar()->showMessage(QStringLiteral("\345\267\262\345\212\240\350\275\275 %1 \344\270\252\347\256\227\346\263\225\346\222\255\344\273\266").arg(plugins.size()));
}

std::vector<gis::framework::ParamSpec> MainWindow::effectiveParamSpecs() const {
    if (!currentPlugin_) {
        return {};
    }

    std::vector<gis::framework::ParamSpec> filtered;
    for (const auto& spec : currentPlugin_->paramSpecs()) {
        if (spec.key == "action") {
            continue;
        }
        filtered.push_back(spec);
    }
    return filtered;
}

std::map<std::string, gis::framework::ParamValue> MainWindow::collectExecutionParams() const {
    auto params = paramWidget_ ? paramWidget_->collectParams() : std::map<std::string, gis::framework::ParamValue>{};
    if (!currentActionKey_.isEmpty()) {
        params["action"] = currentActionKey_.toStdString();
    }
    return params;
}

void MainWindow::onPluginSelected(const std::string& pluginName) {
    currentPlugin_ = pluginManager_.find(pluginName);
    if (!currentPlugin_) {
        paramWidget_->clear();
        currentActionKey_.clear();
        functionTitleLabel_->setText(QStringLiteral("\350\257\267\351\200\211\346\213\251\345\212\237\350\203\275"));
        functionDescLabel_->setText(QStringLiteral("\344\273\216\345\267\246\344\276\247\351\200\211\346\213\251\346\217\222\344\273\266\345\222\214\345\255\220\345\212\237\350\203\275\345\220\216\357\274\214\350\277\231\351\207\214\344\274\232\346\230\276\347\244\272\345\212\237\350\203\275\350\257\264\346\230\216\345\222\214\345\217\202\346\225\260\351\205\215\347\275\256\343\200\202"));
        if (statusAlgorithmLabel_) {
            statusAlgorithmLabel_->setText(QStringLiteral("\345\275\223\345\211\215\347\256\227\346\263\225\357\274\232\346\234\252\351\200\211\346\213\251"));
        }
        navPanel_->clearSubFunctions();
        refreshExecuteButtonState();
        return;
    }

    functionTitleLabel_->setText(QString::fromUtf8(currentPlugin_->displayName()));
    functionDescLabel_->setText(QString::fromUtf8(currentPlugin_->description()));
    if (statusAlgorithmLabel_) {
        statusAlgorithmLabel_->setText(
            QStringLiteral("\345\275\223\345\211\215\347\256\227\346\263\225\357\274\232%1").arg(QString::fromUtf8(currentPlugin_->displayName())));
    }

    std::vector<std::string> actions;
    std::vector<std::string> displayNames;
    for (const auto& spec : currentPlugin_->paramSpecs()) {
        if (spec.key != "action") continue;
        for (const auto& action : spec.enumValues) {
            actions.push_back(action);
            displayNames.push_back(actionDisplayName(QString::fromStdString(action)).toStdString());
        }
        break;
    }
    navPanel_->setSubFunctions(actions, displayNames);

    currentActionKey_.clear();
    paramWidget_->clear();
    refreshExecuteButtonState();
    statusBar()->showMessage(QStringLiteral("\345\275\223\345\211\215\344\270\273\345\212\237\350\203\275: %1").arg(QString::fromUtf8(currentPlugin_->displayName())));
}

void MainWindow::onSubFunctionSelected(const std::string& actionKey) {
    if (!currentPlugin_) {
        paramWidget_->clear();
        refreshExecuteButtonState();
        return;
    }

    currentActionKey_ = QString::fromStdString(actionKey);

    QString displayName = actionDisplayName(currentActionKey_);
    functionTitleLabel_->setText(
        QString::fromUtf8(currentPlugin_->displayName()) + QStringLiteral(" \342\206\222 ") + displayName);
    functionDescLabel_->setText(
        QString::fromUtf8(currentPlugin_->description()));

    paramWidget_->setParamSpecs(effectiveParamSpecs());
    refreshExecuteButtonState();
    refreshParamValidationState();

    if (statusExecutionLabel_) {
        statusExecutionLabel_->setText(
            QStringLiteral("\346\211\247\350\241\214\347\212\266\346\200\201\357\274\232\345\267\262\351\200\211\346\213\251 %1").arg(displayName));
    }
    statusBar()->showMessage(QStringLiteral("\345\275\223\345\211\215\345\255\220\345\212\237\350\203\275: %1").arg(displayName));
}

void MainWindow::onExecute() {
    if (!currentPlugin_) {
        QMessageBox::warning(this, QStringLiteral("\346\217\220\347\244\272"),
                             QStringLiteral("\350\257\267\345\205\210\351\200\211\346\213\251\344\270\200\344\270\252\347\256\227\346\263\225"));
        return;
    }
    if (currentActionKey_.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("\346\217\220\347\244\272"),
                             QStringLiteral("\350\257\267\345\205\210\351\200\211\346\213\251\344\270\200\344\270\252\345\255\220\345\212\237\350\203\275"));
        return;
    }

    const auto specs = currentPlugin_->paramSpecs();
    auto params = collectExecutionParams();
    const std::string validationMessage = gis::gui::validateExecutionParams(specs, params);
    if (!validationMessage.empty()) {
        refreshParamValidationState();
        QMessageBox::warning(this, QStringLiteral("\345\217\202\346\225\260\344\270\215\345\256\214\346\225\264"),
                             QString::fromUtf8(validationMessage));
        return;
    }

    runPluginWithParams(params);
}

void MainWindow::onParamValuesChanged() {
    if (isSyncingParams_) return;
    refreshExecuteButtonState();
    refreshParamValidationState();
}

void MainWindow::refreshExecuteButtonState() {
    if (!executeButton_) return;

    if (!currentPlugin_ || currentActionKey_.isEmpty()) {
        executeButton_->setEnabled(false);
        executeButton_->setToolTip(QStringLiteral("\350\257\267\345\205\210\351\200\211\346\213\251\344\270\273\345\212\237\350\203\275\345\222\214\345\255\220\345\212\237\350\203\275"));
        return;
    }

    const std::string validationMessage = gis::gui::validateExecutionParams(
        currentPlugin_->paramSpecs(),
        collectExecutionParams());
    if (!validationMessage.empty()) {
        executeButton_->setEnabled(false);
        executeButton_->setToolTip(QString::fromUtf8(validationMessage));
        return;
    }

    executeButton_->setEnabled(true);
    executeButton_->setToolTip(QStringLiteral("\345\217\202\346\225\260\345\267\262\345\260\261\347\273\252\357\274\214\345\217\257\346\211\247\350\241\214\345\275\223\345\211\215\347\256\227\346\263\225"));
}

void MainWindow::refreshParamValidationState() {
    if (!paramWidget_ || !currentPlugin_ || currentActionKey_.isEmpty()) {
        if (paramWidget_) {
            paramWidget_->setHighlightedParam({});
        }
        return;
    }

    const std::string invalidKey = gis::gui::findFirstInvalidParamKey(
        currentPlugin_->paramSpecs(),
        collectExecutionParams());
    paramWidget_->setHighlightedParam(invalidKey);
}

void MainWindow::runPluginWithParams(
    const std::map<std::string, gis::framework::ParamValue>& params) {
    reporter_->reset();
    if (statusExecutionLabel_) {
        statusExecutionLabel_->setText(QStringLiteral("\346\211\247\350\241\214\347\212\266\346\200\201\357\274\232\350\277\220\350\241\214\344\270\255"));
    }
    if (progressBar_) {
        progressBar_->setRange(0, 0);
        progressBar_->setFormat(QStringLiteral("\345\244\204\347\220\206\344\270\255..."));
    }
    if (statusProgressBar_) {
        statusProgressBar_->setRange(0, 0);
    }
    executeButton_->setEnabled(false);

    auto* worker = new ExecuteWorker;
    worker->setup(currentPlugin_, params, reporter_);

    auto* thread = new QThread;
    worker->moveToThread(thread);

    auto* progressDialog = new ProgressDialog(reporter_);

    connect(thread, &QThread::started, worker, &ExecuteWorker::run);
    connect(worker, &ExecuteWorker::finished, this,
            [this, progressDialog](const gis::framework::Result& result) {
                QString message = QString::fromUtf8(result.message);
                const bool cancelled = result.message == "\345\267\262\345\217\226\346\266\210\346\211\247\350\241\214";
                progressDialog->setFinished(message, result.success, cancelled);

                if (result.success) {
                    QString summary = QString::fromUtf8(gis::gui::buildResultSummaryText(result));
                    resultSummaryLabel_->setText(
                        QStringLiteral("\342\234\205 \346\211\247\350\241\214\346\210\220\345\212\237\n%1").arg(summary));
                    resultSummaryLabel_->setStyleSheet(
                        QStringLiteral("color: %1;").arg(gis::style::Color::kSuccess));
                    if (progressBar_) {
                        progressBar_->setRange(0, 100);
                        progressBar_->setValue(100);
                        progressBar_->setFormat(QStringLiteral("\345\256\214\346\210\220 100%"));
                    }
                    if (statusExecutionLabel_) {
                        statusExecutionLabel_->setText(QStringLiteral("\346\211\247\350\241\214\347\212\266\346\200\201\357\274\232\346\210\220\345\212\237"));
                    }
                    statusBar()->showMessage(QStringLiteral("\346\211\247\350\241\214\346\210\220\345\212\237: ") + message);
                } else if (cancelled) {
                    resultSummaryLabel_->setText(QStringLiteral("\342\234\226 \345\267\262\345\217\226\346\266\210"));
                    resultSummaryLabel_->setStyleSheet(
                        QStringLiteral("color: %1;").arg(gis::style::Color::kWarning));
                    if (progressBar_) {
                        progressBar_->setRange(0, 100);
                        progressBar_->setValue(0);
                        progressBar_->setFormat(QStringLiteral("\345\267\262\345\217\226\346\266\210"));
                    }
                    if (statusExecutionLabel_) {
                        statusExecutionLabel_->setText(QStringLiteral("\346\211\247\350\241\214\347\212\266\346\200\201\357\274\232\345\267\262\345\217\226\346\266\210"));
                    }
                    statusBar()->showMessage(QStringLiteral("\346\211\247\350\241\214\345\267\262\345\217\226\346\266\210"));
                } else {
                    resultSummaryLabel_->setText(
                        QStringLiteral("\342\235\214 \346\211\247\350\241\214\345\244\261\350\264\245\n%1").arg(message));
                    resultSummaryLabel_->setStyleSheet(
                        QStringLiteral("color: %1;").arg(gis::style::Color::kError));
                    if (progressBar_) {
                        progressBar_->setRange(0, 100);
                        progressBar_->setValue(0);
                        progressBar_->setFormat(QStringLiteral("\345\244\261\350\264\245"));
                    }
                    if (statusExecutionLabel_) {
                        statusExecutionLabel_->setText(QStringLiteral("\346\211\247\350\241\214\347\212\266\346\200\201\357\274\232\345\244\261\350\264\245"));
                    }
                    statusBar()->showMessage(QStringLiteral("\346\211\247\350\241\214\345\244\261\350\264\245: ") + message);
                }

                if (statusProgressBar_) {
                    statusProgressBar_->setRange(0, 100);
                    statusProgressBar_->setValue(result.success ? 100 : 0);
                }
                refreshExecuteButtonState();
            });
    connect(worker, &ExecuteWorker::finished, thread, &QThread::quit);
    connect(worker, &ExecuteWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(progressDialog, &QDialog::finished, progressDialog, &QObject::deleteLater);

    thread->start();
    progressDialog->exec();
}
