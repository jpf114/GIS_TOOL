#include <gtest/gtest.h>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <QCoreApplication>
#include <QLabel>
#include <QMetaObject>
#include <QProgressBar>
#include <QSettings>
#include <QDir>
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
#include "../src/gui/app_settings.h"
#include "test_support.h"

#include <chrono>
#include <atomic>
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

// 21-FixA part2 I-6：这里**不再做重定向**，只做"重定向确实生效"的断言。
// 真正的 QSettings 落点已经上移到 tests/test_main.cpp 的每进程单例（不可绕过）。
// WHY 必须兜底而不是靠约定：旧写法把重定向交给这 13 处手写调用 + 一个 static bool，
//   新用例忘记调用一样跑得绿，没有任何断言会发现"这条用例把配置写进了开发者机器"。
//   现在机制在 main() 钉一次、结果在这里断言一次（落点算式两边共用
//   gis::tests::privateSettingsDir()），任何一侧退化都会被抓到：
//   删掉 main() 那三行 ⇒ 断言 2/3/4 红；改落点算式 ⇒ 断言 2 红；
//   welcome_dialog 若不再走 appSettings() ⇒ 断言 6 红（批次 B2 补的那一条）。
//
// ⚠ 断言必须用**显式带 Format/Scope 的构造形态**，不能用两参形态，这是本块踩过的坑：
//   QSettings probe("GISTool","GISTool") 两参构造走的是 **NativeFormat**，
//   既不吃 setDefaultFormat(IniFormat) 也不吃 setPath(IniFormat, …) ⇒ fileName() 返回
//   HKEY_CURRENT_USER\Software\GISTool\…，与 ini 重定向无关 ⇒ 断言恒红（14 条 MainWindowTest 实测全红）。
//   这条同时登记一个 src 侧历史事实：SettingsManager 原先用的正是两参构造 ⇒ NativeFormat，
//   写的是注册表 HKCU\Software\GISTool 而不是 %APPDATA%\GISTool\GISTool.ini
//   （⇒ 每次跑 MainWindowTest 都在写真实用户注册表）。N100 先加了 GIS_TOOL_SETTINGS_INI 覆盖把测试
//   进程圈回私有文件，批次 B2 再把落点与迁移规则收进 gis::gui::appSettings()（生产落点
//   = AppConfigLocation/settings.ini），只读出口 storagePath() 不变，落点仍由"断言 4"直接测真实对象。
void configureSettingsForTest() {
    const QString privateDir =
        QString::fromStdString(gis::tests::privateSettingsDir().string());
    // 断言 1：默认格式必须仍是 IniFormat —— NativeFormat 写的是注册表，同样是不该碰的用户状态。
    EXPECT_EQ(QSettings::defaultFormat(), QSettings::IniFormat)
        << "test_main.cpp 里的每进程重定向单例没跑起来（或被改成 NativeFormat）";
    // 断言 2：真构造一次，验 GISTool/GISTool.ini 确实落在私有目录里（测结果，不是测"调过 setter"）。
    //   用**显式带 Format/Scope 的四参构造**，理由见上面那段坑登记。
    QSettings probe(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("GISTool"), QStringLiteral("GISTool"));
    const QString landed = QDir::toNativeSeparators(probe.fileName());
    const QString want = QDir::toNativeSeparators(
        privateDir + QStringLiteral("/GISTool/GISTool.ini"));
    EXPECT_EQ(landed, want)
        << "GISTool.ini 的实际落点不是本进程的私有目录（重定向没跑，或并发共享同一份定名 ini）";
    // 断言 3：方向盘——绝不允许落在用户真实 %APPDATA% 下的 GISTool 里。
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty()) {
        EXPECT_FALSE(landed.startsWith(
            QDir::toNativeSeparators(appData + QStringLiteral("\\GISTool"))))
            << "配置写进了用户真实 %APPDATA% 下的 GISTool：" << landed.toStdString();
    }
    // 断言 4（N100）：SettingsManager 这个**真实对象**的落点必须在本进程私有目录里，不能是注册表。
    //   前两版这里只能断到"探针构造的 ini 在私有目录"，因为 SettingsManager 用两参
    //   QSettings("GISTool","GISTool") ⇒ NativeFormat，测试侧改不动（当时记为挂账）。
    //   批次 B2 之后落点判定整个搬进 src/gui/app_settings.cpp：GIS_TOOL_SETTINGS_INI 由那里的
    //   resolvedSettingsPath()/appSettingsPath() 读，test_main.cpp 只负责注入变量的**值**，
    //   SettingsManager 自己不再有任何覆盖分支。
    //   ⚠ 别把这里写成"生产不设该变量 ⇒ 行为不变"：生产不设时行为**已经变了** —— 落点是
    //   AppConfigLocation 下的 settings.ini，不再是 HKCU\Software\GISTool（那正是批次 B 的目的）。
    //   删掉 test_main.cpp 里注入那两行，这里立刻红：落点会退回本进程的 AppConfigLocation
    //   （用户配置目录下的定名文件），同样是"测试写用户状态"这一族害。
    const QString managerLanded = QDir::toNativeSeparators(
        SettingsManager::instance().storagePath());
    const QString managerWant = QDir::toNativeSeparators(
        privateDir + QStringLiteral("/SettingsManager.ini"));
    EXPECT_EQ(managerLanded, managerWant)
        << "SettingsManager 没落在本进程私有文件上（GIS_TOOL_SETTINGS_INI 未生效）"
           "⇒ 落点退回用户配置目录（批次 B2 之前这里是写注册表 HKCU\\Software\\GISTool）";
    // ── 断言 5（B2）：生产落点的两条口径 ─────────────────────────────────────────
    // 5a（已删，判据不在这里）："生产落点是 AppConfigLocation 下的 settings.ini"这一条由
    //    tests/test_app_settings.cpp 的 AppSettingsPath.ProductionShapeWithoutTouchingDisk 断，
    //    那边是 QFileInfo(resolvedSettingsPath(QString())).fileName() == "settings.ini"，
    //    比这里原来的 endsWith("settings.ini") 强（endsWith 会放过 any_settings.ini 这种名字）。
    //    同一个事实留两份判据、其中一份更弱 ⇒ 只会让"改的是哪一份"变成读代码才发现的问题，所以合并。
    // 5b 交接自 B1 复审义务 1：**"生产落点是不是 IniFormat"这条判据归 B2**，正确落点就在这里 ——
    //    format() 是 appSettings() 自己选的构造参数，不是测试传进去的，所以把它换成
    //    NativeFormat/两参形态时这条会红（test_app_settings.cpp 里那条同形状的断言已被删，因为
    //    那里传入的就是被测值本身，恒真）。
    {
        const QSettings probe = gis::gui::appSettings();   // 注入态 ⇒ 只写私有 ini，安全
        EXPECT_EQ(probe.format(), QSettings::IniFormat)
            << "welcome_dialog 走的默认构造在 Windows 生产进程上是 NativeFormat ⇒ 会写 HKCU\\Software\\GIS\\GIS Tool";
        // 注册表对象的 fileName() 是 **HKEY_CURRENT_USER\Software\…** 这一串（落点实测见
        // src/gui/app_settings.cpp 顶部），所以鉴别"落点还是不是注册表"要盯 fileName() 的开头。
        // ⚠ 别改回"看后缀是不是 Registry"那一类写法：QFileInfo 把 '\\' 当分隔符，于是它取的是
        //   键名末段（…\GISTool ⇒ 空串）的后缀，任何注册表路径都不可能等于字面量 "Registry"
        //   ⇒ 那条断言在任何实现下都不会红，是恒真判据（B3a 用同一条注册表形态字符串对照验过）。
        EXPECT_FALSE(probe.fileName().startsWith(QLatin1String("HKEY")))
            << "探针落点看起来仍是注册表：fileName=" << probe.fileName().toStdString();
    }
    // 5c welcome_dialog 的落点守卫（本任务真正的红→绿判据）。
    //    为什么 5b 对它不具鉴别力：test_main.cpp 调了 setDefaultFormat(IniFormat)，所以测试进程里
    //    **默认构造的 QSettings 也是 IniFormat**，落 <私有目录>/Unknown Organization/gis_tests.ini；
    //    而 appSettings() 走显式路径 <私有目录>/SettingsManager.ini。两个都是 ini ⇒ format() 分不出来，
    //    但它们是**两个不同的文件**（且前者 status=AccessError 根本写不进去）。
    //    于是只能断"写进去的那条键，appSettings() 读不读得到"：
    //      改 welcome_dialog 之前 ⇒ markFirstRunComplete() 写的是 Unknown Organization 那份，
    //                            这里读到 false ⇒ 本条红（同时暴露"首启标记永远写不进用户配置"这个真缺陷）；
    //      改之后            ⇒ 两边同一个文件 ⇒ true ⇒ 绿。
    gis::gui::markFirstRunComplete();
    EXPECT_TRUE(gis::gui::appSettings().value(QLatin1String("app/first_run_complete"), false).toBool())
        << "welcome_dialog 与 appSettings() 不是同一个落点 ⇒ 首启标记写到别的文件里去了"
           "（生产进程即 HKCU\\Software\\GIS\\GIS Tool，N101 的第二条腿）";
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

class FlakyRetryPlugin final : public gis::framework::IGisPlugin {
public:
    std::string name() const override { return "retry_test"; }
    std::string displayName() const override { return "Retry Test"; }
    std::string version() const override { return "1.0.0"; }
    std::string description() const override { return "Fails once, then succeeds."; }

    std::vector<gis::framework::ParamSpec> paramSpecs() const override {
        return {};
    }

    gis::framework::Result execute(
        const std::map<std::string, gis::framework::ParamValue>&,
        gis::core::ProgressReporter&) override {
        const int callIndex = ++callCount_;
        if (callIndex == 1) {
            return gis::framework::Result::fail("first attempt failed");
        }
        return gis::framework::Result::ok("second attempt succeeded");
    }

    int callCount() const {
        return callCount_.load();
    }

private:
    std::atomic<int> callCount_{0};
};

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

TEST(MainWindowTest, TaskRunnerRetryKeepsOriginalTaskId) {
    configureSettingsForTest();
    const QString displayGroup = uniqueMainWindowGroup("retry_same_id");
    TaskManager::instance().initializeGroup(displayGroup);
    TaskManager::instance().clearHistory(displayGroup);
    TaskDatabase::instance().clearAllLogs(displayGroup);

    ASSERT_TRUE(waitForCondition([]() {
        return !TaskRunner::instance().isRunning()
            && TaskRunner::instance().queuedCount() == 0;
    }));

    FlakyRetryPlugin plugin;
    QString finishedTaskId;
    bool finishedSuccess = false;
    bool finishedCancelled = true;
    int finishSignalCount = 0;

    QMetaObject::Connection connection = QObject::connect(
        &TaskRunner::instance(),
        &TaskRunner::taskFinished,
        &TaskRunner::instance(),
        [&](const QString& group, const QString& taskId, bool success, bool cancelled) {
            if (group != displayGroup) {
                return;
            }
            finishedTaskId = taskId;
            finishedSuccess = success;
            finishedCancelled = cancelled;
            ++finishSignalCount;
        });

    const std::map<std::string, gis::framework::ParamValue> params = {
        {"input", std::string("D:/data/retry_input.geojson")},
        {"output", std::string("D:/data/retry_output.gpkg")}
    };
    const QString taskId = TaskRunner::instance().run(
        &plugin,
        displayGroup,
        QStringLiteral("retry_test"),
        QStringLiteral("retry_action"),
        params,
        QStringLiteral("Retry Test"),
        QStringLiteral("Retry Action"));

    ASSERT_FALSE(taskId.isEmpty());
    ASSERT_TRUE(waitForCondition([&]() {
        return finishSignalCount == 1
            && !TaskRunner::instance().isRunning()
            && TaskRunner::instance().queuedCount() == 0;
    }));

    QObject::disconnect(connection);

    EXPECT_EQ(plugin.callCount(), 2);
    EXPECT_EQ(finishSignalCount, 1);
    EXPECT_EQ(finishedTaskId, taskId);
    EXPECT_TRUE(finishedSuccess);
    EXPECT_FALSE(finishedCancelled);

    const TaskRecord record = TaskManager::instance().findTask(displayGroup, taskId);
    EXPECT_EQ(record.id, taskId);
    EXPECT_EQ(record.status, TaskRecord::Completed);
    EXPECT_TRUE(record.result.success);
    EXPECT_EQ(QString::fromStdString(record.result.message),
              QStringLiteral("second attempt succeeded"));

    const auto logs = TaskManager::instance().logsForTask(displayGroup, taskId);
    bool sawRetryLog = false;
    for (const auto& log : logs) {
        if (log.message.contains(QStringLiteral("自动重试"))) {
            sawRetryLog = true;
            break;
        }
    }
    EXPECT_TRUE(sawRetryLog);
    EXPECT_EQ(TaskManager::instance().taskCount(displayGroup), 1);
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
