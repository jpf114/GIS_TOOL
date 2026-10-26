# v1.0.0 发布检查清单

## 发布前验证（本地）

### 1) 构建与全量测试

```powershell
vcpkg install --triplet x64-windows --x-manifest-root=$PWD
cmake --preset release --fresh
cmake --build build/release --config Release
cmake --build build/debug --config Debug

ctest --test-dir build/release -C Release -j1 --output-on-failure
ctest --test-dir build/debug -C Debug -j1 --output-on-failure

# 四条真实数据回归已进 ctest（N112 销账，用例名 real_data_raster / real_data_matching /
# real_data_vector_quick / real_data_vector_full）⇒ 上面两条全量就已经跑到它们，
# "改完判据后是否还跑全靠人记得"这一层没有了。下面这一行只保留给"要看某条腿完整 stdout"
# 或"单跑 raster/matching 的 full 模式"（那两个 custom target 仍不在 ctest 里）的场合。
cmake --build build/release --config Release --target real_raster_regression real_matching_regression real_vector_regression
```

`-j1` 与"跑全量期间任何一棵树都不许并发构建"是同一条要求：基线的用时数（见"CI 超时余量"节）
是 `-j1` + 机器安静时测的，开并行会造出**假 Timeout**（本批实测 `ctest -j 8` 叠加另一棵树构建
产出 3 条假失败），照那种跑法去对基线会误判。

`ctest` 必须带 `-C Debug` / `-C Release`。不带时的实际形态（本机 2026-10-21 实测，原文见下）
**不是"两个配置混着跑"，而是更坏的静默失效**：`$<CONFIG>` 展开成空串 ⇒
gtest 发现文件的名字少了配置段，`ctest` 报"找不到 include 文件"并**列出 0 条用例**（连 `Total Tests` 都没有）
⇒ 读起来像"没有用例 ⇒ 没有失败"。实测输出：

```
$ ctest --test-dir build/debug -N
CMake Error at …/build/debug/tests/gis_tests[1]_include.cmake:1 (include):
  include could not find requested file:
    …/build/debug/tests/gis_tests[1]_include-.cmake
Errors while running CTest        # 命名列出数 = 0，且没有 "Total Tests" 行
```
跑全量前先用 `-N` 记注册数：**当前基线 Debug 749 / Release 747**（2026-10-26 批次 C C3k-3 双树全量轮现读；
Release 侧这次是**全目标重建之后**现读的 —— 748/746 那一轮就是重建后取的，C3k-3 把 quick 档最贵的两条重活
拆成新腿 `real_data_vector_join_heavy`（+1 条 `add_test`）⇒ 749/747；Release 恒比 Debug 少 2 条是既有的
debug-only 用例，不是漏跑）。此前一档 **748 / 746**（C3c 收尾那三条 `TEST_F`；Release 那时刚从"未重编 ⇒ 读 743"
补成重建后现读）。从 745 起 Debug 这一档的 **+3** 依次是
`PluginTest.VectorBufferRoundCapStadiumMatchesHandComputed`（C3c-B 第二片）、
`PluginTest.VectorDifferenceSubtractsOverlayGeometry`（C3c-C1）与
`PluginTest.VectorPolygonizeExecutionCarvesEqualValueRegions`（C3c-C3），三条都是新增 `TEST_F`；
⚠ 上一版这里写 745 / 743，是我在 buffer 那一刀把"没新增 `add_test`"错当成"注册数不变"——
`ctest -N` 的计数含每一条 gtest 用例，加一条 `TEST_F` 就加一条注册）。上一档 744 / 742 是
C3k-2 拆腿轮，再上一档 743 / 741 是 C3n-2d，再上一档 742 / 740 是 C3n-2b、741 / 739 是 C3n-2a、740 / 738 是 C3n-1，两棵树各跑
`ctest --test-dir build/<树> -C <Config> -N | tail -n 1`）。⚠ 上一版这一行写的是 692 / 690，而同一文件下面
"全量墙钟基线"表写的是 693 / 691 ⇒ 两处本来就差 1（本轮 +4 条后实测 697 / 695，与 693 / 691 那一档对得上、
  与 692 / 690 差 1）。**那 1 条的来历是确定的，不必挂下一批**：`692 / 690` 是 B21 把
  `source_mojibake_guard` 落成用例**之前**的旧值（两棵树各 +1 ⇒ 693 / 691），我上一轮只改了下面那张表、
  漏改了这一行 prose ⇒ 差集就是这一条，本轮把两处统一成现读值 697 / 695。Release 少的那 2 条**必须按名字核、不能只看总数**
（只看数会漏"某条在 Release 静默消失"）：`comm -23` 实测差集恰为
`gui_matching_stitch_debug_failure_offscreen` 与 `gui_matching_ecc_register_debug_failure_offscreen`，
反向 `comm -13` 为空 ⇒ 这两条是 Debug 轻量闸门用例（见"批次 A 发布说明 → 基线口径"）。

- **四条 `real_data_*` 也必须带 `-C`**（Task C2 / 预检裁定 R-C2c，本批按裁定**不加兜底**）：这四条的命令里
  同样有 `$<TARGET_FILE:gis-cli>` ⇒ 上面那段"不带 `-C` 时展开成空串"的实测对它们一样成立。
  不看脸色的是**注册与否**（四条恒注册，写在 `if(GIS_POWERSHELL_EXECUTABLE)` 之外，解释器缺失由包装
  `FATAL_ERROR` 判红）；但 `-N` 本身**仍必须带 `-C`**——上一段实测就是证据：不带 `-C` 时整棵
  CTestTestfile 报错、一条都不列，所以"`-N` 里永远有它们"这种说法是错的。
  现读复核：`ctest --test-dir build/release -C Release -R "^real_data_"` ⇒ 4 条 **Passed**、
  一条 Skipped 都不许有（出现 Skipped：raster/matching 腿看 `data/`，vector 两条腿看 `data/vector/`）。
- **`data/` 缺失时这四条报 `Skipped` 而不是 `Passed`**（Task C2 / 预检裁定 R-C2a）：
  三份包装在调 `.ps1` 之前判数据目录，缺失则打印带统一前缀 `GIS_SKIP_TESTDATA_ABSENT: ` 的标记并 `return()`
  （raster / matching 判 `${WORKSPACE_ROOT}/data`；**vector 腿判到 `${WORKSPACE_ROOT}/data/vector`**，
  因为只有它的兜底分支是"退回 3 要素合成样本还全绿"；raster/matching 不能照抄——本仓根本没有
  `data\raster` 与 `data\matching`，一收就永久 Skipped）。用例侧 `SKIP_REGULAR_EXPRESSION "GIS_SKIP_TESTDATA_ABSENT: "` 认领
  ⇒ ctest 打 `***Skipped` 并把它列进 "The following tests did not run"。为什么必须有这一条：三条腿的 `.ps1`
  在真实数据缺失时各自兜底现生成合成样本、照样全绿（就是 `tests/CMakeLists.txt` 里那条
  `SKIP_REGULAR_EXPRESSION "\[  SKIPPED \]"` 注释点名的"退出码 0 ⇒ Passed"假绿形态）。
  ⚠ 检查顺序：**先判解释器缺失（红），再判数据缺失（跳）**。反序时"fresh clone（`data/` 本被 gitignore）
  + 本机没有 powershell"会被记成 Skipped，而 Skipped 不参与失败判定 ⇒ 恒注册的设计被绕过（复审 D 条实测）。
  判别取证（三臂各自的根不同，混用会得到错误结论）：臂 1 的根是**含 `tests/` 而无 `data/`** 的目录
  （用空目录会先撞根校验得到 rc=1，不是 Skip）⇒ `***Skipped***`；臂 2 必须换回**真实仓库根**（数据齐）
  再传 `-DMODE=zzz` ⇒ `***Failed***`（沿用臂 1 的无数据根会得到 rc=0 + 标记，上一版就是这么写的）；
  臂 3 `-DPOWERSHELL_EXECUTABLE=` 留空 ⇒ `***Failed***`。逐条命令与期望 rc 写在各包装头部注释里。
  ⚠ 取证过程没有就地打开 `data/` 下的栅格，但**本轮之前的波段探针已经在 `data/` 下留下 15 个 `.aux.xml`
  边车**（见挂账 N115）⇒ "data/ 一字节未动 / 计数 = 0"这种话从现在起不再成立，实测口径是
  `find data -newermt 2026-10-19 -type f ! -name '*.aux.xml' | wc -l` = **0**（像元与矢量本体未动）。

### 2) 安装态冒烟 —— 前置条件：确认运行库依赖扫描器（N84）

`cmake --install` 需要 dumpbin / objdump 之类**运行库依赖扫描器**才能知道该往包里拷哪些 DLL。
扫描器在**配置期**解析成绝对路径并烘进 `cmake/install_runtime_deps.cmake`（由 `configure_file` 生成）；
找不到时 configure **只给 WARNING**，缺陷要到 `cmake --install` 才现形。⇒ 所以先看配置日志：

```powershell
# 步骤 2.0：重跑一次 configure（不需要重新编译），确认扫描器被解析出来了
cmake --preset release --fresh 2>&1 | Tee-Object build/configure_release.log
Select-String -Path build/configure_release.log -Pattern 'Runtime dep scanner:'
```

⚠ 这里**故意不加 `--fresh`**：`--fresh` 会连 `CMakeFiles/`（含对象文件）一起清掉 ⇒ 下一次构建变成全量重编，
而我们只想重跑一次配置、把扫描器那一行取出来。要看的效果性检查用 `cmake --preset release` 就够。
（第 1 节里的 `--fresh` 是另一回事：那是发布前刻意从干净配置起步，代价就是一次全量重编。）

期望出现一行 `-- Runtime dep scanner: <绝对路径>`（例如 `.../Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe`）。

- **没有这一行** ⇒ 配置日志里会是 `未能在配置期定位运行库依赖扫描器（dumpbin / llvm-objdump / objdump）` 的 WARNING，
  下面第 2.1 步会以 `FATAL_ERROR` **拒绝安装（rc≠0）**。补救：在"Developer Command Prompt for VS 2022"
  （或任何已导出 `VCToolsInstallDir` 的 VS 开发者环境）里重新 configure 一次，再重复步骤 2.0。
  根 `CMakeLists.txt` 会用 `vswhere.exe` 找 dumpbin，**与当前 shell 的 PATH 无关**。
- **有这一行** ⇒ 继续：

```powershell
# 步骤 2.1：安装（--prefix install 写的是仓库根 install/，那是 gitignored 的开发机安装位）
#   这就是本项目唯一的交付路径（云端构建与 NSIS 已停用，见"交付方式"节）。
cmake --install build/release --config Release --prefix install

# 步骤 2.2：装完必须用退出码判定，不要用"目录里有文件"判定
install/bin/gis-cli.exe --list
if ($LASTEXITCODE -ne 0) { throw "gis-cli.exe --list rc=$LASTEXITCODE（0xC0000135 = STATUS_DLL_NOT_FOUND ⇒ 包装不全）" }
# ⚠ `--self-test` 会先过 N101/N102 守卫（src/gui/main.cpp:197-204）：GIS_TOOL_SETTINGS_INI 与
#   GIS_CUSTOM_INDEX_PRESET_FILE 没注入时它**故意**返回 3（免得写真实用户配置）。文档旧版只写了
#   `-platform offscreen --self-test`，照抄会拿到 rc=3 并被误读成"包装不全"⇒ 这里按 ctest 的注入口径补齐。
$smokeDir = Join-Path $env:TEMP ("gis_gui_smoke_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $smokeDir | Out-Null
$env:GIS_TOOL_SETTINGS_INI = Join-Path $smokeDir "gis_gui_settings.ini"
$env:GIS_CUSTOM_INDEX_PRESET_FILE = Join-Path $smokeDir "spindex_presets.json"
install/bin/gis-gui.exe -platform offscreen --self-test
if ($LASTEXITCODE -ne 0) { throw "gis-gui.exe --self-test rc=$LASTEXITCODE" }
```

判据说明（都是 21-E 的实测口径）：

- 一个崩掉的原生 exe 只会设置 `$LASTEXITCODE`，PowerShell 继续往下跑，步骤的退出码取自**最后一条命令**
  ⇒ 以前 CI 的 Verify 步以 `Remove-Item … -ErrorAction SilentlyContinue` 收尾，包起不来也照样绿。逐条断言退出码才是对的。
- 成功时 `gis-cli.exe --list` 的 stdout 实测 **1852 字节 / 27 行**（日志 `build/t21E_V2_cli_list.txt`）。
  **引用这个量请写字节数**：字符数随控制台代码页变 —— 本机 Windows PowerShell 5.1 按 GBK 误解码会得 1230、`wc -m` 得 1014，都不是产物本身变了。
- 包内容参照：`install/bin/*.dll` = **78**（扫描分支实测闭包）。
- 无扫描器的旧行为**已废除**：过去会拷一份写死 36 项的清单、静默产出起不来的包（详见发布说明 N84 条）。

### 3) 交付形态与 Debug 安装分支（N85，批次 B 的 B4 已补齐 Debug）

发布产物**仍只做 Release**（交付形态是项目约束）。但"Debug 安装必然残缺"已由 B4 闭合：
`src/gui/CMakeLists.txt` 的 Qt 平台插件与 `qsqlite` 安装规则原先整块 gate 在 `CONFIGURATIONS Release`，
Debug 包连 `bin/platforms/` 都没有却 rc=0 静默成功；现在与构建侧 POST_BUILD 共用同一组按配置 genex，
`--config` 决定装哪套目录与名字：

```powershell
cmake --install build/debug --config Debug --prefix build/tmp/install-debug
Get-ChildItem build/tmp/install-debug/bin/platforms, build/tmp/install-debug/bin/sqldrivers
# 期望：qwindowsd.dll、headless 插件的带 d 名（q${GIS_QT_HEADLESS_PLATFORM}d.dll，本机该臂构建下即 qoffscreend.dll）与 qsqlited.dll
```

`--config Release` 一侧装的仍是与改前逐字节同名的 release 文件（前后整包文件清单 diff 为空）。
无扫描器的机器上两种配置的 `cmake --install` 都会被第 2 节那条 FATAL **拒绝（rc≠0）**——该行为与配置无关，保持不变。

## 依赖版本快照

发布前导出当前全局 vcpkg 安装版本，附在 Release 资产或提交到仓库：

```powershell
.\scripts\export-vcpkg-versions.ps1
```

默认输出：[docs/vcpkg-versions.lock.txt](vcpkg-versions.lock.txt)

## 交付方式（只保留本机 install）

本项目**不出安装包、不走云端构建**（2026-10-21 决定：一切从简）。交付物就是 `cmake --install` 产出的目录树：

```powershell
cmake --install build/release --config Release --prefix install
```

- 产物：仓库根 `install/`（gitignored 的开发机安装位），里面是 `bin/gis-cli.exe`、`bin/gis-gui.exe`、
  `bin/plugins/*.dll`、`bin/platforms/`、`share/proj`、`share/gdal`、`share/leaflet`。
- 要给别人用就直接压缩这个目录；运行库由安装脚本按**导入表实测**拷贝（见"发布前验证 → 第 2 节"），
  所以 `install/` 本身就是自洽的，不需要 NSIS 再包一层。
- 验证它装对了：`ctest --test-dir build/release -C Release -R "^install_closure$"`（这条用例自己会
  跑一次 `cmake --install` 到 `build/tmp/install_closure_Release` 再逐个 PE 判导入闭包，
  缺件与"装了但没人引用"都会红）。
- ⚠ 仍然不含 VC++ 运行库（`MSVCP140.dll` / `VCRUNTIME140*.dll` 按设计排除）⇒
  目标机需要装过 VC++ Redistributable 或装过 Visual Studio。
- **包体与运行库范围（2026-10-21 现测）**：同一棵 Release 构建树装出来的包是
  **430 个文件 / 192.2 MB**（改前 508 个 / 359.6 MB）。少的 78 个文件、167.4 MB 是**逐字节相同的副本**：
  安装脚本原先在装完 `bin/*.dll` 后，又把 `bin/` 里每个运行库再复制一份进 `bin/plugins/`；
  该复制段已删（`cmake/install_runtime_deps.cmake.in`）。
  删除依据不是"看着用不上"：插件 dll 的导入项按**进程可执行文件所在目录**起解析，而那个目录就是 `bin/`，
  副本从未参与解析。在删掉副本的树里实测 —— `gis-cli --list` 输出与有副本时逐字节一致、
  `vector clip`（真实 22836 要素）、`projection reproject`（要读 `share/proj/proj.db`）、
  `gis-gui`（`QT_QPA_PLATFORM=offscreen` 起事件循环）全部照常；`install_closure` 用例 1/1 绿。
  ⚠ 仓库根那份旧的 `install/`（330.3 MB / 535 文件，上一轮规则装的）仍含这些副本，
  它不是我这一轮该覆盖的目录 ⇒ 重跑上面那条 `cmake --install` 前先手工删掉旧目录，
  否则只增不删、看不出差别。
- **剩下的 78 个运行库本项目裁不动**：它们是 GDAL / OpenCV / Qt 的传递闭包，不是"顺手多带的无关库"——
  `gdal.dll` 直接拖进 arrow / parquet / spatialite / netcdf / hdf5 / poppler / libxml2，
  `opencv_dnn4.dll` 拖进 `libprotobuf.dll`，`Qt6Core.dll` 拖进 `icuuc78 → icudt78`
  （`icudt78.dll` 单文件 31.6 MB，是全包最大的一项）。
  本项目自己的代码只有 3.6 MB（2 个 exe + 13 个插件 dll）。
  要再小只能重编 GDAL/Qt 关掉用不上的驱动，那要动 vcpkg 树 ⇒ 与"不回退/不替换依赖"的裁定冲突，不在本轮范围。
- 已删除的流程载体：`.github/workflows/windows-build.yml`（云端构建）与 `installer/installer.nsi`
  （NSIS 脚本）。历史文档里提到这两处的段落一律按"已停用"读。

## 批次 A 发布说明（测试基建 + 算法缺陷修复，2026-10-14 ~ 2026-10-19）

本节的每条都有任务报告或代码/测试出处；数字一律按 21-D 现读现核的口径书写（引用 stdout 的量一律用**字节**）。

### `cutting.tile`（切片）—— 十条用户可见变化

1. **此前三条 profile 全部零输出却报 Success。** 三个 profile 的瓦片高度
   `(tileMinLat - tileMaxLat)/(-wgt[5])` 恒负 ⇒ 每片走 `continue`，而计数器照样 +1，
   于是出现 `Success: … N tiles` 而 `output/` 下一个瓦片文件都没有（取证：非 metadata 文件数全为 0）。
   现在真写得出瓦片，`tiles_generated` 见第 8 条。
2. **`profile=geodetic` / `profile=raster` 改为显式失败**（文案点名 profile 值）。此前两者静默按 Web Mercator
   网格索引，产出的是一套错网格的图。⇒ GUI 下拉**仍供三个取值**（enumValues 未收窄），选到这两个现在会拿到失败而不是产物。
3. **`metadata.json` 的 `bounds` 在 `profile=mercator` 下此前经纬度写反**（WGS84 目标 CRS 未设
   `OAMS_TRADITIONAL_GIS_ORDER`，GDAL 3 把 lat 放在 x），且经度会被纬度钳位截到 `85.05`。
   ⇒ 按 `bounds` 摆地图的用户此前会摆到错误位置；**webviewer 预览中心点同错**。现在 `bounds` 是传统 `(lon, lat)` 顺序。
4. **不再为每个层级物化全球网格。** 临时文件 `gis_tile_level_<z>.tif` 从此消失，高层级改为按瓦片窗口读取。
   ⇒ 耗时**性质**改变：此前 z=7 单层实测 20.0 s、外推 z=10 ≈ 21 min/层、逻辑尺寸 275 GB（"tile 在 z≥10 实际不可用"
   是批次 A 之前的事实）；现在按瓦片数计。这条既是性能面也是产物面（临时目录占用、取消时的残留行为都变了）。
5. **`overview_resampling` 现在真正生效**（驱动逐瓦片 `cv::resize` 插值，支持 `nearest/bilinear/cubic/average`）。
   **`cubicspline` / `lanczos` / `mode` 改为显式拒绝**。⚠ **破坏性**：老脚本传这三项此前"静默用默认值"，现在**报错退出**。
6. **`num_threads` 从 `gis-cli cutting --help` 消失**（已从 `paramSpecs()` 删除）。此前填了不生效，属死参数。
7. **旋转栅格（`gt[2]` / `gt[4]` 非零）改为显式失败。** 此前会静默算错读窗。与第 5 条同类：从"静默出错"变"显式报错"。
8. **`tiles_generated` 改为实际写出的文件数**，并**新增 `tiles_skipped`**。此前 `tiles_generated` 把 `continue`
   掉的片也计数，与产物数无关。
9. **Release 版 `--output_format=jpeg` 曾挂死并留下 0 字节产物**（已修）。根因在**部署层**：构树里 debug 版
   libjpeg 覆盖 release 版，跨 CRT 造成 UB ⇒ **不是 `cutting.tile` 的动作逻辑缺陷**，但用户可见。
   口径：拉取本修复后执行一次 `cmake --build --preset <cfg>` 重链即可。
10. **多波段保留彩色 + 整幅一次统计。** 此前多波段被降成单波段灰度、每张瓦各自 min/max 拉伸 ⇒ 同一地物跨瓦片/跨层级亮度不一致；
    现在读取多波段并按整幅统计做统一拉伸，`add_alpha` 也是真 alpha（此前该开关是空操作：三元两支同为 255）。
    代价实测：真实数据端到端 **+21.7%（544 ms vs 447 ms 均值，上界，含多写一个波段）**。

### 分水岭分割：外部标记改按 OpenCV 约定（数值口径变化）

`processing.watershed` 的外部标记分支现在按 `cv::watershed` 的约定读标记：`1` = 确定背景、`>= 2` = 各区域种子、`0` = 未知区。

⚠ **按自家旧文档准备 `marker_input` 的用户，`segment_count` 会从 `2` 变 `1`。** 旧文档写的"`0` = 背景 / `1,2,3…` = 区域编号"
与 OpenCV 约定相反：`0` 被算法当成未知区整片淹没，而编号 `1` 的那个区域被当成确定背景、不计入 `segment_count`。
⇒ **外部标记请按 OpenCV 约定准备**。程序**不做任何"约定转换"**：用户标记里的 `0` 从像元值上无法区分"背景"还是"未知"，
自动 +1 会把刚修掉的缺陷原样改回去（这条是批次 A 的定案，后来者不要"顺手"加偏移）。
输出侧的标签口径与 `segment_count` 统计范围见 [算法说明/影像处理与分析/分水岭分割.md](算法说明/影像处理与分析/分水岭分割.md)。

### 视域分析（`terrain.viewshed` / `terrain.viewshed_multi`）

- **单点视域默认启用地球曲率与大气折射修正**：新增/生效参数 `curvature_coeff`，默认 `0.85714`（GDAL 合并系数），传 `0.0` 关闭 = 批次 A 之前的行为。
  ⚠ 这不是"精度微调"，是**远距可见性大幅收紧**：同幅 60×60@1000 m 平坦 DEM、`max_distance=30 km`、观察高 10 m，
  可见像元 **2819 → 517**。老用户重跑同一工程会得到不同结果。
- **两个视域动作的口径不一致（已知，未在本批统一）**：多点 `viewshed_multi` 的曲率系数仍是硬编码 `0.0`（乐观），
  所以同幅 DEM 上单点与多点的可见性**不可直接比较**；`curvature_coeff` 也因此**不进** `viewshed_multi` 的可见键
  （面板上放一个改了没用的参数更糟）。
- **CRS 告警口径**：视域的曲率修正按米算距离，输入 CRS 是地理坐标系（或没有 CRS）时修正会静默失效 ⇒
  现在**非投影 CRS 会告警**：`metadata["crs_warning"] = "observer analysis expects projected CRS"`，并追加进 `result.message`（成功时也说）。
- **单点输出的 NoData 语义**：单点视域结果带 `NODATA = 0.0` 声明 ⇒ **"不可见"像元同时也是 NoData**，
  渲染链路会把不可见当透明；且"被遮挡"与"超出最大距离"写的是同一个值（0.0），**在输出里不可区分**。
- **`viewshed_multi` 的输出数据类型变了**（用户可见）：改前走 `matToGdalTiff` ⇒ **GDT_Float32** 的 0/255 掩膜，
  并无条件继承源 DEM 的 NoData 哨兵；改后由插件内 `writeViewshedMaskAsByte` 写 **GDT_Byte** 且**不声明 NoData**。
  ⇒ 手里已有的多点视域产物重跑后**类型与 nodata 声明都变了**：下游若按 Float32 读、或按 nodata 掩膜做透明渲染，会看到不同结果。

### 全色锐化（`processing.pansharpen`）

- **方法取值更名（破坏性）**：`simple_mean` → **`mean_blend`**。旧值不再被接受，而是走**显式拒绝**路径给出可操作错误信息
  （不是静默改名）。GUI 中文标签同步改为"PAN-MS 均值混合"；两处回归脚本的参数值已迁移。
- **新增用户可见提示**：PAN 与 MS 的地理包络相对差异超过容差（较大边长的 `1e-6`）时，写
  `metadata["pan_ms_extent_mismatch"]=<相对差异>` 并把提示拼进 `result.message`；**动作仍然成功**（不硬失败），
  理由是同传感器 PAN/MS 正射后常有厘米到米级差异，硬失败会打断正常用法。
  ⚠ 这条保护**有已知空白**：两侧都没有地理变换、或**只有一侧有变换**时都不报警，且后者会让输出地理参考退化成 GDAL 默认的 `Pixel Size = (1,1)`
  ⇒ 准确说法是"**缺地理参考的输入得不到范围校验保护**"，不是"任何范围不一致都会提示"。

### K-Means（`processing.kmeans_segment`）：旧别名 `k` 已废弃

`kmeans` 的 `k` 别名已从代码里移除，请改用 `kmeans_k`。⚠ 旧写法 `--k=9` 的行为分两个阶段：**B14 之前**不报错、被忽略（静默按 spec 默认 `4` 算）；**B14 之后**未声明键（含旧别名 `k`）由 CLI **拒绝并点名**（`rc=1`），而工作流侧未命中的键仍透传。
影响面：面板与 CLI 今天都已不提供键 `k` 的入口 ⇒ 受影响的是**历史脚本 / 直接调库**的调用方，不是界面上的旧参数。

### 矢量检查：`duplicate_point_check` 支持 MultiPoint（计数口径）

支持 MultiPoint 之后，`metadata` 的 `scanned_count` **仍是"要素数"**（每个要素 +1，不因它含多个点而变），
而 `issue_count` 与 CSV 行数按点对计 ⇒ **`issue_count` 可以超过 `scanned_count`**，这不是缺陷。
**没有为此新增任何参数键或 metadata 键**（口径靠文档固定）。

### 打包与安装（N84 / N85 / N87）

- **N84 已修：装哪些 DLL 不再由"打包者那一刻的 PATH"决定。**
  改前的两条缺陷：① 扫描器靠 `find_program` 在 PATH 里找 ⇒ 包内容随打包环境的 PATH 变化；
  ② 找不到扫描器时**静默拷一份写死 36 项的清单**继续安装 ⇒ 装出**起不来的包**：`gis-cli.exe --list` 返回 `0xC0000135`
  （STATUS_DLL_NOT_FOUND），而当时 `cmake --install` 与 CI 的 Verify **都是 rc=0**，installer.nsi 无脑打包 `install/`，坏包一路绿着发到用户手里。
  数量口径：扫描分支实测闭包 **78 项** vs 静默清单 **36 项**；按"直接导入"口径漏 **35 个**（58 − 23），
  按 78 项闭包口径**漏 47 个、还多塞 5 个没人导入的**（Qt6Xml / md4c-html / pcre2-32 / pcre2-posix / turbojpeg）。
  ⚠ `78 − 36 = 42` **不是任何口径的结果，请勿引用**。
  改后：扫描器在**配置期**解析成绝对路径并烘进安装脚本（`Runtime dep scanner: <绝对路径>` 那一行）；
  36 项静默兜底清单**已废除**；无扫描器（或路径过期）⇒ **安装期 `FATAL_ERROR` 拒绝**，宁可装不出来也不要装出坏包。
  （批次 A 当时还把这四步的退出码逐条断言过；那套云端步骤已于 2026-10-21 停用，
  留这句只为说明当时的验证范围。）
  改后实测：`bin/*.dll` = **78**、`gis-cli.exe --list` rc=0（stdout **1852 字节 / 27 行**）。
- **N85（批次 A 记录；批次 B 的 B4 已补齐 Debug 安装分支）：当时安装产物只支持 Release。** Qt 平台插件与
  `qsqlite` 的三条安装规则全 gate 在 `CONFIGURATIONS Release`（批次 A 时位于 `src/gui/CMakeLists.txt:141-162`）⇒
  **Debug 包当时不是可交付物**（连 `bin/platforms/` 都没有）。在没有扫描器的机器上 `cmake --install --config Debug`
  会**从"装出残缺包"变成"直接拒绝"**；这不是 21-E 引入的回归，因为残缺包本来就起不来。
  现行状态（Debug 分支已按配置安装）见"发布前验证 → 第 3 步"。
- **N87：安装包不含 VC++ 运行库 ⇒ 目标机需自行安装 VC++ Redistributable。**
  包内的 `Lerc` / `Qt6Core` / `arrow` 等十几个 DLL 直接导入 `MSVCP140.dll` 等，而这些被
  `cmake/install_runtime_deps.cmake.in` 的 `POST_EXCLUDE` **按设计排除**（不随包分发微软运行库）。
  ⇒ 干净机器上装完本产品的包，缺 `MSVCP140.dll` 会直接起不来；发布说明与安装文档必须写明前置依赖。

### 工具链（N77 / presets）

`CMakePresets.json` 的 `testPresets` 里 `--output-on-failure` 的合法形态是**嵌套 + 驼峰**：

```json
"testPresets": [
  { "name": "debug", "configurePreset": "debug", "configuration": "Debug",
    "output": { "outputOnFailure": true } }
]
```

⚠ kebab 风格的 `output-on-failure` **只是 ctest 的命令行开关名**，不是 preset 键名；把它塞进 `output` 对象同样会被 CMake 拒收。
（早先流传的"键名就是 `output-on-failure`"是错的，别照抄。）

### 基线口径与 `GOLD` 覆盖数的限定

- **现行基线**：`ctest -N` 注册数 **Debug 750 / Release 748**（2026-10-26 批次 C C4b 双树 reconfigure 后现读；
  上一档 749 / 747 是 C3k-3 双树全目标重建之后取的 —— Release 侧"未重编 ⇒ 读 743"那条未验收项在 C3k-3 已清偿）。
  逐档账：C3m 收口 735 / 733（那一档 **+4** = 三条 `Vector{Centroid,Boundary,ConvexHull}GpkgSpatialIndexIsPopulated`
  + 一条反回潮守卫 `VectorSourceNeverDisablesGpkgSpatialIndex`，详见第 2 节）⇒ C3n 系列 **+8**（C3n-1 +5、C3n-2a/2b/2c/2d +3：`VectorConvexHullMidFailure…`、
  `VectorDoubleInputOutputMustNotOverwriteSecondInput`、`VectorIntersectRejectedFeatureWrite…`；C3n-2c 是把既有
  clip 失败用例的断言翻契约，不增用例）⇒ 743 / 741 ⇒ C3k-2 拆腿 **净 +1**（删 `real_data_vector_overlay`、加
  `real_data_vector_overlay_buffer_join` 与 `real_data_vector_overlay_ops` 两条）⇒ 744 / 742 ⇒ C3c-A 与 C3c-B 第一片
  的**单元侧**判据改动都不增删注册（真实腿是组内用例，不是新 ctest 名；第一片也没加 `TEST_F`，只把既有用例撑大），
  但 C3c-B 把 `simplify_roads` 单独成档 **加一条 `add_test`** `real_data_vector_derive_heavy` ⇒ 745 / 743；
  之后 **C3c-B 第二片 +1**（`VectorBufferRoundCapStadiumMatchesHandComputed`，我当时误记为"不变"，已在该条正文更正）、
  **第三片 +0**（dissolve 只撑大既有块）、**C3c-C0 +0**（只翻登记表一行）、**C3c-C1 +1**
  （`VectorDifferenceSubtractsOverlayGeometry`）、**C3c-C2 +0**（撑大既有 join 用例与夹具）、
  **C3c-C3 +1**（`VectorPolygonizeExecutionCarvesEqualValueRegions`）⇒ **Debug 748** ⇒ 其后 C3h-2b 两片
  **+0**（只往既有用例里加断言、没加 `TEST_F`；C3h-2b2 只改 runner 与档闸）⇒ **748 / 746**（Release 那次也是
  全目标重建后现读，746 = 748 − 2 条 debug-only）⇒ **C3k-3 +1**（`real_data_vector_join_heavy` 一条新腿）
  ⇒ **Debug 749 / Release 747** ⇒ **C4b +1**（文档参数表↔二进制守卫 `docs_match_cli`，载体是
  `cmake -P` 而不是 gtest ⇒ 两棵树各多 1 条注册，不受 debug-only 那 2 条影响）⇒ **Debug 750 / Release 748**。
  两树差集仍恰为下面那两条 Debug 轻量闸门、`comm -13` 空；快照 `build/tmp/c3k2_names_{debug,release}.txt`。
  ⚠ 紧随其后的 **C3m 评审席修复轮不动注册数**（守卫改的是匹配逻辑与 token 名单，两条都是注释/判据成形改动，
  没有增删用例）⇒ 735 / 733 那一档的墙钟与"为什么不能拿它判余量"见第 2 节"C3m 评审席修复轮"；
  上一档 731 / 729 是 C3h-3（+1 = `real_data_vector_union_poly`）；再上一档 730 / 728 是 C3l（+6 = 六条 `PluginTest.Vector*GpkgSpatialIndexIsPopulated`）；再上一档 724 / 722 是 C3h-2 收口轮（C3h-1b 复验同值——那一刀只翻登记表一行的状态列，不增删用例）；
  相比再上一档 723 / 721 **+1** = C3h-2 的 `PluginTest.VectorUnionMixedDimensionCarriesAttributesPerPiece`；再上一档 722 / 720 的 +1 = N122 的 `PluginTest.VectorRemoveStaleShapefileSidecarsUnderNonAsciiPath`；再往上一档 719 / 717 的 **净 +3**，
  由两刀合成：C3k 把一条 `real_data_vector_full` 换成 `checks`/`overlay` 两条（−1+2 = **+1**），
  C3i 新增 `PluginTest.VectorBufferRejectsMissingSrs` 与 `PluginTest.VectorDissolveRejectsMissingSrs`（**+2**）。
  上一档 719 / 717 是同日 C3h-1 收口轮（+6 = `PluginTest.VectorUnion*` 六条，逐名比对得出）；再上一档 713 / 711 是同日 C3j（+8 = N132/N133/N134 三刀的
  4 条 `VectorRasterize*` + 4 条 `RasterInspect*`）；再上一档 705 / 703 是 C3g（+5 = dissolve/rasterize 的前置校验与元数据用例）；批次 A 收尾时是 635 / 633，
  见下一条认证记录——本条只认现读值；上一版这里写 692 / 690、而"全量墙钟基线"表写 693 / 691，两处差 1，
  当时起重读为同一组现值；那 1 条 = B21 新增的 `source_mojibake_guard`，
  上一轮只改表没改这行，本轮已对齐，不留待核）；`comm -23` 差集仍恰为
  `gui_matching_ecc_register_debug_failure_offscreen`、`gui_matching_stitch_debug_failure_offscreen`（Release 侧无独有项；
  C3f 轮按名字复核过：`build/tmp/c3f_names_debug.txt` 700 行、`build/tmp/c3f_names_release.txt` 698 行，
  反向 `comm -13` 为空，且两条新用例在两棵树的名字清单里都在）。
  ⇒ Release **恒少 2** 是注册数口径，不是"少通过 2 条"；两侧各自 100% 通过。
- **批次 A 最终基线认证（21-D 收尾现跑，两棵树重编后各跑一次 `-j1` 全量）**：
  Debug `100% tests passed, 0 tests failed out of 635`（382.54 s）、
  Release `100% tests passed, 0 tests failed out of 633`（188.41 s）；
  两棵树的构建日志均 **0 warning / 0 error**；日志 `build/t21D_build_debug.log`、`build/t21D_build_release.log`、
  `build/t21D_ctest_debug_full.log`、`build/t21D_ctest_release_full.log`。
- **凡引用 `GOLD` 覆盖数，必须带两条限定：N90 已销、N108 未销。**
  N90（"块体注释里写一句 `EXPECT_NEAR` 就能顶替被删掉的真断言"）**已由批次 B 的 B8 关闭**：判据
  `blockHasNumericAssertion`（`tests/test_coverage_meta.cpp:517-524`）不再扫块体原文，而是读 `lexCppViews`
  同一趟产出的 `blanked` 视图（注释字符与字面量内容都已抹掉，两份视图等长、下标可互换）；
  这一形状由四条机器判据钉住（`CoverageMetaJudge.LineCommentOnlyNumericAssertionIsNotCounted` /
  `BlockCommentOnlyNumericAssertionIsNotCounted` / `AssertionNameOnlyInsideStringLiteralIsNotNumericEvidence` /
  `AuditStripsCommentsOnBothCriteriaEndToEnd`，提交 `fb26cd7` → `57fcf3a` → `65df19e`）。
  ⚠ **但 GOLD 数仍不等于"N 个 action 的数值口径已被独立钉住"**：N108（期望侧的值取自被测产物 = 自指期望）
  到今天没有任何一臂在守，见下面挂账——所以限定句从"判据可被注释满足"换成"判据不吃自指期望"。
  引用时请写成**登记表自报口径**：`tests/action_gold_cases.txt` 126 个数据行自报 **GOLD 66 / PENDING 60**（文件另有 51 行注释、0 空行，`wc -l` 得 177）。
  计数请用"排除注释行后再数 `[,]GOLD`"（=66）与"排除注释行后再数 `[,]PENDING`"（=60，与 `grep -c ',PENDING$'` 同值）；
  直接 `grep -c ',GOLD'` 会得 **67**（第 2 行的格式说明含 `,GOLD`，本表新写的注释一律改写成 `[:]GOLD` 来避开这个字面量）。
  上限断言现值 `constexpr int kPendingCeiling = 60`（`tests/test_coverage_meta.cpp:245`，断言落在
  `CoverageMeta.PendingCountDoesNotGrow` 的 `EXPECT_LE(pending, kPendingCeiling)` `:863`），
  且头部 `COUNT pending_rows` 与这个常量由 `CoverageMeta.RegistryHeaderCountsMatchData`（`:873`，对撞段 `:945-951`）
  钉成同一个数 ⇒ "改表忘改上限"这个唯一的静默放宽方向已经红得出来。只允许下降。
- **冻结清单复核结论（21-D，只核不改）**：批次 A 全程 `tests/action_gold_cases.txt` 只有 Task 3 的两个提交动过它
  （`f1bb676` 建档、`987eaa9` fix round 1 只**加第 4 列证据用例名**）；
  两版的 GOLD/PENDING 都是 **52 / 74**（**那是批次 A 当时的数**，与上面那条现值 66 / 60 不是同一档，别混读），
  `diff` 只显示列数变化 ⇒ **没有任何 action 从 PENDING 变 GOLD**（"不反向扩标"守住了），
  **不需要动上限断言**。另核：`EXPECT_LE(pending, 96)` 那一版从未进过代码
  （`git log -S'EXPECT_LE(pending, 96)' -- tests/test_coverage_meta.cpp` 零命中），96 只是计划期的估算。

### 全量用时与单条用例超时（本机；云端构建已停用）

- 云端构建已删除 ⇒ 这些数字**只用于本机跑全量的预期管理**（多久该跑完、哪条用例快撞超时），
  不再对应任何 `timeout-minutes` 步骤。
- **`TIMEOUT`（测试属性）是本工具链唯一的执法者**；`PROCESS_TIMEOUT` **无效** —— 它不是 CMake 属性
  （`cmake --help-property-list` 里有 `TIMEOUT`、`TIMEOUT_AFTER_MATCH`、`TIMEOUT_SIGNAL_NAME`、`TIMEOUT_SIGNAL_GRACE_PERIOD`，没有 `PROCESS_TIMEOUT`），
  接受了也不执行。现值：`GIS_TEST_TIMEOUT = 300`，configure 期用例经 `set_property(TEST …)`、
  gtest 发现用例经 `PROPERTIES`，两侧读同一个变量不会漂移。取证请用文本锚点（行号会随本文件增删失真，
  上一版这里写的 `:155-170,1449-1461` 就是漂了的）：`tests/CMakeLists.txt` 里搜
  `set(GIS_TEST_TIMEOUT 300)`、`gtest_discover_tests(gis_tests` 块内的 `PROPERTIES TIMEOUT`、
  文件末尾的 `set_property(TEST ${GIS_TIMED_TEST_NAMES}`。
- **300 s 那条线现在有四个例外**（Task C2 / 预检裁定 R-C2b，2026-10-21 把四条真实数据回归纳入 ctest 起）：
  `real_data_raster` / `real_data_matching` 各 **60 s**、`real_data_vector_quick` **720 s**、
  `real_data_vector_full` **780 s**。数值不是拍的：取"整条腿墙钟"（`cmake -P` 包装的进程用时，含 `.ps1`
  里 `Sync-PluginDlls` 与生成 `tmp/*_benchmark` 的开销，**不是**用例内计时合计）的实测最大值按 ≥4×
  向上取整到 60 s 的倍数；三轮样本（Release 冷 11.569/2.047/153.050/115.943 s、Release 热
  7.559/1.634/89.358/84.367 s、Debug 热 11.863/1.751/170.935/190.624 s）与算式逐条写在
  `tests/CMakeLists.txt` 里 real_data_* 那一节的注释。
  ⚠ **为什么这四条不能走 blanket**：文件末尾的 `set_property(TEST ${GIS_TIMED_TEST_NAMES} PROPERTY TIMEOUT 300)`
  会把它们整体改回 300，而 Debug 树实测 vector 两条腿已经 170.9 s / 190.6 s ⇒ 300 只剩 1.76× / 1.57×，
  随时假 Timeout（本仓已实测过并发把 300 s 线撞出 3 条假 Timeout）。所以这四条的名字必须留在
  `list(REMOVE_ITEM GIS_TIMED_TEST_NAMES gui_smoke_startup ${GIS_REAL_DATA_TEST_NAMES})` 的豁免名单里，
  名单走变量、不在两处各抄一遍名字。复算：
  复算**必须读生成物**：`grep 'TIMEOUT "300"' build/debug/tests/CTestTestfile.cmake | grep -c real_data` ⇒ 0
  （⚠ 前提：改过 `tests/CMakeLists.txt` 要先重新 configure，否则读的是过期生成物，会稳定得到假 0）。
  只 grep 源码那条（`grep -A3 "set_tests_properties(real_data" … | grep TIMEOUT`）测不到覆盖——
  有人把豁免名单里的名字删掉、blanket 把 720/780 改回 300 时，它照样打印那四个数。
- **全量墙钟基线**（2026-10-21 Task C2 现跑。云端构建停用后已经没有"步骤超时"这一档 ⇒
  时间上界只有**单条用例的 `TIMEOUT`**：blanket 是 300 s，四条 `real_data_*` 是上面那条例外（60/60/720/780）。
  全量墙钟只用作"再加用例要重跑这一栏"的触发线；⚠ 四条腿进全量后新增的耗时是可分的：
  现读 Debug 那四条在全量里 10.20 + 1.47 + 167.46 + 186.12 = **365.25 s**，
  Release 6.36 + 1.51 + 85.86 + 91.28 = **185.01 s**。
  ⚠ **我先前给的"本机波动 ±25 s"是低估**：同一个 697 条的树，fix round 之前跑 741.52 s、之后跑 **702.01 s**
  （差 **39.51 s**，两轮之间只有注释与包装检查顺序这类改动），Release 同计数 392.67 → **367.32 s**（差 25.35 s）。
  更难看的一笔：把四条腿剥掉后"其余用例"本轮只跑 702.01 − 365.25 = **336.76 s**，
  而上一轮 693 条那一档是 429.86 s ⇒ 同一批用例快了约 93 s，**我给不出因果**（候选是文件系统缓存/杀软状态，
  未验证）。⇒ 结论只有一条能站住：**这一栏只能现读现写，跨轮差值（无论涨跌）一律不作证据**，
  连"某个改动让它变慢了"这种话都不许说。

| 配置 | 注册数 | 全量墙钟（`-j1`、无并发构建、跑前硬门控 = 0） | 同计数历史样本 | 本轮最慢单条 |
|---|---|---|---|---|
| Debug | **749** | **1099.05 s**（批次 C C3k-3 双树全量轮现跑，`build/tmp/c3k3_full.log` 不入库；跑前硬门控 = 0、串行 `-j1`、期间无并发构建） | 749 条**首档**；上一档 748（C3h-2b 两片）**1363.88 s**；再往前从下面 744 那一行起是历史 | 最慢 `real_data_vector_quick` **151.22 s** ⇒ 720 ÷ 151.22 = **4.76×**；`real_data_vector_union_poly` **129.78 s** ⇒ 600 ÷ 129.78 = **4.62×**；`real_data_vector_join_heavy` **109.32 s** ⇒ 590 ÷ 109.32 = **5.40×**；`real_data_vector_overlay_ops` **107.53 s** ⇒ 430 ÷ 107.53 = **4.00×**；`real_data_vector_overlay_buffer_join` **72.83 s** ⇒ 630 ÷ 72.83 = **8.65×**；`real_data_vector_derive_heavy` **53.41 s** ⇒ 350 ÷ 53.41 = **6.55×**；`real_data_vector_checks` **30.78 s** ⇒ 100 ÷ 30.78 = **3.25×** |
| Release | **747** | **515.48 s**（同一轮紧随其后跑，`build/tmp/c3k3_full.log`；**747/747 全绿**） | 747 条**首档**；上一档 746（C3h-2b 两片）**714.97 s** | 最慢 `real_data_vector_join_heavy` **91.21 s** ⇒ 590 ÷ 91.21 = **6.47×**；`quick` **63.84 s** ⇒ 720 ÷ 63.84 = **11.28×**；`overlay_buffer_join` **57.09 s** ⇒ 630 ÷ 57.09 = **11.04×**；`derive_heavy` **46.37 s** ⇒ 350 ÷ 46.37 = **7.55×**；`union_poly` **20.30 s** ⇒ 600 ÷ 20.30 = **29.6×**；`checks` **15.25 s** ⇒ 100 ÷ 15.25 = **6.56×**；`overlay_ops` **14.19 s** ⇒ 430 ÷ 14.19 = **30.3×** |
| Debug | **744** | **1017.64 s**（批次 C C3k-2 拆腿轮现跑，`build/tmp/c3k2_close_dbg.log` 不入库；跑前硬门控 = 0、串行 `-j1`、期间无并发构建） | 744 条**首档**；再往前五档（740~743 各计数）**988.45 / 1033.21 / 1033.42 / 1085.26 / 1081.68 s**；再往前十四档（697~735）**702.01 / 741.52 / 761.78 / 732.32 / 761.30 / 754.82 / 851.06 / 936.24 / 1245.47 / 916.43 / 861.11 / 875.64 / 865.12 / 939.02 / 1083.87 s**（730 那一档另有 982.18 s 的一红轮） | 最慢 `real_data_vector_quick` **216.27 s** ⇒ 720 ÷ 216.27 = **3.33×**；`real_data_vector_overlay_buffer_join` **154.74 s** ⇒ 630 ÷ 154.74 = **4.07×**；`real_data_vector_overlay_ops` **103.44 s** ⇒ 430 ÷ 103.44 = **4.16×**；`real_data_vector_union_poly` **125.38 s** ⇒ 600 ÷ 125.38 = **4.79×**；`real_data_vector_checks` **31.83 s** ⇒ 100 ÷ 31.83 = **3.14×**；`real_data_raster` **9.47 s** ⇒ 60 ÷ 9.47 = **6.34×** |
| Release | **742** | **505.37 s**（同一轮紧随其后跑，`build/tmp/c3k2_close_rel.log`；**742/742 全绿**） | 742 条**首档**；再往前五档（738~741 各计数）**511.75 / 447.55 / 537.97 / 525.53 / 497.99 s**；再往前十三档（695~733）**392.67 / 367.32 / 361.72 / 372.09 / 365.34 / 471.77 / 420.27 / 737.79 / 469.43 / 562.99 / 449.02 / 463.39 / 436.46 / 436.88 s** | 最慢 `real_data_vector_overlay_buffer_join` **122.70 s** ⇒ 630 ÷ 122.70 = **5.13×**；`real_data_vector_overlay_ops` **14.73 s** ⇒ 430 ÷ 14.73 = **29.2×**；其余见历史行 |
⚠ **`overlay` 那一半的"在涨"已经证否——同码双读拿到了**：C3h-1b 与 C3h-2 之间**代码一行没改**（只翻了登记表状态列），
于是这一档天然成了"同码双跑"：Debug overlay 242.12 → **242.40 s**（差 0.28 s，很紧），
Release overlay 110.66 → **128.96 s**（差 **18.30 s ≈ 16.5%**，很松）；同轮 quick Release 116.95 → 115.76、checks 17.12 → 17.66。
⇒ 上一版那句"Debug +11.71 s / Release −43 s，两边一涨一跌所以不能归因于路径 B"现在有了更硬的版本：
**同码都能摆 18 s**，那 "+11.71 s" 从来没资格当证据。按本文件纪律：这一栏只现读、不外推。

**触发线（我自己给自己下的规则）——已处置完毕，现在的口径按"半条腿"算**：2026-10-22 拆腿前
`real_data_vector_full` 的 Debug 余量掉到 **2.06×**（378.52 s，单跑复核 376.63 s）⇒ 触发。
处置 = **拆成 `real_data_vector_checks` 与 `real_data_vector_overlay` 两条注册用例、各自带 `TIMEOUT`**（**没有抬数**），
拆后本轮现读 Debug 余量 **3.08× / 2.73×**、Release **5.27× / 6.58×**（明细见上表）。
⇒ **以后每一半各自守 2.5×**：哪一半再跌破，就再把那一半按动作拆，仍然不许抬 `TIMEOUT`
（抬数字等于把"挂起"的检测窗口一起拉长，而 `TIMEOUT` 是本工具链唯一的执法者，`PROCESS_TIMEOUT` 无效）。
每轮加腿后必须现跑现填这一栏，不许按条数外推。

  两轮分开写清（上一版这里把两轮混成"本轮"，正是本文件刚修过的"表改了 prose 没改"同一形态）：
  - **收尾三轮**（693 / 691 那一档）：两棵树重配置后 `-j1` 全量，
    `100% tests passed, 0 tests failed out of 693`（Debug）与 `out of 691`（Release），429.86 / 231.79 s。
  - **批次 C Task C2 + 复审修复轮**（当时那一档 697 / 695）：+4 条 `real_data_*`，修复轮之后重配重编再全量，
    `100% tests passed, 0 tests failed out of 697`（Debug 702.01 s）与 `out of 695`（Release 367.32 s）。
    同一形式在修复轮之前的另一档是 741.52 / 392.67 s —— 两轮都全绿，差值按上面那条纪律**不作任何结论**。
  - **批次 C C3a 收口轮**（当时那一档）：树上多了 `cf50a39`（四条几何派生改两要素夹具 + 钉坐标断言）与
    两个取证批（**零提交**，只证明判别力），跑前硬门控 = 0、串行 `-j1`，
    `100% tests passed, 0 tests failed out of 697`（Debug **732.32 s**）与 `out of 695`（Release **361.72 s**）。
    注册数没变 ⇒ C3a 只改断言与夹具、没增删用例，这是它该有的形状。
  - **批次 C C3b 收口轮**（当时那一档）：树上多了 `b76af64`/`e0f928c`（八条拓扑检查动作拆列数值断言 + 晋升，
    PENDING 69→61）、`d3a10ca`（更正一条已入库的假陈述）、`a9f5a7f`/`34eb753`（三处"期望值不承重"的夹具加固）与
    `9d98497`（11 条真实数据腿 + 矩阵同步）；三个取证批共 10 处变异**零提交**。
    跑前硬门控 = 0、串行 `-j1`：`100% tests passed, 0 tests failed out of 697`（Debug **761.30 s**）与
    `out of 695`（Release **372.09 s**），两棵树 `Not Run/Skip/***` 计数都是 **0**。
    注册数仍 697/695 ⇒ C3b 与 C3a 一样只改判据、夹具与腿，**没有增删用例**。
  - **批次 C C3d 轮**（上一档，产品修复 N125）：树上多了 `19cb39f`（`overlap_check` 删本地交面积 switch、改调用 `polygonIntersectionArea()` + 新夹具用例 + 省界臂收紧）；
    跑前硬门控 = 0、串行 `-j1`、两棵树先重编（`error C` 计数都是 0，`plugin_vector.dll` mtime 晚于被改 `.inc`）：
    `100% tests passed, 0 tests failed out of 698`（Debug **751.87 s**）与 `out of 696`（Release **373.59 s**），
    两棵树 `Not Run/Skip/***` 计数都是 **0**。注册数 **697 → 698 / 695 → 696**，增量恰为那一条新夹具用例 ⇒ 这是产品修复该有的形状（除它之外一条没动）。
  - **批次 C C3f 轮**（当时那一档，产品修复 N127）：树上多了 `fc68e12`（`isSafeWhereClause` 改 UTF-8 感知 +
    两条 WHERE 用例 + 中文属性值真实腿）；跑前硬门控 = 0、串行 `-j1`、两棵树先重编 `plugin_vector` 与 `gis_tests`
    （`grep -c "error C"` 都是 **0**，`plugin_vector.dll` mtime 晚于被改 `.inc`）：
    `100% tests passed, 0 tests failed out of 700`（Debug **754.82 s**）与 `out of 698`（Release **365.34 s**），
    两棵树 `Not Run/Skip/***` 计数都是 **0**。注册数 **698 → 700 / 696 → 698**，增量恰为那两条新用例 ⇒
    这也是产品修复该有的形状（除它之外一条没动）。收尾还单跑了两棵树各自的那道子集
    `-R "^(PluginTest\.Vector.*Filter.*|PluginTest\.VectorOverlapCheck.*|CoverageMeta\..*)$"`：`-N` 现读 **10 条**（两棵树同数），
    Debug **10/10 1.98 s**、Release **10/10 1.25 s**；`source_mojibake_guard` 单跑 Debug **9.61 s**、Release **9.41 s** 全绿
    （新增的中文用例字面量与 .ps1 注释都没造出乱码，编码由这条用例兜底而不是人眼）。
    ⚠ 这一行里挂的那道子集选择器后来被证明**只数了一半判据**：`CoverageMeta\..*` 匹配不到 `CoverageMetaJudge.*`
    （15 条评审核守全在括号外），本文件 C3h/C3i 那两刀的口径已改成 `(CoverageMeta|CoverageMetaJudge)\..*`。
  - **批次 C C3g 轮**（当时那一档，两条产品修复 N129/N131）：树上多了 `91025ec`（dissolve 字段不存在改硬失败）与
    `08d4a14`（rasterize 前置校验 + 失败不留半成品 + 补 5 个 metadata 键），另有 5 条新单测（合计 1.14 s）。
    跑前硬门控 = 0、串行 `-j1`、期间无并发构建：`100% tests passed, 0 tests failed out of 705`（Debug **851.06 s**）
    与 `out of 703`（Release **471.77 s**），两棵树 `Not Run/Skip/***` 计数都是 **0**。
    注册数 **700 → 705 / 698 → 703**（+5，恰为新增用例数，其余一条没动）。
  - **批次 C C3j 收口轮**（当时那一档，三条同族产品修复 N132/N133/N134）：树上多了 `e222594`
    （`raster_inspect` 支持常数栅格值域、全 NoData 才报"无有效像元"）、`9c02668`（rasterize 的 `layer` 参数改用
    `GDALRasterizeLayers` 真正限制烧写图层，`source_feature_count` 同步只数被烧那层）、`3b8df59`
    （更正 N134 的症状：多图层输入在旧实现里是整条硬失败，"其余图层一起烧"从未成立）。
    跑前硬门控 = 0、串行 `-j1`、期间无并发构建：`100% tests passed, 0 tests failed out of 713`（Debug **936.24 s**）
    与 `out of 711`（Release **420.27 s**），两棵树 `Not Run/Skip/***` 计数都是 **0**。
    注册数 **705 → 713 / 703 → 711**（两棵树各 +8、删除 0）：+8 是按 `-N` 名单与上一档**逐名比对**得出的，
    8 条全是插件单测（4 条 `PluginTest.VectorRasterize*` + 4 条 `PluginTest.RasterInspect*`），
    两棵树差集仍恰为 `gui_matching_ecc_register_debug_failure_offscreen` /
    `gui_matching_stitch_debug_failure_offscreen`（`comm -13` 为空）。
  - **批次 C C3h-1 收口轮**（当时那一档，产品修复 N128 路径 A）：树上多了 `8ed6b75`（union 真并集三块 + 两侧整表属性 +
    4 个 `union_*` 计数键）、`ff86362`（复审回收：分块面积改按类族 `dynamic_cast` 读数、加 MultiPolygon 与 `source` 撞名两条判据、
    源独有块减法基换成"本轮已写面积"）、`72f8ff4`（文档去绝对句 + N135 立案）。
    跑前硬门控 = 0、串行 `-j1`、期间无并发构建：`100% tests passed, 0 tests failed out of 719`（Debug **1245.47 s**）
    与 `out of 717`（Release **737.79 s**），两棵树 `Not Run/Skip/***` 计数都是 **0**。
    注册数 **713 → 719 / 711 → 717**（各 +6、删除 0，按 `-N` 名单逐名比对；6 条全是 `PluginTest.VectorUnion*`）。
    ⚠ **本轮踩到我自己的触发线**：Debug `real_data_vector_full` 余量 **2.06×**（378.52 s；收口后又单跑一次
    **376.63 s** ⇒ 稳定新值，不是噪声）⇒ 按规则**拆腿**（`Task C3k`），不抬 `TIMEOUT`。
  - **批次 C C3k 轮**（当时那一档，测试基建：拆腿）：树上多了 `3554165`
    （`real_data_vector_full` → `checks` 与 `overlay` 两条，各按"消费哪批派生件"造件，新增三份名单不变量 +
    "`Group=checks` 必须与 C3b-A 名单逐名相等"）。⚠ 该刀**实现由子代理完成、它撞每日额度上限中断**，
    工作树里留下未提交改动且无报告 ⇒ 控制器按快照复核、自己重建两树跑三条腿、自补反向破坏取证（改归一组 ⇒ `rc=1` 点名缺口；
    `cp` 快照还原后 md5 一致）后提交，并在 commit message 里声明作者与验收分工。
    实测（控制器现读）：Debug quick 236.47 / checks 34.47 / overlay 255.98 s，Release 126.39 / 20.20 / 130.05 s；
    `TIMEOUT` 100 / 670 ⇒ 2.90× / 2.62×。注册数 719→720 / 717→718。
  - **批次 C C3i 收口轮**（当时那一档，产品修复 N130）：树上多了 `15f3ef1`（buffer/dissolve 的投影闸
    `== "geographic"` → `!= "projected"`，文案统一 `failProjectedRequired`；两条无 SRS 判别腿）。
    判别取证：两闸改回旧写法（标记 `N130-ARM`）⇒ 两条新用例双双 Failed；`cp` 还原后 md5 一致、标记 0 命中，
    两棵树各 **30/30** 绿。全量：Debug `100% tests passed, 0 tests failed out of 722`（**916.43 s**，全绿）；
    Release **719/720**——唯一红是 `runtime_dll_cross_config_audit`（**N136**，见下面挂账），与 C3i 代码路径无关。
    注册数 720→722 / 718→720。
  - **批次 C N122 收口轮**（当时那一档，产品修复 N122）：树上多了 `10fe182`（helper + 8 内联 + 24 处扩展名判定
    全部改 `fs::u8path`；新增中文路径边车用例）。全量：Debug `100% tests passed, 0 tests failed out of 723`
    （**861.11 s**）；Release 首轮 **720/721**（唯一红 = N136）→ 隔离 26 个 Debug 配置残留后重跑 **721/721（562.99 s）**。
    注册数 722→723 / 720→721（+1）。真实腿余量：Debug 2.91× / 2.99× / 3.62×，Release 4.36× / 4.23× / 4.90×。
  - **批次 C C3h-2 收口轮**（当时那一档，N128 后半）：树上多了 `cb443fb`（线×面路径补两侧整表属性、
    `split` 退役收敛为 `input`/`both`、T7 + 就地更新老用例）。实现者撞 150 回合上限但**上限前已提交**，
    控制器自验两树 `VectorUnion*|判据族` 29/29、评审席两轴通过且无必须修。
    全量：Debug **724/724（875.64 s）**、Release **722/722（449.02 s）**，两树 `Not Run/Skip/***` 都是 0；
    注册数 723→724 / 721→722（+1）。真实腿余量 Debug **2.77× / 3.04× / 3.35×**、Release 6.05× / 5.84× / 6.16×。
  - **批次 C C3h-1b 收口轮**（当时那一档，判别取证 + 登记晋升；**控制器自跑，未派下级**）：树上多了 `7a9edf8`
    （`vector,union` PENDING→GOLD + 头部四处 COUNT 同步 + `kPendingCeiling 61→60`）。
    ⚠ 这条 SHA 是 **amend 后**的：原先落的是 `df7f3fc`，subject 从我上一刀（拆腿）复制过来没改，与本刀内容不符；
    该提交只在本地、未 push，经用户同意后 `git commit --amend` 改了标题 ⇒ **内容一字未动**：
    两棵 tree 对象同一个哈希 `git rev-parse df7f3fc^{tree} 7a9edf8^{tree}` = `d1d20284f9669bf9fc791c730caa382da1370ddb`，
    旧 SHA 已不在任何分支上（`git log --all --oneline | grep df7f3fc` 无命中，只剩本地 reflog）。**教训**：`git commit -m` 复用上一条
    message 模板时 subject 必须重写并核对——commit log 也是文档，漂了就是一条假线索。
    判别取证（**唯一一条我自己跑的变异**，因为它是"晋升"这个动作的前提而不是产品缺陷）：
    把 `both` 分块的"减掉本轮已写面积"去掉（`piece(intersection->clone());  // C3H1B-MUT`）⇒ 重建 Debug ⇒
    10 条 `PluginTest.VectorUnion*` 里**恰好只有** `VectorUnionDeduplicatesOverlappingOverlayPolygons` 红、其余 9 条绿；
    `cp` 快照还原后 `cmp` 无输出、md5 `02f2566849647a8bb8cf4fe327158ad1`、标记 0 命中
    （⚠ 扫描范围写死 `grep -rn C3H1B-MUT src tests`：本行刚把这个字面量抄进文档，"全库 0 命中"当场就成假话——
    这是本仓那条"为解释某事写的注释会污染数那件事的 grep"的又一次应验，判据范围要排除 docs）。
    ⇒ union 的分块三条、归属判据、词表、元数据求和、面积读数至此各有独立红态（C3h-1/R1 的 M-A…M-E + C3h-2 的 MU-A…MU-C + 本刀）。
    全量：Debug **724/724（865.12 s）**、Release **722/722（463.39 s）**，两树 `Not Run/Skip/***` 都是 0；
    注册数 **724 / 722 不变**（本刀不增删用例，`-N` 现读 + 名单逐名比对复验：`comm -23` 仍恰为那两条 Debug 轻量闸门、
    `comm -13` 空；名单快照 `build/tmp/c3h1b_names_{debug,release}.txt` 不入库）。
    真实腿余量 Debug **2.76× / 3.06× / 3.43×**、Release 5.20× / 5.66× / 6.22× ⇒ 最低一档 2.76×，仍在 2.5× 之上，**不触发拆腿**。
  - **批次 C C3l 收口轮**（当时那一档，产品修复 N138）：树上多了 `d583c9b`（六个 `.gpkg` 写出路径把索引交给驱动 +
    删 `addSpatialIndexIfNeeded`/`ensureGpkgExtensionsTable`/`escapeSqlLiteral` 三个零引用 helper + 六条新用例）、
    `382ed71`（并集.md 引号统一）、`f0b0d0e`（控制器自改形：行内块注释换成两个具名 builder）。实现者 `c3l-implementer`
    （后台一棒）按任务书未跑双树全量，收口门由控制器跑。
    ⚠ **首轮全量各 1 红**（Debug 729/730 = 982.18 s、Release 727/728 = 479.64 s），唯一红是
    `param_guard_visible_keys_all_read`：新写的行内块注释 `/*spatialIndexFromDriver=*/` 撞上该守卫的注释模型哨兵
    （论域 `src/plugins`，前提写在 `tests/check_action_visible_keys_read.cmake:392-398`）⇒ 控制器改形后重跑双树全量。
    收口现读：Debug **730/730（939.02 s）**、Release **728/728（436.46 s）**，两树 `Not Run/Skip/***` 都是 0；
    注册数 724→730 / 722→728（各 +6、删除 0，逐名比对）；真实腿余量 Debug **2.75× / 2.80× / 3.35×**、
    Release 6.73× / 5.21× / 8.24× ⇒ 最低 2.75×，仍不触发拆腿。
    本轮 `Passed <N> sec` 单行配对仍 100%（Debug 730 行求和 938.40 s vs 墙钟 939.02 s）。
    `find data -newermt "2026-10-23 08:44" -type f` = **0 命中**（本会话含两次双树全量都没写过 `data/`）。
    评审席 `c3l-reviewer`（只读、禁止构建）**两轴均通过、必须修清单为空**，并独立复核了两处：
    六个期望数与六个过滤包框逐条对撞产物 rtree 包框（非自指）、两棵树"无 rtree"名单逐字相同且恰是那 11 条路径
    （补上了实现者缺口 3——Release 侧的逐字名单当时被 `head -8` 截断）。
    **登记给下一批的建议（不在本刀做）**：S1 测试块头注释仍写"六个动作"（与改判冲突）、
    S2 `buffer_clip.inc` 里为旧设计辩护的英文注释已悬空（`useTransactions=false` 自 `f18164a` 起就在那里）、
    S3 **判据③"索引行数 == 要素数"与本条目刚写进台账的空几何规则冲突**（将来含 NULL/空几何的夹具会假红），
    S4 几何列名 `geom` 硬编在 `rtree_<层>_geom` 拼装里、S5 四处夹具复制且共用同名输入文件。
    ⚠ 另记一条**部署面**的观察（不在本刀射程，留阶段二核）：`build/{debug,release}/src/gui/{Debug,Release}/plugins/plugin_vector.dll`
    停在 10-22 15:43 / 16:05、`gis-gui.exe` 停在 10-21 15:16，而 ctest 的 GUI 腿是把 `src/gui/*.cpp` **编进 `gis_tests.exe`**
    （`tests/CMakeLists.txt:15-34`）、插件按 exe 目录向上解析到 `build/<树>/plugins/`（本轮 10:18 新鲜）⇒
    本轮收口的 GUI 腿**没有**跑陈旧副本；但独立构建 `gis-gui.exe` 那份插件副本不会被 `--target gis_tests` 刷新，
    阶段二打包前必须**全目标重建**再 `cmake --install`（与本轮 `--target gis_tests` 不刷新 GUI 侧插件副本同一族问题）。
  - **批次 C C3h-3 收口轮**（当时那一档，补 union 路径 A 的真实数据覆盖）：树上多了 `381500b`（新腿
    `real_data_vector_union_poly` + 分组 `union_poly` + 第 6 条名单不变量）、`35f578e`（评审席两条必须修 + 我自己造的静默空跑）。
    实现者 `c3h3-implementer` **撞 150 回合上限**（最后一拍"把第三跑现读并入 TIMEOUT"只落到注释、没落到属性），
    未提交且未写反向对照那一节 ⇒ 控制器按快照接手：复核 diff、把 `TIMEOUT` 从 580 改成注释算出的 **600**
    （同步 `tests/CMakeLists.txt` 复算行与 `real_data_matrix.txt` 的旧值）、补做反向臂、再提交（commit message 里声明分工）。
    ⚠ **接手时我自己造了一次更严重的静默假绿**：修评审席 M2 时把判据分发从"按用例名"改成"按 `$Case.Group`"，
    而 `Invoke-Case` 的结果对象**没有 Group 字段** ⇒ 那条 `if` 永远为假、命名分支又已被我删掉 ⇒
    整条腿 128.14 s 跑完、ctest **Passed**、四档名单全绿、**实际零条判据执行**。
    抓到它靠的是评审席 prescribed 的反向臂（往名单塞一条假用例看会不会红）。修法：结果对象带上 `Group`；
    新增**第 7 条不变量**「跑过的 union_poly 用例数 == `Assert-UnionPolyCase` 被调用次数」；
    双向复验＝注入 `union_poly_dummy` 必红（消息点名 `stdout count: pattern 'Union completed: …' matched nothing`）、
    撤销注入必绿（而"绿"本身就证明 `executed == asserted == 1`）。
    ⚠ 同批更正：注释与台账里"源层含面 ⇒ 必然落路径 A"不成立（Unknown 层的有效类型取**第一个非空要素**，
    省层现读前四要素 `MP/MP/MLine/MP` ⇒ 今天落 A 是数据顺序）⇒ 改为断言 `FirstNonEmptyGeometryType` 是面；
    另修 `Test-GeoJsonGeometryNonEmpty` 遇字符串叶子无限递归（PS `foreach` 过 `[string]` 给整串，`CallDepthOverflow`
    已复现）并删掉"全组轮更慢是锁排队"这条被我自己 143.22 s 读数否证的因果。
    全量现读：Debug **731/731（1083.87 s）**、Release **729/729（436.88 s）**，两树 `Not Run/Skip/***` 都是 0，
    `Passed <N> sec` 单行配对 731/731 与 729/729（求和 1083.31 / 436.24 s vs 墙钟差 0.56 / 0.64 s）；
    注册数 730→731 / 728→729（+1/+1，逐名比对，差集仍恰为两条 Debug 轻量闸门）。
    真实腿余量 Debug **2.72×（quick）/ 2.90×（overlay）/ 3.12×（checks）/ 4.35×（新腿）**、
    Release 6.67× / 7.72× / 5.65× / 26.8× ⇒ 最低 2.72× 仍在 2.5× 之上，**不触发拆腿**。
    `find data -newermt "2026-10-23 11:20:54" -type f` = **0 命中**。
  - **批次 C C3m 收口轮**（本表现值，落地用户裁定"那 11 条也要带索引"）：树上多了 `a7fb866`（vector 里 **17 个显式发图层选项的调用点**
    统一走 `buildGpkgDriverIndexedLayerOptions()`、图层选项 `SPATIAL_INDEX=NO` 整体删除、C3l 那两个具名 builder 并成这一个）、
    `fb40ca4`（判据 ③ 的比较对象从"要素数"改成"非空几何数" + 三条新审计用例 + 反回潮守卫
    `PluginTest.VectorSourceNeverDisablesGpkgSpatialIndex`）、`f339f45`（六条真实腿两棵树两跑现读入测量表）。
    实现者 `c3m-implementer` 按任务书未跑双树全量，收口门由控制器跑。
    全量现读：Debug **735/735（1074.54 s）**、Release **733/733（452.24 s）**，两树 `Not Run/Skip/***` 都是 0；
    `Passed <N> sec` 单行配对 **735/735** 与 **733/733**（逐条求和 1073.88 / 451.62 s，与整轮墙钟各差 0.66 / 0.62 s，
    纯 ctest 调度开销）；注册数 731→735 / 729→733（各 +4、删除 0，逐名比对：与 C3h-3 快照的差集恰是那四个新用例名、
    `comm -23` 仍恰为两条 Debug 轻量闸门、`comm -13` 空；快照 `build/tmp/c3m_names_{debug,release}.txt` 不入库）。
    真实腿余量**两个口径都得记**（本刀改了产物形态 ⇒ 任务书点名必须重测六条腿）：
    ① 控制器**全量轮内**读数——Debug overlay **236.75 s ⇒ 2.83×**、quick 214.78 ⇒ 3.35×、checks 33.55 ⇒ 2.98×、
      union_poly 145.45 ⇒ 4.12×、raster 13.07 ⇒ 4.59×；Release 97.46 ⇒ 6.87×、92.87 ⇒ 7.75×、17.60 ⇒ 5.68×、
      21.97 ⇒ 27.3×、7.62 ⇒ 7.87×；② 实现者**六条腿单独串行组**读数（`tests/CMakeLists.txt:1691-1711`）——
      最紧 overlay **2.66×** 与 checks **2.82×**。⇒ 两口径的最低值 2.66× 仍在 2.5× 触发线之上，
      **没拆腿、六个 TIMEOUT 一个字没抬**。
    ⚠ 补索引的成本在本轮再次实测为可忽略，且**不许读成趋势**：quick 从改前 236.47 读到改后 215.32、
    overlay 的 Release 两跑 103.72 / 127.42 差 22.8% —— 同一条腿本机散布就是这个量级（那条注释已把此口径写死）。
    产物形态收口证明（口径 = `build/tmp/c3mprobe/scan_gpkg_index.py`，纯标准库 sqlite3、`mode=ro` 只读打开；
    控制器收口时复跑、与实现者读数逐字同值）：Debug 树 112 个 `.gpkg` / 114 个要素图层 ⇒
    `index ok 112` / `MISMATCH 1` / **`NO-RTREE 1`**（改前 13）；Release 树 107 个 / 109 个图层 ⇒
    `index ok 109` / `MISMATCH 0` / **`NO-RTREE 0`**（改前 12）。
    Debug 残留两条都**不是活产物**、mtime 现读均早于本刀开批（14:25:17）：`a0_probe_diff_point_out.gpkg`
    （10-14 17:34，`grep -rn a0_probe_diff_point_out tests/ src/` ⇒ 0 命中 ⇒ N138 取证期的孤儿探针件）与
    `e2e_filter_where_reject_output.gpkg`（10-22 07:54，生产它的用例按设计**不该写出产物**——WHERE 被拒 ⇒ 动作失败 ⇒
    那条腿不会重写这个文件；文件里连 `gpkg_extensions` 都没有 ⇒ 是"根本没登记"的自洽空件，不是 N138 那种"登记了但 0 行"）。
    按 mtime 只数开批之后新写出的产物：Debug 104 个 / Release 104 个 ⇒ **缺索引的活产物归零**
    （⚠ 这条"归零"靠的是上面那把全目录扫描 + mtime 过滤两条一起成立，单看 mtime 数不出"缺不缺"）。
    反回潮守卫的**必须先能红**（R-B16 同变异旧绿新红，**这一段是实现者的现读**；控制器当时没复跑该变异，
    只跑了自己那两条独立证据：`grep -rn 'SPATIAL_INDEX' src/` = 0 与双树全量 735/733、733/733 全绿；
    ⚠ 旧版这里写的"复跑要重编 `plugin_vector.dll`"**对守卫本身不成立**——守卫是运行期读源码的，
    下面"评审席 M 项补硬"那一节控制器就是**不重编**直接把两臂打红的）：
    实现者把 `vector_plugin_boundary.inc` **一个**调用点改回带 `SPATIAL_INDEX=NO`，
    `ctest -R "GpkgSpatialIndexIsPopulated|VectorSourceNeverDisables…|C3mProbe…"` 选中 11 条 ⇒ **3 条红**：
    守卫红并点名 `src/plugins\vector\vector_plugin_boundary.inc:59`、`VectorBoundaryGpkgSpatialIndexIsPopulated` 红、
    临时演示用例红；产物侧对撞是 `no such table: "rtree_shapes_geom"`
    ⇒ 那条选项一旦回到任何调用点，审计的 ③ 与 ④ 一起红。
    还原一律 cp（未用 `git checkout`/`reset`/`stash`），实现者附 `cmp` + 演示前后 `md5sum` 同值证明，
    其复验现读：`grep -rn 'C3M-REDDEMO' src/ tests/` = 0 命中（⚠ 论域**不含 docs**——本台账下面这句
    "复验现读"本身就带着这个串，把它算进去会得到 1 个假命中；这是"为解释某事写的文字污染数那件事的 grep"的第 N 次应验）。
    控制器同批复跑 `grep -rn 'SPATIAL_INDEX' src/` = **0 命中**。
    ⚠ 守卫论域是 `src/plugins/**/*.inc|*.cpp`（42 个文件，含注释与字符串字面量一起数、不做排除注释的行过滤），
    `tests/`（守卫自己的 token 名单）与 `docs/算法说明/矢量数据处理/并集.md:68`（N138 的历史说明）里出现该 token
    **不算回潮** ⇒ 口径只能写"`src/plugins` 0 命中"，写"全库 0 命中"是假说法（本台账 574 行旧版就写错过一次）。
    ⚠ **评审席 M 项把这条判成有洞，已按现读补硬**（`fb40ca4` 之后由控制器直接改，证据链如下）：
    旧守卫只数大写形 `SPATIAL_INDEX`，而 GDAL 取图层选项名走 `CSLFetchNameValue`（**大小写不敏感**，
    现读 `ogrgeopackagedatasource.cpp:6722` 的 `bCreateSpatialIndex = (pszSI == nullptr || CPLTestBool(pszSI))`）
    ⇒ 写回 `spatial_index=no` 一字之差就能既关掉索引又躲过守卫；另外那条"写完再补建索引"的老路
    （GDAL 的 SQL 函数 `gpkgAddSpatialIndex`，C3l 删掉的就是它）**根本不带图层选项** ⇒ 单靠选项 token 看不见。
    现在守卫在 **ASCII 大小写折叠后的副本**上数**两个** token：`spatial_index` 与 `gpkgaddspatialindex`，
    命中时点名"文件:行号 + 命中的是哪个 token"。两臂红态由控制器现跑（**不用重编**：守卫是运行期读源码，
    台账旧版那句"复跑要重编 `plugin_vector.dll`"对守卫本身不成立，只对产物侧那 9 条成立）：
    往 `vector_plugin_boundary.inc` 追加一行含小写 `spatial_index=no` 的注释 ⇒ 大写 needle 现读数到 **0**、
    折叠后读到 **1**、守卫 **Failed** 并点名 `…vector_plugin_boundary.inc:129 （命中 token: spatial_index）`；
    换成 `gpkgAddSpatialIndex` 一行 ⇒ 同样 **Failed** 且点名 `（命中 token: gpkgaddspatialindex）`；
    两臂都 `cp` 还原 + `md5sum` 前后同值（`ffe7f0d4…`），还原后两棵树 10/10 绿（9 条审计 + 1 条守卫，
    Debug 5.84 s / Release 4.06 s）。守卫仍拦不住的：自己拼 SQL 建 rtree 而不提这两个名字 ⇒ 产物侧还得靠 ③④ 两条。
    判据 ③ 的"旧写法会假红"演示（**同属实现者现读**，临时用例、跑完即删）：同一函数里旧臂（rtree 行数 == 要素数）
    在既有夹具 `e2e_singlepart_input`（3 要素含 1 个 NULL）上报 `2 != 3`、新臂（== 非空几何数，2 == 2）不报
    ⇒ 读的是既有产物、没新造缺陷。
    同轮顺带把 C3l 评审席那条 **S3 销掉**（③ 与本台账空几何规则的冲突已按新口径改正）。
    ⚠ **但本台账先前判"S1 不再成立"是错的，现按评审席更正**：`tests/test_plugins.cpp:6823` 那句
    说的是"**六个动作**写出的 `.gpkg` 带一个 0 行 rtree"（缺陷射程，不是层名），而 N138 的射程早已改判为
    **五个动作**（buffer 自 `f18164a` 起就没关过索引）⇒ 那句头注释确实是陈的，且 `fb40ca4` 改过同一个注释块
    （加了 ③ 的六行）却没跟上——正是"改注释的人不动相邻的旧断言"这一族。本轮已把块头改成"五个动作 + buffer 是对照"、
    层名那行改成"六个动作各写各的层名，共五类"（`tests/test_plugins.cpp:6823-6827`/`:6835`，评审席 S1 就此销账）。
    ⚠ **新登记一条射程缺口（本刀不自行扩范围）**——并按评审席 M1 把论域算对：
    GPKG 写出点的真实论域**不是 17**。控制器现读 `CreateLayer(` 在 `src/` 共 **25** 处 =
    vector 插件 24 处（**17 处走 builder** + **7 处从来就发 `nullptr`**：geom_metrics/nearest/
    raster_convert 的 polygonize 与 convert/repair/simplify/spatial_join）+ `classification_plugin.cpp:1012` 1 处（`nullptr`）；
    另有 projection 插件走 `GDALVectorTranslate`（`projection_plugin.cpp:284`），**根本不经过 `CreateLayer`**、
    不在上面这套计数里。
    产品侧**没有**缺口：GDAL 的判据是"选项缺省即建索引"（`bCreateSpatialIndex = (pszSI == nullptr || CPLTestBool(pszSI))`），
    而上面那把全目录扫描的口径覆盖所有这些路径的产物 ⇒ "缺索引的活产物"归零；我另直读了
    `e2e_simplify_output.gpkg`/`e2e_repair_output.gpkg`/`projection_vector_output.gpkg` 三个非 builder 产物，rtree 表都在且有行。
    ⇒ 被更正的是**覆盖算术**：具名审计用例只覆盖 **9 个动作**，
    在 25 个 `CreateLayer` 点里其余 **16 处**今天只由"token 0 命中"这条**负向**守卫覆盖
    （守卫论域 `src/plugins` ⇒ classification 那 1 处在论域内），而 projection 那条 translate 路径
    **既无具名用例、也在守卫论域之外** ⇒ 今天对它没有任何索引判据。
    产物扫描脚本在 `build/tmp/` 不入库、不是常驻判据。留作后续审计批（与 S4/S5 同批处理）。
    ⚠ 实现者如实登记的两条缺口（不是我的裁定，是它自己写进报告的）：
    ③ 那条"旧写法假红"演示是**临时用例**、已随还原删除 ⇒ 仓库里现在**没有常驻用例**守"含 NULL/空几何图层的行数判据"
    （三条新用例的夹具都不含 NULL ⇒ 把 ③ 改回旧写法不会被任何常驻用例抓到，只有产物扫描看得出）；
    以及**文档改动 0 个文件**——11 篇动作文档现读都没写过索引相关的句子（`外包矩形.md:61` 那句讲的是下游拿外包矩形做预筛选，
    不是对产物索引形态的断言），按任务书"没写就不必各加一段"执行。
    ⚠ 一处**范围偏离留评审席判**：那 11 个点上的 `const bool useTransactions = outFormat == "GPKG";` 被删掉了。
    删的依据是现读：这 11 个文件里 `StartTransaction`/`CommitTransaction` **0 处**（事务只在 `buffer_clip`/`dissolve`/`overlay`
    三个文件、也就是原本那 6 个 DriverIndexed 点所在文件里）⇒ 那行变量**只喂给图层选项、根本不控制事务**
    （"FastWrite"这个名字把两件事混为一谈），留着就是触发 C4189、又会让下一个读代码的人以为这里批量提交过的死变量。
    任务书 §2.1 的字面口径是"事务批量化一字不许动"，事务本体确实一字未动——控制器收口现读：那 11 个文件的
    `Start/Commit/RollbackTransaction` 全为 **0 处**，事务只活在 `buffer_clip`（11 处）/`dissolve`（4 处）/`overlay`（28 处）
    这三个原本就带索引选项的文件里，且这三处的 `useTransactions` 一个没删（12 / 5 / 25 处）；
    差异在此如实登记。
    `find data -newermt "2026-10-23 14:25:17" -type f` = **0 命中**（含本会话两次双树全量）。
  - **批次 C C3m 评审席修复轮**（本表现值，同计数 735 / 733，**不增删用例**）：评审席 `c3m-reviewer` 判
    **标准轴通过 / 规格轴不通过、必须修 2 条**，两条我逐条独立复验后都成立（M1 论域算术、M2 测试块头注释的
    "六个动作"是陈值而我先前判"S1 不再成立"是误判），另按 S-E 把反回潮守卫补硬（大小写折叠 + 两个 token）。
    改动 = `src/plugins/vector/vector_plugin_common.inc`（块头注释）+ `tests/test_plugins.cpp`（块头注释 + 守卫）
    + 本台账；两棵树 `gis_tests`+`plugin_vector` 重建 **0 warning**，复跑点名族 Debug **10/10（5.84 s）**、
    Release **10/10（4.06 s）**，判据族 `(CoverageMeta|CoverageMetaJudge)\..*` Release **19/19**。
    ⚠ 证据链的时序要说清：上面那轮双树全量（1236.86 / 478.89 s）跑的是 17:43 那批二进制；
    之后我又改了一次**注释**（守卫的"拦不住"那两行措辞），18:41/18:42 重编两棵树的 `gis_tests`（仍 0 warning）
    并复跑点名族 Debug **10/10（5.75 s）**、Release **10/10（2.56 s）** ⇒ 全量那一档的"绿"对应的二进制与最终提交
    之间只差注释，**我没有为重编后的二进制再跑一次全量**，这一档留给下一刀的收口门顺带核。
    ⚠ **本轮双树全量的墙钟比上一档高约 15%，但那是机器状态、不是这一刀的改动**：
    Debug **735/735 = 1236.86 s**（上一档同计数 1074.54 s）、Release **733/733 = 478.89 s**（452.24 s），
    普涨出现在**互不相关的用例上**（`source_mojibake_guard` 15.26→17.00、`install_closure` 11.32→14.83、
    GUI 一堆 3.8→4.5），`Passed` 单行配对仍 735/735 与 733/733（求和 1235.93 / 478.21 s）。
    ⇒ 全量轮里 overlay 那条读到 **281.66 s ⇒ 670 ÷ 281.66 = 2.38×**，**看着像跌破 2.5× 触发线**；
    我在安静态把六条腿逐条单跑复测：overlay **238.95 / 236.35** ⇒ 2.80×、quick **223.95** ⇒ 3.22×、
    union_poly **151.59** ⇒ 3.96×、checks **31.60** ⇒ 3.16×、raster **10.41** ⇒ 5.76×、matching **1.70** ⇒ 35.3×
    ⇒ 全部回到历史区间 ⇒ **判定是机器状态污染了那一轮全量，不拆腿、不抬 TIMEOUT**。
    成因**没证明**（候选：新构建的 `gis_tests.exe`/`plugin_vector.dll` 被杀软实时扫描；本轮没有并发子代理或构建，
    跑前硬门控 = 0）⇒ 按"同一条腿本机散布就是这个量级"处理，**只登记读数与结论，不登记因果**。
    ⚠ 由此加一条操作口径：**余量必须按安静态逐条单跑的读数判**，全量轮内的读数只能当对照——
    否则一次机器状态异常就会把我推去拆一条本来健康的腿。
  - **批次 C C3n 输出契约系列**（五段：`b7ade30` → `c9658b8` → `e4cfd0d` → `5f78c4e` → `51000fd`）：
    用户 2026-10-23 裁定"先写同目录临时件 → 全部成功 → 替换目标"，本系列把 vector 的 **24 个矢量写出点**
    全部迁到同一个 helper。机制：① `begin()` 逐输入做 `fs::equivalent` 硬闸（**`output == input` 今天不是截断，
    是 `fs::remove` 把输入文件本身删掉**）→ 生成 `<stem>.gis-tmp-<进程号>-<进程内 atomic 自增><扩展名>`
    （同目录同卷，禁止放 `build/tmp/`）→ 清同前缀孤儿件 → `drv->Create(临时名)`；
    ② `commit()` **先 `dataset_.reset()` 真正关闭** → 逐文件 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)`
    （**显式不带 `COPY_ALLOWED`**——MSVC STL 的 `std::filesystem::rename` 带这个标志，跨卷会悄悄 copy+delete，
    本机现读 `crt/src/stl/filesystem.cpp:722`）→ 清旧边车；③ 失败/异常展开由析构 `discardStagedFiles()` 兜清理。
    三点带事务（buffer/clip/dissolve）与 overlay 三点的事务**全部在 `commit()` 之前收口**（实现者现读：
    union 511<519、difference 728<735、intersect 912<919）。
    落地读数：`drv->Create(output.c_str()` 由 24 处降到 **1 处**（只剩论域外的 `doRasterize`），helper 1 处；
    vector 侧的 `removeExistingVectorOutput` 已删；J5 名单 **24 已迁 / 0 待迁**，并新增
    `EXPECT_EQ(stagedDeclarations.size(), 24u)` 防真空跑闸（逐条断言"每个已迁点恰好声明一次、该函数体内直写
    output 的 Create 数为 0"）。
    判据 10 条具名：J1 `Vector{Boundary,ConvexHull}MidFailureKeepsOldProductAndLeavesNoTempFile`、
    J2 `VectorStagedOutputKeepsProductForm`（11 动作 / 13 臂，期望值按夹具手算）、J3 `…LeavesNoJournalSidecar`（11 动作）、
    J4 `VectorOutputMustNotOverwriteInput`（24 动作 × 2 格式 × 2 拼法 = 94 臂）+ 双输入
    `VectorDoubleInputOutputMustNotOverwriteSecondInput`、J5 `…HalfMigratedRosterIsLoadBearing`、
    N139 三条 `Vector{Clip,Difference,Intersect}RejectedFeatureWriteDoesNotCorruptHeap`（先红后绿）。
    红态证据：J1 绕开临时件 ⇒ 红；停掉临时件清理 ⇒ 红并点名残留 `boundary_out.gis-tmp-25792-2.gpkg`；
    J3 的不先关闭 ⇒ 跑出 `Win32 error 32`（ERROR_SHARING_VIOLATION）；J5 摘名单一条 ⇒ 红"实际 3、名单 2"；
    J4 把 simplify 的保护清单置空 ⇒ 红（点名"输出不能覆盖输入"缺失）。
    登记缺口：J2/J3 只覆盖 11 个动作，其余 13 个由各自执行用例间接承重；J4 的 CLI 动作名与 J5 的（文件,函数）
    是两张手抄名单、无交叉校验；N135 的三条 GEOS 失败分支按用户裁定**永久无判据**。
  - **批次 C C3k-2 拆腿**（`9b81d2b`）：安静态复测 overlay 由 238.95/236.35 s 涨到 **273.20/265.40 s**
    ⇒ 670 ÷ 273.20 = **2.45×**，跌破 2.5× 触发线（**成因未证，不编因果**）⇒ 按"不许抬 TIMEOUT、拆腿"，
    依逐用例读数（`buffer_quick` 64.54、`spatial_join_focus` 55.66 是两大头）拆成
    `real_data_vector_overlay_buffer_join`（15 条，Debug 155.30/152.78 ⇒ TIMEOUT **630** = 4.06×）与
    `real_data_vector_overlay_ops`（11 条，Debug 105.25/103.55 ⇒ TIMEOUT **430** = 4.09×），
    两条新腿按 ≥4× 定值；26 条用例一个没丢（15 + 11），"四组之并 == 全量 / 无重名 / 无未归组"三条名单不变量
    现读通过。注册数 743 / 741 → **744 / 742**。
  - **批次 C C3c-A**（`d814e1d`，**本条是补记** —— 那一刀提交当时漏了这一节）：`info` / `filter` / `convert` /
    `repair` 四个执行用例把"成功 + 产物存在"级别的断言换成手算写死的整串/数值断言（info 的 extent 四边、
    filter 的 900×400 外环逐点与面积 + 中文否定臂、convert 的格式/层名/字段/逐点坐标、repair 修复后
    MultiPolygon 双三角形面积 1250 与顶点集合）。判别取证（A 半边）：四处实现**同时**改坏 ⇒ 四条用例全红并各自
    点名数值差异，恢复后 4/4 绿。真实腿（B 半边）：`overlay_buffer_join` 15 → 18 条（新增 `info_province` /
    `info_roads` / `repair_province`，输入先复制到 `build/tmp/c3cA/` 以避 N115 的 `.aux.xml` 串证），
    `convert_roads_geojson` 增强为输入/输出两侧对撞；该腿 Debug 三个现读 **188.07 / 199.20 / 192.16 s** ⇒
    630 ÷ 188.07 = **3.35×**（按最慢那一跑 199.20 算是 3.16×），`real_data_vector_quick` 因为三条新用例也注册在
    quick 里降到 **256.03 s** ⇒ 720 ÷ 256.03 = **2.81×**（两处都仍 >2.5× 触发线，登记为"再涨就拆"）。
    登记四行 PENDING→GOLD、`kPendingCeiling` **60 → 56**。收口门双树全量 Debug **744/744（1195.05 s）**、
    Release **742/742（660.45 s）**，`find data -newermt <开批时刻>` = 0。
    那一刀自己登记的缺口（我认可、并延续到下面）：B 半边没做变异红臂（工单只要求 A 半边）；
    `repair_province` 的省层 42 条全有效 ⇒ 真实腿判的是恒等关系，坏几何真值只由单测承重；
    `info` 的 D1 旧口径（`--output` 占位、`doInfo` 不落盘）仍在；`convert_roads_geojson` 是"增强"不是"新增"。
  - **批次 C C3c-B 第一片（simplify）**（控制器自跑；起因是 C3c-B 实现者在 37 次工具后撞**当日对话额度**，
    我按"丢实现还是丢证据"复验过树：HEAD 未动、工作树干净、登记表与上限原样 ⇒ 只是没干活，不是丢工作）：
    - **单元侧**：`PluginTest.VectorSimplifyExecution` 原来只有一条 `EXPECT_LT(outLine->getNumPoints(), 5)`
      —— 那是"少一个点就通过"的弱判据 ⇒ 换成五点夹具（(0,0)(10,0.5)(20,-0.5)(30,0.4)(40,0)，EPSG:3857，
      三个中间点到弦的距离就是各自 |y| ≤ 0.5）在 `tolerance = 2.0` 下的**手算整串**期望：两点线
      (0,0)/(40,0)、长度 40、层名 `roads`、CRS 3857、属性 `main_road`；再加第二条容差臂 `tolerance = 0.1`
      （小于全部中间点偏移）⇒ **原样五点线整串**。长度读数走 `toLineString()` 的判类下转，不拿基类
      `get_Length()`（它不挂在 `OGRGeometry` 上，同 :4553 那条理由）。
    - **变异取证（R-B16，旧判据 / 新判据）**：MUT-S1 `tolerance` 不生效恒用 10.0 ⇒ 旧**绿** / 新红
      （`kept.size()` 2 vs 5）；MUT-S2 `CreateLayer` 传 `nullptr` SRS ⇒ 旧**绿** / 新红（`outSrs` NULL）；
      MUT-S3 层名改写 `simplified` ⇒ 旧**绿** / 新红（`"simplified"` vs `"roads"`）；MUT-S4 完全不简化
      （`clone()` 顶掉 `SimplifyPreserveTopology`）⇒ 旧**也红**（`getNumPoints() < 5`，5 vs 5）/ 新红 ——
      诚实登记：这一条旧判据本来就抓得住，它红在新判据把"该剩哪两个点"也钉死了。旧判据那一臂是用
      `git show HEAD:tests/test_plugins.cpp` 另编一个 `gis_tests` 可执行文件**真的跑**出来的，不是推理。
      每次只重建 `plugin_vector`，恢复一律 `cp` + `cmp`，红→绿闭环复跑通过。
    - **真实腿**：新增 ctest 用例 `real_data_vector_derive_heavy`（`-DMODE=full -DGROUP=derive_heavy`、
      TIMEOUT **350**、`RESOURCE_LOCK real_data_regression`）里的一条 `simplify_roads`（roads core 3857
      的 `build/tmp/c3cB/` 复制件，22 836 条线，`tolerance = 2.0` m）。判据 = **三方对撞**（CLI 报的完成数、
      metadata `feature_count`、产物回读要素数都 == **输入**现读的要素数）+ 层名 / 几何类族 / CRS / 字段名与顺序
      四项保真 + **包框被输入包住**（Douglas-Peucker 只删顶点 ⇒ `Assert-ExtentInsideOf` 容差 0）+
      **产物晚于本腿开跑时刻**（`Refs.StartUtc`，防"上一轮同名件在场"顶替本轮产物）。期望全部由输入侧现读导出，
      不写样本标定常量。MUT-S5（层声明降级成 `wkbLineString`）**单元侧绿、真实腿红**
      （`output Geometry='LineString', input 'MultiLineString'`）⇒ 这条腿有自己独有的牙。
    - **名单机器扩到五档**：`$rosterGroupNames` 加 `derive_heavy`、`GIS_CASE_ROSTER[derive_heavy]` 打印行、
      第 3 条的式子与消息一起改；新增第 8 条（组标 == `$script:C3cBCaseNames`，与第 5/6 条同形）、
      "名单 vs 注册"正向对撞、"跑过数 == 判过数"不变量。三条反向臂**实测**：改档 ⇒ 红在
      `C3c-B group invariant: Group=derive_heavy disagrees with the C3c-B roster`；分发标签拼错 ⇒ 红在
      `1 case(s) ran but 0 were judged by Assert-C3cB*`；MUT-S5 ⇒ 红在 Geometry 判据。造件按档闸
      `$buildDeriveHeavyInputs`（只在 full +（all|derive_heavy）为真），其余四档不付这份 7.5 MB 复制与一次
      22 836 要素的 info 现读。
    - **为什么单独成档而不是挂进 `overlay_buffer_join`**：`simplify_roads` 用例内计时 **54.22 s**，先挂进去
      那一跑**实测整腿 250.11 s** ⇒ 630 ÷ 250.11 = **2.52×**，贴着 2.5× 的拆腿触发线；规矩是"不许抬 TIMEOUT、
      跌破就拆"⇒ 挪出来单独成条，新档 Debug 三跑 **85.84 / 86.69 / 84.47 s** ⇒ TIMEOUT **350** = **4.04×**。
    - **一条实测事实（不是判据）**：同一份 roads 复制件在 `tolerance = 2.0` 下顶点 **215 154 → 129 287**
      （各用 `vertices_extract` 数一遍，取动作自报数），产物 7 565 312 → 6 008 832 字节。记下它是为了说明
      真实腿那几条结构判据今天在真实数据上不是空转；真实腿**不**判顶点数 —— runner 这一层没有顶点计数读数，
      而为它加两次 `vertices_extract` 实测要 441.7 s + 271.5 s。
    - **登记**：`vector,simplify` PENDING→GOLD（证据用例 `PluginTest.VectorSimplifyExecution`），
      `kPendingCeiling` **56 → 55**；登记表头部 COUNT 按声明行自带的现读命令逐条重跑
      （data_rows 126 / gold_rows 71 / pending_rows 55 / gold_rows_all_lines 72 / table_driven 15 / literal_slot 56）。
      覆盖族 `(CoverageMeta|CoverageMetaJudge)\.` **19/19** 绿、`PluginTest.Vector*` + `source_mojibake_guard`
      **92/92** 绿（Debug 树）。注册数 744 / 742 → **745 / 743**。
    - 本片**未验收**的四条：(1) 这条新腿的 Release 侧没有现读（release 树要收口轮全目标重建才配得上；
      按 union_poly 那栏的量级差 Release 更快，350 对它只会更宽）；(2) 745 / 743 的双树全量还没跑（收口门归我）；
      (3) C3c-B 还剩 `buffer` / `dissolve` 两片，`kPendingCeiling` 的目标 53 因此还差 2
      （**这两片已在下面两条里结案** ⇒ 上限现值就是 53，本行只留作当片的现场记录）；
      (4) `simplify` 的 GEOS 失败分支按用户裁定**无判据**（N135 家族，产品二进制里不放注入缝）。
    - 我自己的一次过程错误，记下来防复发：第一条反向臂（A1）我当时用 `sed -i` 改档，它把这个 **CRLF** 文件
      整文件归一成 LF ⇒ powershell 直接 `ParserError` ⇒ 那一跑的"红"是**假红**、不作证据；改用 Edit 重做才拿到
      预期的红。本仓早已登记"`sed -i` 归一行尾而 `git status` 看不见"，这是它第一次真的废掉一条臂。
  - **批次 C C3c-B 第二片（buffer）**（控制器自跑，接上一条同一节奏）：
    - **单元侧新开一条** `PluginTest.VectorBufferRoundCapStadiumMatchesHandComputed`，不动
      `VectorBufferExecutionReportsMetadata`（那条夹具是 200 条三点斜线，是给"两万级要素 + 别触发
      gpkg_extensions 报错"用的，形状不可手算）。新夹具是一条 (0,0)-(100,0) 的两点线 ⇒ 产物是"运动场"形，
      四条判据全部手推、不照抄产物：**面积带** [2dL + (n/2)d²sin(2π/n)|n=32, 2dL + πd²]
      （GEOS 圆头是**内接**折线 ⇒ 上界是理想圆；下界取 n=32 而不是实际 n=120，是为了不把判据钉在
      GDAL 的缺省象限段上 —— 本仓 vcpkg 现读 `ogr_geometry.h:565 Buffer(double dfDist, int nQuadSegs = 30)`）；
      **包框四边** (-d,-d)-(L+d,d) 容差 0.1（极值落在相邻两段之间时误差上界 d·(1−cos(2.8125°)) = 0.060 @ d=50
      ⇒ 0.1 有约 1.7× 余量；写 0 会被圆头相位差判红，那不是缺陷）；**层名** = 源层名 + `_buffer`；
      **CRS / 属性 / 要素数** 保真。**两条容差臂**（25 与 50）都跑。
    - **手推模型被变异轮独立验证**：MUT-B1（恒用 100）实测面积 **51401.573745766305**，模型预测
      2·100·100 + 60·100²·sin(3°) = **51401.58**；MUT-B2（象限段=1）实测 **6250 / 15000**，模型预测
      2dL + 2d² = **6250 / 15000** ⇒ 带的形状与位置不是拿产物倒推出来的。
    - **判别取证（R-B16）**：四条变异都是"旧判据绿、新判据红"，而且"旧"不是改造出来的旧版本 ——
      是**现有的 `PluginTest.VectorBuffer*` 用例**在变异下照过：B1 那一轮跑了四条 4/4 通过，
      B2 与 B3 各跑了其中两条 2/2 通过（**没有**给 B4 重跑单元侧旧臂，见下一句）；
      B4 的"旧臂仍绿"证据来自真实腿自己那一跑 —— `buffer_quick` 里前面两条旧判据（自报 `feature_count`
      与 `srs_type`）先通过，才红在我新加的层名判据上。
      B1 `distance` 不生效恒 100 ⇒ 新红（面积超上界 + 四边各差 75/50）；
      B2 象限段写死 1 ⇒ 新红（面积低于下界 6250 vs 6950.90）；
      B3 缓冲后 `swapXY()` ⇒ 新红只在**两条 max 边**（各差 100），面积与两条 min 边**不动**
      —— 这正是本仓那条老规矩的现场版：面积/包框面积是 x↔y 反射不变量，抓不到写反，**必须逐边比**；
      B4 层名不再加 `_buffer` ⇒ 新红（层名整串）。恢复一律 `cp` + `cmp`，恢复后 53 条 `PluginTest.Vector*` 全绿。
    - **真实腿 `buffer_quick` 补判据**：它过去只比对 CLI 自报的 `feature_count` 与 `srs_type` 两个数，
      一个都没落地读产物 ⇒ 层名/几何类族/CRS/字段整批丢掉也能整腿绿。现在加五条：产物要素数与**输入**对撞
      （独立读端）、`LayerName == "input_buffer"`、`Geometry == MultiPolygon`、`Crs == EPSG:3857`、
      9 个字段名与顺序 == 基准件契约。**MUT-B4 同时把真实腿判红**（`buffer_quick: LayerName='input',
      expected 'input_buffer'`，整腿 96.02 s 处红）⇒ 这五条不是装饰。
      顺手把 roads core 的层名与字段名单**收成一处**（`$script:RoadsCoreLayerName` /
      `$script:RoadsCoreFieldNames`，`info_roads` 与 `buffer_quick` 共用），消掉两处各抄一份的漂移面。
    - 该腿用时：`real_data_vector_overlay_buffer_join` 恢复后 Debug 单跑 **188.16 s**（C3c-A 那轮 188.07 s）
      ⇒ 630 ÷ 188.16 = **3.35×**，新增的那次 `info` 读产物落在噪声里（约 0.1 s）。**没有抬任何 TIMEOUT**。
    - **登记**：`vector,buffer` PENDING→GOLD（证据用例就是新那条），`kPendingCeiling` **55 → 54**，
      头部 COUNT 按声明行自带命令重跑（data_rows 126 / gold_rows 72 / pending_rows 54 / all_lines 73 /
      table_driven 15 ⇒ literal_slot 57）；覆盖族 + 乱码守卫 **20/20** 绿。
      **注册数 745 → 746 / 743 → 744**（本片新增一条 `TEST_F` ⇒ `ctest -N` 就多一条；
      本行第一版写的是"不变 745 / 743"，那是**我数错了** —— 当时只看了"没新增 `add_test`"，
      忘了 gtest 发现模式是 PRE_TEST、每条新 `TEST_F` 本身就是一条注册；现值由 C3c-C1 那一跑的
      `-N` 现读反推更正，Release 侧的 744 要等收口轮重编才读得到）。
    - 本片未验收：`buffer` 的真实腿仍**不**判面积（`vector info` 的 Extent 只印 6 位有效数字 ⇒ 在
      1.28e7 的坐标上是 ±50 m 的粒度，比不了 100 m 的缓冲量级；要判就得对 22 836 个面跑 `geom_metrics`，
      那已经不是"一条 quick 腿"的量）。面积/圆头形状这一半由合成夹具的两条容差臂承重。
  - **批次 C C3c-B 第三片（dissolve）**（控制器自跑，C3c-B 到此三片齐全）：
    - **单元侧**把 `PluginTest.VectorDissolveExecution` 从"只有一句 `EXPECT_TRUE(result.success)`"补成两臂手算。
      臂 1 是两个 100×100 共边（x=100）的正方形、都写 `type='A'` ⇒ 融合体必是 (0,0)-(200,100) 这个矩形：
      面积 20 000、包框逐边手推、**且面积恰好等于包框面积** —— 这一条同时排掉"留了缝""把内边当成洞"
      "多出一块"三种坏法，而**不必**去数顶点（GEOS 保不保留 (100,0)/(100,100) 那两个共线顶点是实现细节，
      钉它等于把判据钉在 GEOS 的版本行为上）；再钉 `doDissolve` 写死的两条输出契约：层名 `dissolved`、
      字段就一个且名字就是用户点名的 `type`。臂 2 多加一块**离散**的 `type='B'` ⇒
      "分组丢失"交 1 个要素（面积 30 000）、"根本不融合"交 3 个要素、"分组键取错"面积/包框对不上
      —— 三种坏法各撞不同断言；WHY 必须有这条：臂 1 只有一组，"把所有几何并成一个"在臂 1 上看不出来。
    - **判别取证（R-B16）**：四条变异 ⇒ D1 分组键写死成 `"all"`、D2 只写第一块几何不做真融合、
      D3 分组键没写对（写 `"X"`）、D4 层名少一个 `d`。四条都是**新用例红、既有六条 `PluginTest.VectorDissolve*`
      全绿**（红点行号：D1 :4258/"all" + :4329/:4330/:4337；D2 :4262 面积差 10 000 + :4267 包框差 100；
      D3 :4360 点名 `unexpected group key: 'X'`；D4 :4251 层名）。D1 那一跑里连本用例自己那句
      `EXPECT_TRUE(result.success)` 都没红 ⇒ 旧判据确实一个都抓不到。恢复一律 `cp` + `cmp`，恢复后
      7/7 绿、全 vector 族 + 覆盖族 + 乱码守卫 **112/112** 绿。
    - **真实腿 `dissolve_bbox` 从"抠消息"补到落地读产物**：它过去唯一的判据是 `Assert-TextContains
      "Dissolve completed: 1 features"` ⇒ 产物层名/几何类族/CRS/字段/要素数整批丢掉也整腿绿。现在六条：
      要素数 == 1（不点名分组字段就是"整体融合"的契约）、`LayerName == "dissolved"`、
      `Geometry == MultiPolygon`（`CreateLayer` 写死的声明）、`Crs == EPSG:3857`、`FieldCount == 0`、
      **包框与输入逐边相等**（并集不改外包框 ⇒ 比的是"输入侧现读"与产物，不是拿产物比产物）。
      容差走 C3f 那条相对式子（1e-5 × 坐标最大绝对值 ≈ 128 m）—— `info` 的 Extent 在 1.28e7 这一档只印
      6 位有效数字，拿 0 去比会被印数精度判红，那不是缺陷。**D4 那一跑整腿红在 `LayerName='dissolve'`**
      ⇒ 这五条承重（用例内计时 dissolve_bbox 0.20 s，比补判据前的 0.19 s 只多一次小层 info 读）。
    - 该腿用时：`real_data_vector_overlay_buffer_join` 补完两片的判据后 Debug 单跑 **195.69 s**
      （buffer 片那一跑 188.16、C3c-A 那轮 188.07）⇒ 630 ÷ 195.69 = **3.22×**，三次散布 188~196 s；
      仍 >2.5× 触发线 ⇒ **没有抬任何 TIMEOUT**，登记为"这条腿再涨就拆"。
    - **登记**：`vector,dissolve` PENDING→GOLD（证据用例 `PluginTest.VectorDissolveExecution`），
      `kPendingCeiling` **54 → 53**（工单给 C3c-B 定的目标值 53 到此达成），头部 COUNT 按声明行自带命令重跑
      （data_rows 126 / gold_rows 73 / pending_rows 53 / all_lines 74 / table_driven 15 ⇒ literal_slot 58）。
      **注册数不变**（本片只把既有 `VectorDissolveExecution` 的块体撑大、没有新增 `TEST_F` 或 `add_test`）——
      本行第一版写的"不变 745 / 743"里的**数**是错的（上一条 buffer 已经把它推到 746 / 744），"不变"这个判断本身对。
      vector 侧剩 4 行 PENDING：`rasterize` / `polygonize` / `difference` / `spatial_join`。
    - 本片缺口（如实登记）：(1) **分组语义在真实数据上仍是 0 覆盖** —— 唯一那条真实腿的输入是单个面、
      且不点名分组字段 ⇒ 走的是"整体融合"分支；多组真实数据要补进 `union_poly` 那一档，而那一档的判据是
      **按组标分发**的（加第二条用例得先改分发结构，风险大于收益）⇒ 挂到 C3c-C 一起裁。(2) `doDissolve`
      写出段那三处 `result = nullptr; … delete result;`（同一变量先置空再 delete 的空转，不是缺陷但读起来
      像 use-after-free）留给 C3h-2b 冗余/一致性批。(3) `dissolve` 的 GEOS 失败分支按用户裁定无判据（N135 家族）。
  - **批次 C C3c-C0（rasterize 那行是陈旧状态，翻行不改代码）**：C3j 修 N132/N133/N134 时给
    `PluginTest.VectorRasterizeBurnsAttributeFieldAndReportsMetadata` 写了**手算逐像元**判据
    （4×4 夹具里 A 只盖 (i=0,j=3) 一个像元、B 盖 (2,0)(3,0) 两个 ⇒ `EXPECT_NEAR(sum, 13.0, 1e-4)` 加
    三条逐位置 `EXPECT_EQ` 与"值集合 ⊆ {0,3,5}"），但登记表的 `vector,rasterize` 一直没翻 ⇒
    那一行是**登记面的漂移**，不是覆盖面的缺口。本轮按块体现读全部 7 个
    `Vector{Difference,SpatialJoin}*` 用例块（含 Srs 拒绝臂、GPKG 索引臂与 N139 拒写臂，块边界由括号配对导出、
    判宏前先剥行注释）⇒ **没有一块**含 `EXPECT_NEAR` / `EXPECT_DOUBLE_EQ` / `EXPECT_FLOAT_EQ`；
    `polygonize` 根本没有专属执行用例（只在 J4/J5 的临时件表驱动里作为动作名出现）
    ⇒ 这三行是真缺，归 C3c-C1~C3 补。
    翻行后 `kPendingCeiling` **53 → 52**、头部 COUNT 按声明行自带命令重跑
    （data_rows 126 / gold_rows 74 / pending_rows 52 / all_lines 75 / table_driven 15 ⇒ literal_slot 59）；
    覆盖族 **19/19 绿**就是这次翻行的机器级凭据 —— `CoverageMeta.GoldEvidenceIsAuditable` 会剥掉注释后
    回读第 4 列那个块体，核"同一个块里既有 `"vector"` 又有把 `"rasterize"` 送进动作槽的形状、且仍有数值断言"，
    所以这不是我给自己发的合格章。**注册数不变**（本片只翻登记表一行、改一个常量，既不新增 `TEST_F` 也不新增
    `add_test`；那一版写的"不变 745 / 743"同样带上了上一条留下的错数，正确现值是 746 / 744，见 C3c-C1 的 `-N` 逐名核对）。
    真实腿 `rasterize_bbox` 早在 C3b-A 就在场（0.11 s 级，归 overlay_buffer_join）。
  - **批次 C C3c-C1（difference 三臂手算形状判据）**：新增 `PluginTest.VectorDifferenceSubtractsOverlayGeometry`
    —— 此前 5 条既有 `VectorDifference*`（含 Srs 拒绝臂、GPKG 索引臂、N139 拒写臂）**没有一条**含
    `EXPECT_NEAR`/`EXPECT_DOUBLE_EQ`/`EXPECT_FLOAT_EQ`（按块体现读、判宏前先剥行注释）。三臂各压实现里一条不同分支：
    臂 A 线 × 面（`useLinearPolygonSegmentation` 那条**不走 GEOS Difference** 的路径）期望两段
    `[0,80]` 与 `[120,200]`、总长恰好 160、层名 `difference`、CRS 3857、属性 `road` 保真；
    臂 B 面 × 面（100×100 挖掉 50×50 的一半）期望面积 7 500、包框 (0,0)-(100,100)；
    臂 C 被完全覆盖期望"整条丢掉 ⇒ 报 0 features、产物 0 个要素"。
    - **判别取证**：E1 `buildMergedSegmentGeometry(segments, false)` 翻成 `true`（取落在叠盖**里面**的那一段）
      ⇒ 新用例红在 :5911/:5927/:5928（段数 1 vs 2、总长差 120），**5 条既有用例全绿**；
      E2 面路径 `Difference` 改成 `Intersection` ⇒ 红 :5968（面积 2 500 vs 7 500）+ 包框两边，5 条既有用例全绿；
      E3b 把写出段的挡位改成 `if (true)`（没有差集结果也记账）⇒ 臂 C 红在"Difference completed: 1 features"，5 条全绿；
      E4 层名 `difference` 改成 `diff` ⇒ 红 :5898，5 条全绿。
    - **我写错的一条注释被变异否证并当场改正**：E3（只把 `!IsEmpty()` 挡位摘掉 ⇒ `if (diffGeom)`）
      我原以为臂 C 会红，实测**不红** —— 因为"被完全覆盖"这一支里 `diffGeom` 本来就是 null，那条挡位根本没被用到；
      要把臂 C 判红必须连记账一起改（E3b）。⇒ 用例注释里"臂 C 钉的是空几何污染产物"那句是错的，
      已按实测改成"钉的是记账口径：一条都没留下必须报 0"。**这就是"为解释判据而写的注释"自己也需要一条红臂**。
    - **新挂账 N143（本刀发现，未修）**：`doDifference`、`doIntersect` 与 `doPolygonize` 三个动作的 `Result::ok`
      （C3c-C3 复核时把同一文件里的 `doPolygonize` 也数了一遍：函数体内 `metadata` 出现 **0** 次）
      **一个 metadata 键都不发**（只回一句 "… completed: N features"），而同插件 17 个动作都发 `feature_count`
      ⇒ 真实腿与 GUI 只能从消息字符串里抠数（runner 现在正是用正则抠）。这与 C3g 当年给 `dissolve`
      补齐 `mergeStandardRunMetadata` + `feature_count` 是同一族（那次顺手修了一个，这次剩两个）。
      归 **C3h-2b 冗余/一致性批**（本刀不扩射程：改产品输出键要同步 GUI/spec 白名单与真实腿抠数逻辑，
      且加键属于产品行为变更，与"补判据"这一刀不同类）。
    - **登记**：`vector,difference` PENDING→GOLD（证据用例即新那条），`kPendingCeiling` **52 → 51**，
      头部 COUNT 按声明行自带命令重跑（data_rows 126 / gold_rows 75 / pending_rows 51 / all_lines 76 /
      table_driven 15 ⇒ literal_slot 60）；覆盖族 + 乱码守卫 + 6 条 `VectorDifference*` **26/26** 绿，
      全部 `PluginTest.Vector*` **93/93** 绿。
    - **注册数 746 → 747 / 744 → 745**（本刀新增一条 `TEST_F`；Release 侧此刻 `-N` 仍读 743，
      因为 release 树的 `gis_tests` 还没重编、gtest 的发现缓存没刷新 ⇒ 745 要等收口轮全目标重建后现读，
      登记为未验收）。`ctest -N` 逐名比对确认新增恰为 `PluginTest.VectorDifferenceSubtractsOverlayGeometry`
      与 `PluginTest.VectorBufferRoundCapStadiumMatchesHandComputed`（上一条漏记的那一条）。
    - 本片缺口：`difference` 的三条真实腿（`difference_focus` / `_far` / `_stress`）仍只判**消息里的要素数**，
      层名与几何类族契约在 22 836 要素的量级上没判 —— 与 buffer 那条同为"结构判据在合成夹具承重、
      真实腿判数量级"的分工；面积侧真实腿无从手算（省界不是可手推的坐标），保持不判。
  - **批次 C C3c-C2（spatial_join 两条口径 + "几何不许动"）**：
    - **夹具从 1 条 join 加到 3 条**（join_a 与 src_a 角部重叠、join_b 与 src_a 另一角重叠、第三条与 src_a 重叠但
      **值和第一条重**）。WHY 必须三条：实现里 `join_count` 数**所有**相交要素、`join_value` 只拼**去重后**的值
      且按 encounter order（现读 `vector_plugin_spatial_join.inc:135-160`）⇒ 只有一条 join 时这两条口径
      **长得一模一样**（1 / "join_a"），"计数也去重"和"值不去重"两种坏法都无法判。三条并存的期望是手推的：
      `src_a.join_count == 3`、`src_a.join_value == "join_a;join_b"`、`src_b` 全零。
    - **新增"几何不许动"这一半**：`doSpatialJoin` 里 `dstFeat->SetGeometry(srcGeom)` ⇒ 每条源要素的面积与包框
      必须逐字不变（两块 100×100 ⇒ 面积各 10 000、包框各 (0,0)-(100,100) 与 (150,0)-(250,100)）。
      这一组断言的存在理由很具体：**把连接做成交集/缓冲那一族坏法，join_count 与 join_value 照样算得对**，
      上面那些一个都发现不了。
    - **判别取证**：F1 `++joinCount` 挪进去重分支 ⇒ 红 :6444（2 vs 3）；F2 摘掉 `seenValues` 判重 ⇒ 红 :6445
      （"join_a;join_b;join_a"）；F3 把 `SetGeometry(srcGeom)` 换成 `SetGeometry(srcGeom->Buffer(1.0))` ⇒
      **只有新数值臂红**（:6451-6461，面积差 403.14016、四边各差 1），`join_count`/`join_value` 两条全绿。
      顺带第三次独立验证了那套圆头模型：100×100 面外扩 1 的锐角面积 10 404 减去四角补的 `(4−π)·1²`
      = **10 403.14016**，实测 **10 403.1401574**。
    - **真实腿 `spatial_join_focus` 补五条产物侧判据**（原来只有"CLI 自报要素数 + 文件在场"）：层名 = **源层名原样**
      （`CreateLayer(srcLayer->GetName(), …)`，不是重命名）、字段 = 输入 9 个 + `join_count`
      （本用例没点 `join_field` ⇒ 按契约**不该**有 `join_value`，这一条同时挡住"无条件加两个字段"）、
      几何类族 `MultiLineString`、CRS `EPSG:4326`、**包框与输入现读逐边相等**。期望来源全是输入侧
      （`$InputFeatureCount` / `$roadsStats` / `$script:RoadsCoreFieldNames` 单点名单）。
      同一变异 F3b 在这一跑红在包框那条：`worst boundary offset 1 > tolerance 0.00117451`；恢复后整腿
      192.72 / 196.45 s ⇒ 630 ÷ 196.45 = **3.21×**，**没抬 TIMEOUT**。
    - **我当场违反的一条老规矩**（登记防复发）：我往 `.ps1` 的**字符串字面量**里写了中文与 `⇒`，
      PS 5.1 按 ANSI 解无 BOM 的 UTF-8 ⇒ 某个字节序列被解成裸引号，整脚本 `ParseErrors=34`（ParserError，
      整条腿 2 s 就红）。改成 ASCII 字面量后 `ParseErrors=0`，且现扫我这一带 2650-2690 行**只剩 ASCII 字面量**
      （文件里 7 处历史非 ASCII 字面量不是我这次引入的，也没被这次改动碰到）。
      抓它的正是每次改 runner 后都要跑的那条 `Parser::ParseFile` 检查 ⇒ **这条检查不能省**。
    - **登记**：`vector,spatial_join` PENDING→GOLD（证据用例 `PluginTest.VectorSpatialJoinWritesJoinCountAndValue`），
      `kPendingCeiling` **51 → 50**，头部 COUNT 按声明行自带命令重跑（data_rows 126 / gold_rows 76 /
      pending_rows 50 / all_lines 77 / table_driven 15 ⇒ literal_slot 61）。覆盖族 + 乱码守卫 + 两条
      `VectorSpatialJoin*` **22/22** 绿。**注册数 747 不变**（本片只撑大既有用例的块体与夹具，没加 `TEST_F` ——
      这次是先跑了 `-N` 再写这句）。vector 侧只剩 **1** 行 PENDING：`polygonize`。
    - 本片缺口：真实腿不判 `join_count` 的**分布**（要逐要素求和，runner 这一层没有属性求和读数）；
      `spatial_join` 的多值拼接顺序依赖 join 层读取序，这一点由合成夹具钉住、真实腿不钉。
  - **批次 C C3c-C3（polygonize 三臂 + vector 侧 PENDING 归零）**：新增
    `PluginTest.VectorPolygonizeExecutionCarvesEqualValueRegions` —— 这个动作此前**没有专属执行用例**
    （只在 J4/J5 的临时件表驱动里作为动作名出现，那一层判"产物在场 + 要素数 + 层名/字段"，几何一个没读）。
    - 三臂期望全是**像元算术**（GT = `{100,1,0,200,0,-1}` ⇒ 每像元是世界 1×1 方块，整幅覆盖
      (100,190)-(110,200)）：臂 1 两值左右分 ⇒ 2 个面、DN=1 (100,190)-(105,200)、DN=2 (105,190)-(110,200)、
      面积各 50、**两者之和 = 100 = 整幅面积**（守恒式，等值区必须不重不漏）；臂 2 在左半放一个单像元第三种值 3
      ⇒ 3 个面、DN=1 面积 49 且**带一个内环**、DN=3 面积 1 且包框恰是那个像元 (102,194)-(103,195)、三者和仍 100；
      臂 3 双波段（band 1 左右切 / band 2 上下切）带 `band=2` ⇒ 期望面积仍各 50，但**包框方向必须变成横切**。
    - **判别取证三条**：G1 `GetRasterBand(band)` 写死成 1 ⇒ **只有臂 3 红**，而且红的确实是两条方向断言
      （`maxX`/`minY` 各差 5），面积断言照样过 ⇒ 当初"取错波段时面积仍各 50，判据必须落在包框方向上"
      那句设计判断是被这一跑证实的，不是事后解释；G2 栅格投影没抄进产物 ⇒ 三臂全红在 CRS 断言（:6538）；
      G3 输出层名 `polygonized` 改成 `poly` ⇒ 单元侧红 + **真实腿同一条变异红**
      （`polygonize_bbox_raster: LayerName='poly', expected 'polygonized'`）。恢复一律 `cp` + `cmp`，
      恢复后单元 1/1 绿、真实腿 191.86 s 绿。
    - **真实腿 `polygonize_bbox_raster` 从一句抠文本补到五条落地判据**：层名 `polygonized`、层声明 `Polygon`、
      字段恰好 `DN` 一个、要素数由**独立读端**再数一遍与自报的 1 对撞。⚠ 这一腿的输入是 `rasterize_bbox`
      那一步的产物（栅格链路本来就串在产品里）⇒ **不拿"输入包框"当期望**（那是拿产物比产物），
      包框与波段选择的判据由合成夹具承重。
    - **登记**：`vector,polygonize` PENDING→GOLD，`kPendingCeiling` **50 → 49**，头部 COUNT 按声明行自带命令
      重跑（data_rows 126 / gold_rows 77 / pending_rows 49 / all_lines 78 / table_driven 15 ⇒ literal_slot 62）。
      **vector 侧 PENDING 归零**：`grep -c '^vector,'` = 33、`grep -c '^vector,.*,GOLD'` = 33，
      且 `CoverageMeta.EveryActionIsRegistered` 是双向钉（漏登记红、陈旧行也红）现读通过 ⇒
      这个 0 是由覆盖全部 33 行的那条规则数出来的，不是窄规则的 0。覆盖族 + 乱码守卫 **20/20** 绿。
      注册数 **747 → 748**（新增一条 `TEST_F`；这句是写完 `-N` 现读之后才落笔的，上一轮同类错误已登记在 C3c-C1）。
      Release 侧仍是"未重编 ⇒ 读不到新注册"，收口轮现读。
    - 全表余量（三个数都是 `git show <提交>:tests/action_gold_cases.txt | grep -c` 现读，不是回忆）：
      批次 C 开工那一次覆盖矩阵提交 `c225d8b` 是 **PENDING 72 / 其中 vector 23**；
      到 C3f 那一档 `fc68e12` 是 **61 / 12**；本轮 C3c 收尾后是 **49 / 0**。
      四拍板里"72 条 PENDING 全晋升"仍未完成，剩下的 49 条全在**非 vector** 插件。
    - 本片缺口：`polygonize` 的真实腿仍不判面积/包框（输入是产物）；`band` 参数只在合成夹具的双波段臂上有判据；
      `CONNECTEDNESS`（4/8 邻接）本刀没碰 —— 两臂的等值区在两种邻接下划分相同，不构成可判差异，
      留作后续若要钉该语义时的设计前提（不要拿现在的夹具硬造一个"看起来不同"的期望）。
  - **批次 C C3h-2b（vector 冗余与一致性批 · 第一片：metadata 家族 + 死代码 + 取数口）**（控制器自跑）：
    - **先复核我自己那份清单**（不信任旧登记）：`progress.md:823` 记的两条死代码
      （`writeUnionFeature` / `writeUnionFeatureOwned`）与 N141 的 `gis::core::RasterBlockProcessor`
      现读 `grep -rn ... src/ tests/` 均 **0 命中** ⇒ 已由 C3n-2d 那一刀 `be8b443` 连定义一起删掉，本刀无需再动；
      `progress.md:960` 那条 param_guard 旁注漂移维持"叙述性引用、不参与比对 ⇒ 本刀不动"的原裁定。
      ⇒ 清单三条里两条已结案、一条维持原判，本刀真正动手的是下面四项。
    - **(d) doDissolve 的 Ownership 记账**：旧写法是 `OGRGeometry* result = unionGeometryList(geoms);`
      紧跟 `writeGeometryFeatureOwned(dstLayer, std::unique_ptr<OGRGeometry>(result), …)`，
      然后在四条路径上各写一次 `result = nullptr;` 再 `delete result;`。读 `unionGeometryRange` 现确认后
      （base case 直接返回 `geoms[begin]` 这个别名；两侧都非空时 `left->Union(right)` 之后把 left/right 都 delete；
      单侧为空则返回另一侧）⇒ 传进去的每个几何都由这条链消费，而 `writeGeometryFeatureOwned` 收的是
      **值参 `unique_ptr`** ⇒ 那四处置空与其后的 `delete` 全是 `delete nullptr` 空转：不致 double free，
      但读起来像 use-after-free，并把"谁拥有这块几何"这个唯一要紧的事实藏住了。改成
      `std::unique_ptr<OGRGeometry> result(...)` + `std::move`，移交点在代码上可见。
    - **(g) N143 补全，并且更正我立案时的普查**：用脚本对 **HEAD 树**与**工作树**各跑一遍
      （`do*` 函数体为界，判三件事：有没有 `mergeStandardRunMetadata`、有没有 `metadata["feature_count"]`、还发哪些键）：
      HEAD 里 33 个 `do*` 中 **17** 个发 `feature_count`、**5** 个走标准三键、标准键与计数键都有的只有 3 个
      （buffer/convert/dissolve），而"**写出矢量产物却一个 metadata 键都不发**"的族是 **6** 个函数
      （clip、difference、filter、info、intersect、polygonize）—— 我立案时写的是 3 个，**漏了 clip 与 filter**，
      `info` 则是第 7 种情形（它的产物就是那段 info 文本：`Result::ok(oss.str())`，不写文件）⇒ 不算同族。
      本刀把 **5 个写件动作**（difference/intersect/polygonize/clip/filter）全部补成
      `mergeStandardRunMetadata(...) + feature_count`，工作树现读：`feature_count` **22** 个、标准三键 **10** 个、
      零 metadata 只剩 `doInfo` 一个。代码里那句"同插件 17 个动作都发 feature_count"就是这次普查的 HEAD 侧数。
      - **同数异名的两案裁定（不补键）**：`doUnion` 已有 `union_total_pieces`（就是同一个 `count`）、
        `doRepair` 已有 `repaired_count`（写循环里每成功写一条自增一次，也就是消息里那个数）⇒
        再发一个 `feature_count` 就是同一个数两个键，正是本批要清的冗余而不是要铺的口径。
        把这条裁定同时写进了实现注释（`vector_plugin_overlay.inc` union 记账处）与真实腿
        `union_focus` 的取数口注释里，避免下一个人为"对齐"再补一刀。
      - **判键必判**：新增 10 条断言，全部落在既有用例里（注册数因此**不变**）——
        difference 三臂（`action`=difference、feature_count 1/1/0）、intersect（action + count=1）、
        polygonize 三臂（action + count=2/3/2）、clip（消息整串 + action + count=1）、filter 两臂（1 与否定臂 0）。
        filter 那条"本动作**没有** feature_count"的辩护注释按 C3f 的旧实态作废，已改写并保留错因。
      - **变异取证两轮**（恢复一律 `cp` + `git hash-object` 对基线 blob）：把三处 `feature_count` 写成 `count + 1`、
        并把 `mergeStandardRunMetadata` 的 action 字面量换成同族另一个动作名 ⇒ difference 三臂 / intersect /
        polygonize 三臂逐条红，红的是**键值差**（`Which is: "2"` 这类）而不是 `.at()` 抛异常；
        再把 clip/filter 两处同样改形 ⇒ clip 两条 + filter 三条（含否定臂）红。恢复后 `PluginTest.Vector*` **94/94** 绿。
    - **(e) doBuffer 的死事务支**：`const bool useTransactions = false` 挂着 5 处 `if (useTransactions)`
      与一个恒取 5000 的 `transactionBatchSize`。追历史：`f18164a`（2026-04-25）先给它加上
      `outFormat == "GPKG"`，两天后 `699429a`「修复缓冲区 GeoPackage 输出异常」把它钉成 `false`，
      理由（当时的英文注释）是事务批量写会走 GDAL 的延迟 SQL 索引路径 ⇒ 产出"rtree 表登记了但一行不回填"的空索引，
      正是 N138 那一族；buffer 因此一直是 N138 案发现场里的**对照件**。⇒ 这不是"漏了对齐"，是带理由的取舍，
      所以本刀**不**把它改成与 clip/union 同形，而是删掉那 5 处永不执行的分支，并把裁定改写成中文注释留在原地；
      同文件 doClip 那句"（理由同 doBuffer）"随所指对象消失，已改成自含说明。运行期逐字不变（空转消除）。
    - **(h) J4 动作表的名字级去重闸**：`VectorOutputMustNotOverwriteInput` 原本只有一条
      `ASSERT_EQ(sizeof(actions)/sizeof(actions[0]), 24u)`，它挡得住"掉队"挡不住"顶替"——
      把一个动作名写两遍、另一个漏掉，数组仍是 24 条且每条都照样跑绿 ⇒ 射程静默缩水。
      补一条 `std::set` 去重计数（仍须 24）。**判别取证**：把 `vertices_extract` 那一行改成重复的 `centroid` ⇒
      24 条臂全部照跑、只有这条新闸红（`19232: 23 vs 24`），恢复后连同 `CoverageMeta.*` 与乱码守卫 **21/21** 绿。
    - **真实腿取数口统一**（本批的"一致性"落点）：矢量腿此前从 CLI 的**散文消息**里抠要素数
      （`Difference completed: (\d+) features` 这类），而插件早就把同一个数放进 metadata。
      新增 `Assert-ReportedMetadataCountEquals`，把 **17 处**取数口换成整行锚定的 `feature_count` 键，
      按原出口分三类数（`git diff` 现读逐条点过）：`Assert-CountEquals` 抠消息的 **10** 处
      （buffer_quick、clip focus/far/partial/stress 4 处、difference focus/far/stress 3 处、
      intersect focus/far/partial 3 处、spatial_join_focus 1 处 —— 其中 buffer_quick 原本是**未锚定**的
      `feature_count: (\d+)`，正是本仓"短数字被长数字当前缀吃"那一族的暴露面）、
      `Get-ReportedCount` 抠消息的 **5** 处（filter 三处：`filter_cn_attribute`、geom_metrics 分类对账、
      nearest 的 sameDataset 否定臂；外加 clip_partial 与 intersect_partial 两处取数后还要交叉对账的），
      以及 `Assert-TextContains` 整串比散文的 **2** 处（dissolve_bbox、polygonize_bbox_raster）。
      10+5+2 = 17。`polygonize_bbox_raster` 的落盘读端另从写死 `-eq 1` 改成
      **与自报数对撞**（自报数那边仍有一条 `= 1` 的闸 ⇒ 两侧不一致时能报出是哪一侧）。
      `union_focus` / `union_poly` 两条**不**跟着换（理由见上面同数异名裁定），并在那儿写了注释。
      helper `Assert-CountEquals` 改完后仓库内调用者归零（`grep -rn` 现读 0，只剩定义）⇒ 按死代码口径连定义删除。
      ⚠ 这一族判据"换出口"不增加独立性（消息与键同源于同一个 `count`），它的价值是**取数口锚定**与
      不再被文案改动连带红；真正独立的第二出口仍是既有那条"自报数 vs `vector info` 回读数"对撞。
    - **验收状态（Debug 现读）**：`PluginTest.Vector*` **94/94**（47.60 s）；`CoverageMeta` 族 + J4 +
      乱码守卫 **21/21**；六条矢量真实腿**全绿**（一轮串行 980.21 s）—— quick 292.87 / checks 73.34 /
      overlay_buffer_join 202.97 / overlay_ops 158.62 / union_poly 170.32 / derive_heavy 82.02 s。
      ⚠ **其中 quick 与 checks 两个读数是脏的**：同一时间窗里我自己跑了一条仓库级 `grep`（含 `.git/` 与 `data/`，
      120 s 后才终止），属"全量耗时会被并行任务污染"的已知形态 ⇒ 不作数，安静态复测另记。
      安静态复测把 checks 的真凶找到了（见下一条第二片）。
      注册数 `ctest -N` 仍 **748**（本片只往既有用例里加断言，没有新 `TEST_F`、没动 `tests/CMakeLists.txt`），
      Release 侧仍未重编 ⇒ 收口轮现读。`data/` 未被写入：`find data -newermt "2026-10-25 00:00:00" -type f` = **0**，
      目录里最新 mtime 是 2026-10-21 20:50 的一批 `.aux.xml`。
    - **新挂账**：**N144**（矢量插件 23 个 `do*` 函数把 `srcSRS` 建成裸指针克隆，取消路径各漏一个，
      普查 54 个 `throwIfCancelled` 点，修法与射程见 `task-C3h-2b` 台账条目，单独一刀做机械 RAII 替换）；
      **N145**（输出契约的一致性射程：14 个动作发 `feature_count` 却不走标准三键，另有 union/repair 两个"同数异名"键
      ⇒ 要么把三键铺满要么把 `feature_count` 定为唯一计数口径，需要一个口径裁定而不是逐案补键）。
    - 本片缺口：`clip`/`filter` 的新键只在**合成夹具**那一侧有判据（真实腿已改读键，但没为这两个动作新增产物侧断言）；
      `union` 的 `union_total_pieces` 与"要素数"口径不同（它是分块数）⇒ 本刀没去动它的语义，只把"为什么没有
      feature_count"写进注释。文档侧如实度已现读：`并集.md:115` 写的是 `union_total_pieces`＝"输出总条数"、
      `修复.md:38` 写的是 `repaired_count`＝"成功写出到结果图层的要素数量" ⇒ 两篇都在描述**自己的**键，
      缺的不是文档而是"这些键与 `feature_count` 是否同一口径"那层约定（N145 要裁的就是它）。
  - **批次 C C3h-2b 第二片（真实腿的输入现读按档加闸，2026-10-25）**（控制器自跑）：
    - 起因是我自己上一片那轮六腿读数里的两个异常：quick 292.87 / **checks 73.34**。我先按"并行污染"把两个数
      一起作废（那条仓库级 grep 确实在同一时间窗扫了 `.git/` 与 `data/`，120 s 后才终止），安静态复测后
      quick 回到 **258.90 / 267.03**（作废成立），但 checks 复测仍是 **63.71 / 66.01**，而它的历史读数是
      31.83 / 33.55 / 34.47 ⇒ **污染解释不了 checks，这是一笔真实的退化**。
      ⇒ 记一条自纠：**"读数是脏的"这个判断本身也要被复测证伪**，不能整批作废了事。
    - 归因先猜错、再打点测出来：猜的是"province GeoJSON 被解析两遍"，独立实测 `copy=0.03 s`、`parse=0.21 s`
      ⇒ 假设当场作废。改在 runner 的**副本**里插 `[System.Diagnostics.Stopwatch]` 打点跑真实一档：
      `Ensure-BenchmarkData 0.07 s` → `Ensure-C3cARealInputs 39.14 s` → 注册与其余准备 `8.48 s`
      → 18 条用例本体 `23.23 s`，整腿 `70.92 s` ⇒ 一半以上墙钟付给一份该档**根本不消费**的输入现读，
      真凶是 province 那 42 个面的包框在 PowerShell 里逐点遍历（`Get-FeaturesExtentStats`）。
      上游那条"C3c-B 其余约 32 s 是包装开销（含 Ensure-C3cA/C3cB）"的记账就是这笔钱第一次露头 ——
      当时只当开销记下，没去查它**凭什么由每一档来付**。
    - 修法（与本批主题同源：这是"没人用的准备工作"，不是性能调参）：`$buildC3cAInputs` 只在
      `Group=all / overlay_buffer_join` 走完整现读，其余四档走**同一个函数**的 `-PathsOnly` 分支
      （只给路径、事实一律 `$null`；路径构造只留一处，不许抄第二份）。四条 C3c-A 用例仍在所有档**照旧注册**
      ⇒ 名单不变量不受影响，注册段用到的两个路径照样给得出。
      ⚠ **另一条我以为该一起加的闸被证据否掉**：c3f 的 refs 里有派生值（`--where` 子句本身）直接进注册段，
      给它加档闸会改变那四条用例的语义 ⇒ 不加。
    - 加闸带来一种新的失败形态：旧的 `$null -ne $Refs` 判据挡不住"**对象在、路径在、事实全是 `$null`**"，
      而这种形态在 PS 里会让后续比较退化成"既不满足也不报错"⇒ 正是本仓反复出现过的"绿着把期望蒙过去"。
      新增 `Assert-C3cARefsBuilt` 判事实级（四个消费点全部改用它），并做**反向臂**：把闸拧成恒假再跑
      `overlay_buffer_join` ⇒ 该腿当场红在
      `convert_roads_geojson: C3c-A refs are path-only (this Group was gated by False)`（114.82 s 处红，
      恢复后 `git hash-object` 与基线 blob 逐字相同）。
    - 加闸后的安静态六腿现读（Debug、串行 `-j1`、期间零并发任务，整轮 764.91 s，改前同轮 980.21 s）：
      quick 257.17 ⇒ **2.80×**、checks 33.37 ⇒ **3.00×**、overlay_buffer_join 178.38 ⇒ **3.53×**、
      overlay_ops 110.16 ⇒ **3.90×**、union_poly 137.23 ⇒ **4.37×**、derive_heavy 48.57 ⇒ **7.21×**。
      ⇒ 六档全部回到 2.5× 触发线之上 ⇒ **不拆腿、一个 TIMEOUT 都没抬**；checks 回到加闸前的历史水平，
      也顺带说明那条"1.52× 跌破"不需要拆腿手术、需要的是把不该收的钱退了。最紧的一档仍是 quick（2.80×），
      登记为"再涨就拆"。Release 侧仍无现读 ⇒ 收口轮全目标重建后回填。
    - 验收（Debug 安静态现读）：全量单元 **740/740**（`-E '^real_data'`，384.37 s；748 条注册里真实腿占 8 条 ⇒
      740 是这个排除法的算术结果，不是漏跑）；六条真实腿另计（上面那栏）；乱码守卫与 `CoverageMeta` 族在 740 里。
      Release 侧仍未重编 ⇒ 收口轮现读。`data/` 依旧未被写入（`find data -newermt` 见上一片那条）。
    - 过程中的另一条 gotcha 已入册：打点副本第一次写成 **LF** ⇒ 同一批字节在 PS5.1 下 `ParseErrors=153`
      （而原文件 CRLF 形态 `ParseErrors=0`）。原因是 PowerShell 按 ANSI 码页解码无 BOM 的 UTF-8 时，
      GBK 双字节的配对结果取决于 ASCII 字节的**奇偶与位置**，而行尾正是那批 ASCII 字节的一部分
      ⇒ **这份 runner 的行尾本身会影响它能不能解析**，"改完必跑 `Parser::ParseFile`"这条流程省不掉。
  - **批次 C C3k-3（quick 档里最贵的两条重活拆成 join_heavy，2026-10-26）**（控制器自跑）：
    - **触发**：阶段二双树全量轮现读 Debug `real_data_vector_quick` **302.42 s**，TIMEOUT 720 ⇒ **2.38×**，
      跌破本仓 2.5× 拆腿触发线（同一配置安静态单跑是 257.17 / 267.03 ⇒ 2.80×）。按"抬 TIMEOUT 不是选项、
      跌破就拆"的规矩处理；**不按"安静态没跌破"放过**——本仓的定值口径是"取两棵树里更慢那一棵的**最大现读**"，
      302.42 是同一配置的实测值，不能因为另一跑更好看就丢掉。
    - **归因**：quick 档 27 条里 buffer_quick（约 57~76 s）与 spatial_join_focus（约 48~67 s）两条占小一半；
      其余 25 条加起来才百秒量级。不是"哪条腿变慢"，是 quick 档自 C3c-A/B/C 起一路加用例、没人回头量。
    - **拆法**（沿用 C3k / C3c-B 的形状）：两条挪进新组 `join_heavy`，**只在 Mode=full 注册**
      （quick 档的成员名单就是"注册了哪些"，不收窄注册的话 Mode=quick 仍会跑，拆腿等于白拆）；
      判据一个字没动 —— `Validate-CaseOutputs` 对这两条按**名字**分发，不按组标。
      组标机器同步扩到六组：`$rosterGroupNames` + `$rosterJoinHeavy` + 第 3 条消息 + 新增**第 9 条**
      （join_heavy 成员名单 ↔ `$script:JoinHeavyCaseNames` 双向钉，名单单一来源）。
    - **测量**（Debug）：安静态单跑 133 s / **146 s**（用例内 buffer_quick 68.48/75.73、spatial_join_focus 61.64/67.02）
      ⇒ 新腿 TIMEOUT = ceil(146×4/10)×10 = **590**（4.04×，按"新腿 ≥4× 起步"）；拆后 quick 安静态 166.81 / 165.90 s
      ⇒ 4.32×。
    - **双树全量复跑（本刀收口门）**：**Debug 749/749（1099.05 s）、Release 747/747（515.48 s）**，
      整轮墙钟对比改前 1363.88 / 714.97 s。逐腿现读（Debug / Release，TIMEOUT 取更慢的 Debug 树）：
      quick 151.22 / 63.84 ⇒ **4.76×**、checks 30.78 / 15.25 ⇒ **3.25×**、overlay_buffer_join 72.83 / 57.09 ⇒ **8.65×**、
      overlay_ops 107.53 / 14.19 ⇒ **4.00×**、union_poly 129.78 / 20.30 ⇒ **4.62×**、
      derive_heavy 53.41 / 46.37 ⇒ **6.55×**、join_heavy 109.32 / 91.21 ⇒ **5.40×**。
      ⇒ 七档全部 ≥3.25×，没有一条需要再拆；`overlay_buffer_join` 因为挪走两条重活，余量反而从 2.77× 升到 8.65×。
    - **安装态冒烟（同轮现做）**：Release/Debug 两配置 `cmake --install` 到 `build/tmp/install_*` 均 rc=0；
      `gis-cli.exe --list` rc=0 且 stdout **1852 字节 / 27 行**（与文档记载逐字节一致，按字节判）；
      Release `bin/*.dll` = **78**；Debug `bin/platforms/` = `qwindowsd.dll` + `qoffscreend.dll`、
      `bin/sqldrivers/` = `qsqlited.dll`；`install_closure` 用例在双树全量里绿。
    - **顺带发现并当场改正的文档漂移**：`发布前验证 2.2` 那条 `gis-gui.exe -platform offscreen --self-test`
      直接跑会 **rc=3**（不是崩溃：`src/gui/main.cpp:197-204` 的 N101/N102 守卫拒绝在
      `GIS_TOOL_SETTINGS_INI` / `GIS_CUSTOM_INDEX_PRESET_FILE` 未注入时运行，免得写真实用户配置）。
      按 ctest 的注入口径补上两个环境变量后两配置都 rc=0 ⇒ 文档已按此改写（见 2.2 那条）。
  ⚠ **不要把墙钟当刻度读**：本机同一套用例的历史分布本来就很宽 —— 692 条那两轮是
  **350.65 s 与 405.02 s**（差 54 s），690/691 条的 Release 是 **177.65 / 200.72 / 228.62 / 231.79 s**。
  所以本轮 Debug 374.46 → 429.86、Release 200.72 → 231.79 **不去找因果**，按机器波动处理。
  ⇒ 上一版那句"多出来的约 16 s 我没有解释"是同一个意思，只是当时样本太少；现在样本够了，
  结论就是**这一栏只能现读现写：不许按条数外推，也不许拿它论证某个改动"变慢了"**
  （本仓有"条数涨而用时降"的先例）。
  ⚠ **"最慢单条"这一栏的取法要说清**：693 条的用时**全部**能被 `Passed <N> sec` 抓到
  （逐条求和 427.8 s，与整轮墙钟 429.86 s 只差 2 s，那 2 s 是 ctest 自己的调度开销）⇒
  最慢值 22.31 s 是全库最慢，不是子集最慢。但**名字与用时的配对**只成功 594/693 条
  （其余断在两行），所以"最慢的是哪一条"要单独核：本轮最大值 22.31 s 恰在配对成功的那批里，
  与 `source_mojibake_guard` 同一行 ⇒ 这一条成立；以后若最大值落在没配上的 99 条里就得手工认领。
  ⇒ **C3h-1b 轮这一栏不需要手工认领**：`Passed <N> sec` 单行抓到 **724/724**（Debug）与 **722/722**（Release）条、
  逐条求和 864.67 / 462.95 s，与整轮墙钟 865.12 / 463.39 s 各差 **0.45 s**（纯 ctest 调度开销）⇒
  上表四个"最慢"就是全库最慢，且名字与用时**逐条成对**，不用再走上面那条手工认领。
  ⚠ 还有一轮 **371.56 s**（B17 修复后那次全量）**没有**进上表，因为当时四个终审评审席正在并发读盘
  ⇒ 按本文件"全量耗时会被并发活动污染"那条纪律，它只能算**下限样本**，不能当安静值引用。
  三条脚本类用例的同轮现跑：`source_mojibake_guard` **22.31 s（Debug）/ 18.33 s（Release）**、
  `install_closure` **15.40 s / 10.32 s**（安装审计类，它自带 RESOURCE_LOCK、不会与别的用例抢同一个前缀；
  比 B21 轮的 17.26 / 11.92 各快约 1.9 s / 1.6 s —— "少拷 78 个文件"只是**候选解释**，
  本机波动本身就是 ±2 s 量级，不作定论）、`param_guard_visible_keys_all_read` 4.19 s / 3.50 s
  ⇒ 距 300 s 的 `TIMEOUT` 线最紧的一条有 **300 ÷ 22.31 = 13.4×** 余量。
  ⚠ 早先流传的 197.96 s / 385.53 s（632/630 时期）、370.40 s / 176.74 s（634/632 时期，21-D 收尾）
    与 382.54 s / 188.41 s（批次 A 收尾 635/633）都已被上表取代；留这些只为对账，引用请走上表现值。
  ⚠ 余量的真实风险不在均值墙钟，而在**并发构建叠加**：本批实测 `ctest -j 8` 与另一棵树构建同时跑会造出 3 条假 `Timeout`（见下一节）。
  - **批次 C C4a（阶段三第一片：125 篇动作说明的参数表 ↔ 二进制逐行对撞，2026-10-26）**（控制器自跑）：
    - **射程**：126 动作 ↔ 125 篇（缺的 1 篇是 D5 `精度评估.md`）、**591 个参数行**逐行配对后比
      标签/条数/必填/默认值。期望侧取编译进产物的三张表（`actionUiConfigStorage()` 的 per-action
      visible/required、两张标签表 + `paramSpecs()`、`gis-cli <plugin> --help` 的 required/Default）。
    - **收口前的现值**：`findings=0`，`verified-suppressed=7`（每条都带源码出处，见漂移清单 D9），
      逐行配上键的覆盖率 **590/591**（唯一未配上的是 `cutting.tile` 的「重采样方法」= `resample`，
      它是 tile 实读却没进 `visibleKeys` 的键 ⇒ N109 藏键方向，文档那行必须留着）。
    - **改掉的事实类漂移**（数字都取自动化本轮的实测）：**补行 5 / 删行 2 / 默认值 4 / 必填 5 /
      口径注 2 / 标签对齐 141 处（80 个文件）**。代表性的四条：`processing.gabor_filter` 的
      `核大小 9→5`、`sigma 2.0→1.5`；`processing.kmeans` 的 `KMeans K 5→4`（兜底与 spec 同值，
      `processing_plugin.cpp:1477`）；`terrain.fill_sinks` 缺 `微坡度` 行**且整段"算法内容"还停留在
      批次 A Task 11 之前的 3x3 邻域迭代** ⇒ 按 priority-flood 重写（这一条是参数核对抓不到的类别，
      记为 D7）；`vector.sliver_remove` 的 `最小面积` 文档标必填、实际兜底 `10.0`。
    - **比对器自己的两个缺陷**（都造成过真实误判，不是"顺手改改"）：
      (a) 解析 `kConfigs` 不剥 C++ 注释 ⇒ 条目内一条 `//` 被当成一个字段，把 visible/required 右移一位，
      `terrain.viewshed` 被误报成"白名单为空、文档多 9 行"，而 `gui_data_support.cpp:1137-1145` 恰好声明了 9 个键；
      (b) 标签比对大小写敏感 ⇒ `sigma`↔`Sigma` 配不上，而**配不上的行会跳过必填/默认值检查**，
      `gabor_filter.sigma` 的默认值漂移就是这么漏掉的。修完 (b) 它立刻浮出来。
      ⇒ 教训入册：**"事实类 0 条"只有在配上率 100% 时才是结论**（D10），这也是先做 141 处标签对齐的
      真实理由——不只是为了措辞统一，而是为了让每一行都进入默认值/必填检查。
    - **工具备忘（本轮踩的）**：机械改文档单元格时，Python 的 `$` 允许匹配在串尾换行之前 ⇒
      `re.match(r"^(...)(.*)$")` 重建整行会把那个 `\n` 丢掉，一次改坏 80 篇的参数表（行两两粘连）。
      已改成"先摘行尾再重建"，并给每个文件加了 **换行数/CRLF 数/BOM 三项写前写后相等**的自证；
      改前还拦下一次错配（逐行"先到先得"会把 tile 的「重采样方法」配上 `overview_resampling`，
      只因两者都含"重采样"；删掉"剩一个就配一个"的猜测规则后，kmeans 的「波段号」行才暴露出它本该删）。
    - **代码侧只动了一处注释**（`processing_plugin.cpp` 的 doKMeans）：记清"K-Means 没有 band 参数、
      `band` 取出来从不使用"。死读本体**没删**——删除要连同反向参数守卫与用例一起过，留给参数契约批。
    - **本轮新增挂账**：N146（GUI `requiredKeys` 未覆盖"按动作硬失败"的键，现值 24 种键、去掉
      `input`/`output` 后 22 种）、N147（`viewshed` 观察点、`reservoir_volume` 水位缺省静默按 0 算，
      返回成功但结果无意义）；另记 **乱码守卫射程不含 `docs/`**（`tests/check_source_mojibake.ps1:103`
      只遍历 `src, tests, cmake, .github`），文档侧编码/表格完整性目前只有我这轮的一次性脚本查过 ⇒ 没有机器级兜底。
    - **验证**：`findings=0` 由比对器现读；80 篇改动文件严格 UTF-8 解码 0 失败、0 BOM、
      参数表每行 5 列 0 违例、逐文件换行/CRLF 数与改前一致；`data/` 未被写入
      （`find data -newermt "2026-10-26 00:00:00" -type f | wc -l` = **0**）；跑动前硬门控 = 0。
      ⚠ **未验收**：本片段没跑 ctest（只改注释，产品二进制不变）；文档同步守卫进 ctest、D5 补文档、
      正文级（非参数表）逐篇对撞都在下一片。
  - **批次 C C4b + C4c（D5 补文档 + 文档同步守卫进 ctest，2026-10-26）**（控制器自跑）：
    - **C4c**：新写 `docs/算法说明/分类统计/精度评估.md`（126 动作里唯一缺文档的 `accuracy_assessment`，
      即 N113/D5）。内容全部按 `doAccuracyAssessment` 现读代码写：混淆矩阵按 `[参考值][分类值]` 建、
      **PA 分母=行合计 / UA 分母=列合计**（批次 A Task 8 纠正过的口径）、Kappa 用 `Pe=Σ(行×列)/N²`、
      NoData 只在**声明过**时剔除（不看 pbSuccess 会把合法的 0 类整类删掉）、只取第 1 波段、
      不做几何对齐（尺寸不同直接失败）、类值按 `GDT_Int32` 读（浮点类别栅格会被截断）、
      单类输入下 `1-Pe=0` 未特判。该动作现有 5 条用例（Execution / UsesStandardPaUa /
      KeepsZeroClassWhenNoDataUndefined / ExcludesDeclaredNoDataPixel / HandlesClassPresentOnlyOnOneSide），
      文档里每一条都可回溯到用例或代码行。
    - **C4b**：守卫落成三件 —— 判据本体 `tests/check_docs_match_cli.py`、载体
      `tests/check_docs_match_cli.cmake`、豁免与人工映射 `tests/docs_sync_allowlist.json`
      （8 条动作名↔文件名同义 + 6 条覆盖 D9 那 7 处判定，**逐条带 evidence**）。注册为用例
      `docs_match_cli`，**双树现读 Debug 750 / Release 748**（比 C3k-3 的 749/747 各 +1）。
    - **三条硬设计**：(1) **豁免用不上也算失败**（`DEAD-ALLOWLIST` / `DEAD-OVERRIDE`），防止"漂移修好了、
      豁免还留着"把下一处真漂移吃掉；(2) 判据自带 `--self-test` 合成自证（2 键 3 行的假文档，
      `count/label/required/default` 四类缺陷必须全点名，"好文档 0 漂移"的绿臂也必须成立）——
      **第一版夹具只埋 1 行，只能报出 2 类，自证当场把我的判据拦下来**（这正是"配不上的行不检查
      必填/默认值"的同一件事在夹具上的投影）；(3) Python 探测 = "跑起来并回显 `GIS_DOCS_PY_OK`"，
      缺失/二进制未构建一律 `FATAL_ERROR` 不 SKIP。
    - **编码口径**：Python stdout 只用 ASCII（`backslashreplace`），逐行原文写
      `build/{debug,release}/tests/docs_match_cli_report.txt`。实测经 cmake `message()` 转述会把
      UTF-8 再按 GBK 编一次 ⇒ `1 行没配上` 变 `1 琛屾病閰嶄笂`（与 B21 给乱码守卫定"stdout 只用 ASCII"
      同一条本机限制）。
    - **能红的证据**：把 `地形分析/坡度.md` 的 `Z因子` 默认值 `1.0` 注成 `9.9` ⇒ 用例红、报告首行
      `DRIFT terrain.slope 键 z_factor 默认值：文档=9.9 二进制=1`、`drift=1`；`cp` 还原后
      `git hash-object` 与注入前逐字节相同（`306c128a405d9deec3be2260d6d3077700ef37f2`）⇒ 再绿。
    - **同轮现读**：`docs_match_cli` 两配置 1.10 s / 0.74 s（TIMEOUT 走 300 的 blanket ⇒ 余量 >200×）；
      判据现值 `actions=126 docs_checked=126 missing_doc=0 rows=594 matched=593 drift=0 allowed=7
      dead_allowlist=0 dead_override=0 overrides=8`；`source_mojibake_guard` 扩到新文件后
      **311 文件 0 违例**（上一档 309，+2 = 新增的 `.cmake` 与 `.json`）；`data/` 未被写入；跑动前硬门控 = 0。
    - ⚠ **未验收**：本片段只单跑了 `docs_match_cli` 与乱码守卫，**没重跑双树全量**（注册数从 749/747
      变 750/748，全量绿与墙钟要在阶段三收口时现读）；正文级（非参数表）逐篇对撞（C4d）与
      `类型`/`说明`/`Options:` 三个维度仍未进判据；`*.py`/`*.md` 仍不在乱码守卫扩展名清单里。
  - **批次 C C4d 第一轮（正文级漂移对撞：28 篇射程、两席取证 + 控制器逐条复验，2026-10-26）**：
    - **做法**：两席各读 14 篇动作的**完整 `doXxx` 函数体及其 helper**（非 vector / vector 两组），
      每篇报告"读了哪段行区间 + 每条发现的三件套（文档原文行号、代码行、结论）"；
      控制器不采信任何未亲验的条目 —— **本轮 22 条改掉的都是我自己对回代码行之后才落笔的**。
      两席共报 52 条（A 38 / B 14），剩余 30 条如实留在报告里未处理（清单里逐插件列了数）。
    - **改掉的 8 篇**：`栅格切片`(8) `并集`(2) `矢量裁切`(2) `修复`(1) `空间连接`(2) `填挖方`(2) `库容计算`(2) `边缘检测`(3)。
      代表性的三条都是"参数核对永远抓不到"的类别：tile 正文还在讲"每层级 GDALTranslate 降采样"而该步骤
      已被 R-T17-7 删除（doTile 段内 `GDALTranslate` 现读 **0** 次）；`edge` 的 `scharr` 要求
      `sobel_dx`/`sobel_dy` 一正一零、而两者默认都是 1 ⇒ 不改参数直接选就必然失败，正文一字未提；
      `union`/`clip` 正文写"修复失败则整条失败"，代码是**逐条静默丢弃且不计数**（只有全表空才失败）。
    - **新挂账**：**N148**（叠加类对非法输入几何逐条静默丢弃、无计数 ⇒ 产物可"少一块"而元数据无痕）、
      **N149**（`repair` 把 EMPTY 几何原样写出并计入 `repaired_count`，两计数相加仍自洽）、
      **N150**（`spatial_join` 的"不自计"用 `fs::equivalent` 判定 ⇒ 换路径拼法就在自计/不自计间跳变；
      `join_value` 宽 254 是否被驱动截断本轮未取证）。文档一律**按真实行为**改写，代码未动。
    - **我这轮被下级否证的三处凭空（记进清单防复发）**：工单里写了不存在的动作 `processing.scharr`
      （scharr 只是 `edge_method` 枚举值，正确条目 `processing.edge`）、不存在的文档路径
      `栅格工具/邻近分析.md`（真实篇名 `欧氏距离.md`），以及"C3c 给 `info`/`repair` 补了 `feature_count`"
      —— 现读 `doInfo` 一条 metadata 都不发、`doRepair` 发的是 `repaired_count`/`skipped_count` ⇒
      **是我登记口径错，不是文档错**。
    - **待裁定的两条**：`fill_volume`/`cut_volume` 的命名方向与土方工程惯例相反（现状高于参考=填方），
      代码与文档一致、无漂移，是否算缺陷要产品裁定；"输出不得等于任一输入"这道硬闸在 14 篇双输入正文里
      一篇都没提，本轮按整族只记一次，摊开补写留给下一轮。
    - ⚠ **未验收**：C4d 只做完 22/52，余 30 条未处理；正文级对撞**没有机器判据**（`docs_match_cli` 只管参数表），
      这一类漂移目前仍靠人跑；本片段没重跑任何构建或全量（纯文档 + 台账，产品二进制不变）。

### `ctest -j` 并行：现在开会得到假失败

`ctest -j 8` 的 Debug 全量实测出现 **3 条 `Timeout`**：`gui_vector_buffer_geographic_failure_offscreen`、
`gui_spindex_ndmi_offscreen`、`gui_vector_singlepart_offscreen`；这三条**单跑 1.65 s / 1.92 s 通过**，
超时线是 `GIS_TEST_TIMEOUT = 300` ⇒ **纯属争用**（当时还叠着另一棵树的并发构建），不是回归。
⇒ **在共享状态清单做完之前不要开 `-j`**；GUI offscreen 用例对负载最敏感。
判据：**报 `Timeout` 先单跑那几条**，秒过即判"非回归"。跑全量期间**任何一棵树都不许有并发构建**。

**开 `-j` 前必须改什么**（21-D 逐条定判；口径 = 同一字面文件名出现在多个 TEST 块，再看它们是否落在同一条路径上）：

| # | 共享字面名 | 涉及用例块 | 是否同一路径 | 定判 | 开 `-j` 前要改的 |
|---|---|---|---|---|---|
| 1 | `metadata.json`（5 个用例块 + 1 个共用读取 helper） | `TileMetadataBoundsKeepTraditionalLonLatOrder`、`TileUserExtentDrivesMetricProjectionAndSkipsZeroAreaTiles`、`TileSchemeTmsInvertsWrittenRowNumberButKeepsTileSetSize`、`TileStretchIsWholeSceneWideNotPerTile`、`TileBlankGuardsAndSkipBlankCombination` | **否** —— 各写自己的子目录：`t17_bounds_output`、`t17_extent_far_output`、`t17_tms_xyz_output`/`t17_tms_tms_output`、`t18_stretch_output`、`t18_blank_<a\|n><s\|k>` | 不冲突 | 无需改 |
| 2 | `t17_profile_input.tif`（2 块） | `TileRejectsGeodeticProfileAndNamesIt`、`TileRejectsRasterProfileAndNamesIt` | **是** —— 两块都写 `getTestDir()/t17_profile_input.tif`（内容逐字段相同） | **真冲突** | 各用例改用各自的输入名（或挪进各自子目录）；GDAL 的 `.aux.xml` 边车随同一根因一起消失 |
| 3 | `e2e_spatial_join_output.gpkg`（2 块） | `VectorSpatialJoinRejectsMismatchedSrs`、`VectorSpatialJoinWritesJoinCountAndValue` | **是** —— 同一个 `getTestDir()/` 平铺路径（两块的输入靠 `.shp` / `.gpkg` 扩展名区分，不冲突） | **真冲突**（既知） | 输出名按用例分开；顺带把这一族平铺命名统一成 `用例名/` 前缀 |
| 4 | `10/512/512.png` 一类的瓦片路径（3 块） | 同 #1 的前三块 | **否** —— `z/x/y` 只是相对路径，父目录已按 #1 分开 | 不冲突 | 无需改 |
| 5 | `t17ScanWarpedLeftovers`（扫系统临时目录） | `TileCleansWarpedTempOnCancel`、`TileCleansWarpedTempOnWarpFailure` | 目录相同，但**文件名按 pid 隔离**（现为 `gis_tile_warped_<CPLGetPID()>_<seq>.tif`：pid 分开进程、进程内再加一个静态原子序号分开先后调用，批次 B 的 m-4；`TileCleansWarpedTempOnCancel` 现场抓到的实例名 `gis_tile_warped_12212_1.tif`。判据认两种形态，旧 `<pid>.tif` 也算本进程的）；Task 18 F3 已把判据从"扫全目录"收窄到"只扫本进程那一份" | 不冲突 | 无需改；历史残留（例如 `build/gis_tile_warped_17548.tif`、`…_28940.tif`）归脚本/人工巡检，不属用例间冲突 |
| 6 | `.aux.xml`（`tests/` 现跑 43 处 = `test_plugins.cpp` 40 + 3 个 GUI cmake 脚本） | 多处 | —— | GDAL 边车，**不算用例间冲突** | 无需单独立项 |

⇒ 净结论：**只有 #2 与 #3 是真冲突**（同一 `getTestDir()/` 平铺路径被两个用例块写），把它们分开 + 保证不并发构建，
才谈得上开 `-j`；其余命中都是"同名字符串出现在不同路径"的假阳性。

### 批次 B 待办（本批明确不修，登记在此以免丢失）

- **N81**：多波段且**各波段洞位置不同**时，`add_alpha` 仍会改变色波段数值（`-dstalpha` 把被遮像元写成 0）；**单波段源不受影响**。
- **N86**（内部债，不进用户可见发布说明）：包内容完整度无人自动断言 —— `Unresolved` 只 WARNING；
  复审实测删掉包内 `bin/sqldrivers/qsqlite.dll` 时 Verify 仍然绿。
- **N88**（内部债，不进用户可见发布说明）：~~`cmake/gis_runtime_dlls.cmake:1-77` 构建侧存在两份手抄 DLL 名单~~
  —— **已还**（`e726710` 加单源表与派生、`bcfb5cd` 删掉两条手抄名单）。它剩下的那一半"样式标错无人判"另立 **N105**。
- **N104（审计覆盖面，B6 取证时实测出、终审席 2 独立复现并把范围算准）**：
  `gis_copy_minimal_runtime` 一共往 **5 个**目录写运行库——`${CMAKE_BINARY_DIR}/plugins`、
  gis-cli 的 exe 目录、gis-cli 的 `exe/plugins` 子目录、gis-gui 的 exe 目录、gis_tests 的 exe 目录
  （调用点：`src/cli/CMakeLists.txt:20-21`、`src/gui/CMakeLists.txt:93`、`tests/CMakeLists.txt:150`、
  根 `CMakeLists.txt:310`），而 `runtime_dll_cross_config_audit` 的 `file(GLOB "${_dir}/*.dll")`
  **只扫其中 2 个**且不递归 ⇒ 有 3 个写入目标不在射程。现读（`python` 递归比 vcpkg 两侧目录）：
  Debug 树 `src/cli/Debug/plugins` 有 **46 个**只属 Release 命名的 dll（Qt6*/gdal.dll/opencv_*/proj_9.dll…），
  Release 树同形目录有 **45 个**只属 Debug 命名的 dll；另有 13 个插件各自的输出目录里各 1 个 `minizip.dll`
  （mtime 2026-05-22，属历史残留、当前构建不再产生）。把该目录喂给**同一条规则**立刻红
  （`cmake -DPLUGINS_DIR=<该目录> -P tests/check_runtime_dll_config.cmake` ⇒ rc=1，报"跨配置残留=46"）
  ⇒ 规则本身没问题，**是覆盖面窄**；先前"构树残留已清零"这个结论只对那两个受检目录成立。
  修法（未做，需要单独一轮）：审计改成从 `${CMAKE_BINARY_DIR}` 递归取目录、并显式排除 `tmp/`（测试耗材树，
  里面装的是对面配置的包），清理现存残留后用"植入⇒红／还原⇒绿"复验。
- **N105（样式标错无定向守卫，B6 取证时实测出）**：单源表里把某项的样式标错（实测把
  `geotiff.dll|underscore_d` 改成 `suffix_d`）后，配置期四条自守（格式 / 归零 / 派生项数 / 重名）
  **全部放行**，配置照常绿；只有那行 `[runtime-dlls] 单源表 36 项（suffix_d=25 underscore_d=1 same=10）`
  的分布数字变了，而没人断言它。为什么闭包也看不见：`GIS_RUNTIME_DLLS*` 只被
  `gis_copy_minimal_runtime` 消费（根 `CMakeLists.txt` 里 0 处引用），**装出来的包**由扫描器按导入闭包
  直接从 vcpkg 取，与这份名单无关 ⇒ 样式标错的后果只落在构树。而构树少一个运行库 dll 的实测形态是
  **用例 Timeout 挂住**（本机把 `gdald.dll` 从加载目录与 exe 目录各删一份 ⇒ `CoreTest.InitGDAL…`
  跑到 300 s 超时被杀，不是干净红）。修法（未做）：配置期对派生结果做**存在性断言**——
  release 名单逐项 `EXISTS ${GIS_VCPKG_RELEASE_BIN_DIR}/<名>`、debug 名单逐项查 `debug/bin`，
  缺项就 `FATAL_ERROR` 点名（名单是派生的、不是手抄，所以这条闸本身不引入新的真源）。
- **N90**（~~可被注释满足~~ **已销**，批次 B 的 B8 三连 `fb26cd7` → `57fcf3a` → `65df19e`）：
  判据现读 `blanked` 视图，注释与字面量内容都不再能充当数值断言；口径与四条机器判据见上面"GOLD 覆盖数的两条限定"。
  ⚠ 本条销的是"注释能顶替断言"这一型，**不等于 GOLD 已免疫自指期望**——那是 N108，仍未销。
- **N108（终审席 3 H1，静态取证，未实测红）**：数值断言的判据只认断言宏名，所以
  "期望侧的值取自被测产物"这一型（批次 A 之后、B11 刚修过的那类自指）**没有任何一臂在守**。
  评审给出的静态变异是把 `tests/test_plugins.cpp:8656` 的字面量参数改回
  `readRasterPixel(output, 7, 4)` ⇒ 判据仍判它"有数值断言"。⇒ 修法需要第三臂"期望侧不得含读产物调用"，
  且必须避开会把"用夹具算期望"的正常写法误判（"期望侧必须含字面量"那版写法实测会误降 16 行，已排除）。
  ⚠ 本条**未做变异实验**（评审席按任务书只读、未跑测试），落笔前要先跑一次注入确认。
- **N109（终审席 1 A2，静态取证）**：反向参数守卫只算 `白名单 − 实读集`（多列的键会红），
  **不判 `实读集 − 白名单`** ⇒ "有意藏键"与"抄漏一个键"不可区分。HEAD 上就有一个有意实例：
  `cutting.tile` 的 `resample` 被 `doTile` 读取却故意不进面板（进来会让未改动提交实发 spec 默认 `nearest`，
  改变既有产物 ⇒ 见 R-B20 与本文件"命令行拒绝未声明键"那节）。⇒ 修法不是加一条对称判据，
  而是加一张**带理由的豁免表**并把"表外即红"钉住。
- **N110（终审席 4 F1，静态取证）**：`copyMissingKeys` 缺"旧落点打开成功但 0 键"这一格的用例，
  于是"干净机器不该每次启动重扫真实旧落点"这条语义无人判（`app_settings.h` 的声明与实现都承认这个形状）。
  评审的推演是"把 `if (opened == 0)` 改成 `if (copied == 0)`，17 条用例逐条静态核对都还绿"——
  ⚠ 这是**静态核对不是实测**（该席按任务书未跑测试），落地前先跑一次注入确认。
- **N111（终审席 4 F2，静态取证）**：GUI 侧 `execute_worker.cpp` 用 `threwException` 决定要不要加
  `Exception: ` 前缀，这条分流**无判据**（把条件换成 `if (!result.success)` 没有用例会红），
  而"绝大多数失败是插件自己报的"这一类用户可见文案正是它在管。对照：工作流侧同一区分**有**判据
  （`tests/test_framework.cpp` 断插件自报失败必须走 `failed:` 而非 `threw:`），CLI 侧是**明写的合流**
  （同文案同 rc）⇒ 所以这不是"全网都缺"，是 GUI 半边单独缺。
- **`ctest -j` 并行安全改造**：见上一节，先改冲突用例再开并行。

---

## 批次 B 发布说明（用户可见变更，2026-10-19 起）

### 用户设置存储位置：Windows 注册表 → ini 文件

用户设置存储位置从 Windows 注册表（`HKCU\Software\GISTool`、`HKCU\Software\GIS\GIS Tool`）改为
`%LOCALAPPDATA%\GIS\GIS Tool\settings.ini`；首次启动自动**补缺不覆盖**地导入旧值，
旧注册表键不会被删除或改写；卸载程序不清理该 ini（避免误删未迁移用户的设置）。

- "补缺不覆盖"指：只把 ini 里**还没有**的键从旧落点搬过来，已经在 ini 里写过的值不会被注册表的历史值盖掉；
  导入只做一次，靠 ini 内的 `meta/migrated_from_registry` 标记记账（标记写在 ini 而不是注册表，
  所以卸载器清掉注册表也不会让它"再导一遍"）。
- 迁移是**只读**地访问旧注册表键：程序不会 `reg delete`、不会改写任何既有的值。
- **但"只读"也可能留下一个空键壳（N103）**：首启迁移要打开旧落点才能读它，而 Windows 上 `QSettings`
  在 NativeFormat 下**光是被构造出来**就会把 `HKCU\Software\GISTool\GISTool` 这一层键建出来，
  公开 Qt API 没有"只探测存不存在、不创建"的写法。留下的壳**恒为空**（0 值 0 子键）：
  没有任何用户数据被写进那里，既有值也没被改写，卸载器不删它、下次启动也不会往里写。
- ⇒ 于是"有没有污染用户状态"的判据口径是：**看那把键下的 values 是否为空，不看键是否存在**
  （键不存在才是需要机器历史才能满足的条件，它不该当判据；本仓取证一律用 values 计数 +
  `HKCU\Software\GIS\GIS Tool\app\first_run_complete` 未被改动 + `%LOCALAPPDATA%\GIS`、
  `%LOCALAPPDATA%\gis_tests` 不存在这三条）。
- **环境变量 `GIS_TOOL_SETTINGS_INI` 残留会整体改写设置落点（N103 补充）**：该变量只要非空（**含只有空格**），
  `appSettingsPath()` 就把它当作设置文件路径（`src/gui/app_settings.cpp:214-225`）⇒ 整个设置库的读写都落在那个文件，
  用户真实的 `%LOCALAPPDATA%\GIS\GIS Tool\settings.ini` 不再被使用；同时旧注册表迁移被门控**整段跳过**、
  也不写一次性标记（`:190-198`、`:180-187`）⇒ 每次启动都会重试迁移，且**没有任何诊断输出**。
  这是已知且有意不在拒绝层加日志的行为：那两条生产臂每轮回归都会被正常触发两次，加日志等于恒假警报；
  要加只能加在门控层。只有变量指向瞬时路径时才会表现为"每次启动都是新设置库"，指向固定文件时设置会持久化在那里。
- 测试与 CI 不落在这个位置：每条 GUI 用例另有自己私有的 ini（在它的产物目录里），
  回归跑完不会往用户配置里写东西（N101）。

### Debug 配置的安装产物补齐（N85，B4）

`cmake --install build/debug --config Debug --prefix …` 现在会装出**起得来**的 Debug 包：`bin/platforms/` 含
`qwindowsd.dll` 与 headless 插件 `qoffscreend.dll`，`bin/sqldrivers/` 含 `qsqlited.dll`
（此前这三条规则只随 Release 安装，Debug 包 rc=0 却是残缺的——装出来的 Debug GUI 连平台插件都没有，
offscreen self-test 起不了进程：改前后两次跑都只观测到 stdout 为 0 字节，而"0 字节"对
"挂在缺插件弹窗上"与"被别的早退拦下"**没有鉴别力**，所以**机制是推断、不是观测**）。
Release 包的**文件名集合逐条不变**（名字级 diff；未做内容逐字节比对）。
**交付形态仍是 Release**；本条只消掉"Debug 装出残包还静默成功"这一类风险。判据命令见"发布前验证 → 第 3 步"。

### 批次 B 变更明细（用户可见变更与已知边界）

本节数字均为 2026-10-21 定稿时现读/现跑；设置落点与 `GIS_TOOL_SETTINGS_INI` 残留（N103）见上一节，
口径与复算命令见 [specs/2026-10-21-批次B收尾口径与决策稿.md](superpowers/specs/2026-10-21-批次B收尾口径与决策稿.md)。

**参数面板按动作收窄（39 个动作）**

`vector` / `processing` / `classification` 中此前没有逐动作面板配置的 **39 个动作**
（vector 30 / processing 5 / classification 4）现在各有自己的 `visibleKeys` 白名单：
面板只显示该动作**实际读取**的键，不再把插件级 `paramSpecs` 全集铺出来。
其中 vector 30 个动作的逐键账是 **30 × 19 = 570 = 102 个保留 + 468 个不再显示**。
消失的键按风险分三级：**A 类 1 个 / B 类 29 个动作 / C 类 0**。唯一的 A 类是
`vector.info`（矢量信息）的"输出文件"框：spec 把它标成必填，但 `doInfo` 只读 `input`/`layer`、
报告写文本，从不读 `output`，去掉后产物不变。其余 29 个动作消失的键都是该动作从不读的死键
（判据：30 条动作的实读键集与消失键集交集为空）；判据强度如实写明——468 个键里
**120 个有源码级证据**，其余 **348 个**只由"消失键 ∩ trace 实读集 = ∅"支撑，依赖扫描器而非独立证明。
另有 **14 处必填加严**（判据是 `required = spec.required || 面板 requiredKeys`，
所以"加严"= 面板列了、而 spec 标 `required=false` 的键）：
`difference` / `intersect` / `union` 的 `overlay_vector`、`nearest` 的 `nearest_vector`、
`spatial_join` 的 `join_vector`（vector 5 处）；`pansharpen` 的 `pan_file`（processing 1 处）；
`svm_classify` / `random_forest_classify` / `max_likelihood_classify` 各自的 `input` + `training_csv`、
`accuracy_assessment` 的 `classified_raster` + `reference_raster`（classification 8 处）。
这些键插件对空值直接失败，面板现在按必填处理（加严，不是放宽）。
⚠ 这一条本批第一版发布说明只写了"5 处"（只数了 vector 那一族），漏报 processing 与 classification
的 9 处 ⇒ 由终审复算纠正；复算口径见上一条里的判据式子。
同一批还为这 39 个动作**新写了动作级"功能说明"文案**（此前面板显示的是插件级说明，
`buildActionSelectionText` 里"动作说明为空才回退插件说明"那条分支）⇒ 也是用户可见的文案变化，只是不改产物。
命令行与工作流的参数键不受影响（收窄的只是 GUI 面板显示）。

**工作流占位符：未解析即显式失败（含字面量边界）**

参数值里形如 `${名字}`（名字限 `[A-Za-z0-9_.-]`）且上下文没有同名变量时，步骤在派发前失败并点名：
`Step n (plugin.action) placeholder not resolved: <名字> (parameter <键>), ...`，不再把 `${...}`
原样当路径交给动作（旧形态会报"打不开文件 `${input_before}`"这种 I/O 假象）。
边界必须写明：**判定只看替换后的结果串，凡是 `${name}` 形态都会被当占位符**——即使用户想写的是
字面量（例如 `.../literal_${raw}.tif`），也会收到 `placeholder not resolved: raw`；要字面量请改名，
或等"转义/字面量"支持（已登记为后续项）。反过来，`${a b}`（含空格）与不配对的 `${` 仍按普通字面量透传。
内置预设 `remoteSensingPreprocess` 的两个错 actionKey 同批修好
（`georef.radiometric` → `radiometric_calibration`、`georef.dos` → `dos_correction`）：
该预设此前两步 `Unknown action`、第三步非法组合，按定义跑不通；改后这三步的键都真实存在
（派发校验层面：前两步放行到插件，缺输入时才报 `Cannot open raster`；第三步 `cutting.clip`
仍要求 `extent` 或 `cutline`）——修的是"键根本不存在"，不是让预设免填参数。
注意 GUI 工作流页只取预设、不填参数，内置预设仍会先撞上面这条未解析占位符错误（B13），
只是失败原因从"I/O 打不开文件"变成"占位符没给值"。
`changeDetection` 等内置预设的失败形态同样从"打不开文件 `${input_before}`"变成这条明确原因。

**命令行拒绝未声明键；tile 的 `--resample=` 开始生效（B14 与 B16 必须同版）**

这两条必须一起发布。B14 之后，`gis-cli` 对**未声明**的参数键报错并点名（`rc=1`，如
`Error: unknown parameter "k" (plugin processing, action kmeans_segment); ...`，大小写敏感、不做别名）；
`cutting.tile` 的 warp 插值此前唯一真正生效的拼写是**未声明**的 `--resampling`，它从此会被 CLI 拒绝；
B16 让 tile 改读已声明的 `--resample` ⇒ **旧拼写被拒 + 新拼写生效**是同一版的两半
（只发 B14 不发 B16 的中间态最糟：原来能用的键被拒、替代品还不生效）。
tile 不传 `resample` 时仍用插件内部默认 **`average`**（与 spec 声明的默认 `nearest` 不同，
但与改前内联默认相同；用例钉住"不传"与"显式 `resample=average`"的产物逐字节相同）；
原来显式传 `--resampling` 的老脚本则会收到上述未声明键错误，属破坏性变化。
GUI 面板**不显示**该键——显示它会让"没改过就提交"也实发 `nearest`，把默认产物改掉。
**工作流**里未命中的键仍会透传给插件、由插件按默认值跑（不报错）。这是明写的范围边界、不是遗漏：
把工作流也收紧会改变已有工作流文件的语义，属语义变更，待拍板。

**插件异常与工作流取消（B15）**

GUI / CLI / 工作流三侧的 `plugin->execute` 现在统一经一个 `executeGuarded` 兜获，捕获顺序固定为
`CancelledException` → `std::exception` → `...`：

- **`std::exception` 之外的抛出不再终止进程**：此前 CLI 与工作流没有 `catch (...)`，非 std 派生的抛出
  （实测 `throw 42;`）会走 `std::terminate`（Release 实测 rc=0xC0000409、无输出；Debug 停在 abort 对话框）；
  现在统一转成失败消息（`Unknown exception (non-std)`），CLI 以 `rc=1` 结束。
- **工作流的"用户取消"从失败变取消**：`WorkflowResult` 新增 `isCancelled`（默认 false，
  `src/framework/include/gis/framework/workflow.h:37`），`CancelledException` 与插件自己返回的
  `cancelled()` 不再写成 `Step n (plugin.action) threw: ...` 的失败，而是 `cancelled: ...` 且
  `isCancelled=true`。但 GUI 的**工作流页**目前既不提供取消按钮、也不读这个标志
  （单动作执行页走 `Result::isCancelled`，不受影响）⇒ 今天没有用户可达的"取消工作流"路径；
  将来若加取消按钮，取消会被显示成失败（已登记，届时同批补显示分支）。

**文案乱码清理（B17）**

三批共修 **31 行**中文文案：
**13 行**是可 GBK roundtrip 还原的双重编码（分类插件 10 行 + 测试支撑 3 行，`cd774e4`），
**15 行**是有损的私用区乱码 U+E000–U+F8FF（分类插件 14 行 + GUI 主程序 1 行，`fe0a1b2`），
**3 行**是终审复查时才发现的第三种形状（分类插件 `:229/:234/:257`，不可 roundtrip 还原、
也不在私用区：损坏尾部落在注音 U+3105–U+312F、排版形式 U+FE30–U+FE4F、全角货币 U+FFE0–U+FFE6 与 € U+20AC，
按 `e6b7f8b` 的历史原文逐行重建）。三批互不重叠，合计 31 行。

⚠ 这条要说清**判据为什么曾经漏掉那 3 行**（否则读者会以为"检测器归零"等于"清干净了"）：
最初的检测器只有"GBK roundtrip 成功"一条规则 ⇒ 漏掉一切不可还原的形态；补上私用区规则后
本批仍报 0，而这 3 行**在工单 Step 1 的核对清单里被点过名**（它们的孪生行 `:210/:215/:251` 被修了，
`:229/:234/:257` 没修）。⇒ 归零类验收的正确姿势是**先证规则覆盖该缺陷族的形状**，
不是跑一遍看它返回 0；现在的规则是 4 条（roundtrip、私用区、注音/假名异物、排版形式/货币/替换符）。
复算口径：`PYTHONIOENCODING=utf-8 python build/tmp/b20v_b17_detector.py`（现读 283 个受控源文件、命中 0 行）。
**检测器本身仍未落库、未注册成 ctest**（待拍板项见下一节）。

**内部项与待拍板（不进用户版）**

- `tests/test_gui_support.cpp` 的 `kMustBeConfigured` 仍是 13 个产品插件的手抄名单；建议改成
  "对所有加载到的插件执法 + 与 `cmake/PluginTargets.cmake` 的 13 个 target 对撞"，本批未实施。
- 今天没有断言锁"spec `required=true` 的键必须保持可见"；这类收窄今后仍可能静默发生（判据缺口，已登记）。
- **待拍板项的裁定结果（2026-10-21 用户拍板）**：
  ① **B17 乱码检测器 ⇒ 已落地**为一条 ctest 用例 `source_mojibake_guard`（四规则全带：
  roundtrip、私用区 U+E000–U+F8FF、注音 U+3105–U+312F、排版形式/全角货币/替换符）。
  判据本体 `tests/check_source_mojibake.ps1`（R1 要 CP936 编解码器，CMake 没有 ⇒ 用 PowerShell 实现，
  stdout 刻意只用 ASCII，因为 PS 5.1 会把非 ASCII 输出渲染成乱码、恰恰在失败那一路读不出）；
  载体 `tests/check_source_mojibake.cmake`（解释器缺失 ⇒ `FATAL_ERROR`，**不 SKIP**，
  理由与 N84/N86 同：这条用例的"没检查"绝不能长得像"检查通过"）。
  **自带夹具自证**：`tests/mojibake_fixtures/mojibake_bad.txt` 必须命中全部 4 条规则、
  `mojibake_ok.txt`（14 行从本仓真实中文文案里抽）必须 0 命中 ⇒ 规则退化或过宽都会先红在这里。
  取证三段：基线两棵树各 1/1 通过（11.75 s / 13.36 s）⇒ 往 `src/gui/main.cpp` 与
  `src/gui/app_settings.cpp` 植入一行乱码 ⇒ 用例 Failed（`MOJIBAKE R1+R2+R3+R4 <路径>:<行号>`）
  ⇒ `cp` 还原并 `cmp` 逐字节相同 ⇒ 回到 1/1 通过。
  ⚠ **扫描范围刻意不含 `docs/`**：文档会合法地引用这些码点作为证据与说明——本文件第 511 行就写着
  "与 € U+20AC"来解释规则本身，纳入扫描等于让解释文本被判成缺陷（决策稿 §6 原写"扫 src/tests/docs"，
  现按这个事实收窄为 `src` `tests` `cmake` `.github` + 根 `CMakeLists.txt`/`CMakePresets.json`/`vcpkg.json`）。
  ② **`.gitattributes` 与去 BOM ⇒ 裁定不做**，保持现状（索引侧 0 变化但下次检出会让 74 个工作区文件
  换行尾；影响面非 0 而收益只在"换机器克隆"这一侧）。8 个 BOM 文件与行尾混用的现状不变，
  复算口径仍留在决策稿 §5。

### 收尾二轮（2026-10-21）：真实矢量回归暴露的四条判据缺陷

`real_vector_regression` 当时是 `add_custom_target`，**不在 ctest 的 693/691 里** ⇒ 只有
`cmake --build build/release --config Release --target real_vector_regression` 会跑它
（**本批 Task C2 已把四条腿纳入 ctest**，用例名 `real_data_*` ⇒ 判据再被改坏就是全量当场红/跳，
销账记录见本节末尾 N112 那一条）。
本轮按"各功能要能真出结果"的要求首跑这条链路（输入是 `data/vector/北京市_道路.shp` 过滤后的 22836 要素），
一次暴露四处，且**全在判据侧、没有一处是产品算错**：

1. **期望值是合成样本标定的常量**：脚本自 `58158e0`（2026-04-27）起优先取 `data/vector` 的真实道路，
   而 clip / union / intersect / spatial_join / convert 的期望仍写死 `2`（那是 3 要素合成样本过滤后的数）。
   ⇒ 本轮首跑就红；不能说它曾绿，因为这条从来不是 ctest 用例。
   现改为**由输入图层自己的要素数推导**（`Get-LayerStats` 读 `vector info`），合成样本与真实数据同时成立。
2. **窗口退化**：`china_bbox` 恰好完全覆盖道路实测范围 `(115.4185,39.4437)-(117.4509,41.0580)`
   ⇒ clip 是恒等、difference 是空集，几何引擎一刀没切。现按范围生成三个窗口：外扩 20%（恒等关系）、
   整块挪到范围之外（clip/intersect 必须 0、difference 必须全留）、取中间 25%~75%（真会切，
   并钉住 `clip` 与 `intersect` 在同一窗口上报告的要素数必须相等）。
3. **一条假绿**：`buffer_quick` 的判据是 `Contains("feature_count: 2")`，真实输出是 `feature_count: 22836` ——
   `"feature_count: 2"` 恰是它的前缀，所以本轮它"通过"了，而它想钉的数字一个字都不对。现按整数相等判。
   ⚠ 与前几条同一根：**子串包含**做判据时，短数字会被长数字的前缀吃掉，且永远不会自己响。
4. **编码盲读**：`convert_roads_geojson` 用 `Get-Content -Raw | ConvertFrom-Json` 整读 12 MB 的 UTF-8 GeoJSON，
   PowerShell 5.1 默认按 ANSI 代码页解码 ⇒ 中文属性值被读坏，抛的是
   "JSON 语法无效 (position 230)" 这种**假产品缺陷**（实测该文件严格 UTF-8 可解、`次干道` 完好）。
   现不整读 JSON，改判"文件头是 FeatureCollection + 转出的图层能被读回同样的要素数"。

挂账 **N112**（**批次 C Task C2 已销**，2026-10-21）：这四条修复自身没有机器级兜底 —— 目标不在 ctest 里，
改完判据后是否还跑全靠人记得；把 `real_*_regression` 纳入 ctest（带数据缺失即 SKIP 的语义）留待批次 C 决定。
→ **销账内容**：`tests/CMakeLists.txt` 新增四条 ctest 用例 `real_data_raster` / `real_data_matching` /
`real_data_vector_quick` / `real_data_vector_full`，复用同一套 `tests/run_real_*.cmake` 包装
（六个 `add_custom_target` 原样保留做人工入口；用例名刻意与目标名不同 ⇒ `ctest -R` 不会把两种语义混进一次筛选）。
四条共用一把新锁 `RESOURCE_LOCK real_data_regression`（它们都写 <仓库根>/tmp/*_benchmark 与 *_regression），
各带按实测墙钟 ≥4× 向上取整到 60 s 的 `TIMEOUT`（60 / 60 / 720 / 780 s，三轮样本与算式写在该文件注释里，
不再沿用拍来的 900），并**恒注册**在 `if(GIS_POWERSHELL_EXECUTABLE)` 之外（解释器缺失由包装脚本判红，
不是"用例静默不注册"）。`data/` 缺失时三份包装在调 `.ps1` 之前打印 `GIS_SKIP_TESTDATA_ABSENT: data/ not present` 并 `return()`，
用例侧 `SKIP_REGULAR_EXPRESSION` 认领该 token ⇒ ctest 报 **Skipped** 而不是 Passed（裁定 R-C2a）。
  ⚠ 认领串已从通用前缀改成独占 token：CTest 的 Skip 匹配**优先于退出码**（隔离探针里"打标记后 FATAL_ERROR"
  仍报 Skipped 且 "100% tests passed"），所以"只允许三份包装打印"必须是**形状上不可能撞车**，不能只靠现状。
判别取证（控制器端到端复跑；取证本身不就地打开 `data/` 下的栅格，但 `data/` 里已有本轮早先探针留下的
15 个 `.aux.xml` 边车（见 N115），故此处不写"一字节未动"）：把 `run_real_matching_regression.cmake` 挂进临时最小工程
当真用例跑三臂 —— ① 根含 `tests/` 而无 `data/` ⇒ `***Skipped***` 并列入 "did not run"；
② **真实仓库根** + `-DMODE=zzz`（腿真失败）⇒ `***Failed***`；③ `-DPOWERSHELL_EXECUTABLE` 留空 ⇒ `***Failed***`。
⇒ Skip 只在命中那个独占 token 时发生，真失败仍然是红 ⇒ 用例不空转。
④ 本批加固：三份包装在 data 检查**之前**先验 `${WORKSPACE_ROOT}/tests/<对应 .ps1>` 存在，缺失即 `FATAL_ERROR`。
成因是控制器自己踩出来的：`add_test` 里把参数写成 `-DWORKSPACE_ROOT="${X}"` 时 Windows 的 argv 复原会把引号
留在**值内部**，于是 `data` 检查认不出路径 ⇒ 四条真用例整批静默 `***Skipped***` 而全量照绿。
⚠ **遗留（登记不修，归 C5 / C10 那类批次）**：本轮现读 `data/` 只有 `dem`、`image`、`vector` 三个子目录，
**没有** `data/raster` 与 `data/matching` ⇒ raster 与 matching 两条腿的"真实数据"取径一直在走
`tmp/*_benchmark/generated_data` 的合成兜底分支。这四条兜住的是"`data/` 整目录不在时不装绿"与
"判据本体每次全量都真跑"，**不等于**那两条腿判的是真图（要按真实图收紧判据，等 C1 矩阵排到那两个插件）。
挂账 **N117**（**2026-10-21 已修**，用户裁定"直接改，带前后对比验证后提交"）：

- 改的是什么：`src/plugins/vector/vector_plugin_interior_point.inc` 里 `interior_point` 原来调
  `OGRGeometry::Centroid()`（面积重心），与 `centroid` 动作**同一行实现**；现改走 GDAL 的
  `OGR_G_PointOnSurface`（GEOS point-on-surface），语义变成"保证落在几何内部的代表点"。
  没有保留"取不到点就退回 `Centroid()`"的兜底 —— 那等于把形外点这条后门留在原地。
- 前后对比（同一夹具、同一二进制新鲜度检查：`grep -c "error C"`=0 且 dll mtime 晚于源）：
  · 凹面夹具（两条宽 1 的臂组成的 L 形，面积 19）：改前输出 `(2.8684210526, 2.8684210526)`，
    `Contains()` 与 `IsPointOnSurface()` **双假**（用例红，见 `tests/test_plugins.cpp` 的
    `VectorInteriorPointExecution`）；改后落在形内，两条性质断言均绿。
    期望值由夹具形状手算（横臂 10×1 形心 (5,0.5) + 竖臂 1×9 形心 (0.5,5.5) ⇒ 54.5/19 = 2.8684），
    不从产物回填。改前那次红同时就是这条断言的 R-B16 变异取证（变异 = 上一版已发布实现）。
  · 凸/矩形夹具 `(0,0)-(10,0)-(10,4)-(0,4)`：改前改后**同为 (5,2)**（三种定义在此重合），
    用来证明没有把简单情形改坏。
  · 真实混合图层（`中国_省.geojson` 的副本，42 要素 = 34 面 + 8 线）：改前 `feature_count: 42 /
    skipped_count: 0`，改后 **42 / 0 两棵构树各自复现** ⇒ 没有把线要素改写成跳过（这正是选
    `OGR_G_PointOnSurface` 而不是 `OGRSurface::PointOnSurface` 的原因）。
  · 纯线图层（`bj_roads_core_3857.gpkg`）：改前改后都是显式 `Error: Interior point currently only
    supports polygon layers` ⇒ 该动作的适用面没变。
- **用户可见行为变更**：凹面/多部件要素上，本动作输出的点坐标会变（原来可能在形外，现在保证在形内）；
  面内点唯一的情形（凸面、矩形）不变。依赖旧坐标的下游需重跑。
- 为什么先前用 `reinterpret_cast` 而不是直接写 `normalized->PointOnSurface(&point)`：本仓 GDAL 3.12 的
  `OGRGeometry` **基类没有**该虚函数（它在 `OGRSurface` 上，`ogr_geometry.h:2385`），且 `OGRGeometryH`
  是 `void*` —— 隐式转换在 Debug 配置报 `C2664`。GDAL 自己的 `FROM_H/TO_H` 也是显式转换。

挂账 **N119**（未解释，登记不修）：同一份 `vector_plugin_interior_point.inc` 的隐式句柄转换，
**Release 配置编译通过、Debug 配置报 4 个 `error C`**（两棵树的 `plugin_vector.vcxproj`
预处理定义与 include 路径逐字比对无差异）。强制重编 Release 亦 0 error ⇒ 不是"没编译"。
我没有这个分歧的因果，只把代码改成配置无关的显式 `reinterpret_cast` 消除依赖；
若以后再出现"一个配置编得过一个编不过"，要先查构树差异再改代码。

挂账 **N118**（C3a-3a 的负对照实测，属判据强度问题、不是登记造假）：
`vector.convex_hull` / `vector.envelope` / `vector.boundary` 三条 GOLD 的数值判据都是**转置不变量**
（只比 `get_Area()` / `get_Length()`）：把凸包顶点整体 x↔y 转置后三条用例**全绿**（且编译真过、
dll 比源新）⇒ 这类判据抓不到"坐标轴互换"。另有夹具盲区：三条夹具各 1 要素 ⇒"只处理第一个要素"
"要素数算错"不可见；`boundary` 夹具是 10×10 正方形 ⇒ 转置与起点选择都不可见。
⇒ 需要一轮"补钉坐标断言"的小批（不是重做取证）；补断言时注意 `get_Length()` 不隐式闭合而
`get_Area()` 隐式闭合，同一族"丢收尾点"变异在两条判据上的可见性相反，配方别照抄。

**N118 处置进度（2026-10-22，按可证部分收，不整体销账）**：
- **断言面**（提交 `cf50a39`）：`convex_hull` / `envelope` / `boundary` / `midpoints_extract` 四条的夹具
  改成**两要素非对称 L**（A = `(0,0)-(12,0)-(12,3)-(4,3)-(4,8)-(0,8)`，其点集关于 y=x 转置后不等于自身；
  B = `(20,0)-(24,0)-(24,2)-(20,2)`，包框与 A 不相交），并补**顶点集合**（先去重再断基数、基数不等立刻红，
  按集合不钉顺序 —— GEOS 给的环起点与绕向是实现细节）、**包框四极值**、`feature_count` + 落盘
  `GetFeatureCount()` + 第 2 要素在场三处计数腿。期望值全部由夹具算出（shoelace 与半分位算式见工单 C3a-4 小节）。
- **判别面**（C3a-5a/5b 两刀，同一变异装旧臂 `32fafe7` 与新臂 `cf50a39` 各跑一遍）：**8 处注入全部
  呈"旧臂 8/8 绿 / 新臂红"，无一条例外**，且两臂共用同一份变异 DLL（唯一变量是测试文件）。
  实测坐实的两条：① 反射变异下**面积 96.0/8.0、周长 40.0/12.0、`get_IsClosed()` 全都不红**，
  红的只有带方向的顶点与极值 ⇒ "反射不变量抓不到坐标写反"从推断升级为观测；
  ② "只处理第一个要素"类变异**只红计数腿**、端点平均类变异**只红坐标腿** ⇒ 两条腿缺一条必漏一整族缺陷。
- **仍缺**：`centroid` / `vertices_extract` / `endpoints_extract` / `interior_point` 四条**还是单要素夹具**
  ⇒ "只处理第一个要素"在那四条上依旧结构性不可见 ⇒ 新挂 **N120**（多要素夹具补齐，归 C3b 之后的 vector 顺手批）。
  故 N118 的正确说法是"**四条几何派生判别证据已闭合**"，不是"vector 判据强度问题已清零"。

挂账 **N120**（2026-10-22 由 C3a-5b 的边界如实登记引出）：`centroid` / `vertices_extract` /
`endpoints_extract` / `interior_point` 四条用例的夹具各 1 要素，且 `vertices_extract`/`endpoints_extract`
的期望按"第 i 个顶点"写死 ⇒ 顶点断言钉住了坐标但**没有覆盖多要素与跳过分支**。
复算口径：`grep -c "feature_count" ` 那四个用例块（现 `tests/test_plugins.cpp` 内）应随批从 `"1"` 变 `"≥2"`。
后果面：与 N118 同源的一族缺陷（循环里 `break`、聚合方向写反、跳过计数漏加）在这四条上测不出。

C3b 建账时的只读调研产出四条产品侧缺口（**本批只记账、不修**，修归 C3c 之后的产品批；
每条都带现读载体，不是"看起来像问题"）：

- 挂账 **N121**（注释与行为不一致）：`buildSliverFilteredGeometry`（`src/plugins/vector/vector_plugin_common.inc:718` 起）
  注释写 `Rebuild without interior rings that are too small (slivers)`，但循环体把 `getNumInteriorRings()` 的
  **所有内环原样 `addRing` 搬运**，没有任何面积判断 ⇒ 用户以为"细缝多边形里的洞也会被清理"，实际只丢外环面积 `< min_area` 的面。
  另：比较用的是 `<` 而非 `<=` ⇒ 面积**恰等于** `min_area` 的面不删，这条边界全仓无用例。
- 挂账 **N122**（**已复现的产品缺陷**，2026-10-22 探针；建账时我写的是"未探针、只登记现象"，那条说法已被实测推翻，此处更正）：
  `singlepart` / `sliver_remove` / `geom_metrics` / `nearest` 的扩展名判定与 `removeExistingVectorOutput`
  用**窄** `fs::path(output)`（`src/plugins/vector/vector_plugin_common.inc:299-300`），
  而 CSV 侧与 `ensureParentDirectoryForFile` 用 `fs::u8path`（`:311`）。
  Windows 上从 UTF-8 `std::string` 构造窄 `fs::path` 会按 **ANSI** 解码 ⇒ 含非 ASCII 的目录名在
  `fs::directory_iterator(parent_path())` 里找不到 ⇒ **"先删旧产物"这一步静默什么都没删**。
  五臂实测（都现跑，输入同一个 2 部件 `MultiPolygon`，输出 `build/tmp/n122/`）：
  ① ASCII 路径 + 预置非法同名文件 ⇒ `Success`；② 非 ASCII 路径 + 同一份非法同名文件 ⇒
  **`Error: Cannot create output file: build/tmp/n122/中文目录/bad2.gpkg`**；③ 重复 ② 仍失败（可复现，不是偶发）；
  ④ 非 ASCII + 先手工删掉该文件 ⇒ `Success`（⇒ 唯一障碍确实是"删不掉"）；
  ⑤ 非 ASCII + 已存在**合法** GPKG ⇒ `Success`（GPKG 驱动能就地覆盖 ⇒ **这就是它一直没被发现的原因**：
  只有"目标位置上躺着一个不是数据库的文件"时才暴露）。
  对照：同目录下 CSV 动作（走 `fs::u8path` + `ofstream` trunc）写 `r.csv` 正常 ⇒ 现象只属于那四个矢量化动作。
  后果面：用户把成果导到中文目录（很常见的习惯）时，若目标文件是上次别的东西留下的占位文件，
  动作直接失败而报错文案只说"Cannot create output file"，**不会提示是路径编码问题**。
  修法与门：把这几处 `fs::path(output)` 换成 `fs::u8path(output)`（共享 helper 被全部矢量化动作使用 ⇒ 属跨动作改动），
  并补一条机器级用例：在非 ASCII 目录里预置非法同名文件、跑一次、断言成功且产物可读。
  **本批不修**（正在跑的取证/晋升批共享同一棵树与同一套用例，改 `src/` 会污染它们 ⇒ 单独开一批带用例修）。
  **已修 `10fe182`（2026-10-23）**：共享 helper（17 个动作）+ 8 处内联复制全部改 `fs::u8path`；
  新用例当场又抓到**第二类同族缺陷**——`fs::path(output).extension().string()` 在中文路径上会抛
  `No mapping for the Unicode character exists in the target multi-byte code page`，24 处
  （vector 23 + projection 1）一并改 `fs::u8path(output).extension().u8string()`。
  判据 = `PluginTest.VectorRemoveStaleShapefileSidecarsUnderNonAsciiPath`（中文目录 + 驱动默认不重写的
  `.sbn/.sbx/.qix`；buffer 走 helper、simplify 走内联各判一次）；两臂：29 个文件退回窄串 ⇒ 用例红，
  按快照逐文件还原（md5 一致）⇒ 绿。曾加过一条 projection 侧同类用例，**两臂证明不承重**
  （GDAL 覆盖式建数据集自己会删同 stem 边车）⇒ 已删，缺口如实登记。
- 挂账 **N123**（判据覆盖面）：`topology_check` 的四类问题里 `empty_geometry` 与 `invalid_geometry` **无任何用例覆盖**
  （现读实现是两个独立 `if`、可同写两行；成对判定 `duplicate_geometry` 与 `overlap` 是 `if/else if`、互斥）。
  "自交面会不会同时被判 invalid"我**没有算式依据** ⇒ C3b-B1 明确不做这两行，等用 `IsValid()` 实测后再定期望值。
- 挂账 **N124**（可核对性缺口）：`overlap_check` / `topology_check` / `adjacency` 三个 O(n²) 动作对
  **空几何/非法几何静默丢弃且不计数**，metadata 只有 `relation_count` / `issue_count`，
  **没有任何"扫了多少要素"的键** ⇒ 用户无法从产物判断覆盖范围；`nearest` 同类（空源几何 `continue` 不计数，
  `feature_count` 可以小于输入要素数且差额无处可查），而它的 `nearest_vector` 键是**入参字符串回显**、不是目标要素数，极易读错。
- 挂账 **N125**（**产品缺陷，已由控制器独立复现**，2026-10-22 立案 ⇒ **同一日由工单 C3d 修复销账**，提交 `19cb39f`；修法与夹具见工单 C3d 与 `task-C3d-report.md`）：
  `overlap_check` 与 `topology_check` 对"这两个面是否重叠"给出**互相矛盾**的答案。
  同一个真实省界派生件（`tmp/vector_benchmark/c3ba/prov3857.gpkg`，复制到 `build/tmp/n125/` 后现跑）：
  `vector overlap_check` ⇒ `Success: Overlap check completed: 0 relations`、`relation_count: 0`、CSV 只剩表头；
  `vector topology_check` ⇒ `issue_count: 1`，行是 **`12,14,overlap,401282`**
  ⇒ fid 12 与 14 两个省**重叠约 0.4 km²**，而"重叠检查"报"一处都不重叠"。
  根因（现读代码）：`vector_plugin_overlap_check.inc:79-89` 的 switch 只认 `Polygon`/`MultiPolygon`，
  交集是 `wkbGeometryCollection` 时落 `default: 0.0` ⇒ `:92` 的 `overlapArea > 0.0` 不成立 ⇒ **整行不写**；
  而 `topology_check` 走 `polygonIntersectionArea()`（`vector_plugin_common.inc:492-511`），会**递归累加集合里的子面面积**。
  真实省界常是 `MultiPolygon`（飞地/跨海），相邻两省的交集里同时含面与共边线段是**常态**，不是罕见角落。
  后果面：用"重叠检查"做数据质检会**漏报真实重叠**，而"拓扑检查"能报出来 ⇒ 同一份数据两个动作自相矛盾，
  这是算法口径问题，不是文案问题。
  **销账状态（C3d，`19cb39f`）**：`overlap_check` 的那段本地 switch 已删除，改为复用同一个
  `polygonIntersectionArea()`（单源，实现里不再有第二份面积算法）。
  合成夹具两态取证（Release 树）：修复前那条新用例红在 `relation_count "0"` 与"CSV 只剩表头 1 行"，
  同轮的夹具前提断言同时证明 GEOS 对这个配方给出的 `A∩B` 实测类型就是 `wkbGeometryCollection`
  且集合里既有面子也有非面子 ⇒ 判据没有降级成"只要非 0"；修复后同一用例绿（1 行、`overlap_area` 25.0，
  算式 `5×5 + 线部分 0`）。真实省界腿同一天复跑：`overlap_check(prov3857)` 的 CSV 现读
  `12,14,401282`，与 `topology_check` 的 `12,14,overlap,401282` 同值同对 ⇒ 关系数 1 == 成对行数 1，
  省界臂因此从"只敢写 `>=`"收紧成等式（`>=` 那条原样保留，等式另开一条）。
  文档口径同步：`docs/算法说明/矢量数据处理/重叠检查.md` 与 `拓扑检查.md` 原来各说一套（前者写的正是缺陷语义），
  现已写成同一个口径并互相点名。双树全量 698/696 全绿，见上面"全量墙钟基线"的 C3d 轮。
- 挂账 **N126**（测试基建口径，不是产品缺陷）：C3b-A 执行者自查发现 11 条腿的判据第一次被插在
  `Invoke-SetupCommand` 的 `return` **之后**（死代码），正向跑 `rc=0` 全绿 ⇒ **"跑过"不等于"判过"**。
  识破它的是**反向对照**（故意改坏表头，`rc` 仍 0 就说明判据在空转）；修正后两条反向对照当场红，
  其中 NEG-2 的红点是 `singlepart_prov: 588 != multipart_check part_count 588`。
  ⇒ 现口径：**每批真实腿交付必须至少附一条"已知会红"的反向对照原始输出**；
  是否把反向自检臂做进脚本（工单 C3e）待定，代价是每轮多跑一次与脚本复杂度。

批次 C 的 C3c 建账（只读调研 12 个动作 + 控制器逐条复核）又查出五条产品侧缺陷，**本批只立案、修在 C3f~C3i**
（细节、射程与"哪些还没核"都写在工单 `Task C3c` 那张表里，这里只给结论级描述；**N127 已由工单 C3f 修复销账，见该条的销账状态**）：

- 挂账 **N127**（**优先级最高**，`vector filter`；2026-10-22 立案 ⇒ **同一日由工单 C3f 修复销账**，提交 `fc68e12`；取证见 `task-C3f-report.md`）：
  **非 ASCII 的 `--where` 一律被拒** ⇒ 中文属性值过滤不了，
  而本仓样例数据（省名/市名）全是中文，这是用户主场景。现读根因在
  `src/plugins/vector/vector_plugin_info_filter.inc:1-16`——白名单用 `std::isalnum(unsigned char)`，
  默认 C locale 下 `>=0x80` 字节不是"字母数字" ⇒ 落到 `return false`；调用点唯一（`:157`）⇒ **射程只有 filter**。
  **双臂实测**（控制器现跑，夹具 `build/tmp/cnfilter/in.json`）：
  `--where=name='北京'` ⇒ `Error: WHERE clause contains disallowed characters or keywords`；
  同一文件 `--where=name='shanghai'` ⇒ `Success: Filter completed: 1 features written`。
  修复方向是"UTF-8 字节序列合法化 + 保留注入面拦截（引号/分号/`DROP` 等关键字）"，不是把白名单整个删掉。
  **销账状态（C3f，`fc68e12`）**：`>=0x80` 的字节现在**只有在构成合法且规范的 UTF-8 序列时**才放行
  （新增 `utf8LeadContinuations` + `consumeUtf8Sequence`：`C0/C1`、`F5..FF`、落单续接字节不能作 lead；
  `E0→A0..BF`、`ED→80..9F`、`F0→90..BF`、`F4→80..8F` ⇒ overlong、UTF-16 代理区、`>U+10FFFF` 全拒；悬挂 lead 拒）。
  **不是** `c >= 0x80 continue` 那种形态，而且这一点是**变异实测**出来的：临时把它写成 `>=0x80 continue` 后，
  反向用例的五条 UTF-8 形态臂（`0xFF 0xFE`、`0xC0 0x80`、`0xF5 80 80 80`、悬挂 `0xE4`、落单 `0x80`）当场全红。
  ASCII 白名单集合（含 `'`、不含 `"` `` ` `` `;` `/`）、危险关键字表与单词边界匹配、分号/`--`/`/*` 三道闸**逐字未动**
  ⇒ 注入面不回退（`1=1; DROP TABLE x`、`name="…"`、`` name=`…` `` 三条臂同钉）。
  两态取证：未修复实现 + 新正向用例 ⇒ 红在那条 `disallowed characters or keywords` 文案上；修复后 ⇒ 绿。
  真实数据腿 `filter_cn_attribute`（进 `real_data_vector_quick`，两档都跑）：省层复制件的 `name` 众数值
  （现读为 3 字中文、8/42 条）做 `--where`，中文值由脚本 `-Encoding UTF8` 运行时现读构造（脚本字面量仍全 ASCII），
  判据是自报数 == 现读 Features == 输入侧手推 M 的三段等式 + 输出 Extent 等于被留要素在输入里的包框 +
  四边不得与全层逐边相同。反向对照同一命令只换 DLL：旧 `plugin_vector.dll` ⇒ `rc=1` + 白名单文案，新 ⇒ `8 features written`。
  ⚠ **本刀顺手现读到一条与本缺陷无关的口径不一致，未改、请控制器裁定是否立案**：`vector filter` 是这批矢量动作里
  **少数不写 `metadata["feature_count"]`** 的（`doFilter` 只回消息与输出路径，`grep -rn feature_count src/plugins/vector/`
  可复现：写这个键的 18 处里没有 `vector_plugin_info_filter.inc`），而文档口径也确认"返回消息中的数量"才是它的产物
  （`docs/算法说明/矢量数据处理/空间过滤.md` 结果解释）。后果：GUI/CLI 的统一展示面与任何"按 `feature_count` 核数"的
  通用判据对 `filter` 都拿不到键（本刀的真实腿因此改用消息里的数）。**同族的 `clip/union/intersect/difference/spatial_join`
  是否也缺，本刀没核**，不外推。
- 挂账 **N128**（`vector union`）：**它不是并集**。只遍历源图层要素（`vector_plugin_overlay.inc` union 段的
  `while (srcLayer->GetNextFeature())`）⇒ 叠加层独有的几何被静默丢弃，产物覆盖 ⊆ 源覆盖，
  与 `docs/算法说明/矢量数据处理/并集.md` 的语义相反；**并且输出图层只建一个 `source` 字符串字段**
  （缺陷当时的现读位置是 `:85-86`，**该号随 C3h-2 已失效**，现在两条路共用同一张建表代码），
  源属性整表丢失——而同一文件里的 `intersect`/`difference` 都各自有字段循环（`:264`、`:437`）
  ⇒ **同一文件三个动作两种口径**，union 是异类。
  **裁定（2026-10-22，用户选"真并集 + 整表属性"）**：输出必须覆盖"源 ∪ 叠加"的全部几何，两边字段整表合并，
  另带 `source` 标记；同名冲突的叠加字段改名 `<name>_2`/`<name>_3`…（GDAL 的 `OGR_F_FoundUniqueName` 不在公开头里，
  实测 `grep -rn FoundUniqueName vcpkg/installed/x64-windows/include/` 为空 ⇒ 本仓自实现并把规则写进文档）。
  开工刀 = 工单 `Task C3h-1`（只改路径 A；"线 × 面"那条混合维度路径 `usePracticalMixedUnion` 本刀逐字不动，
  它的属性补齐排在 C3h-2，因为既有 `VectorUnionLinePolygonOutputsSplitLinesOnly` 钉的就是它的 3 片段形状）。
  **路径 B 也已兑现 `cb443fb`（C3h-2，两轴评审通过、无必须修）**：输出表与路径 A 共用同一条建表规则
  （源字段→叠加字段撞名 `<name>_2`→标记列撞名由标记列让位），片段按归属叠加面带两侧整表属性；
  `source` 词表收敛为 `input`/`both`（**`split` 退役**），路径 B 的 `union_overlay_pieces` 恒 0 是契约不是缺陷
  （线×面不产出叠加面）。⚠ 工单原文写"取第一个 `Intersects` 为真的要素"是**错的**：切分结果保留切口端点 ⇒
  三条"面外"片段对相邻面 `Intersects` 全为真（实现者与本席各自用 GEOS 复算过，
  但注意 shapely 用的 GEOS 与本仓 vendored 3.15.0 **不是同一份**，所以真正的证据是自建构件上的 MU-A/B/C 红绿）；
  落点改用"差集为空 = 被盖住"（等价 covered-by，允许贴边界），并已写进代码注释与文档。
  **路径 A 已修 `8ed6b75` + 复审回收 `ff86362`**（六条用例 T1–T6，两棵树 `VectorUnion*|CoverageMeta*|CoverageMetaJudge*`
  选择器 28/28，三条老用例 3/3 单跑通过）：三条分块 `input`/`both`/`overlay` 互不重叠且面积可加，
  两侧字段整表合并（**撞名时让位的是我们附加的标记列 `source`→`source_2`，用户数据列名逐字保留**——
  评审席推荐反过来，控制器否决：改用户列名会让下游按原名取值的代码静默拿不到值，正是 N128 这一族缺陷的镜像），
  元数据 5 键（`union_input_pieces`/`union_overlap_pieces`/`union_overlay_pieces`/`union_total_pieces`
  + 只在 >0 出现的 `union_skipped_overlay_pieces`）。
  ⚠ **`union` 的 GOLD 登记已落地**（C3h-1b `7a9edf8`：判别取证通过 ⇒ `vector,union` PENDING→GOLD，
  证据用例 `PluginTest.VectorUnionSplitsIntoThreePieceClasses`，`kPendingCeiling 61→60`）；
  **路径 A 的真实数据腿已在 C3h-3 补上**（`381500b` + `35f578e`：新注册用例 `real_data_vector_union_poly`，
  源 `data/vector/中国_省.geojson` × 叠加 `data/vector/中国_市.geojson`，两侧都是"面 + 线"混合层、同 EPSG:4490；
  产物 641 行 = 三类分块 `input 4 + both 628 + overlay 9`，`source` 词表与整表属性一起落地）；
  ⚠ 归属判据不是"源层含面"（那是评审席 M1 判掉的假前提）：Unknown 层的有效几何类型取**第一个非空要素**的类型，
  省层现读前四个要素是 `MultiPolygon/MultiPolygon/MultiLineString/MultiPolygon` ⇒ 今天落路径 A 是**数据顺序**决定的，
  因此该腿把"首个非空要素是面"写成断言、并由哨兵 `union_overlay_pieces >= 1` 在产物侧兜住退回路径 B 的情况。
  ⇒ 口径仍然成立但含义更新了：**GOLD ≠ 真实数据覆盖**这条现在两条都齐了，但两者是**不同性质的凭据**，
  合成夹具钉语义、真实腿钉"这份数据真能走通"，谁也不许顶替谁。
  ⚠ 口径写死在这里防止误读：**GOLD ≠ 真实数据覆盖**——这一行的凭据全在合成夹具与插件单测里，
  真实数据那条腿是空的；两者是不同的缺口，不许互相顶替。
- 挂账 **N137**（GPKG 写出路径的 FID 与失败回滚，**独立立案，不由 C3h-2 承担**）：评审席在变异体 MU-C 下抓到
  `UNIQUE constraint failed: union.fid`，且**同一条代码路径在两个夹具下 FID 起点不同**（老用例产物 fid 从 0 起、
  新用例从 1 起）⇒ 驱动分配的 FID 序列不可期望；另有"写出失败即 `RollbackTransaction()` 把刚建的表一起回滚"
  （现象里那两条 `no such table: main.union`）——两件事都落在共享的 `writeGeometryFeature`/GPKG 事务批量写上
  （`SetFID(OGRNullFID)` 三处自 `99d9c91` 起就有），且 `VectorClipRepairsInvalidOverlayGeometry` 已为**同族**症状
  专门钉过"`UNIQUE constraint failed` 不得出现"，union 这条腿却没有等价断言。
  定位实验：MU-D（保留 covered-by、强制归属取 `overlayFeatures.front()`）与 MU-E（老用例输出换 `.geojson`）。
  ⚠ **2026-10-23 控制器现读（不靠变异就能复现，HEAD `f67e724` 的产物即证据）**："FID 起点不同"不是随机噪声，
  而是**跟着输入格式走**：扫 `build/debug/tests/Debug/test_e2e_output/` 全部 103 个 `.gpkg` 的落盘 fid 首值 ⇒
  **4 个从 0 起、99 个从 1 起，且那 4 个的输入都是 `.shp`**（`e2e_clip_invalid_output` / `e2e_clip_line_output` /
  `e2e_difference_no_overlap_output` / `e2e_union_line_output`），输入是 `.gpkg` 的没有一例从 0 起。
  ⇒ 与"源要素的 FID 漏进了目标要素"这条直觉相容，但**代码读下来不成立**：`writeGeometryFeature`
  （`vector_plugin_overlay_support.inc:63`）在 `CreateFeature` 前显式 `SetFID(OGRNullFID)`，全仓也没有任何
  "从源要素取 FID 写进目标"的赋值点（`grep -rn "SetFID(" src/` 现读 **10 处，实参全是 `OGRNullFID`**，
  分布在 endpoints/midpoints/nearest/spatial_join/vertices_extract 与 `overlay_support` 的 5 个写出helper）。
  ⚠ 所以这一半**仍是未解**：`fid` 首值 0 需要 `INSERT` 里显式带 fid 才可能出现（表是
  `fid INTEGER PRIMARY KEY AUTOINCREMENT`，`sqlite_sequence.seq` 记的是最大值 2 ⇒ 0 不是自动分配的结果），
  下一步要么在写出前打印 `dstFeat->GetFID()`，要么按 GDAL 侧 `m_bInsertStatementWithFID` 那条语句缓存分支查。
  **另半边（回滚删表）机制已确认**：`CPL_DEBUG=1` 下 union 老用例的日志顺序是
  `OGR_SQLITE: BEGIN Transaction` → `GPKG: exec(CREATE TABLE "union" …)` → `COMMIT` ⇒
  建表 DDL 确实落在事务里（GDAL 的延迟建表 `RunDeferredCreationIfNecessary` 在首次写入时才 exec），
  因此"第一条片段就写失败 ⇒ `RollbackTransaction()` 连表一起撤销 ⇒ 产物只剩一个没有要素表的 `.gpkg`"成立；
  而输出文件在动作开头就被 `drv->Create()` 截断 ⇒ **失败时旧产物已经没了，新产物又不可用**（这才是要裁的契约点）。
  ⚠ 已裁定（2026-10-23，用户）：**改成"先写同目录临时件 → 全部成功 → 原子改名替换"**，
  失败时旧产物原样存活、不留下半截新产物 ⇒ 排 **C3n**（Task #110）。
  ⚠ **射程更正（控制器 17:5x 现读，先前写的"17 个写出点"是错的口径）**：截断这件事的论域是
  `drv->Create(` 的 **25 处**（`grep -rn "drv->Create(" src/plugins/vector/` = 25，分布 20 个文件：
  `buffer_clip` 2、`overlay` 3、`raster_convert` 3，其余各 1）。17 只是"显式发图层选项的调用点"数
  （索引那一刀的口径），两者不是一回事。**而且 17 与 25 之差没有语义**：
  `buildGpkgDriverIndexedLayerOptions()` 的函数体就是 `return {};` ⇒ 走 builder 的点和从来就发 `nullptr` 的点
  交出去的是同一个空选项。
  ⚠ **旧产物处理其实有三套并存**（取证席现读、控制器复核）：
  ① **16 个点**调 `removeExistingVectorOutput(output, outFormat)`（`vector_plugin_common.inc:327`）——
  Shapefile 按同 stem 删边车、**其余格式 `fs::remove(outputPath)` 整个删掉**；
  ② **8 个点**内联一个只在 `format == "ESRI Shapefile"` 时才遍历删边车的循环（GPKG 根本不删，靠 GDAL 覆盖）；
  ③ `doRasterize` 什么都不删。⇒ C3n 要统一的是这三套，不是只加临时件改名。
  ⚠ **附带条件② 的字面说法要更正**：本机 MSVC STL（工具集 14.44.35207，`crt/src/stl/filesystem.cpp:722`）
  把 `std::filesystem::rename` 实现成 `MoveFileExW(..., MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)` ⇒
  目标已存在是**覆盖成功不报错**（这一点正是"旧产物被替换"要的），但带着 `COPY_ALLOWED` ⇒
  跨卷时**悄悄退化成 copy+delete**，不是失败 ⇒ "原子改名"这个承诺 `fs::rename` 兑不了，
  实现必须直接用不带 `COPY_ALLOWED` 的 `MoveFileExW`（跨卷就让它失败），或者诚实改名叫"覆盖替换"。
  ⚠ **附带条件③ 的载体已找到、且不欠产品代码**：命令行造不出中途失败（`CliProgress::isCancelled()` 恒 false），
  但本仓已有的 `T17CancellableProgress` 式 reporter 可以在用例里数到第 N 次就抛 `CancelledException` ⇒
  `vector boundary` + 两面输入即可；因全插件 **0 处显式 `Close()`**（`grep GDALClose src/plugins/vector` = 0）、
  `dstDS` 析构才走 `GDALClose` ⇒ 异常展开时半截产物必落盘。这条覆盖 16 个点（`doDissolve` 的 throw 在循环外、
  `doUnion` 在两个写循环之间，那两处够不到）。**取消缝解不了 N135**（那三条要的是"第 N 次几何运算返回失败"，不是"第 N 次被取消"）。
  顺带那条保护现在**必须有**而不是"自然变安全"：今天 16 个点在 `drv->Create` **之前**就 `fs::remove(outputPath)` ⇒
  `output == input` 时**输入文件本身被删掉**（不是截断，是删）；改成临时件后这次删除推迟到结尾的改名，
  对用户反而更坏（跑到一半才失败）。全仓零保护（`grep -rn "output == input\|fs::equivalent" src/` 里两处
  `equivalent` 都是输入 vs 输入）⇒ 实现要在入口用 `fs::equivalent(input, output)` **硬失败**，
  口径参照同插件 `spatial_join` 对"字段不存在"的硬失败（N129 那次对齐）。
  ⚠ **2026-10-24 C3n 收口后的状态更新**：本条两半现在一半销、一半留——
  ① **"写失败 ⇒ 回滚把表一起撤销 ⇒ 旧产物已毁、新产物半截"那半已被彻底规避**：C3n 把 24 个矢量写出点全部改成
  "先写同目录临时件、全部成功后替换" ⇒ 动作失败时**终名根本没被碰过**（旧产物原样在场），临时件由析构删除；
  J1 两条用例（中途取消）与 J4（`output == input`）在两棵树绿，且各自证明过能红。
  ② **FID 首值异常仍无解释**（4/103 个 e2e `.gpkg` 从 0 起、输入全是 `.shp`；`SetFID` 10/10 是 `OGRNullFID`），
  但今天**没有用户可见影响的证据、也没有判据** ⇒ 状态改为"**已知未解、无判据**"，不再作为阻塞项；
  将来要查的入口是"写出前打印 `dstFeat->GetFID()`"或 GDAL 侧 `m_bInsertStatementWithFID` 的语句缓存分支。
- 挂账 **N138**（GPKG 空间索引写成"声明了但是空的"，**用户可见级**）——**已销 `d583c9b` + `f0b0d0e`（批次 C C3l）**：
  **射程 5 个动作**（`clip`/`dissolve`/`union`/`difference`/`intersect`）产出的 `.gpkg` 带一个 **0 行的 rtree**，
  而 `gpkg_extensions` 里已登记 `gpkg_rtree_index` ⇒ 任何按包框查的读端拿到 **0 个要素**（表里明明有 N 行）。
  ⚠ **立案时我把范围写成"六个"是错的**：`vector_plugin_buffer_clip.inc:54` 写的是
  `const bool useTransactions = false;`（旁注 "prefer the driver's native spatial index creation path"）
  ⇒ buffer 既不关索引，那条补索引调用也被外层 `if (useTransactions)` 挡死（本就是死代码），它的产物索引一直回填过。
  实现者按现读把偏差摆出来（工单要"6 条全红"，实测 **5 红 + buffer 绿**），控制器裁定为**改判射程**而不是砍判据。
  旧态现读：`test_e2e_output/` 103 个 `.gpkg` 里 Debug 树 **16 个图层**"表有 N 行、`rtree_<层>_geom` 0 行"
  （Release 树 15 个）；立案句里那个"**31**"是**两棵树相加**，不是单棵树。
  ⚠ **立案句的"对照①"是一条假证据，此处更正**：当时用的 `-spat -1 -6 31 6` 是照用例坐标猜的包框，
  而那份产物的真实 extent 是 `(0,50)-(100,50)` ⇒ 包框**不与任何要素相交**，"加包框 0 个要素"是**正确的查询结果**，
  它证明不了缺陷。换成三条能承线的证据：
  ① **同一份 parcels 数据的 A/B**：`ogr2ogr -lco SPATIAL_INDEX=NO` + 建 `gpkg_extensions` + `gpkgAddSpatialIndex`
  ⇒ 索引 0 行 ⇒ `-spat ±1000` 得 **0**；`ogr2ogr -gt 100000`（驱动自己建）⇒ 索引 3 行 ⇒ 同一包框得 **3**；
  ② 旧产物 `e2e_dissolve_invalid_output.gpkg`：extent `(0,0)-(120,100)` 落在 `±1000` 内，表 1 行 / 索引 0 ⇒ 查询 **0**；
  ③ **同一份 union 夹具数据改前/改后 A/B**：改前 rows=3 / rtree=0 / `-spat -1 -1 101 51` ⇒ **0**；
  改后 rows=3 / rtree=3 / 同一包框 ⇒ **3**。
  ⇒ "**没有索引**"是合法状态（读端退回全表扫描，实测 `e2e_boundary_output.gpkg` 无过滤 2 / 加包框 2），
  "有索引但是空的"才是缺陷；GDAL 只看 `rtree_*` 表在不在就认定有索引（`ogrgeopackagetablelayer.cpp:5143-5159`），
  唯一的坏索引启发式要 ≥100 000 要素（`OGR_GPKG_THRESHOLD_DETECT_BROKEN_RTREE`），本仓最大产物 22 855 行够不着。
  根因：`gpkgAddSpatialIndex` **只建 rtree 表和 6 个触发器、不回填已经在表里的行** ⇒
  "先关掉索引批量写、写完再补索引"必然交出空索引；且 `addSpatialIndexIfNeeded` 把 `ExecuteSQL` 的返回值当布尔用，
  三种失败形态全部静默（本仓"没检查长得像检查通过"这一族的又一例）。
  **修法（已落地）**：6 个调用点改走 `buildGpkgDriverIndexedLayerOptions()`（一个图层选项都不发 ⇒
  GDAL 在 `FlushCache` 里延迟建索引并一次性回填，`ogrgeopackagedatasource.cpp:3457-3461`），
  `addSpatialIndexIfNeeded`/`ensureGpkgExtensionsTable`/`escapeSqlLiteral` 删调用后零引用 ⇒ 整体删除；
  事务批量化一字未动 ⇒ **批量写与索引正确性不冲突**。另外 11 个不建索引的调用点
  （boundary/centroid/convex_hull/endpoints_extract/envelope/filter/interior_point/midpoints_extract/singlepart/
  sliver_remove/vertices_extract）在 **C3l 这一档**源码与产物形态逐字不变（改前改后"无 rtree"的图层名单一致）。
  ⚠ 上面那句"11 个不建索引、产物形态不变"**只在 C3l 这一档成立**：**2026-10-23 用户裁定这 11 条也要带索引 ⇒ 已由 C3m 落地**
  （`a7fb866` + `fb40ca4` + `f339f45`，逐条读数见第 2 节"批次 C C3m 收口轮"那一条）⇒ 现在 17 个 `.gpkg` 写出点
  共用同一个 `buildGpkgDriverIndexedLayerOptions()`、产物侧"没有 rtree 的活图层"归零
  （NO-RTREE Debug 13→1 / Release 12→0，Debug 残留那条是 10-22 的孤儿件、生产它的用例按设计不写产物），
  源码侧 `SPATIAL_INDEX` 在 `src/` **0 命中**且由常驻守卫守着。
  判据（C3l 当时）是六条 `PluginTest.Vector*GpkgSpatialIndexIsPopulated`，按**读端性质**写：主判据 =
  `SetSpatialFilterRect(夹具全 extent)` 后 `GetFeatureCount()` == 由夹具坐标手算的期望数
  （buffer 200 / clip 1 / dissolve 1 / union 3 / difference 1 / intersect 1），索引行数只作解释性判据；
  旧态 5 条红、新态 6 条绿（两棵树），注册数 730 / 728。
  ⇒ **C3m 把这一族扩到 9 条 + 一条负向守卫**（新增 centroid 点 / boundary 线 / convex_hull 面三类几何各一条，
  加 `VectorSourceNeverDisablesGpkgSpatialIndex`），注册数现值 **735 / 733**。
  **真实数据侧的收口证明**：`tmp/vector_regression/union_focus.gpkg` 由 C3l 收口轮重跑生成，
  表 **22 855** 行 / rtree **22 855** 行（文件从 5 582 848 B 增至 8 085 504 B，多出的就是索引），
  官方 `ogrinfo -so -al -spat <覆盖整层 extent 的包框>` 读回 **22 855** 个要素（改前该产物索引 0 行）。
  ⚠ 复算命令的字面形要记对：**`-so` 不给 `-al`（或不给层名）时只打印一行层名，既没有 `Feature Count`
  也没有 `Extent`** ⇒ 本台账旧版写的"`ogrinfo -so -spat`"是转录失真（数字可复现，命令行不可复算）。
  控制器 17:4x 重跑现读：`-so -al` 无过滤 22 855；`-so -al -spat 115.418507 39.443683 117.450919 41.057989`
  （= 该层 `Extent` 本身）⇒ `Feature Count: 22855`，文件 8 085 504 B。
  ⚠ **评审席把这条做宽了**：磁盘上同时留着改前（`build/tmp/c3a/legtest`，10-21）与改后（`tmp/vector_regression`，10-23）
  两轮真实腿产物 ⇒ **5 个动作都有同腿 A/B**（union 22 855 行，其余量级 15 077 / 22 836 一类）；
  本条最硬的证据是这张 5 动作表，不是上面任何单条。评审席同时判定**"对照②与对照③同机理、属重复"**、
  "对照①归档产物的行数（1 行）与本条目引用的 3 行不同源" ⇒ 结论不靠这三条撑（读端源码 + 5 动作真实腿撑住），
  台账目前写法**偏保守而非偏乐观**。
  ⚠ 评审席另给一条**比本刀论证更强的机制事实**：GDAL 的延迟建索引把"回填 / 登记扩展项 / 装触发器"放在同一段执行、
  任一步失败整段回滚 ⇒ 走驱动这条路**结构上不可能**再产出"登记了但是空的"rtree（旧写法是三段分开，才会留下半截状态）。
  **buffer 那条的证据折扣已销**（免构建复验，控制器 11:15 现跑）：复制改后产物 `e2e_buffer_index_output.gpkg`、
  `DELETE FROM "rtree_e2e_buffer_input_buffer_geom"` 清空索引 ⇒ `ogrinfo -so -al -spat <该层 Extent 本身>` 读 **0**、
  不加过滤读 **200** ⇒ 主判据④在 buffer 上有自己的红态，不必再等"旧态红"。
  ⚠ 这条 17:5x 由控制器**重做并加了对照**（评审席曾反证"迭代读端拿到全量 200"，我复现不出来，查下来是**包框没盖住数据**）：
  该产物是投影坐标、`Extent = (12939975.000319, 4851975.000319) - (12944034.998187, 4852224.998187)`，
  拿地理值域 `±180/±90` 去框它，**改前改后都读 0**（那是我的框错，不是索引的状态）；改用该层自身 Extent ⇒
  坏索引产物 `-so -al` 与 `-al -spat`（逐条迭代）**双双读 0**、完好产物同框读 200。
  ⇒ 结论："按包框查的读端拿到 0 个要素"**同时成立于计数与迭代两条路径**，但复算前必须先打印 `Extent` 证明框盖得住数据
  （本仓这条已经错过两次，都错过在同一处：拿地理值域框投影坐标）。
  ⚠ **本条最该沉淀的方法**：31 个产物、两棵树几百条用例连续几周全绿，缺陷却在每一个受影响的 `.gpkg` 里——
  因为所有断言都是"我们写什么就读什么"（要素数、面积、字段名），**没有一条按读端会怎么用这个文件去查**。
  这次是靠引入仓库外的第二个客户端（vcpkg 的官方 `ogr2ogr`/`ogrinfo`）才打出来的；
  以后"产物格式合规"类判据一律配一个独立客户端，别再加第十条自产自读的断言。
  ⚠ 顺带修正一条扫描口径：**"索引行数 == 要素数"对含 NULL/空几何的图层天然不成立**——GDAL 触发器的条件是
  `WHEN new."geom" NOT NULL AND NOT ST_IsEmpty(new."geom")`，实测
  `e2e_singlepart_input`（3 行含 1 个 NULL ⇒ 索引 2）与 `t20_empty_point_input`（4 行含 2 个空点 ⇒ 索引 2）
  都是**正确状态** ⇒ 比的是"非空几何数"，不是行数；否则会把合规产物读成缺陷（我自己差点把它记成第 2、3 条红）。
  ⇒ **这条口径已由 C3m 落进判据 ③**（`fb40ca4`：helper `countGpkgNonEmptyGeometries` 自己遍历产物数非空几何，
  比较对象从"要素数"改成"非空几何数"；旧臂/新臂并排演示＝既有夹具 `e2e_singlepart_input` 上旧臂报 `2 != 3`、新臂不报）。
  ⚠ 已裁定并**已落地**（2026-10-23，用户）：**那 11 条也要带索引** ⇒ **C3m**（Task #109，`a7fb866`/`fb40ca4`/`f339f45`）
  把 17 个写出点（**= "显式发图层选项的调用点"数**，索引论域的真值是 25 个 `CreateLayer` 点，见上面 C3m 那条的论域更正）
  统一交给驱动建索引、并加一条反回潮守卫（口径现值：在 `src/plugins` 全文做 ASCII 大小写折叠后数
  `spatial_index` 与 `gpkgaddspatialindex` 两个 token，合计必须 0 次）；
  成本再次实测到可忽略（22 855 要素补索引 = 文件 5 582 848 → 8 085 504 B、CLI 墙钟 134.18 → 135.20 s），
  代价改到"必须重测真实腿余量"这一头——**现值六条腿**（立案时写"四条"，C3h-3 之后多了 `union_poly` 与拆腿后的分组，
  名单以 `tests/CMakeLists.txt` 的 `GIS_REAL_DATA_TEST_NAMES` 为准）两棵树各两跑，最低余量 2.66×、未拆腿未抬 TIMEOUT。
- 挂账 **N139**（`vector clip` / `difference` / `intersect` 把写失败吞成"少写一条"）——**2026-10-24 已修、且三支都有真载体判据**：
  立案时的症状：这三个动作的写循环只在 `if (write…)` 为真时 `count++`，**为假时既不报错也不中断**，
  末了照常 `CommitTransaction`/返回 `Result::ok` ⇒ 产物少要素、stdout 计数也跟着少，而**动作报成功**
  （立案现读 `vector_plugin_buffer_clip.inc:264`/`:291`、`vector_plugin_overlay.inc:640`/`:685`/`:825`/`:846`）。
  同一插件里 `buffer`（`:106` `Failed to write buffer feature`）、`dissolve`（`:125`/`:146`）、
  `union`（`overlay.inc:270`/`384`/`421`/`491`）本来就是**回滚 + 硬失败** ⇒ 同类错误两种行为（N129 同族）。
  **修法（已落地）**：clip 在 `buffer_clip.inc:290`/`:325`、difference/intersect 在 `overlay.inc` 各自的对偶点
  一律改成"若在事务里 `RollbackTransaction()` + 返回 `Result::fail("Failed to write … feature")`"。
  **判据（已落地，先红后绿）**：`Vector{Clip,Difference,Intersect}RejectedFeatureWriteDoesNotCorruptHeap` ——
  载体是**驱动拒写**本身（A0 时代那条 clip 用例就已造出真实的 `CreateFeature` 失败），
  clip 那条把旧契约的 `EXPECT_TRUE(success)` 翻成 `EXPECT_FALSE` + "失败不许留半成品"，
  difference/intersect 是照它新写的两条。⇒ 本条的判据缺口**不需要注入缝**。
  ⚠ 与 N135 的关系要分清：N135 的三条分支要的是"**第 N 次几何运算**返回失败（GEOS 层）"，
  驱动拒写造不出那种失败 ⇒ **用户 2026-10-23 裁定：不为 N135/N139 在产品代码里引入失败注入缝**
  （产品二进制零隐藏开关优先）；N139 恰好另有载体所以已覆盖，**N135 仍永久无判据**。
  N139 剩下的缺口只有一条：`RollbackTransaction()` 那条回滚支本身**零覆盖**（用例走的是"失败后不留半成品"，
  没有断言事务确实回滚过）。
- 挂账 **N140**（`.kml` 输出**是一条被声明却从未被走过一次的路**，低优先，C3n 实现前顺手定口径）：
  `grep -rn 'ext == ".kml"' src/` 现读 **17 处**（vector 插件 16 处 + `projection_plugin.cpp` 1 处）⇒
  十六个矢量动作把 `.kml` 后缀接进格式分派；而 `find build -name "*.kml"` = **0 个**、
  `tests/` 里提到 `.kml` 的 3 处全在 GUI 保存框字符串断言（`test_gui_support.cpp:581`/`:587`/`:592`）
  ⇒ **没有任何一条用例真的写出过一个 KML**。驱动是有的（本机 vcpkg GDAL：`ogrinfo --formats` 现读
  `KML -vector- (rw+v)` 与 `LIBKML -vector- (rw+uv)`），所以这不是"驱动缺失"而是"能力被分派、没被判据覆盖"。
  需要判的三件事（都不在本刀）：① KML 驱动只允许单图层、图层名是它自己定的 ⇒ `CreateLayer(srcLayer->GetName(), …)`
  在这条路上会不会静默改名；② KML 要求 CRS 为 WGS84 ⇒ 投影坐标写出去是坐标原样落进 `<coordinates>` 还是被 GDAL 拒；
  ③ KML 是单文件还是 `kmz` 多文件集（关系到 N137/C3n 的"旧产物处理"口径）。
- 挂账 **N141**（`gis::core::RasterBlockProcessor` **197 行零调用者**，低优先）：控制器 2026-10-24 现读
  `block_processor.h` 72 行 + `block_processor.cpp` 125 行，编进 `gis_core`；四个符号
  `RasterBlockProcessor` / `BlockProcessor` / `processBlock` / `processBlocks` 在 `src/plugins/`、`src/gui/`、
  `tests/` 三个域逐个命中都是 **0**，全仓 6 处 `grep` 命中全在它自己的声明与实现里；`docs/` 零提及。
  ⇒ 一件从未接进执行链的分块基础设施（同族先例："已知限制"里的 `PerformanceMonitor` 未接入主执行链）。
  **删还是接进去用**不在本刀裁定范围内，与 C3h-2b 的冗余批一起判。
  ⚠ 判"某东西有没有人用"要按头文件逐个符号搜：`ExpressionEngine` 这个类名搜出来也是 0，但表达式引擎的真身是
  自由函数 `compileExpression`/`evaluateExpression`（`src/core/.../expression_engine.h`），按类名搜会得到假结论。
- 挂账 **N142**（`projection` 插件**自己那份** `removeExistingVectorOutput` 与同族的失败契约问题）：
  C3n 收口时把 vector 侧那份删掉后，现读 `grep -rn removeExistingVectorOutput src/` 还剩两处——
  `projection_plugin.cpp:157` 的定义与 `:251` 的调用（该插件走 `GDALVectorTranslate`，不经 `CreateLayer`，
  按用户裁定"先限 vector"不在 C3n 射程）。它今天的行为与 C3l/C3n 修好之前**同形**：
  在建新产物之前先把旧产物删掉 ⇒ 失败时"旧产物已毁、新产物半截"。要不要按 C3n 的临时件契约改造，另行裁定。
- 挂账 **N135**（`vector union` 路径 A 的三条"GEOS 返回失败"分支，**今天无判据**，由 `ff86362` 复审席点名）：
  ① 已写面积累加时某次 `Union` 返回 nullptr ⇒ 减法基少一块 ⇒ 源独有块会把已写相交面积再算进去（不计数）；
  ② 逐条差集只减动部分候选 ⇒ 写出的叠加独有块与没减动那条源几何重叠（不计数）；
  ③ 级联并集的子树失败返回"只并了一半"的**非空**结果（`vector_plugin_overlay_support.inc:181-186` 的
  `if (!left) return right;`）⇒ 少减一条且不计数。三条都要靠一个"第 N 次几何运算返回失败"的测试闸才可达，
  `docs/算法说明/矢量数据处理/并集.md` 已把这三类如实列为限制（不再写"绝不重叠"那种绝对句）。
- 挂账 **N129**（`vector dissolve`）：`dissolve_field` 写错（输入层没有这个字段）时**不报错**，
  静默把所有几何并入一组（`vector_plugin_dissolve.inc:41-47` 的 `else allGeoms.push_back`）⇒ 用户以为按字段融合、实际融成一坨；
  对立面是同插件 `spatial_join.inc:38-43` 对"字段不存在"**硬失败** ⇒ 同类错误两种行为。
  **已修 `91025ec`**（改硬失败，案文与 `spatial_join` 对齐），判据用例 `PluginTest.VectorDissolveRejectsUnknownDissolveField`。
- 挂账 **N130**（`buffer` / `dissolve` / `simplify`）：**投影闸三档并存**——前两者只拒 `geographic`
  （无 CRS ⇒ 放行并**静默按"度"做米制运算**），`simplify` 要求 `== "projected"`（无 CRS ⇒ 拒）
  （现读 `vector_plugin_buffer_clip.inc:27-29`、`vector_plugin_dissolve.inc:24`、`vector_plugin_simplify.inc:26`）。
  同一份无 CRS 数据在两个动作能跑、在第三个被拒 ⇒ 属跨动作口径问题，修复要单独一刀。
  **裁定（2026-10-22，用户选"统一为必须投影坐标系"）**：`!= "projected"` 是唯一口径。开工刀 = `Task C3i`。
  **本轮把口径分布重读了一遍**（`grep` 全插件）：已经是 `!= "projected"` 的有 6 处（`adjacency.inc:29`、
  `geom_metrics.inc:157`、`nearest.inc:37`、`overlap_check.inc:29`、`simplify.inc:26`、`topology_check.inc:29`），
  仍是 `== "geographic"` 的只剩 2 处（buffer、dissolve）⇒ 我上一版把这条写成"三档并存"时把 `simplify` 也算成一档，
  实际它是严格族的一员，**待收的只有两刀**，不是三处。
  **已修 `15f3ef1`**（两闸收成 `!= "projected"`，文案统一 `failProjectedRequired`；两条无 SRS 判别腿
  经"旧闸必红"双臂验证后两树 30/30）。⚠ 工单里那条"`VectorDissolveRejectsUnknownDissolveField` 夹具没挂 SRS"
  是**错的**（SRS 在它调用的 `buildDissolveTypeFixture`，`:4057` 有 `importFromEPSG(3857)`）⇒ 无需连带修复。
- **N136（测试基建 / 构建产物卫生）——已处置，翻转成因未查明**：Release 树全量里 `runtime_dll_cross_config_audit` 曾红，
  报 `build/release/plugins` 下 26 个"只属于 Debug 配置"的运行库残留（`Qt6Cored.dll`、`gdald.dll`、
  `opencv_*4d.dll`、`pcre2-*d.dll`、`proj_9_d.dll`、`tiffd.dll`、`zd.dll` 等）。**成因未查清，不写因果**：
  `dir /tc` 显示这些文件创建于 2026-10-14，但同日 11:44（C3j）与 13:0x（C3h-1）两轮 Release 全量里同一条审计都是绿的
  —— 同一批文件、同一条命令（`-V` 现读 `-DCONFIG=Release`、`-DPLUGINS_DIR=build/release/plugins`）、两种结论。
  两条互斥的可能都必须先取证：① 文件时间戳不可信（`file(COPY)`/`CopyFile` 会保留源时间戳，即它们是今天才被"搬进来"的）；
  ② 前两轮的绿是空转（例如目录/名单当时不满足扫描条件）。**在取证前不许按审计提示直接删文件**
  —— 残留名单里含 `gdal.dll` 这类 Release 侧运行库，删错会打断产物加载。
  影响面：只挡 Release 全量收口，不碰任何被测代码；C3i 的两条判据腿与判据族在两棵树各 30/30。
  **处置（2026-10-23 01:00）**：按审计自带的补救①，把 26 个文件**移动**到 `build/tmp/n136_quarantine/`（可原样搬回），
  重建 `plugin_vector` 后审计转绿；Release 三条真实腿（124.85 / 20.45 / 135.33 s）与随后整轮全量 **721/721（562.99 s）** 全绿
  ⇒ 这批 Debug 库对 Release 产物确实无用。**翻转成因仍未查明**，已排除：vcpkg 两侧 bin 目录今天未变（最新 mtime 仍是 10-14 16:24）；
  真实腿不写这些文件（跑腿前后 mtime 完全一致）；`copy_if_exists` 没有 debug 回退；两棵树都不是 junction。
  下一步（未做）：给审计加"每次运行把扫描清单/残留清单落盘"的日志，让下一次翻转自带证据。
- 挂账 **N131**（`vector rasterize`）：`attribute` 字段不存在时不校验（`vector_plugin_raster_convert.inc:72-78`），
  而初始化已经是 `band->Fill(0)` + `SetNoDataValue(0)`（`:63-65`）⇒ 产物整幅是背景 0，
  **用户无法区分"这一片本来就没有面"与"字段名写错"**；成功返回还不带任何 metadata 键（`:103-105`）⇒ 事后也查不出烧了什么。
  （GDAL 在该路径是否发 `CE_Warning` **未核**，发也没被采集。）
  **已修 `08d4a14`**（建输出前先校验字段存在，失败不留半成品，成功补 `raster_width/raster_height/source_feature_count/burn_field|burn_value`）。

另有四条**代码质量类**可疑点登记在工单里（`overlay.inc:285` 混合维路径每要素泄一条几何、
两处死代码、`buffer_clip.inc:53` 造成的 4 处死臂、`dissolve.inc:119/:138` 两处 `delete nullptr`），
归"冗余/一致性"顺手批，不与修复刀混批。
⚠ 上面这些行号是**登记时刻**的现值，`overlay.inc` 自 C3h-1 起长了 300+ 行 ⇒ 复用前必须重读，不许按名字外推。

C3h-1 复审又攒出五条同类项（同样归"冗余/一致性"批，行号为 2026-10-22 现读）：
`vector_plugin_overlay_support.inc:40` 的 `writeUnionFeatureOwned` **零调用者**（`grep -rn` 全仓只剩定义本身，
`src/` 与 `tests/` 合计命中 1 次）；`tests/test_plugins.cpp:4464` 写的 `UnionOutputPiece::source`
在 `findUnionPiece` 改按列名配对之后**没有读者**（`:4350`/`:4396` 那两处读的是夹具侧结构，不是这个）；
`vector_plugin_overlay.inc:386` 与 `:469` 两处 `progress.throwIfCancelled()` 位于 `srcSRS`（裸 `Clone()` 指针）
已分配之后 ⇒ 取消路径既泄 `srcSRS` 又不回滚已开的 GPKG 事务（**同一形态在 `difference`/`intersect` 里也存在，
处理时要按"整个 overlay 三动作"一并核，不是只补 union**）；
`mergeGeometryList`（`vector_plugin_overlay.inc:263-272`）把"过滤 null/空几何"的责任推给了唯一调用方，
头上注释只写"空集合返回 nullptr"、没写这条新前置条件；
`src/gui/gui_data_support.cpp:653` 的 union 动作描述仍是"合并输入要素与 overlay_vector 的重叠部分，输出并集要素"，
真并集 + 整表属性之后这句已偏（GUI 文案归阶段三文档同步批，且要同时看 `N111` 的前缀分流挂账）。

**执行顺序裁定（R-C3c-1）**：**已确诊缺陷的动作先修后测**——C3d 刚发生过"真实腿把缺陷产物形状钉成期望值
（`overlap_check_prov` 的 `-eq 0`），修好后那条断言必须跟着退役"，所以本轮不允许先写测试再修缺陷。

**修复进展与两处自我更正（2026-10-22）**：
- **N127 已修**（`fc68e12`）：`isSafeWhereClause` 改 UTF-8 感知（非法字节、悬挂 lead、overlong、`>U+10FFFF`、代理区仍拒），
  ASCII 白名单与危险关键字表逐条保留。控制器双臂复跑：`--where=name='北京'` ⇒ `Success: 1 features`；
  `--where=name='北京'; DROP TABLE x` ⇒ 仍 `Error`。下级另做了一刀我没要求的负面控制——
  真把实现改成"一刀切放行 `>=0x80`"重编一版 DLL，注入面用例如期变红 ⇒ 那条用例不是装饰。
- **N129 已修**（`91025ec`）、**N131 已修**（`08d4a14`）。
  ⚠ **我上面两处对症状的描述都错过，是下级开工时纠正、我复核后改写的**：
  N129 的真实症状不是"静默全部合并"，而是**产出 0 要素空图层却 `success=true`**
  （要素被推进 `allGeoms`，而写出段在非空字段分支只遍历 `groups`）；
  N131 也不是"静默成功"——字段不存在时 **GDAL 自己就报错** ⇒ `success=false`，真正的伤害是
  ①失败前输出 tif 已 `Create`+`Fill(0)` 落盘 ⇒ **留一张全 0 半成品**（0 恰是 NoData）、
  ②案文不点名参数、③成功路径零 metadata。
- 由这两处纠正又长出三条同族候选，已立案（细节与实测见工单 `Task C3c` 那张表）：
  **N132**——`attribute` 指向**存在但非数值**的字段时静默烧 0 且 `Success`（我做了字节级双臂实测：
  4×4 Float32 尾 64 字节，传字符串字段 ⇒ 16 个 `00000000`，不传 attribute 走 burn=1 ⇒ 16 个 `0000803f`）。
  **已修 `aa6391a`**（读数前先判字段类型，非数值直接 `Error: attribute field is not numeric: <字段名>`）；
  **N133**——`raster_inspect histogram`/`info` 把**常数栅格**判成"没有值域"（burn=1 那张全 1.0 的产物就报
  `Band has no value range`）。**已修 `e222594`**（常数栅格报 `bins:1 min:max:` 与真实像元数，只有整幅 NoData 才报"无有效像元"）；
  **N134**——`rasterize` 的 `layer` 不限制烧写范围；⚠ 我原先把这条的症状写成"其余图层一起烧"是**错的**，
  且我上一轮据此写下的"下级把 `source_feature_count` 按全层求和反而是对的"跟着一起作废——
  本机 GDAL 3.12.4 的 utils 版 `GDALRasterize` 在没有 `-l`/`-sql` 时要求源数据集只有一个图层（`apps/gdal_rasterize_lib.cpp:1005-1014`）
  ⇒ 真实症状是**多图层输入整条动作硬失败**，"全烧"这个前提从未成立，也就无所谓"求和口径对不对"。
  **已修 `9c02668`**（`GDALRasterizeLayers` 只递交点名层；`source_feature_count` 同步收紧为"只数被烧那一层"，
  层内要素数取不到缓存值时现读遍历、不许把 −1 当 0 写进元数据）。
- 还有一条**测试侧**的更正：`ctest -R` 的正则**不认 `\w`**——我写在任务书里的
  `-R '…(Dissolve|Rasterize|SpatialJoin)\w*…'` 两棵树都只收到 4 条 CoverageMeta，
  换成 `[A-Za-z0-9_]*` 才是 14 条 ⇒ 看起来"全绿"其实是"没测到"。
  同理，`docs/算法说明/矢量数据处理/融合.md` 原文把 N129 的旧行为写成"字段不存在 → 整体联合为一个结果"，
  **比实现还乐观**，已随修复按现读改写。⇒ 每条产品修复都要回头问一句：
  **有没有哪篇文档或哪条注释在替旧行为说话？**



（原文如下，保留以便对账：）`vector.interior_point` 没有履行
"点在面内"的语义 —— 它与 `vector.centroid` **走同一行实现**（都调 `normalized->Centroid(&point)`），
所以对凹形给出的是**面积重心**，可以落在形外。实测夹具：不对称 L 形 `(0,0)-(12,0)-(12,3)-(4,3)-(4,8)-(0,8)`
（底条 12×3、左条 4×5、总面积 56）⇒ 两动作都输出 `(3.2222, 3.2222)`，而该 L 形只在
`x∈[0,4]∪y∈[0,3]` 上有条带，点 `(3.2222, 3.2222)` 不在形内。
复算：`ctest --test-dir build/release -C Release -R "Vector(Centroid|InteriorPoint)Execution" -j1` 对照
`src/plugins/vector/` 里两处 `Centroid(&point)` 调用点。
后果面：用户按"内部点"取标注/放置点位时，点可能掉在面外（GIS 里 ` interior point` 的定义性要求就是在形内）。
正解候选：改用 GEOS `OGRGeometry::PointOnSurface()` / `Centroid` 与 `PointOnSurface` 二选一，
并按动作语义分开（`centroid`=重心、`interior_point`=形内点）。**属行为变更 ⇒ 需要用户裁定**，
本批不擅自改 `src/`；`interior_point` 的 GOLD 断言因此只钉"两种定义都唯一"的矩形夹具（中心 (5,2)），
不把"落在形外的值"当期望锁进库。

挂账 **N116**（复审第 6 条的残留，非 C2 引入）：已提交的批次 B 工单里有一处把细节只留在 gitignored 台账里
——`docs/superpowers/plans/2026-10-19-批次B设置迁移与构建测试补齐.md:747`"细节清单见
`.superpowers/sdd/<本计划目录>/task-B3-extra-obligations.md` 的 §A"。拿不到那个文件的人就丢了这份义务清单。
同文件 :525 与 :644 两处经核**不算违规**（一个是取证命令的落盘路径、一个显式写着"证据，不入库"）。
⇒ 归 C22/C24 文档批把 §A 内联进工单；本轮只登记不动（改历史工单要连着核对它引用的现状，不该塞进 fix round）。

挂账 **N115**（本轮控制器自己撞出来的产品缺陷，判据级严重）：**`raster_inspect info` 不是只读操作**。
它计算波段统计后，GDAL 会把结果写进输入文件旁边的 `<name>.aux.xml`。证据：本轮两批波段探针在 `data/`
  下若干栅格上跑过 `info` 之后，`find data -newermt 2026-10-19 -type f` 从 **0 变成 15**，逐条列出后
  **15 个全是 `.aux.xml` 边车**（20:47~20:50 落盘；新建还是更新已无法区分——`data/` 被 gitignore，没有基线）；同时核 `.tif/.tiff/.shp` 本体的 mtime 仍是 2014 / 2016 / 2024-06 / 2026-04，
**像元数据未被改动**，被写的是边车。后果有两层：① 对用户是"我给的只读数据被工具悄悄加了文件"，
与交付承诺冲突；② 对测试是纪律问题——阶段一要求"以真实数据出结果"，如果各批就地打开 `data/` 下的栅格，
每跑一次都在改用户目录，而且这个改动**不会被 git 看见**（`data/` 被 gitignore），所以不会有任何人发现。
⇒ 本轮起加硬规则（已写进工单 Global Constraints）：**测试与探针一律先把输入复制到 `build/tmp/` 再跑**，
`data/` 只允许被复制、不允许被就地打开；`find data -newermt <批次起始日> -type f | wc -l` 必须 = 0
作为每批收尾断言。修复面（归批次 C 之后的独立小批，或 C22 一并处理）：`info` 类只读路径应改为
不写 PAM 边车（`GDAL_PAM_ENABLED=NO` 或不调 `bForceCreate=true` 的统计缓存），并在用例里断言
"跑完 info 后输入目录不新增文件"。
⚠ 本轮不改代码：这条是我探针触发的既有行为，不是 C2 引入的回归；把它塞进 C2 提交会混淆两件事。
另需说明：本轮之前我报给用户的"`data/` 一字节未动（计数 = 0）"在当时是**真测过的**，
是这次波段探针之后才变成 15 的——所以这条也是对我自己"只读证明"纪律的修正：证明必须在最后一次触碰之后重取。

挂账 **N114**：批次 A 改过的 17 项算法问题里，有 **4 个动作今天仍是 `PENDING`**（登记表现为机器判定，
不是印象：`CoverageMeta.GoldEvidenceIsAuditable` 会回到用例块体核"剥完注释后是否仍有数值断言"）——
`cutting.tile`（Task 17/18）、`processing.pansharpen`（Task 13）、`raster_math.band_math`（Task 5）、
`vector.duplicate_point_check`（Task 20/A0）。这四处改当时都有"同一变异、旧绿新红"的判别证据，
但**动作级数值口径没被钉下来**（`terrain.watershed` 已 GOLD，`processing.watershed` 是另一个动作、仍 PENDING）
⇒ 批次 C 要给这四个补数值断言并把行改成 GOLD，同时把 `CoverageMeta.PendingCountDoesNotGrow` 的上限
从 72 往下调。

挂账 **N113**：`docs/算法说明/` 逐插件点数是 **125 篇 vs 126 个动作**，缺的一篇正是
`classification.accuracy_assessment`（批次 A Task 8 改过它的 PA/UA 算法，却没有对应文档；
`地物分类统计 / SVM 分类 / 随机森林分类 / 最大似然分类` 四篇都在）。
其余 120 篇的参数表与 `--help` 的机械核对**仍未做**，已逐条复验的只有漂移清单里的 D1~D5。
> **销账（2026-10-26 批次 C C4a/C4b/C4c）**：其余各篇的参数表已**全量**机械核对完（126 动作、594 行、
> 四个维度），缺的那一篇 `分类统计/精度评估.md` 已按 `doAccuracyAssessment` 现读代码补上，
> 且这条核对已落成 ctest 用例 `docs_match_cli`（豁免逐条带证据、用不上即报红）。
> 仍**未做完**的两件：参数表之外的正文与代码逐篇对撞（C4d）、`类型`/`说明`/`Options:` 三个维度进判据。

---

## 已知限制

- Windows 代码签名未配置（SmartScreen 可能提示）
- **交付目录不含 VC++ 运行库**：目标机需自行安装 VC++ Redistributable（N87）
- **只走 `cmake --install` 交付**（2026-10-21 决定：不出安装包、不做云端构建）；
  Debug 的 `cmake --install` 分支已由批次 B 的 B4 补齐，见"发布前验证 → 第 3 步"
- `classification` / `georef` 插件版本 0.1.0
- `PerformanceMonitor` 未接入主执行链；`Logger` 已接入 CLI/GUI 进度消息（任务中心日志仍为 GUI 主路径）
- 切片预览使用本地 Leaflet 资源（`share/leaflet`），不再依赖 CDN
- 单点视域的"被遮挡"与"超出最大距离"输出同值（0.0），在结果里不可区分；多点视域不做曲率修正（N44/N46 → 批次 B）
- 视域/全色锐化的范围校验存在已知空白：输入缺地理参考时得不到保护
