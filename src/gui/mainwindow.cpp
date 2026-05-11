#include "mainwindow.h"
#include "execute_worker.h"
#include "gdal_config.h"
#include "nav_panel.h"
#include "param_widget.h"
#include "style_constants.h"
#include "gui_data_support.h"
#include "icon_manager.h"
#include "settings_manager.h"
#include "task_manager.h"
#include "task_runner.h"
#include "task_database.h"
#include "task_center_page.h"

#include <gis/core/runtime_env.h>

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QCheckBox>
#include <QLineEdit>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace {

QString formatDuration(qint64 ms) {
    if (ms < 1000) {
        return QStringLiteral("%1 ms").arg(ms);
    }
    double seconds = ms / 1000.0;
    if (seconds < 60) {
        return QStringLiteral("%1 s").arg(seconds, 0, 'f', 1);
    }
    int mins = static_cast<int>(seconds) / 60;
    double secs = seconds - mins * 60;
    return QStringLiteral("%1 m %2 s").arg(mins).arg(secs, 0, 'f', 0);
}

QString actionIconText(const QString& actionKey) {
    return actionKey.isEmpty() ? QStringLiteral("default") : actionKey;
}

QPixmap badgeIconPixmap(const QString& text, const QColor& bg, const QColor& fg, int size = 38) {
    auto& mgr = gis::gui::IconManager::instance();
    std::string key = text.toStdString();
    QPixmap iconPixmap;
    bool hasSvg = false;

    if (mgr.hasPluginIcon(key)) {
        iconPixmap = mgr.pixmapForPlugin(key, static_cast<int>(size * gis::style::Size::kBadgeIconRatio), fg);
        hasSvg = true;
    } else if (mgr.hasActionIcon(key)) {
        iconPixmap = mgr.pixmapForAction(key, static_cast<int>(size * gis::style::Size::kBadgeIconRatio), fg);
        hasSvg = true;
    }

    if (hasSvg) {
        QPixmap result(size, size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(QRectF(0.5, 0.5, size - 1.0, size - 1.0), 8, 8);
        int iconSize = static_cast<int>(size * gis::style::Size::kBadgeIconRatio);
        int offset = (size - iconSize) / 2;
        painter.drawPixmap(offset, offset, iconPixmap);
        return result;
    }

    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawRoundedRect(QRectF(0.5, 0.5, size - 1.0, size - 1.0), 8, 8);
    QPen pen(fg);
    pen.setWidthF(1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.drawEllipse(QRectF(11, 11, 16, 16));
    return pixmap;
}

QIcon executeIcon() {
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);

    QPolygonF triangle;
    triangle << QPointF(4.0, 2.5) << QPointF(13.0, 8.0) << QPointF(4.0, 13.5);
    painter.drawPolygon(triangle);
    return QIcon(pixmap);
}


std::vector<NavPanel::SubFunctionItem> collectSubFunctionItems(
    gis::framework::PluginManager& pluginManager,
    const std::string& selectionKey) {
    std::vector<NavPanel::SubFunctionItem> items;
    auto appendPluginActions = [&](const std::string& pluginName) {
        auto* plugin = pluginManager.find(pluginName);
        if (!plugin) {
            return;
        }
        for (const auto& spec : plugin->paramSpecs()) {
            if (spec.key != "action") {
                continue;
            }
            for (const auto& action : spec.enumValues) {
                items.push_back({
                    pluginName,
                    action,
                    gis::gui::actionDisplayName(pluginName, action).toUtf8().toStdString()
                });
            }
            break;
        }
    };

    if (selectionKey == gis::gui::rasterToolsGroupKey()) {
        for (const auto& pluginName : gis::gui::rasterToolsPluginNames()) {
            appendPluginActions(pluginName);
        }
        return items;
    }

    appendPluginActions(selectionKey);
    return items;
}

std::string resolveGroupedActionPlugin(
    gis::framework::PluginManager& pluginManager,
    const std::string& selectionKey,
    const std::string& actionKey) {
    for (const auto& item : collectSubFunctionItems(pluginManager, selectionKey)) {
        if (item.actionKey == actionKey) {
            return item.pluginName;
        }
    }
    return {};
}

int displayPluginCount(const std::vector<gis::framework::IGisPlugin*>& plugins) {
    std::set<std::string> groups;
    for (const auto* plugin : plugins) {
        groups.insert(gis::gui::displayGroupForPlugin(plugin->name()));
    }
    return static_cast<int>(groups.size());
}



bool isZeroExtent(const std::array<double, 4>& extent) {
    return extent[0] == 0.0 && extent[1] == 0.0 && extent[2] == 0.0 && extent[3] == 0.0;
}

std::string lowerString(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::vector<std::string> splitCommaList(const std::string& text) {
    std::vector<std::string> items;
    std::istringstream iss(text);
    std::string item;
    while (std::getline(iss, item, ',')) {
        const auto begin = item.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) {
            continue;
        }
        const auto end = item.find_last_not_of(" \t\r\n");
        items.push_back(item.substr(begin, end - begin + 1));
    }
    return items;
}

bool endsWithOneOf(const std::string& path, const std::vector<std::string>& suffixes) {
    const std::string lowerPath = lowerString(path);
    for (const auto& suffix : suffixes) {
        if (lowerPath.size() >= suffix.size() &&
            lowerPath.compare(lowerPath.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
        }
    }
    return false;
}

std::optional<std::array<double, 4>> extentParamValue(
    const std::map<std::string, gis::framework::ParamValue>& params,
    const std::string& key) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return std::nullopt;
    }
    if (const auto* arr = std::get_if<std::array<double, 4>>(&it->second)) {
        return *arr;
    }
    return std::nullopt;
}

std::optional<double> doubleParamValue(
    const std::map<std::string, gis::framework::ParamValue>& params,
    const std::string& key) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return std::nullopt;
    }
    if (const auto* value = std::get_if<double>(&it->second)) {
        return *value;
    }
    if (const auto* value = std::get_if<int>(&it->second)) {
        return static_cast<double>(*value);
    }
    return std::nullopt;
}

std::optional<int> intParamValue(
    const std::map<std::string, gis::framework::ParamValue>& params,
    const std::string& key) {
    const auto it = params.find(key);
    if (it == params.end()) {
        return std::nullopt;
    }
    if (const auto* value = std::get_if<int>(&it->second)) {
        return *value;
    }
    return std::nullopt;
}

std::optional<gis::gui::ActionValidationIssue> actionSpecificValidationIssue(
    const std::string& pluginName,
    const std::string& actionKey,
    const std::map<std::string, gis::framework::ParamValue>& params) {
    return gis::gui::validateActionSpecificParams(pluginName, actionKey, params);
}

}

namespace {

void initIconManager() {
    namespace fs = std::filesystem;
    auto exeDir = fs::canonical(fs::path(QApplication::applicationFilePath().toStdWString()).parent_path());
    auto iconsDir = gis::core::findRuntimePathFrom(exeDir, "share/icons");
    if (iconsDir.empty()) {
        iconsDir = exeDir / ".." / ".." / "resources" / "icons";
    }
    gis::gui::IconManager::instance().setIconsBasePath(iconsDir.string());
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    gis::gui::configureGdalRuntime();
    setAcceptDrops(true);
    initIconManager();
    setupUi();

    connect(&TaskRunner::instance(), &TaskRunner::taskProgressChanged,
            this, [this](const QString& /*taskId*/, double percent) {
        const int value = std::clamp(static_cast<int>(percent * 100.0), 0, 100);
        if (statusProgressBar_) {
            statusProgressBar_->setRange(0, 100);
            statusProgressBar_->setValue(value);
        }
    });

    connect(&TaskRunner::instance(), &TaskRunner::taskFinished,
            this, &MainWindow::onTaskRunnerFinished);

    loadPlugins();
}

MainWindow::~MainWindow() = default;

void MainWindow::selectPluginByName(const std::string& pluginName) {
    if (navPanel_) {
        navPanel_->setCurrentPluginSelection(pluginName);
    }
    onPluginSelected(pluginName);
}

void MainWindow::selectActionByKey(const std::string& actionKey) {
    std::string pluginName = currentPlugin_ ? currentPlugin_->name() : std::string{};
    if (pluginName.empty() && !currentDisplayGroupKey_.empty()) {
        pluginName = resolveGroupedActionPlugin(pluginManager_, currentDisplayGroupKey_, actionKey);
    }
    if (pluginName.empty()) {
        return;
    }
    if (navPanel_) {
        navPanel_->setCurrentSubFunctionSelection(pluginName, actionKey);
    }
    onSubFunctionSelected(pluginName, actionKey);
}

QString MainWindow::actionDescription(const std::string& pluginName, const QString& actionKey) {
    return gis::gui::actionDescription(pluginName, actionKey.toStdString());
}

void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("GIS 工具台"));
    resize(gis::style::Size::kWindowDefaultWidth, gis::style::Size::kWindowDefaultHeight);
    setMinimumSize(gis::style::Size::kWindowMinWidth, gis::style::Size::kWindowMinHeight);
    setStyleSheet(gis::style::globalStyleSheet());

    auto* centralWidget = new QWidget;
    setCentralWidget(centralWidget);

    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    navPanel_ = new NavPanel;
    connect(navPanel_, &NavPanel::pluginSelected, this, &MainWindow::onPluginSelected);
    connect(navPanel_, &NavPanel::subFunctionSelected, this, &MainWindow::onSubFunctionSelected);

    auto* rightPanel = new QWidget;
    rightPanel->setObjectName(QStringLiteral("pagePanel"));

    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(20, 20, 20, 18);
    rightLayout->setSpacing(gis::style::Size::kCardSpacing);

    auto* titleCard = new QFrame;
    titleCard->setObjectName(QStringLiteral("heroCard"));
    auto* titleLayout = new QVBoxLayout(titleCard);
    titleLayout->setContentsMargins(22, 20, 22, 20);
    titleLayout->setSpacing(10);

    auto* headerTopLayout = new QHBoxLayout;
    headerTopLayout->setContentsMargins(0, 0, 0, 0);
    headerTopLayout->setSpacing(10);

    auto* heroBadgeLabel = new QLabel(QStringLiteral("算法工作台"));
    heroBadgeLabel->setObjectName(QStringLiteral("heroBadge"));
    headerTopLayout->addWidget(heroBadgeLabel, 0, Qt::AlignLeft);
    headerTopLayout->addStretch();
    titleLayout->addLayout(headerTopLayout);

    auto* heroMainLayout = new QHBoxLayout;
    heroMainLayout->setContentsMargins(0, 0, 0, 0);
    heroMainLayout->setSpacing(12);

    functionIconLabel_ = new QLabel;
    functionIconLabel_->setObjectName(QStringLiteral("heroIconBadge"));
    functionIconLabel_->setAlignment(Qt::AlignCenter);
    functionIconLabel_->setPixmap(badgeIconPixmap(QStringLiteral("default"), QColor("#EAF3FF"), QColor("#2F7CF6")));
    heroMainLayout->addWidget(functionIconLabel_, 0, Qt::AlignTop);

    auto* heroTextLayout = new QVBoxLayout;
    heroTextLayout->setContentsMargins(0, 0, 0, 0);
    heroTextLayout->setSpacing(2);

    functionTitleLabel_ = new QLabel(QStringLiteral("请选择功能"));
    functionTitleLabel_->setObjectName(QStringLiteral("heroTitle"));
    heroTextLayout->addWidget(functionTitleLabel_);

    functionDescLabel_ = new QLabel(QStringLiteral("从左侧选择主功能和子功能后，这里会显示功能说明和参数配置。"));
    functionDescLabel_->setObjectName(QStringLiteral("heroDesc"));
    functionDescLabel_->setWordWrap(true);
    heroTextLayout->addWidget(functionDescLabel_);

    functionMetaLabel_ = new QLabel(QStringLiteral("当前状态：等待选择主功能"));
    functionMetaLabel_->setObjectName(QStringLiteral("heroMeta"));
    heroTextLayout->addWidget(functionMetaLabel_);

    heroMainLayout->addLayout(heroTextLayout, 1);
    titleLayout->addLayout(heroMainLayout);

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
    executionCard->setObjectName(QStringLiteral("execCard"));
    auto* executionLayout = new QVBoxLayout(executionCard);
    executionLayout->setContentsMargins(18, 18, 18, 18);
    executionLayout->setSpacing(12);

    auto* execHeaderLayout = new QHBoxLayout;
    execHeaderLayout->setSpacing(12);

    auto* execTitleLabel = new QLabel(QStringLiteral("执行控制"));
    execTitleLabel->setObjectName(QStringLiteral("cardTitle"));
    execHeaderLayout->addWidget(execTitleLabel);
    execHeaderLayout->addStretch();

    executeButton_ = new QPushButton(QStringLiteral("执行处理"));
    executeButton_->setObjectName(QStringLiteral("primaryButton"));
    executeButton_->setIcon(executeIcon());
    executeButton_->setIconSize(QSize(16, 16));
    executeButton_->setEnabled(false);
    connect(executeButton_, &QPushButton::clicked, this, &MainWindow::onExecute);
    execHeaderLayout->addWidget(executeButton_);

    executionLayout->addLayout(execHeaderLayout);

    auto* batchLayout = new QHBoxLayout;
    batchLayout->setSpacing(8);

    batchCheckBox_ = new QCheckBox(QStringLiteral("批量处理"));
    batchCheckBox_->setObjectName(QStringLiteral("batchCheckBox"));
    batchCheckBox_->setToolTip(QStringLiteral("开启后可选择输入目录，对目录下所有匹配文件执行同一算法"));
    batchLayout->addWidget(batchCheckBox_);

    batchDirEdit_ = new QLineEdit;
    batchDirEdit_->setPlaceholderText(QStringLiteral("输入目录..."));
    batchDirEdit_->setVisible(false);
    batchLayout->addWidget(batchDirEdit_, 1);

    batchDirButton_ = new QPushButton(QStringLiteral("..."));
    batchDirButton_->setFixedWidth(32);
    batchDirButton_->setVisible(false);
    batchLayout->addWidget(batchDirButton_);

    batchFilterEdit_ = new QLineEdit(QStringLiteral("*.tif"));
    batchFilterEdit_->setPlaceholderText(QStringLiteral("文件过滤"));
    batchFilterEdit_->setFixedWidth(80);
    batchFilterEdit_->setVisible(false);
    batchFilterEdit_->setToolTip(QStringLiteral("支持通配符，如 *.tif、*.tif *.img"));
    batchLayout->addWidget(batchFilterEdit_);

    batchCountLabel_ = new QLabel;
    batchCountLabel_->setVisible(false);
    batchLayout->addWidget(batchCountLabel_);

    executionLayout->addLayout(batchLayout);

    connect(batchCheckBox_, &QCheckBox::toggled, this, [this](bool checked) {
        batchDirEdit_->setVisible(checked);
        batchDirButton_->setVisible(checked);
        batchFilterEdit_->setVisible(checked);
        batchCountLabel_->setVisible(checked);
        if (!checked) batchCountLabel_->clear();
        refreshExecuteButtonState();
    });

    connect(batchDirButton_, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择批量输入目录"),
            SettingsManager::instance().lastInputDirectory());
        if (!dir.isEmpty()) {
            batchDirEdit_->setText(dir);
            updateBatchCount();
        }
    });

    connect(batchDirEdit_, &QLineEdit::textChanged, this, [this]() {
        updateBatchCount();
    });

    connect(batchFilterEdit_, &QLineEdit::textChanged, this, [this]() {
        updateBatchCount();
    });

    resultSummaryLabel_ = new QLabel;
    resultSummaryLabel_->setWordWrap(true);
    resultSummaryLabel_->setObjectName(QStringLiteral("execSummary"));
    resultSummaryLabel_->setMinimumHeight(28);
    resultSummaryLabel_->setText(QStringLiteral("当前未执行任务。选择子功能并补全参数后，可以直接开始运行。"));
    executionLayout->addWidget(resultSummaryLabel_);

    rightLayout->addWidget(executionCard);

    tabWidget_ = new QTabWidget;
    tabWidget_->setObjectName(QStringLiteral("pagePanel"));
    tabWidget_->setTabPosition(QTabWidget::North);
    tabWidget_->addTab(rightPanel, QStringLiteral("功能配置"));

    taskCenterPage_ = new TaskCenterPage;
    tabWidget_->addTab(taskCenterPage_, QStringLiteral("任务中心"));

    connect(taskCenterPage_, &TaskCenterPage::rerunTaskRequested,
            this, &MainWindow::onRerunTask);
    connect(taskCenterPage_, &TaskCenterPage::editTaskRequested,
            this, &MainWindow::onEditTask);
    connect(taskCenterPage_, &TaskCenterPage::deleteTasksRequested,
            this, &MainWindow::onDeleteTasks);
    connect(taskCenterPage_, &TaskCenterPage::clearHistoryRequested,
            this, &MainWindow::onClearHistory);
    connect(taskCenterPage_, &TaskCenterPage::clearLogsRequested,
            this, &MainWindow::onClearLogsForTask);
    connect(taskCenterPage_, &TaskCenterPage::clearAllLogsRequested,
            this, &MainWindow::onClearAllLogs);

    connect(&TaskRunner::instance(), &TaskRunner::taskStarted,
            this, [this](const QString& displayGroup, const QString& id) {
        if (displayGroup != taskCenterPage_->currentGroup()) return;
        auto rec = TaskManager::instance().findTask(displayGroup, id);
        if (rec.id.isEmpty()) return;
        taskCenterPage_->addTaskRow(rec.id,
            rec.actionDisplayName.isEmpty() ? rec.actionKey : rec.actionDisplayName,
            static_cast<int>(rec.status),
            rec.startTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    });
    connect(&TaskRunner::instance(), &TaskRunner::taskFinished,
            this, [this](const QString& displayGroup, const QString& id,
                         bool /*success*/, bool /*cancelled*/) {
        if (displayGroup != taskCenterPage_->currentGroup()) return;
        auto rec = TaskManager::instance().findTask(displayGroup, id);
        if (rec.id.isEmpty()) return;
        taskCenterPage_->updateTaskRow(id, static_cast<int>(rec.status),
            rec.endTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
            rec.durationMs);
    });
    connect(&TaskRunner::instance(), &TaskRunner::taskLogMessage,
            taskCenterPage_, &TaskCenterPage::appendLog);
    connect(&TaskRunner::instance(), &TaskRunner::taskProgressChanged,
            this, [this](const QString& taskId, double percent) {
        taskCenterPage_->updateTaskProgress(taskId, percent);
    });

    navPanel_->setFixedWidth(gis::style::Size::kSidebarWidth);

    mainLayout->addWidget(navPanel_);
    mainLayout->addWidget(tabWidget_, 1);

    statusPluginCountLabel_ = new QLabel(QStringLiteral("已加载主功能：0"));
    statusAlgorithmLabel_ = new QLabel(QStringLiteral("当前算法：未选择"));
    statusSubFunctionCountLabel_ = new QLabel(QStringLiteral("已加载子功能：0"));
    statusProgressBar_ = new QProgressBar;
    statusProgressBar_->setRange(0, 100);
    statusProgressBar_->setValue(0);
    statusProgressBar_->setFixedWidth(180);
    statusProgressBar_->setTextVisible(false);
    statusPluginCountLabel_->setObjectName(QStringLiteral("statusBarLabel"));
    statusAlgorithmLabel_->setObjectName(QStringLiteral("statusBarLabel"));
    statusSubFunctionCountLabel_->setObjectName(QStringLiteral("statusBarLabel"));
    statusBar()->addPermanentWidget(statusPluginCountLabel_);
    statusBar()->addPermanentWidget(statusAlgorithmLabel_);
    statusBar()->addPermanentWidget(statusSubFunctionCountLabel_);
    statusBar()->addPermanentWidget(statusProgressBar_);
    statusBar()->showMessage(QStringLiteral("就绪"));
}

void MainWindow::loadPlugins() {
    namespace fs = std::filesystem;

    const auto exePath = fs::canonical(fs::path(QApplication::applicationFilePath().toStdWString()).parent_path());
    const auto pluginsDir = gis::core::findPluginDirectoryFrom(exePath);
    if (!pluginsDir.empty()) {
        pluginManager_.loadFromDirectory(pluginsDir.string());
    }

    static const std::vector<std::string> preferredOrder = {
        "projection", "cutting", "matching", "processing", "raster_math", "raster_inspect", "raster_manage", "raster_render", "georef", "terrain", "classification", "vector", "spindex"
    };

    std::vector<gis::framework::IGisPlugin*> plugins = pluginManager_.plugins();
    plugins.erase(
        std::remove_if(plugins.begin(), plugins.end(), [&](auto* plugin) {
            return std::find(preferredOrder.begin(), preferredOrder.end(), plugin->name()) == preferredOrder.end();
        }),
        plugins.end());
    std::sort(plugins.begin(), plugins.end(), [&](auto* lhs, auto* rhs) {
        const auto leftIt = std::find(preferredOrder.begin(), preferredOrder.end(), lhs->name());
        const auto rightIt = std::find(preferredOrder.begin(), preferredOrder.end(), rhs->name());
        return leftIt < rightIt;
    });

    navPanel_->setPlugins(plugins);

    if (plugins.empty()) {
        statusBar()->showMessage(QStringLiteral("未找到插件，请检查 plugins 目录"));
        if (statusPluginCountLabel_) {
            statusPluginCountLabel_->setText(QStringLiteral("已加载主功能：0"));
        }
        if (statusSubFunctionCountLabel_) {
            statusSubFunctionCountLabel_->setText(QStringLiteral("已加载子功能：0"));
        }
        return;
    }

    if (statusPluginCountLabel_) {
        statusPluginCountLabel_->setText(QStringLiteral("已加载主功能：%1").arg(displayPluginCount(plugins)));
    }
    if (statusSubFunctionCountLabel_) {
        statusSubFunctionCountLabel_->setText(QStringLiteral("已加载子功能：0"));
    }
    statusBar()->showMessage(QStringLiteral("已加载 %1 个插件").arg(plugins.size()));
}

std::vector<gis::framework::ParamSpec> MainWindow::effectiveParamSpecs() const {
    if (!currentPlugin_ || currentActionKey_.isEmpty()) {
        return {};
    }

    const auto actionKey = currentActionKey_.toStdString();
    const auto* config = gis::gui::findActionUiConfig(
        currentPlugin_->name(), actionKey);
    const std::set<std::string> visibleKeys =
        config ? config->visibleKeys : std::set<std::string>{};
    const std::set<std::string> requiredKeys = config ? config->requiredKeys : std::set<std::string>{};
    auto filtered = gis::gui::buildEffectiveGuiParamSpecs(
        currentPlugin_->name(),
        actionKey,
        currentPlugin_->paramSpecs(),
        visibleKeys,
        requiredKeys);
    for (auto& adjustedSpec : filtered) {
        if (const auto* sharedText = gis::gui::findCommonParamText(adjustedSpec.key)) {
            adjustedSpec.displayName = sharedText->displayName.toUtf8().toStdString();
            adjustedSpec.description = sharedText->description.toUtf8().toStdString();
        }
        if (const auto* actionText = gis::gui::findActionSpecificParamText(
                currentPlugin_->name(), actionKey, adjustedSpec.key);
            actionText != nullptr) {
            adjustedSpec.displayName = actionText->displayName.toUtf8().toStdString();
            adjustedSpec.description = actionText->description.toUtf8().toStdString();
        }
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

bool MainWindow::setParamValue(const std::string& key, const std::string& value) {
    if (!paramWidget_ || !paramWidget_->hasParam(key)) {
        return false;
    }
    const bool applied = paramWidget_->setValueFromString(key, value);
    if (applied) {
        syncDerivedParams();
        refreshExecuteButtonState();
        refreshParamValidationState();
    }
    return applied;
}

void MainWindow::triggerExecute() {
    onExecute();
}

bool MainWindow::lastExecutionSuccess() const {
    return lastExecutionSuccess_;
}

bool MainWindow::lastExecutionCancelled() const {
    return lastExecutionCancelled_;
}

QString MainWindow::lastExecutionMessage() const {
    return lastExecutionMessage_;
}

QString MainWindow::lastExecutionRawMessage() const {
    return lastExecutionRawMessage_;
}

void MainWindow::onPluginSelected(const std::string& pluginName) {
    resetDerivedParamTracking();
    currentDisplayGroupKey_ = pluginName;
    currentPlugin_ = pluginManager_.find(pluginName);
    if (!currentPlugin_ && pluginName != gis::gui::rasterToolsGroupKey()) {
        paramWidget_->clear();
        currentActionKey_.clear();
        functionTitleLabel_->setText(QStringLiteral("请选择功能"));
        functionDescLabel_->setText(QStringLiteral("从左侧选择主功能和子功能后，这里会显示功能说明和参数配置。"));
        if (functionIconLabel_) {
            functionIconLabel_->setPixmap(badgeIconPixmap(QStringLiteral("default"), QColor("#EAF3FF"), QColor("#2F7CF6")));
        }
        if (functionMetaLabel_) {
            functionMetaLabel_->setText(QStringLiteral("当前状态：等待选择主功能"));
        }
        if (statusAlgorithmLabel_) {
            statusAlgorithmLabel_->setText(QStringLiteral("当前算法：未选择"));
        }
        if (statusSubFunctionCountLabel_) {
            statusSubFunctionCountLabel_->setText(QStringLiteral("已加载子功能：0"));
        }
        navPanel_->clearSubFunctions();
        refreshExecuteButtonState();
        return;
    }

    const bool isRasterToolsGroup = pluginName == gis::gui::rasterToolsGroupKey();
    const QString groupName = isRasterToolsGroup
        ? QStringLiteral("栅格工具")
        : QString::fromUtf8(currentPlugin_->displayName());

    functionTitleLabel_->setText(groupName);
    functionDescLabel_->setText(isRasterToolsGroup
        ? QStringLiteral("集中提供栅格管理、检查、渲染与波段运算相关子功能。")
        : QString::fromUtf8(currentPlugin_->description()));
    if (functionIconLabel_) {
        functionIconLabel_->setPixmap(badgeIconPixmap(
            isRasterToolsGroup ? QStringLiteral("raster_tools") : QString::fromStdString(currentPlugin_->name()),
            QColor("#EAF3FF"),
            QColor("#2F7CF6")));
    }
    if (functionMetaLabel_) {
        functionMetaLabel_->setText(
            QStringLiteral("当前主功能：%1  |  子功能数：载入中")
                .arg(groupName));
    }
    if (statusAlgorithmLabel_) {
        statusAlgorithmLabel_->setText(
            QStringLiteral("当前算法：%1").arg(groupName));
    }

    const auto items = collectSubFunctionItems(pluginManager_, pluginName);
    navPanel_->setSubFunctions(items);
    navPanel_->setCurrentPluginSelection(pluginName);
    if (statusSubFunctionCountLabel_) {
        statusSubFunctionCountLabel_->setText(
            QStringLiteral("已加载子功能：%1").arg(static_cast<int>(items.size())));
    }
    if (functionMetaLabel_) {
        functionMetaLabel_->setText(
            QStringLiteral("当前主功能：%1  |  子功能数：%2")
                .arg(groupName)
                .arg(static_cast<int>(items.size())));
    }

    currentActionKey_.clear();
    paramWidget_->clear();
    if (resultSummaryLabel_) {
        resultSummaryLabel_->setStyleSheet(QString());
        resultSummaryLabel_->setText(QStringLiteral("请选择该主功能下的子功能，然后补全参数。"));
    }
    refreshExecuteButtonState();
    statusBar()->showMessage(QStringLiteral("当前主功能：%1").arg(groupName));

    if (taskCenterPage_) {
        taskCenterPage_->setCurrentGroup(
            QString::fromStdString(gis::gui::displayGroupForPlugin(pluginName)));
    }
}

void MainWindow::onSubFunctionSelected(const std::string& pluginName,
                                       const std::string& actionKey) {
    currentEditingTaskId_.clear();
    currentPlugin_ = pluginManager_.find(pluginName);
    if (!currentPlugin_) {
        paramWidget_->clear();
        refreshExecuteButtonState();
        return;
    }

    resetDerivedParamTracking();
    currentDisplayGroupKey_ = gis::gui::displayGroupForPlugin(pluginName);
    currentActionKey_ = QString::fromStdString(actionKey);
    navPanel_->setCurrentPluginSelection(pluginName);
    navPanel_->setCurrentSubFunctionSelection(pluginName, actionKey);
    if (functionIconLabel_) {
        functionIconLabel_->setPixmap(badgeIconPixmap(actionIconText(currentActionKey_), QColor("#EAF3FF"), QColor("#2F7CF6")));
    }

    QString displayName = gis::gui::actionDisplayName(
        currentPlugin_->name(), currentActionKey_.toStdString());
    functionTitleLabel_->setText(displayName);

    QString desc = actionDescription(currentPlugin_->name(), currentActionKey_);
    functionDescLabel_->setText(desc.isEmpty()
        ? QString::fromUtf8(currentPlugin_->description()) : desc);
    if (functionMetaLabel_) {
        const QString groupName = currentDisplayGroupKey_ == gis::gui::rasterToolsGroupKey()
            ? QStringLiteral("栅格工具")
            : QString::fromUtf8(currentPlugin_->displayName());
        functionMetaLabel_->setText(
            QStringLiteral("当前主功能：%1  |  当前子功能：%2")
                .arg(groupName)
                .arg(displayName));
    }

    paramWidget_->setUiContext(currentPlugin_->name(), actionKey);
    paramWidget_->setParamSpecs(effectiveParamSpecs());
    if (resultSummaryLabel_) {
        resultSummaryLabel_->setStyleSheet(QString());
        resultSummaryLabel_->setText(QStringLiteral("参数面板已刷新，补全必填项后即可执行当前子功能。"));
    }
    syncDerivedParams();
    refreshExecuteButtonState();
    refreshParamValidationState();

    statusBar()->showMessage(QStringLiteral("当前子功能：%1").arg(displayName));

    if (taskCenterPage_) {
        taskCenterPage_->setCurrentGroup(
            QString::fromStdString(gis::gui::displayGroupForPlugin(pluginName)));
    }
}

void MainWindow::onExecute() {
    if (!currentPlugin_) {
        QMessageBox::warning(this, QStringLiteral("\346\217\220\347\244\272"),
                             QStringLiteral("请先选择一个主功能"));
        return;
    }
    if (currentActionKey_.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("\346\217\220\347\244\272"),
                             QStringLiteral("请先选择一个子功能"));
        return;
    }

    if (batchCheckBox_ && batchCheckBox_->isChecked()) {
        QStringList files = scanBatchFiles();
        if (files.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("批量处理"),
                                 QStringLiteral("未找到匹配的文件，请检查输入目录和过滤条件。"));
            return;
        }

        const auto specs = effectiveParamSpecs();
        auto baseParams = collectExecutionParams();

        std::string inputKey = "input";
        std::string outputKey = "output";
        bool hasOutput = false;
        for (const auto& spec : specs) {
            if (spec.key == "output" || spec.key.find("output") != std::string::npos) {
                outputKey = spec.key;
                hasOutput = true;
                break;
            }
        }

        std::string baseOutput;
        if (hasOutput) {
            auto it = baseParams.find(outputKey);
            if (it != baseParams.end()) {
                baseOutput = std::get<std::string>(it->second);
            }
        }

        int submitted = 0;
        for (const auto& filePath : files) {
            auto params = baseParams;
            params[inputKey] = filePath.toStdString();

            if (hasOutput && !baseOutput.empty()) {
                QFileInfo fi(filePath);
                QString outPath = QString::fromStdString(baseOutput);
                QFileInfo outFi(outPath);
                QString derivedOutput = outFi.dir().filePath(
                    fi.completeBaseName() + QStringLiteral("_%1.%2")
                        .arg(currentActionKey_)
                        .arg(outFi.suffix().isEmpty() ? QStringLiteral("tif") : outFi.suffix()));
                params[outputKey] = derivedOutput.toStdString();
            }

            runPluginWithParams(params, true);
            submitted++;
        }

        resultSummaryLabel_->setStyleSheet(QString());
        resultSummaryLabel_->setText(
            QStringLiteral("批量处理：已提交 %1 个任务到队列").arg(submitted));
        if (tabWidget_) {
            tabWidget_->setCurrentIndex(1);
        }
        return;
    }

    const auto specs = effectiveParamSpecs();
    auto params = collectExecutionParams();
    std::string validationMessage = gis::gui::validateExecutionParams(specs, params);
    if (validationMessage.empty()) {
        if (const auto issue = actionSpecificValidationIssue(
                currentPlugin_->name(), currentActionKey_.toStdString(), params)) {
            validationMessage = issue->message;
        }
    }
    if (!validationMessage.empty()) {
        refreshParamValidationState();
        QMessageBox::warning(this, QStringLiteral("参数未完成"),
                             QString::fromUtf8(validationMessage));
        return;
    }

    if (!currentEditingTaskId_.isEmpty()) {
        TaskManager::instance().updateAndRerunTask(
            taskCenterPage_->currentGroup(), currentEditingTaskId_, params);
        currentEditingTaskId_.clear();
        runPluginWithParams(params);
    } else {
        runPluginWithParams(params);
    }
}

void MainWindow::onParamValuesChanged() {
    if (isSyncingParams_) return;
    syncDerivedParams();
    refreshExecuteButtonState();
    refreshParamValidationState();
}

void MainWindow::refreshExecuteButtonState() {
    if (!executeButton_) return;

    const bool hasSelection = currentPlugin_ && !currentActionKey_.isEmpty();
    std::string validationMessage;
    if (hasSelection) {
        const auto params = collectExecutionParams();
        validationMessage = gis::gui::validateExecutionParams(
            effectiveParamSpecs(),
            params);
        if (validationMessage.empty()) {
            if (const auto issue = actionSpecificValidationIssue(
                    currentPlugin_->name(), currentActionKey_.toStdString(), params)) {
                validationMessage = issue->message;
            }
        }
    }

    const auto state = gis::gui::buildExecuteButtonState(hasSelection, validationMessage);
    executeButton_->setEnabled(state.enabled);

    int queued = TaskRunner::instance().queuedCount();
    bool running = TaskRunner::instance().isRunning();
    if (running && queued > 0) {
        executeButton_->setToolTip(
            QStringLiteral("执行处理（队列中 %1 个任务）").arg(queued));
    } else if (running) {
        executeButton_->setToolTip(QStringLiteral("执行处理（任务运行中，新任务将自动排队）"));
    } else {
        executeButton_->setToolTip(QString::fromUtf8(state.tooltip));
    }
}

void MainWindow::refreshParamValidationState() {
    if (!paramWidget_) {
        return;
    }
    const bool hasSelection = currentPlugin_ && !currentActionKey_.isEmpty();
    if (!hasSelection) {
        if (paramWidget_) {
            paramWidget_->setHighlightedParam({});
        }
        return;
    }

    const auto specs = effectiveParamSpecs();
    const auto params = collectExecutionParams();
    const auto issue = actionSpecificValidationIssue(
        currentPlugin_->name(), currentActionKey_.toStdString(), params);
    paramWidget_->setHighlightedParam(
        gis::gui::resolveHighlightedParamKey(hasSelection, specs, params, issue));
}

void MainWindow::syncDerivedParams() {
    if (!paramWidget_ || !currentPlugin_ || currentActionKey_.isEmpty()) {
        return;
    }

    const auto actionKey = currentActionKey_.toStdString();
    auto params = collectExecutionParams();
    const std::string inputPath = paramWidget_->stringValue("input");
    const std::string referencePath = paramWidget_->stringValue("reference");
    const std::string primaryPath = !inputPath.empty() ? inputPath : referencePath;

    isSyncingParams_ = true;

    auto syncOutputField = [&](const std::string& key, std::string& lastAutoPath) {
        if (!paramWidget_->hasParam(key) || primaryPath.empty()) {
            return;
        }
        const std::string currentValue = paramWidget_->stringValue(key);
        const std::string formatValue = key == "output" ? paramWidget_->stringValue("format") : std::string{};
        const auto update = gis::gui::computeDerivedOutputUpdate(
            currentValue,
            lastAutoPath,
            primaryPath,
            currentPlugin_->name(),
            actionKey,
            key,
            formatValue);
        if (update.shouldApply) {
            paramWidget_->setStringValue(key, update.value);
        }
        lastAutoPath = update.autoValue;
    };

    syncOutputField("output", lastAutoOutputPath_);
    syncOutputField("vector_output", lastAutoVectorOutputPath_);
    syncOutputField("raster_output", lastAutoRasterOutputPath_);

    if (paramWidget_->hasParam("expression") && paramWidget_->hasParam("preset")) {
        const std::string currentExpression = paramWidget_->stringValue("expression");
        const std::string presetKey = paramWidget_->stringValue("preset");
        const auto update = gis::gui::computeDerivedExpressionUpdate(
            currentExpression,
            lastAutoExpressionValue_,
            currentPlugin_->name(),
            actionKey,
            presetKey);
        if (update.shouldApply) {
            paramWidget_->setStringValue("expression", update.value);
        }
        lastAutoExpressionValue_ = update.autoValue;
    }

    if (!inputPath.empty()) {
        const auto info = gis::gui::inspectDataForAutoFill(inputPath);
        const QString inputPathLower = QString::fromStdString(inputPath).toLower();
        const std::string currentLayer = paramWidget_->stringValue("layer");
        if (paramWidget_->hasParam("layer")
            && !info.layerName.empty()
            && !inputPathLower.endsWith(QStringLiteral(".shp"))) {
            if (gis::gui::shouldAutoFillLayerValue(currentLayer, lastAutoLayerName_, info.layerName)) {
                paramWidget_->setStringValue("layer", info.layerName);
            }
            lastAutoLayerName_ = info.layerName;
        }
        if (paramWidget_->hasParam("extent")) {
            const auto extent = extentParamValue(params, "extent");
            if (gis::gui::shouldAutoFillExtentValue(extent, lastAutoExtent_, info.hasExtent)) {
                paramWidget_->setExtentValue("extent", info.extent);
                lastAutoExtent_ = info.extent;
            }
        }
    }

    isSyncingParams_ = false;
}

void MainWindow::resetDerivedParamTracking() {
    lastAutoOutputPath_.clear();
    lastAutoVectorOutputPath_.clear();
    lastAutoRasterOutputPath_.clear();
    lastAutoExpressionValue_.clear();
    lastAutoLayerName_.clear();
    lastAutoExtent_.reset();
}

void MainWindow::runPluginWithParams(
    const std::map<std::string, gis::framework::ParamValue>& params) {
    runPluginWithParams(params, false);
}

void MainWindow::runPluginWithParams(
    const std::map<std::string, gis::framework::ParamValue>& params,
    bool skipOverwritePrompt) {
    lastExecutionSuccess_ = false;
    lastExecutionCancelled_ = false;
    lastExecutionMessage_.clear();
    lastExecutionRawMessage_.clear();

    for (const auto& [key, value] : params) {
        if (key.find("output") == std::string::npos) continue;
        const auto* strVal = std::get_if<std::string>(&value);
        if (!strVal || strVal->empty()) continue;

        QString outputPath = QString::fromStdString(*strVal);
        QFileInfo fi(outputPath);
        QDir dir = fi.absoluteDir();

        if (!dir.exists()) {
            if (!dir.mkpath(QStringLiteral("."))) {
                if (!skipOverwritePrompt) {
                    QMessageBox::warning(this, QStringLiteral("目录创建失败"),
                        QStringLiteral("无法创建输出目录：%1").arg(dir.absolutePath()));
                }
                return;
            }
        }

        if (fi.exists() && !skipOverwritePrompt) {
            auto ret = QMessageBox::question(this, QStringLiteral("文件已存在"),
                QStringLiteral("输出文件已存在，是否覆盖？\n%1").arg(outputPath),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret != QMessageBox::Yes) return;
        }
    }

    const QString displayGroup = QString::fromStdString(
        gis::gui::displayGroupForPlugin(currentPlugin_->name()));
    const QString pluginDisplayName = currentDisplayGroupKey_ == gis::gui::rasterToolsGroupKey()
        ? QStringLiteral("栅格工具")
        : QString::fromUtf8(currentPlugin_->displayName());
    const QString actionDisplayName = gis::gui::actionDisplayName(
        currentPlugin_->name(), currentActionKey_.toStdString());

    auto taskId = TaskRunner::instance().run(
        currentPlugin_, displayGroup,
        QString::fromStdString(currentPlugin_->name()),
        currentActionKey_, params,
        pluginDisplayName, actionDisplayName);

    if (taskId.isEmpty()) return;

    if (resultSummaryLabel_) {
        resultSummaryLabel_->setStyleSheet(QString());
        resultSummaryLabel_->setText(QStringLiteral("正在执行，请稍候..."));
    }
    if (statusProgressBar_) {
        statusProgressBar_->setRange(0, 0);
    }
    executeButton_->setEnabled(false);

    if (tabWidget_) {
        tabWidget_->setCurrentIndex(1);
    }

    pendingResultTaskIds_[taskId] = displayGroup;
}

QStringList MainWindow::scanBatchFiles() const {
    QStringList results;
    if (!batchCheckBox_ || !batchCheckBox_->isChecked()) return results;

    QString dirPath = batchDirEdit_->text().trimmed();
    if (dirPath.isEmpty()) return results;

    QDir dir(dirPath);
    if (!dir.exists()) return results;

    QString filterText = batchFilterEdit_ ? batchFilterEdit_->text().trimmed() : QStringLiteral("*.tif");
    QStringList filters = filterText.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (filters.isEmpty()) filters << QStringLiteral("*.tif");

    QFileInfoList entries = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& fi : entries) {
        results << fi.absoluteFilePath();
    }
    return results;
}

void MainWindow::updateBatchCount() {
    if (!batchCountLabel_) return;
    QStringList files = scanBatchFiles();
    if (files.isEmpty()) {
        batchCountLabel_->setText(QStringLiteral("未找到匹配文件"));
    } else {
        batchCountLabel_->setText(QStringLiteral("匹配 %1 个文件").arg(files.size()));
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (!event->mimeData()->hasUrls()) return;

    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;

    QString filePath = urls.first().toLocalFile();
    if (filePath.isEmpty()) return;

    if (paramWidget_ && paramWidget_->hasParam("input")) {
        paramWidget_->setStringValue("input", filePath.toStdString());
        syncDerivedParams();
        refreshExecuteButtonState();
        refreshParamValidationState();
    }

    event->acceptProposedAction();
}

void MainWindow::onRerunTask(const QString& taskId) {
    auto rec = TaskManager::instance().findTask(taskCenterPage_->currentGroup(), taskId);
    if (rec.id.isEmpty()) return;
    currentPlugin_ = nullptr;
    for (auto* plugin : pluginManager_.plugins()) {
        if (plugin->name() == rec.pluginName.toStdString()) {
            currentPlugin_ = plugin;
            break;
        }
    }
    if (!currentPlugin_) return;

    currentActionKey_ = rec.actionKey;
    currentEditingTaskId_.clear();
    resetDerivedParamTracking();

    runPluginWithParams(rec.params);
}

void MainWindow::onEditTask(const QString& taskId) {
    auto rec = TaskManager::instance().findTask(taskCenterPage_->currentGroup(), taskId);
    if (rec.id.isEmpty()) return;

    selectPluginByName(rec.pluginName.toStdString());
    selectActionByKey(rec.actionKey.toStdString());

    for (const auto& [key, value] : rec.params) {
        std::visit([this, &key](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) {
                paramWidget_->setStringValue(key, v);
            } else if constexpr (std::is_same_v<T, int>) {
                paramWidget_->setStringValue(key, std::to_string(v));
            } else if constexpr (std::is_same_v<T, double>) {
                paramWidget_->setStringValue(key, std::to_string(v));
            } else if constexpr (std::is_same_v<T, bool>) {
                paramWidget_->setStringValue(key, v ? "true" : "false");
            }
        }, value);
    }

    currentEditingTaskId_ = taskId;
    if (tabWidget_) {
        tabWidget_->setCurrentIndex(0);
    }
}

void MainWindow::onDeleteTasks(const QStringList& taskIds) {
    TaskManager::instance().deleteTasks(taskCenterPage_->currentGroup(), taskIds);
    taskCenterPage_->removeTaskRows(taskIds);
}

void MainWindow::onClearHistory() {
    TaskManager::instance().clearHistory(taskCenterPage_->currentGroup());
    taskCenterPage_->refreshAll();
}

void MainWindow::onClearLogsForTask(const QString& taskId) {
    TaskDatabase::instance().clearLogsForTask(taskCenterPage_->currentGroup(), taskId);
    taskCenterPage_->clearLogDisplay();
}

void MainWindow::onClearAllLogs() {
    TaskDatabase::instance().clearAllLogs(taskCenterPage_->currentGroup());
    taskCenterPage_->clearLogDisplay();
}

void MainWindow::onTaskRunnerFinished(const QString& displayGroup,
                                       const QString& taskId,
                                       bool success,
                                       bool cancelled) {
    if (!pendingResultTaskIds_.contains(taskId)) return;
    pendingResultTaskIds_.remove(taskId);

    auto result = TaskManager::instance().findTask(displayGroup, taskId);
    const QString localizedMessage =
        QString::fromUtf8(gis::gui::localizeResultMessage(result.result.message));
    lastExecutionSuccess_ = success;
    lastExecutionCancelled_ = cancelled;
    lastExecutionMessage_ = localizedMessage;
    lastExecutionRawMessage_ = QString::fromUtf8(result.result.message);

    if (success) {
        if (!result.result.outputPath.empty()) {
            SettingsManager::instance().addRecentFile(
                QString::fromUtf8(result.result.outputPath));
        }
        QString summary = QString::fromUtf8(gis::gui::buildResultSummaryText(result.result));
        if (result.durationMs > 0) {
            summary += QStringLiteral("\n耗时: %1").arg(formatDuration(result.durationMs));
        }
        resultSummaryLabel_->setText(
            QStringLiteral("✓ 执行成功\n%1").arg(summary));
        resultSummaryLabel_->setStyleSheet(
            QStringLiteral("color: %1;").arg(gis::style::Color::kSuccess));
        statusBar()->showMessage(QStringLiteral("执行成功"));
    } else if (cancelled) {
        QString cancelText = QStringLiteral("✖ 已取消");
        if (result.durationMs > 0) {
            cancelText += QStringLiteral("\n耗时: %1").arg(formatDuration(result.durationMs));
        }
        resultSummaryLabel_->setText(cancelText);
        resultSummaryLabel_->setStyleSheet(
            QStringLiteral("color: %1;").arg(gis::style::Color::kWarning));
        statusBar()->showMessage(QStringLiteral("执行已取消"));
    } else {
        QString failText = QStringLiteral("✖ 执行失败\n%1").arg(localizedMessage);
        if (result.durationMs > 0) {
            failText += QStringLiteral("\n耗时: %1").arg(formatDuration(result.durationMs));
        }
        resultSummaryLabel_->setText(failText);
        resultSummaryLabel_->setStyleSheet(
            QStringLiteral("color: %1;").arg(gis::style::Color::kError));
        statusBar()->showMessage(QStringLiteral("执行失败：") + localizedMessage);
    }

    if (statusProgressBar_) {
        statusProgressBar_->setRange(0, 100);
        statusProgressBar_->setValue(success ? 100 : 0);
    }
    refreshExecuteButtonState();
    emit executionFinished(success);
}

