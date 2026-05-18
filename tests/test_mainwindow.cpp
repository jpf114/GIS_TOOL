#include <gtest/gtest.h>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <QCoreApplication>
#include <QLabel>
#include <QMetaObject>
#include <QProgressBar>
#include <QSettings>
#include <QStatusBar>
#include <QTextEdit>
#include <QTreeWidget>

#define private public
#include "../src/gui/mainwindow.h"
#undef private

#include "../src/gui/param_widget.h"
#include "../src/gui/task_center_page.h"
#include "../src/gui/task_runner.h"
#include "../src/gui/task_manager.h"
#include "../src/gui/task_database.h"
#include "../src/gui/welcome_dialog.h"
#include "../src/gui/settings_manager.h"
#include "test_support.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {

fs::path mainWindowTestDir() {
    return gis::tests::defaultTestOutputDir("test_mainwindow_output");
}

QString uniqueMainWindowGroup(const char* suffix) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return QStringLiteral("mainwindow_%1_%2").arg(QString::fromUtf8(suffix)).arg(now);
}

void configureSettingsForTest() {
    static bool configured = false;
    if (configured) {
        return;
    }
    const fs::path settingsDir = mainWindowTestDir() / "settings";
    gis::tests::ensureDirectory(settingsDir);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QString::fromStdString(settingsDir.string()));
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                       QString::fromStdString(settingsDir.string()));
    gis::gui::markFirstRunComplete();
    configured = true;
}

QString createFinishedTask(const QString& displayGroup,
                           const gis::framework::Result& result,
                           const QString& pluginName = QStringLiteral("vector"),
                           const QString& actionKey = QStringLiteral("buffer"),
                           const std::map<std::string, gis::framework::ParamValue>& overrideParams = {}) {
    TaskManager::instance().initializeGroup(displayGroup);
    std::map<std::string, gis::framework::ParamValue> params;
    params["input"] = std::string("D:/data/input.geojson");
    params["output"] = std::string("D:/data/output.gpkg");
    for (const auto& [key, value] : overrideParams) {
        params[key] = value;
    }

    const QString taskId = TaskManager::instance().submitTask(
        displayGroup,
        pluginName,
        actionKey,
        params,
        QStringLiteral("矢量工具"),
        QStringLiteral("缓冲区"));
    TaskManager::instance().updateTaskStatus(displayGroup, taskId, TaskRecord::Running);
    TaskManager::instance().finishTask(displayGroup, taskId, result);
    return taskId;
}

QTreeWidget* findTaskTree(TaskCenterPage* page) {
    return page ? page->findChild<QTreeWidget*>(QStringLiteral("taskTree")) : nullptr;
}

QLabel* findLogTaskLabel(TaskCenterPage* page) {
    return page ? page->findChild<QLabel*>(QStringLiteral("logTaskLabel")) : nullptr;
}

QTextEdit* findLogDisplay(TaskCenterPage* page) {
    return page ? page->findChild<QTextEdit*>(QStringLiteral("logTerminal")) : nullptr;
}

struct DatasetCloser {
    void operator()(GDALDataset* ds) const {
        if (ds) {
            GDALClose(ds);
        }
    }
};

fs::path createMainWindowVectorGpkg(const fs::path& path,
                                    const std::string& layerName = "roads") {
    GDALAllRegister();
    auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    EXPECT_NE(driver, nullptr);
    if (!driver) {
        return {};
    }

    if (fs::exists(path)) {
        fs::remove(path);
    }

    std::unique_ptr<GDALDataset, DatasetCloser> ds(
        driver->Create(path.string().c_str(), 0, 0, 0, GDT_Unknown, nullptr));
    EXPECT_NE(ds, nullptr);
    if (!ds) {
        return {};
    }

    OGRSpatialReference srs;
    srs.importFromEPSG(4326);
    auto* layer = ds->CreateLayer(layerName.c_str(), &srs, wkbPolygon, nullptr);
    EXPECT_NE(layer, nullptr);
    if (!layer) {
        return {};
    }

    OGRFeatureDefn* defn = layer->GetLayerDefn();
    std::unique_ptr<OGRFeature> feature(OGRFeature::CreateFeature(defn));
    const char* wktText = "POLYGON ((100 20, 110 20, 110 30, 100 30, 100 20))";
    char* wkt = const_cast<char*>(wktText);
    OGRGeometry* geometry = nullptr;
    EXPECT_EQ(OGRGeometryFactory::createFromWkt(&wkt, nullptr, &geometry), OGRERR_NONE);
    feature->SetGeometryDirectly(geometry);
    EXPECT_EQ(layer->CreateFeature(feature.get()), OGRERR_NONE);

    return path;
}

template <typename Predicate>
bool waitForCondition(Predicate predicate,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

} // namespace

TEST(MainWindowTest, TaskRunnerFinishedSuccessUpdatesExecutionStateAndSummary) {
    configureSettingsForTest();
    SettingsManager::instance().clearRecentFiles();

    const QString displayGroup = uniqueMainWindowGroup("success");
    const QString outputPath = QString::fromStdString(
        (mainWindowTestDir() / "success_output.geojson").generic_string());
    const QString taskId = createFinishedTask(
        displayGroup,
        gis::framework::Result::ok("custom success", outputPath.toStdString()));

    MainWindow window;
    bool signalTriggered = false;
    bool signalValue = false;
    QObject::connect(&window, &MainWindow::executionFinished, &window,
                     [&](bool success) {
        signalTriggered = true;
        signalValue = success;
    });

    window.pendingResultTaskIds_[taskId] = displayGroup;
    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window,
        "onTaskRunnerFinished",
        Q_ARG(QString, displayGroup),
        Q_ARG(QString, taskId),
        Q_ARG(bool, true),
        Q_ARG(bool, false)));

    EXPECT_TRUE(window.lastExecutionSuccess());
    EXPECT_FALSE(window.lastExecutionCancelled());
    EXPECT_EQ(window.lastExecutionMessage(), QStringLiteral("custom success"));
    EXPECT_EQ(window.lastExecutionRawMessage(), QStringLiteral("custom success"));
    ASSERT_NE(window.resultSummaryLabel_, nullptr);
    EXPECT_TRUE(window.resultSummaryLabel_->text().contains(QStringLiteral("执行成功")));
    EXPECT_TRUE(window.resultSummaryLabel_->text().contains(outputPath));
    ASSERT_NE(window.statusProgressBar_, nullptr);
    EXPECT_EQ(window.statusProgressBar_->value(), 100);
    EXPECT_EQ(window.statusBar()->currentMessage(), QStringLiteral("执行成功"));
    EXPECT_TRUE(signalTriggered);
    EXPECT_TRUE(signalValue);
    EXPECT_FALSE(window.pendingResultTaskIds_.contains(taskId));
    EXPECT_TRUE(SettingsManager::instance().recentFiles().contains(outputPath));
}

TEST(MainWindowTest, TaskRunnerFinishedFailureUpdatesExecutionStateAndProgress) {
    configureSettingsForTest();

    const QString displayGroup = uniqueMainWindowGroup("failure");
    const QString taskId = createFinishedTask(
        displayGroup,
        gis::framework::Result::fail("custom failure"));

    MainWindow window;
    bool signalTriggered = false;
    bool signalValue = true;
    QObject::connect(&window, &MainWindow::executionFinished, &window,
                     [&](bool success) {
        signalTriggered = true;
        signalValue = success;
    });

    window.pendingResultTaskIds_[taskId] = displayGroup;
    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window,
        "onTaskRunnerFinished",
        Q_ARG(QString, displayGroup),
        Q_ARG(QString, taskId),
        Q_ARG(bool, false),
        Q_ARG(bool, false)));

    EXPECT_FALSE(window.lastExecutionSuccess());
    EXPECT_FALSE(window.lastExecutionCancelled());
    EXPECT_EQ(window.lastExecutionMessage(), QStringLiteral("custom failure"));
    EXPECT_EQ(window.lastExecutionRawMessage(), QStringLiteral("custom failure"));
    ASSERT_NE(window.resultSummaryLabel_, nullptr);
    EXPECT_TRUE(window.resultSummaryLabel_->text().contains(QStringLiteral("执行失败")));
    EXPECT_TRUE(window.resultSummaryLabel_->text().contains(QStringLiteral("custom failure")));
    ASSERT_NE(window.statusProgressBar_, nullptr);
    EXPECT_EQ(window.statusProgressBar_->value(), 0);
    EXPECT_EQ(window.statusBar()->currentMessage(),
              QStringLiteral("执行失败：custom failure"));
    EXPECT_TRUE(signalTriggered);
    EXPECT_FALSE(signalValue);
}

TEST(MainWindowTest, TaskRunnerFinishedCancelledUpdatesCancelledState) {
    configureSettingsForTest();

    const QString displayGroup = uniqueMainWindowGroup("cancelled");
    const QString taskId = createFinishedTask(
        displayGroup,
        gis::framework::Result::cancelled("Cancelled by user"));

    MainWindow window;
    bool signalTriggered = false;
    bool signalValue = true;
    QObject::connect(&window, &MainWindow::executionFinished, &window,
                     [&](bool success) {
        signalTriggered = true;
        signalValue = success;
    });

    window.pendingResultTaskIds_[taskId] = displayGroup;
    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window,
        "onTaskRunnerFinished",
        Q_ARG(QString, displayGroup),
        Q_ARG(QString, taskId),
        Q_ARG(bool, false),
        Q_ARG(bool, true)));

    EXPECT_FALSE(window.lastExecutionSuccess());
    EXPECT_TRUE(window.lastExecutionCancelled());
    EXPECT_EQ(window.lastExecutionMessage(), QStringLiteral("操作已取消"));
    EXPECT_EQ(window.lastExecutionRawMessage(), QStringLiteral("Cancelled by user"));
    ASSERT_NE(window.resultSummaryLabel_, nullptr);
    EXPECT_TRUE(window.resultSummaryLabel_->text().contains(QStringLiteral("已取消")));
    ASSERT_NE(window.statusProgressBar_, nullptr);
    EXPECT_EQ(window.statusProgressBar_->value(), 0);
    EXPECT_EQ(window.statusBar()->currentMessage(), QStringLiteral("执行已取消"));
    EXPECT_TRUE(signalTriggered);
    EXPECT_FALSE(signalValue);
}

TEST(MainWindowTest, DeleteTasksRemovesTaskRowsAndDatabaseRecords) {
    configureSettingsForTest();

    const QString displayGroup = uniqueMainWindowGroup("delete");
    const QString taskIdA = createFinishedTask(
        displayGroup,
        gis::framework::Result::ok("delete a"));
    const QString taskIdB = createFinishedTask(
        displayGroup,
        gis::framework::Result::ok("delete b"));

    MainWindow window;
    ASSERT_NE(window.taskCenterPage_, nullptr);
    window.taskCenterPage_->setCurrentGroup(displayGroup);
    window.taskCenterPage_->refreshAll();

    auto* taskTree = findTaskTree(window.taskCenterPage_);
    ASSERT_NE(taskTree, nullptr);
    ASSERT_EQ(taskTree->topLevelItemCount(), 2);

    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window,
        "onDeleteTasks",
        Q_ARG(QStringList, QStringList{taskIdA})));

    EXPECT_EQ(TaskManager::instance().taskCount(displayGroup), 1);
    EXPECT_TRUE(TaskManager::instance().findTask(displayGroup, taskIdA).id.isEmpty());
    EXPECT_EQ(TaskManager::instance().findTask(displayGroup, taskIdB).id, taskIdB);
    EXPECT_EQ(taskTree->topLevelItemCount(), 1);
}

TEST(MainWindowTest, EditTaskLoadsTaskParamsAndSwitchesBackToConfigTab) {
    configureSettingsForTest();

    const QString displayGroup = uniqueMainWindowGroup("edit");
    const std::map<std::string, gis::framework::ParamValue> params = {
        {"input", std::string("D:/data/edit_input.geojson")},
        {"output", std::string("D:/data/edit_output.gpkg")}
    };
    const QString taskId = createFinishedTask(
        displayGroup,
        gis::framework::Result::ok("edit success"),
        QStringLiteral("vector"),
        QStringLiteral("buffer"),
        params);

    MainWindow window;
    ASSERT_FALSE(window.pluginManager_.plugins().empty());
    ASSERT_NE(window.taskCenterPage_, nullptr);
    ASSERT_NE(window.tabWidget_, nullptr);
    ASSERT_NE(window.paramWidget_, nullptr);

    window.taskCenterPage_->setCurrentGroup(displayGroup);
    window.tabWidget_->setCurrentIndex(1);

    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window,
        "onEditTask",
        Q_ARG(QString, taskId)));

    ASSERT_NE(window.currentPlugin_, nullptr);
    EXPECT_EQ(window.currentPlugin_->name(), "vector");
    EXPECT_EQ(window.currentActionKey_, QStringLiteral("buffer"));
    EXPECT_EQ(window.currentEditingTaskId_, taskId);
    EXPECT_EQ(window.tabWidget_->currentIndex(), 0);
    EXPECT_EQ(window.paramWidget_->stringValue("input"), "D:/data/edit_input.geojson");
    EXPECT_EQ(window.paramWidget_->stringValue("output"), "D:/data/edit_output.gpkg");
}

TEST(MainWindowTest, EditTaskRestoresExtentParamValues) {
    configureSettingsForTest();

    const QString displayGroup = QStringLiteral("vector");
    TaskManager::instance().initializeGroup(displayGroup);
    const std::map<std::string, gis::framework::ParamValue> params = {
        {"input", std::string("D:/data/extent_input.geojson")},
        {"output", std::string("D:/data/extent_output.geojson")},
        {"extent", std::array<double, 4>{1.25, 2.5, 110.75, 220.125}}
    };
    const QString taskId = createFinishedTask(
        displayGroup,
        gis::framework::Result::ok("extent success"),
        QStringLiteral("vector"),
        QStringLiteral("filter"),
        params);

    MainWindow window;
    ASSERT_NE(window.taskCenterPage_, nullptr);
    window.taskCenterPage_->setCurrentGroup(displayGroup);

    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window,
        "onEditTask",
        Q_ARG(QString, taskId)));

    const auto collected = window.paramWidget_->collectParams();
    const auto it = collected.find("extent");
    ASSERT_NE(it, collected.end());
    const auto* extent = std::get_if<std::array<double, 4>>(&it->second);
    ASSERT_NE(extent, nullptr);
    EXPECT_DOUBLE_EQ((*extent)[0], 1.25);
    EXPECT_DOUBLE_EQ((*extent)[1], 2.5);
    EXPECT_DOUBLE_EQ((*extent)[2], 110.75);
    EXPECT_DOUBLE_EQ((*extent)[3], 220.125);
}

TEST(MainWindowTest, EditTaskRestoresMultiFileListAsCommaSeparatedText) {
    configureSettingsForTest();

    const QString displayGroup = QStringLiteral("cutting");
    TaskManager::instance().initializeGroup(displayGroup);
    const std::map<std::string, gis::framework::ParamValue> params = {
        {"input", std::string("D:/data/base_band.tif")},
        {"output", std::string("D:/data/merge_output.tif")},
        {"bands", std::vector<std::string>{
            "D:/data/band_1.tif",
            "D:/data/band_2.tif",
            "D:/data/band_3.tif"}}
    };
    const QString taskId = createFinishedTask(
        displayGroup,
        gis::framework::Result::ok("bands success"),
        QStringLiteral("cutting"),
        QStringLiteral("merge_bands"),
        params);

    MainWindow window;
    ASSERT_NE(window.taskCenterPage_, nullptr);
    window.taskCenterPage_->setCurrentGroup(displayGroup);

    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window,
        "onEditTask",
        Q_ARG(QString, taskId)));

    EXPECT_EQ(window.paramWidget_->stringValue("bands"),
              "D:/data/band_1.tif, D:/data/band_2.tif, D:/data/band_3.tif");
}

TEST(MainWindowTest, SettingInputAutoFillsOutputAndKeepsManualOverride) {
    configureSettingsForTest();

    MainWindow window;
    window.selectPluginByName("vector");
    window.selectActionByKey("buffer");

    ASSERT_TRUE(window.setParamValue("input", "D:/data/source.geojson"));
    EXPECT_EQ(window.paramWidget_->stringValue("output"),
              "D:/data/source_vector_buffer.gpkg");

    ASSERT_TRUE(window.setParamValue("output", "D:/data/manual_output.gpkg"));
    ASSERT_TRUE(window.setParamValue("input", "D:/data/changed.geojson"));
    EXPECT_EQ(window.paramWidget_->stringValue("output"),
              "D:/data/manual_output.gpkg");
}

TEST(MainWindowTest, SettingGeoPackageInputAutoFillsLayerAndExtent) {
    configureSettingsForTest();

    const fs::path gpkgPath = createMainWindowVectorGpkg(
        mainWindowTestDir() / "autofill_input.gpkg");
    ASSERT_FALSE(gpkgPath.empty());

    MainWindow window;
    window.selectPluginByName("vector");
    window.selectActionByKey("filter");

    ASSERT_TRUE(window.setParamValue("input", gpkgPath.generic_string()));
    EXPECT_EQ(window.paramWidget_->stringValue("layer"), "roads");

    const auto params = window.paramWidget_->collectParams();
    const auto it = params.find("extent");
    ASSERT_NE(it, params.end());
    const auto* extent = std::get_if<std::array<double, 4>>(&it->second);
    ASSERT_NE(extent, nullptr);
    EXPECT_DOUBLE_EQ((*extent)[0], 100.0);
    EXPECT_DOUBLE_EQ((*extent)[1], 20.0);
    EXPECT_DOUBLE_EQ((*extent)[2], 110.0);
    EXPECT_DOUBLE_EQ((*extent)[3], 30.0);
}

TEST(MainWindowTest, RerunTaskCreatesNewTaskRecordAndClearsEditingState) {
    configureSettingsForTest();

    const QString displayGroup = QStringLiteral("vector");
    TaskManager::instance().initializeGroup(displayGroup);
    TaskManager::instance().clearHistory(displayGroup);
    TaskDatabase::instance().clearAllLogs(displayGroup);

    const QString rerunOutput = QString::fromStdString(
        (mainWindowTestDir() / "rerun_output.gpkg").generic_string());
    const std::map<std::string, gis::framework::ParamValue> params = {
        {"input", std::string("D:/data/missing_input.geojson")},
        {"output", rerunOutput.toStdString()}
    };
    const QString taskId = createFinishedTask(
        displayGroup,
        gis::framework::Result::fail("original failure"),
        QStringLiteral("vector"),
        QStringLiteral("buffer"),
        params);

    MainWindow window;
    ASSERT_NE(window.taskCenterPage_, nullptr);
    window.taskCenterPage_->setCurrentGroup(displayGroup);
    window.currentEditingTaskId_ = QStringLiteral("editing-placeholder");

    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window,
        "onRerunTask",
        Q_ARG(QString, taskId)));

    EXPECT_TRUE(window.currentEditingTaskId_.isEmpty());
    EXPECT_EQ(window.currentActionKey_, QStringLiteral("buffer"));
    ASSERT_NE(window.currentPlugin_, nullptr);
    EXPECT_EQ(window.currentPlugin_->name(), "vector");

    ASSERT_TRUE(waitForCondition([&]() {
        return TaskManager::instance().taskCount(displayGroup) >= 2
            && !TaskRunner::instance().isRunning()
            && TaskRunner::instance().queuedCount() == 0;
    }));

    const auto recent = TaskManager::instance().recentTasks(displayGroup, 2);
    ASSERT_GE(recent.size(), 2);
    EXPECT_NE(recent[0].id, taskId);
    EXPECT_EQ(recent[0].pluginName, QStringLiteral("vector"));
    EXPECT_EQ(recent[0].actionKey, QStringLiteral("buffer"));
    EXPECT_EQ(recent[0].params.at("input"), params.at("input"));
    EXPECT_EQ(recent[0].params.at("output"), params.at("output"));
}

TEST(MainWindowTest, ClearHistoryRemovesAllTaskRowsAndRecords) {
    configureSettingsForTest();

    const QString displayGroup = uniqueMainWindowGroup("clear_history");
    createFinishedTask(displayGroup, gis::framework::Result::ok("history a"));
    createFinishedTask(displayGroup, gis::framework::Result::ok("history b"));

    MainWindow window;
    ASSERT_NE(window.taskCenterPage_, nullptr);
    window.taskCenterPage_->setCurrentGroup(displayGroup);
    window.taskCenterPage_->refreshAll();

    auto* taskTree = findTaskTree(window.taskCenterPage_);
    ASSERT_NE(taskTree, nullptr);
    ASSERT_EQ(taskTree->topLevelItemCount(), 2);

    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window,
        "onClearHistory"));

    EXPECT_EQ(TaskManager::instance().taskCount(displayGroup), 0);
    EXPECT_EQ(taskTree->topLevelItemCount(), 0);
}

TEST(MainWindowTest, ClearLogsForTaskClearsDatabaseAndCurrentDisplay) {
    configureSettingsForTest();

    const QString displayGroup = uniqueMainWindowGroup("clear_logs");
    const QString taskId = createFinishedTask(
        displayGroup,
        gis::framework::Result::ok("logs success"));
    TaskManager::instance().appendLog(displayGroup, taskId, QStringLiteral("第一条日志"));
    TaskManager::instance().appendLog(displayGroup, taskId, QStringLiteral("第二条日志"));

    MainWindow window;
    ASSERT_NE(window.taskCenterPage_, nullptr);
    window.taskCenterPage_->setCurrentGroup(displayGroup);
    window.taskCenterPage_->refreshAll();

    auto* taskTree = findTaskTree(window.taskCenterPage_);
    auto* logTaskLabel = findLogTaskLabel(window.taskCenterPage_);
    auto* logDisplay = findLogDisplay(window.taskCenterPage_);
    ASSERT_NE(taskTree, nullptr);
    ASSERT_NE(logTaskLabel, nullptr);
    ASSERT_NE(logDisplay, nullptr);
    ASSERT_EQ(taskTree->topLevelItemCount(), 1);
    taskTree->setCurrentItem(taskTree->topLevelItem(0));

    EXPECT_FALSE(TaskManager::instance().logsForTask(displayGroup, taskId).isEmpty());
    EXPECT_TRUE(logTaskLabel->text().contains(taskId));
    EXPECT_TRUE(logDisplay->toPlainText().contains(QStringLiteral("第一条日志")));

    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window,
        "onClearLogsForTask",
        Q_ARG(QString, taskId)));

    EXPECT_TRUE(TaskManager::instance().logsForTask(displayGroup, taskId).isEmpty());
    EXPECT_TRUE(logTaskLabel->text().contains(taskId));
    EXPECT_TRUE(logDisplay->toPlainText().isEmpty());
}

TEST(MainWindowTest, ClearAllLogsClearsDatabaseAndCurrentDisplay) {
    configureSettingsForTest();

    const QString displayGroup = uniqueMainWindowGroup("clear_all_logs");
    const QString taskId = createFinishedTask(
        displayGroup,
        gis::framework::Result::ok("logs all success"));
    TaskManager::instance().appendLog(displayGroup, taskId, QStringLiteral("日志甲"));
    TaskManager::instance().appendLog(displayGroup, taskId, QStringLiteral("日志乙"));

    MainWindow window;
    ASSERT_NE(window.taskCenterPage_, nullptr);
    window.taskCenterPage_->setCurrentGroup(displayGroup);
    window.taskCenterPage_->refreshAll();

    auto* taskTree = findTaskTree(window.taskCenterPage_);
    auto* logDisplay = findLogDisplay(window.taskCenterPage_);
    ASSERT_NE(taskTree, nullptr);
    ASSERT_NE(logDisplay, nullptr);
    ASSERT_EQ(taskTree->topLevelItemCount(), 1);
    taskTree->setCurrentItem(taskTree->topLevelItem(0));

    EXPECT_EQ(TaskManager::instance().logsForTask(displayGroup, taskId).size(), 2);
    EXPECT_TRUE(logDisplay->toPlainText().contains(QStringLiteral("日志甲")));

    ASSERT_TRUE(QMetaObject::invokeMethod(
        &window,
        "onClearAllLogs"));

    EXPECT_TRUE(TaskManager::instance().logsForTask(displayGroup, taskId).isEmpty());
    EXPECT_TRUE(logDisplay->toPlainText().isEmpty());
}
