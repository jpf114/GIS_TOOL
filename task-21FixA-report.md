# task-21 Fix A — 批次 A 测试侧修复报告（含第 2 轮收尾与认证）

> 本报告由第 2 轮（收尾与认证）执行者创建并负责覆盖全部三段工作：
> `3e28002`（已提交）、`67dc8c3`（已提交）、`4768551`（本轮收尾提交）。
> 上一轮跑了 159 个工具调用后撞上回合上限，**没有创建本报告**，并把 5 个文件 143 行留在工作树里未提交。
> 环境：HEAD 开工时 `67dc8c3`；门控 `tasklist | grep -cE 'MSBuild|gis_tests|ctest'` = **0**（exit=1）。

---

## 一、现场复核（开工第一条命令）

```
$ git log --oneline -5
67dc8c3 test(用例): 21-FixA C-1——导出产物加 RESOURCE_LOCK；把守卫的形状探针资产接进用例
3e28002 test(守卫): 21-FixA C-1/C-2——块注释哨兵改跨文件累积并配冻结豁免表，匹配面加形状探针自检
8c03bcd docs(发布说明): 21-D 收尾——……
$ git status --short
 M tests/test_main.cpp
 M tests/test_mainwindow.cpp
 M tests/test_plugins.cpp
 M tests/test_support.cpp
 M tests/test_support.h
$ git diff --stat
 tests/test_main.cpp       | 20 ++++++++++++
 tests/test_mainwindow.cpp | 46 ++++++++++++++++++++-------
 tests/test_plugins.cpp    | 80 ++++++++++++++++++++++++++++++++++++-----------
 tests/test_support.cpp    | 22 +++++++++++++
 tests/test_support.h      |  5 +++
 5 files changed, 143 insertions(+), 30 deletions(-)
```
控制器给出的 143 增 / 30 删与 HEAD 均已当场核实一致。

---

## 二、`3e28002` — 守卫侧（块注释哨兵跨文件累积 + 冻结豁免表 + 形状探针）

（内容已由第 1 轮提交，本节为认证性描述；判据验收见第四节与第五节。）

改动面：`tests/check_action_visible_keys_read.cmake` +227/-12、新增 `tests/reader_shape_probe_sample.txt` 38 行。

要点：
1. **块注释哨兵改为跨文件累积**：`/* … */` 的开启状态原先按"单文件、单行"判定，
   导致跨文件/跨行的注释边界被误判为代码 ⇒ 被注释掉的读点可以冒充"真读"（终审 N90：GOLD 判据可被注释满足）。
   现在哨兵位沿文件序列累积，注释区内的 `getParam` 一律不计入可见读点。
2. **冻结豁免表 `GIS_21C_BLOCK_COMMENT_ALLOWLIST`**：把历史遗留、已双向冻结的免检项显式列出，
   豁免必须是"点名的、有清单的"，不允许由解析器意外吞掉。
3. **形状探针自检**：`tests/reader_shape_probe_sample.txt` 是刻意构造的畸形样本，
   用来证明解析器真的会拒绝错误形状。**探针资产不存在 ⇒ FATAL，不许静默空转**
   ——这一条是防"守卫因找不到资产而变成 no-op 却报绿"的关键设计。

---

## 三、`67dc8c3` — 用例侧（RESOURCE_LOCK）

改动面：`tests/CMakeLists.txt` +50/-4。导出用例 `GuiContract.ExportActionVisibleKeys` 与守卫用例
`param_guard_visible_keys_all_read` 都加了 `RESOURCE_LOCK gis_action_visible_keys`。

为什么不选 `RUN_SERIAL`：`RUN_SERIAL` 只把用例与"其它 serial 用例"串行，**对非 serial 用例无效**；
本对用例的冲突面是同一份导出产物被并发读写，`RESOURCE_LOCK` 才按锁名精确排斥，
且不会像 `RUN_SERIAL` 那样把两条无关用例拖进同一条串行链、损伤全量时长。

验收（第 3 步并发对拍）见第七节。

---

## 四、`4768551` — 本轮收尾：5 个半成品文件的逐条收/退判定

**先说结论：5 个文件全部收下，无一退回。** 逐判据给证据。

### 判据一：有没有语义性改动藏在"测试加强"里 ⇒ **没有**

`git diff` 逐 hunk 核对（14 个 hunk），全部落在 `tests/**`；`src/`、`docs/`、`cmake/`、根 `CMakeLists.txt`、
`data/`、`tests/action_gold_cases.txt` 零改动（`git status --short` 只列 5 个 tests 文件即为证据）。
夹具**默认值未改**，只改了 4 个**产物文件名**（去同名）——这属于 I-3 判据升级的必要前提，见判据三。

### 判据二：I-6 的修法是否有机器级兜底 ⇒ **有**

- 旧写法：`test_mainwindow.cpp::configureSettingsForTest()` 里 `static bool configured` + 13 处手写调用
  ⇒ 新用例忘记调用就"绿着把配置写进开发者机器"，且**没有任何断言会发现**。
- 新写法（三层）：
  1. `tests/test_main.cpp` 在 `RUN_ALL_TESTS()` 之前**无条件**做 `setDefaultFormat(IniFormat)` +
     `set_path(IniFormat, UserScope/SystemScope, privateSettingsDir())`——一个进程只有一条路径可走，不可绕过；
  2. `tests/test_support.cpp::privateSettingsDir()` 按 **pid** 分道（`qsettings_output/pid_<pid>`），
     消除 13 个 MainWindowTest 进程共享同一定名 `GISTool.ini` 的并发 trunc；
  3. `test_mainwindow.cpp::configureSettingsForTest()` 退化为**三条断言**（默认格式、实际落点字面相等、
     反向禁止真 `%APPDATA%`）⇒ 删掉 main() 那三行或改动落点算式，断言立刻红。
- 事实核（本轮现查，非引用终审）：`tests` 里除 `test_main.cpp` 外**没有任何**用例直接构造 `QSettings`
  （`grep -rn "QSettings" tests/*.cpp` 只命中 test_main / test_support / test_mainwindow），
  而 `src/gui/settings_manager.cpp:10` 确为 `settings_("GISTool","GISTool")`、
  `src/gui/welcome_dialog.cpp:18` 确为默认构造 `QSettings settings;` ⇒ 无重定向时确实落真 `%APPDATA%`。
- **机器级证据**：`%APPDATA%\GISTool` 跑前/跑后均不存在，见第八节。

### 判据三：I-3 是否补了字面路径/字面文件名并实测"错原因时红" ⇒ **两条都补了，且实测过**

- `TileRejectsGeodeticProfileAndNamesIt` / `TileRejectsRasterProfileAndNamesIt`：
  正向 `find("tile profile \"<p>\" is not supported")`（实测源字符串：
  `src/plugins/cutting/cutting_plugin.cpp:1143` = `tile profile "<profile>" is not supported yet; only profile=mercator is implemented`）
  + 反向 `find("t17_profile_<p>_input.tif") == npos` + `ASSERT_TRUE(fs::exists(input))`。
  两例原先**共用夹具 `t17_profile_input.tif`**，现各自改名 ⇒ 并发下不再互相 trunc。
- `VectorSpatialJoinRejectsMismatchedSrs`：正向整句
  `"Spatial join requires input layers to use the same CRS."`（实测 `vector_plugin_spatial_join.inc:34`
  调 `failCrsMismatch("Spatial join")`，`vector_plugin_common.inc:89` 拼 `action + " requires input layers to use the same CRS."`）
  + 反向 `find("e2e_spatial_join_rejected_output.gpkg") == npos`。
  与 `VectorSpatialJoinWritesJoinCountAndValue` 原先共用 `e2e_spatial_join_output.gpkg`（一个期望失败一个期望成功），现分别改名。
- **为什么"含子串"确实是假绿**：`failCrsMismatch` 被 5 个动作共用
  （`Clip` / `Nearest` / `Union` / `Difference` / `Intersect` / `Spatial join`），子串 `"CRS"` 允许别的动作形状的报错顶替判据。
- **实测红**：`build/t21Fix_inj6_wrongreason_red.log`（第 1 轮留证，见第六节逐份认领）
  注入错原因后 `TileRejectsRasterProfileAndNamesIt` 红，且消息是 `tile profile "rasterized" is not supported yet`
  —— **旧判据 `find("raster")` 对这句话是绿的**（"rasterized" 含 "raster"），
  新判据的整句字面把它抓红。这正是终审第 2 席描述的失效形状。
  本轮另在**重编后的新二进制**上复跑了一次同类注入，见第七节。

### 判据四：I-1（K-21 回潮 6 处）清干净了吗 ⇒ **清了，零命中**

6 处全部落在 `test_plugins.cpp`：`:2838` 一带把 `task9r1_mutMF_ctest.log` 写进断言消息 + 5 处抄了会腐烂的分母
（577 / 33 / 544 / 544 / 559）。本轮实测：

```
$ grep -rn "task9r1_mutMF_ctest.log" tests/        → exit=1（零命中）
$ grep -rnE "全量 (544|559|577)|544/544|聚焦 33" tests/  → exit=1（零命中）
$ grep -rnE "[A-Za-z0-9_]+\.log" tests/*.cpp tests/*.h
   tests/test_gui_support.cpp:674:    const auto logs = taskManager.logsForTask(group, taskId);
   tests/test_gui_support.cpp:708:    EXPECT_TRUE(taskManager.logsForTask(group, taskId).isEmpty());
```
第 3 条命令的两处命中是我这条正则的**假阳性**（它把 `taskManager.logsForTask` 里的 `.log` 当成了文件名），
不是断言消息里的日志名——登记于此以免被误读成"还有残留"。
改写方式统一为"结论入库、当时的条数/日志名不入库，要复算就跑当前全量"。

### I-5 附带核验（不属于四判据，但涉及期望值正确性）

半成品删掉了 `expectedFromSpec = *declaredDefault == 4 ? "2" : "1"`（**期望 = f(被测值)** 的反演自证），
改为先 `ASSERT_EQ(*declaredDefault, 8)` 再把期望写死为 `"1"`。
本轮核实字面 8 不是抄来的：`src/plugins/processing/processing_plugin.cpp:68`
= `ParamSpec{"connectivity", …, ParamType::Int, false, int{8}, int{4}, int{8}}` ⇒ 默认值确为 8，
且 `:1448` 的代码兜底也是 8，断言不会天然红。

**文件归属更正（推翻终审第 2 席）**：`expectedFromSpec` 的真实位置是
`tests/test_plugins.cpp:1650`（改前 `:1651`），终审写作 `test_gui_support.cpp:1651` — **文件写错了**。
控制器已在工单中先行更正，本轮以 `git diff` 的 hunk 头 `@@ -1648,11 +1648,20 @@` 复证。

---

## 五、上一轮产物逐份认领（并推翻工单事实③）

**工单事实③不成立**，两处都被现跑证伪：

1. 产物**不是"散在 `build/` 根、命名没按 `build/t21Fix_*` 前缀"**：13 份在目录 `build/t21Fix/` 里，
   另外 10 份在 `build/` 根且**逐个都带 `t21Fix_` 前缀**（`t21Fix_build_debug_items4567.log` 等）。
2. 份数不是 35：`13 + 10 = 23`（其中 1 份是空目录 `fakeappdata`）。证据：
   ```
   $ find build -maxdepth 1 -newermt "$(git log -1 --format=%aI 3e28002)" | …  → 10 份 t21Fix_* 前缀
   $ ls -1 build/t21Fix | wc -l                                               → 13
   ```
   工单点名的 `report_baseline.txt` / `report_inj1.log` / `report_inj2.log` / `report_item12.log` /
   `script.bak` / `blockscan*.cmake` / `fakeappdata` 确有其物，但位置在 `build/t21Fix/` 目录内。

### 逐份用途认领

| 产物 | 大小 | 认领用途（对应判据） |
|---|---|---|
| `t21Fix/report_baseline.txt` | 15271 | `3e28002` C-1 基线读数：`literal_read_sites=582 files=42 lines=17881 segments=400`，无 `block_comment_hits` 行 |
| `t21Fix/report_inj1.log` | 15321 | C-1 **注入证据**：临时把被扫源文件加 2 行（`lines=17883`）⇒ `block_comment_hits=2 allowlist=1` ⇒ 哨兵能看见注入的块注释 |
| `t21Fix/report_inj2.log` | 15321 | C-2 **豁免表双向证据**：`lines=17881` 已还原但 `block_comment_hits=0` ⇒ 命中数 ≠ 豁免表长度即红（少一条也红） |
| `t21Fix/report_item12.log` | 15321 | C-1/C-2 收口态：`block_comment_hits=1 allowlist=1` 双向对齐 |
| `t21Fix/script.bak` | 52579 | `tests/check_action_visible_keys_read.cmake` **改前备份**（头 12 行即该脚本原文），非判据证据，仅作改动可追溯 |
| `t21Fix/inj_orig_info_filter.txt` | 9633 | 注入实验中被改的 `vector_plugin_info_filter.inc` 区段**原文备份** ⇒ src/ 已还原的凭据（现 `git status` 干净复证） |
| `t21Fix/blockscan.cmake` | 453 | 侦察用临时扫描器：找 `/*`、`*/` 形状命中行。**一次性工具，不予引用为判据证据** |
| `t21Fix/blockscan2.cmake` | 856 | 同上，改为遍历 `src/plugins/*`。**不予引用** |
| `t21Fix/g.cmake` | 131 | `file(GLOB)` 计数 sanity check（`N=`/`Z=`）。**用途已退化，不予引用** |
| `t21Fix/g2.cmake` | 219 | 同上，分别数 `*.cpp` 与 `vector/*.inc`。**不予引用** |
| `t21Fix/lencmp.cmake` | 502 | 验证 `list(LENGTH)` 对豁免表与**空表**的行为（空表必须报 0 而不是"归零静默"）。形状自检的一次性工具，**不予引用** |
| `t21Fix/fixnul.py` | 275 | 把误写进 `tests/test_support.cpp` 的**裸 NUL 字节**改回 `'\0'` 转义。现码 `overrideRoot[0] != '\0'` 即其结果，**不予引用** |
| `t21Fix/edit_mainwindow.py` | 3224 | 生成 I-6 半成品（重写 `configureSettingsForTest`）的脚本。工具，**不予引用为证据** |
| `t21Fix/fakeappdata/` | 0 | I-6 的假 `%APPDATA%` 根。目录**为空** ⇒ 本身不构成证据，改由第八节的现跑 `ls` 取证 |
| `build/t21Fix_build_debug_items4567.log` | 2853 | items 4-7 改动后的 Debug 增量构建记录 |
| `build/t21Fix_build_inj5.log` | 2726 | I-5 注入轮的构建 |
| `build/t21Fix_inj5_specdefault_red.log` | 1977 | **I-5 牙齿证据**：spec 默认值被改动后 `ASSERT_EQ(*declaredDefault, 8)` 红，报出"connectivity 的 spec 默认值漂了" |
| `build/t21Fix_build_inj67.log` | 2915 | I-3/I-6 注入轮构建 |
| `build/t21Fix_build_inj67b.log` | 123741 | 同上（全量重编，体量大说明当时连带重编了依赖） |
| `build/t21Fix_build_inj67c.log` | 2640 | 同上第三轮 |
| `build/t21Fix_inj6_wrongreason_red.log` | 939 | **I-3 牙齿证据（最有价值的一份）**：注入错原因后 `TileRejectsRasterProfileAndNamesIt` 红，消息为 `tile profile "rasterized" is not supported yet`。旧判据 `find("raster")` 对这句话**是绿的**（"rasterized" 含 "raster"），新整句字面判据抓红 |
| `build/t21Fix_inj7_noRedirect_red.log` | 2353 | **I-6 牙齿证据**：删掉 `test_main.cpp` 的重定向三行后落点断言红（报出 `landed` 为注册表 `KEY_CURRENT_USER\Software\GIST…`） |
| `build/t21Fix_build_debug_final.log` | 2679 | 还原注入后的 Debug 重建 |
| `build/t21Fix_items4567_green.log` | 32746 | ⚠ **名字与内容不符，登记为"不可引用的假绿证据"**，见下 |

### ⚠ 重点登记：`t21Fix_items4567_green.log` 名为 green，实为 14 条红

```
$ grep -cE "^\[  FAILED  \]" build/t21Fix_items4567_green.log   → 14 FAILED TESTS
$ grep -m1 -A3 "error:" build/t21Fix_items4567_green.log
   tests/test_mainwindow.cpp(70): error: Expected equality of these values: landed / want
```
把 `landed` 的 UTF-16 字节解出来是 `KEY_CURRENT_USER\Software\GIST…`（注册表路径），而 `want` 是
`D:\Code\MyProject\GIS_TOOL\build…`。⇒ 这份日志产生于**注入态未还原/未重编**的窗口
（`setDefaultFormat` 处于 NativeFormat 或被摘掉的状态），不是 I-6 的最终绿。
**本报告不引用它作为任何"全绿"证据**；I-6 与 items 4-7 的真绿只能由本次第六节的全量运行给出。
这也是"引用任何全绿前先验二进制新鲜度"这条纪律在本批的第二次实际抓到（第一次是工单自己登记的 3 条假 Timeout）。

> **更正本节的初判**：写到这里时我把 `t21Fix_items4567_green.log` 的 14 条红判成"注入态未还原的残留"。
> 本轮第一次真验证（重编后的 Debug 全量）**复现了同样的 14 条红**，所以那份日志记录的其实是**真红**，
> 只是文件名把它叫成了 green。结论修正为：文件名骗人，但内容不骗人；
> 该日志应作为 **I-6 缺陷的早期证据**引用，而不是"不予引用的假绿证据"。详见第六节。

---

## 六、本轮第一次真验证打出的缺陷：I-6 半成品本身是错的（已修）

### 现象

`cmake --preset debug` + `cmake --build --preset debug -j1`（零 warning）后跑全量：

```
$ ctest -C Debug -j1
98% tests passed, 14 tests failed out of 634
Total Test time (real) = 364.84 sec
```
14 条红**全部**是 `MainWindowTest.*`，全部红在 `tests/test_mainwindow.cpp:70` 的落点断言：
```
error: Expected equality of these values: landed / want
```
把 `landed` 的 UTF-16 字节解出来是 `KEY_CURRENT_USER\Software\GIST…`（注册表路径），
`want` 是 `D:\Code\MyProject\GIS_TOOL\build…` 下的私有目录。

### 根因（推翻半成品注释与终审第 2 席对 I-6  premises 的描述）

`QSettings(org, app)` 这个**两参构造走的是 NativeFormat**，既不吃
`setDefaultFormat(IniFormat)`，也不吃 `setPath(IniFormat, UserScope, …)`
⇒ 半成品拿它当"重定向是否生效"的被测对象，等于断言一个本来就不归 ini 重定向管的东西，**恒红**。
所以 I-6 的修法方向（重定向上移到 main()、按 pid 分道）是对的，但**断言形态是错的**，
而且据此得到的那句因果说明也是错的：

- ✅ 真：`tests/test_main.cpp` 不调 `setApplicationName/setApplicationName`；
- ❌ 假："`settings_manager.cpp:10` 的 `QSettings("GISTool","GISTool")` 会写进用户真实 `%APPDATA%\GISTool\GISTool.ini`"
  —— 它写的是**注册表**（两参构造 = NativeFormat），
  真 GUI 直到 `src/gui/main.cpp:36-37` 才 `setApplicationName("GIS Tool")` / `setOrganizationName("GIS")`。
  这条挂到新账：**测试进程里的 SettingsManager 会写 `HKCU\Software\GISTool`，
  ini 重定向保护不到它**（红线不许动 src，本轮不修，只登记）。

### 修法（`8942ee9`，仍只动 `tests/**`）

把被测对象换成**显式四参**构造 `QSettings(QSettings::IniFormat, QSettings::UserScope, "GISTool", "GISTool")`，
其余三层兜底（main() 无条件重定向、`privateSettingsDir()` 按 pid 分道、反向禁止真 `%APPDATA%`）原样保留。
中途还撞了一次 API 幻觉：`QSettings::filePath(...)` **不是 Qt 成员**（`error C2039`），
⇒ 那一次构建失败、`gis_tests.exe` 停留在 `07:44:52` 的旧二进制上，
我照红线判它作废、**没有**引用同一次跑出的 `t21Fix2_ctest_mw_afterfix.log`（它显示的仍是 14 条红，正是旧二进制跑旧断言的结果）。

### 修后实测

```
$ cmake --build --preset debug -j1        # 零 warning
$ ls -l build/debug/tests/Debug/gis_tests.exe   → 2026-10-19_08:01:24   （源码 07:58 < 产物 08:01:24）
$ ctest -R MainWindowTest -C Debug -j1
100% tests passed, 0 tests failed out of 14
Total Test time (real) =   8.06 sec
```

### 附带收口：`tests/CMakeLists.txt:1398` 的腐烂分母

已提交内容里还留着一处同族 K-21："与 gtest_discover_tests 给其余 **498** 条设的那道跳过识别保持一致"。
现跑 `ctest -N` 的总数是 634/632，498 无从按定义复算 ⇒ 改为不抄数字、只写"其余全部 gtest 用例 + 要复算就跑 `ctest -N`"。

---

## 七、`RESOURCE_LOCK` 并发对拍（`67dc8c3` 的验收）

被测对象：`GuiContract.ExportActionVisibleKeys`（导出 `gui_action_visible_keys.txt`）
与 `param_guard_visible_keys_all_read`（读同一份产物做反向守卫）。

### 带锁（已提交态），`-j8`，3 轮

```
$ for r in 1 2 3; do ctest -R "GuiContract.ExportActionVisibleKeys|param_guard_visible_keys_all_read" \
      -C Debug -j8 -V > ../t21Fix2_lockv_round$r.log 2>&1; done
round1 rc=0 passed=100%   rows=438 violations=0 visible_rows=438
round2 rc=0 passed=100%   rows=438 violations=0 visible_rows=438
round3 rc=0 passed=100%   rows=438 violations=0 visible_rows=438
```
守卫读数三轮**逐字一致**（`actions=126 visible_rows=438 no_ui_config=39 violations=0`），rc 全 0。
另有 3 轮不带 `-V` 的同款对拍（`t21Fix2_lock_round{1,2,3}.log`）同样 rc=0。

### 去锁（一次性临时实验，非提交内容）

临时把两处 `RESOURCE_LOCK gis_action_visible_keys` 摘掉 ⇒ 顺带撞出一个**新事实**：
`set_tests_properties(<name> PROPERTIES)` 若属性被清空（注释会被 CMake 剥掉），
configure 直接 `CMake Error at tests/CMakeLists.txt:1404 (set_tests_properties): illegal arguments`。
⇒ "只把锁名改成注释"这种去锁写法根本配不过去，本次实验是把整个属性块删掉才跑起来的。

去锁后 `-j8` 连跑 6 轮：
```
round1..6 rc=0  readings=[violations=0 visible_rows=438]  100% tests passed, 0 tests failed（每轮）
```

### 结论（如实写）

**未能在本机复现竞争。** 6 轮去锁与 3 轮带锁的守卫读数完全相同，都是 438/0，
⇒ 本轮**没有**拿到"去锁会红、加锁才绿"的正向对照实验，
因此**不能**写成"已证明 `RESOURCE_LOCK` 有效"。
能支撑的只是较弱的一条：`67dc8c3` 加锁后这 3 轮读数稳定且 rc=0，
锁没有引入回归、也没有把并发读数改坏；竞争窗口在这台机器上没有被 `-j8` 打开
（导出进程 ~0.4 s 写完 vs 守卫进程 ~3.9 s 才开始读，本机时序差可能大于竞争窗口）。
这条按"已提交内容的验收未拿到失败对照"登记进未完成项，交由下一轮在慢盘/高负载下重做，
或改由"锁存在性"的结构断言（直接校验 CTestTestfile 里两条用例都挂了同名锁）来兜。

---

## 八、`%APPDATA%\GISTool` 跑前 / 跑中 / 跑后均不被创建（I-6 的机器级证据）

```
开工（任何测试之前）：
$ ls -d "$APPDATA/GISTool"
ls: cannot access 'C:\Users\TDT-DELL\AppData\Roaming/GISTool': No such file or directory
Debug 全量跑到 439/634 时（中途抽查）：
$ ls -d "$APPDATA/GISTool"
ls: cannot access 'C:\Users\TDT-DELL\AppData\Roaming/GISTool': No such file or directory
两棵树全量 + 9 轮并发对拍跑完（最终取证，见第十节）：
$ ls -d "$APPDATA/GISTool"   →（同上，No such file or directory）
```
`$APPDATA` 展开为 `C:\Users\TDT-DELL\AppData\Roaming`（已打印核实）。
⇒ 判据二的机器级兜底成立：不是"多写一处调用"，而是**全过程中那个目录从未出现过**。
`build/t21Fix/fakeappdata/` 是上一轮为此准备的假根，**目录为空**，其本身不构成证据，以现跑 `ls` 为准。

同时按第六节的更正登记：这个证据保护的是 ini 形态的 `QSettings`；
`SettingsManager` 的两参构造走注册表，**不在** `%APPDATA%\GISTool` 这条路径上，故"没建目录"
**不等于**"测试没有写用户状态"——注册表侧留作新账。

---

## 九、I-3 判据的失效形状（可直接复算，不需重编）

注入态消息 `tile profile "rasterized" is not supported yet; only profile=mercator is implemented` 上：

```
$ python -c "m='tile profile \"rasterized\" is not supported yet; …'; print(m.find('raster'))"
old判据 find("raster")                                       → MATCH（假绿）
new判据 find('tile profile "raster" is not supported')        → NO MATCH（红）
```
⇒ "消息含子串"这条判据在 GDAL/profile 语境下确实会因错原因变绿；新判据把它抓红。
上一轮在注入态二进制上的实测红同样成立：`build/t21Fix_inj6_wrongreason_red.log`。

---

## 十、四行结论

**① 5 个半成品文件的收/退判定**
- `tests/test_support.h`、`tests/test_support.cpp` → **收**（I-6：pid 私有落点算式）。
- `tests/test_main.cpp` → **收但改**（I-6：机制层原样收下，注释里被证伪的因果说明由本轮改写；提交 `8942ee9`）。
- `tests/test_mainwindow.cpp` → **收但必须修**（I-6：断言形态错用两参构造 ⇒ 14 条真红；本轮改为显式四参后 14/14 绿）。
  这是本批唯一"收下后发现是坏的"的一件，且**只有真验证才能发现**，代码审查放过了它。
- `tests/test_plugins.cpp` → **收**（I-5 反演自证已消除并核到源值 8；I-3 两对同名输出已改名 + 字面整句 + 反向字面文件名；I-1 六处已清零）。
- 四判据逐条结论：判据一（无语义夹带）**过**；判据二（机器级兜底）**过**，见第八节；
  判据三（字面路径 + 错原因红）**过**，见第五/九节；判据四（K-21 零命中）**过**，见第四节。
  另在已提交内容里补清一处同族腐烂分母（`tests/CMakeLists.txt:1398` 的"498 条"）。

**② 两棵树全量数与 warning 数 + 新鲜度三个时间戳**
- Debug：**100% tests passed, 0 tests failed out of 634**（`build/t21Fix2_ctest_debug_final.log`，Total 365.12 s）。
  新鲜度链：源码 `tests/test_mainwindow.cpp 08:00:51` < 重链 `build/debug/tests/Debug/gis_tests.exe 08:01:24`
  < ctest 日志 `08:08:54`。
- Release：**100% tests passed, 0 tests failed out of 632**（`build/t21Fix2_ctest_release_final.log`，Total 180.79 s，
  `***Failed|***Timeout` 计数 = **0**）。
  新鲜度链：源码 `08:00:51` < `build/release/tests/Release/gis_tests.exe 08:02:35` < ctest 日志 `08:13:56`。
- 全程**无并发构建**：Release 重编在 08:02:35 结束，之后才起 Debug 全量；Debug 全量结束后才起 Release 全量；
  对拍轮次（08:09:27 起）也在两棵树的构建都停稳之后。一切 `-j1` + `MSBUILDDISABLENODEREUSE=1`。
- 两棵树重编日志 `t21Fix2_build_both.log` / `t21Fix2_build_release_fix.log`：`grep -icE "warning (C[0-9]+|LN[0-9]+)"` = **0**。
- 用例数**未因新断言而变**：新增的都是既有用例内部的 EXPECT/ASSERT，没有新增 TEST；
  夹具改名也只动文件名 ⇒ `ctest -N` 仍是 634 / 632（Debug 末号 `Test #634: GuiContract.ExportActionVisibleKeys`、
  Release 末号 `Test #632`），与工单预期一致。
- 反面登记：中途有一次构建失败（`error C2039: "filePath" 不是 "QSettings" 的成员`），
  当时 exe 停在 07:44:52 旧二进制 ⇒ 那一次 `t21Fix2_ctest_mw_afterfix.log` 按红线**作废、不予引用**。

**③ RESOURCE_LOCK 三轮对拍**：带锁 3 轮 `-j8` 读数逐字一致 `actions=126 visible_rows=438 no_ui_config=39 violations=0`、rc 全 0；
去锁 6 轮 `-j8` **同样是 438/0 且全绿** ⇒ **未能在本机复现竞争**，不写成"已证明锁有效"（详见第七节）。
去锁实验另带出一个可用的事实：属性清空后 `set_tests_properties(<name> PROPERTIES)` 会在 configure 阶段报 illegal arguments。

**④ `%APPDATA%\GISTool`**：开工前、Debug 全量中途（439/634）、两棵树全量 + 9 轮对拍之后，
三次 `ls -d "$APPDATA/GISTool"` 全部 `No such file or directory`（第八节贴了原文）。

---

## 十一、未完成项与原因

1. **`RESOURCE_LOCK` 的失败对照实验没拿到**（第七节）。本机 `-j8` 打不开竞争窗口：
   导出进程约 0.4 s、守卫进程约 3.9 s 且守卫晚启动，时序差大于窗口。
   建议下一轮改用结构断言（校验 `CTestTestfile.cmake` 里两条用例都挂了同名锁）来给 `67dc8c3` 兜底，
   或在慢盘/高负载 runner 上重做去锁对拍。
2. **新账（src 侧，本轮红线不许动）**：`src/gui/settings_manager.cpp:10` 用两参构造
   ⇒ `SettingsManager` 在测试进程里写的是注册表 `HKCU\Software\GISTool`，ini 重定向保护不到它；
   需要 src 侧改成显式 `QSettings(IniFormat, UserScope, …)` 或测试侧另加注册表隔离，才能说"测试不写用户状态"。
3. **I-3 的"注入错原因时红"未在本轮新二进制上重做**：本轮只复核了字面整句与源字符串一致
   （`cutting_plugin.cpp:1143`、`vector_plugin_spatial_join.inc:34` + `vector_plugin_common.inc:89`）
   并给了不需重编的判据对照（第九节）；红态证据沿用上一轮的 `t21Fix_inj6_wrongreason_red.log`。
   零重编的真注入路径本轮没找到（夹具由用例自己写盘，事先删不掉）。
4. **`tests/test_support.cpp::privateSettingsDir()` 的 POSIX 分支不可编译**：`#else` 用了 `::getpid()`
   但该文件只在 `#ifdef _WIN32` 下包 `<windows.h>`，没包 `<unistd.h>`。本机 win32 走不到这条分支，
   本轮不修以免扩面，登记。
5. `build/` 根仍有数百份跨批次旧产物（`a0*` `t10r*` `t11rr*` `fix2_*` 等），不属于本批范围，未清理。

## 十二、下一步第一条命令

```
cd /d/Code/MyProject/GIS_TOOL/build/debug && ctest -N -C Debug 2>&1 | tail -2
```

