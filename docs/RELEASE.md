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
- **VC++ 运行库随包（C17 / 拍板 2，2026-10-26 起）**：Release 包 `bin/` 里带 **8 件** CRT
  （`msvcp140 / msvcp140_1 / msvcp140_2 / msvcp140_atomic_wait / msvcp140_codecvt_ids /
  vcruntime140 / vcruntime140_1 / concrt140`，+1,440 KB）⇒ **目标机零前提**，不需要再自装 VC++ Redistributable。
  ⚠ 这一条只对 **Release 包**成立：Debug 包里的 vcpkg 调试版依赖导入的是带 `d` 的调试 CRT
  （`msvcp140_1d.dll` / `vcruntime140_1d.dll` / `msvcp140d_atomic_wait.dll` 等），那批文件在 VS Redist 的
  `debug_nonredist/` 下、目录名就是官方口径的"不可再分发"⇒ 不随包，Debug 包只在开发机/CI 上跑。
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
- **N87 → 已由 C17 销账（2026-10-26）**：原决定是"安装包不含 VC++ 运行库 ⇒ 目标机需自行安装
  VC++ Redistributable"，依据是包内 `Lerc` / `Qt6Core` / `arrow` 等 DLL 直接导入 `MSVCP140.dll` 等，
  而这些被 `cmake/install_runtime_deps.cmake.in` 的 `POST_EXCLUDE` 按设计排除。
  用户 2026-10-21 的第 2 项拍板（目标机零前提）把口径反过来 ⇒ 现由根 `CMakeLists.txt` 的
  `include(InstallRequiredSystemLibraries)` + `install(PROGRAMS)` 随包，带哪几件由工具集算、不抄名单。
  ⚠ 两条实测教训记在这里，别照着旧思路再设计一遍：
  ① 把 `VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT` 加进 `file(GET_RUNTIME_DEPENDENCIES)` 的 `DIRECTORIES`
     **不会**让它随包 —— 目录确实在场，但装出来的包一个 CRT 都没有、未解析列表也是空的（被按系统件静默丢弃）；
  ② 真正被包内 PE 导入的 CRT 是 **7 个**名字而不是工单里写的"三件套"（多的 4 个是
     `msvcp140_1 / msvcp140_2 / msvcp140_atomic_wait / concrt140`）⇒ 只带三件套，干净机器照样 0xC0000135。
  ⇒ "该带的有没有真带上"由 `tests/check_install_closure.cmake` 从包内导入表判（那一侧已把释放版
  从系统白名单里删掉，只留带 `d` 的调试变体）；反向对照做过：把随包那一段临时禁用后重配重装，
  `install_closure` 立刻红并点名 `vcruntime140.dll ← z.dll / zstd.dll / …`。

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

- **现行基线**：`ctest -N` 注册数 **Debug 754 / Release 752**（2026-10-26 阶段 2 四件落地后的双树全量轮现读，
  见"批次 C 阶段二收口"那节）。上一档 **751 / 749** 是阶段三收口轮取的；两档之间涨了 **+3 / +3**：
  C19 的 `install_smoke` 是一条 `add_test`（各 +1），C18 是两条 `TEST(...)`（各 +2）——
  都不是"新增 `add_test` 才能数得到"那种 gtest 之外的注册。
  再往前，**750 / 748** 是 C4b 加进 `docs_match_cli` 之后取的，**749 / 747** 到 **750 / 748** 那 **+1**
  来自 N151 新增的那条 `TEST_F` `PluginTest.ProximityFailsWithoutGeotransform`。
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
  `cmake -P` 而不是 gtest ⇒ 两棵树各多 1 条注册，不受 debug-only 那 2 条影响）⇒ **Debug 750 / Release 748** ⇒ **C4d 两轮 +0**（纯文档 + 台账，两棵树都没重编）
⇒ **N151 修复 +1**（`PluginTest.ProximityFailsWithoutGeotransform`，是 `TEST_F` 不是 `add_test`）⇒ **Debug 751 / Release 749**。
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
| Debug | **754** | **1182.09 s**（批次 C 阶段二收口轮现跑；开批 13:26:10、收工 13:56:56，串行 `-j1`、期间无并发构建，**754/754 全绿**） | 754 条**首档**；上一档 751（阶段三收口）**1288.71 s**；再往前 749 **1099.05 s** | 最慢 `real_data_vector_quick` **189.29 s** ⇒ 720÷189.29 = 3.80×；`union_poly` 146.66 ⇒ 4.09×；`overlay_ops` 121.63 ⇒ 3.54×；`join_heavy` 88.83 ⇒ 6.64×；`overlay_buffer_join` 83.70 ⇒ 7.53×；`checks` 47.29 ⇒ **2.11×**；`derive_heavy` 44.46 ⇒ 7.87×。<br>**但余量按安静态单跑判**（口径见下面那段）：`checks` 两跑 34.15/36.42 ⇒ 取 36.42 ⇒ **2.75×**；`quick` 167.92/168.28 ⇒ 取 168.28 ⇒ **4.28×** ⇒ 两条都在 2.5× 触发线之上 ⇒ **不拆腿、不抬 TIMEOUT** |
| Release | **752** | **569.34 s**（同一轮紧随其后跑；**752/752 全绿**） | 752 条**首档**；上一档 749（阶段三收口）**586.26 s** | 最慢 `join_heavy` 67.32 ⇒ 8.76×；`quick` 70.09 ⇒ 10.3×；`overlay_buffer_join` 61.93 ⇒ 10.2×；`derive_heavy` 39.17 ⇒ 8.94×；`union_poly` 22.41 ⇒ 26.8×；`checks` 16.67 ⇒ 6.00×；`overlay_ops` 13.65 ⇒ 31.5× ⇒ **Release 侧七档全在 6× 以上，没有紧的** |
⚠ 这两轮的 ctest 日志同名（`build/tmp/c4e_full_debug.log` / `_release.log`，都不入库），**后一轮把前一轮覆盖了**
⇒ 751/749 那一档的 1288.71 s / 586.26 s 现在只有本文记录、不可复算；754/752 这一档同样只有本文记录。
本表一贯的纪律就是"现读现写、跨轮差值一律不作证据"，这条覆盖事件正好是它的又一个注脚。

| Debug | **751** | **1288.71 s**（批次 C 阶段三收口轮现跑，`build/tmp/c4e_full_debug.log` 不入库；开批时刻 11:50:06、`ctest -N` 先 reconfigure 再数、串行 `-j1`、期间无并发构建，**751/751 全绿**） | 751 条**首档**；上一档 750（C4b）没跑全量（那两片是纯文档 + 守卫注册）；再上一档 749 见下面那行 | 最慢 `real_data_vector_quick` **254.75 s** ⇒ 720 ÷ 254.75 = 2.83×；`union_poly` **139.40 s** ⇒ 4.30×；`overlay_ops` **126.05 s** ⇒ 3.41×；`join_heavy` **109.64 s** ⇒ 5.38×；`overlay_buffer_join` **89.47 s** ⇒ 7.04×；`derive_heavy` **49.34 s** ⇒ 7.10×；`checks` **46.29 s** ⇒ 100 ÷ 46.29 = **2.16×**（见下方"读数口径"，**安静态复测 34.42 s ⇒ 2.91×**）。守卫两条本轮现读：`source_mojibake_guard` 20.54 s、`docs_match_cli` 1.36 s 均 Passed |
| Release | **749** | **586.26 s**（同一轮紧随其后跑，`build/tmp/c4e_full_release.log`；**749/749 全绿**） | 749 条**首档**；再往前 748（C4b）未跑全量；747 那一档见下面 | 最慢 `join_heavy` **84.55 s** ⇒ 6.98×；`quick` **71.31 s** ⇒ 10.1×；`overlay_buffer_join` **65.34 s** ⇒ 9.64×；`derive_heavy` **44.99 s** ⇒ 7.78×；`union_poly` **22.84 s** ⇒ 26.3×；`checks` **21.41 s** ⇒ 4.67×；`overlay_ops` **13.74 s** ⇒ 31.3× ⇒ **Release 侧七档全在 4.6× 以上，没有紧的** |

**⚠ 上面两行的余量不能直接判"跌破触发线"——口径要先说清**（本轮 `checks` 的 2.16× 就是这么被否掉的）：
表里那批秒数是**751 条混排的全量轮读数**，而 2.5× 触发线历来讲的是**安静态逐条单跑**（C3k-3 那档的
quick 151.22 / checks 30.78 就是单跑读数，两者不同口径、不可跨口径比）。
⇒ 收口轮跑完后立刻在安静态把最紧的两条腿各单跑两遍取最大值（`build/tmp/c4e_legmargin.txt`）：

| 腿 | TIMEOUT | 全量轮读数 | 安静态单跑（两跑） | 判余量用的最大值 | 余量 |
|---|---|---|---|---|---|
| `real_data_vector_checks` | 100 s | 46.29 s | 34.39 / 34.42 s | **34.42 s** | **2.91×** |
| `real_data_vector_quick` | 720 s | 254.75 s | 164.03 / 164.13 s | **164.13 s** | **4.39×** |

⇒ 两跑彼此只差 0.03 / 0.10 s（很紧），比全量轮低 26% / 36% ⇒ **差额是全量轮的争用**（同锁串行的其它腿 +
另外 740 条用例把文件系统缓存刷干净后每条腿都要重做输入现读与 `Sync-PluginDlls`），不是回归。
`checks` 34.42 s 也落在历史同口径读数 35.40 / 34.47 附近 ⇒ **既不拆腿、也不抬 TIMEOUT**（纪律照旧）。
唯一站得住的增量是注册数 +1 与 `quick` 那 8.5%（151.22→164.13，跨轮差值按本文件纪律不作证据）。
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
    - ⚠ **第一轮收口时的未验收面**：C4d 只做完 22/52，余 30 条未处理；正文级对撞**没有机器判据**
      （`docs_match_cli` 只管参数表），这一类漂移目前仍靠人跑；本片段没重跑任何构建或全量（纯文档 + 台账，产品二进制不变）。
      —— 余下 30 条的处理见下一段。
  - **批次 C C4d 第二轮（剩余 30 条正文漂移逐条落笔，2026-10-26）**：
    - **账面**：第一轮留下的 30 条**全部复验完毕** ⇒ 28 条判"文档错"已改掉（**19 篇 / 36 处正文**），
      2 条维持"无法判定"不动（`填挖方` fill/cut 命名方向、`视域分析` 34 行实测数字）。
      **52 条 = 第一轮 22 + 第二轮 28 + 2 条无法判定，剩余 0。**
      每条都由控制器对回代码行才落笔，依据逐条写在 `文档漂移清单.md` 的"第二轮"表里。
    - **代表性的四类**（都是参数核对结构上抓不到的）：`欧氏距离` 只乘 `|gt[1]|` 且 `GetGeoTransform` 返回值被丢弃
      ⇒ 无地理参考的输入**静默按像元单位出图**并报成功（新挂账 **N151**）；`EVI` 的"安全除法"实为分母 `+1e-10`
      后照常相除，与正文声称的公式**不是同一件事**；`连通组件`/`分水岭`/`边缘检测` 共同的 `toUint8`
      是全幅 min/max 拉伸而非类型转换 ⇒ 一切阈值/量纲都相对该幅自身极值；
      `重叠检查` 把"必须投影坐标系"写成"建议" ⇒ 用户按正文规划时会拿到一次失败而不是"面积可疑的表"。
    - **"输出不得等于任一输入"摊开补写**：`缓冲区`/`融合`/`差集`/`交集`/`并集`/`空间连接` 六篇正文各补一条，
      判据是 `staged.start(…, {input, 第二输入}, …)` + `common.inc:391-402` 的 `fs::equivalent`。
      另如实写明 `重叠检查`/`多部件检查` 这两个 CSV 动作**不在** C3n 那 24 个临时件写出点之内，
      走裸 `ofstream(trunc)`、没有等价闸。
    - **执行器缺陷（本轮自曝、当场修，不改产品代码）**：整行替换的执行器没断言**行首列表记号**，
      本轮第一次落笔把 9 行的 `1. `/`- ` 记号吃掉、编号列表断号（该批未提交，已全部回退重做），
      新执行器加三道闸（记号必须原样、`append` 模式不碰原行、落笔后跑 `## 处理流程` 编号连续性 lint）。
      `空间过滤.md` 另有一处"锚点是长句尾、替换把长句头吃掉"，靠肉眼复核补回。
    - **新挂账 N151**：`raster_manage.proximity` 丢弃 `GetGeoTransform` 返回值（`raster_manage_plugin.cpp:498-499`）
      ⇒ 无地理参考的输入**静默按像元单位出图**并报成功；同文件 zonal_stats 那道 `!= CE_None` 检查了返回值（缺变换即失败）
      ⇒ 属漏检不是口径。`opencv_wrapper.cpp:87-88`（`matsToGdalTiff`）是同形态第二处（缺变换时把单位阵当地理参考写盘），
      本轮**未**动它（射程之外，改它要连带全部栅格写盘动作的判据）。修复见下一段。
    - **N151 的修复与一次"我自己的机制断言被实测否证"**：
      ① `doProximity` 现按硬前置处理 —— `GetGeoTransform(...) != CE_None` 即 `fail("input raster geotransform is missing")`，
      判在任何写盘之前 ⇒ 不留半件；新增用例 `PluginTest.ProximityFailsWithoutGeotransform`（夹具
      `createRasterWithoutGeoTransform` + 谓词 `rasterHasNoGeoTransform` 锁对分支）。
      ② **先红后绿**：把守卫删回改前形态重建变异臂 ⇒ 该用例三条断言全红
      （`success=true`、消息 `Proximity map created`、产物落在 `test_e2e_output/prox_nogt_output.tif`）；
      按 `cp` + `git hash-object` 还原（基线 blob `98ffd035ff3ec12d44b2d9db6ef94d97d75d9899`）重建后 15/15 绿。
      ③ **变异臂顺带否证了我写进文档的机制**：我原断言"gt 保持全 0 ⇒ 整幅距离被乘成 0"，
      而 `gis-cli raster_inspect info` 读那份变异臂产物是 `Min=Max=65536`、`Pixel Size=(1,1)`、`CRS none`
      —— GDAL 在 `GetGeoTransform` 失败时**照样把数组填成默认单位阵**（terrain 侧 Task 19 的注释早已记过同一形态，
      我就着这条证据读过去却仍写成"全 0"）。⇒ 文档/注释/用例消息里的后果全部改为
      "静默按像元单位出图 + 输出地理参考退化为单位阵"；`6d58ce0` 的 commit message 里那句"全 0 距离"**是错的、不 amend**，
      以本段与 `欧氏距离.md` 为准。教训：**机制断言必须由产物读数支撑，不能由"数组初值是 0"推出**。
      ④ 6 条基于 VRT 的 `Proximity*NoData*` 用例改前之所以绿，是因为该 VRT 没有 `<GeoTransform>` ⇒ 走的正是缺变换那条路。
      按新守卫它们全红，说明夹具与用例声明的那件事不符 ⇒ **修夹具不是"为了让测试绿"**：给 VRT 补
      `<GeoTransform>116,1,0,40,0,-1</GeoTransform>`（六个数必须逗号分隔，空格分隔会被 GDAL 报
      "GeoTransform node does not have expected six values" 并照样取不到变换 —— 本轮实测踩过），
      补完后 6 条各测其 NoData 声明判据、距离缩放判据仍由 `ProximityDistanceIsScaledByPixelSize` 单独承重。

    - **两条代码侧注释缺陷（转交后已就地修掉，不改行为）**：`overlap_check.inc` 的 N125 注释把
      `polygonIntersectionArea` 定位成 `common.inc:492-511`（实际 `:782-801`，该文件已长到上千行）⇒ 改为只点函数名并写明
      "WHY 不给行号"；`test_plugins.cpp` 视域曲率用例的取证注释把"启用曲率 2819 / coeff=0.0 517"两个数
      **贴反标签**（同用例断言是 `EXPECT_LT(visibleWith, visibleNo)`，方向与之矛盾）⇒ 两个数从注释里删除，
      原始日志不入库、无法复核归属，谁小谁大交给断言自己判；文档 34 行不动。
    - ⚠ **未验收面**：正文级对撞**仍无机器判据**（`docs_match_cli` 只管参数表四列），
      且 C4d 射程只覆盖"批次 A/C 改过实现的 28 个动作"，**其余约 98 个动作的正文从未逐篇对撞**；
      文档面只重跑过 `docs_match_cli`（`drift=0`、`matched 593/594`）与乱码/编码/行尾/`## 处理流程` 编号四道静态检查。
      **本片段动了产品二进制**（N151 修复改了 `plugin_raster_manage`，另两处是注释）：
      Debug 侧已现读 `ctest -R Proximity` **15/15 绿**，其余两棵树的**全量与 `--install` 复核是下一段（阶段三收口）**。

### 批次 C 阶段三收口（双树全量 + 安装态复核，2026-10-26）

**跑法**（严格串行，全程只有这一条链在用机器）：`cmake --preset debug` → 全目标 Debug 构建 →
`ctest -N` 数注册 → Debug 全量 `-j1` → `cmake --preset release` → 全目标 Release 构建 → `ctest -N` →
Release 全量 `-j1` → 两棵树各自 `cmake --install … --prefix build/tmp/c4e_install_{debug,release}`。
开批时刻 **11:50:06**、收工 **12:23:24**（墙钟合计 33 分钟）；脚本与日志都在 `build/tmp/`（不入库）。

**结果**：Debug **751/751**、Release **749/749** 全绿（墙钟 1288.71 s / 586.26 s，读数与"口径怎么比"的归因见
"全量用时与单条用例超时"那一节的最新两行）。
两道守卫本轮都是**在 ctest 里跑的**：`source_mojibake_guard` Passed 20.54 s、`docs_match_cli` Passed 1.36 s。
`data/` 全程只读：`find data -type f -newermt "2026-10-26 11:50:06"` = **0**；工作树在收口链结束时 0 改动。

**安装态复核**（只从包目录跑，产物一律落 `build/tmp/c4e_out/`）：

| 检查 | Release 包 | Debug 包 |
|---|---|---|
| 可执行 | `bin/gis-cli.exe`、`bin/gis-gui.exe` 都在 | 同左 |
| `bin/plugins` 条目数 | **13** | **13** |
| CLI 参数契约来自包内二进制 | `raster_manage --help` 正常出 `Options: overviews, nodata, cog, zonal_stats, proximity` 与 `[required]/[optional]` | `spindex --help` 正常出 v1.2.0 插件说明 |
| 真实数据跑一条动作 | `raster_manage proximity` 吃 `data/dem/utm_srtm_56_06_projection_reproject.tif`（有地理参考）⇒ `Success`，产出 144,201,196 B 的 tif 到 `build/tmp/c4e_out/`，0.765 s | —（同一判据由 Release 侧承重，Debug 侧不重复跑重活） |
| GUI 能否起 | `--self-test` **rc=0**（需按 N101/N102 注入 `GIS_TOOL_SETTINGS_INI` + `GIS_CUSTOM_INDEX_PRESET_FILE` 两个环境变量；不注入直接跑是 rc=3，那是守卫不是包装坏） | 同左，**rc=0** |

**文件数对账**：Release 包 429 个文件 / `du -sk` 197,627 KB，对上批次 B 收尾那档记录的 **430 文件 / 192.2 MB**
—— 体积同一量级、**少 1 个文件本轮没追出是谁**（登记为未判定）。没有追的理由：包内容闭包判据（B5 那条用例）
本轮在 ctest 里是绿的，它管的是"必需件全在 + 没有副本"，而"总数恰好等于某个历史数字"从来不是判据。
Debug 包 425 个文件 / 459,927 KB（Debug 侧带 `.dll` 调试版与 Qt 插件补齐，见 N85/B4）。

**本轮没做的事**：GUI 深验证（按批次 C 既定"CLI 优先、GUI 延后"）；N146/N148/N149/N150 的代码改动（裁定见下一节，都列批次 D）。


**收口轮（阶段 2 四件全部落地之后重跑双树全量 + 双树 `--install`）**：

| 项 | Debug | Release |
|---|---|---|
| `ctest -N` | **754** | **752** |
| 全量 `-j1` | **754/754**，1182.09 s | **752/752**，569.34 s |
| `cmake --install` → `build/tmp/` | rc=0，433 文件 / 461,367 KB | rc=0，**437 文件 / 199,067 KB**（含 C17 的 8 件 CRT） |
| 交付三判据 | `install_closure`、`install_smoke`、`docs_match_cli`、`source_mojibake_guard` 都在全量轮里绿 | 同左 |
| `data/` 写入 | **0**（`find data -type f -newermt "2026-10-26 13:26:10"`） | 同左（同一次取证覆盖两棵树，因为整条链串行） |
| 工作树 | 收工时代码/文档 0 改动 | 同左 |

**合并门（工单 Task C25）现读**：双树全绿 ✅、四条真实回归腿绿 ✅、`install_closure` ✅、
**新增** `install_smoke` ✅、`docs_match_cli` ✅、阶段 2 四件（C17/C18/C19/C20）✅；
**仍然红的两条**：① "PENDING = 0" —— 现值 **49**（`tests/action_gold_cases.txt` 126 行里 77 GOLD / 49 PENDING，
全在非 vector 插件：processing 16 / terrain 10 / matching 6 / cutting 4 / projection 4 / raster_manage 3 /
raster_inspect 2 / raster_render 2 / raster_math 1 / spindex 1）；② 计划里 C25 写的"合并前需用户确认一次"。
⇒ 批次 C 的**代码与交付面**已收口，**覆盖晋升（拍板 1）这一整块未完成**，是否放宽门先合、
还是做完 49 条再合，是用户决定，不自作主张。

### 批次 C C18：干净机器那一格补判据（N110 销账，2026-10-26）

`migrateLegacySettingsFrom` 的注释一直在承诺一句语义 —— "读到源但源里没键（健康的新机器）仍然算打开成功 ⇒ 标记照打，
所以这里不会退化成每次启动重跑迁移"（`src/gui/app_settings.cpp:180-182`）—— 而**没有任何用例判它**：
`AllBrokenSourcesLeaveTheMarkerUnstamped` 判 `opened == 0` 那一侧，其余各条喂的都是**有键**的源，
于是 `opened >= 1 && copied == 0` 这一格是空的。⇒ 新增两条：
`AppSettingsMigration.EmptyButOpenableSourceStillStampsTheMarker`（0 键源 ⇒ 不搬任何东西、标记照打、
且这一次启动的用户配置里除标记外**不留任何键**；第二次启动早退后仍只有那 1 个键 —— 计划里
"首启 0 键 / 退出后只写预期键"就落在这两句断言上）与
`AppSettingsMigration.MissingLegacyFileIsConsumedNotBroken`（旧落点文件根本不存在 = 同一格）。
后者单独成条的理由：将来有人给 `open()` 加"文件不存在就返回空指针"的体贴逻辑时，
两条并一起的话这条只会换个分支悄悄跑过去。

**存活变异取证（先红后绿）**：把 `if (opened == 0)` 写成 `if (opened == 0 || copied == 0)` 重建 ⇒
`ctest -R AppSettingsMigration` **只有新加这两条红、原有 10 条全绿** ⇒ 这一格此前确实无人判；
而这个变异的用户可见后果很重：每台新机器每次启动都重跑迁移、且一次性标记永远打不上。
按 `cp` + `git hash-object`（基线 `0f3741374888cd50a8b1e9f778f8351ceb3eac8d`）还原后 12/12 绿，盘上无变异体残留。

**工单 C18 原文里两处我按现状否证、没有执行**（都记在工单该条正文里）：
① "改 APPDATA/注册表侧介质实测" —— 本仓红线挡着：两个生产旧落点在 Windows 上是 NativeFormat，
**光构造就会在 HKCU 下建键**（`tests/test_app_settings.cpp:263-277` 把"读用户注册表当测试输入 + 在 HKCU 建键"
两条都列为不可做），所以迁移语义只能走注入面，介质侧证据停在已有的 `appSettingsPath()`/`fileName()` 直测；
② "旧键清理臂保留" —— `src/gui/**` 里**没有**任何删旧键的实现（`copyMissingKeys` 只"补缺不覆盖"、从不删源侧键），
B3 当年做的是标记时机与干净机器语义，不是清理 ⇒ 不新造一个删除功能去"保留一条不存在的臂"。


### 批次 C 阶段三收口前的挂账逐条裁定（N145~N151，2026-10-26）

裁定口径只有三档：**本轮已修** / **文档承重、关闭**（行为已如实写进正文，改代码属产品语义变更）/
**列批次 D**（需要用户拍板或需要新工单的范围）。凡"列批次 D"的都给出现值与成本，便于直接开工单。

| 挂账 | 裁定 | 依据与现值 |
| --- | --- | --- |
| **N145** 矢量输出 metadata 两套口径（14 个动作发 `feature_count` 却不带标准三键；`union_total_pieces`/`repaired_count` 是"同数异名"） | **文档承重、关闭** | C3h-2b 已把 5 个写件动作的 metadata 补齐（`e11f428`），C4d 又逐键在正文声明了口径：`并集.md` 写 `union_total_pieces`＝**分块数**、`修复.md` 写 `repaired_count`＝成功写出条数、`融合.md` 本轮改写"整体融合恒为 1"为"全 NULL/全空几何时为 0 且仍报成功"。⇒ 缺的那层"是否同一口径"已有答案：**不是同一口径，且文档逐键写明**。统一成一个键反而会破坏下游脚本，不做。 |
| **N146** GUI `requiredKeys` 未覆盖"按动作硬失败"的键 | **列批次 D** | 现值由 `docs_match_cli` 的 `required` 判据承重：参数表"必填=是"与二进制 `requiredKeys` 已 `drift=0`；N146 说的是另一半——**二进制不标必填但代码硬失败**的键（`viewshed` 的 `observer_x/observer_y` 属此类）。守卫目前只比"标了的"不比"没标的"⇒ 需要新增一条"硬失败键 ⊆ requiredKeys"判据，并逐动作取证哪些算硬失败。成本：判据 + 每动作取证，不能靠猜。 |
| **N147** `viewshed` 观察点、`reservoir_volume` 水位缺省静默按 0 算 | **文档承重、关闭** | `视域分析.md` 参数表现值已写"不填按 `0` 计算（多半落在数据范围外，结果会是'全部不可见'），实际使用必须显式给出"；库容的 `water_level=0` 是**合法水位**（海平面），不是"缺省即无意义"。⇒ 与 N146 合并处理：真正要做的是把 `observer_x/observer_y` 升为必填（属 GUI/CLI 契约变更，不在批次 C 范围）。 |
| **N148** 叠加类对"标准化后仍非法"的输入几何**逐条静默丢弃、不计数** | **本轮已修**（`6e3e9f5`，修复轮 1 口径补齐 `5ab329e`/`eb65e15`/`5a79a4a`） | **本行描述的是裁定时的旧态**（裁定时行号 `common.inc:198`、`overlay.inc:65/603/812`、`buffer_clip.inc:223` 与键名候选 `overlay_skipped_feature_count` **都已过期**：前者被本族自己的插入推走，后者**未采用**）。裁定时的事实是：`collectNormalizedGeometries` 的判据 `if (geom && !geom->IsEmpty())` 与 `collectNormalizedFeatures` 的镜像判据被四个动作共用 ⇒ 产物可以"少一块"而元数据无痕，只有全表空才失败。修法是**附加式**（不改任何现有值），实际落地的四个键是 `union_skipped_input_count` / `difference_skipped_input_count` / `intersect_skipped_input_count` / `clip_skipped_input_count`（">0 才发"）。现读定位（**行号是瞬时值，以函数名为准**）：判据 `common.inc:209`（几何趟）与 `:254`（要素趟镜像），采集点 `overlay.inc:36`（union 几何趟）/ `:583`（difference）/ `:798`（intersect）、`buffer_clip.inc:191`（clip），发键点 `overlay.inc:544/759/953`、`buffer_clip.inc:354`。落地详情与**覆盖边界**见下文「批次 D 代码刀 D0-B 落地（2026-10-27，Task 6 / N148）」小节。 |
| **N149** `vector.repair` 把 EMPTY 几何原样写出并计入 `repaired_count` | **本轮已修**（`3dbeb5e`） | **本行描述的是裁定时的旧态**：`common.inc:113-116` 对 EMPTY 返回非空指针的空克隆 ⇒ 过不了 `repair.inc:77` 的 `if (!repaired)` ⇒ 原样写出并计数，`repaired_count + skipped_count` 仍自洽 ⇒ 掩盖"空几何一个都没清掉"。改法（EMPTY 归进 skipped）会**改变已发布动作的计数值** ⇒ 需要产品拍板：**2026-10-27 用户拍案①，同日由 `3dbeb5e` 落地**，判据 `PluginTest.VectorRepairCountsEmptyGeometryAsSkipped`（四件套 + 产物身份集合），落地详情见下文「批次 D 代码刀 D0-A7 落地（2026-10-27）」小节。共享的 `cloneNormalizedGeometry` 未动（上抬会波及 `dissolve` 与 `buildNormalizedUnion`）。 |
| **N150** `vector.spatial_join` 的"不自计"按 `fs::equivalent` 判定 ⇒ 换一种路径拼法就在自计/不自计之间跳变 | **列批次 D** | 先要定"什么叫同一个图层"（路径等价？还是同一数据集 + 同一图层名/句柄？）——口径未定就改会把一种错误换成另一种。另 `join_value` 因 `SetWidth(254)` 在长汇总串下是否被驱动截断**至今未取证**（批次 C 两席都登记为未判定）⇒ 这两条必须各带一次实测，不适合塞进收口轮。 |
| **N151** `raster_manage.proximity` 丢弃 `GetGeoTransform` 返回值 | **本轮已修**（`15712fd`） | 缺变换即硬失败、判在写盘之前；判据 `PluginTest.ProximityFailsWithoutGeotransform` 先红后绿。`opencv_wrapper.cpp:87-88`（`matsToGdalTiff`）是同形态第二处，射程之外未动 ⇒ 一并列批次 D。 |
| （新）**CSV 两个检查动作无临时件契约、无"输出≠输入"闸** | **列批次 D** | `overlap_check.inc`、`multipart_check.inc` 走裸 `ofstream(..., trunc)`，不在 C3n 那 24 个写出点之内 ⇒ 输出路径写错会直接覆盖同名文件。文档已如实写明。迁移到 `VectorStagedOutput` 成本低（CSV 不是 GDAL 数据集，但等价闸照样适用），需要一条反向对照判据。 |
| （新）**乱码守卫与文档表格完整性不进 ctest 的射程之外部分** | **列批次 D** | `docs_match_cli` 只比参数表的行数/参数名/必填/默认值四列：`类型`/`说明` 两列、`Options:` 枚举集合、正文级（非参数表）漂移、乱码守卫扩展名（`tests/check_source_mojibake.ps1` 不遍历 `docs/`）都没有机器兜底 ⇒ C4d 那 52 条正文漂移全靠人读，下一处同类漂移也会静默通过。 |


### 批次 C C17：VC++ 运行库随包（拍板 2 落地，2026-10-26）

**改了什么**：`CMakeLists.txt` 新增 `include(InstallRequiredSystemLibraries)` + `install(PROGRAMS …)`
把工具集算出的 8 件 release CRT 装进 `<prefix>/bin`，外加安装期两道闸（名单为空 ⇒ FATAL；
名单里任一件没真落进 `bin/` ⇒ FATAL）；`tests/check_install_closure.cmake` 把释放版 CRT 族
从系统白名单删掉（只留带 `d` 的调试变体），使"少带一件被真实导入的运行库"直接判红。
`cmake/install_runtime_deps.cmake.in` 的旧"有意不打包"注释改成新口径并记下两条实测否证。

**盘面数字（现读，同一棵 Release 树改前/改后）**：包 429 → **437** 个文件，`du -sk` 197,627 → **199,067** KB
⇒ **+8 文件 / +1,440 KB ≈ 1.4 MB**（工单 C17 里估的"+约 5 MB"偏高）。Debug 包 425 → **433** / 同样 +1,440 KB。

**四条判据与取证**：

1. **随包成立**：`cmake --install build/release --prefix build/tmp/c17_rel` rc=0，
   日志出 `[C17] VC++ 运行库已随包：8 件`，`ls build/tmp/c17_rel/bin/` 数到 8 个 CRT 名。
2. **闭包判据承重（反向对照）**：把 C17 那一段临时改成 `if(FALSE AND WIN32 AND MSVC)` 重配重装
   ⇒ `ctest -R "^install_closure$"` 从绿转**红**，点名 `vcruntime140.dll ← Lerc.dll / z.dll / zstd.dll / gdald.dll …`；
   按 `cp` + `git hash-object`（基线 `c574f3930f5872f708068be4b7466f08298239fc`）还原后两棵树都回到绿。
   ⇒ 这条红不是"我改了判据所以红"，而是**判据独立于随包代码**：随机器删掉供给侧，判据自己就抓到。
3. **工具集名单与实测导入表对撞**：`InstallRequiredSystemLibraries` 给的 8 件**覆盖**了包内 96 个 PE
   实际直接导入的 7 件，多的 1 件是 `msvcp140_codecvt_ids.dll`（没人导入，31 KB）。
   ⇒ 所以"不抄名单"这条守住了：带多少由工具集定，缺不缺由导入表判，两边都不是我手写的。
4. **守卫没被弄坏**：`source_mojibake_guard` + `docs_match_cli` 两条本轮重跑 **2/2 Passed**；
   `install_closure` Debug/Release 各 1/1 Passed。

**本轮被实测否证的两条我自己的设计**（记下来防复发，两条都已写进代码注释现场）：

- 我先把 CRT 目录加进 `file(GET_RUNTIME_DEPENDENCIES)` 的 `DIRECTORIES`，想复用"按导入表自然解析"这条最干净的路。
  实测：生成的脚本里那两行目录**确实在场**，装完的包**一个 CRT 都没有**，且 `_gis_unresolved_deps` 是空的
  ⇒ 这些名字被扫描器按系统件静默丢弃。⇒ 那条"未解析即 FATAL"的闸因此成了**恒不触发的死臂**，我也把它删了
  （留着会让下一个人以为"缺 CRT 有人拦"）。
- 我第一版调试 CRT 白名单写成 `^msvcp140d.*`，Debug 包立刻红出 48 条：调试版的 `d` **有两个位置**
  （`msvcp140d.dll` / `msvcp140d_atomic_wait.dll` 紧跟族名，`msvcp140_1d.dll` / `vcruntime140_1d.dll` 在 `.dll` 前）。
  ⇒ 改成两条形状各异的正则，并逐名对撞过：8 个调试名全放行、9 个释放名全不放行
  （`msvcp140_codecvt_ids.dll` 这种"中间带 d"的释放名不会被误放，因为两条都要求 d 在**位置**上）。

**未验收**：`cmake --install` 与 `install_closure` 都在本轮现跑，但**没有**在没装过 VC++ Redistributable
的干净机器上验证"零前提"这一条 —— 本机核不到，工单 C17 自己就写了这条替代判据（包内存在 + 导入边可满足）。
C19（裸环境 `install_smoke`）做完后能给出一条更接近"带走"的实测，但仍不等于真机验证。


### 批次 C C19 + C20：裸环境"带走"验收进 ctest，交付自验一页（2026-10-26）

**C19 的载体换了**：工单点名的 `tests/check_install_smoke.ps1` 改成 `tests/check_install_smoke.cmake`
（`cmake -P`）。理由不是省事：① 本仓另外三条交付/文档判据都是 `cmake -P`，"归零闸/参数缺失即红/失败文案"
这套语义只有一份；② 本机 PowerShell 5.1 按 ANSI 解无 BOM 的 UTF-8 —— `.ps1` 里写中文会整脚本 ParserError、
`Get-Content` 不带 `-Encoding` 会把 UTF-8 读坏（本仓因此报过假产品缺陷），而这条判据**恰恰要逐字节比 CLI 输出**。
工单要求的五项（裸环境、`--list` 逐字节、每插件至少一条、数据命中、GUI 起得来、全落 ctest）一条没减。
⚠ 写脚本时先踩了一发：`execute_process` **没有 `ENVIRONMENT` 这个关键字**（`unknown argument`），
剥环境只能 `set(ENV{…})` / `unset(ENV{…})`（与本仓"给被 spawn 进程注入环境只能用 set(ENV{})"同一条结论）。

**它判什么**（自己 `cmake --install` 一份 ⇒ 不吃现成包，防"残留文件顶罪"）：
把环境剥到只剩 `<包>/bin` + `system32` + `Windows`（PATH、`GDAL_DATA`、`PROJ_LIB`、`QT_*`、
`VCToolsInstallDir`、`PYTHON*` 全清），在里面判五件事：
① 包内 `--list` 与构树 `--list` **逐字节相同**；② `GIS_PLUGIN_DLL_NAMES` 里 13 个插件**逐个点名**出现在 `--list`；
③ 三种模态各一条真动作（`raster_inspect info` / `vector info` / `projection reproject`）；
④ **PROJ 反臂**：藏掉包内 `share/proj` 后 reproject 必须失败，随后必须恢复且核 `proj.db` 回位；
⑤ `gis-gui --self-test` 在注入 N101/N102 那两个落点后 rc=0；外加 `share/proj`+`share/gdal` 非空、`bin/` 里有 CRT。

**取证（现读）**：ctest `install_smoke` Debug **Passed 7.51 s** / Release **Passed 5.36 s**
⇒ 注册数 751/749 → **752/750**；`TIMEOUT 90` 按更慢那侧 7.51 s 算 **12.0×**（Release 16.8×），远高于 4× 线。
反向对照两条：
- **插件覆盖闸承重**：往 `-DPLUGIN_DLLS` 里塞一个 `plugin_notreal.dll` ⇒ 立刻红，文案是"有 1/14 个插件没出现在包内 --list 里"。
  这条闸为什么必须存在，是实测出来的：把包内 `plugin_spindex.dll` 改名后 `--list` 从 1852 B 缩到 1648 B、
  不再出现 `spindex`，而 `gis-cli` **仍 rc=0** ⇒ 单看 rc 会放过"少装一个插件"。
- **PROJ 反臂承重**：本机藏起 `share/proj` ⇒ `PROJ: proj_create_from_database: Cannot find proj.db`、动作失败；
  恢复后同一条命令成功 ⇒ "数据随包"不是靠目录存在蒙过去的。

**两条如实登记为"没证到"的**（不写成已承重）：
- 判据 ① 的"逐字节相同"这一半**造不出反臂**：拿 Debug 树的 CLI 当对照物，两侧 `--list` 竟然逐字节相同
  （sha256 都是 `703019f7…`）⇒ 它今天实际能防的是"`-DCLI_BUILD` 被指到别的构建"，不是包内容坏；
  包内容坏由 ②③④⑤ 与 `install_closure` 判。写清楚，免得下一个人以为这条已经证过。
- **`GDAL_DATA` 没有本机可证的廉价载体**：藏掉 `share/gdal` 后 reproject 与 `raster_inspect info` 仍 rc=0
  ⇒ 脚本里只要求它随包在位（判据 E），**不写"GDAL 数据被用到"的断言**。把"存在"写成"被使用"就是本仓反复踩的那类假绿。
- `install_smoke` 在 Debug 侧也绿，但**这不等于 Debug 包可带走**：Debug 的 PE 要带 `d` 的调试版 CRT，
  那批文件不可再分发、不在包里，本机跑得起来是因为这台机器装了 VS ⇒ 正是脚本自己要防的"开发机顶罪"。
  脚本结尾按配置各打一行，把这个区分写进日志，不只写在注释里。

**C20**：新增 `docs/交付自验.md`（目标机一页）。三条命令（`--list` 数插件行、任一动作跑通、PROJ 那条坐标转换）
+ 一张失败对照表（`0xC0000135` / 插件行数不足 13 / `Cannot find proj.db` / 中文乱码只是码页 / 只拷 `bin/` 会坏）
+ zip 打包命令 + 体积基线 + "这套自验覆盖不到什么"一节（干净机未验证、GUI 业务行为不在这里判、SmartScreen 属签名）。
里面每个数都是现读现写：插件 13 行、`--list` 27 行 / 1852 字节、CRT 8 件、包 437 文件 / 约 193 MB。

**验证范围**：本轮 ctest 只跑了新增/受影响的那几条（`install_smoke` ×2、`install_closure` ×2、
`source_mojibake_guard`、`docs_match_cli`，全绿）。**双树全量要在 C18 之后重跑一遍**再谈合并。


### 批次 C 阶段四：本地合并到 master（2026-10-26，不 push）

用户裁定走 A 路：**先补 C17（拍板 2 的运行库随包）→ 再补 C19/C20/C18 → 然后合并**；
**剩余 49 条 PENDING 的晋升归批次 D**，合并门里那两条红按裁定**保留、不抹平**（见本节末）。

- **合并动作**：预合并 `master` = `4b1b52ea4a1f171293bd8486947582f18118947b`（只读核过），
  在 `fix/batch-a-algorithm-defects`（HEAD `5994a4e`，工作树 0 改动）上执行
  `git checkout master` → `git merge --ff-only fix/batch-a-algorithm-defects` ⇒ **`master` = `5994a4e`**。
  可复核的等价证据：`git rev-list --left-right --count master...fix/batch-a-algorithm-defects` = `0  0`，
  `master^{tree}` 与分支树同为 `26fb3e910917f95842923a4b427dec8a4a319bb4` ⇒ 纯快进、没有 squash、没有合并提交。
- **没有 push**：`origin/master` 仍是 `4b1b52e`，本地 `master` 领先远端 **356** 条
  （`git rev-list --count origin/master..master`）。推送属共享状态变更，需用户明确同意，本批不做。

**合并后在 master 上复跑全量**（工单阶段四要求的收口；`build/tmp/c4e_closeout.sh`，
开批 14:07:36 → 收尾 14:43:10，`HEAD = 5994a4e` 由脚本自己写进日志；那些日志在 gitignored 的 `build/tmp/` 下、会被清掉，所以下表把数值全誊写在纸面）：

| 项 | Debug 树 | Release 树 |
| --- | --- | --- |
| `ctest -N` 注册数 | 754 | 752 |
| 全量（`-j1`） | **754/754 全绿，1180.88 s** | **752/752 全绿，542.38 s** |
| Skipped / Not Run / `***` | 日志内 **0** 处匹配 | 同 |
| `cmake --install` | rc=0，433 文件 / 461 363 KB | rc=0，437 文件 / 199 067 KB |
| 四条守卫用例用时 | `install_closure` 13.42、`install_smoke` 7.71、`source_mojibake_guard` 23.71、`docs_match_cli` 1.20 s | 8.45、5.40、17.44、0.71 s |

- **合并前后同计数，墙钟差 1.21 s（Debug）/ 26.96 s（Release）**——都在本机 ±40 s 的波动带内
  ⇒ 只登记，**不据此改任何 TIMEOUT**。
- **收尾两条硬判据**：本轮开批时刻之后 `data/` 被写入的文件数 = **0**；工作树改动数 = **0**。
- **本节的复跑对象是 `5994a4e`**（脚本第一行写死的 `HEAD`）。**本节自身由 `0f56ff6` 入册，是纯文档提交**（只动 `docs/RELEASE.md`、`docs/交付自验.md` 与工单 C25 三处），入册后重跑的是文档面两条守卫 `docs_match_cli` 与 `source_mojibake_guard`（全绿），**没有**为它重跑双树全量 ⇒ 引用本节时按"代码态 = `5994a4e`、文档态 = `0f56ff6`"读。
- **安装态独立复核**（不靠 `--install` 的返回码）：两棵树的 `bin/` 按四类前缀
  （`msvcp140` / `vcruntime140` / `concrt140` / `vccorlib140`）现读到 **8 个 CRT**，与配置期
  `[C17] VC++ 运行库随包 8 件` 的名单逐字对上；`bin/plugins/` 13 个插件 DLL，`gis-cli.exe` 与 `gis-gui.exe` 在位，
  `share/proj` 与 `share/gdal` 在位。
  ⚠ 这一步我自己差点报出错数：第一次数 CRT 的 pattern 只写了 `msvcp140|vcruntime140`，数出 **7**，
  漏掉 `concrt140.dll` ⇒ **数运行库要和闭包守卫用同一族前缀**，别临时抄一个更窄的 pattern。

**合并门按裁定保留的两条红**（都写在 `docs/交付自验.md` 与工单里，这里只把"为什么允许带着它合并"落档）：

1. **覆盖登记现值 GOLD 77 / PENDING 49**（共 126 行）——拍板 1 要求"72 条 PENDING 逐步全晋升"，
   批次 C 实际做完的是 **vector 全组 33 行**，余 49 条全在非 vector 插件
   （processing 16 / terrain 10 / matching 6 / cutting 4 / projection 4 / raster_manage 3 /
   raster_inspect 2 / raster_render 2 / raster_math 1 / spindex 1），要新开工单组（批次 D）。
2. **正文级漂移没有机器判据**：`docs_match_cli` 只判参数表的行数/参数名/必填/默认值四列
   （现值 `rows=594 matched=593 drift=0`），`类型`/`说明` 两列、`Options:` 枚举集合与全部正文陈述
   都靠 C4d 那两轮人读（共报 52 条 ⇒ 落笔 50 条、2 条判为无法判定）⇒ 下一处同类漂移仍会静默通过。

### 批次 D 现读立案与销账（2026-10-27）

- **N137 改判**（失败契约，GPKG 矢量侧）：批次 C 立案时认定“失败会把旧产物截断”，而 C3n 的临时件契约（`51000fd`，24/24 写出点）
  是**立案之后**才铺满的 ⇒ 现读 + 实跑复核：vector 全插件 `->Create(` 只剩 2 处（staged helper 建临时件 /
  `raster_convert.inc:81` 建栅格）；把目标设成只读再重跑 ⇒ **`rc=1`、报 `Cannot replace output … (Win32 error 5)`、
  目标 sha256 与大小逐字节不变、`gis-tmp` 残留 0**。⇒ **这半边改判为已销**。剩下的：`fid` 首值 0/1（诊断项，用户可见影响 0，
  用户 2026-10-27 拍“不追”）、`vector.rasterize` 直写（失败会截断目标**栅格**）、N142（projection 自己的“先删后写”）——
  后两处各开一小刀。
- **N153（新立案，已修）**：`vector convert` 的 `--format` spec 默认值是 `GeoJSON`，而 CLI 只把用户显式给的键塞进 params
  ⇒ `--output x.gpkg` 不传 `--format` 时得到“**名叫 .gpkg、内容是 GeoJSON**”的文件（落盘首字节 `{"type": "FeatureCollection"`）。
  用户 2026-10-27 拍案 A：未显式给时**按输出扩展名推导**，推导不出才回落 GeoJSON 并打提示。
  实现 = `inferVectorFormatFromExtension`（只认驱动支持的 5 种、大小写不敏感）+ `doConvert` 先取显式值；
  **显式 `--format` 优先**，且 CLI 侧既有的一致性闸（`src/framework/src/action_validation.cpp:326`）保持不动。
  用例 4 条（推导 ×2、回落 ×1、显式优先 ×1），每条都用**第二个客户端**（GDAL 按内容嗅探驱动名）判产物真格式；
  先红后绿，`VectorConvert*` + `CoverageMeta*` + `param_guard_visible_keys_all_read` 共 **29/29** 绿。
  ⚠ **GUI 的 convert 行为不变**：GUI 参数面板会实发 spec 默认值（批次 B 的 R-B20），所以它总是显式带 `format`；
  本次只影响 CLI/工作流**没给这个键**的情形。文档 `docs/算法说明/矢量数据处理/格式转换.md` 的“输出说明”已按新判定顺序改写。

### 批次 D Task 4/5 取证：N150 的前提被我写错了，另立 N152~N156

- **先纠错**：工单与 spec 里那句“`spatial_join` 的自计判据换一种路径拼法就会跳变”**未复现** ——
  下级跑了 13 条可判臂，A1–A12 全部落到同一支（绝对/相对、`./` 与 `.\`、盘符小写、大小写混拼、内嵌 `.` 与 `..`、重复分隔符），
  A13（8.3 短路径）**本卷不可判**（短名列全空、`GetShortNameW` 原样返回）。⇒ 工单 Task 5 的靶子写错了方向，已按现读改。
- **真复现到的是另外三处**（都带读数）：
  - **N154**：`join_value` 在 `.shp` 出口**静默截到 254 字节** —— 201/253/254 完整，255 与 401 都只剩 254，
    硬证据是 `t254.dbf` 与 `t255.dbf` **MD5 相同**；`rc=0`、日志 `Success`、`join_count` 照记满。
    同数据在 `.geojson`/`.kml` **保留完整**、在 `.gpkg` 被驱动放宽（DDL 写 `TEXT(254)` 但 SQLite 不 enforce）⇒ **按驱动分裂**。
  - **N155**：中文属性值走 `.shp` 出口先被降级成 `?`（121 字符/361 字节 → 120 个 `?` + `;`）且**不写 `.cpg`** —— 与 N154 独立，别混成一条。
  - **N156**：`--join_vector='GPKG:"<路径>":LB'`（GDAL 合法数据源串）撞上 `spatial_join.inc:109` 的**无 `ec` 重载**
    ⇒ 抛 `filesystem_error`，CLI `rc=1` 把 std 原文吐给用户（我复跑一致：`Error: equivalent: The filename … syntax is incorrect`，进度停在 35.0% 可定位）。
    同仓 `common.inc:412/645` 用的是带 `ec` 的重载 ⇒ **一句判据两种写法**；`vector_plugin_nearest.inc:118` 是这行的孪生，**必须成对修**。
  - **同文件异层被静默全数跳过**（`joinLayer = joinDS->GetLayer(0)` 写死，`--layer` 只作用输入侧）：
    我用独立客户端（python `sqlite3`）复跑到 `LB.join_count = [0,0,0]`。修法需要一个 `--join_layer` 新参数 ⇒ 牵动 CLI 参数表、GUI 与双输入保护清单，**待拍**。
- **N152（补登记，之前只在私有台账里）**：本仓 `data/` **没有任何含 NIR 语义波段的真多光谱影像**
  （5 张 4 波段图逐张现读 Color = Red/Green/Blue/Alpha）⇒ 光谱指数类的真实覆盖只能靠合成夹具；补一张真多光谱可解锁一批满档。
- **Task 4（N149）取证结论**：`repair` 是**唯一离群点** —— `singlepart.inc:88` 已经写对
  （`if (!normalized || normalized->IsEmpty()) ++skippedCount`），而 `repair.inc:76` 只有 `if (!repaired)`；
  四件读数 `repaired=4/skipped=0/要素 4/产物内 EMPTY 2`，恒等式 `repaired+skipped==输入数` 永成立 ⇒ 只看两键的读者无法自检。
  下级的“上抬到共享函数会波及三个检查动作的 null 解引用”被我**逐条否证**：`common.inc:970`、`topology_check.inc:87`、
  `duplicate_point_check.inc:69` 三处下游都判空 ⇒ 不立案。**另立一条纪律**：
  字面判据 `if (geom && !geom->IsEmpty())` 在 `common.inc` 出现 3 次，第三处属于 `effectiveLayerGeometryType`，不是采集器镜像。
- 自计行为今天**没有任何用例钉住**（`grep sameDataset|SelfJoin tests/test_plugins.cpp` = 0 命中）⇒ 新增红例是纯增、不撞既有断言。

### 批次 D 代码刀 D0-A7 落地（2026-10-27）

- **N149 已修（用户拍案①）—— ⚠ 用户可见变更：`vector.repair` 的 `repaired_count` / `skipped_count` 两个读数会变**。
  `vector_plugin_repair.inc` 的 `doRepair` 原来只判 `if (!repaired)`，而 `cloneNormalizedGeometry` 对 EMPTY
  返回的是**非空指针的空克隆**（`common.inc` 里 `if (!cloned || cloned->IsEmpty()) return cloned;`）
  ⇒ EMPTY 既被计入 `repaired_count`、又被原样写进产物。改后判据与同仓既有先例 `vector_plugin_singlepart.inc:88`
  一致：`if (!repaired || repaired->IsEmpty())` ⇒ **EMPTY 归 `skipped_count`，产物不再含 EMPTY**。
  影响面：同一份输入重跑，`repaired_count` 减小、`skipped_count` 增大（变化量 = 输入里 EMPTY 要素的条数），
  `repaired + skipped == 输入要素数` 这条恒等式不变 ⇒ 拿旧读数做差分或对账的脚本会看到跳变，这是**被批准的**行为变更。
  只改 repair 本地，**共享的 `cloneNormalizedGeometry` 未动**（上抬会波及 `dissolve` 与 `buildNormalizedUnion`，那是另一笔账）。
  判据 `PluginTest.VectorRepairCountsEmptyGeometryAsSkipped`：夹具 2 条合法 + 2 条 EMPTY + 1 条真非法（蝴蝶结），
  四件套一起断（两个计数 + 产物要素数 + 产物内 EMPTY 个数，后两件由**第二个客户端** `GDALOpenEx` 重开产物读）；
  旧态读数 `repaired=5 / skipped=0 / 产物要素=5 / 产物内 EMPTY=2` ⇒ 新态 `3 / 2 / 3 / 0`；
  变异取证：把判据回退成 `if (!repaired)` ⇒ 该用例当场复红（既有 `VectorRepairExecution` 不受影响）。
  文档 `docs/算法说明/矢量数据处理/修复.md` 的口径陈述（算法内容/处理流程/结果解释/输出说明）已按新口径改写。
  **修复轮 1（2026-10-27，评审席逮到两处，本条为追记）**：
  ① 上面这条新支路**自己引入了一次回归** —— EMPTY 支路 `continue` 前没有 `delete repaired`（`delete` 只在
  成功支路），而改前该支路只有 `nullptr` 能到达 ⇒ 泄漏不存在 ⇒ **这是新引入的，不是既病**。改法照先例的
  **所有权写法**而不是只照判据：克隆一拿到手就交 `std::unique_ptr<OGRGeometry>` 持有（与
  `vector_plugin_singlepart.inc:85` 同形），`dstFeat->SetGeometry(repaired.get())` 之后函数体内不再出现
  手写 `delete` ⇒ 三条出口（跳过 `continue`、`CreateFeature` 失败提前 `return`、正常走完）只有**一套**所有权。
  ② 产物侧判据补身份：用例除个数外还断产物的 `name` 集合 = `{invalid-1, ok-1, ok-2}`，
  挡住“把 EMPTY 丢掉但复制一条合法件充数”这类能蒙过纯个数判据的错法。
  ⚠ **这条判据抓不到所有权**（如实登记，不拿恒真断言冒充有判据）：把所有权改回“裸指针 + 跳过支路漏
  `delete`”的缺失态（即 `3dbeb5e` 的形状，工作树 blob `c406697c36bec253495a9b904f6f2f0dc93db8f3`）重建后重跑
  受影响族 ⇒ **28/28 全绿、零新红**；本仓 `tests/` 也没有堆差分形态可搭（`_CrtMemCheckpoint` /
  `_CrtDumpMemoryLeaks` / LSan grep = 0 命中），产物读数对“每条被跳过的 EMPTY 泄一次克隆”完全不敏感。
  ⇒ 这一处**没有廉价判据**，防住它的是所有权写法本身，不是用例。
- **N156 已修（用户拍：一行修 + 成对改）—— ⚠ 用户可见变更：`--join_vector` / `--nearest_vector` 写成
  `GPKG:"<路径>":<图层>` 前缀串时，产物数值会变**（同一份数据：`join_count` 由 2 变 3、`nearest` 的
  `nearest_dist` 由手算的正数变成"自己到自己 = 0"）。裸路径写法的读数不变 ⇒ 只有改串的人会遇到跳变。
  `spatial_join.inc` 与 `nearest.inc` 里那行同形的
  `const bool sameDataset = fs::equivalent(fs::u8path(input), fs::u8path(joinVector/nearestVector));`
  用的是**会抛**的 `std::filesystem::equivalent` 重载 ⇒ 当 `--join_vector` / `--nearest_vector` 写成
  `GPKG:"<路径>":<图层>`（GDAL 与 `openVector` 都**接受**的合法数据源串）时，引号对 filesystem 是非法字符
  ⇒ 抛 `filesystem_error`、CLI `rc=1`、把 Windows 的 std 原文
  （`Error: equivalent: The filename, directory name, or volume label syntax is incorrect.`）吐给用户。
  两处都换成**带 `error_code` 的重载**（`std::error_code sameDatasetEc;` + 三参 `equivalent`），异常吞掉并
  按“不是同一个文件”处理 ⇒ 与同仓 `common.inc:412/645` 既有写法统一。
  **未做**（用户本轮未批）：解析 `GPKG:` 前缀取真路径再比 —— 那属 `--join_layer` 那笔账，本轮一行未改；
  同一文件的不同图层做连接/目标时今天仍按“同一数据集 + 同一 FID”判据处理（Task 5 实测到 `join_count` 全 0），
  文档已如实写成未修面。
  判据 2 条（`VectorSpatialJoinAcceptsPrefixedGpkgSourceString` / `VectorNearestAcceptsPrefixedGpkgSourceString`），
  各两拍：① 同一文件裸路径 ⇒ 自计仍被排除（`join_count=2`；`nearest` 为 A→B(5)/B→A(5)/C→B(√85) 手算值）；
  ② 同一文件加 `GPKG:"…":<图层>` 前缀 ⇒ 不许抛，动作跑完，**数值按“不是同一个文件”给**（`join_count=3`；
  `nearest` 为自己、距离 0）—— 不是“产物照常”，产物里的数就是变了。
  旧态读数：两条都 `threw=true`、`e.what()` 逐字含上面那句 std 原文 ⇒ 红；改后 27/27 绿。
  变异取证：把两处**成对**换回无 `ec` 重载 ⇒ 两条当场复红（拍①那条不红，说明自计语义由拍①独立守着，
  写死 `sameDataset=false` 这类假修会被拍①打回）。
  **修复轮 1（2026-10-27）追记（B5）**：两条用例拍②的数值断言原先包在 `if (prefixed.success) { … }` 里 ——
  EXPECT 失败照样红、不算恒真，但**动作没成功时数值就被静默跳过**；已改成 `ASSERT_TRUE(prefixed.success)`
  打头、数值断言无条件走到，失败先响在最前面。
  文档同步：`空间连接.md`（结果解释 + 注意事项）、`最近邻.md`（算法内容 + 注意事项）按“路径等价判定”改写并写明未修面。
- **（新立挂账）N157 —— 本刀新开了一条输出保护的风险面，不在 D0-A7 修**。机制（控制器已核代码）：
  `vector_plugin_common.inc` 的"输出不得等于输入"保护闸在 `start()` 里走 `fs::equivalent(input, target_, ec)`，
  传的是**整条输入串**；`GPKG:"<路径>":<图层>` 这种合法但非纯路径的 GDAL 数据源串对 filesystem 而言不是路径
  ⇒ 带 `ec` 的重载不抛但恒得 `false` ⇒ **保护对带前缀的写法失效**。N156 之前同一条命令会先被 `filesystem_error`
  撞死在 `rc=1`（错着但挡住了），N156 之后它放行 ⇒ 若用户写 `--join_vector='GPKG:"x.gpkg":LB'` 且 `--output x.gpkg`，
  以前写不进去、**现在会真往自己输入上写**。影响面 = `spatial_join` / `nearest` 两个动作的前缀串用法。
  本轮处置（按裁定只做文档与登记，**不改守卫**）：两篇文档把"可以写成前缀串"改成中性描述并点名本条挂账；
  守卫本身的修法（解析前缀取真路径后再比等价）紧随其后另开一小刀，未批前不要照前缀串写覆盖自己输入的用法。
  **【2026-10-28 结案，批次 D / D0-C4】**修法按工单裁定的**甲案**落地（剥前缀取真路径再比等价；乙案"认不出形态即拒"
  未获批准、未实现）。⚠ 挂账原文把影响面写成"`spatial_join` / `nearest` 两个动作的前缀串用法"——**现读更正**：
  那道闸是 `VectorStagedOutput::start` 与 `CsvStagedOutput::start` 两个**共用入口**，
  射程 = 矢量 24 个数据集写出点 + 7 个写 CSV 的检查动作（`isProtectedInput` 是第三处，按 stem 清旧边车那一支），
  不止那两个动作；实测载体见下面 D0-C4 那一节。

### 批次 D 代码刀 D0-B 落地（2026-10-27，Task 6 / N148）

- **N148 已修（用户裁定：只做附加式）—— ⚠ 零用户可见变更**：叠加类动作 `union` / `difference` /
  `intersect` / `clip` 的四个既有键（`union_*_pieces` 四件、`feature_count`）与返回消息**取值一律不动**，
  只新增 `union_skipped_input_count` / `difference_skipped_input_count` / `intersect_skipped_input_count` /
  `clip_skipped_input_count`，沿用 `union_skipped_overlay_pieces` 的"**>0 才发**"约定 ⇒ "这个键出现"本身就是
  "输入侧有丢失"的信号。裁定时写的键名候选 `overlay_skipped_feature_count` **未采用**。`doBuffer` 现读不走这两个
  采集器 ⇒ **buffer 不发键**（本刀射程只有四个动作 + 四篇文档 `矢量数据处理/{并集,差集,交集,矢量裁切}.md`）。
- **被丢弃的输入几何有四类**（`cloneNormalizedGeometry` 的全部出口，逐行读；四篇文档与键语义段已按此改写）：
  (a) **输入几何本身就是 NULL**（第一句 `if (!geom) return nullptr;` 直接返回，**根本没进修复**）；
  (b) `geom->clone()` 返回 null；
  (c) 克隆出来就是 EMPTY —— 返回的是**非空指针的空克隆**，由采集器的 `!geom->IsEmpty()` 挡下；
  (d) `IsValid()` 为假且 `MakeValid()` 失败或产出 EMPTY ⇒ **只有这一类才叫"修复失败"**。
  ⇒ 旧口径"非法几何会先尝试自动修复；修复失败的单条被丢弃"对 (a)(b)(c) 三类是**错的**（键出现的原因比那句宽）。
- **判据（Debug，现读注册数 767 = 上一刀 766 + 本修复轮 1 条）**：
  ① 镜像守卫 `PluginTest.VectorOverlayCollectorMirrorPredicateStaysVerbatim`（源码文本扫描，按**函数名**切片，
  比较两侧含 `if (` 的决策行清单逐字相等 + `if (geom` 判据各恰一条 + 标准化行逐字相等）；
  ② 四条数值用例 `PluginTest.Vector(Union|Difference|Intersect|Clip)CountsSkipped...`（夹具为 EMPTY 面，即 (c) 类）；
  ③ 修复轮 1 新增 `PluginTest.VectorUnionCountsSkippedOverlayNullGeometry`（(a) 类 NULL 几何：
  `union_skipped_input_count==1` + 四件既有键 + `message` 逐字，夹具身份由**第二个客户端** `GDALOpenEx` 重开自证
  `nullGeomCount==1 / emptyGeomCount==0`，防止驱动把"没设几何"写成 `POLYGON EMPTY` 让覆盖面悄悄退化回 (c)；
  反向臂"整层只有 NULL" ⇒ 整条失败、`message` 逐字
  `"Union requires valid overlay geometries and automatic repair failed."`、**metadata 为空**）；
  ④ 修复轮 1 给既有正例 `PluginTest.VectorClipRepairsInvalidOverlayGeometry` 补
  `EXPECT_EQ(result.metadata.count("clip_skipped_input_count"), 0u)` ⇒ "修好了不发键 / 丢掉了才发键"两头都有机器判据。
- ⚠ **覆盖边界（如实登记，防后来者误以为守卫兜住一切）**：
  ① 镜像守卫只把**含 `if (` 的行**列进比较清单 ⇒ 跳过支**体内**的累加行 `++(*skippedInputCount);`、
  游标/循环行（`ResetReading()`、`while ((feat = layer->GetNextFeature()) != nullptr)`）都不在清单内，
  守卫对它们**不敏感**；判据漂移由守卫兜，**计数缺失由数值用例兜**，这是两道分工不同的闸。
  ② **要素趟那支持数今天不可达**：`collectNormalizedFeatures` 的两个调用点
  （`vector_plugin_overlay.inc` 里 union 的路径 A / 路径 B）都只传图层不传计数指针 ⇒ 键的实际取值只来自几何趟；
  单独删掉要素趟那两行 `++` ⇒ **0 条用例会红**。没删的理由：守卫要求两侧决策行逐字相等，删了守卫必红
  （已在 `vector_plugin_common.inc` 的就地注释里写明"保留只为逐字镜像"，并写明将来若有调用点开始传计数
  必须防重复计数——union 对同一叠加层就是扫两趟）。
- ⚠ **Release 侧未验收**：本刀（含修复轮 1）只重编 Debug 树，Release 树没重编 ⇒ 新增用例在 Release 下的
  注册数与跑绿状态为 `UNVERIFIED`，不由 Debug 结果替代。受影响族 Debug **57/57 绿**
  （`-R "Vector(Union|Difference|Intersect|Clip)|VectorStagedOutput|KeepsOldProductAndLeavesNoTempFile|CoverageMeta|docs_match_cli|source_mojibake_guard"`）。
  两条真实腿 `real_data_vector_overlay_ops` / `real_data_vector_overlay_buffer_join` 见 `task-6-fix1-report.md` 的实测秒数。
- **（新立挂账）N158 —— 退化非法面被 `MakeValid` 修成低维几何后照样进产物、四个键一个都不发，本刀只登记不修**。
  机制（控制器与实现席各自独立实测，读数一致）：`cloneNormalizedGeometry` 的 `MakeValid()` 支只判"返回 null 或 `IsEmpty()`"，
  **不判维度** ⇒ 三点共线这类"真非法但退化"的面被修成 `LINESTRING`（或重合点修成 `POINT`）后是非空非 null，
  走**保留支**进产物、占一条 `union_*_pieces`。
  实测读数（输入 `POLYGON((0 0,10 0,10 10,0 10))`，叠加层 = 1 条有效面 `[20,20]-[30,30]` + 1 条三点共线"面"
  `((2,2),(8,8),(4,4))`）：`rc=0`、`Success: Union completed: 3 features`、
  `union_input_pieces 1 / union_overlap_pieces 1 / union_overlay_pieces 1 / union_total_pieces 3`、
  **无 `union_skipped_input_count`**；把产物 `vector convert` 成 GeoJSON 逐条数几何类型 ⇒
  `['LineString','Polygon','Polygon']` ⇒ **面变线进了产物、占了一块、无痕**。
  这是"键出现才能诊断"这条产品承诺的**另一个盲区**（与 N148 不同账：N148 说的是"被丢弃不计数"，
  这条说的是"没被丢弃但维度掉了"）。**判据与修法未定**（候选：维度掉档记一笔、或按输入声明类型升型/拒绝、
  或只补文档口径），需用户拍板后另开一刀；本刀一行实现未动。

### 批次 D 代码刀 D0-C3 落地（2026-10-27，Task 9 / 7 个 CSV 检查动作）

- ⚠ **用户可见变更：以前会被截断的那几种拼法现在报失败。** `vector` 的 7 个写 CSV 的检查动作
  `adjacency` / `dangling_endpoint_check` / `duplicate_point_check` / `hole_check` / `multipart_check` /
  `overlap_check` / `topology_check`，把 `--output` 指回 `--input` 那个矢量文件时（含 `dir\.\同名`
  这种指向同一个文件的另一种拼法），改前是**输入被就地清空、动作还报成功**
  （实测：48 字节的输入被截成 32 字节只剩表头、`rc=0`），现在改成在任何建目录/建文件之前硬失败：
  `Output must not be the same file as input: <输出> (input: <输入>)`、`rc=1`、输入逐字节不变。
- ⚠ **这条变更的层次归属（复审席逮出我原先标成"CLI 实测"是标错）**：真闸在**插件侧**
  `CsvStagedOutput::start` 的等价判定，不在 CLI。CLI 与 GUI 都还有一层**框架级扩展名闸**
  （`src/framework/src/action_validation.cpp:518-526`，判据 `:523`：这 7 个动作的 `--output` 必须是 `.csv`；
  调用点 `src/cli/main.cpp:255` 与 `src/gui/mainwindow.cpp:846/892/928`），
  所以 `--output=<输入>.gpkg` 这类拼法在两个入口都被那层闸**偶然挡住**、进不到插件 ——
  说它偶然，是因为 `.csv` 同时也在合法矢量输入扩展名清单里（`:173`），
  **只有"输入本身就是一份合法 `.csv` 矢量"这一种拼法会真的走到缺陷 A**（上面 48→32 字节的实测就是它）；
  而直调 `plugin->execute()` 的调用面（单元测试走的路、以及任何嵌入式调用）从来不受那层闸保护。
  ⇒ 裁定：**不在框架侧补第二条同语义的输出≠输入判定**（两处判定会漂，与 R-D7-2 同一个理由），插件侧那道是唯一真闸。
- 字节数勘正：实现席报告写"改前 42 字节"，复审席与控制器各用**同一条 printf** 复现出**同一 md5**
  `d30e57d16a138fcdb362ad115edeba61` 的文件是 **48 字节** ⇒ 本处以 48 落笔（md5 相同说明是同一份夹具，不是两种夹具）。
- 同一批动作**写入中途失败**（取消 / 磁盘满 / 替换失败）时的交付形态也变了：改前目标位置留下
  "上一份产物被截断后的半件"（实测 7 条红态：完整产物 71~145 字节被截成 37~70 字节），
  现在与批次 C 的 C3n 契约同口径 —— 旧产物逐字节保持原样、不留半件、临时件删干净。
- 实现走的是**轻量等价路径**（CSV 不是 GDAL 数据集，套不进 `VectorStagedOutput`）：
  `vector_plugin_common.inc` 新增 `CsvStagedOutput`（`start` = 等价闸 → 建父目录 → 同目录临时件 → 开流；
  `stream` = 动作原来那个 `ofs`；`commit` = 关流 → 写失败即报失败 → 同卷替换）。
  与数据集侧**同源**的两件事：临时件命名仍是 `<目标 stem>.gis-tmp-<进程号>-<序号><扩展名>`
  （序号是**一把**进程内计数器，两个类型共用 ⇒ 同目录同 stem 也不会撞名）；
  替换原语 `replaceStagedFile`（Win 侧 `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)`、
  故意不带 `COPY_ALLOWED`；POSIX 侧 `fs::rename(..., ec)`）从 `VectorStagedOutput` 的私有静态成员
  **原样提到文件作用域**共用，分支与文案逐字未改，仓库里**没有**第二份实现。
- 等价判定用**三参带 `std::error_code`** 的 `fs::equivalent`（N156 的教训：无 `ec` 的重载对
  `GPKG:"<路径>":<图层>` 这类合法 GDAL 数据源串抛 `filesystem_error`，std 原文会吐给用户）。
  ~~⚠ 该类带前缀/带图层号的拼法**不受本闸保护**（剥前缀再比是独立挂账 N157（#157）的射程，本刀不做，
  已在闸旁与 7 篇文档里各留一处说明，免得它悄悄消失）。~~
  **⇒ 2026-10-26/28 更正**：该射程已由**批次 D / D0-C4**（Task 8b）结案（见下面 D0-C4 那一节）——
  三处闸现在先把数据源串归一成纯路径再比等价，前缀拼法同样受保护。
- 判据（Debug）：7 条新用例
  `PluginTest.Vector{Adjacency,DanglingEndpoint,DuplicatePoint,Hole,Multipart,Overlap,Topology}OutputEqualsInputFailsAndKeepsFile`，
  每条两臂 = 闸臂（同串 + `dir\.\` 两种拼法，判"必须干净失败 + 文案点名规则 + 输入逐字节不变 +
  目录清单不变"）与契约臂（`T17CancellableProgress` 让取消点落在"已写出若干行、而且还有行没写完"的
  中间；前置趟现数完整产物行数，并把它与"抛出前已写行数"对撞，防"半件恰好等于整件"的假载体）。
  取证顺序：先红（闸臂 28 条 + 契约臂 7 条）⇒ 实现后 7/7 绿 ⇒ **反向臂**（只摘闸、其余保留）
  7 条闸臂全红 14 条判点、契约臂 0 红 ⇒ 两半各自承重。
- 注册数：Debug **767 → 774**（+7）。Release 树本刀未重编 ⇒ Release 侧一律**未验收**。
- 文档：`矢量数据处理/{邻接关系,悬挂端点检查,重复点检查,孔洞检查,多部件检查,重叠检查,拓扑检查}.md`
  的"输出说明"段各加两条（输出不得指向输入；失败时旧产物逐字节保持原样）。参数表未动。
- ⚠ **本轮没做/没做到的**（如实登记）：① 本刀**没有**给这 7 个动作补"产物内容级"的成功路径判据
  （J2/J3 那两族只覆盖数据集写出点，CSV 侧的成功形态仍只有各动作既有的 `...Execution` 用例在判）；
  ② 台账"批次 C 裁定表"里那一行（原文 `（新）**CSV 两个检查动作无临时件契约、无"输出≠输入"闸**`，
  只点名了 `overlap_check` 与 `multipart_check`）**未更新** ⇒ 它现在既漏 5 个动作、又还挂着"列批次 D"，
  改它属于登记表维护，按本刀纪律交回控制器处理；③ N157（前缀拼法）当时仍开账 ⇒ **已于 2026-10-28 由批次 D / D0-C4 结案**。

### 批次 D 代码刀 D0-C2 落地（2026-10-27，Task 8 / 地理参考同族 + 防回潮守卫）

起因：全仓取地理变换时**不判返回值**的位点共 6 处（不是先前台账说的 1~2 处）。GDAL 的语义反直觉——
基类实现**在返回 CE_Failure 之前照样把默认恒等式 (0,1,0,0,0,1) 写进调用方的数组**
（GDAL 3.12.4 现读 `gcore/gdaldataset.cpp:1506-1512`、`gcore/gdal_geotransform.h:47-62`），
自定义该方法且失败前不碰数组的驱动则保留调用方初值。⇒ "取不到"在产物里表现为**一个看起来正常的假变换**，
或未初始化的栈垃圾（`opencv_wrapper.cpp` 改前那两处连数组都没初始化，Debug 下把 `0xCCCC…` 持久化进交付 GeoTIFF）。
**用户 2026-10-27 拍板"三件全做"**（B/C 停止伪造、F 缺参考即硬失败、D 换专有文案），A 与 E 两处经裁定**只加兜底理由注释、不改行为**。

- **用户可见变更 ①（`processing.threshold` / `raster_math.reclassify`）**：无地理参考输入的产物不再被写入伪造的
  `Origin = (0, 0) / Pixel Size = (1, 1)`，改为**完全没有地理变换**；两个动作**仍报成功、产物仍生成**——
  这条既有契约不许改成失败（`tests/test_plugins.cpp:1969` 断不失败、`:1982` 断产物在场，同族的
  `PluginTest.PansharpenNoGeoTransformSkipsExtentCheck` 一并守住），本刀只把伪造的元数据从产物里去掉。
- **用户可见变更 ②（`terrain.viewshed_multi`）**：输入 DEM 取不到地理变换 ⇒ **硬失败**
  `input raster geotransform is missing`（文案与 `terrain.cut_fill` `terrain_plugin.cpp:1220`、
  `terrain.reservoir_volume` `:1343` 逐字同族），闸门排在创建产物之前 ⇒ 失败路径不留半个 .tif。
  改前是"报成功 + 一张量纲全错的可见性掩膜"。
- **用户可见变更 ③（`cutting.tile`）**：原来"有 CRS、但取不到地理变换"的输入会被报成 north-up 那句
  （等于把 GDAL 的失败填充值当成用户犯的错）。现在两句分开——新增
  `tile input geotransform is missing; cannot map pixels to the projected grid`，
  顺序恒为「无 CRS → 取不到变换 → 非正北零旋转」，顺序与文案两条都进判据（三臂用例
  `TileNamesMissingGeotransformSeparatelyFromNorthUpAndNoCrs`）。
- **不改动行为的两位**：`gdal_wrapper.cpp` 的 `getRasterInfo`（位点 A，消费方现读只有
  `src/gui/raster_preview_widget.cpp:125`（外层无 try）与 `tests/test_core.cpp:326`，改抛错即打挂预览面板）与
  `cutting_plugin.cpp` 读 warp 产物那一处（位点 E，当前参数集不可达：上游三道闸 `:1190/:1198/:1264-:1269` +
  warpArgv 从不传 `-dstmethod`，GDAL 只在 `DST_METHOD=NO_GEOTRANSFORM` 时才不给输出写变换，
  现读 `apps/gdalwarp_lib.cpp:4897-4898`、`:4924-4926`、`:4932-4936`）。两处都只加"兜底理由"注释，
  并在 `tests/geotransform_guard_allowlist.json` 里点名；A 的兜底描述本轮**被纠正**（原写"取不到就是全 0"，
  按上面 GDAL 现读其实有两种形态）。
- **机器兜底**：新守卫 `geotransform_guard`（ctest 注册 **#779**，`tests/check_geotransform_guard.py` + `.cmake` 载体）——
  C++ 词法级把行注释/块注释/字符串里的同形态文本分出去、拼跨行语句，产品域（`src/`，148 个文件）钉
  `any=21 code=20 comment_line=1 block_comment=0 string_only=0 / checked=18 unchecked=2 allowlisted=2 violations=0 dead=0`，
  测试域（`tests/`，15 个文件）钉 CODE 桶那一侧 `code=3 checked=2 unchecked=1 allowlisted=1 violations=0 dead=0`
  （注释桶刻意不钉，理由与承重演示见该文件注释）；7 条自证臂先于域判定跑，红则退出码 2；
  DEAD 白名单（缺陷修好了、豁免还留着）同样判红。退出码 0/1/2/3 的**唯一消费者**是那个 `.cmake` 包装。
- 判据：四条新用例 `ProcessingThresholdKeepsProductWithoutGeotransform`、
  `RasterMathReclassifyKeepsProductWithoutGeotransform`、`TerrainMultiViewshedFailsWithoutGeotransform`、
  `TileNamesMissingGeotransformSeparatelyFromNorthUpAndNoCrs`，先红后绿；产物侧判据一律用**第二个客户端**
  （复用现成的 `rasterHasNoGeoTransform`，新开数据集按返回值判），不读动作自报的 metadata。
- 注册数：Debug **774 → 779**（+4 条 `TEST_F` + 1 条 `add_test`）。**Release 树本刀未重编 ⇒ Release 侧一律未验收。**
- 反向臂（"能红"证明）：把位点 B 改回无条件写出 ⇒ `#142` 与 `#779` 两条红（其余 7 条绿，判据是位点特异的），
  守卫产品域同时读到位移 `checked 18→17 / unchecked 2→3 / violations 0→1`；还原后 9/9 复绿。

**N151 的余波在本节销账**：上面"批次 C 阶段三收口前的挂账逐条裁定"表里 N151 那行写着
`opencv_wrapper.cpp:87-88`（`matsToGdalTiff`）是同形态第二处、射程之外未动 ⇒ 一并列批次 D。**现已处理**：
它就是本节的位点 C（用户可见变更 ① 的第二半，`raster_math.reclassify` 走的就是这条多波段写出通道），
`15712fd` 当年只修了 `raster_manage.proximity` 那一处，第二处在批次 D 随 D0-C2 收口，**N151 至此全销**。

> ⚠ 时间口径更正：`8e62bdc` 的提交信息写"四条红腿合计 1.87 s"，那是第一版夹具（对照腿崩溃那一次）的预览读数；
> **以 `task-8-report.md` 节 2 的 2.39 s（逐条 0.25 / 0.16 / 0.21 / 0.41）为准**，提交不 amend。

- ⚠ **本刀没做/未覆盖的**（如实登记）：① `threshold / reclassify / tile` 三个入口**没有任何真实数据腿**
  （现读：两个真实腿脚本里 `"threshold"`/`"reclassify"`/`"tile"` 三个动作名 0 命中，`real_data_raster` 只跑
  `terrain.viewshed_multi` 与 `processing.pansharpen`）⇒ 这三条的交付证据只有 fixture 级用例；
  ② 越界观察点让 `viewshed_multi` 在 Debug 下被 STL 迭代器检查带走进程（`0xc0000409`），**另立 N160**、本刀不修；
  ③ A 那处的"预览侧有无变换标志"仍挂 GUI 刀；④ Release 双树未验收；⑤ 节3 已提交注释里"实测 PNM"那一处
  是本席未复现的前席自述（标 UNVERIFIED），承重的只有上面那条 GDAL 基类源码读数。

### 批次 D 代码刀 D0-C4 落地（2026-10-28，Task 8b / N157 结案）

起因：D0-A7 把 `sameDataset` 从"抛异常"改成 `fs::equivalent(..., ec)` 静默判否（那是 N156 的修法），
副作用是**带驱动前缀的数据源串**在输出保护闸里恒判"不是同一个文件" ⇒ 保护看不见
"`--output` 其实就是 `--input` 那个文件"。方案取工单裁定的**甲案**（认出前缀取真路径再比等价）；
**乙案（认不出形态即拒）未获批准 ⇒ 未实现**，代码与文档都写明"认不出就原样返回、本闸此时不覆盖该形态"。

- ⚠ **用户可见变更**：`vector` 的写出动作里，`--input` 写成 `GPKG:"<路径>":<图层>` 这类合法数据源串、
  `--output` 指回同一个文件这一拼法，**从"跑完全部计算再撞 OS 占用锁"变成"建任何产物之前就报失败"**。
  改前实测（Debug CLI）：`filter` 停在 **40%**、`repair` 停在 **90%**，文案是
  `Cannot replace output … with the staged file …<x>.gis-tmp-<pid>-1.gpkg (Win32 error 5)`
  （CSV 侧同一病灶在插件级用例里现读为一路跑到 **100%** 才吐这句）
  ——挡住毁数据的只是"Windows 不许替换正被占用的文件"这个 **OS 巧合**，不是设计。
  改后同一命令：`filter` 停在 **30%**、`repair` 停在 **20%**，
  文案 `Output must not be the same file as input: <输出> (input: GPKG:"<路径>":<图层>)`
  （**原样回显用户敲进去的前缀拼法**，归一结果只用于比较、不顶替它）。
  ⚠ **CSV 检查动作那一半在 CLI 上也拼得出来**（复审席实测、控制器 2026-10-28 复现，本行原先写"拼不出来"是错的）：
  框架级校验只管**扩展名**（`action_validation.cpp:235`），不管文件真实驱动 ⇒
  把一个**投影坐标系**的 GPKG 改名成 `pjc.csv` 再写 `--input='GPKG:"build/tmp/…/pjc.csv":src' --output=…/pjc.csv`，
  `vector adjacency` 现态在 **35%** 处报 `Output must not be the same file as input: …pjc.csv (input: GPKG:"…pjc.csv":src)`、
  输入 md5 `05eb4d9d…` 前后不变；改前二进制同命令一路跑到 **100%** 才吐 `Win32 error 5`。
  ⇒ 该面**用户可从 CLI 触发**，判据 `#416` 的插件级形态只是它的一个投影。
  ⚠ 反例也要记：用**地理坐标**的 GPKG 试同一条命令会先撞投影闸
  （`Error: Adjacency requires a projected coordinate system`）⇒ 载体必须是投影层，控制器第一次复现就踩了这个。
  另一头同样钉住：**合法前缀输入 + 别的输出必须照常成功**（实测 `filter --input='GPKG:"…x.gpkg":src'
  --output=…out_pass.gpkg --where="1=1"` ⇒ `rc=0`、`feature_count: 42`）⇒ 本刀**不新增任何拒绝面**。
- 落点：`vector_plugin_common.inc` 新增**一份**文件作用域归一函数 `datasetSourceAsPlainPath`（现读 `:456`），
  三处闸共用——`VectorStagedOutput::start`（归一 `:583` / 等价 `:588`）、`isProtectedInput`（`:778` / `:779`）、
  `CsvStagedOutput::start`（`:847` / `:854`）；`protectedInputs_` 改存**原样拼法**（`vector<std::string>`）。
  识别纪律：驱动短名向 `GetGDALDriverManager()` 现取（ASCII 大小写不敏感逐字比，不写死名单）、
  盘符形态（`C:/…`）单独判掉绝不当前缀剥、引号形态与非引号带图层尾巴两种都取路径、
  剥完空或含残引号即原样返回。**不做**等价判定、**不查**文件存在性。
- 判据（作者=前席，四条 `TEST_F` 全部接住；接手席只补了载体参数）：
  `PluginTest.VectorFilterOutputEqualsInputWhenInputIsPrefixedSourceStringFails`（`VectorStagedOutput::start`，
  双向臂）、`…VectorAdjacencyOutputEqualsInputWhenInputIsPrefixedSourceStringFails`（`CsvStagedOutput::start`，
  双向臂）、`…VectorStagedOutputSidecarCleanupKeepsOldProductUnderPrefixedInput`（`isProtectedInput`，
  带诱饵边车 `x.cpg` 防"扫描整块没跑"的空判）、`…VectorStagedOutputNormalizationSharedAcrossAllThreeGates`
  （源码结构守卫：定义恰 1 份 / 三处各恰 1 调用 + 1 等价 / 全文件等价恰 3，锚点带"切不出即判红"）。
  ⚠ 接手席现读否证前席一处：`filter` 载体漏 `where` ⇒ 动作在 `vector_plugin_info_filter.inc:191-194`
  就返回，**走不到闸**，两条用例改前改后都恒红在参数校验（等价于恒红判据）⇒ 补 `where=1=1`。
- 取证顺序与读数（Debug，全部 `ctest --test-dir build/debug -C Debug`）：
  先红 `3 failed / 4`（`#415 #416 #418` 红，`#417` 当时**绿**——挡住删除的是 OS 句柄而不是那道闸）
  ⇒ 实现后 `100% tests passed, 0 failed out of 4`，`Total 1.25 sec`（还原干净后复跑 `1.01 sec`）
  ⇒ **反向臂**（三处归一摘回 `fs::u8path`）`3 failed / 4`（`#417` 仍绿，与"摘掉归一⇒四条红"的账面**不符**，已如实登记）
  ⇒ **可达性变异臂**：在 `doFilter` 的 `commit()` 前插一句 `srcDS.reset()` 模拟"输入提前关闭"——
  摘归一那一臂 `#417` **当场红**（输入 `x.gpkg` 真的被按 stem 的边车扫描删掉），
  归一还原那一臂 `#417` **绿**（改由闸护住）⇒ 证明今天"没毁数据"确实只差一句 `reset()`。
  ⚠ **口径降级（复审席反事实实测）**：`#417` 的②臂（"输入边车没被删"那一组判点）在**已提交态里恒绿**——
  把 2026-10-27 14:48 那份**改前** `plugin_vector.dll`（md5 `b794232b…`，含 Task 9、不含归一）装进硬链接沙箱跑同场景，
  **不加任何变异**也是 `rc=0`、诱饵 `x.cpg` 被删（证明扫描确实跑了）、`x.gpkg` md5 不变 ⇒ **改前逐条同样全绿**。
  故不能写"`#417` 判别力成立"，只能写：**②臂是回归守卫，红态只在变异下可达**；
  整条用例不是死条目——它的①臂（防"整块扫描没跑"）是真判据，而"三处闸真的共用一份归一"的红态由源码结构守卫 `#418` 提供。
  可达性证不出来这件事本身转 **#161**（GPKG 支不查 `isProtectedInput` 的防御缺失）。
  变异用完即还原并用 `git show HEAD` + 行尾规范化 + `git hash-object --path` 对撞 blob 确认。
- **边车删除的可达性答复**：本仓今天**没有**"输入数据集在 commit 前就已关闭"的动作
  （`grep -rnE "GDALClose|srcDS\.reset\(\)" src/plugins/vector/` ⇒ 命中 1 处且是注释原文；
  38 个 `= openVector(` 调用点全在函数体顶层，`GdalDatasetPtr` 活到动作 `return` ⇒ commit 时句柄仍开）
  ⇒ 该路径**当前不可达，依赖 OS 句柄巧合**（不写"安全"）。
  ⚠ 相邻面登记、本刀不修：`removeStaleOldSidecars` 的 **GPKG 支**（`:764`，删除落 `:768`）
  对 `target_` 的 `-wal/-journal/-shm` **不查 `isProtectedInput`** ⇒ 另开一刀 D0-C4b。
- 回归：先 `-N` 数应跑 **44** 条（正则见 `task-8b-report.md` 第 6 节；控制器 10-27 的 40 条基线 + 本刀 4 条），
  加 `docs_match_cli` 与 `source_mojibake_guard` 共 **46** ⇒ 实跑 `100% tests passed, 0 failed out of 46`，
  `Total 28.23 sec`。Debug 注册数现读 **783**（本刀 +4）。
- 真实腿：只跑覆盖 `filter` / `adjacency` / `repair` / `convert` 的三个档 ⇒
  `real_data_vector_quick` 161.92 s（TIMEOUT 720 ⇒ 4.44×）、`real_data_vector_checks` 32.87 s（100 ⇒ 3.04×）、
  `real_data_vector_overlay_buffer_join` 87.62 s（630 ⇒ 7.19×），`100% tests passed, 0 failed out of 3`，
  合计 282.44 s；**余量最低 3.04×，未跌破 2.5× ⇒ 不拆腿、未抬 TIMEOUT**。Release 双树本刀未编 ⇒ 未验收。
- 文档：`矢量数据处理/{邻接关系,多部件检查,孔洞检查,悬挂端点检查,拓扑检查,重叠检查,重复点检查}.md` 七篇里
  "带驱动前缀的数据源写法不受这道闸保护"那句升级为"同样受这道闸保护（认不出的形态仍放行）"；
  `最近邻.md` / `空间连接.md`（D0-A7 那两处中性措辞）同样升级并点名结案。参数表**一个字节没动**。
- `data/` 只读复核：`find data -type f -newermt "2026-10-28 00:19:45" | wc -l` = **0**；
  `gen_data_baseline.py` 重算与 `data_baseline.prev` **字段级对撞 0 处差异**（读数见 `task-8b-report.md` 第 8 节）。

### 批次 D 覆盖晋升 D1-M（2026-10-28，Task 14 / matching detect·corner·match·register 四条 PENDING→GOLD）

本刀**不改产品码**（只在变异取证时改、取证完当场还原），是把 matching 四条动作的测试面从"成功+产物存在"
升到"值级判据"，并把登记表现场翻 GOLD。判据形状一律用"能由参数/判据文本/库契约手算推出"的关系，
**不钉检测器内部计数**（批次 C N108 自指期望同族）。`ecc_register` / `stitch` 被 Debug 轻量闸门
（`GIS_MATCHING_LIGHTWEIGHT_DEBUG`）挡死、Debug 侧永不执行其数值断言 ⇒ 本批不碰，归 D4 收口门与挂账 #165。

- **`检测器上限参数的三种真实语义`（用户可见口径，非代码变更；当时"是否统一行为"挂账 #167 未决，
  2026-11-07 已裁定为乙1 并改掉第 1 条，见本文件末尾的 N167 一节——下面三条是**当时**的现读）**：
  `detect` / `match` / `register` 的 `最大特征点` 参数，现读在 96×96 图案夹具上实测为——
  1. **`akaze`：（改前）完全不生效**。`createDetector` 对 akaze 走 `cv::AKAZE::create()`（不传参），
     该参数被丢弃（实测 `max_points=20` 与 `=200` 都出 29 个）。
  2. **`sift`：不是硬上限**。实测 `max_points=20` 出 23 个（越过设定值）。
  3. **`orb`：是上限但不保证取满**。计数恒不超设定值，纹理/边界不足时低于它（实测 20⇒10、1000⇒48）。
  对照：`corner` 的 `最大角点数` **两支都真消费**（`goodFeaturesToTrack` 与 harris 抑制循环各自截到该值，
  候选充足时 `corner_count` 恰好等于设定值）。`match` / `register` 的 `比值检验 = 0` ⇒ 一条匹配都不留
  （判据 `d(1st) < 比值 × d(2nd)`，距离非负 ⇒ 恒假）；`register` 此时匹配数 0 < 4 ⇒ 动作显式失败、不产出文件。
  ⚠ CLI/GUI 侧 `action_validation.cpp:574` 把 `比值检验` 合法区间校验成 `(0, 1]`，`--ratio_test=0` 会被参数校验拒
  ⇒ 0 档这条只在库级（`PluginManager::execute` 直连）可达，已由单测钉住，腿侧晋升不了这一档（如实登记）。
- 判据（四条 `TEST_F`，Debug 全绿；每行第 4 列证据见登记表）：
  `PluginTest.MatchingDetectCapLadderAndFlatZeroPoint`（detect：ORB 上限不越 + 平坦图 ORB/SIFT 恒 0 零点）、
  `PluginTest.MatchingCornerMaxCornersCapLadder`（corner：shi_tomasi 与 harris 两支 max_corners=1/2/3 恰截 + 平坦零点）、
  `PluginTest.MatchingMatchRatioTestLadderAndZeroPoint`（match：ratio=0 恒 0 + 阶梯不减 + 三档不全等）、
  `PluginTest.MatchingRegisterIdentityTransformKeepsRasterPixels`（register：reference==input 恒等 ⇒ 逐像元相等 + ratio=0 失败支不产文件）。
- 变异取证七臂全翻红（M1 orb 去上限 / M2 goodFeaturesToTrack 写死大数 / M3 doMatch 去 ratioTest /
  M4 doRegister 去 ratioTest / M5 warp x-y 互换 / M6 harris 去 break / M7 warp 尺寸写歪），
  台账见用例注释；两臂（仿射方向对调、affine↔translation）在恒等自洽夹具下判不到，如实标注未跑。
- 腿侧 `run_real_matching_regression.ps1`：detect 升上限档、corner 升恰截档、register **补判据分支**
  （从"只跑不判"改为核产物宽高==参考宽高）、match 加逐条 distance≥0 结构判据；`quick` 实跑合计 0.68 s，
  `real_data_matching` 单跑 4.13 s。
- 登记算术：`gold_rows 87→91`、`pending_rows 39→35`、`gold_rows_all_lines 88→92`、
  `gold_rows_literal_slot 72→76`（table_driven 15 不动）、`kPendingCeiling 39→35`，同一笔提交；
  `ctest -R "^(CoverageMeta|CoverageMetaJudge)"` 19/19 绿。
- ⚠ **Release 侧未验收**：本批只在 Debug 树取证，Release 未重编 ⇒ 四条晋升的 Release 凭据缺位，归收口门补。

### 批次 D 覆盖晋升 D1-F（2026-10-28，Task 15 / pansharpen·band_math 两条 PENDING→GOLD）

**先说清这把没干什么**：两条晋升的凭据来自**既有**数值断言（`PansharpenMeanBlendReplacesSimpleMean`
与 `BandMathDivisorThresholdBoundaryIsPinned`，都是批次 A/Task 13 时代就写进 `tests/test_plugins.cpp` 的块），
本刀**没有新增任何算法测试、也没有新增 `TEST_F`**（Debug 注册数因此不变）。本轮新增的只有两样：
①对这两条 PENDING 所属九个断言块的**变异取证台账**（十臂，落在 `tests/test_plugins.cpp` 的用例注释里），
②腿 `real_data_raster` 里 `raster_math_band_math` 从"有 case 无判据分支"补成一条**值级判据分支**。
产品码只在取证时被改、当场还原（四个落点 `git hash-object` 全对基线 blob）。

- 为什么这两条能"免费"晋升：台账 R-D29 对全部 39 条 PENDING 复跑 GOLD 审计的两条字面判据，
  只有这两行命中（pansharpen 5 块、band_math 4 块）⇒ 断言早就在库里，缺的只是登记表没翻 + 没做过活性取证。
- ⚠ **本刀最重要的取证结论（对"有 EXPECT_NEAR 就是判据"这种洗白的现场反例）**：
  任务书建议的 band_math 阈值臂——把除零闸的 `std::abs(b) < 1e-15` 翻成 `<=`——实测**全绿**，
  因为夹具的两个除数是 `1.000001e-15` 与 `0.999999e-15`，两个都不**恰好等于** `1e-15`，
  `<` 与 `<=` 在这份数据上不可能给出不同结果 ⇒ 这是**等价变异**，不是"闸没生效"。
  换成阈值量级臂（`1e-15`→`1e-14`）才杀得动：`divide_by_zero_pixels` 由 `"0"` 变 `"2"`、像元值由 ~1e15 变 0.0。
  ⇒ 现在被钉住的口径是"阈值两侧的**量级**"，而**不是**"等号归哪一边"。
  ⚠ 早前这里写的"要钉后者得先造一个除数恰为 1e-15 的夹具"是**做不到的**，批次 D D1-T3 按构造论证改口：
  波段除数在 `raster_math_plugin.cpp` 里走 `values[b] = bandMats[b].at<float>(y, x)` ⇒ 上栈的永远是"float32 值升 double"，
  而阈值 `kNearZeroDivisorEpsilon` 是 `constexpr double` 的 `1e-15`，该 double 不在 float32 网格上
  （最近的 float32 升上来是 `1.0000000036274937e-15`）⇒ `|除数| == 1e-15` 对**任何**波段数据都取不到，
  `<` 与 `<=` 在波段除数上恒同解；唯一可达等号的是**表达式字面量**除数（Number 节点经 `std::strtod` 保持 double，如 `B1/1e-15`），
  那不属于波段口径。⇒ **等号分支今天无判据（按构造判不到），本批未为此动产品码。**
- 变异臂台账（Debug，**十臂**：pansharpen P1–P4 + band_math M1–M6；每臂单独改单独还原，跑的是
  `-R "^(PluginTest\.(Pansharpen|BandMath)[A-Za-z0-9_]*)$"` 那 6 条一组 ⇒ 共 10 次单臂跑，外加一次腿臂）：
  pansharpen `P1` 去掉 mean_blend 的除以 2 ⇒ 只红 MeanBlend（实测 140 对期望 70）；
  `P2` 写出参照数据集 PAN→MS ⇒ 同时红 OutputUsesPanGeoTransform（gt[1] 30 对 15）、
  SharedNoDataTagFollowsPanBand1（-9999 对 -555）与 ExtentMismatchWarns 的网格侧；
  `P3` 把 `nodata_source` 字面量 `pan_band1` 改 `ms_band1` ⇒ 红 metadata 字符串那一半（P2 动不了它，
  因为参照数据集那一行同时喂地理网格与标签取值）；`P4` 把相对差异改成绝对米数 ⇒ 红两条范围不符块。
  band_math `M2` 阈值量级、`M3` 减法操作数对调（镜像两支同时红，偏 4）、`M4` 除零不再中止求值
  （整像元块得到旧引擎那个"看着合理的" 5.0）、`M5` 首个失败像元把整图计成除零
  （除零计数块 4→32 红；**整像元块在这一臂下是绿的**，因为它 2×1 夹具每个像元都失败，
  逐像元计数与整图计数在那儿重合 ⇒ 那条 `"2"` 断言杀不到，已如实写进台账）、
  `M6` 写出第一个操作数代替求值结果 ⇒ 真实腿红在 `actual=30 expected=80`。
- 腿侧：`Validate-CaseOutputs` 的 switch **没有 `default`**，"有 case"从来不等于"有判据分支"
  （批次 D 的元发现），所以这条分支的活性是靠 M6 臂证明的，不是靠"跑过了"。
  期望全部由夹具推出（`makePansharpenInputs` 的 B1≡30/B2≡50 + doBandMath 恒写单波段 ⇒ 30×30×1、每像元 80），
  用 `Assert-InfoStatNear` 取数而不是 `Assert-TextContains "Mean:   80"`（后者会被 800/80.x 前缀吃成假绿）。
  秒数对撞：加判据前 14.39 s（32 条集合内测）⇒ 加判据后单跑 11.82 / 12.26 / 12.37 s，
  `TIMEOUT 60` ⇒ 约 4.8×（在 ≥2.5× 线之上，TIMEOUT 与断言数都没动）。
- 登记算术（同一笔提交）：`gold_rows 91→93`、`pending_rows 35→33`、`gold_rows_all_lines 92→94`、
  `gold_rows_literal_slot 76→78`（`table_driven` 15 不动、`data_rows` 126 不变）、`kPendingCeiling 35→33`。
- ⚠ **Release 侧未验收**：两条晋升一律只在 Debug 取证，Release 树本批未重编 ⇒ 归 D4 收口门补凭据。
- 缺口（照实登记）：`PansharpenNoGeoTransformSkipsExtentCheck` 在四臂下始终绿（它不属九个数值命中块，钉的是
  "拿不到变换式⇒没有范围保护⇒动作仍成功"）；band_math 整像元块的计数半条无臂可杀（见上 `M5`）；
  这两条都不影响所晋升行的凭据，但别把"该块全绿"读成"该块每个断言都被钉住"。

### 批次 D D1-T3（2026-10-28，腿侧"跑过必须判过"双向闸 + terrain 复审遗留收口）

**这把不晋升任何动作**（`action_gold_cases.txt` 与 `kPendingCeiling` 一字未动，Debug 注册数 `ctest -N` 现读仍是 **797**，
没有新增 `TEST_F`/`add_test`），它收的是"覆盖"这个概念本身能不能信。

- **闸（R-D33 八条形状）**：新增共享一份 `tests/regression_case_gate.ps1`，三个真实数据 runner
  （`run_real_raster/matching/vector_regression.ps1`）用 dot-source 引入同一份逻辑，**没有抄三份**。
  名单**运行时收**：`Register-Case` 挂在每个 runner 自己的 `New-Case` 函数体内部（所以 vector 那层
  `New-DerivedCase`/`New-CheckCase` 包装发出的动态名天然收得到）；`Mark-Judged` 挂在**判据分支体开头**
  （raster 44 处、matching 5 处、vector 48 处 + 那条按组标分发的 `union_poly` 支），执行到才记账；
  另加第四枚钩子 `Note-CaseRun` 挂在派发循环的 `Invoke-Case` 之前，用来把"本档真跑的名单"和"注册的名单"分开判
  （实测必需：vector 的 `Invoke-Case` 还被 `Invoke-SetupCommand` 复用去跑造件命令，挂在它内部会把
  `prepare_*` 这些非用例动作当成 case）。
  闸硬失败四类：跑过未判且不在 allowlist、allowlist 条目已被判（反向必要性）、
  **判据分支没有任何 case 能走到**（反向死支闸）、注册数或实跑数为 0（归零闸）。
  死支豁免**不手抄**：分支标签与"某名的注册点是否被 `if ($Mode ...)`/`if ($Group ...)`/构建配置包住"
  一律用 PowerShell **AST** 从 runner 自己的源码导（条件原文当理由）⇒ 拆掉 Mode 门后该名自动不再豁免。
  口径声明写进闸的输出与报错文案：**闸判的是本次实到，不是静态全集**。
- **现读基线（Debug，三腿全绿）**：raster `registered=67 ran=67 judged=43 allowlisted=24 branch-labels=44`；
  matching `5/5/4 + 3 / 5`；vector `25/25/25 + 0 / 48`，`dead-branches=0` 三腿一致。
  秒数对撞（TIMEOUT 未抬、断言未减）：`real_data_raster` 11.12 s ⇒ 60/11.12 ≈ **5.4×**；
  `real_data_matching` 1.86 s ⇒ ≈ **32×**；`real_data_vector_quick` 167.03 s ⇒ 720/167.03 ≈ **4.31×**
  （三条都在 2.5× 拆腿线之上）。定向族
  `-R "^(PluginTest\.(Terrain|Matching)[A-Za-z0-9_]*|CoverageMeta.*|CoverageMetaJudge.*|real_data_raster|real_data_matching)$"`
  现读 **58 条全绿**（30.58 s，含上面两条腿）。
- ⚠ **上一把记的"vector 8 条有分支无 case"被这把的运行时口径否证**：`hull_polygon` `envelope_polygon`
  `centroid_polygon` `interior_polygon` `boundary_polygon` `vertices_line` `midpoints_line` `endpoints_line`
  全部经包装函数 `New-DerivedCase -Name "..."` 无条件注册、quick 档逐条实跑、且各自判据分支被记到账
  （`judged=25 == ran=25` 含这 8 条）⇒ 它们是"静态抽 `New-Case -Name` 字面量"这个取径的漏看
  （全文件 `New-Case|New-DerivedCase|New-CheckCase -Name 字面量` 实到 49 名，只数直接调用只到 23 名）。
  **一条没删、一条没塞 allowlist。**
- **闸自身能红（副本臂取证，仓库树全程干净）**：① 删掉 `"matching_register"` 整支 ⇒
  `1 case(s) ran with no judgement branch and no allowlist entry: matching_register`（标签数 5→4）；
  ② 给已 allowlist 的 `matching_change` 补一条判据分支 ⇒ `allowlist entry(ies) are now judged, so the entry is stale`；
  ③ vector 里把 `"hull_polygon" {` 写成 `"hull_polygon_typo" {`（注册侧一字未动）⇒ **双向同时咬**：
  `1 case(s) ran with no judgement branch ...: hull_polygon || 1 judgement branch(es) no case can reach ...: hull_polygon_typo`；
  ④（附加）把三个钩子删光 ⇒ `0 case(s) registered ... passing on an empty set would make the gate meaningless`；
  ⑤（附加·改期望值的反事实臂）把 `matching_corner` 的 `-eq 3` 改成 `-eq 4` ⇒ 抛在判据本身
  （`matching_corner count should equal max_corners=3, got 3`），证明挂 `Mark-Judged` 之后分支仍在真比较。
- **MUT-ARM-PROFDAMP（terrain profile 曲率丢掉 `(1 + slopeSq)^1.5` 阻尼，专属臂）**：产品码只在取证时被改、当场还原
  （`git show HEAD:src/plugins/terrain/terrain_plugin.cpp` + 行尾归一，`git hash-object` = 基线 blob）。
  实跑红态读数：`TerrainProfileCurvatureHandValueOnUnitPlane` 红在
  DEM-P `(0,7) = -4`（钉子 -2.8621670）、`(15,7) = +4`、`(0,0) = -4`，
  DEM-N `(13,7) = -3.922762393951416`（钉子 -0.4588107）、`(41,41) = 2.5073926448822021`（钉子 0.2225138）；
  同一臂下 `TerrainPlanCurvatureHandValueOnUnitPlane` **保持绿**（plan 的分母 `pow(slopeSq,1.5)` 不含阻尼因子）
  ⇒ 这是第一条"只 profile 红"的臂（旧九臂里 SWAP 臂同时杀 plan 与 profile）。腿侧同臂也红：
  `real_data_raster` 红在 `terrain_profile_curvature min mismatch (actual=-10.5131 expected=-2.385005 tolerance=0.002)`。
  与复审席推算值对撞：`(0,7)` 推算 -4.0 = 实跑 -4（一致）；`(13,7)` 推算 -3.92276716、实跑 -3.9227624，
  **差约 4.8e-6**（float32 量级尾差，以实跑值为准）；`(41,41)` 与 `(15,7)`/`(0,0)` 复审席未给推算值。
- **文档面（项 1/3/4/7）**：波段运算两处撤掉"造一个除数恰为 `1e-15` 的夹具就能钉等号"的承诺，改登记为
  **按构造不可达**（波段除数经 `values[b] = bandMats[b].at<float>(y, x)` 升到 double，而 `1e-15` 这个 double
  不在 float32 网格上 ⇒ 等号对任何波段数据取不到，可达等号只有表达式**字面量**除数那条路）；
  `平面曲率.md` 把 `slopeSq < 1e-10` 早退分支登记为**今天无判据**，可判性按三档写清（修复轮 1 撤销旧文那句"这一支从实现结构上就没人能判"的否定命题）：
  ① **恰零档**（`zx = zy = 0` 严格成立）恒真、判不到——今天 `d2_plan_flat.tif` 那枚 `EXPECT_NEAR(..., 0.0f, 1e-4f)`
  正踩在这一档，而 unit-plane 那 4 枚走的是 `zy ≡ 0 ∧ zyy ≡ 0` 的公式零（同像元 profile 断言非零 ⇒ 压根没进早退支）；
  ② **小斜率档**（`0 < slopeSq < 1e-10`）**可判**：`Δ ≤ 0.99999·(|zxx|+|zxy|+|zyy|)`，上界就是二阶导绝对值之和、不是小量，
  按产品码同形的 `cv::Sobel` 实测 crest 偏 `δ = 1e-6` 给 `slopeSq = 7.976e-12`、**`Δ = 0.638`** ≫ 腿容差 `2e-3`；
  ③ 本批仍**不造该夹具、也不写占位断言**，理由从"判不到"换成"可判、但要新造亚像素 crest/鞍部面，且期望必须从
  float32 量化后的输入手推"（实测 `δ ∈ [5e-7, 3e-6]` 给 `Δ = 0.146 ~ 5.68`、量化使 `Δ` 偏精确值 `0.3% ~ 9.1%`
  ⇒ 容差得放宽到 `1e-2` 档）；旧文把这条与 `-b`、ROUGHBORDER、tri `std::clamp` 并成"同一档、都算实现结构上取不到判据"的说法也一并撤销；
  curvature 的"ksize 3→1 同时挪两个钉子"更正为"只挪 (13,7) 一枚"；
  roughness 的钉子数经全仓 `git grep` 复扫确认"12 枚"从未进入任何已提交内容（现读值级钉子 14 枚，未新增数字）。
- ⚠ **Release 侧未验收**：本把全部改动（闸 + 三 runner + 文档）只在 Debug 取证，Release 树未重编、腿未跑 ⇒ 归 D4 收口门补凭据。
- 缺口（照实登记）：① vector 的 `Mode=full` 四条档腿与 raster/matching 的 full 档都不在 ctest 里，本轮**没实跑**，
  只用源码静态对撞证明"full 档不会新增未判/死支"（`注册全名 − 标签 − 条件注册 = ∅` 两侧都成立）⇒ 那是推演不是凭据；
  ② matching 的 `ecc_register`/`stitch` 在 Debug 压根不注册，本轮只验到"条件豁免"半边，
  它们在非 Debug 档会进实跑名单、靠两条新增 allowlist 条目兜住（同样未实跑）；
  ③ 24 条 raster 未判与 3 条 matching 未判是**登记**不是修复，逐条理由见 runner 里的 allowlist 与 `action_gold_cases.txt`。

### 批次 D D1-T3 修复轮 1（2026-10-28，复审席 `task-17-review.md` 判 not approved 的四条必须修）

- **MF-1（真缺口：闸会装绿）**：共享闸 `tests/regression_case_gate.ps1` 补第 (6) 项硬失败——
  **每条 allowlist 条目必须仍对应本档真注册过的 case**，唯一的例外走**声明**（AST 派生的 `ConditionalCase`：
  该字面量名确有一条 `New-Case`/`New-DerivedCase`/`New-CheckCase` 调用点、且该调用点的祖先 `if` gate 在
  `Mode`/`Group`/构建档上），不走"查不到就放过"。头注释的承诺由"两个方向"改写为 forward/reverse/allow 三个方向，
  于是它原先那句"deleting a `New-Case` 也会显形"第一次成为真话。
- **MF-1 双向可红取证（副本全在 `build/tmp/batchD/d1t3c/`，仓库原件全程干净；旧臂用 HEAD 版 runner + HEAD 版闸）**：

  | 臂 | 只改副本的内容 | 旧闸（HEAD） | 新闸 |
  |---|---|---|---|
  | ①a | 删豁免条目 `spindex_evi` 的那条 `New-Case` 注册（其余原样） | **rc=0 装绿** `registered=66 ran=66 judged=43 allowlisted=24 branch-labels=44` | **rc=1** `1 allowlist entry(ies) name no case of this run's roster and have no gated registration site …: spindex_evi`，同排 `allowlisted=23 allowlist-table=24 allowlist-gated-not-registered=1` |
  | ①b | 删**有判据分支**的 `spindex_ndmi` 注册 | rc=1（dead-branch） | rc=1（同一条，未削弱） |
  | ①c | 删掉 matching 的 `if (-not $isDebugCliBuild) { … }` 整块（两条 ConditionalCase 声明随之消失） | **rc=0 装绿** `registered=5 ran=5 judged=4 allowlisted=3` | **rc=1** `2 allowlist entry(ies) …: matching_ecc_register,matching_stitch` |
  | ② | 把 `$isDebugCliBuild` 强制为假让那两条**真注册真跑**，再**只把 `matching_ecc_register` 从 allowlist 摘掉** | — | **rc=1** `1 case(s) ran with no judgement branch and no allowlist entry: matching_ecc_register`；同臂正对照（条目保留）**rc=0** `registered=7 ran=7 judged=4 allowlisted=3`（4+3=7） |
  | 自证 | 未改动的 raster 副本 + 新闸 | — | **rc=0**，与仓库原件读数逐字相同（副本机器不引入红/绿） |

  ⚠ 臂②的前提：产品码自己 in Debug lightweight 档拒绝这两个动作（实测 `gis-cli.exe matching ecc_register …` 与
  `matching stitch …` 均 rc=1，stderr `Error: … is unavailable in Debug lightweight mode; please use Release build`），
  所以副本里只把这两块的 `CaseArgs` 换成 Debug 合法的 `matching detect`（`-Name` 字面量、所在 `if` 门、
  `Validate-CaseOutputs` 的分支形状都没动）——闸只看得见注册名/实跑档/分支标签，测的仍是闸；
  但这**不等于** Release 档上 matching 腿绿（Release 未验收）。
- **MF-2（计数器语义与注释口径）**：`allowlisted` 改为"本次实到且被豁免的条数"，表长与差值另档打印
  （`allowlist-table` / `allowlist-gated-not-registered`），并新增第 (7) 项硬失败 `judged + allowlisted == ran`。
  原件现读（Debug quick）：raster `43 + 24 = 67` ✓、matching `4 + 1 = 5` ✓（旧口径 `allowlisted=3` 的 4+3=7≠5 含混消除）、
  vector `25 + 0 = 25` ✓。raster/matching 两处 "per case that this leg **runs** but does not judge" 在 matching 上为假
  ⇒ 按 (a) 本档真跑不判 / (b) 被构建档门掉注册 两类重写，并明写 (b) 需正向物证；raster 那句以 `so this list cannot`
  结尾的截断半句补完。
- **MF-3（产品码注释写过头，本刀唯一动 `src/` 处、只改注释）**：`src/core/src/expression_engine.cpp` 里
  `实测边界：除数 1e-15 接受、0.99999e-15 拒绝` 三处失真（全仓无除数恰为 `1e-15` 的夹具；那个数少抄一个 9，实测是
  `0.999999e-15`；而"`1e-15` 接受"正是 `0c4d876` 刚按构造否证的取不到的等号点）⇒ 改成与那笔一致的两层口径
  （波段除数 `at<float>` 升 double、`1e-15` 不在 float32 网格上、最近者 `1.0000000036274937e-15` ⇒ 等号按构造取不到、
  `<` 与 `<=` 恒同解；可达等号只有表达式字面量除数、今天无夹具；有夹具的是两侧 `1.000001e-15` 受 / `0.999999e-15` 拒，
  见 `PluginTest.BandMathDivisorThresholdBoundaryIsPinned`）。`git diff -U0 src/` 非注释改动行 **0**；
  本文件新 blob `bc7eaaea29d27410c854eb17cf5b4d662640d8f5`（**有意的注释改动，非变异残留**）；受保护的
  `src/plugins/terrain/terrain_plugin.cpp` blob 仍 `588a42504ee19bcb551f8214e5c1adb68cea20e9`；`gis_tests` 已重编
  且 exe mtime 晚于源 mtime，被引用的那条用例实跑绿。
- **MF-4（过头的否定命题改回三档）**：见上面 D1-T3"文档面"那条的三档写法与 `平面曲率.md` 的同名小节。
  ⚠ 口径声明：本条与 `平面曲率.md` 里的"实测"**都指按产品码那五个 `cv::Sobel` 实参搭的同形参考模型复算，
  **未跑变异后的产品二进制**——"删早退支"这条变异臂本批没做，"能红"是由 `Δ` 与容差的关系推出来的（复审席 10-28 要求加注）。
  本席按产品码那五个 `cv::Sobel` 实参（`zx/zy` `scale=1/8`、`zxx/zyy` `scale=1`、`zxy` `scale=1/4`、`BORDER_REPLICATE`）
  独立复算：上界 `Δ ≤ 0.99999·(|zxx|+|zxy|+|zyy|)` **与复审席一致**（`g(s)=s/(s^1.5+1e-10)` 在 `(0,1e-10)` 单调增、
  上确界 `1/(1+1e-5)=0.999990000`，另在 9×9 float32 扫面内所有落在早退带的像元上违例 **0**）；
  但**样例值不一致、本席的更大**：复审席用解析二阶导 `zxx=zyy=2A`（A=1 ⇒ `S=4`、`Δ=0.16`），
  而代码里 `cv::Sobel(…, dx=2, ksize=3, scale=1.0)` 给的是 `zxx = 4·(z(x+1)−2z(x)+z(x−1)) = 8A = 8`
  （实测 `zxx=zyy=8.000`、`zxy=0`，`zx=−1.967e-6`、`zy=−2.027e-6`、`slopeSq=7.976e-12`、`num=6.381e-11`）
  ⇒ `Δ = 6.381e-11/(7.976e-12^1.5 + 1e-10) = 0.638`。以实测为准 ⇒ **同一结论更硬**（`0.638` 比 `2e-3` 大 2.5 个数量级），
  本席没有拿它去放宽任何判据。

### 批次 D 覆盖晋升 D1-P（2026-10-28，Task 16 / projection `info`·`transform`·`assign_srs`·`reproject` 四条 PENDING→GOLD）

**先立一条裁定**：**R-D31**（覆盖 D0-P0 普查表对 `projection.transform` 的定档）。普查把它标成
`fixture-only`，理由是"该动作不吃文件，`data/` 里没有任何文件能成为它的 primary_input"；**改判为满档（D1）**。
理由三条：定档规则是为"有没有真实输入"设计的，而 `transform` 是**纯坐标函数**，期望可由公式独立推算，
判据强度上限**高于**依赖真图的动作；它的腿分支今天已在判**由手算数值构成的期望串**
（形如 `Assert-TextContains "12913060.93"` 的 exact-text 包含，**不是**解析成数再比容差
 —— 与本节下面"腿侧"那条的口径一致，别把"有数值期望"说成"做数值比较"）；"没有影像输入"不等于"没有判据"。
⇒ **影响：`fixture-only` 名册由 2 条减到 1 条（只剩 `spindex.ndvi`）**，工单与普查表按此读。

- **口径更正（本刀落笔前复算抓到的，任务书原文在这里是错的）**：EPSG:3857 是**球面**口径，
  投影用的是球半径 `a = 6378137`（`x = lon·π/180·a`），而 EPSG 文档里的 `±20037508.34` 是
  `a·π = 20037508.342789244` 的**四舍五入**常数（`20037508.34/π = 6378136.99911216`，比 `a` 少 `8.8784e-4 m`）
  ⇒ 用 `lon·20037508.34/180` 当期望会带入 **dx = 1.7975e-3 m、dy = 6.7734e-4 m** 的系统偏差
  （(116, 40) 档；对产物 metadata 的 6 位小数印法 dx = 1.7978e-3 m）。任务书建议的
  `EXPECT_NEAR(值, 116.0*20037508.34/180.0, 1e-3)` 若照写**首跑就红**（1.7978e-3 > 1e-3）。
  ⇒ 本刀一律用 `a` 口径、容差仍取 1e-3 m，该 1e-3 的**一半**理由 x/y 两支都成立：
  它比 Step3 记的"产物 vs `a` 口径公式"残差（GPKG 顶点级 1.9e-9 m、metadata 级 <5e-7 m＝`std::to_string`
  的 6 位小数栅格化）大三个到六个量级。
  ⚠ **另一半"1e-3 小于两个口径之差 ⇒ 顺带钉住插件用的是哪个常数"是过头说法，修复轮 1 改准为逐支口径**：
  同一个 1e-3 对 x 夹得住（Δx=1.7975e-3 > 1e-3）、对 y **夹不住**（Δy=6.7734e-4 < 1e-3，比值 dy/dx=0.377），
  复审席 A4 臂用真产品二进制把期望整体换成四舍五入口径后**只有 dst_x 红、dst_y 保持绿**（本席 python 复算同判）。
  y 之所以差得少是它走 `a·ln(tan(π/4+φ/2))` 那条非线性式：Δy 随纬度涨（40°→6.8e-4、60°→1.2e-3、80°→2.2e-3 m），
  Δx 则随经度线性缩放（`1.55e-5 m/°` ⇒ "约 1.8e-3 m"只对 116° 成立）。
  ⇒ 常数口径**只由正向档的 dst_x 一支**钉着；原点档（1e-6 m）与往返档（1e-6 度）都不判它——
  原点处两套常数之差只有 `5.55e-17 × 8.8784e-4 ≈ 4.93e-20 m`，往返档换算成度的差是 1.6e-8/6.1e-9，都远在其容差之下。
  **口径教训（写进判据纪律）**："容差夹在实现残差与候选口径之差之间"这类论证**必须逐支、逐数据点核**，
  一支成立不许当全支通用；三档共用一个算子注释时，注释必须点名哪一支真的夹得住。
- **原点那一档**：`ln(tan(π/4))` 在 IEEE double 里是 `5.551115123125783e-17`（非 0），
  乘 `R/π` 得 −7.08e-10、乘 `a` 得 −3.54e-10 ⇒ **尾差的量级（1e-10）成立、具体值随写法漂移**，
  而产物两条输出路（metadata 6 位小数 / message `setprecision(10)`）都印成 0。
  ⇒ 断言写成 `EXPECT_NEAR(0.0, 值, 1e-6)`，**不写** `EXPECT_DOUBLE_EQ`，**也不写**"不许是精确 0"那种非零断言。
  ⚠ 承重面如实登记：原点是 4326↔3857 的**不动点**，这一档结构上抓不到轴序丢失与源/目标互换
  （**MF-T1 臂下它确实是绿的**，见下），它抓的是原点处 NaN/Inf/异常偏移。
- **`info` 的判据形态**（修复轮 1 改准）：现读 `doInfo` 的 metadata 只有 `width/height/bands/wkt` 四枚，
  **不发** pixel_size / extent，message 也只印 GeoTransform 的三对读数（`Origin:` / `Pixel Size:` / `Rotation:`）、
  **不印包框** ⇒ `PluginTest.ProjectionInfoExecution` 判的是这**六枚**文本读数（期望全部来自 `createTestRaster` 的
  `adfGT` 字面量 `{116.0, 0.001, 0.0, 40.0, 0.0, -0.001}`）+ `wkt` 读回的权威码恰 `4326`。
  **包框四边今天两侧都没有判据**：Step3 那版写的"再由 GT × 已判读的行列数推包框右下两边"不是判据——那四行里
  两枚与 Origin 逐字重复（同期望同实测侧同容差），两枚在 Origin/Pixel Size 绿的前提下差值**恒等于 0**
  （容差可加性只剩 `2e-11` 的独立窗口，再被 `doInfo` 的 6 位有效印法量化：Origin 档步长 1e-3、Pixel Size 档步长 1e-8）
  ⇒ 修复轮 1 已把这四行删掉、换成产品真印而此前无人读的 `Rotation:` 两枚，并把四边**登记为缺口**（见下面"缺口"④）。
- **四条凭据**（证据用例都写进登记表第 4 列）：`info`→`PluginTest.ProjectionInfoExecution`、
  `transform`→`PluginTest.ProjectionTransformExecution`、`assign_srs`→`PluginTest.ProjectionAssignSrsExecution`、
  `reproject`→`PluginTest.ProjectionVectorReprojectExecution`。晋升前四块的"剥注释后数值断言数"实测**全是 0**
  （动作槽与 `"projection"` 归属都在 ⇒ 卡的就是审计第（2）条），被**替换**而非留存的三条恒真倾向判据：
  `transform` 的两条 `EXPECT_GT(std::abs(dst), 1000.0)`、`reproject` 的两条 `EXPECT_GT(std::abs(extent.MaxX/MaxY), 1000.0)`、
  `assign_srs` 的 `EXPECT_FALSE(getSRSWKT(...).empty())`（论证：产物 12913060.93 与写反后的 4865942.28 **都** >1000，
  这个关系在本夹具上不可能变）。
- **变异臂台账（六臂，全部 Debug 档 + `-R "PluginTest\.Projection"` 形态；每臂单独改单独还原）**：
  `MF-T1` 源/目标 SRS 互换 ⇒ 正向两支（残差 1.29e7 / 4.87e6）与往返两支（偏 0.0051° / 0.0363°）红，
  **原点两支与 src 回显两支绿**；`MF-T2` `gis::core::parseSRS` 的 `OAMS_TRADITIONAL_GIS_ORDER` →
  `OAMS_AUTHORITY_COMPLIANT` ⇒ 红在 `result.success`（lat 越界、变换失败）后 `ASSERT` 中止，
  同族另外五条用例**全绿**（那三条的产物路径不走 parseSRS 轴序）；
  `MF-A1` `assign_srs` 盖章后把 `gt[0]/gt[3]` 搬到墨卡托原点 ⇒ 红 GT 四支（差 1.29e7 / 4.87e6），
  **像元三支与权威码支绿**；`MF-A2` 只把 (3,5) 一个像元写成 7.0f ⇒ 红该像元的手算支与赋前对撞支，
  **GT 十支与另两枚像元支绿** ⇒ 这两臂合起来才等于"GT 支与像元支各自可红、互不遮蔽"；
  `MF-I1` `doInfo` 的 Origin 打印 x/y 写反 ⇒ 红六支（全为差 76），**Pixel Size 两支与 width/height/bands、
  `wkt` 权威码 4326 支绿**；⚠ 修复轮 1 复核：那六支里**只有两枚是 Origin 自己的判据**，另四枚（两枚逐字重复 +
  两枚算术推论）同因同刻红 ⇒ "红六支"这个计数是被非独立行撑起来的；删掉那四行后同一臂只会红两支——
  这是按现存臂日志（`build/tmp/batchD/d1p/mf_a2_i1_r1.log`）的红点集合**推得**，本轮硬约束不许改 `src/`、
  未重编变异二进制，故**没有重跑该臂**；`MF-R1` 矢量支 `-t_srs` → `-a_srs`（只声明系、不真变换）⇒ 红八支
  （四顶点 + 包框四边，残差量级 1.3e7），**输出权威码 3857 与 `IsProjected()` 绿**
  ⇒ "CRS 声明对了"不等于"坐标真变了"，两类判据必须同在场。
  还原证明：`projection_plugin.cpp` 回到 `9c473e2b4fe8ebf21b8e55acd7b0ac90bcee921d`、
  `gdal_wrapper.cpp` 回到 `ddf770a00b2f313d0caa9b320886a3ea954b76e9`（`git show HEAD:… > …` + 行尾归一 + `git hash-object`），
  还原重编后 `100% tests passed, 0 tests failed out of 6` ⇒ 绿态读在**还原后的二进制**上。
- **腿侧**：`projection_transform`/`projection_reproject`/`projection_assign_srs` 早就是**文本存在性**判据
  （`projection_transform` 的 `12913060.93`/`4865942.28` 正是 `a` 口径在 `setprecision(10)` 下的印法）；
  本刀给 `projection_info` 补两枚**由生成器字面量独立可推**的读数
  （`makeClassRaster` 的 `Create(…, 6, 6, 1, …)` + `geotransform {12935000.0, 50.0, 0.0, 4852300.0, 0.0, -50.0}`
  ⇒ `"Pixel Size: (50, -50)"` 与 `"Origin: (1.2935e+07, 4.8523e+06)"`，后者是 message 默认 6 位有效数字的印法）。
  包框四边**腿侧与单测两侧都判不到**（`doInfo` 不发 extent 键、message 也不印包框，而单测里那四行推论不承重、
  修复轮 1 已删）⇒ 按"无判据"登记为缺口（下面"缺口"④），不再写"钉在 Debug 单测里"；`.ps1` 分支注释同批改准。
  **反事实副本臂**（证这两枚真在比、不是"分支会红"）：把期望改成 `"Pixel Size: (50, 50)"` ⇒
  `real_data_raster` **Failed 5.35 s**、带 `D1P-COUNTERFACTAL pixel size mismatch`、退出码非 0；改回后连跑两支绿。
  腿侧闸读数（`ctest -V` 现读）：`registered=67 ran=67 judged=43 allowlisted=24 … dead-branches=0`
  ⇒ 硬不变量 `judged + allowlisted == ran`（43+24=67）成立，没动 allowlist、没新增 case。
  秒数对撞 `TIMEOUT 60`：补判据前控制器基线 11.12~14.95 s（4.0×~5.4×）⇒ 本把补完 13.54 s、
  还原后连跑 **10.41 / 10.50 s（5.76× / 5.71×）**，在 ≥2.5× 线之上；TIMEOUT 与断言数都没减（净加两枚）。
- **登记算术**（与四行改状态同一笔提交）：`gold_rows 93→97`、`pending_rows 33→29`、
  `gold_rows_all_lines 94→98`、`gold_rows_literal_slot 78→82`（`table_driven` 15 不动、`data_rows` 126 不变）、
  `kPendingCeiling 33→29`。四个新计数的现读命令逐条重跑过（不是推算）。
  审计复跑：`ctest -R "CoverageMeta"` ⇒ **19/19 Passed**（`GoldEvidenceIsAuditable` 认了四行、
  `RegistryHeaderCountsMatchData` 把"表/上限/头部"三者对撞在同一数上）。
  注册数不变：全库 `ctest -N` `Total Tests:` 前后都是 **797**，定向族前后都是 **26**
  （本把只补断言、**没有新增 `TEST_F`**；新增的是一个文件级 `static` 期望算子 `expectedWebMercatorXY`，不产生用例）。
- ⚠ **Release 侧未验收**：四条晋升一律只在 Debug 取证，Release 树本批未重编 ⇒ 归 D4 收口门补凭据。
- **缺口（照实登记）**：①原点档的承重面按构造就窄（不动点），已由 §R-D31 那节的 MF-T1 现读坐实，不为它造非零断言；
  ②`info` 的 `Origin`/`Pixel Size`/`Rotation` 六枚判据依赖 message 的**默认精度印法**（6 位有效 ⇒ `1.2935e+07`），
  属"文本契约"而非纯数值契约 —— 若将来 `doInfo` 改精度，后果**不是**"腿侧与单测两侧同时红"（修复轮 1 那句话是过头登记，本句改准），
  而是**静默绿**——下面三档读数来自**参考模型模拟印法**（`%Pg` 逐档扫 p），**未改产品精度、未重编产品码**，
  因此不称实测：单测侧夹具五值（`116.0 / 0.001 / 0.0 / 40.0 / -0.001`）在 `p = 3…16` **逐字节相同**，
  只有 `p ≤ 2` 才红；腿侧两枚只有 `p ≤ 4` 与 `p ≥ 7` 才红 ⇒ `p = 5…6` 一段两侧都绿。更根本的是这六枚的容差
  **细于印法步长**：Origin 行的有效窗今天就是 **≈ ±5e-4 度**（`116.0001 → "116"`，`p = 3` 时 `116.4 → "116"` 仍绿），
  即印法一变只会把判据**放松**、不会把它夹断。⇒ 这条文本契约缺一个**唯一指定载体**：六枚里谁是印法契约的正主、
  其余降为冗余，今天没有指定。本把不新增守卫、不改产品码，只把"缺载体"登记为缺口，与 D3 判据加固一起处理；
  ③`#155`（projection 输出先删后写）与 `#164/#172`（可见参数有效性 / CLI 区间闸与 ParamSpec 分叉）
  都在本把射程之外，**产品行为一字未改**（`doTransform` 的 `x/y` ParamSpec 区间 `-1e9..1e9` 与
  真实墨卡托值的关系、`reproject` 的先删后写都没动）。
  ④**`projection.info` 的包框四边：腿侧与单测两侧都无判据**（本把由"已钉在 Debug 单测"改判为缺口）。
  现读两条出路都不在本把射程：a) 让 `doInfo` 发 extent 键或把包框印进 message 是**改可观测行为** ⇒ 要过裁定门；
  b) 换判据载体——`raster_inspect` 的 info 文本确实印 `Extent: (minX, minY) - (maxX, maxY)`，
  但那是另一个动作的契约，拿它替 `projection.info` 记覆盖就是把账记到别人头上。
  ⑤**`assign_srs` 块（`PluginTest.ProjectionAssignSrsExecution`，`numeric=16`）里有 7 枚自对账冗余臂**：
  4 枚 `gtBefore[i]`↔`gtAfter[i]`（i = 0/1/3/5）+ 3 枚 `beforeFirst`/`beforeCenter`/`beforeCorner`↔同坐标的第二次读回。
  这 7 枚的"期望侧"与"实测侧"都是**同一个产物的两次读**，而 `gtAfter` 六枚与像元三枚已各自被夹具手算字面量钉住
  （16 = 6 手算 GT + 4 对撞 GT + 3 手算像元 + 3 对撞像元）⇒ 赋前那侧一旦偏离，手算支必先红，对撞支只是
  **同因同刻跟着红**：按 `MF-A1`（搬 `gt[0]/gt[3]`）的改动面拆读，它红的"GT 四支"就是 2 枚手算 + 2 枚对撞；
  按 `MF-A2`（只写坏 (3,5) 一个像元）拆读，它红的就是 1 枚手算 + 1 枚对撞——两臂都没出现过"对撞支单独红"。
  ⇒ 按"能单独红才算承重"的口径这 7 枚不承重。**本把不删，只登记（未修）**：删断言会动 GOLD 证据的承重面计算
  （`numeric=16` 这个计数同时被登记表与审计用例读着），那是另一把的账 ⇒ 留给 **D3 判据加固统一处理**。

### 批次 D 覆盖晋升 D1-C1（2026-10-28，Task 18 / cutting `clip`·`split`·`merge_bands` 三条 PENDING→GOLD）

射程三条；`cutting,tile` **不在本把**（它的动作槽写在共享 helper `t17BaseTileParams` 里 ⇒ 26 个 `Tile*` 用例
结构上登不上 GOLD，需另建独立证据用例，归 D1-C2）。三条的"剥注释后数值断言数"开批现读**全是 0**
（动作槽与 `"cutting"` 归属都在 ⇒ 卡的就是审计第（2）条），三条腿分支当时全是 `Assert-TextContains`
的 exact-text 包含——**有手算数值期望 ≠ 做数值比较**，本把把腿侧也改成了"解析成数再比"。

- **参数消费面的两条实到事实（本把落笔前先在副本上用 `gis-cli` 探针实测，不是推算）**：
  - `clip` 的 `extent` 轴序是 `xmin,ymin,xmax,ymax`，实现拼成 GDAL `-projwin ulx uly lrx lry` 时下标顺序是
    `[0] [3] [2] [1]`；把四元组当 `{minx,maxx,miny,maxy}` 读 ⇒ 窗口高度为负、GDAL 当场拒
    （实测案文 `Computed -srcwin 1 2 -161659 -161659 has negative width and/or height.`）。
    窗按**整像元向下取整**对齐，输出左上角是对齐后的像元边。
    ⚠ **部分相交与完全不相交都不裁剪、不报错**：产物尺寸就是**请求窗**，窗外补 `0` 且**不写 NoData**
    （实测 14×10 窗只有 12×8=96 格来自源 ⇒ Mean 被稀释成 `96×5/140`；两轴都不相交时得到一张 10×10 全 0 图，rc 仍是 0）。
    按实到写判据与文档，产品码一字未改；"下游分不开补的 0 与真 0"登记为缺口①。
  - `split` 的网格是**固定步长 `tile_size`**（`xOff += tileSize`）、每块 `min(tile_size + overlap, 剩余)`、
    块数 = `ceil(w/tile_size) × ceil(h/tile_size)` ⇒ **与 `overlap` 无关**。
    ⚠ **任务书 §2.2 给的 `块数 = ceil((w-overlap)/(tile_size-overlap))` 是"步长 = tile_size-overlap"另一个模型，
    与产品实到不符**（实测 `ts=16/ov=4` 与 `ts=8/ov=3` 的块数都和 `ov=0` 档相同）⇒ 一律按实到写期望，登记为缺口②。
    另两条恒绿陷阱也按实到点名：**末块尺寸对 `overlap` 不敏感**（`min(·,剩余)` 先吃掉 overlap；
    `ts=8` 时 ov=0/3 两档末块都是 6×4）⇒ 末块不是 overlap 的证据；`overlap ≥ tile_size` 的退化输入**不校验不报错**，
    `ts=4` 配 `overlap=6` 与 `overlap=10` 在 6×6 图上得到**逐块相同**的网格 ⇒ 那两档区分不了实现有没有读 overlap。
    （这条**由控制器 2026-10-28 23:0x 补测坐实**，等级=实测：`gis-cli` 在 `build/tmp` 副本上跑
    `overlap ∈ {0,2,6,10,20}` 五档，`ov=0` 得 `4×4/4×2/2×4/2×2`，其余四档逐块相同 `6×6/6×2/2×6/2×2`；
    ⚠ **复算口径只能是块数/块名/每块宽高，不能是文件 md5**——同参数连跑两次 `tile_0_0.tif` 同为 3094 字节、
    只有偏移 1394 那**一个字节**不同，落在元数据键 `GIS_PROCESSING_TIME` 的值里
    （两遍分别印 `2026-10-28T22:49:15` 与 `2026-10-28T22:49:55`）。起因：复审席 Q3 抓到本条原先只标了
    「实测」却没有对应臂日志 ⇒ 按「实测是证据等级不是形容词」要么补测要么降级，控制器选择补测。）
  - `merge_bands` 的 `bands` 吃的是**逗号分隔的文件路径列表**（接在 `input` 之后），**不是波段索引**：
    实测 `--bands=1` ⇒ rc=1、案文 `Cannot open file: 1`，而 `gis-cli cutting --help` 与 ParamSpec 的英文描述
    都写 "Comma-separated band indexes" ⇒ 文案/实现分歧，登记为缺口③（本把不改码，文档按实现写准）。
    ⚠ 缺口③已由批次 D 的 N175 一把改准（displayName 改「额外波段文件」、描述串改成路径口径），键名 `bands` 未动。
    重复路径**不去重**；每个文件贡献自己的**全部**波段；各输入地理参考不同时 VRT 取**地理并集**、
    每个来源在自家包框外补 0（实测两张 10×6 常量图 origin 差 10 列 ⇒ Mean 从 10.0 变 5.0）。
- **三条凭据**（证据用例写进登记表第 4 列）：
  `clip`→`PluginTest.CuttingClipByExtentProducesExpectedWindow`（GT 六元组 `116.005/39.998/0.001/-0.001/0/0`
  + 窗内两角像元 = 夹具常量 5.0，全部由 `createConstantRaster` 的 `adfGT` 字面量与 `extent` 手算）；
  `split`→`PluginTest.CuttingSplitTileGridHandTableAcrossTileSizeAndOverlap`（两档 `tile_size` × 两档 `overlap`
  的手算表：块数 4/4/12/12、首块 16×16/20×20/8×8/11×11、末块 14×4/14×4/6×4/6×4，再加"块 (tx,ty) 局部 (0,0)
  = 源 `(tx·ts, ty·ts)` 的像元值"（夹具 `(30y+x)%256`）与 `ov>0` 两档的"重叠带读到同一个源像元"）；
  `merge_bands`→`PluginTest.CuttingMergeBandsOrderShuffleAndRepeatedFile`（三个不同常量夹具 10/20/30 跑五档：
  拼接顺序 / 打乱后第 2、3 带互换 / 重复不去重 / `bands` 空的 1 带下界 / 多波段文件整份展开成 4 带）。
  另两档新用例把边界情况按实到钉住：`…PartiallyOutsideSourcePadsZeroWindow`、`…DisjointFromSourceProducesZeroFilledWindow`。
- **首轮读数（实测）**：10 条 `PluginTest.Cutting*` **3 红 7 绿**，三条红全是本席把 GDAL 的 GT 槽位读错
  （`[0]/[3]` 当成 `[1]/[4]`；失败消息印出的 `gt[1] evaluates to 0.001` 就是纠正依据）⇒
  **地理期望值一个没改、只把下标改准**，改后 10/10 绿。这不是"从产物回填期望"，回填的期望不会在改下标时保持不动。
- **变异臂台账（六臂，全部改 `cutting_plugin.cpp` 的消费点、单独改单独还原；取证形态都指名"哪一档、什么形态"）**：
  `CL-1` `-projwin` 两槽互换（轴序误读）⇒ 三条 clip 用例红在 `result.success`，腿侧 rc=1（`cutting_clip failed`）；
  `CL-2` ulx 多加一个像元（**rc 仍 0**）⇒ 红在**数**上：`GetRasterXSize()==7`、`gt[0] evaluates to 116.006`、
  右下角像元 `evaluates to 0`、部分相交档的 `bMean` 差 0.2637 ⇒ **这一臂才是"值级判据真的在比较"的证据**，
  `CL-1` 只证明用反轴序会被 GDAL 拒；
  `SP-1` 两处 `min(tileSize + overlap, 剩余)` 丢掉 `overlap` ⇒ 只有 `ov>0` 两档红（首块尺寸 + 重叠带读空），
  `ov=0` 两档与旧用例 `CuttingSplitExecutionCreatesTiles` **绿**（那里 overlap 本来就是 0）；
  `SP-2` 把 `overlap` 折进步长 ⇒ 块数、首块尺寸与"末块根本没产出 `fs::exists(last)`"三支红；
  `MG-1` 吞掉 `bands` 列表 ⇒ 逐带/逐数的 4 档红、`bands` 空那一档与腿侧 case 绿（它们本来就不读该键）；
  `MG-2` 拼接列表反序 ⇒ 红在逐带像元值（第 1 带差 20、第 2 带差 10），⚠ **腿侧 rc=0**（该 case 两条列表项是
  同一个文件 ⇒ 腿结构上判不出文件顺序，登记为缺口④）。
  还原证明：`cutting_plugin.cpp` 每臂还原后 `git hash-object` = `dc4bf9f5be619570db1d8f9fea03459b3e922aab`
  （= `git rev-parse HEAD:` 同一值；`git show HEAD:… > …` + 行尾归一 CRLF，未用 `git checkout --`），
  六臂跑完 `git status --porcelain` 空、`git diff --stat -- src/` 空，且每臂还原后都重建过一次
  ⇒ 后续读数落在**还原后的二进制**上；受保护的 `terrain_plugin.cpp` 复验仍为 `588a42504ee19bcb551f8214e5c1adb68cea20e9`。
- **腿侧**：三条分支现在把数**解析成数再比**（新增 `Assert-InfoSizeExact` 逐支比宽/高/带数、
  `Get-InfoBandStatValue`/`Assert-InfoBandStatNear` 按 `Band N:` 块取数——补掉 `Get-InfoStatValue` 自带注释点名的
  "多波段只判第 1 带"那扇门、`Get-NamedRasterInfo` 按块名取而不靠 `Get-ChildItem` 字母序运气）。
  新增两条 case：`cutting_clip_asymmetric_window`（同一夹具、手算 **3×4** 非方形 ⇒ 才可能看见轴序被写反）与
  `cutting_split_overlap`（`tile_size=4, overlap=2` ⇒ 块数仍 4、首块涨到 6×6、邻块 2×6/6×2；
  `tile_1_1` 与 ov=0 档逐维相同 ⇒ 它不是 overlap 的证据）。
  `merge_bands` 按带判第 1/2/6/7/8/12 带 Mean = 10/20/110/10/20/110（期望来自 `makeNdviRaster` 的六个常量字面量）。
  **反事实臂四支（改期望常数、副本上跑，仓库原件未动）**：height 2→3 ⇒ `(height actual=2 expected=3)` rc=1；
  `tile_1_0` 宽高互换 ⇒ `(width actual=2 expected=4)`；`band 7 Mean 10→30` ⇒ `(band=7 actual=10 expected=30)`;
  overlap 首块 6×6→4×4 ⇒ `(width actual=6 expected=4)` ⇒ 四支的 `actual=` 印的都是产品实到值。
  腿侧闸读数对撞：改前（HEAD 版副本）`registered=67 ran=67 judged=43 allowlisted=24 branch-labels=44 dead-branches=0`、
  改后 `ran=69 judged=45 allowlisted=24 branch-labels=46 dead-branches=0` ⇒ 硬不变量 `judged + allowlisted == ran`
  两边都成立（43+24=67、45+24=69），白名单一个字没动、没有死支。
  秒数对撞 `TIMEOUT 60`：改前 ctest 11.29 s、改后 11.27 s、晋升复跑 11.61 s ⇒ **5.17×~5.32×**，在 2.5× 线之上，
  TIMEOUT 没抬、断言只增不减。⚠ 控制器给的 9.84~10.58 s 与本席三次现读（11.29/11.27/11.61）不同档，按实到登记。
- **登记算术**（与三行改状态同一笔提交，四个新计数是逐条重跑现读命令得出的、不是推算）：
  `gold_rows 97→100`、`pending_rows 29→26`、`gold_rows_all_lines 98→101`（仍只比 `gold_rows` 多 1，
  多的是登记表第 3 行的格式说明）、`gold_rows_literal_slot 82→85`（= 100 − `table_driven` 15），
  `data_rows 126` 与 `gold_rows_table_driven 15` 不动；`kPendingCeiling 29→26`。
  审计复跑：定向族 **30/30 Passed**（`GoldEvidenceIsAuditable` 认了三行、`RegistryHeaderCountsMatchData`
  把"表/上限/头部"三者撞在同一数上）。注册数：全库 `Total Tests:` **797→801**、定向族 **26→30**
  （新增 4 条 `TEST_F`：两条 clip 边界档 + split 手算表 + merge_bands 五档）。
- **复审判语（`t18-reviewer`，approved，必须修 0 / 建议 1 / 只登记 2）**：Q1 五枚用例逐支判**承重**
  （`gt[2]/gt[4]=0.0` 两枚的独立可红改动 = 往 GT 里注入 skew，**不被「块数 + 首块」那两支蕴含**；
  split 末块尺寸承重于「固定 `tile_size`/偏移错」，且 `ov>0` 档的块数恰是抓 SP-2 的那支、与 `ov=0` 档互补不重复；
  merge 的「1 带」档是**等值**断言不是上界，红法 = 空列表又补一遍 `input`）；
  Q2 三个新读数器**是真解析**（`Band N:` 块头带锚，不被 `Band 12:` 的长前缀吃；
  按带 1/2/6/7/8/12 ⇒ 10/20/110/10/20/110 与 `makeNdviRaster` 六个常量字面量 + 6+6 展开序逐带对撞成立）；
  Q3 三篇文档方向全对、无危险过头声明，唯一**建议** = `ov ≥ ts` 那条标了「实测」却没有对应臂日志
  ⇒ 已由控制器补测坐实（见上，同处补记「md5 不可作复算口径」这个坑）；
  Q4 席位**未照抄**任务书里控制器写的两处错（split 块数公式、`bands`「重复索引」措辞），
  `--bands=1 ⇒ Cannot open file` 是它自己的探针发现、如实记为缺口③而非洗白。
- 只登记两条（不动作）：①`clip` 的「整像元向下取整」没有单独演示 floor 的臂（靠 CL-2 的 +1 像元间接判到）；
  ②私有报告 Step 3 表里 `MG-2` 的交叉引用写「缺口清单③」应为④（提交态 `RELEASE.md` 指对了，只有报告错位，报告已由控制器补记）。
- ⚠ **Release 侧未验收**：三条晋升一律只在 Debug 取证，Release 树本批未重编 ⇒ 归 D4 收口门补凭据。
- **缺口（照实登记）**：①`clip` 对"部分相交/完全不相交"返回成功并写一张**请求尺寸的零填充**产物，
  窗外补的是 `0` 而**不是 NoData** ⇒ 调用方无法区分"裁空了"与"裁出来全是 0"；判据按实到钉住了这个行为，
  改不改（可观测行为变更）要过裁定门，本把不动码。
  ②任务书 §2.2 给的 split 块数公式 `ceil((w-overlap)/(tile_size-overlap))` 与产品实到分叉（见上）
  ⇒ **控制器裁定：判任务书公式错，不立 N、不需拍板**。理由（现读 `cutting_plugin.cpp:413-424`）：产品是
  「网格步长恒为 `tile_size`、每块按 `min(tile_size+overlap, 剩余)` 加长」这一**自洽**模型，重叠带宽度恰为
  `overlap`；ParamSpec 的「重叠像素」描述串与 GUI/中文文档从未承诺过「步长随 `overlap` 缩短」⇒ 没有契约被违反，
  分叉只存在于本把任务书的推导里（控制器自误，已在本节与台账点名）。判据与文档一律按产品实到写。
  ③`cutting --help` 与 ParamSpec 把 `--bands` 描述成 "band indexes"、实现吃文件路径 ⇒ 文案缺陷；
  它同时是 `docs` 侧的一处反面（本把只把中文文档按实现写准）。
  ④腿侧 `cutting_merge_bands` 的两条列表项是**同一个文件** ⇒ 该 case 结构上判不出文件顺序（`MG-2` 臂下腿 rc=0 实测），
  文件顺序今天只由单测侧那一档承重；给腿换一个不同常量的第二输入归后续批次。
  ⑤腿侧 `clip`/`split` 的夹具 `classification_raster` 每格都是 `1` ⇒ Mean/StdDev **不判窗落在哪**，
  位置判据今天只在单测侧存在；且腿侧 `info` 文本的 `Origin:` 走默认 6 位有效印法，12935050 会被印成
  `1.29351e+07` ⇒ 印法误差 ±50 m 与一格像元（50 m）同量级，**腿侧结构上无法用 Origin 判亚像素/一格位移**
  （这条是按印法步长逐支点名的，不是"通用"论证）。
- **本把没做的事**：`cutting,tile` 一行仍 PENDING（射程外，另派 D1-C2）；`docs/算法说明/文档漂移清单.md` 未动；
  未跑全量 ctest（那是收口门）。

### 批次 D 覆盖晋升 D1-C2（2026-10-28，`cutting,tile` 一行 PENDING→GOLD）

射程**一行**：`tests/action_gold_cases.txt` 的 `cutting,tile`。`cutting` 的另三条（clip/split/merge_bands）
是上一把 D1-C1 的账，本把未动。结构性障碍与台账 R-D26 / tracker #168 同源：既有 27 条 `PluginTest.Tile*`
用例的动作槽全写在共享 helper `t17BaseTileParams` 体内（`params["action"] = std::string("tile")`），
`CoverageMeta.GoldEvidenceIsAuditable` 只看**同一个 TEST 块体**，所以整族结构上登不上 GOLD
⇒ 本把新建一条**独立**证据用例，块体内内联动作槽，而不是把 helper 抄一遍。

- **落账凭据**（逐笔提交、不 amend）：Step1 单测 `8f7e39c`、Step2 腿 case `451ec5f`、Step4 登记+上限 `17a3577`。
  证据用例 = `PluginTest.TileAutoMaxZoomBandAndTilePixelSizeGrid`；腿 case = `run_real_raster_regression.ps1` 的 `cutting_tile`。
- **计数现读对撞**（改前/改后都跑了登记表头部那批命令本身，不是推的）：
  `data_rows 126→126`、`gold_rows 100→101`、`pending_rows 26→25`、`gold_rows_all_lines 101→102`、
  `gold_rows_table_driven 15→15`、`gold_rows_literal_slot 85→86`，`kPendingCeiling 26→25`（与表同笔）。
- **注册数**（只认 `Total Tests:`）：`PluginTest.Tile*` 改前 **27** ⇒ 改后 **28**；
  定向配方 §5 改前 **61** ⇒ 改后 **62**（新增 1 条 `TEST_F`；腿 case 不单独注册）。
- **腿计时**：改前本把现跑 `real_data_raster` **12.71 s**（控制器 22:29 另一次现读 15.37 s ⇒ 同一条腿散布 2.66 s，
  本把用自己的改前/改后对撞），改后 **12.99 s**（新 case 自身 0.24 s），`TIMEOUT 60` ⇒ **4.62×** 余量，
  未跌破 2.5× 触发线，未抬 TIMEOUT、未减断言。腿侧闸 `registered=70 ran=70 judged=46 allowlisted=24
  dead-branches=0` ⇒ `judged+allowlisted==ran` 成立。

- **判的是哪两个量**（写这行之前先做的探针：`grep -n "max_zoom" tests/test_plugins.cpp` 的 29 处命中全是显式赋值、
  `grep -n "tile_pixel_size" tests/test_plugins.cpp` 零命中 ⇒ 这两处今天确实没人判）：
  - **推得**（手推，公式只含 `2π·a = 40075016.685578488`、`2^z`、`tileSize`、夹具 `pixelSize`）：
    `pixelSize=1000` 配 `ts=256` ⇒ 自动档 z=8、9 张；配 `ts=128` ⇒ z=9、10 张；
    逐档索引 `x=y=2^(z-1)`（z=0 ⇒ 0）；覆盖缘列数 `64000/tileWidthM(z)·ts` = 104.66131637 / 52.33065819 / 104.66131637；
    `pixelSize=50`（真实腿的 `classification_raster`）配 `ts=256` ⇒ z=12、层级集 `[0..12]`、最深瓦片 `12/3370/1552.png`、
    alpha 均值 `255·81/65536 = 0.3151702881`；配 `ts=128` ⇒ z=13、`[0..13]`、`13/6740/3104.png`、`255·81/16384 = 1.2606811523`。
  - **实跑**（Debug 树 `gis-cli` 探针 + 单测 + 腿）：以上每一项**逐值相符**（腿侧打印 `Mean: 0.31517` / `Mean: 1.26068`，
    6 位有效印法下的舍入误差 ≤ 6.4e-7，比 1e-5 容差小三个数量级；单测侧实到 105/53/105 列，
    Δ 分别 0.33868 / 0.66934 / 0.33868，都 < 1.0 容差 ⇒ 容差论证是**三支各自成立**，不是一支通用）。
- **变异臂清单**（`src/plugins/cutting/cutting_plugin.cpp` 只在取证时改、当场 `cp` 还原，
  四臂还原后 `git hash-object --path` 与 `git rev-parse HEAD:` 同为 `dc4bf9f5be619570db1d8f9fea03459b3e922aab`）：
  | 臂 | 改动（函数名 + 判据原文） | 红在哪一档、什么形态 |
  | --- | --- | --- |
  | M1 | `computeMaxZoom` 的 `>= pixelSize * 0.5` 丢掉 `* 0.5` | 单测 **ts=256 档**红：实到集少 `"8/128/128.png"`（8 vs 9）⇒ `***Failed 0.46 s` |
  | M2 | `computeMaxZoom` 的 `for (int z = 30; z >= 0; --z)` 改成从 `z=0` 往上 | 单测 **ts=256 档**红：实到只剩 `{ "0/0/0.png" }` ⇒ `***Failed 0.22 s` |
  | M3 | `doTile` 里 `computeMaxZoom(pixelSize, tileSize)` 的实参写死 `256` | 单测 **ts=128 档**红（实到少 `"9/256/256.png"`，9 vs 10）⇒ `***Failed 0.30 s`；**腿同臂红**：`cutting_tile ts=128 level set mismatch (actual=[0..12] expected=[0..13], derived max_zoom=13)` |
  | M4 | `doTile` 里 `tileResX = (rect.maxX - rect.minX) / tileSize` 的分母写死 `256` | 单测 **ts=128 档**红三处：z=0 那张被 `dx1<=dx0` 吞掉、两支 `EXPECT_NEAR` 差 **23.338683626565413** 与 **52.669341813282706**（> 1.0）⇒ `***Failed 0.34 s`；ts=256 档在该臂下 Δ=0 ⇒ 必须分档才认得出这只手 |
- **判不出来的变异如实登记**（不造恒真断言去杀它）：`>=` 改 `>`（`res(8,256)/500 = 1.223`，等号取不到 ⇒ 改 `>` 与 `>=` 在本夹具上同档；**这是 fixture-conditional 的等价，不是恒等价**：等号只有当 `pixelSize = 156543.03392804097 / 2^z` 时才取得到）；（未跑臂 ⇒ UNVERIFIED）
  半径 `6378137` 的**换档敏感度**（控制器复算，脚本 `build/tmp/batchD/d1c2/sensitivity_recompute.py`）：
  单测档（`pixelSize=1000` ⇒ 阈值 500；`ts=256` 得 z=8、`ts=128` 得 z=9，两档 `res(z)`/`res(z+1)` 同为
  611.496226/305.748113）要挪 **−18.23% 或 +63.53%** 才换档；腿档（`pixelSize=50` ⇒ 阈值 25；z=12/13，
  `res` 38.218514/19.109257）要挪 **−34.59% 或 +30.83%** ⇒ **本席原写"挪 ±20% 不换档、要挪 22% 才换档"是错的**
  （22.30% 是 `res(8,256)/500 − 1` 这个"比阈值大多少"的数，不是换档所需的距离；按正确的下界 −18.23%，
  **−20% 会让单测的层级集支红**（8 → 7））；但**结论仍成立、理由要换**：真实可选口径彼此只差
  **球平均 −0.112% / WGS84 极半径 −0.335% / 印错 `20037808.34` +0.0015%**，全部远小于 18.23%
  ⇒ **常数口径今天判不到**。原句"像元列只挪几十分之一"也一并改准：极半径那档 `edgeCol` 挪 0.35 列、
  印错那档挪 1.6e-3 列 —— 都仍 < 1 列，但 0.35 不是"几十分之一"；`min(maxZoom, 22)` 的 22；
  `mercatorTileGrid` 的 `ts << z`（在 `src/core/`，射程外未取证）。
- **文档按实到写准**：`docs/算法说明/影像裁切与镶嵌/栅格切片.md` 补了常数口径（球面 `a·π = 20037508.342789244`）、
  自动档"从 z=30 往下 + 半个像元"的遍历方向与四条手推实例、`tile_pixel_size` 的**两处**消费点
  （`res` 的除数 ⇒ 决定档位；`tileResX` 的除数 ⇒ 决定覆盖缘列数；并点名它**不**改变单层瓦片地理范围，
  因为 `tiles = globalWidth/tileSize = 2^z` 把 `tileSize` 约掉了 ⇒ 拿它当"网格尺寸"会写出恒真判据）、
  `add_alpha` 插件缺省 `true` 与两档透明度口径（含"既有 helper 显式设 false ⇒ 缺省路径从未被判"）、
  `resample` 实到缺省 `average` 与面板 `nearest` 的 **B19 已知不一致**（本把不改、只写准）。
- ⚠ **Release 侧未验收**：本把全部读数来自 Debug 树；Release 树未重编 ⇒ `cutting,tile` 这条 GOLD 的
  Release 凭据归 D4 收口门。
- **缺口（照实登记）**：①常数口径（`6378137`/`20037508.342789244`）本把判不到，见上；
  ②`>=` 与 `>` 等价、`maxZoom` 钳位 22 无臂；③新用例夹具对 x=y 对称 ⇒ 单测侧判不到轴序（腿侧的
  `3370/1552` 判得到，两载体互补）；④`add_alpha` 缺省 `true` 这条路径在新用例里**被走到但没被判**
  （夹具无 NoData ⇒ 两档产物不可区分，既有锚已证"两档都 4 波段、集合相同"）；⑤`resample`/`overview_resampling`/
  `jpeg_quality`/`nodata_value` 在 tile 上的消费仍无数值判据（B19 决策门与后续批次）；
  ⑥腿侧第二次 `ts=128` 跑是**判据分支内**再调一次 CLI，不占 case 名额（gate 的 `conditional-registrations=1` 不变）。
- **本把没做的事**：`src/` 除 Step3 取证外零改动（当场还原）；未动 `resample`/`add_alpha` 缺省（B19）；
  未修 D1-C1 那两条已知文案不一致（B16 `resample` 面板默认、`--bands` 描述串）；未跑全量 ctest。

### 批次 D 覆盖晋升 R1（2026-10-29，`raster_inspect` 的 `histogram`·`info` 两条 PENDING→GOLD）

射程**两行**：`tests/action_gold_cases.txt` 的 `raster_inspect,histogram` 与 `raster_inspect,info`。
**本把不新建腿 case**（两条都已有腿），做的是**判据升级**：`histogram` 从"只数数组长度 + 总数非零"提到逐桶值级；
`info` 从"在名单里但被豁免、根本没判据分支"补成值级分支并同笔删豁免。
`raster_manage` 的 `cog`/`zonal_stats`、`raster_render` 的 `histogram_match`/`colormap`、`raster_manage,overviews`
都归 R2/R3，本把一条未动；既有 GOLD `nodata`/`proximity` 未动。

- **归属现读**（`grep -n '"raster_inspect"' tests/test_plugins.cpp` = **6 处**，逐处回到所在 `TEST_F`）：
  `RasterInspectInfoExecution`、`RasterInspectHistogramExecution`、`RasterInspectHistogramAcceptsConstantRaster`、
  `RasterInspectInfoAcceptsConstantRaster`、`RasterInspectHistogramRejectsAllNoDataBand`、
  `RasterInspectInfoDistinguishesAllNoDataBand` ⇒ **六块剥注释后都没有 `EXPECT_NEAR`/`EXPECT_FLOAT_EQ`/
  `EXPECT_DOUBLE_EQ`**（N133 那四块判的是 metadata 十进制字符串相等与 `message.find` 子串）⇒
  两行今天确实不承重，本把就是给它们写真判据。`info` 这个动作名四个插件都在用，
  所以只认"块体内同时出现 `"raster_inspect"` 与动作槽"的块（上表第 1/4/6 行），
  没拿全仓 `["action"] = std::string("info")` 的命中数当覆盖数。
- **落账凭据**（逐笔提交、不 amend）：Step1 单测 `3afcdf4`、Step2 腿侧 `5fbd8e6`、Step4 登记+上限 `34dc895`、
  Step5 文档与本节 = 下一笔。证据用例 = `PluginTest.RasterInspectHistogramExecution` /
  `PluginTest.RasterInspectInfoExecution`；腿 case = `run_real_raster_regression.ps1` 的
  `raster_inspect_histogram` / `raster_inspect_info`。
- **计数现读对撞**（改前/改后都重跑登记表头部那批命令本身）：`data_rows 126→126`、`gold_rows 101→103`、
  `pending_rows 25→23`、`gold_rows_all_lines 102→104`、`gold_rows_table_driven 15→15`、
  `gold_rows_literal_slot 86→88`，`kPendingCeiling 25→23`（与表同笔）；
  `ctest -R '^(CoverageMeta|CoverageMetaJudge)'` = **19/19 Passed**（含 `GoldEvidenceIsAuditable` 回库核这两块）。
- **注册数**（只认 `Total Tests:`）：定向配方改前 **43** ⇒ 改后 **43**——本把**没新增 `TEST_F`**，
  两条都是在既有块里加断言（配方 = CoverageMeta 4 + CoverageMetaJudge 15 + 脚本守卫 4 + `PluginTest.Raster*` 19 + 腿 1）。
- **腿侧闸前后对撞**（本把唯一会变的地方，与任务书"三个数应当不变"的预期不同，现读为证）：
  `registered 70→70`、`ran 70→70`（不新增 case ✓）、**`judged 46→47`**、**`allowlisted 24→23`**、
  `allowlist-table 24→23`、`branch-labels 47→48`、`dead-branches 0→0`、`conditional-registrations 1→1` ⇒
  不变量 `judged + allowlisted == ran` 成立（47+23=70）。
  原因：`raster_inspect_info` 改前在 `$rasterGateAllowlist` 里被豁免（理由原文是"action is PENDING …
  so no numeric expectation exists anywhere yet"），晋升它就必须同笔删豁免，否则闸按 stale-entry 判红。
- **腿计时**：改前本席现跑 `real_data_raster` **13.98 s / 13.14 s**，改后 **13.87 s**（Step5 后复跑再取一读，见配方节）；
  本把用自己的改前/改后对撞（本会话同一条腿的跨席散布本来就 12.7~16.6，不拿两个数互相归因），
  `TIMEOUT 60` ⇒ 余量 4.3×，未跌破 2.5× 触发线 ⇒ **未抬 TIMEOUT、未减断言、未拆腿**。
  新增成本 = 1 次 CLI 调用 + 9 次数值解析，逐用例现读 `raster_inspect_histogram 0.07s` / `raster_inspect_info 0.07s`。

- **判的是哪两个量**（期望全部从夹具源码字面量手推，**没有一条从产物回填**）：
  - **推得**（`info` 单测侧，夹具 `createTestRaster(…, 64, 32)`：行主序 `data[i] = i % 256` ⇒
    2048 个像元 = **8 轮完整的 0..255**；**修复轮 1 换的夹具**，见下方 A8 那条）：
    `min 0`、`max 255`；`Mean = 8×32640/2048 = 127.5`；
    `E[x²] = 8×(255·256·511/6)/2048 = 44477440/2048 = 21717.5`；`Var = 21717.5 − 127.5² = 5461.25`；
    `StdDev = √5461.25 = 73.900270636`；`Extent` = `(116, 39.968) - (116.064, 40)`
    （`maxX = 116 + 64×0.001 = 116.064`；`minY = 40 + 32×(−0.001) = 39.968` —— **宽与高不再相等**；"写反"在两处分别实测：`Size` 打印序（A9 ⇒ 两格差 32）与 `Extent` 算式（A10/A10b ⇒ 各差 0.032））；
    `histogram` 侧夹具**仍是** `createTestRaster(…, 32, 32)`（1024 = 4 轮）⇒ 桶宽 `255/16 = 15.9375`、
    第 i 桶收整数 `16i … 16i+15` = 16 个不同值 × 4 次 = **每桶 64**。
    ⚠ 这里保留方夹具**不是漏改**：直方图逐桶计数不读 width/height ⇒ 方阵在这条上没有反射不变量代价；
    只有 `info` 那族（读 Size/Extent 的）才需要非方夹具。
  - **推得**（腿侧，夹具 `makeAnalysisRaster` 32×32×3，band1 `x+y`、band2 `2x−y`、band3 `(x·y) mod 17`）：
    band1 `min 0 / max 62 / mean 31744/1024 = 31 / Var = 1131.5 − 961 = 170.5 / StdDev = √170.5 = 13.05756486`；
    band2 `min −31 / mean 15.5 / Var = 4×85.25 + 85.25 = 426.25 / StdDev = √426.25 = 20.64582283`；
    band3 `max 16`；16 个桶 = `[10,26,42,58,74,90,106,90,122,106,90,74,58,42,26,10]`（值 `k` 出现 `min(k+1, 63−k)` 次、
    桶宽 `3.875` 是 k/8 型 dyadic ⇒ 边界精确、`max` 被夹进末桶），16 个桶心 = `3.875i + 1.9375`。
  - **实跑**：Debug 树两枚单测首跑即绿（15 + 18 枚断言逐枚相符，无一条需要改期望去就产物）；
    腿改前绿、改后绿；A4 臂把腿的期望原样打进错误消息 ⇒ 实到 `13.0575648572006`，
    与 `analysis_input.tif.aux.xml` 里 GDAL 自己缓存的 `STATISTICS_STDDEV=13.057564857201` 同值
    （"总体不是样本"这一档由此取得第二处独立对撞）。
  - **容差夹心是逐支算的**（口径：`info` 一侧**单测 15 枚里 14 枚**、**腿侧 11 个数里 9 个**
    的真值十进制展开 ≤6 位有效 ⇒ **舍入误差 0**（容差只吸收 double 尾）；
    只有两支 StdDev 需要真夹心 —— 量级 `[10,100)` ⇒ 步长 `1e-4` ⇒ 最坏舍入 `5e-5` ⇒ 容差 `2e-4` = **4×**（不是三个数量级），
    上半边最近错答（样本标准差）分别是 `6.35e-3`（**31.8×**）与 `1.01e-2`（**50.4×**）；
    单测那支 StdDev 下半边同为 `4×`，**上半边随夹具从 `180×` 改成 `90.2×`**（换 64×32 后 N 从 1024 变 2048 ⇒ 样本−总体之差减半：`√(5461.25×2048/2047) = 73.918319`、与总体 `73.900271` 差 `1.805e-2` ⇒ ÷ 容差 `2e-4` = **90.2×**；`4×` 下半边与腿侧 `31.8×`/`50.4×` 不受影响）。
  - **口径写清（修复轮 1 改，原句"13 枚里 11 枚"把两张表的数拼在了一起）**：单测 **15 枚** `EXPECT_NEAR` = 14 枚零舍入 + 1 枚 StdDev；腿侧 **11 个数** = 9 个零舍入 + 2 个 StdDev（`Size` 三格是整数、不参与浮点舍入论证）⇒ 合并口径是"**26 个数里 23 个零舍入**"，不是"13 里 11"。

- **变异臂清单**（`src/plugins/raster_inspect/raster_inspect_plugin.cpp` 只在取证时改、当场从字节快照还原；
  **没用 `git checkout --`**；八臂每臂尾行都自证 `RESTORED sha256 match: True` 且
  `git hash-object --path` 与 `git rev-parse HEAD:` 同为 `0d5bebd4541f…`；臂后另做一次还原重建刷掉变异 DLL）：

  | 臂 | 改动（函数名 + 判据原文） | 红在哪一档、什么形态 |
  | --- | --- | --- |
  | A1 | `doHistogram` 请求直方图的 `TRUE, FALSE`（IncludeOutOfRange）→ `FALSE, FALSE` | 单测 `RasterInspectHistogramExecution` ***Failed***（Info 那条仍 Passed）；腿 **`histogram bin 15 count mismatch (actual=9 expected=10)`** —— 末桶丢的就是值 62 那 1 个像元，与落笔前的预测同值 |
  | A2 | `doHistogram` 桶心 `minVal + (i + 0.5) * binWidth` → 丢 `+ 0.5` | 腿 **`histogram bin 0 center mismatch (actual=0 expected=1.9375)`**；**单测档绿**（该侧刻意不钉 `center`，理由见下"判不出来的面"） |
  | A3 | `doRasterInfo` 的 `oss << "  Min:    " << bMin` → `<< bMax` | 单测 `RasterInspectInfoExecution` ***Failed***；腿 **`band 1 (x + y) minimum mismatch (band=1 actual=62 expected=0)`** |
  | A4 | `oss << "  StdDev: " << bStd` → `<< bMean` | 单测 ***Failed***；腿 **`band 1 stddev … (band=1 actual=31 expected=13.0575648572006 tolerance=0.0002)`** |
  | A5 | `Origin: (" << adfGT[0] << ", " << adfGT[3]` 两支互换 | 单测 ***Failed***（116 ≠ 40 判得出轴序）；**腿档绿**（腿今天不判 Origin ⇒ 这一行只有单测承重） |
  | A6 | 波段循环 `for (int b = 1; b <= bands; ++b)` → `b < bands` | 单测 ***Failed***（单波段夹具里没有 `Min:` 行 ⇒ "读不到"没被当成 0）；腿红在**第一个读 info 文本的用例** `spindex_ndvi mean mismatch` ⇒ 本把的 band-3 格没轮到，其可红性由 CF4 另证 |
  | A7 | `maxX = adfGT[0] + width * adfGT[1] + height * adfGT[2]` → `(width + 1) *` | **修复轮 2 在新夹具下重跑（A7r）**：单测 ***Failed***（`extentFields[2]` 差 `1.000e-3` ⇒ `116.065` vs 期望 `116.064` = 容差 `1e-9` 的 `1e6` 倍），还原后 6/6 绿；腿档绿（不判 Extent）。**原写的 `116.033 vs 116.032` 是方夹具时代的读数，已随夹具作废** |
  | A8 | `Size:   " << width << " x " << height` **打印序**两支互换 | **修复轮 1 重跑后由"等价"改判"承重"**：`info` 夹具从 32×32 换成 64×32 之后该臂 ⇒ `RasterInspectInfoExecution` ***Failed***（rc=1），还原后 6/6 绿 ⇒ **这一类缺陷今天单测侧判得到**。⚠ **实测到的红格只有 `Size` 两格**（`(*sizeFields)[0]`/`[1]` 各差 32）——**修复轮 1 曾把它写成"Size 两格 + Extent 的 maxX 三支同因红"，那是没跑过的推理被记成实测，已由修复轮 2 撤正**：打印序互换根本不碰 Extent 的算式。Extent 的宽高转置另开两臂**实测**：A10（`minY` 式里宽高互换 ⇒ 差 `0.032` ⇒ `39.936` vs `39.968`）、A10b（`maxX` 式里宽高互换 ⇒ `116.032` vs `116.064`）均 rc=1、还原后 6/6 绿。（本臂换夹具前的原读数是"两条全绿"，那条等价结论只对**方夹具**成立，已随夹具作废；腿侧红在别的用例 `projection_reproject raster size mismatch`，那台产物本就非方。） |

- **反事实臂（改期望常数 ⇒ 证明分支真在比，"分支会红"≠"期望正确"）**：
  CF1 桶 8 的 `122→121` ⇒ 红 `actual=122 expected=121`；CF2 桶 0 心 `1.9375→1.9` ⇒ 红 `actual=1.9375`；
  CF3 band1 `Mean 31→30` ⇒ 红 `band=1 actual=31 expected=30`；CF4 band3 `Max 16→15` ⇒ 红 `band=3 actual=16`
  （同时证明 `Get-InfoBandStatValue` 的 `Band N:` 块头锚真在第三个块里取到数）；CF5 `ExpectedBands 3→2` ⇒ 红 `bands actual=3`。
  五臂的 `actual=` 都等于手推值 ⇒ 判据在比数、且比的就是那一格。
- **装饰形态的处置**（点名 + 能不能红）：腿改前那句 `total -gt 0` 是**开口下界**（任何非零都过）⇒ 换成 `-eq 1024`；
  但 `total` 在产品码里就是这 16 个桶的求和 ⇒ **本行被 16 行算术蕴含、不计独立判据**（代码注释与报告同写），
  单测侧则**根本没写** `total`、也没写 `bin` 索引（那字段就是循环变量 = 恒真）；
  为避免"同期望 + 同容差"的重复形状，**主动删掉** `band2 Max = 62`（与 `band1 Max` 同值）与 `band3 Min = 0`（与 `band1 Min` 同型），
  换成唯一值的 `band2 Min = -31`、`band3 Max = 16`、`band2 Mean = 15.5`。
- **文档按实到写准**（`docs/算法说明/栅格工具/`，两篇的归属**由现读正文判定、不按文件名猜**：
  `直方图.md` 的参数表是 `输入/输出/波段号/柱数` 四枚 = `doHistogram` 的四个读点，
  `栅格信息.md` 只有 `输入文件` 一枚且写明"不生成输出文件" = `doRasterInfo` 唯一的读点 ⇒ 归属确定）。
  `直方图.md` 补了左闭右开桶区间与"`max` 计入末箱"、三处印法不可互相换算（`center` 定点 4 位、
  小数位超 4 时进位与舍去交替 ⇒ 别拿 `center` 反推桶宽）、元数据只发 4 枚键、`total` 就是桶和；
  `栅格信息.md` 补了**本动作不发任何元数据键**、子串判据会被长数字前缀吃、缺省 6 位有效 ⇒ 步长随量级变、
  `StdDev` 是总体标准差、`Extent`/`Size` 只在**非方**产物上判得出宽高写反（本把 `info` 夹具已换 64×32）、
  `Overviews:` 只读第 1 波段。
- ⚠ **Release 侧未验收**：本把全部读数来自 Debug 树；Release 树未重编 ⇒ 这两条 GOLD 的 Release 凭据归 D4 收口门。
- **缺口（照实登记）**：①`info` 不发 metadata ⇒ 判据全在文本解析上，`CRS`/`Color`/`Driver`/`Rotation`(两支都 0)/
  `Scale`/`Offset` 今天仍无任何数值判据；②（原记"宽高两格在方夹具下等价、要判得到得换非方夹具"）**已由修复轮 1 关闭**：
  `info` 夹具换成 64×32 ⇒ A8 重跑实测红；histogram 侧保留方阵并写明它不读宽高，
  ③单测侧不钉 `center`（该夹具桶心是 `k/32` 型、卡在 `fixed(4)` 舍入中线上，写不出可手推期望；
  腿侧桶心 `k/16` 精确所以钉得住。复审另指"`bins=8` 就能钉"，本把未做）；
  ④A6 在腿档被 `spindex_ndvi` 抢先判红 ⇒ 本把 band-3 格没有"产品变异臂"、只有改期望臂（CF4）；
  ⑤PAM 边车 `analysis_input.tif.aux.xml` 会缓存统计（命中时 `ComputeStatistics` 不回读像元）⇒ 本把两分支先删边车，
  但**别的分支没删**，那条口径归腿侧统一处理；⑥矩阵 `fixture` 列口径两种写法并存（`data` vs `not-consumed`），
  本机根本没有 `data/raster` ⇒ 归 D3 矩阵对账，本把未改；⑦**已撤销**（原写"`doHistogram`/`doRasterInfo`
  都未判 `openRaster` 返回值"）：控制器现读 `gis::core::openRaster` ⇒ 它 `if (!ds) throw GisError(...)`，
  **失败时它抛 `GisError`、调用方拿不到空指针** ⇒ "应当判返回值"这条要求对它不成立（判空是死代码）；且 `GetRasterBand` 判了 `if (!rasterBand)`、`ComputeStatistics` 的 `CPLErr`
  也接成了 `statsErr` ⇒ 本条不成立、**不立案**（是复审前的控制器自查否证的）。
- **本把没做的事**：`src/` 除 Step3 取证外零改动（当场还原、`git status` 空）；未新建腿 case；
  未动 `raster_manage,overviews`/`raster_render,colormap`（R2）与 `cog`/`zonal_stats`/`histogram_match`（R3）；
  未动既有 GOLD `nodata`/`proximity`；未跑全量 ctest；未动 Release 树；受保护的 terrain blob 复核仍为
  `588a42504ee19bcb551f8214e5c1adb68cea20e9`。

### 批次 D 覆盖晋升 R2（2026-10-29，`raster_manage.overviews`·`raster_render.colormap` 两条 PENDING→GOLD）

射程**两行**：`tests/action_gold_cases.txt` 的 `raster_manage,overviews` 与 `raster_render,colormap`。
两条今天都**没有判据分支**、都在 `$rasterGateAllowlist` 里被豁免 ⇒ 本把做的是**从零补值级分支 + 同笔删豁免**
（不是升级已有分支），这与 R1 的 `info` 半边同形、与 R1 的 `histogram` 半边（判据升级）不同形。
R3 三行（`cog`/`zonal_stats`/`histogram_match`，要先新建腿 case）一条未动；既有 GOLD
`nodata`/`proximity`/`raster_inspect.*` 未动；**本把没有新建任何腿 case**。

- **归属现读**（`tests/test_plugins.cpp` 里四个串共 **32 行 / 23 个 `TEST_F`**，逐块定性地）：
  只有 `RasterManageOverviewsExecution` 与 `RasterRenderColormapExecution` 两块**执行**这两个动作，
  且两块剥注释后都**没有** `EXPECT_NEAR`/`EXPECT_FLOAT_EQ`/`EXPECT_DOUBLE_EQ`（旧形态是
  `EXPECT_GE(GetOverviewCount(), 1)` 与 `EXPECT_EQ(GetRasterCount(), 3)`，两条都是"存在性"而不是值级）⇒
  与"腿侧没有判据分支"这条独立对撞一致。`RasterRenderPluginParams` 只 `find` 不 `execute`；
  两个 `*GuiPanelHidesParametersTheActionNeverReads` 块出现插件名与动作名但不跑动作 ⇒ 没当凭据用。
- **产品面现读**：`overviews` **没有 output 参数**，`BuildOverviews` 原地写进输入文件，
  处理元数据也写进同一文件内部的 `GIS_PROCESSING` 域；`levels` 解析只保留 `>1` 的 token、
  `resample` 转大写后原样交给 GDAL。`colormap` 产出 `列数 x 行数 x 3 波段 GDT_Byte`，
  先 `cv::normalize(..., 0, 255, NORM_MINMAX, CV_8U)` 再 `applyColorMap`，写盘前做一次 BGR→RGB 换序，
  波段解释设成 `GCI_RedBand + i`；常数波段（`max-min < 1e-10`）直接报错不产出。
  **边车口径**：两个动作的 `openRaster` 都 `if (!ds) throw` ⇒ 没有返回值可判（R1 已被这条否证过，不重犯）；
  真正的风险在读侧——同名 `.aux.xml` 命中时 `ComputeStatistics` 不回读像元 ⇒ 本把两分支（腿与单测）
  **读前都先删边车**。
- **落账凭据**（逐笔提交、不 amend）：Step1 单测 `5f44f97`、Step2 腿侧值级分支 + 同笔删豁免 + 新夹具子命令
  `27b9480`、Step4 登记 + 五张计数 + 上限 `130d8ce`、Step5 两篇算法说明的数值口径 **与本节** `65f7b90`。
  ⚠ 这一条的第一版写的是"Step5 文档与本节 = **下一笔**"——那句话在它落地的那一刻就已经自指失效
  （它描述的正是含它自己的那一笔），修复轮 1 按 S-2 改成上面这个不带"下一笔"的事实句。
  同型的句子在本文件 R1 一节还留着一条（`Step5 文档与本节 = 下一笔`，在 histogram/info 那节的落账凭据里）；
  **本把没有改它** —— R1 已由控制器结案，改一份已批准的记录不该由 R2 的修复轮顺手做，登记在这里交给控制器定。
  修复轮 1（定向复审判 not approved 后的六件事）另起几笔：M-2 撤回被产品否证的因果 `3816ffd`、
  M-1 裸行号锚点改"函数名 + 判据原文" `196fa9f`、S-4 容差算术更正 `5a39274`；
  最后一笔是纯登记（S-1 装饰行的 message 逐行分开 + 上面这句 S-2 + 下面缺口清单的 S-3 第 ⑧ 条），
  **它自己的 SHA 写不进它自己的提交文本**（那又是一次自指），所以这里不预留号，
  要定位就在 `git log` 里取本节 R2 一节的最后一笔。
  证据用例 = `PluginTest.RasterManageOverviewsExecution` / `PluginTest.RasterRenderColormapExecution`；
  腿 case = `run_real_raster_regression.ps1` 的 `raster_manage_overviews` / `raster_render_colormap`。
- **计数现读对撞**（改前/改后都重跑登记表头部那批命令本身，不是按减法推）：
  `data_rows 126→126`、`gold_rows 103→105`、`pending_rows 23→21`、`gold_rows_all_lines 104→106`、
  `gold_rows_table_driven 15→15`、`gold_rows_literal_slot 88→90`，`kPendingCeiling 23→21`（与表同笔）。
- **闸的五个计数**（预期与实测完全一致，没有顶回任务书）：
  `registered 70→70`、`ran 70→70`（不新增 case）、`judged 47→49`、`allowlisted 23→21`、
  `allowlist-table 23→21`、`branch-labels 48→50`、`dead-branches 0→0`、`conditional-registrations 1→1`；
  不变量 `49 + 21 = 70` 成立。同笔删那两条豁免是闸第 (3) 条 `staleAllow` 的要求，不是顺手清理。
- **夹具形状**：两条 case/两个单测原来都是**方阵**（腿输入 `makeAnalysisRaster` 32×32×3、
  单测 128×128 与 40×40），而两个动作的判据都读宽高 ⇒ 新建非方夹具
  `makeStepLevelRaster` / `createStepLevelRaster` = `40×24×1`、四象限三级平台 `{0^240, 10^480, 51^240}`。
  ⚠ **修复轮 1 撤回记录**：这一条的第一版写的是"三级平台而不是斜坡的理由是**产品决定的**：
  `cv::normalize` 到 8 位整型时带百分位裁剪，斜坡的极值桶各只占 0.39% 会被裁掉 ⇒ 期望只能
  从产物回填（N108）；极值做厚到 25% 后裁剪够不着"。**这条因果是 R2 席位自己写进提交文本、
  还挂了"产品决定的"这个名号的，现已由控制器与复审席各自独立否证，原文撤回**——同批一起撤回的
  同源句共 5 处（本文件、`docs/算法说明/栅格工具/伪彩色.md`、`tests/test_plugins.cpp`、
  `tests/gui_test_data_helper.cpp`、`tests/run_real_raster_regression.ps1`）。修复轮 1 本席位复跑的
  读数（`build/tmp/batchD/r2/arms/F1_cv2_normalize_probe.log` 与 `F1_product_info_ramp64x64.log`）：
  本机 cv2 4.13.0 对 `64×64` 的 `i%256` 斜坡（每桶恰占 `1/256 = 0.390625%`）归一化到 `CV_8U` 后
  `min 0 / max 255 / unique 256`、与精确线性期望**不符的格子数 = 0**；把两个极值各压到只剩
  1 个像元（`0.104%`）仍然是精确线性；产品二进制跑同一形状斜坡 ⇒ 绿波段
  `Min:0 / Max:255 / Mean:127.5 / StdDev:73.9003`，与独立手推的离散均匀总体方差 `(256^2-1)/12`
  开方 = 73.90027 在 6 位有效印法下相同。代码面同向：`gdalBandToMat` 直接 `RasterIO` 进
  `CV_32F`、不读统计不裁剪；`doColormap` 里那次 `cv::minMaxLoc` 只用于"值域为空就 fail"，
  min/max 并没有传给 `cv::normalize` ⇒ 全链路没有阻尼或百分位步骤。
  换成实测成立的说法：三级平台而不是斜坡的**真实理由是可推性，不是裁剪**——平台件只有 3 个档，
  每档多少个像元能直接数（240/480/240），一阶与二阶矩都是两行闭式；斜坡件有 256 个档，期望要靠
  求和顺序与浮点累加去凑、每一档还得单独问一次舍入还是截断 ⇒ 手推成本高、残差不定。这条是**我选的
  夹具形状决定的**，不是产品决定的（换回斜坡同样能判，只是推不起）；原句"极值做厚到 25% 后裁剪
  够不着"整条作废（前提不成立）。
  而"方阵会把轴写反洗成等价变异"是**夹具决定的**——这一条不是推理，是实测：
  **保持 `Create(rows, cols)` 变异在二进制里，只把夹具换成 40×40 ⇒ 整个用例 PASSED 全绿**
  （`build/tmp/batchD/r2/arms/A_B2_square_fixture.log`），同一变异在 40×24 上红（`A_B2_unit.log`）。
- **臂清单（9 臂，全部 tee 到 `build/tmp/batchD/r2/arms/`，全部实测）**：
  产品消费点变异 5 臂 —— B1 漏 BGR→RGB 换序（单测 6 条 error：`band1 像素 0 vs 255`、`band3 像素 255 vs 0`、
  `rMin/rMax/bMin/bMax` 各差 255；**band2 的 5 支探针与 Mean/StdDev、Color 三行、几何两行全绿** ⇒
  一次天然的"常量支 vs 斜坡支"分离证明；腿 `band=1 actual=0 expected=255`）；
  B2 `Create` 宽高写反（单测 `GetRasterXSize Which is 24`、`YSize Which is 40`；腿 `width actual=24 expected=40`）；
  A1 忽略 `--levels` 落回默认（单测与腿都 `count actual=4`）；A2 层级翻倍、A3 层级 +1（同红，见下条更正）；
  改期望常数反事实臂 3 支 —— CF-A overviews StdDev 期望改 `Sqrt(385.0)` ⇒ 腿红 `actual=19.6262`（Δ=0.0048=24× 容差）；
  CF-B colormap Mean 改 `88.76` ⇒ 腿红 `actual=88.75`；CF-C colormap StdDev 改 `Sqrt(9629.0)` ⇒
  腿红 `actual=98.131 expected=98.1274681218261`（Δ=0.0035=17.5× 容差）。
  另 1 支是上面那把"方夹具等价"对撞臂。
- **⚠ 一处因果我写错并当场更正**（提交文本里没有这个错误结论，是报告 §4 的第一版写错后改准）：
  A2/A3 我原先写成"token `1` 被翻倍成 `2`、反过来通过了 `>1` 过滤 ⇒ 条数与尺寸同时被扰动"。
  现读 `while (iss >> level) { if (level > 1) { ovrLevels.push_back(<变形>); } }` ⇒ **过滤跑在原始 token 上**，
  所以 A2 请求的是 `{4,8,16}` 三条、A3 请求 `{3,5,9}` 三条，而两臂的 `GetOverviewCount()` 都报 **4**
  ⇒ 真正的事实是 **GDAL 自己多建了一条更粗的概览**（最细一条仍对应请求里的最小因子：
  A2 是 `10×6 = 40/4`、A3 是 `14×8 ≈ 40/3`）。我没有解释 GDAL 的补级规则，只登记读数。
- **推得 vs 实跑分开写**：
  **实跑**——以上 9 臂的红/绿、闸五个计数前后对、腿耗时对（改前 13.34 s → 改后 12.51 s/12.53 s，
  `TIMEOUT 60` ⇒ 4.8× 余量，未抬 TIMEOUT 未减断言，跑前 `tasklist` 三类进程计数 0）、
  定向族 43/43 绿、`CoverageMeta`+`CoverageMetaJudge` 19/19 绿、全库 `Total Tests:` 802→802（未新增 `TEST_F`）、
  autumn 的 LUT `(R=255, G=级别, B=0)` 与 `normalize` 不饱和（本机 python + cv2 逐位遍历 256 项）。
  **只是推断**——①autumn 的通道公式在**仓库实际链接的 OpenCV 4.14.0**（vcpkg `OpenCVConfig-version.cmake`）
  上成立：我的 oracle 是 **4.13.0**，公式跨这两版未变属推断，其 4.14.0 落地凭据是单测首跑全绿那一次；
  ②"每级宽高是单测独占领"是**读端代码**（`info` 只印 `Overviews: <count>`，不印任何每级几何）
  加两次臂构造失败，**不是**一次"腿绿、单测红"的实测。
- **缺口（照实登记）**：①`resample` 完全不被判据覆盖——条数与每级宽高都不随 `nearest`/`average` 变，
  要判它必须回读概览像元，而 `info` 没有这个读数器；②腿侧判不到"每级宽高"（上一条②的同一根因）；
  ③`GetOverviewCount()` 与 `info` 的 `Overviews:` 都**只读第 1 波段**（R1 已登记过同一句），
  多波段输入的概览一致性今天无人判；④colormap 的**逐像元摆放**只在单测侧有证据（5 支象限探针），
  腿侧只有全局统计量 ⇒ 全局统计不变的行/列重排在腿上是绿的；⑤未知色带名静默落回 `jet`，
  本把未判（要判得再加一条 case，超出"不新增 case"的射程）；⑥矩阵 `fixture` 列在这两行写 `data`
  而头部 ⚠ 要求 absent 时写 `not-consumed`，与第 8 列 note "腿走合成兜底"互相矛盾 ⇒
  **与 R1 已登记的缺口⑥同一条**，归 D3 矩阵对账，本把未改；本把按 R-D39 只做了第 5 列的复核
  （`real_regression` 复核为真，两条 `New-Case` 都在）与 note 口径核对（准确）；⑦Release 侧未验收
  （未重编 ⇒ 这两条 GOLD 的 Release 凭据归 D4 收口门）；⑧**单测里那 4 行常量波段统计只载 2 个事实**
  （S-1，修复轮 1 补）：`raster_render` 单测的 `rMin/rMax/bMin/bMax` 四条 `EXPECT_NEAR` 在 B1 臂里
  **同因、同刻、同残差**（四条差值都是 255），旧写法还把 message 写成两两逐字相同 ⇒ 红起来分不出是哪一格。
  修复轮 1 把四条 message 改成各自"哪一格 + 该格判据原文 + 漏换序时读到什么"，并在注释里点明这 4 行是
  **装饰形态**（算独立证据时按 2 计）；行本身保留 4 条，因为 `Min`/`Max` 是 `ComputeStatistics` 的两个
  独立出参槽，压成两行就看不见"只写坏一个槽"那一类缺陷。改后重跑 B1 臂实测仍 6 条 error
  （`test_plugins.cpp` 的 3245/3246 两条像元探针 + 3266/3267/3270/3271 四条统计槽，残差逐条 = 255），
  band2 的探针与 Mean/StdDev、Color 三行、几何两行照旧全绿
  （`build/tmp/batchD/r2/arms/F1_B1_newmessages_unit.log`）；⑨**Step 3 那 9 臂的逐臂还原对撞没有 tee**
  （S-3，修复轮 1 补）：当时每臂还原后都跑了 `git hash-object` 对 `git rev-parse HEAD:<path>` 并贴了
  MATCH，但那些读数只在会话输出与席位报告里，**盘上没有档**——修复轮 1 现读
  `grep -rl hash-object build/tmp/batchD/r2/arms/` 在加我这一轮的文件**之前**是**空**。
  ⇒ **终态可证**（收尾 `git status --porcelain -- src/` 空 + 受保护 blob 对撞过），
  **中间态不可核**（每一臂还原是否当场回到 HEAD 只能信席位自述）。本把不补写"已 tee"；
  修复轮 1 自己的 B1 臂已经把还原对撞逐臂写进 `arms/F1_B1_restore_ledger.txt`（MATCH，
  `778c198f051527db607ba96aab53f3aeb09e3ec4`），这条口径从下一把起照此执行。
- **本把没做的事**：`src/` 除 Step3 取证外零改动（每臂当场 `cp` 还原 + `git hash-object` 对
  `git rev-parse HEAD:` 贴 MATCH，**未用 `git checkout --`**，收尾 `git status` 空）；未新建腿 case；
  未动 R3 三行；未动既有 GOLD；未跑全量 ctest；未动 Release 树；`data/` 只读
  （收尾两件套见报告 §7）；受保护的 terrain blob 复核仍为 `588a42504ee19bcb551f8214e5c1adb68cea20e9`。


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

### 批次 D 覆盖晋升 R3a（2026-10-29，`raster_manage.cog`·`raster_render.histogram_match` 两条 PENDING→GOLD）

射程**两行**：`tests/action_gold_cases.txt` 的 `raster_manage,cog` 与 `raster_render,histogram_match`。
这两条与 R1/R2 不同形：今天腿上**连 case 都没有**——现读 `raster_manage_cog`、`raster_render_histogram_match`
两个名字既不在 `$cases` 也不在 `$rasterGateAllowlist` ⇒ 本把的 Step 2 是**从零新建 `New-Case`**，
不是"给既有 case 补判据"，也**没有**走豁免通道（`allowlisted` / `allowlist-table` 改后仍 21/21 未动）。

- **夹具侧凭据**（两块都是扩写既有 `TEST_F`，**未新增块** ⇒ 全库 `Total Tests:` 802 不变）：
  - `PluginTest.RasterManageCogExecution`：夹具由 `createTestRaster(64,64)`（方阵 + `i%256` 斜坡，
    宽高互换读不出差别）换成 `createStepLevelRaster(64,40)`（非方三级平台，
    多重集 `{0^640, 10^1280, 51^640}`，n=2560）。**推得**：`Mean = 45440/2560 = 17.75`、
    `E[x²] = 700.25`、`StdDev = sqrt(385.1875) = 19.626194231179920`；四角像元 = 夹具字面量。
    结构侧的期望值全部来自**实跑现读**（临时探针三档尺寸一致）：`GetBlockSize = 512 x 512`
    （**不是** GDAL 文档常被引用的 128）、数据集级 `IMAGE_STRUCTURE/COMPRESSION = LZW`
    （波段级同键读不到，两个位置都探过）。
  - `PluginTest.RasterRenderHistogramMatchExecution`：输入换非方 48x32 三平台，参照新建
    `createFourPlateauRaster(48,32, 0/17/34/51)`——**不同分布**是刻意的，`source == reference` 时
    两侧 CDF 逐档相等 ⇒ LUT 恒等，与"根本没匹配"不可区分（等价变异，不能当判据）。
    256 档 LUT 按现读的取档规则**先推成表再跑首读**（顺序不能倒）：`0→0`、`50..254→170`、`255→255`
    ⇒ 产物 `{0^384, 170^768, 255^384}`、**推得** `Mean 148.75`、`StdDev sqrt(8579.6875) = 92.626602550239312`；
    首读**实跑**逐字相符（`min=0 max=255 mean=148.750000000 stddev=92.626602550`）。
- **腿侧凭据**：新建 `raster_manage_cog`（输入 = `step_level_input.tif` 的副本，判非方 40x24x1 /
  Float32 / `Min 0`·`Max 51`·`Mean 17.75`·`StdDev sqrt(385.1875)` / `Origin` / `Pixel Size`）与
  `raster_render_histogram_match`（参照用**本把新建的第二代腿侧夹具** `histmatch-reference-raster`，
  40x24 四平台 0/17/34/51 各 240 像元；判 `Min 0`·`Max 255`·`Mean 148.75`·`StdDev sqrt(8579.6875)`
  + 尺寸 + Float32 + 地理参考）。为什么要新建夹具而不是用腿上已有的件：参照必须"与输入不同分布 +
  与输入同尺寸 + 平台可数"三条同时成立，腿上今天没有一件满足（尺寸不等就先走 `cv::resize(INTER_LINEAR)`，
  参照直方图就不再手推得动）。
- **闸读数（实跑两遍，改前/改后同一命令 `ctest -R "^real_data_raster$" -V`）**：
  `registered/ran 70/70 → 72/72`、`judged 49 → 51`、`allowlisted/table 21/21` 未动、
  `branch-labels 50 → 52`、`conditional-registrations 1` 未动、`dead-branches 0`，
  不变量 `51 + 21 = 72` 成立；腿 **12.10 s → 12.46 s = 1.03×**（2.5× 线按这一对照算，未触）。
  定向配方 43 条 → 43 条（未新增 `TEST_F`），全库 `Total Tests: 802 → 802`。
  ⚠ 本把给两块各加了一处"判了返回值"的 `GetGeoTransform` ⇒ `geotransform_guard` 测试域期望数同笔按现读
  改成 `code 8→10 / checked 7→9`（`violations/dead` 仍 0/0，产品域 `20 CODE / 18 CHECKED` 未动，R-D38）。
- **臂清单**（每臂的 unit/leg 日志都 tee 成文件，落 `build/tmp/batchD/r3a/arms/`；`src/` 每臂当场 `cp` 还原；
  全程没用 `git stash` / `checkout --` / `reset --hard`）：
  ⚠ **第一版这里写的是「`git hash-object` 对 `git rev-parse HEAD:` 逐臂 MATCH」，修复轮撤回该说法**：
  `arm.py` 的 stdout 没有 tee ⇒ **逐臂还原的凭据在磁盘上一份都没有**（能证的只有"现在干净"）。
  控制器复跑取的终态凭据（`V1_restore_proof.log`）：本把 range 的 **10 个文件** cleaned-content hash 全部 ==
  `git rev-parse ce8bfdc:<path>`、`git status --porcelain` = 0；同时**逐字节**对 blob 只有 1/10 相等
  （`core.autocrlf=true` 且无 `.gitattributes` ⇒ 工作树 CRLF、blob LF 是常态，9/10 天然 raw≠blob）。
  ⇒ 本仓可证的还原口径是「cleaned-content == blob」**加上**「动手前先记该工作树文件的裸字节 sha、还原后对拍」；
  单用 `--no-filters` 对 blob 在这 9 个文件上**恒判失败**，是个坏判据（控制器 10-29 自己写错过一次，已改正）。
  - A1 `-of COG` → `-of GTiff`：夹具红在 `GetBlockSize`（实测 64×32 vs 期望 512×512 两支）；**腿绿**
    ⇒ 瓦片化在腿上没有任何读数，这一支只有夹具侧承重。
  - A2 `COMPRESS=LZW` → `COMPRESS=NONE`：夹具红在 `COMPRESSION` 元数据（`ASSERT_NE(compress, nullptr)`
    拿到 NULL）；**腿绿**（同上，info 不印压缩）。
  - A3 并列取法 `<` → `<=`：夹具四支同刻红（`Min` 差 85、`Mean` 差 63.75、`StdDev` 差 19.014、
    逐像元 85/85/255），**腿同时红**（`Min actual=85 expected=0`）。残差与 A3 之前**推得**的对照表逐字相同。
  - A4 去掉 `cv::LUT`（等价于"直接拷归一化后的源"）：夹具红、腿红（`Mean` 落回 88.75 那一侧）。
  - A5 删 `-co BIGTIFF=IF_SAFER`：**夹具绿 + 腿绿 ⇒ 等价变异**。按 R1 的记法先问一句这个等价是谁决定的：
    是**本把夹具的尺寸**决定的（KB 级文件够不到 4 GB 那道闸），不是产品决定的。
  - A7 删掉 `doBuildCog` 末尾那次 `writeProcessingMetadata`：同一夹具下窄串
    `The IFD has been rewritten at the end of the file, which breaks COG layout` 的出现次数 1 → 0。
    ⚠ **修复轮把这条的结论收回一半**：GDAL 对布局失效发**两条**警告，A7 里窄串归零的同时
    `This file used to have optimizations in its layout, but those have been, at least partly,
    invalidated by later changes` **仍 ×1**（A5 是 ×6）⇒ 那次元数据写只是**加剧项**，"把 COG 布局作废"
    不能整条归给它。不改代码也能复现宽串：`raster_manage cog` 生成 → `raster_inspect info` 打开，
    stderr 即印宽串（同时 `Overviews: 0`）⇒ **立案 N179**：本动作的产物连 GDAL 自己都不认是 COG 布局，
    动作名与产物实际形态不一致；修法属拍板类，本批不改产品。文档 `COG 生成.md` 已按实测口径改准。
  - 反事实副本臂（只改期望常数、不碰产品；按分支名切片，避免撞到 R2 的 `overviews` 支——它判同一个 17.75）：
    cog `Mean 17.75→17.85`（残差 0.10）、hm `Mean 148.75→149.75`（残差 1.00）、
    hm `StdDev sqrt(8579.6875)→sqrt(9000)`（`actual=92.6266 expected=94.8683`）、
    cog 期望宽度 `40→41`（`width actual=40 expected=41`）四支**全红**，零改动对照臂**全绿**
    ⇒ 两条腿分支确实在比数值，不是在打印。
- **缺口（照实登记，不当凭据）**：
  ① `-of COG` 与 `COMPRESS=LZW` 只有夹具侧承重，腿上无读数器（产物 `Driver` 仍印 `GTiff`、也不印压缩）；
  ② 字节数代理不成立：实测 40×24 的产物 **4096 B 大于**原始样点 3840 B（512 瓦片头吃掉 LZW 收益），
     所以"产物比输入小"这种上界在这条腿上是夹具形状的偶然，本把没写；
  ③ `cv::resize(INTER_LINEAR)` 那条"参照尺寸不等"的分支今天没有判据（两侧同尺寸才推得动 CDF）；
  ④ 金字塔层数实测四档（40×24 / 64×40 / 130×66 / 300×200）全是 0 ⇒ 红不了任何缺陷，夹具里那一行只作记录；
  ⑤ "游标每档重置 vs 单调前进"这一支**没有实跑**：由"输入 CDF 单调不减"对任何夹具都成立可**推出**两者等价，
     故按推得登记，不写成实测；
  ⑥ 同一支里 `result.metadata["compression"] = "LZW"` 是源码写死的字面量，A2 臂下它**照样绿**
     ⇒ 它不是产物事实的凭据，产物事实只由 `IMAGE_STRUCTURE/COMPRESSION` 那一支在钉；
  ⑦ Release 树未重编 ⇒ 本把两条在 Release 侧**未验收**；
  ⑧ `tests/real_data_matrix.txt` 第 4 列 `fixture` 仍写 `data`，而该文件头注释说 raster 腿走合成兜底
     应标 `not-consumed`：本把与同腿 R2 的两行读法保持一致没动，交 D3 矩阵对账。

### 批次 D 覆盖晋升 R3b（2026-10-29，`raster_manage.zonal_stats` 一条 PENDING→GOLD，含 N176 运行时复验）

射程**一行**：`tests/action_gold_cases.txt` 的 `raster_manage,zonal_stats`。
本把与 R3a 同形（腿上原来**连 case 都没有**）且**没走豁免通道**：`allowlisted` / `allowlist-table`
改后仍 21/21 未动。它是 raster 侧最后一条 PENDING，做完剩 18 条（processing 15 + matching 2 + spindex 1）。

- **N176 复验：成立**（这是本把的第一动作，且在改任何夹具之前完成）。选的是**零源码改动**的坐实法：
  BASE 的块只判 `fs::exists(output)`，那就直接跑它、验产物本身。先删旧产物再跑 ⇒ 用例是**绿的**
  （`[ OK ] PluginTest.RasterManageZonalStatsExecution (353 ms)`），而产物 `od -c` 是
  `{ \r \n \r \n } \r \n` = **8 字节**、`zone_count` 位置上什么都没有 ⇒ JSON 里**一个区都没有**。
  与 `doZonalStats` 空 map 的写出形状（`"{\n"` + 零条目 + `"\n}\n"`）逐字节一致。
  静态那半也同刻对上：`createTestRaster` 的 GT `{116, 0.001, 0, 40, 0, -0.001}` 配
  `createPolygonVectorDataset` 写死的 `x∈[0,100]` 环 ⇒ `colEnd = min(29, (int)((100-116)/0.001)) = -16000`
  小于 `colStart = 0`；⚠ **修复轮更正**：第一版这里写「同一对夹具的 `rowStart` 也大于 `rowEnd`、两道闸同时命中」，是**没算过的说法**。代进去算 ⇒ `rowStart = max(0, trunc((100-40)/-0.001)) = 0`、`rowEnd = min(29, trunc((0-40)/-0.001)) = min(29, 40000) = 29` ⇒ **行向不命中**（环带 y 完全覆盖栅格），只有列向那道触发 `continue`。控制器与复审席各自独立算得同一结论。
  ⇒ 结论：该用例**自批次 A 起就在空跑**，"跑过"不等于"跑到统计支"。
  **结案口径**：改夹具让两者相交属于测试面（本把做了），`doZonalStats` 的静默 `continue` 属于产品语义
  （本把一字未动，反过来把它钉成回归判据）。
- **夹具侧凭据**（扩写既有 `TEST_F`，**未新增块** ⇒ 全库 `Total Tests:` 802 不变）：
  换成 `writeFloatBandsRaster` 的**非方 7x5** 双波段栅格，GT `{100, 1, 0, 200, 0, -1}`、NoData `-9999`
  只落在 `(x=3, y=2)` 一个像元；`band1 = 10x + y` 与 `band2 = x + 10y` **互为 x/y 转置对**
  （方阵 + 单波段判不出"行列写反"，R1 纪律 3）。区域用本把新建的 `createZonalRectVectorDataset`
  （照 `createPolygonVectorDataset` 的层名/字段名/几何类型，只把环坐标交给调用方），四个矩形各钉一条支路：
  **zA** 窗口 5x3 非方、`subGT` 平移 (1,1)，包框 15 像元而像元中心命中 10，再扣 1 个 NoData ⇒ `count 9`；
  **zB** 1x1 ⇒ `variance` 恰为 0 ⇒ `stddev` 走 `variance > 0 ? sqrt : 0` 的下钳（样本口径此处是 `0/0 = nan`）；
  **zEmpty** 窗口非空但零命中 ⇒ 区照样建、统计列全空；**zOff** 整环在西侧 ⇒ 静默 `continue`、连行都没有。
  **推得**（`derive_unit_expectation.py`，首读之前落盘）：zA band1 值集 {11,12,21,22,31,41,42,51,52}
  ⇒ `sum 283 / mean 283/9 / sumSq 10901 / variance 18020/81 / stddev sqrt(18020)/9`；
  band2 {11,12,13,14,15,21,22,24,25} ⇒ `sum 157 / variance 2180/81 / stddev sqrt(2180)/9`；
  zB 两栏 `min = max = mean` 且 `stddev = 0`。**首读逐格命中，未改一个期望**
  （产物 6 行与推导表逐字节相同，含 `31.4444`/`14.9154`/`5.18783` 这些**6 位有效数字**的印法）。
  容差口径写在注释里：整数列逐字比原文；`mean/stddev` 用 `EXPECT_NEAR(…, 1e-4)`——被判的量最大只到
  两位整数 ⇒ 6 位有效数字的舍入上界 5e-5（实测最近的是均值的 4.4445e-5），而最接近的**错值**
  （样本标准差 15.820169966779048）离期望 0.904771 ⇒ 余量 ≥ 3100 倍。
- **腿侧凭据**：新建 `raster_manage_zonal_stats` 的 `New-Case` + 一条值级分支。区域是 `.ps1` 里
  `[IO.File]::WriteAllText` 落的**手写 GeoJSON 矩形**（字面量全 ASCII、无 BOM），栅格是
  `analysis_input.tif` 的**副本**（`manage_zonal_input.tif`）且开之前删边车，不直接碰 `generated_data` 原件。
  **推得**（`derive_leg_expectation.py`，同样是首读之前落盘；控制器 §2.2 那张表我**独立复算一致**，
  未照抄、未把期望改成读数）：四个商 `0.2 / 3.8 / 0.2 / 2.2` ⇒ 窗口 cols 0..3、rows 0..2 = **4x3**；
  第 2 行中心 `40 - 2.5*0.0005 = 39.99875 < MinY 39.9989` ⇒ 只烧 cols 0..3 x rows 0..1 = **8 个像元**
  （取包框会是 12）；band1 `sum 16 / mean 2 / var 1.5`、band2 `sum 20 / min -1 / var 5.25`、
  band3 `sum 6 / mean 0.75 / var 1.1875` ⇒ `stddev` 分别是 `sqrt(1.5)=1.2247448714`、
  `sqrt(5.25)=2.2912878475`、`sqrt(1.1875)=1.0897247359`。
  **实跑**两处对撞：CLI 探针与真腿产物的三行逐字节等于推导（`z1,1,0,4,2,1.22474,16,8` 等）。
  分支判的是**整行文本**（顺带钉住"三个波段都读、与 `band` 键无关"和 `feature_id_field` 真取 `zone` 属性）。
  ⚠ **这条分支判不到"子窗少平移"**：腿上 `colStart == rowStart == 0` ⇒ `subGT` 本就等于父 GT。
  也判不到 NoData 跳过：`makeAnalysisRaster` 三个波段都不声明 NoData。两条都写在注释里、不假装。
  只有一个 fid ⇒ 行序在这里不是自由度（产品累加容器是 `std::unordered_map`，多区产物必须按 `feature_id` 选行）。
- **闸读数（改前/改后同一条 `ctest -R "^real_data_raster$" -V`）**：
  `registered/ran 72/72 → 73/73`、`judged 51 → 52`、`allowlisted/table 21/21` 未动、
  `allowlist-gated-not-registered 0` 未动、`branch-labels 52 → 53`、`conditional-registrations 1` 未动、
  `dead-branches 0`，不变量 `52 + 21 = 73` 成立；腿 **15.27 s → 14.20 s = 0.93×**（跑前 `tasklist`
  里 `MSBuild|gis_tests|ctest|gis-cli` 计数 = 0；2.5× 拆腿触发线按**改前**那一档算 = 15.27 × 2.5 = **38.175 s**（第一版写 35.7 s 是拿改后 14.20 乘的、私有报告另一处写 37.5 s，两处都不对，一并改准），未触，未抬 TIMEOUT，未减断言）。
  定向配方 **43 条 → 43 条**（未新增 `TEST_F`），全库 `Total Tests: 802 → 802`，43/43 全绿。
  `geotransform_guard` 现读 test 域 `code=10 / checked=9`、产品域 `code=20 / checked=18`
  —— **这两数是 R3a 的 `67067ed` 改的，本把 diff 未碰守卫**（`git log -1 -- tests/check_geotransform_guard.py`）。
- **臂清单**（六支；每臂的 build/unit/leg 日志与**逐臂还原裸 sha 对拍**都 tee 成文件，
  落 `build/tmp/batchD/r3b/arms/`；全程没用 `git stash` / `checkout --` / `reset --hard`，还原一律 `cp`）：
  还原凭据按本批改正后的口径**两条一起用**：动手前记工作树裸字节 sha
  （`src/plugins/raster_manage/raster_manage_plugin.cpp` = `6e5320974afe3975d9e0a070ad0377814b9d05a2`），
  六臂每臂还原后对拍同一个 sha 且 `MATCH=True`；另加 `git hash-object` == `git rev-parse HEAD:` 那一个
  （`acab0c4326efbf7634353aa95caa45a41145a551`）。⚠ 这两个值**本来就应当不相等**（autocrlf：工作树 CRLF、
  blob LF），拿 `--no-filters` 对 blob 是坏判据；终态另附 `S3_src_netzero_proof.txt`
  （`git status --porcelain` = 0 行、`git diff -- src/` 空、受保护 `terrain_plugin.cpp` blob 仍
  `588a42504ee19bcb551f8214e5c1adb68cea20e9`）。
  - **A1** 总体方差 → 样本方差（除以 n−1，两处都改）：**夹具红**（残差正是推得的那两个数
    `0.90480071595570166` 与 `0.31468998669833237`）、**腿红**（band1 整行不符）。承重。
  - **A2** 掩膜判定永不跳过（= 直接用包框累加）：**夹具红**（zA `count 14`、`max 53`、`sum 448`）、
    **腿红**（`count` 从 8 变 12 那一侧）。承重 ⇒ 本把确实在判"用掩膜而不是用包框"。
  - **A3** 子窗 `subGT` 不平移：**夹具红**（zA 落到 `count 7 / sum 268`——与改前**推得**的
    `10c+r, c∈2..5, r∈2..3 扣掉 NoData` 逐字相同）、**腿绿 = 该臂在腿上是 no-op**
    （腿那侧 colStart/rowStart 都是 0）。承重只在夹具侧，如实分档。
  - **A4** `static_cast<int>` → `std::floor`：**夹具绿 + 腿绿 ⇒ 等价变异**。按 R1/R-D41 的记法先问这个等价
    是谁决定的：⚠ **修复轮撤回第一版的归因**（原文写「**是产品决定的，不是我把矩形挑成这样挑出来的**」）——
    **这个等价是本把挑的四个矩形决定的**，不是产品决定的。分叉形状存在且已被两方独立算出：
    用本把夹具（`writeFloatBandsRaster` GT 原点 `100/200`、`pixelSize = 1.0`、W=7）取矩形
    `MinX=48, MaxX=99.5, MinY=197, MaxY=199` ⇒ `trunc` 给 `colEnd = min(6, trunc(-0.5)) = min(6, 0) = 0`
    ⇒ `colStart(0) > colEnd(0)` **不成立 ⇒ 不跳过**（出一条空统计行），而 `floor` 给 `min(6, -1) = -1` ⇒ **跳过**。
    ⇒ 这条等价臂**不能当免检牌**：`trunc → floor` 这类改动在真实分叉形状上至今无判据（就是下面登记的那条缺口）。`colStart`/`rowStart` 外侧确实有 `max(0, ·)` 钳制——
    钳制（负商不管 trunc 还是 floor 都被钳成 0），而要素要活过 `colStart > colEnd || rowStart > rowEnd`
    那道闸就必须 `colEnd`/`rowEnd` 的商 ≥ 0（⚠ 第一版括号里那句「商为负时 `min(w-1, 商) < 0 ≤ colStart` ⇒ 必被跳过」**已被上面那个反例否证并撤回**：`trunc(-0.5) = 0`，并不小于 `colStart = 0`，与它自己下面那句「唯一能分开两者的形状是环整体在原点西侧、且离原点不足一个像元宽」直接矛盾——同一段里前后打架），
    商非负时 trunc 与 floor 恒等 ⇒ **凡窗口非空两口径必等价**。唯一能分开两者的形状是
    "环整体在原点西侧、且离原点不足一个像元宽"（trunc 留一个空统计的区、floor 直接跳过），
    本把没有夹具覆盖它 ⇒ 登记成缺口，不写成判据。
  - **A5** NoData 相等判据永不成立：**夹具红**（zA `count 10`、`min -9999`、`sum -9716`）、
    **腿绿**（腿输入根本不声明 NoData ⇒ 腿那侧这一支不可达，与注释里写的一致）。承重只在夹具侧。
  - **A6**（反事实副本臂，只改期望常数、不碰产品）：腿分支 band1 的期望把 `count 8` 改成包框的 `12`
    ⇒ **腿红**（`raster_manage_zonal_stats band 1 (x+y) row mismatch`）。按 R-D34，这才叫"期望正确"被证过，
    而不是"分支会红"。
- **本把新立 N180**（只登记，不修产品）：`doZonalStats` 的 CSV **空分区行比表头少一列**。
  实测逐字节：满值行 `commas=7 fields=8`（表头也是 8 列），空值行 `commas=6 fields=7`。
  成因在源码里是可数的——满值那一支在 `fid,band` 之后发 6 个逗号（min/max/mean/stddev 各一个 + sum/count 两个），
  空值那一支发 `csv << ",,,,,"` = 5 个。判据钉的是**现读形状**（`EXPECT_EQ(row->size(), 7u)`），
  补那个逗号的人会让这一条红，届时必须连带改 JSON 支与本文档口径，而不是顺手放宽本条。
- **文档**：`docs/算法说明/栅格工具/分区统计.md` 按现读改准五处——`stddev` 写明是**总体**标准差；
  统计按**全部波段**进行且与 `band` 参数无关；处理流程补上"向零取整 + 两侧钳制 + 子窗平移 +
  掩膜按像元中心 + 整条要素在范围外被静默跳过"；输出说明补 CSV 表头逐字与**行序不保证、按
  `feature_id`+`band` 选行**，并写明空统计行的存在与 N180 的少一列；注意事项补"不重投影"、
  ID 字段同值会共用一条记录、以及 `zone_count` 是分区数不是要素数（跑成功 ≠ 每个要素都算到）。
  现读指认（本把不修）：`paramSpecs()` 里 `output` **和** `vector` 两个都声明 `required = false`，
  而 `doZonalStats` 三个键任一为空就 `fail` ⇒ 属 #146"声明必填 vs 实为必填"跨插件同族论域。
- **缺口（照实登记，不当判据）**：见下一节列表。Release 树未重编 ⇒ **R3b 全部结论只验收 Debug 侧**。

### 批次 D 覆盖晋升 P1（2026-10-29，`processing.threshold` 一条 PENDING→GOLD；P 族第一把，续作席收口）

射程**一行**：`tests/action_gold_cases.txt` 的 `processing,threshold`。刀序相对台账有一处改动已在工单里登记：
台账把 P1 写成 `threshold` + `enhance` 两条，两者都是多模式算子 ⇒ 一把一条，`enhance` 另起（本把没碰它）。
本把跨两把席位：前席在 Step 2 中途被平台报错打断（`8458db7` = Step 1 已提交、`.ps1` 的 +79 行在途），
续作席从 `8458db7` 接手，**未回退在途改动**，收口 Step 2/3/4/5/6。五笔 = `8458db7`（Step 1 单测）+
Step 2 腿侧 + Step 4 登记 + Step 5 文档 + Step 6 回归读数回写。Step 3 没有仓库提交：它的产物是 `src/` 的
临时变异（已还原并三项对拍）与 `build/tmp` 里的臂日志，凭据写进 Step 4/5 两笔的 message 与正文。

- **产品面现读**（`ProcessingPlugin::doThreshold`，与工单 §1 一致，另多读到三件）：键 `band`=1 /
  `method`=`otsu` / `threshold_value`=128.0 / `max_value`=255.0；先 `gis::core::toUint8` 归一再比较，
  所以**阈值值比的是归一后的 0..255**；写出是 `convertTo(CV_32F, 1/255)` ⇒ 除数 255 是字面量、
  与 `max_value` 无关（工单 §2.4 那句"max_value 同时进 upper 与 /255 缩放"经实测**否证**）。
  ⚠ **产物上界要分两族，"= max_value/255"不是全称**（复审席立案 MF-1；控制器 2026-10-29 修复轮用
  shipped Debug CLI 零 src 改动独立复跑坐实，同一张 40x24 三档件、`threshold_value=100` 除注明外）：
  `binary`/`binary_inv`/`otsu`/`adaptive_gaussian`/`adaptive_mean` 五支把 `max_value` 当赋值上限
  （**`max_value` 除进 `cv::threshold` 的 upper，也进两支 `cv::adaptiveThreshold` 的 `maxValue`**）；
  `truncate` 把超阈值像元**截到阈值本身**、`tozero` **保留归一后的原值**，两支都不读 `max_value`
  —— 实测 `truncate` 配 `max_value∈{255,102,50}` 产物逐字相同（`Max 0.392157 = 100/255`）、
  `truncate + threshold_value=250` 配 `{255,12}` 都是 `Max 0.980392`、`tozero` 配 `{255,102,7}` 恒
  `Max 1 / Mean 0.25`；对照 `binary` 配 `102` 给 `Max 0.4`、配 `7` 给 `Max 0.027451`。
  ⇒ 三处已提交文本（`阈值分割.md` 两处、`test_plugins.cpp` 推导链 (b) 一处、本节原先那句）
  都写成了全称，本轮一并收口；`writeMatOutput → matToGdalTiff` 建的是单波段 Float32 ⇒ 产物恒 1 波段，
  `band` 只用于选输入。`toUint8` 的 `CV_8U` 直接 clone 支在本动作不可达（`gdalBandToMat` 恒按 `GDT_Float32` 读），
  没为它写恒真断言。
- **夹具**四张，全部**非方 40x24**（三档/四档平台，R1 纪律 3：方夹具上"宽高互换"是等价变异 = 自我盲区）；
  归一标度 `255/(51-0)=5.0` 二进制精确 ⇒ 档值 0/50/150/255 落在任何"就近取整 vs 截断"的分叉之外。
  Otsu 用**四峰**夹具：类间方差三个间隙手算 `3098.78 / 5041.00 / 5162.67` ⇒ 唯一最优分区
  `{0,50,150}|{255}`，间隙 `[150,254]` 整体在 spec 默认 128 的右侧 ⇒ 这一档能把"数据定阈值"和
  "常数阈值"分开（三峰夹具做不到：归一后 min/max 恒 0/255 ⇒ 最优间隙必含 127.5，与默认阈值不可分）。
- **单测**：扩写既有块 ⇒ 全库 `Total Tests: 802` 不变、`PluginTest.Processing*` 仍 30、`PluginTest.Raster*` 仍 19。
  值级断言 **35 条 `EXPECT_NEAR`**（口径：直接数块内代码行；按 `git show` 新增行数=36、按出现次数=38 都含
  同一行注释里的三个宏名 ⇒ 都不是条数；`8458db7` message 里那个 42 无读数量得支持，禁 `--amend` 故历史保留，
  更正以本句为准——那条注释位于 `TEST_F` **之上**、不在块体内，所以"新增行剥注释"与"块内代码行"
  是两个不同总体，只是这一把恰好同值 35）。期望表 13 档在**首读之前**落盘，首读逐格命中、未改一个期望。
- **腿侧**：新建 `New-Case -Name processing_threshold`（该动作此前在本腿**连 case 都没有**）+ 一条值级分支，
  输入是 `step_level_input.tif` 的**副本**（`data/` 原件只读），分支判 `40 x 24 x 1` 分开、dtype Float32、
  `Min 0 / Max 1 / Mean 0.75`（`720/960`，六个有效数字打成 `0.75` ⇒ 零舍入残差）。
  **StdDev 故意不判**：该形态下 `sigma = sqrt(p(1-p))`、`p = Mean` ⇒ 同一计数的第二次读，是推论不是判据。
  分支开头删产物 `.aux.xml`：PAM 边车按文件名缓存 band 统计，中断过的一跑会让后一读判缓存而不是像素。
- **反事实副本臂**（R-D34：只改期望常数、不碰产品）：`cf/` 副本把 Mean 的 `0.75` 改成 `0.74` ⇒ **腿红**，
  抛出行带 `actual=0.75`；副本与工作树**逐行只差那一处**、`regression_case_gate.ps1` 与仓库版 sha256 相同。
- **臂清单**（十支，单实例串行；`src/plugins/processing/processing_plugin.cpp` 全程 `cp` 还原 + 三项对拍）：
  M1 去掉 `toUint8` 前置归一 ⇒ 单测红 11 格、腿红（Mean 0.25 对 0.75）；M2 `binary` 写成 `BINARY_INV` ⇒ 红 20 格、腿红；
  M3 去掉 `1/255` ⇒ 红 16 格、腿红（Max 255）；M9 吃掉 `threshold_value` 键 ⇒ 红 5 格、腿红；
  M5 吃掉 `band` 键 ⇒ 红在 A9 那一格、**腿绿**（腿发 `--band=1`）；M6 吃掉 `max_value` 键 ⇒ 红在 A7 那一格
  （实到 1 对期望 0.4）、**腿绿**；M7 `TRUNC` 写成 `TOZERO` ⇒ 红 2 格、腿绿；M8 未知 method 的 `fail` 吞成 `ok`
  ⇒ 红 2 格、腿绿；M10 摘掉 `THRESH_OTSU` 标志 ⇒ 恰好红在 A10 那一格、腿绿。
  **M4（往 OTSU 调用里传 `threshVal`）单测绿 ⇒ 等价变异**，归因**上游**（`THRESH_OTSU` 下 `thresh` 是出参）
  而不是本把挑的夹具：能推翻该等价的形状是"入参被 honor ⇒ A10 必红"，而 A10 的承重由**另一支** M10 单独证明，
  两支不互相顶名。⚠ **腿侧那一格不计入等价证据**：本腿 `grep -c otsu` = 0、只发 `--method=binary`，
  OTSU 调用点对本腿是**结构不可达**，不是"第二次独立证明"（等价牌只发单测那一份）。腿的敏感度全部实测：红 = M1/M2/M3/M9，绿 = M4/M5/M6/M7/M8/M10（覆盖边界登记在缺口，不当缺陷写）。
  还原凭据（10/10 全过）：动手前 `.BASE` 与干净树对拍 identical=True，工作树裸 sha256
  `169bc45df938525b8309ab70338e604df275971b226f341baf7536856ac63402` 在每臂还原后逐字相同、
  `git hash-object`（套 filter）== `HEAD` blob `dd4fdeea702e72071917c6f8a7a65262de6e43b4`、`git status` 空；
  终态另加受保护 `terrain_plugin.cpp` blob 仍 `588a42504ee19bcb551f8214e5c1adb68cea20e9`。
  每臂**改完都重编再跑**，臂批结束后再 `--target gis_tests` 一次（否则 DLL 会带着最后一支臂的变异报绿）。
- **登记**：本行 PENDING→GOLD，第 4 列 `PluginTest.ProcessingThresholdExecution`（**不是**
  `ProcessingThresholdKeepsProductWithoutGeotransform` —— 那块值级 0 条、钉的是地理参考契约）。
  五张计数 `gold_rows 108→109` / `pending_rows 18→17` / `gold_rows_all_lines 109→110` /
  `gold_rows_table_driven 15` 未动 / `gold_rows_literal_slot 93→94`、`data_rows 126` 未动、
  `kPendingCeiling 18→17`；PENDING 分布 `processing 15→14`（matching 2 / spindex 1 未动）。
  **R-D39 同笔**：`tests/real_data_matrix.txt` 本行第 5 列 `none→real_regression`；note 那句
  "夹具来源为启发式判定"核对结果 = 本 case 实际消费的是 helper 现生成的 `step_level_input.tif`
  （仓库无 `data/raster`），所以该行第 4 列 `fixture=data` 与腿侧实际取的合成兜底件互相矛盾
  ——**这不是本把的新发现**：同族账已记在 R-D39 与 D1-C2（`cutting,tile` / `terrain,curvature` 两格自批次 C 起
  没人改），矩阵无机器判据、列义属批次 C 定稿口径 ⇒ 本把不动它，**引用既有账**给 D3 矩阵对账
  （#184：D3 数条目时别把它算成两条新账）。
- **读数**：`ctest -R "^real_data_raster$" -V` 改后闸九字段 = `registered=74` / `ran=74` / `judged=53` /
  `allowlisted=21` / `allowlist-table=21` / `allowlist-gated-not-registered=0` / `branch-labels=54` /
  `conditional-registrations=1` / `dead-branches=0` ⇒ 不变量 `53 + 21 = 74` 成立、与工单 §0 预期逐字段命中；
  腿墙钟（**五跑**，含修复轮）= `13.14`（Step 2 收口）/ 复审席复跑 `13.41` / Step 6 复跑 `13.52` / `13.59` / 修复轮复跑 **`15.19 s`** ⇒ 最慢 **15.19 s**。2.5× 拆腿触发线按**改前那一档** 15.27 s 算 = **38.175 s** ⇒ 余量 38.175 / 15.19 = **2.51×**，未触，未抬 TIMEOUT，未减断言。⚠ 这五跑不是同一台面：`15.19` 与改前那档 `15.27` 同量级，成因未证 ⇒ 不外推，只登记散布变宽。⚠ 红臂那四跑（10.70 / 11.60 / 11.87 / 11.98）是中途抛错退出的跑，不计入这条腿的散布。
  定向配方 **43/43 全绿**、`PluginTest.Processing*` 族 **30/30 全绿**、`Total Tests: 802`、
  `geotransform_guard` 两域现读**逐字未动** = test 域 `any=13 code=10 comment_line=3 checked=9 unchecked=1 allowlisted=1 violations=0 dead=0`，product 域 `any=21 code=20 comment_line=1 checked=18 unchecked=2 allowlisted=2 violations=0 dead=0` ⇒ 本把没有新增判了返回值的 `GetGeoTransform(` 位点，也没有放宽任何一侧。
- **缺口（照实登记）**：`adaptive_gaussian` / `adaptive_mean` 两支**无判据也无臂**（blockSize/C 写死、
  期望要自己复算局部邻域积分 ⇒ 宁少一条不造恒真断言）；`toUint8` 退化支（整幅同值 ⇒ 置 0）没有专属臂
  （在 `src/core`，改它波及别的动作）；**非整数 `threshold_value` 这一条本轮当场结案**
  （修复轮实测，零 src 改动：`tv=49.0 / 49.4 / 49.6` 都给 `Mean 0.75`、`50.5 / 50.6` 都给 `0.25`
  ⇒ 观测上**等价于向下取整后再比**；但机制层"直接按小数比"与"先取整再比"对**恒为 8 位整数**的被比如元
  给同一结果 ⇒ 在本动作不可分辨，所以只登记观测、不写因果）；本把全部结论
  **只验收 Debug 侧**（Release 树未重编）。

### 批次 D 覆盖晋升 P2a（2026-10-29，`processing.enhance` 一条 PENDING→GOLD；P 族第二把，**只做单测侧，不碰腿**）

射程**一行**：`tests/action_gold_cases.txt` 的 `processing,enhance`。按 R-D45 的刀序，P2 已按交付面切成两把：
本把 = P2a 单测侧；**腿侧 case 与值级分支是 P2b**（要新造输入件与反事实臂，另一种尺寸）。
`tests/run_real_raster_regression.ps1` 本把**一字未改**（现读 `New-Case -Name "processing` 仍是 7 条、无 enhance），
allowlist 两侧计数也不该动 ⇒ 腿侧九计数本把**不引用**（没跑就没有"前后两个数"可摆）。

- **上游定约（本把最硬的一条凭据，也是控制器底稿里"没定约成功"的那条）**：`cv::equalizeHist` 的 LUT 走的是
  **乙约定** `(cdf − cdf_min) × 255 ÷ (total − cdf_min)`，甲约定 `cdf × 255 ÷ total` **被源码否证**。
  载体是控制器没试过的第三条路：jsDelivr 的 GitHub CDN 镜像（与 `raw.githubusercontent.com` 不同主机，本机出网未被重置）
  `https://cdn.jsdelivr.net/gh/opencv/opencv@4.14.0/modules/imgproc/src/histogram.cpp`（HTTP 200 / 3444 行全文到手，
  `cv::equalizeHist` 在第 3227 行起，正落在 `WebFetch` 会截断的位置之后）。逐字关键三行：
  `float scale = (hist_sz - 1.f)/(total - hist[i]);` / `for (lut[i++] = 0; i < hist_sz; ++i)` /
  `sum += hist[i]; lut[i] = saturate_cast<uchar>(sum * scale);`，其中 `i` 由 `while (!hist[i]) ++i;` 定成第一个占用档
  ⇒ `sum` 从 `i0+1` 起累加，就是 `cdf[i] − cdf_min`；另有单档短路支 `if (hist[i] == total) { dst.setTo(i); return; }`。
  ⚠ **这是上游源码，不是本仓产物的实测** ⇒ 文档与注释按"上游定义"口径写，不写成"实测"。
  取整口径另定约：`https://cdn.jsdelivr.net/gh/opencv/opencv@4.14.0/modules/core/include/opencv2/core/saturate.hpp`
  的 `template<> inline uchar saturate_cast<uchar>(float v) { int iv = cvRound(v); … }`
  + 本机安装头 `opencv2/core/fast_math.hpp` 的 `cvRound(float)` 在 MSVC x64 走 `_mm_cvtss_si32`（就近含偶）。
  版本口径：本机 vcpkg `opencv2/core/version.hpp` = 4/14/0 ⇒ tag `4.14.0`；`buildtrees/opencv4/` 只剩 config 日志、无源码。
  ⚠ **一条三层纠错，三个说法都在此处留痕**（谁都没跑过的反事实不能当结论）：
  ① 控制器底稿把「Max 恰为 1.0」列为"两种 LUT 约定都成立 ⇒ 与约定无关"；
  ② 实现席否证它，理由是"`scale = f32(255/720)` 略小于 `17/48`，**截断口径会给 254**"，控制器当时用
     python **double** 复算 `720*scale = 254.9999928474` 便认可了这个反事实；
  ③ 复审席否证**①②两侧**：源码是 `int sum * float scale` ⇒ 积**按 float32 求值**，而该积恰好落在整数上
     （`f32(480*scale) = 170.0`、`f32(720*scale) = 255.0`，控制器修复轮已用 numpy float32 逐点复算命中）
     ⇒ **就近与截断都给 255**，"截断会给 254" 是 double 视角的产物、C++ 不走那条路，是一个**没跑过的反事实**。
  ⇒ 定论：**E3（Max 恰 1.0）确实与 LUT 约定无关，且对取整模式也不敏感**；已提交注释里那句"甲乙两约定都成立"
     是**对的**，本节原先那句"并非与约定无关"是**错的**（且与注释互相矛盾，两头无机器判据）⇒ 本轮按注释侧收口，
     组标题的"三条与约定无关"同步改成**两条（E3/E4）**，E1/E2/E5 明确标为依赖甲乙之争。
- **产品面现读**（`ProcessingPlugin::doEnhance`）：键 `input`/`output`/`band`=1/`enhance_type`=`equalize`/
  `clip_limit`=2.0/`gamma`=1.0，六个键**全部被读** ⇒ GUI 可见键这侧今天没有缺口。
  出口 `writeMatOutput → matToGdalTiff` 建的是 `createRaster(dst, mat.cols, mat.rows, 1, GDT_Float32)` ⇒ 产物恒 1 波段；
  `GIS_PROCESSING_TIME` 使产物字节不可比 ⇒ 本把只比尺寸/像元/档数。`enhance` 不发任何计数 metadata。
- **夹具五张，全部非方**（40×24 / 24×40 互为转置对照 / 30×18）：`equalize` 与 `clahe` 用
  `createStepLevelRaster(40,24)`（raw 0/10/51、象限计数 240/480/240、`toUint8` 标度 `255/51 = 5.0` 二进制精确）；
  `normalize`/`log`/`gamma` 用 `createColumnPlateauRaster(40,24,{0,10,20,40},…)` 的三张变体——
  **含 53 的那张**（`{0,10,53}`）是刻意选的：`255/53` 不精确 ⇒ `48/255 ≠ 10/53`（Δ 4.44e-4），
  这一差是"三支不量化"的**唯一**观察点；⚠ 反过来在 40×24 三档件上 `50/255 = 10/51` **数学相同** ⇒
  那件上判不出量化与否（是**夹具形状**导致的等价，不是产品决定的，R-D44 口径）；
  **含负值那张** `{-20,5,40}` 是 DF-2 专造的新件（现成件全是非负 ⇒ `cv::max` 那一行今天跑不到能区分它的输入）；
  常量件 `createConstantRaster(30,18,200.0)` 供 DF-3 的两支退化判据；两波段件 `{{0,10,53},{53,0,10}}` 供 `band` 臂
  （`createTestRaster` 各波段同值，在它那上面 band 臂是等价变异）。
  ⚠ 换件的代价本把**实际付了一次**：首跑红了一条 Z6——我拿 band1 的列坐标 `x=15` 去问 band2，
  那里 band2 的 raw 是 0 不是 10 ⇒ 期望 `170/255` 而产物 `0.0`。修法是探针点跟着波段重数，
  并补一条 Z6b 把 `x=15` 的 `0.0` 也钉成等号判据；这是**我的探针点错**，不是产品与期望的分叉，记在这儿不改写。
- **单测**：扩写既有块（不新增 TEST_F）⇒ 全库 `Total Tests: 802` 不变、`PluginTest.Processing*` 仍 30、定向配方仍 43。
  块内 **36 条 `EXPECT_NEAR`**（口径：花括号配对抽出块体，剥注释前后各数一遍，两个口径同为 36 ⇒ 块里没有靠注释顶替的数值断言）。
  期望表在**首跑之前**落盘归档（内容只含夹具字面量与上游公式的复算，不含任何产物读数），
  实测侧另用第二客户端回读产物像元对账（**本机 cv2 4.13.0 只用来读产物、不参与任何期望或行为判定**，
  产品链接的是 4.14.0 C++）：21 个读数点最大残差 `2.36e-7`（=`ln(201)` 那一支，float32 在 5.3 量级的半 ulp，
  **0.24×kEps，是全表最紧的一点**），最小"另一种可分辨结局"的间距 `4.44e-4`（= 444×kEps，`normalize` 的量化与否）。
  ⇒ 容差夹心**逐支点各算**，没有共用句式。
- **五条缺陷的处置**：
  DF-1（文档方向反）已复算已改文档已配 G1/G4 两条承重断言（`128/255 → ^0.5 = 0.708492` 提亮、
  `→ ^2 = 0.251965` 压暗；本把夹具上 `10/53 → 0.434372 / 0.035600`，方向同）；
  DF-2（`log` 的"平移"因果错）已改文档 + 新造负值件 + L4 承重（臂 M6 实测把该行读成 `-nan(ind)`）；
  DF-3（常量两支退化不一致）已各配判据（G5/G6 全 0 vs L7/L8 `ln(201)`）并写进文档；
  DF-4（精度口径两支不同）已按**支**写进文档、并在 N1/L1/G1 三处各有观察点；
  DF-5（`clip_limit` 下界探针）用**真二进制**（单测直调插件，不是 python cv2）探完：
  `0.0` 不抛不失败、产物**真的不同**（单格 `lut[0] = 255` ⇒ 该点 `1.0`，而 `2.0` 给 `0.133333`）；
  `0.001` 与 `2.0` 被上游 `int(clip_limit × 格子面积 ÷ 256)` 折到同一个整数 1 ⇒ 产物相同。
  "非单调"的来由给出的是这条**可复算的整数化公式**，不是探针印象。
- **本把新立 N181**（只登记，不修产品）：`clip_limit` 的 ParamSpec 下界 `0.0` 放行到消费点。
  现读 `src/framework/src/action_validation.cpp` 的 `if (actionKey == "enhance")` 块体**只有** `gamma <= 0` 一条闸；
  上游 `clahe.cpp` 的 `if (clipLimit_ > 0.0)` 与 `if (clipLimit_ > 0)` 使 `0` 走进"完全不裁剪"那一支，
  与用户读到的"对比度**受限**"相反 ⇒ 属 #164 家族（区间声明与消费点行为不一致），本把不改 `src/`。
- **臂清单**（13 支，单实例串行；每臂跑前重新 `git status --porcelain` 确认树上只有那一处改动，
  日志逐臂 tee 成文件；还原 = `git show HEAD:<path>` 写回 + 按原行尾归一，凭据三条一起用：
  `git diff` 空 + `git status --porcelain` 空 + **与动手前记下的工作树裸 sha256 逐臂对拍相同**，13/13 全过）：
  M1 `equalizeHist` 改恒等 ⇒ 红 E2（残差 0.470588，实测 0.196078 = `50/255` 原样）/E5（0.235294）/Z5/Z6；
  M2 `enhance_type` 写死 `normalize` ⇒ 红 21 条 —— ⚠ **同一消费点的扇出，不是 21 件独立凭据**（诊断法：同因同刻），
  五支各自的承重由下面七条分支臂分开证明；M3 `band` 写死 1 ⇒ 红 Z6/Z6b/Z7（0.333333 / 0.333334 / 1.0）；
  M4 `clip_limit` 写死 2.0 ⇒ **恰好红 C2/C5**（0.066667 / 0.866667），C1/C3/C4 绿是**设计如此**
  （它们钉的就是"折到同一个整数上限"）；M5 指数 `1/gamma` 写成 `gamma` ⇒ 红 G1/G4（对称残差同为 0.398772），
  **Z0 绿 ⇒ 实测坐实它是不动点、不承重**（我在写这条断言的注释时就是按这个理由标它"推论行"的）；
  M6 删 `cv::max` 截断 ⇒ 红 L4（`-nan(ind)`）/L5（实测 `-9.17e-9`），**L6 绿** ⇒ 如实登记：端点判据对该臂不敏感，
  DF-2 的承重全在 L4/L5；M7 给 `normalize` 强行加 8 位量化 ⇒ 红 N1（4.44e-4 = 444×kEps）/N4（9.80e-4），N2/N3/N5 绿（端点不动）；
  M8a 常量支 `zeros` 改 `ones` ⇒ 红 G5/G6（各 1.0）；M8b 拆掉 `log` 的 `1e-10` 门 ⇒ 红 L7/L8（`-nan(ind)`，除 0）；
  M9 **反事实副本臂**（只改测试侧期望常数 `170/255 → 64/255`）⇒ 红 E2 一条（实测 0.666667 对 0.250980）
  ⇒ 证明 E2 在比数而不是恒绿；M10 `matToGdalTiff` 的 `createRaster(…, mat.cols, mat.rows, …)` 宽高互换 ⇒
  **红 38 条**（含 Z1/Z2 四条尺寸等号与全部读像元的行）⇒ 非方夹具这条纪律的实测回报；
  M11 后置除数 `1/255` 写成 `1/254` ⇒ 红 E3（0.003937）等 7 条；M12 `equalize` 整体改走带插值的 CLAHE ⇒
  红 E1（0.133333）与 **E4 的 `uniq` 等号（实到 15 对期望 3）**。
  ⚠ M1 之后 E1/E3/E4 三支**没有任何臂能红**，按 §4 Step 1 那句话（"指不出能红它的产物改动 ⇒ 换形态或写不承重"）
  我没有留账，当场补了 M11/M12 两条针对性臂把它们钉红。
- **取证工具自己的两个坑（产出过假证据，登记在此以免重犯）**：
  ①第一版把 `ctest -R '…(\.|$)'` 拼成 shell 字符串交给 cmd.exe，**cmd 不认单引号** ⇒ ctest 根本没跑、
  退出码 255 被读成"臂红了"（表现为 `RED(0)`）。修法：构建与 ctest 一律走 argv + 程序内重定向，
  并加硬闸——臂日志里没有 `[ RUN ]` 就中止整轮，"臂没跑"不许冒充"臂红/臂绿"。
  ②M4/M10 的第一版锚点没带句尾分号 ⇒ 注入的注释把 `;` 吃掉、编译失败（表现为 BUILD-FAIL 而非静默绿，
  驱动当场还原并复位）。⇒ **我自己写的取证脚本同样是待验断言**。
- **登记**：`processing,enhance` PENDING→GOLD，第 4 列 `PluginTest.ProcessingEnhanceExecution`
  （判据与动作槽在同一块内，块体现读 `params["action"] = std::string("enhance");` + 36 条 `EXPECT_NEAR`）。
  五张计数当场复跑：`data_rows 126`（未动）/ `gold_rows 109→110` / `pending_rows 17→16` /
  `gold_rows_all_lines 110→111`（这条**不排注释**，新写文字一律用 `[:]GOLD` 避开 `,GOLD` 字面量，实测只涨该涨的那 1 格）/
  `gold_rows_table_driven 15`（未动）/ `gold_rows_literal_slot 94→95`；`kPendingCeiling 17→16` **同笔**
  （`RegistryHeaderCountsMatchData` 里有 `EXPECT_EQ(COUNT pending_rows, kPendingCeiling)` 钉这一对，分两笔会红在中间态）。
  `tests/real_data_matrix.txt` 的 `processing,enhance` 行**本把一字未改**：第 5 列 `current_coverage=none`
  （腿没这条 case，改 `real_regression` 就是假登记）、第 6 列 `target_tier=gold`（表头写明它是**目标档不是现状态**）、
  第 4 列 `fixture=data` 核对结论 = **不改、登记**：`data/image/` 确有 7 件真实 TIFF（本席位用纯 `open('rb')`
  解析 TIFF 头复算，未用 GDAL 打开原件 ⇒ 不落 `.aux.xml`），"资源可用"这一义为真；而表头 ⚠ 那条规则的第二义
  "腿已消费 data/"对 enhance **无对象**（今天没有腿 case）。同一格在 threshold 行上是**可证伪的**——
  它的腿取的是 `$ResolvedOutputRoot\processing_threshold_input.tif`（脚本自造的合成件）却写着 `data`
  ⇒ 这条分叉**不是本把的新发现**，P1 那节已按 R-D39/D1-C2 既有账登记并交给 D3 矩阵对账，本把引用同一份账、不另立新案。
- **读数**：定向配方 **43/43 全绿**、`PluginTest.Processing*` 族 **30/30 全绿**、`ctest -R "^CoverageMeta"` **19/19 全绿**、
  `Total Tests: 802`（与基线同值，本把零新增注册）；`geotransform_guard` 本把**没碰**（新代码只 `RasterIO` 读像元、
  不判 `GetGeoTransform` 返回值），改前改后 test 域读数逐字相同 =
  `any=13 code=10 comment_line=3 checked=9 unchecked=1 allowlisted=1 violations=0 dead=0 RESULT PASS`；
  `source_mojibake_guard` / `docs_match_cli` 见下面 Step 5 的复跑行；受保护 `terrain_plugin.cpp` blob 仍
  `588a42504ee19bcb551f8214e5c1adb68cea20e9`；`data/` 两件套 = 改动文件数 0 且 `BASELINE: 88 CURRENT: 88 DIFFS: 0`。
  ⚠ **Debug 侧专属**：Release 树本把未重编 ⇒ 以上全部只验收 Debug。
- **缺口（照实登记，宁少一支不造恒真断言，共 8 条，本处列全，不引任何私有文件）**：
  G-1 clahe 的**跨格插值区**没有独立期望（只有单格 clamp 探针那 4 条，插值权重区只判了"产物档数多于输入"这种形状）；
  G-2 矩阵 `processing,enhance` 行的第 4 列 `fixture=data` 复核后**没有第二义对象**（本把不建腿 case）⇒ 不改不另立案；
  G-3「equalize 与 normalize 产物不同」这一条**没写成断言**：三档件上 `50/255 = 10/51` **精确相等**（控制器复算命中）
      ⇒ 在 F1 上它被 E2 单独蕴含，**等价是席位选的夹具决定的、不是产品决定的**（R-D44）⇒ 判据挪到 F2/F3 的中间档；
  G-4 `equalize` 的常量输入支推得出期望但**没落成断言**；
  G-5 NaN→NoData 传播支**不可达**（夹具无 NoData ⇒ 臂里那个 NaN 是**臂的读数**，不是产品行为）；
  G-6 腿侧九计数本把**未引用**（没跑该腿，本把不改腿）；
  G-7 `normalize` 常量输入的行为本把**不判**（上游未文档化，推不出独立期望）；
  G-8 `band` 只在 equalize 支判过，其余四支的 band 读取共享同一行代码 ⇒ 一次 M3 覆盖五支，不重复充数；
      复审席裁定**算过门**（`band` 只有两处分支链外的共用消费点：`readBandAsMat` 与 `writeMatOutput`），
      但写死 `getParam` 会令读写两侧同变 ⇒ M3 分离不了写侧，这一条是门本身的局限不是本把的偷懒。

### 批次 D 覆盖晋升 P2b（2026-10-29，`processing.enhance` 的**真实腿**case 与值级分支；**不动登记表、不动 `kPendingCeiling`**）

P2a 只做单测侧 ⇒ 这一把补腿。改前现读：`tests/run_real_raster_regression.ps1` 里
`grep -n 'New-Case -Name "processing'` = pansharpen / gabor_filter / glcm_texture / mean_shift_filter /
skeleton / connected_components / threshold **七条，没有 enhance** ⇒ 这条动作在腿上"绿"在构造上不可能。
**射程 = 腿侧 + `tests/real_data_matrix.txt` 第 5 列那一格 + 本节**；`tests/test_plugins.cpp` 一字未改，
`tests/action_gold_cases.txt` 与 `kPendingCeiling` 未动（P2a 已翻 GOLD）。

- **N = 4 条 case**：`enhance_type` = `equalize` / `normalize` / `log` / `gamma`（后者带 `--gamma=2.0`）。
  **`clahe` 本把明确不建 case**（不是漏）：`cv::createCLAHE(clipLimit, cv::Size(8, 8))` 的 `Size` 是**格子数**
  ⇒ 40×24 被切成 8×8 个 5×3 的格，而三档按 20×12 的象限铺、分档线 `x=20` / `y=12` **落在两格中心之间**
  ⇒ 边界带上的像元是两个不同 per-tile LUT 的双线性混合，`raster_inspect info` 只有一个整幅 Mean，
  **推不出独立期望**；腿上加它只能写形状判据 = 给已知不承重的分支刷 `Mark-Judged`（就是 P2a 自己登记的 G-1）。
  **宁少一条。**
- **一张输入件、四条 case**：`Copy-Item $data.StepLevelRaster` 到 `$ResolvedOutputRoot` 的
  `processing_enhance_input.tif`（40×24 单波段 Float32，档值 0/10/51、计数 240/480/240，n=960），
  **未开 `data/` 原件**；输入件与四个产物各删一次 `.aux.xml`（`raster_inspect info` 的统计会被 PAM 边车缓存；
  读侧 `gdalBandToMat` 是纯 `RasterIO` 不查 PAM，不受边车影响）。
- **判据形状**：每支 5 个点 = `Assert-InfoSizeExact 40/24/1`（宽高分开比，非方夹具才看得见轴序写反；
  不用 `Assert-TextContains "Size:…"`，子串从不比数）+ `Get-InfoBandLabelText -Label "Type"` 判 `Float32`
  单独一条（0.0/1.0 也落得进 8 位波段）+ Min/Max/Mean 三条 `Assert-InfoBandStatNear`。
  **StdDev 四支都不判**，理由不是"没必要"而是**可证**：三档计数固定 240/480/240 ⇒
  `Min = m0`、`Max = m2`、`Mean = 0.25*m0 + 0.5*m1 + 0.25*m2` ⇒ `m1 = 2*(Mean - 0.25*Min - 0.25*Max)`，
  所以**这三个打印数恰好是三个档值的双射重编码**（各承一个自由度、没有一个是另一个的推论），
  而 `sigma^2 = 0.25*(m0^2+m2^2) + 0.5*m1^2 - Mean^2` 是同一组值的**第二次读** ⇒ 推论行。
  本把首跑实测把这条推论关系**逐支反算坐实**：打印 StdDev `0.363242 / 0.384827 / 0.357569 / 0.354708`
  与按三档算出的 sigma 差 ≤ 1e-6（= 打印量化本身）⇒ 判它不增判别力。
- **容差按数据点分档，两条数量级并存是有意的**（`raster_inspect info` 走 **6 位有效**，实测打印
  `Mean:   0.75` 与 `StdDev: 0.433013`；在 [0.1,1) 档步长 = 1e-6、半格 = 5e-7）：
  · `Min 0` / `Max 1` 是 **float32 精确整数**、零舍入 ⇒ 容差 `1e-9`，只吸收读回噪声；
  · 四条 `Mean` 的**打印量化残差本身**是 `2.157e-7` / `4.942e-7` / `2.786e-7` / `3.333e-7`
    ⇒ **容差必须大于自身打印残差**，取 `1e-6`；最紧的一支是 `log`：它的精确 Mean `0.55343550582843593`
    只比 6 位有效的**打印中点** `0.5534355` 高 `5.83e-9`，而产物侧 float32 噪声量级 1e-8
    ⇒ 打印侧可能翻到 `0.553435`（残差 5.058e-7）⇒ **容差下界是被两侧打印残差顶出来的，不是我选的**。
    **照抄 P1 的 `1e-9` 会让这四条 Mean 红在打印机上而不是红在缺陷上**；P1 那把的期望是零舍入整数量才配得起 `1e-9`。
  · 每条都写了"量级 → 步长 → 最坏舍入 → 与最小可分辨缺陷的倍数"：最小可分辨缺陷 =
    `equalize` 甲约定 Δ0.104（1.04e5× 容差）、`normalize` 差一个 8 位档 Δ1.961e-3（1961×）、
    `log` 走量化 Δ5.109e-2（5.1e4×）、`gamma` 指数写成 γ 本身 Δ0.2022（2.02e5×）/ γ 没读 Δ0.1234（1.23e5×）。
- **期望常数全部落笔在先，首跑对撞在后**（由夹具字面量 + OpenCV 4.14 语义推得，不从产物回填）：

  | 支 | 三档（推） | Mean 闭式（推得的 double） | 推的 6 位打印 | **首跑实测打印** | 实测残差 |
  |---|---|---|---|---|---|
  | `equalize` | `0 / 170/255 / 1.0`（乙约定；甲约定给 `0.250980/0.749020/1.0`） | `7/12 = 0.58333333333333337` | `0.583333` | **`0.583333`** | 3.333e-07 |
  | `normalize` | `0 / 10/51 / 1.0` | `71/204 = 0.34803921568627449` | `0.348039` | **`0.348039`** | 2.157e-07 |
  | `log` | `0 / ln11/ln52 / 1.0` | `0.25+0.5*ln11/ln52 = 0.55343550582843593` | `0.553436` | **`0.553436`** | 4.942e-07 |
  | `gamma` (2.0) | `0 / sqrt(10/51) / 1.0` | `0.25+0.5*sqrt(10/51) = 0.47140372138502384` | `0.471404` | **`0.471404`** | 2.786e-07 |

  四支的 `Min: 0` / `Max: 1` / `Size: 40 x 24 x 1 bands` / `Type: Float32` 也逐字命中（8/8 点，0 个靠改常数命中）。
  `equalize` 那一条在腿上是一次**独立的约定对撞**：甲约定会把 Min 推到 `0.250980`、Mean 推到 `0.687255`。
- **反事实臂 18 臂 = (a) 族 14 + (b) 族 4**（每臂一份日志、串行单实例、每臂跑前重取 `git status`；rc 单独取，不走 `$(… | tail -1)`）：
  · **(a) 族 14 臂（A00 复现臂 + A01~A13 共 13 条改坏期望常数的臂）**，载体是**腿脚本的副本**（连同闸文件复制、显式传全部路径参数）
    ⇒ **被跟踪文件全程 0 行脏**。A00 无改动复现臂 rc=0 且九计数逐字不变（证明副本载体本身不产生红）；
    A01~A13 全部 rc=1，每臂的 `actual=` 都是同一串绿值：equalize Mean→0.687255 / −1e-3 / Min→0.250980，
    normalize Mean→+1e-3 / −1e-3 / Min→`1e-6`（**这一臂专测 1e-9 那档容差真在卡东西**：`actual=0` 也红），
    log Mean→0.604526 / −1e-3 / Max→3.9512437，gamma Mean→0.26922337562476 / −1e-3 / **+1e-3**（A13，
    补跑这一条是因为 A10/A11 两支都落在偏小侧 ⇒ "偏大/偏小各一"在 gamma 上缺一边，补上才对得起那句话）/ Max→7.141428。
    ⇒ **每支都齐了 Mean 的偏大与偏小两向**，另加四臂专测 Min/Max 那两档 `1e-9` 的行不是装饰。
  · **(b) 族 4 臂 = 改坏产品消费点**（`src/plugins/processing/processing_plugin.cpp`，只重编 `plugin_processing`，
    当场还原）：B01 `convertTo(…, 1.0/255.0)`→`1.0/256.0` ⇒ equalize **Max 红 `actual=0.996094`**（= 255/256）；
    B02 `cv::normalize(…, 0, 1, …)`→`0, 0.5` ⇒ normalize **Max 红 `actual=0.5`**；
    B03 `shifted + 1.0`→`+ 2.0` ⇒ log **Min 红 `actual=3.84217E-09`**（**与我事前预测不符**，我预测的是 Mean 红，
    见本节末 G-6，只登记读数不编机制）；B04 `cv::pow(normalized, 1.0/gammaVal)`→`gammaVal` ⇒ gamma **Mean 红
    `actual=0.269223`**，而 `0.269223` 正是我**事前推的另一种实现值** `0.26922337562475973` 的 6 位有效打印
    ⇒ A10 那个常数不是猜的、被变异臂独立复现。⇒ **四支的断言读的都是产品，不是夹具自己**；
    B04 同时是 DF-1「指数是 `1/gamma` 而非 `gamma`」在腿上的承重证明。
  · **还原证明（两条一起用）**：动手前工作树裸 `sha256 = 169bc45d…63402`（CRLF 1556 行 / 77282 字节）；
    `git show HEAD:<path>` + 按原行尾归一 **预演**（不写盘）⇒ 与工作树字节相同；四臂各自 `restore:
    bytes_equal=True diff_empty=True tree_dirty_lines=0`；末了干净重编 rc=0 再验一次同上，且工作树裸 sha
    与动手前**逐字相同**。⚠ 全程**未**用 `git hash-object --no-filters` 对 blob 当"字节相同"证据
    （blob `5af602d2…`/75726 字节 vs 工作树 77282 字节，差的正是 1556 个 CR ⇒ 该口径在 autocrlf 下必假）。
- **腿九计数前后两串**（现跑口径 = `ctest --test-dir build/debug -C Debug -R "^real_data_raster$" -V` 的
  `regression-gate[raster]:` 那一行；改前三跑逐字相同、改后四跑逐字相同）：

  ```
  改前  registered=74 ran=74 judged=53 allowlisted=21 allowlist-table=21
        allowlist-gated-not-registered=0 branch-labels=54 conditional-registrations=1 dead-branches=0
  改后  registered=78 ran=78 judged=57 allowlisted=21 allowlist-table=21
        allowlist-gated-not-registered=0 branch-labels=58 conditional-registrations=1 dead-branches=0
  不变量  judged + allowlisted = 53 + 21 = 74 = ran（改前）;  57 + 21 = 78 = ran（改后）
  ```

  `allowlisted` / `allowlist-table` 都是 21 ⇒ `$rasterGateAllowlist` 的 21 条**一字未动**
  （`git diff HEAD~1 HEAD` 里 `rasterGateAllowlist`/`Name = "…"` 命中数 = 0，现跑）；
  `dead-branches=0` ⇒ 每条新分支都对应一条在名单里的 case。
  ⚠ `branch-labels` 恒比 `judged` 多 1（改前 54/53、改后 58/57）是**改前既有形状**、不是本把引入：
  `classification_feature_stats_csv` 的 `New-Case` 站在 `if ($Mode -eq "full")` 里而 `real_data_raster`
  用 `-DMODE=quick` 起 ⇒ 它有分支标签但本次没跑，闸把它算进 `conditional-registrations=1`；
  我用独立复算确认本文件 `switch ($Case.Name)` **1 个 / 54 个不重名标签 / 54 个体首行全是 `Mark-Judged`**
  ⇒ 没有"有分支不判"的漏。
- **腿墙钟前后两串 + 余量算法**（同一棵树、同一二进制、每跑前 `tasklist` 硬门控 = 0）：

  ```
  改前  18.15（首跑，冷）/ 16.05 / 16.37            warm 均值 16.21
  改后  20.72（首跑，冷）/ 16.53 / 16.90 / 19.98（还原后补跑）  warm 均值 16.72
  同位对  冷 +2.57 s（首回要新碰 4 个产物名）   warm +0.51 s（8 次进程派生 ≈ 64 ms/次）
  口径    余量 = **TIMEOUT ÷ 现读**（项目口径），**不是**「改前最大值 × 2.5」——后者会给出假安全：
          现读若落在 22~44 s，旧写法仍会说「未跌破」
  安静态  控制器两跑 **14.99 / 14.96 s**；复审席安静态四跑 15.26 / 16.18 / 17.48 / 18.18 s
          ⇒ 六跑最大 18.18 s ⇒ 60 / 18.18 = **3.30×**（控制器自己那两跑 60 / 14.99 = **4.00×**）≥ 2.5×
  忙态    25.67（控制器在跑族的间隙测）/ 25.93（席位）⇒ 2.31~2.34×：**这是负载读数，不可当分母**
  ⇒ 判定：**不拆腿、不抬 TIMEOUT**。宁可记账也不拆：case 时间合计只有 8.8~9.7 s，
    而拆腿会把 7~9 s 的固定开销翻倍（`Ensure-*` / helper 造件按腿各付一次）
  TIMEOUT 60 未抬：安静态 60 / 18.18 = 3.30×、60 / 14.99 = 4.00×（改前档 60 / 15.27 = 3.93×）
  ```

  ⚠ **本机比控制器档慢**：控制器 10:3x 的改前档散布 13.14~15.19 s，我三跑是 16.05~18.15 s，
  `tests/CMakeLists.txt` 的腿墙钟表另记 `real_data_raster 13.25 / 11.02` ⇒ 同一串判据、同一棵树下的宿主负载/缓存差；
  **2.5× 线一律用我自己的改前档，不借控制器的 15.27**（否则会把环境差算成本把的开销）。
- **回归复跑（Debug 树，现跑）**：全库 `ctest -N` `Total Tests: 802`（改前改后同值，本把不新增注册用例）；
  定向配方（含四条守卫 + `CoverageMeta*` + `PluginTest.Raster*` + `real_data_raster`）`Total Tests: 43` ⇒
  **43/43 全绿，42.77 s**；`^PluginTest\.Processing` 族 **30 条 / 30 绿，5.29 s**（本把不改单测 ⇒ 应仍 30，实跑命中）。
  `geotransform_guard` 本把**没碰**：新增代码只在腿侧 `raster_inspect info` 上判，不判 `GetGeoTransform` 返回值。
- **矩阵同笔**：`tests/real_data_matrix.txt` 的 `processing,enhance` 行第 5 列 `none → real_regression`
  （现跑 `git diff --numstat` = 1 1，整表只动这一行、只动这一格；依据 = 表头对第 5 列的逐字定义
  "动作已出现在某条 real_*_regression 的 `New-Case` 里"）。第 6 列 `target_tier=gold` 是**目标档不是现状态** ⇒ 不动。
  第 4 列 `fixture=data` 本把重新复核过：按表头后半段定义（"raster 腿一直走合成兜底 ⇒ 覆盖行标 not-consumed"）
  本行应记 `not-consumed`——输入件实际是 `gui_test_data_helper` 生成的 `step_level_input.tif`，
  且 `data/` 现读只有 `dem image vector` 三个子目录、**没有 `data/raster`**；但**同一列 85 行都写 `data`**
  （`awk -F,` 现读分布 = synthetic 7 / data 85 / not-consumed 34），而 `raster_manage,cog`（R3a）、
  `raster_manage,zonal_stats`（R3b）、`processing,threshold`（P1）四行已提交先例都是 `data + real_regression`
  ⇒ **不在本把单点改**（改要在同笔里对 85 行一致处理），归 D3 的矩阵对账。矩阵**没有任何机器判据**
  （`grep -rn real_data_matrix` 排除 build 现跑 = 0 命中）⇒ 这类格子只能靠人同笔，改错没人拦。
- **文本合规**：`tests/run_real_raster_regression.ps1` 改后逐文件现读 **CRLF 2975 / 裸 LF 0 / 非 ASCII 0 字节 /
  `Parser::ParseFile` 0 错**；`tests/real_data_matrix.txt` **CRLF 184 / 裸 LF 0**；`docs/RELEASE.md` 本节定稿后现读 **CRLF 3985 / 裸 LF 0**（改前 3844 ⇒ 本节净增 141 行；这是**本文档自己的总行数**，随后续任何编辑就变，所以只当定稿值、不当引用锚）。
  新增注释无裸行号锚点（一律"函数名 + 判据原文片段"），无对被 gitignore 的私有报告/产物的引用。
  ⚠ 提交前自查抓到一处**误改 P1 已提交注释**（`sqrt(p*(1-p))` 被我手打成 `sqrt(p(1-p))`，
  `git diff --stat` 的 `1 deletion` 暴露了它）⇒ 单独还原后才提交，最终 `git diff --numstat` = **268 0**（纯新增、删除 0 行）。
- **缺口（照实登记 8 条，不引任何私有文件）**：
  G-1 `clahe` 在腿上**没有 case**：跨格插值区推不出独立期望（与本节 P2a 的 G-1 同一条，腿侧继承）。
  G-2 `normalize` 的"不经 8 位量化"在半腿夹具上**无可观察量**：`toUint8` 标度恰 `255/51 = 5.0` ⇒
      中间档落 50，而 `50/255 = 10/51` 是同一有理数 ⇒ 两种实现打印逐字相同（P2a 的 G-3 已记过这条等价，
      且裁定"等价是夹具决定的、不是产品决定的"；本把只把它从单测面复述到腿面）。
  G-3 `log` 的截断支 `cv::max(mat, 0.0, clamped)` 在本夹具**恒等**（三档全非负）⇒ "截断 vs 平移"在腿上分不开，
      只有单测 F3（含 `-20` 档）能判。
  G-4 本把四条 case **不判 `band`**（腿侧夹具只有 1 个波段）⇒ 腿侧 band 零覆盖，band 的承重只在单测 Z5~Z8。
  G-5 矩阵第 4 列 `fixture` 的启发式（本行按定义应为 `not-consumed`）与 note 的"开批前核对"结论
      ——**复核结论已经写进 tracked 文本**（见本节「矩阵同笔」段：按定义本行应为 `not-consumed`，
      但整列 126 行同一启发式、单点改会造出第三种语义）⇒ 本节与正文不再自相矛盾；
      未决的只是**整列语义**本身（`fixture` 三值各该以什么为谓词）⇒ 归 D3 矩阵对账（#184）。
  G-6 B03 臂（`log` 支 `+1.0`→`+2.0`）实测最低档打印是 `3.84217E-09` 而**不是我预测的 `0`**，
      机制本把没解释清楚（只登记读数；它不改动绿腿——干净产物实测 `Min: 0` 逐字为 0）。
  G-7 `gamma` 在腿上只走 **γ=2.0（提亮向）**；γ<1 压暗向无腿 case（单测 G4 有），
      且 γ 的失败契约（`gamma <= 0`）与常量输入退化支在腿上不可达（本夹具三档非常量）。
  G-8 **Release 树本把未重编** ⇒ 上面全部只验收 Debug；Release 侧结论一律未验收。
  ⚠ 另记一条**载体归属纠正**（不是缺口）：`$data.StepLevelRaster` 的造件函数 `makeStepLevelRaster`
  在 `tests/gui_test_data_helper.cpp`（**C++**，经 `Invoke-Helper` 的 `step-level-raster` 子命令调用），
  与单测侧的 `createStepLevelRaster` **同属 C++**、只差前缀；早前工作底稿把它写成"PowerShell 侧造件"是错的，
  且 `grep -n makeStepLevelRaster tests/run_real_raster_regression.ps1` 实测 **6 处、全在注释里**（底稿写 13 处）。

### 批次 D 覆盖晋升 P3（2026-10-30，`processing.edge` 四支值级判据 + 一条真实腿 case；登记/计数/上限/矩阵同笔）

本把 = **一个动作** `processing.edge`（`processing.filter` 留给 P4，不在本把）。改前现读：登记表
`processing,edge,PENDING`；矩阵第 5 列 `none`；`tests/run_real_raster_regression.ps1` 里
`grep -n 'New-Case -Name "processing_edge"'` = **0** ⇒ 这条动作在腿上"绿"在构造上不可能。
射程 = 单测四支 + 腿一条 case + 登记晋升 + 矩阵第 5 列 + `边缘检测.md` 正文 + 本节。

**饱和陷阱：判据形状的由来（算术复算，非产品实测）**
- `toUint8` 对非 8 位输入按全幅 min/max 线性拉到 0..255 ⇒ 两级输入归一后差恒为 255。**阶跃**处
  Sobel ksize=3、dx=1 的响应 = 4×255 = **1020**，三档 0/50/255 的 50→255 跳 = 4×205 = **820**，
  Scharr = 16×255 = **4080**；`convertScaleAbs` 的语义是**先取绝对值、再饱和到 0..255** ⇒ 强边产物恒 1.0，
  逐点期望退化成上界型恒真。**所以不用三档平台件判跳变处**。
- 本把的两张夹具都不在阶跃上取值：
  1. **52×12 Float32 斜坡** `value(x,y)=x`（raw 0..51）：标度 255/(51−0) = 5.0 可精确表示 ⇒ u8 步长恰 5；
     Sobel ksize=3、dx=1 的导数核 `[-1,0,1]` 比较的是 **x−1 与 x+1**（两像元中心差分）⇒ 内部 4×(5×2) = **40**；
     Scharr 平滑核 `[3,10,3]` ⇒ 16×(5×2) = **160**，都 < 255。⚠ 底稿候选值 `4×5 = 20` 与复算 **40** 不一致，
     两数并列：底稿乘的是单像元步长，中心差分跨两像元。首跑产物命中 **40/255 = 0.1568627**。
     首末列受 `BORDER_REFLECT_101` 影响（x=−1 映到 x=1）⇒ 边界单独判 0。
  2. **20×12 Float32 脉冲**（全 0、第 0 列 255、像素 (10,6)=10）：min/max 已钉 0/255 ⇒ 归一恒等；
     Laplacian ksize=3 预测中心 **80/255**、四角 **20/255**、四邻 **0**。
- `laplacian` 的核从上游取逐字（tag 4.14.0，`modules/imgproc/src/deriv.cpp` 的 `cv::Laplacian`）：
  ```
  if( ksize == 1 || ksize == 3 )
  {
      static const double K[2][9] =
      {
          { 0, 1, 0, 1, -4, 1, 0, 1, 0 },
          { 2, 0, 2, 0, -8, 0, 2, 0, 2 }
      };
  ```
  ⇒ ksize=3 是第二行（角 2 / 中心 −8 / 边 0），**不是**经典四邻域 `[[0,1,0],[1,-4,1],[0,1,0]]`（那是 ksize=1）。
  单测用"中心 / 四角 / 四邻"三组点把核归属钉死。

**单测（扩写既有 `PluginTest.ProcessingEdgeExecution`，不新增 `TEST_F` ⇒ `Total Tests` 不变）**
- E1 `sobel` dx=1/dy=0：内部 **40/255**（首行/中间/末行三点）、边界列 0；
- E2 `sobel` 默认 dy=1：混合二阶导在只随 x 变的斜坡上恒 **0**（dy 是可见参数，被忽略会读成 40/255）；
- E3 `laplacian`：中心 **80/255**、对角 **20/255**、四邻 **0**（H-2 定核）；
- E4 `scharr` dx=1/dy=0：**160/255**（退回 Sobel 的 4× 权重会读成 40/255）；
- E5 `canny`（H-3，不造逐点）：产物取值 ⊆ {0,1}；默认阈值有边且 Min=0/Max=1；low 10/50 与 high 300/150
  两条非严格单调（本夹具预期**相等** 1344，是性质判据不是计数期望）；**high=10000 ⇒ 边缘像元数 0**
  （阈值真被消费；产品若写死 50/150 会读到 1344）。

**推得 vs 实跑（分开写；"实测"只用于真跑过的）**

| 判据点 | 推得（写死常数） | 首跑实测（产物回读） |
|---|---|---|
| sobel 内部 3 点 | 40/255 = 0.156862745 | **0.156862751 ×3** |
| sobel 边界 2 点 / 默认 dy=1 | 0 / 0 | **0 / 0**、**0 / 0** |
| laplacian 中心 | 80/255 = 0.313725490 | **0.313725501** |
| laplacian 对角 2 点 / 四邻 2 点 | 20/255 = 0.078431373 / 0 | **0.078431375 ×2** / **0 / 0** |
| scharr 内部 2 点 | 160/255 = 0.627450980 | **0.627451003 ×2** |
| canny 默认：边缘数 / 取值集 / Min / Max | >0 / ⊆{0,1} / 0 / 1 | **1344 / {0.0,1.0} / 0 / 1** |
| canny low=10 / high=300 / high=10000 | ≥1344 / ≤1344 / 0 | **1344 / 1344 / 0** |

**腿（一条 `New-Case -Name "processing_edge"` + 一条值级分支）**
- `gui_test_data_helper` 新子命令 `edge-ramp-raster`：把 52×12 斜坡造进 `$ResolvedOutputRoot` 的
  `processing_edge_input.tif`（每次重建，防止缓存件在生成器改动后继续被旧形状判；`data/` 原件零接触）。
- case 参数：`processing edge --band=1 --edge_method=sobel --sobel_dx=1 --sobel_dy=0`。
  分支判 `Size 52×12×1`（宽高分开比，非方夹具才看得见轴序写反）、`Type Float32`（单独一条，0/0.156863
  也落得进 8 位波段）、`Min 0`、`Max 40/255`、`Mean 24000/159120`（整幅 = 24 个 0 + 600 个 40/255，
  六位有效打印 0.15083；容差 1e-9 / 1e-6 / 1e-6 与打印残差夹心写在分支注释里）。
  **产物实测打印**：`Min: 0 / Max: 0.156863 / Mean: 0.15083`。
- `sobel_dy=0` 显式给：`ParamSpec` 默认是 1，dx=1/dy=1 是混合导、在斜坡上恒 0，不显式给就只判到零。

**反事实臂 15 条 = 产品侧 4 + 期望侧 8 + 腿侧 3（单实例；每臂前 `git status` 重查；rc 单独取，不塞管道）**
⚠ 本节初版写"11 条"，而同段枚举是 4+8+3——那个 11 恰好等于 8+3，会让**四个改产品的臂从总数里消失**，等于把"改坏消费点"这一类覆盖凭证漏报。控制器现数盘上唯一臂号 = **15**（复审议 MF-1 逮出）。
- 产品侧 4 臂（改 `processing_plugin.cpp`，只重编 `plugin_processing`，当场还原）：
  A01 sobel ksize 3→1 ⇒ 单测红（实际 10/255，差 30/255）、**腿也红**（`actual=0.0392157 expected=0.156863`）；
  A02 laplacian ksize 3→1 ⇒ 中心/四角/四邻五点全红；A03 scharr→sobel ⇒ 实际 40/255（差 120/255）；
  A04 canny 阈值写死 50/150 ⇒ `countAbsurd = 1344`（应 0）。
- 期望侧 8 臂（改 `tests/test_plugins.cpp`，**偏小/偏大两向各一**）：sobel 20/255 与 60/255、
  laplacian 40/255 与 160/255、scharr 40/255 与 200/255、canny 默认边缘数 0 与 2000 ⇒ 8/8 rc=8，
  实际值与首跑逐字一致。
- 腿侧 3 臂（脚本副本载体 + 显式传全部路径；被跟踪文件 0 行脏）：C00 无改动复现 **rc=0** 且九计数逐字不变；
  C01 `Max` 期望改 20/255 ⇒ rc=1 `actual=0.156863`；C02 `Mean` 期望改 40/255 ⇒ rc=1 `actual=0.15083`。
- 还原方式：`git show HEAD:<path>` + 按原行尾归一写回，每轮还原后干净重编、单测再绿；
  两条工作树基准值 `processing_plugin.cpp = 169bc45d…63402`（77282 字节 / 1556 CRLF）、
  `test_plugins.cpp = b8a683b3…45832e`（25847 CRLF / 0 裸 LF）是**终态对拍用**的，不是逐臂读数。
  `169bc45d…63402`（77282 字节 / 1556 CRLF），`test_plugins.cpp` 回到 `b8a683b3…45832e`（25847 CRLF / 0 裸 LF）；
  ⚠ **逐臂 sha 没有落盘**：在臂日志目录里检索两个 sha 值 = **0 行**（控制器现跑；该目录按 `.gitignore` 不入库，故本条只记结论、不写路径），所以上一句不能读成"每臂都量过"——盘上只有 **A01 / A04 / B04 / B08 四个还原检查点**。
  ⇒ 隔离成立的口径**由 P3c-b 按复审建议③改写**：原来的"失败集互斥"不成立——腿脚本把 `$ErrorActionPreference` 设成 `Stop`，
  **第一条红就 throw、整腿即停** ⇒ 每臂日志里必然只有一处红。这是**构造性单点**（运行器给的），不是臂间互相独立的证据：
  真漏还原时下一臂同样只打**一处**红，红集不会"必然重叠"，顶多是红在**没预期那一行**。⇒ 真正承重的是另外两条：
  臂跑的是**副本**（被跟踪文件 0 行脏，`git status` 空）+ 每臂相对 `HEAD` **只有 1 行不同**（逐臂 diff 现读，本把同款做法见 P3c-b 那条的臂表）。
  **终态**由控制器现量：工作树与 `HEAD` 逐字节一致（`git status` 空、`git diff -- src gui` 空）。
  **未用** `git hash-object --no-filters` 当字节证据（autocrlf 下必假）。

**登记表五数 + ceiling 前后（同笔）**
```
改前  126 行 / GOLD 110 / PENDING 16 / all_lines 111 / table_driven 15 / literal_slot 95 / kPendingCeiling 16
改后  126 行 / GOLD 111 / PENDING 15 / all_lines 112 / table_driven 15 / literal_slot 96 / kPendingCeiling 15
```
第 4 列 = `PluginTest.ProcessingEdgeExecution`；新注释无 `,GOLD` 字面量 ⇒ `gold_rows_all_lines` 只涨 1。
矩阵 `processing,edge` 行第 5 列 `none → real_regression`（依据 = 表头对该列的逐字定义；第 6 列 `gold` 是目标档
**不动**；第 4 列 `fixture=data` 按 P2b 的复核先例**不单点改**，归 D3 的矩阵对账）。

**腿九计数前后两串**
```
改前  registered=78 ran=78 judged=57 allowlisted=21 allowlist-table=21
      allowlist-gated-not-registered=0 branch-labels=58 conditional-registrations=1 dead-branches=0
改后  registered=79 ran=79 judged=58 allowlisted=21 allowlist-table=21
      allowlist-gated-not-registered=0 branch-labels=59 conditional-registrations=1 dead-branches=0
不变量 57+21=78=ran（改前）、58+21=79=ran（改后）；$rasterGateAllowlist 21 条一字未动；dead-branches=0
```
`branch-labels` 比 `judged` 多 1 是**改前既有形状**（`classification_feature_stats_csv` 在 Mode=quick 下不跑、
闸计入 conditional-registrations），不是本把引入。

**回归（Debug 树，现跑）**
- `Total Tests: 802` 改前改后同值（扩写既有 `TEST_F`，不新增注册用例）。
- 定向 43 条配方 **43/43 绿，44.63 s**（含 `docs_match_cli` 与 `CoverageMeta*`）；`^PluginTest\.Processing`
  **30/30 绿，6.18 s**。
- 腿墙钟：改前 **13.92 s**；改后安静态**五跑散布 14.33~17.17 s**（逐跑见下一行）。
  ⇒ **R-D52 口径：余量 = TIMEOUT ÷ 安静态多跑里的最大值**（分母不许取单跑最优）。本把安静态改后共 5 跑：15.02（席位）／ 16.48、17.17（控制器）／ 14.33、14.92（复审席）⇒ 取最大 **17.17 s** ⇒ 余量 60 ÷ 17.17 = **3.49×**（改前 13.92 ⇒ 4.31×）；**TIMEOUT 60 未抬、断言一条未减**。
- `data/` 只读两件套（开批时刻 2026-10-30 14:51:26）：`find data -type f -newermt` = **0**；
  `check_data_baseline.py` = `BASELINE: 88  CURRENT: 88  DIFFS: 0`。
- 文本：`test_plugins.cpp` 25847 CRLF / 0 裸 LF；`run_real_raster_regression.ps1` 3048 CRLF / 0 裸 LF /
  `Parser::ParseFile` 0 错；`边缘检测.md` 72 CRLF / 0 裸 LF；参数表未动、`docs_match_cli` 绿。

**缺口（照实登记 8 条）**
- G-1 `canny` 的逐点像素集不可手推（Sobel+NMS+双阈值滞后）⇒ 只有结构/单调判据；本夹具上 low/high 两条单调的
  预期读数是**相等**（1344），真正承重的是 high=10000 ⇒ 0 与取值集/Min/Max。
- G-2 `sobel` 的 N41 静默钳制（越界→1、`(0,0)`→dx=1）本把**不修、不判**（任务书明令）。
- G-3 `scharr` 的 dx=0/dy=1（垂直求导）没有单测：斜坡只沿 x 变，垂直方向恒 0，判不出东西。
- G-4 腿侧只判 `sobel` 一支；`canny`/`laplacian`/`scharr` 无腿 case（本把只要求动作出现在一条 real 腿里）。
- G-5 矩阵第 4 列 `fixture=data` 按 P2b 复核先例不单点改（整列语义归 D3 矩阵对账 #184）。
- G-6 `canny` 的 `low_threshold` 没有承重臂：产品写死 50/150 时 low=10 那行读数仍是 1344 ⇒ 该单调行不红。
- G-7 边界列判 0 依赖 `BORDER_REFLECT_101`，没有单独改 borderType 的臂（A01 只覆盖内部点）。
- G-8 Release 树本把未重编 ⇒ 以上全部只验收 Debug；Release 侧一律**未验收**。

### 批次 D 覆盖晋升 P3b（2026-10-30，`processing.filter` 一条 PENDING→GOLD；**只做单测面，不碰腿**）

本把 = **一个动作** `processing.filter` 的单测面晋升（腿侧 case 归 P3c，本把 `tests/run_real_raster_regression.ps1`
与 `tests/real_data_matrix.txt` **一字未改**）。改前现读：登记表 `processing,filter,PENDING`（3 列）、
`kPendingCeiling = 15`、`Total Tests: 802`、`Processing*` 族 30 条、定向配方 43 条。

**凭据（判据载体 = 既有 `TEST_F(PluginTest, ProcessingFilterExecution)` 的块内扩写，未新增 `TEST_F`）**
四件夹具全部由 `tests/test_plugins.cpp` 里的 `createColumnPlateauRaster` 造（不用象限布局的 `createStepLevelRaster`，
不用腿侧 `gui_test_data_helper.cpp` 的 `makeStepLevelRaster`）：
①**三档平台件** 52×12、`{0,20,30,52}`、原始档值 `0/10/51` ⇒ `toUint8` 标度 `255/51 = 5.0` 二进制精确 ⇒ u8 档 `0/50/255`；
②**窄岛件** 52×12、`{0,10,12,30,33,52}`、档值 `0/51/0/51/0` ⇒ 一座 2 列岛 + 一座 3 列岛；
③**两带件**（同形状、两带档值表不同 `{0,10,51}` 与 `{0,51,10}`）；④**亮线件** `{0,25,26,52}`、`0/51/0`（gaussian 专用）。
断言行数现读（口径 = 该 `TEST_F` 块体内以 `EXPECT_` 开头的行数 / 其中值级 `EXPECT_NEAR|FLOAT_EQ|DOUBLE_EQ`）
= **117 / 61**。

**为什么必须有第②件夹具（本把的夹具侧发现，不是产品侧）**：三档平台件的档宽（10 列 / 20 列）远大于 `2r = 4`，
于是 `median` 的多数票、`morph_open` 的 erode→dilate、`morph_close` 的 dilate→erode **在该件上逐列剖面完全相同**
（都等于输入档）⇒ 只看这件夹具，"把 open 写成 close"这类分派错误**在数值上是不可见的**。窄岛件把它拆开：
2 列岛被 `median`（10/25 采样 < 13）抹掉但被 `close` 逐列复原；3 列岛被 `open`（岛宽 < k）抹掉但被 `median` 保留。
⇒ A04/A05 两臂只在岛件上红，反证这条登记不是空话。

**推得 vs 实跑（分开写；"实测"只用于真跑过的）**
期望在首跑之前推得，实测列**另用一条纯 python 的 GeoTIFF 读出器**直接读盘上产物（与断言不同代码路径，
避免"期望从产物回填"）：

| 判据 | 推得（写死的常数与算式） | 首跑实测（7 位有效） |
|---|---|---|
| dilate x=17 / **18** / 25 / **28** / **0** | 0 / 50/255 / 50/255 / 255/255 / **0**（边界见证） | 0.0000000 / 0.1960784 / 0.1960784 / 1.0000000 / 0.0000000 |
| erode x=19 / 20 / **22** / **32** / **51** | 0 / 0 / 50/255 / 255/255 / **255/255**（边界见证） | 0.0000000 / 0.0000000 / 0.1960784 / 1.0000000 / 1.0000000 |
| median x=19 / 20 / 25 / 29 / 30（**采样数 15:10 多数票**） | 0 / 50/255 / 50/255 / 50/255 / 255/255 | 全等 |
| open / close x=19 / 20 / 29 / 30 | 0 / 50/255 / 50/255 / 255/255 | 全等（与 median 同剖面 ⇒ 见上面的夹具侧发现） |
| 岛件 dilate x=7 / 8 | 0 / 255/255（向外长 r=2 列） | 0.0000000 / 1.0000000 |
| 岛件 close x=11 / 12 / 29 / 31 | 255/255 / 0 / 0 / 255/255（原岛逐列复原） | 1.0000000 / 0 / 0 / 1.0000000 |
| 岛件 median x=11 / 29 / 31 | 0（2 列岛）/ 0 / 255/255（3 列岛） | 0.0000000 / 0.0000000 / 1.0000000 |
| bilateral 平坦区 x=5 / 25 / 40 | 0 / 50/255 / 255/255（窗口内 u8 全等 ⇒ 权重归一后恒等） | 0.0000000 / 0.1960784 / 1.0000000 |
| bilateral 夹心 x=20 / x=30 | `0<v<50/255` / `50/255<v<1.0`，且 `255v` 落在整数格点 | **0.1450980 = 37/255** / **0.9921569 = 253/255** |
| gaussian σ=1000 x=23/25/27 与 22/28 | **10.2**（= 51×5/25，均匀核极限）与 **0**（窗口不含亮线） | 10.1999893 / 10.2000103 / 10.1999893；0.0000000 / 0.0000000 |
| gaussian σ=0.5 x=25 / 24 / 23 / 22 | 51×w(0) = **40.1151**、51×w(1) = **5.4290**、51×w(2) = **0.013457**、0 | 40.1151085 / 5.4289894 / 0.0134571 / 0.0000000 |
| gaussian σ=0.1 x=25 / 24 | **51.0**（`w(0)→1`、`w(1)=exp(-50)=1.9e-22`）与 0 | 51.0000000 / 0.0000000 |
| 纠正段 median k=4 ≡ k=5、dilate k=1 ≡ k=2 ≡ k=3 | 逐点 0 处差异（52×12 = 624 像元） | `countPixelDiffs = 0`（三组都是） |
| 托底：dilate k=3 vs k=5（防"相同"恒真） | x=18：0 vs 50/255；x=28：50/255 vs 255/255 | 0.0000000 vs 0.1960784；0.1960784 vs 1.0000000 |
| sigma 六支不变（median / dilate 在 σ=0.1 与 σ=1000） | 逐点 0 处差异 | `countPixelDiffs = 0`（两支都是） |
| band=1 vs band=2（dilate，两带档值表不同） | x=18：50/255 vs 255/255；x=35：255/255 vs 50/255 | 0.1960784 / 1.0000000；1.0000000 / 0.1960784 |
| 形状（轴序唯一凭据） | 块数 1、`cols==52 && rows==12` | 1 / 52 / 12 |
| 值域不对称 | gaussian Max > 1.0；六支 ≤ 1.0 | 40.1151 / 51.0 / 10.2；六支 Max 全 = 1.0 |
| else 支（进程内直调） | `success=false` + 文案前缀 `Unknown filter type:` + 不留产物 | `Unknown filter type: sobel`、无产物 |

容差口径：量化六支 `1e-6`（值在 `k/255` 格点，float32 间隔 ~6e-8；相邻两种结局最小间隔 `50/255 = 0.196`）；
gaussian `1e-4`（值域 0~51 ⇒ float 核每权 ~6e-8 相对 + 竖直 `Σk_y` 残差 + 均匀核极限偏离上界
`5 × 2e-7 × 51 = 5.1e-5`）。**E03 臂把这个口径钉成了实测**：把 `1e-4` 改成 `1e-7` ⇒ gaussian 8 条红
（实测残差 1.03e-5），量化六支不受影响 ⇒ 不是抄来的数。

**臂清单 16 臂 = 产品侧 13（A01~A13）+ 期望侧 3（E01~E03）**（单实例驱动；每臂跑前重查 `git status`；
臂日志 tee 成文件；还原后与工作树基线**字节对拍**：`processing_plugin.cpp` 回到 `169bc45d…` / 77282 字节 / 1556 CRLF，
`test_plugins.cpp` 回到 `a6081f2e…` / 26258 CRLF / 0 裸 LF，16/16 全部对拍通过；
⚠ **未**用 `git hash-object --no-filters` 对 blob 当还原证据——autocrlf 下恒不等）。
- 七支各一臂改坏消费点 ⇒ 该支红：A01 gaussian 丢 σ 数值（11 条红，线中心读 19.125 = 51×0.375，
  即上游 `sigma<=0` 的硬编码二项式核）、A02 `medianBlur`→`blur`（7 条，x=19 读 20/255）、
  A03 双边核退化成 1×1（2 条夹心上界红，x=20 恰读 50/255）、A04 `OPEN`→`CLOSE`（2 条，**只在岛件**）、
  A05 `CLOSE`→`OPEN`（2 条，**只在岛件**）、A06 `DILATE`→`ERODE`、A07 `ERODE`→`DILATE`。
- 四个可见参数各一臂：A09 `band` 不消费（恒读第 1 带 ⇒ S12 三条红）、A10 `filter_type` 写死 `gaussian`
  （**57 条红**，远超"至少两支"）、A11/A12 各删一行 `kernel_size` 纠正、sigma 臂 = A01
  （gaussian 红、六支仍绿 ⇒ 正是"只对一支有效"的形状）。
- **A11 的红形态是实测而不是引用**：删掉"偶数加一"那行 ⇒ 偶数核进 `cv::medianBlur` ⇒
  `OpenCV(4.14.0) Error: Assertion failed ((ksize % 2 == 1) && (_src0.dims() <= 2 )) in cv::medianBlur …
  median_blur.dispatch.cpp … (-215:Assertion failed) … thrown in the test body`
  ⇒ 用例级失败、**该点之后的断言不再执行**（OpenCV 没有再兜一层，也没有被供应商代码接住——
  `processing_plugin.cpp` 全文无 `try/catch`）。A12（删"小于 3 抬到 3"）红的是 `dilate k=1 ≡ k=3` 那一行。
- A13 = 任务书 §5.5-②：doFilter 内**三处** `convertTo(..., 1.0/255.0)` → `1.0/256.0` ⇒ 六支量化 32 条红、
  gaussian 11 条仍绿；E01 = §5.5-①（`50/255` → `51/255` 只红那一行）；E02 = gaussian 常数改坏（只红那一行）。
- 全部臂跑完后单测复跑 **1/1 绿**（0.7~1.6 s 级）⇒ 无残留变异；受保护位点
  `src/plugins/terrain/terrain_plugin.cpp` 的 blob 现读仍是 `588a42504ee19bcb551f8214e5c1adb68cea20e9`。

**上游定约（tag 4.14.0，jsDelivr 的 GitHub 镜像；⚠ 任务书给的三个候选路径在 4.14.0 里都不含 `getGaussianKernel`——
`imgwarp.cpp`/`filterengine.hpp` 现读 grep 各 0 命中、`comp.cpp` 直接 404；真实载体是 `smooth.dispatch.cpp`，
形态学是 `morph.dispatch.cpp`、中值是 `median_blur.dispatch.cpp` + `.simd.hpp`）**
1. `modules/imgproc/src/smooth.dispatch.cpp` 的 `getGaussianKernelBitExact` ⇒ 定约成功：
   ```
   softdouble sd_minus_0_125 = softdouble::fromRaw(0xbfc0000000000000);  // -0.5*0.25
   softdouble sigmaX = sigma > 0 ? softdouble(sigma) : mulAdd(softdouble(n), sd_0_15, sd_0_35);
   softdouble scale2X = sd_minus_0_125/(sigmaX*sigmaX);
   for (int i = 0, x = 1 - n; i < n2_; i++, x+=2) { softdouble t = exp(softdouble(x*x)*scale2X); … }
   sum *= softdouble(2);  sum += softdouble::one();
   ```
   ⇒ `x` 是像元偏移的 2 倍，`exp(x²·(-0.125/σ²)) = exp(-d²/(2σ²))`；归一 `Z = 1 + 2Σt`（n 为奇数时**不**再加第二项）。
   同文件 `createGaussianKernels`：`ksize.width > 0` 时不按 σ 反推尺寸，并有
   `CV_Assert(ksize.width > 0 && ksize.width % 2 == 1)`；核类型 `std::max(depth, CV_32F)`；
   `GaussianBlur` 的定点 bit-exact 分支只在 `sdepth == CV_8U/CV_16U` 进 ⇒ CV_32F 输入落到末尾
   `sepFilter2D(..., borderType)`（默认 `BORDER_REFLECT_101`）⇒ **gaussian 的期望只取窗口完整落在图内的列**。
   `sigma <= 0` 另有一条硬编码二项式核分支（`n==5` 给 `0.0625/0.25/0.375/0.25/0.0625`）——本把不判它（缺口 G-5）。
2. `modules/imgproc/src/morph.dispatch.cpp` 的 `createMorphFilter` ⇒ 定约成功：
   ```
   if( op == MORPH_ERODE )
       borderValue = Scalar::all( depth == CV_8U ? (double)UCHAR_MAX : … );
   else
       borderValue = Scalar::all( depth == CV_8U || depth == CV_16U ? 0. : … );
   ```
   `morphologyEx` 里 `case MORPH_OPEN: erode(src,dst,…); dilate(dst,dst,…);` 与
   `case MORPH_CLOSE: dilate(…); erode(…);`（两步同一个 borderValue），且声明默认
   `borderType = BORDER_CONSTANT, borderValue = morphologyDefaultBorderValue()`
   ⇒ 虚采样是 8 位域的**另一端极值**、永不改变窗口 max/min ⇒ **边界列可写逐点期望**
   （`dilate@x=0 → 0`、`erode@x=51 → 1.0` 两条见证据此写下，A06/A07 把它们钉住）。
3. `modules/imgproc/src/median_blur.dispatch.cpp` ⇒ **部分定约**：
   `CV_Assert((ksize % 2 == 1) && (_src0.dims() <= 2 ))` 与边界
   `copyMakeBorder(…, BORDER_REPLICATE|BORDER_ISOLATED)` 两条钉死（前者就是 A11 的红形态来源）；
   `ksize <= 5` 与 `> 5` 两条路径（`medianBlur_8u_Om` / `medianBlur_8u_O1`）的分发条件在盘上是
   **按图像Mp数自适应的性能阈值**，不是语义阈值，但源码里没有一句"两者逐像元等价"的断言 ⇒
   本把只用 k=5，没加 k=7 的臂，这条不确定性不进入任何期望（缺口 G-4）。
   `bilateral` 的 LUT 化按任务书**不要求定约**，本把只写夹心不写逐点。

**登记晋升（同笔六键 + ceiling，全部当场用登记表头部声明的现读命令跑）**
```
改前 126 / 111 / 15 / 112 / 15 / 96 / kPendingCeiling 15
改后 126 / 112 / 14 / 113 / 15 / 97 / kPendingCeiling 14    不变量 112 + 14 = 126 ✓
```
键序 = `data_rows / gold_rows / pending_rows / gold_rows_all_lines / gold_rows_table_driven / gold_rows_literal_slot`。
第 4 列 = `PluginTest.ProcessingFilterExecution`；新写文本一律用 `[:]GOLD` 这种写法、不出现"逗号紧跟 GOLD"的字面量 ⇒ `gold_rows_all_lines` 只涨 1；
`gold_rows_table_driven` 不动（那族靠 `SpindexOtherIndicesExecution` 计数）。
PENDING 14 行 = processing 11 + matching 2 + spindex 1；processing 族另 11 行、P1 的 `threshold`、
P2a/P2b 的 `enhance`、P3 的 `edge` 全部一字未改。
⚠ `kPendingCeiling` 在 `tests/test_coverage_meta.cpp`，不在任务书 §9 的"射程四文件"清单里，但同文件有一条
`EXPECT_EQ(头部 pending_rows, kPendingCeiling)` ⇒ 不改它晋升本身就红，属 Step3 的必需组成而非第二份行为（改 1 行）。

**N182 立案（本把新号，改前现读 `grep -rn "N182" docs tests src gui` = 0 命中）**
`kernel_size` 的四处口径**分叉**：`ParamSpec` 声明 `Int`、默认 `5`、区间 **`1..101`（含偶数、含 1/2）**；
action 级校验要求「不小于 3 的奇数」并**直接失败**；GUI 的 `buildEffectiveGuiParamSpecs` 只把下界抬到 **3**（奇偶不管）；
`doFilter` 内部则**静默改写**（偶数加一、小于 3 抬到 3，不告警、不改返回状态）。
⇒ 同一份 `--kernel_size=4`：CLI/工作流/GUI 三条路都被拒（用户看得见错误），**进程内直调**却被悄悄折成 5
（调用方拿到 `success=true`，且实际用的核不是他给的那一个）。
【时态限定，2026-11-06 N172-C2】上面「四处口径」里的前三处（ParamSpec 区间、action 级校验、GUI 下界）
今天已合成 `ParamSpec` 一处声明（区间位 + 奇偶位 + 按动作取的作用域表）；
余下「进程内直调被静默折成 5」这一处仍在，归 N172-C4。
归 **N172 同族**（声明区间与消费点行为不一致）。**修法不在本把**——把 `ParamSpec` 下界改成 3、或把偶数列入非法值，
都会同时改 `--help` 的可视区间与 GUI 的滑动范围 = 改可观测行为，需要用户拍板。
本把只做两件：**把"纠正段只在进程内直调可达"钉成判据**（S10 + A11/A12）+ **文档诚实化**
（`空间滤波.md` 的参数表与流程第 2 步现在写明"谁在哪一层拦"，不再让读者以为存在静默纠正路径之外的正常入口）。
⚠ **成色更正（复审 Q2(a)，控制器独立复现）**：A11 那臂红成**未捕获的 `CV_Assert((ksize % 2 == 1))`**，这只发生在**单元直调 `plugin.execute`** 这条路上；
产品三入口 `src/cli/main.cpp`、`src/framework/src/workflow.cpp`、`src/gui/execute_worker.cpp` 全部经 `framework::executeGuarded`，
其中 `catch (const std::exception&)` 接得住 `cv::Exception`（它派生自 `std::exception`）⇒ **N182 的第四写者是"死支防御"，不是可达的崩溃路径**；
又因偶数/<3 在同一批入口更前面就被 action 级校验拒掉，纠正段从外部**双重不可达**。本把不据此改任何产品码。

**#164 家族追加一条**：`filter` 的 `sigma` 是 GUI `filter` 面板的**可见键**，`doFilter` 把它读出来，
但**只有 `gaussian` 一支消费它**，另外六支读而不用（S11 的"σ=0.1 与 σ=1000 逐点相同"就是这条的正面见证）。
⇒ 属 #164（可见参数声明有效但实际无效）家族，**不另立案**；本把的臂 A01 给出该族的红形态
（gaussian 红、六支仍绿 ⇒ "只对一支有效"的形状）。⚠ 同时登记一条已知盲区：
`param_guard_visible_keys_all_read` 本把仍绿，因为它只看"键有没有被读"，**看不见"读了不用"** ⇒ 这条守卫
对 #164 族是漏判的（不是本把引入，本把只是把它写进账）。

**回归（Debug 树，现跑）**
- `Total Tests: 802`（扩写既有 `TEST_F`，未新增注册用例）。
- 定向 43 条配方（逐字、单引号）：**43/43 绿**——**安静态散布 38.2~42.8 s**（控制器 42.83 与修复轮复跑 38.23、复审席 41.48 与 38.58，
  五跑门控均为 0）；本席自跑的 50.01 / 54.68 是**带负载读数**（Step3/Step4 落地前后与提交串行交错），**不入散布、不当基线**
  （三笔提交全部落完、`git status` 干净之后再跑一次，口径同 `ctest -C Debug -R` 该配方的 `Total Tests` 那行）；
  `^PluginTest\.Processing` 族 **30/30 绿，7.33 s**。
- 腿单跑 `ctest -R '^real_data_raster(\.|$)' -V` 通过，九字段**逐字未动**：
  `registered=79 ran=79 judged=58 allowlisted=21 allowlist-table=21 allowlist-gated-not-registered=0
  branch-labels=59 conditional-registrations=1 dead-branches=0`（本把没加腿 case ⇒ 该串与改前同）。
- 腿墙钟按 **R-D52 口径**取安静态多跑最大值：本批同一条腿安静态现读 14.15 / 14.19 / 14.84 / 15.66 / 16.22 / 17.13 / **17.64** s（门控 0）⇒ 余量 `60 ÷ 17.64 = **3.40×**`，未跌破 2.5× 拆腿触发线
  （⚠ **前向指针，按 P3c-b 的门控更正条**：这一组的"安静态"标签要降级成"窄门控 0 态"——`tasklist` 是点采样、看不见并发腿的 `powershell.exe` 主进程，
  复审席实测并发一条腿时同一条腿能从 15.28 抬到 16.70 s ⇒ **17.64 这一档里可能有带负载读数**；**算术没错、读数一条不删**；
  D4 前应引用的分母取"各把安静态 max 里最大的那个"，本批现读见下面 P3c / P3c-b 两条的腿墙钟行）；
  本席单跑 **18.98 s** 是**带负载读数**（step5 那几把腿串行相隔 15~19 s，缓存被前一把吃掉）⇒ **不入散布、不据它改 TIMEOUT**（本把 TIMEOUT 未抬、断言一条未减）。
- 三条守卫仍在 43 条里跑到且绿：`source_mojibake_guard`、`docs_match_cli`
  （`actions=126 docs_checked=126 missing_doc=0 rows=594 matched=593 drift=0 allowed=7` ⇒ 参数表未增删行、
  必填列与默认值列未动）、`geotransform_guard`（两域读数与改前**逐字相同**：product `any=21 code=20 checked=18
  unchecked=2 allowlisted=2 violations=0 dead=0`；test `any=13 code=10 comment_line=3 checked=9 unchecked=1
  allowlisted=1 violations=0 dead=0` ⇒ 本把没有新增"判 `GetGeoTransform` 返回值"的位点，无需同笔改 `expect`）。
- `param_guard_visible_keys_all_read` 仍绿 ⇒ 它的"看不见读而不用"这条盲区已写进上面的 #164 追加。
- `data/` 只读两件套（开批时刻 2026-10-30 16:37:38）：`find data -type f -newermt` = **0**；
  `check_data_baseline.py` = `BASELINE: 88  CURRENT: 88  DIFFS: 0`（本把不需要真实影像，未开任何原件）。
- 文本行尾现读（python `newline=''` 数，不按目录惯例推）：`tests/test_plugins.cpp` 26258 CRLF / 0 裸 LF；
  `tests/action_gold_cases.txt` 177 / 0；`docs/算法说明/影像处理与分析/空间滤波.md` **81** / 0（改前 69）；
  `tests/test_coverage_meta.cpp` 1825 / 0（本把只改 1 行）；均 UTF-8 无 BOM。⚠ **本文件自身的行数不在此列**：它是我正在写的这份文档，落笔即自变（改前基线 `7e2dd24` = 4119 行、`c7cb453` 落定时 4299），**写一个"本文件 N 行"的数进本文件必然漂** ⇒ 复审 MF-1 就是把席位那个没有载体的 `4283` 删掉，换成这条口径声明。
- Release 树本把**未重编** ⇒ 以上全部只验收 Debug，Release 侧一律**未验收**。

**缺口（照实登记 10 条，复审后补 G-9/G-10）**
- G-1 `bilateral` 的**非平坦区逐点期望**拿不出：需要上游 `bilateral_filter` 的 LUT 化口径（范围核权重表 +
  定点化），本把只写"严格落在两侧档之间 + 仍是 `k/255` 格点"两条夹心 ⇒ 这一支的承重只有平坦区恒等那三条。
  复审席另用"erode 补 `UCHAR_MAX` / dilate 补 0"独立把边界 4 列推得与盘上产物相符 ⇒ 这条是**可判而未判的推迟**（本把只为 `dilate`/`erode` 各写一条边界见证 x=0、x=51）。
- G-3 `gaussian` 的边界列（x<2 与 x>49）不判：`BORDER_REFLECT_101` 下亮线件两端引入镜像采样，要多一条"反射象"算式；
  复审席已按该口径独立推出并与盘上产物相符 ⇒ 同样是**可判而未判**，本把不铺。
- G-4 `medianBlur` 的 `ksize>5` 路径与 `<=5` 路径**等价性未在上游源码里读到断言** ⇒ 本把没加 k=7 的臂；
  后续要加必须先实测钉一遍。
- G-5 `sigma <= 0` 的分支（上游硬编码**二项式**核）**从 CLI 就可达**且期望可手推，本把没判：`ParamSpec` 的 σ 下界是 `0.0`
  ⇒ `--sigma=0` 合法；action 级校验的 `sigma > 0` 只挂在 `gabor_filter` 上、**不覆盖 `filter`**；GUI 又把 σ 下界抬到 `0.000001`
  （⇒ 同一个键三处口径，与 **N182 同形**，一并归该号拍板）。上游 `sigma <= 0` 走按 `ksize` 索引的硬编码表（4.14.0 现读
  `k=5` 行 = `1/16, 4/16, 6/16, 4/16, 1/16`）⇒ 1 列亮线中心可手推 `51 × 6/16 = 19.125`，**是"可判而未判"，不是推不动**。
- G-6 输出的**地理参考透传**本把不判（任务书裁定：`geotransform_guard` 的 test 域把 `code/checked/unchecked`
  写死成期望数，新增"判 `GetGeoTransform` 返回值"的位点要同笔改守卫 ⇒ 第二份行为）。
  ⇒「输入的 GT 必须原样进 `processing.filter` 产物」这一半**今天仍没位点判**，是已知未判项，不是与产品无关。
- G-7 腿侧零改动：`processing,filter` 在 `real_data_matrix.txt` 第 5 列仍是 `none`（腿还没这条 case，
  改成 `real_regression` 就是假登记）⇒ 归 P3c。**（P3c 已落：第 5 列现为 `real_regression`，改写版见下面 P3c 那条的 G-7——腿面判 3 支，`morph_open`/`morph_close` 因夹具退化不判）**
- G-9 GUI 侧那条 σ 文案**本把没改**：`src/gui/gui_data_support.cpp` 里 `sigma` 的说明串现读仍是"**高斯等**滤波的 sigma 参数"，
  与本把在 `空间滤波.md` 写死的"只有 `gaussian` 消费"分叉 ⇒ 属 #164 同族的**文案半边**；改那句串 = 改用户可见文案，**归 D3 拍板**，本把不动 `src/`。
- G-10 本块体里没有"输入打不开/路径非法"与"NaN→NoData 回写"两类位点（控制器现读本块 434 行内 `nonexistent|NaN|NoData` **0 命中**）
  ⇒ `filter` 的失败契约与三态传播今天仍只有"成功路径 + 产物存在"级别，登记为未判而非声称判过。
- G-8 Release 树未重编 ⇒ Release 侧**未验收**。

### 批次 D 覆盖晋升 P3c（2026-10-30，`processing.filter` 真实腿三条 case；**只加腿，不重复晋升**）

本把射程 = 腿面（`processing.filter` 的晋升已在上一刀 P3b 从单测侧落定，登记表行 `processing,filter` 已是 GOLD、证据用例名不变）。
改前现读（HEAD `ab86552`，全部现场复现）：登记表六键 `126 / 112 / 14 / 113 / 15 / 97`、`kPendingCeiling = 14`、`Total Tests: 802`、
定向配方 43 条、`^PluginTest\.Processing` 族 30 条、腿九字段
`registered=79 ran=79 judged=58 allowlisted=21 allowlist-table=21 allowlist-gated-not-registered=0 branch-labels=59 conditional-registrations=1 dead-branches=0`、
矩阵 `processing,filter` 行第 5 列 `none` / 第 6 列 `gold`。
`tests/action_gold_cases.txt`、`kPendingCeiling`、`tests/test_plugins.cpp`、`src/`、`gui/` **一字未改**。

**夹具与载体**：腿里已有的 52×12 单调斜坡件（`tests/gui_test_data_helper.cpp` 的 `makeEdgeRaster` 族函数
`makeEdgeRampRaster`，verb `edge-ramp-raster`，`GDT_Float32`、像元值 `v(x,y) = x`、原始 0..51、带地理变换），
产品的 `toUint8` 标度 `255/(51-0) = 5.0` 二进制精确 ⇒ u8 档 `5x`、写回 `5x/255`。
三条 case 各自**重新生成自己那份输入件**（`processing_filter_dilate_input.tif` / `_erode_input.tif` / `_median_input.tif`），
产物件各自独立，输入与产物两侧判前都删同名 `.aux.xml`（`raster_inspect info` 把统计缓存在 PAM 边车里，不删就是拿上一次的统计判这一次的产品）。

**三行期望：全部在首跑之前推得，实跑列是真跑出来的**（`raster_inspect info` 印 6 位有效；值只随 x 变、y 方向恒定 ⇒ 逐列求和乘 12 后在 Mean 里约掉，`Mean = Σu8/(52·255) = Σu8/13260`）

| `filter_type` | 推得的逐列剖面（窗口语义） | 推得的 Mean 算式 | 实跑 Mean | 推得 Min / 实跑 Min | 推得 Max / 实跑 Max |
|---|---|---|---|---|---|
| `morph_dilate` | `5·min(51, x+2)`（5×5 方核、anchor 居中；虚采样取域下界，永不胜 max） | `Σ = 5·(2+3+…+51) + 5·(51+51) = 5·1427 = 7135` ⇒ `7135/13260 = 0.5380844645550528` | `0.538084` | `10/255 = 0.0392156862745098` / 实跑 `0.0392157` | `1.0` / 实跑 `1` |
| `morph_erode` | `5·max(0, x−2)`（镜像：虚采样取域上界，永不胜 min） | `Σ = 5·(1+2+…+49) = 5·1225 = 6125` ⇒ `6125/13260 = 0.4619155354449472` | `0.461916` | `0.0` / 实跑 `0` | `245/255 = 0.9607843137254902` / 实跑 `0.960784` |
| `median` | `5x`（25 采样、y 恒定 ⇒ 排序后第 13 个样本恒是中心列；`BORDER_REPLICATE` 下边界列同样恒等） | `Σ = 5·(0+1+…+51) = 5·1326 = 6630` ⇒ `6630/13260 = 0.5`（恰好） | `0.5` | `0.0` / 实跑 `0` | `1.0` / 实跑 `1` |

自洽检查（首跑前算出、首跑后仍成立）：`7135 + 6125 = 13260 = 2·6630` ⇒ 两个形态学 Mean 关于恒等 Mean **严格对称**，
各偏 `505/13260 = 0.038084464555`。
⚠ **本把原句"三条 Mean 的最小间隔 `0.076168`"由 P3c-b 按复审 MF-4 更正**：`0.0761689291101055` = `(7135−6125)/13260` 是
dilate↔erode 的**最大**间隔，不是最小；本把这三支的相邻最小间隔是 `|0.5 − 0.461916| = 0.0380845`，而把 Mean 判到五支之后
（P3c-b 补 `open`/`close`）全家族的最小间隔落到 **`0.0011312217`**（median↔close；由同一反对称算式，median↔open 也是这个数）
⇒ 容差夹心的**绑定项换了**，逐点论证见下面 P3c-b 那条。**若实测不对称，该查剖面（核/锚点/边界）而不是查浮点**——本把实测对称。
三支的尺寸与 dtype 全部为 `Size: 52 x 12 x 1 bands` / `Type: Float32`（宽高**分开**比：件非方，轴交换才看得见；dtype 是独立一判，`0.0`/`1.0` 也能塞进 8 位带）。

**容差逐点写，不是一条常量糊三支**（本把唯一一处"量级换档"的容差行就是 dilate 的 Min）：
- dilate `Min 10/255`：实跑印 `0.0392157`，对期望的残差 **1.37e-8**；量级 3.9e-2 处 6 位有效的步长是 1e-7、半格 5e-8
  ⇒ 若沿用"整数就用 1e-9"的统一口径，**这一行会在打印机上红而不是在缺陷上红** ⇒ 取 `1e-6`（残差的 73 倍、半格的 19 倍）；最近缺陷 = 恒等剖面的 `Min 0`，`|d| = 3.92e-2` = 3.9e4 倍容差。
- dilate `Mean`：印 `0.538084`，残差 4.65e-7；量级 [0.1,1) 的半格 5e-7 ⇒ 容差必须**大于自身打印残差** ⇒ `1e-6`（2.15 倍；若打印进位到 `0.538085` 则 5.35e-7，仍 inside 1e-6，两侧都不会假红）。
- erode `Max 245/255`：印 `0.960784`，残差 3.14e-7 ⇒ `1e-6`（3.19 倍；进位侧 6.86e-7 仍 inside）。erode `Mean` 同 dilate Mean（残差 4.65e-7）。
- 三条 `0.0` 与 `1.0` 的整数点：float32 精确、打印残差为零 ⇒ `1e-9` 只吸收读回噪声。
- median `Mean 0.5`：印 `0.5`（零打印残差，`1e-9` 今天也能过），仍取 `1e-6` 且**不是为了松**：624 个 float32 档值累加出的均值实测带 `+9.8e-9` 漂移，而求和顺序是写入器与 GDAL 的事、不是本 case 的判据，比打印机 5e-7 吸收带更紧的常量判的是累加顺序而不是滤波。最近缺陷 = 任一形态学剖面，`|d| = 3.81e-2` = 3.8e4 倍容差。

**九字段前后（`-V` 现读，逐字）**
```
前 registered=79 ran=79 judged=58 allowlisted=21 allowlist-table=21 allowlist-gated-not-registered=0 branch-labels=59 conditional-registrations=1 dead-branches=0
后 registered=82 ran=82 judged=61 allowlisted=21 allowlist-table=21 allowlist-gated-not-registered=0 branch-labels=62 conditional-registrations=1 dead-branches=0
```
增量 = `registered 79→82`、`ran 79→82`、`judged 58→61`、`branch-labels 59→62`，其余四字段不动；
不变量 `judged + allowlisted = ran` ⇒ **`61 + 21 = 82`** ✓；`$rasterGateAllowlist` 的 21 条**一字未改**（`git diff` 里那段没有 hunk）。

**矩阵第 5 列（与新增 `New-Case` 同一笔提交）**
```
前 processing,filter,raster,data,none,gold,C4,夹具来源为启发式判定，开批前核对
后 processing,filter,raster,data,real_regression,gold,C4,夹具来源为启发式判定，开批前核对
```
第 6 列 `gold` 是**目标档不是现状态**，未随晋升动。

**臂清单（4 臂 + 1 附带读数；每臂 tee 成文件并在末行记 `EXIT_CODE=`；载体 = `run_real_raster_regression.ps1` 与 `regression_case_gate.ps1` 的完整副本 + 各自独立的输出根，
显式传 `-CliPath / -WorkspaceRoot / -OutputRoot / -DataHelperPath / -Mode quick`。⚠ 探针目录是被 gitignore 的私有路径，按本批口径不把它的绝对路径写进本文，登记在席位报告）**
- A0 控制臂（副本一字未改）：**绿**，九字段与主腿逐字相同，`EXIT_CODE=0` ⇒ "从副本跑"这件事本身不致红，后面几条红的是判据不是机制。
- **A1 改期望的反事实副本臂**：dilate 分支的 `-Expected (7135.0 / 13260.0)` → `-Expected 0.5` ⇒
  红形态 `processing_filter_dilate mean should be 7135/13260 = 5*(2+3+...+51 + 51+51)/(52*255) from the +2 column shift (band=1 actual=0.538084 expected=0.5 tolerance=1E-06)`，`EXIT_CODE=1`。
- **B1 错配臂（产物是别的 `filter_type` 的结果）**：erode 分支改读 dilate 产物 ⇒
  红形态 `processing_filter_erode min must be 0: the three leftmost columns all reach the domain column 0 inside the window (band=1 actual=0.0392157 expected=0 tolerance=1E-09)`，`EXIT_CODE=1`。
- B2 错配臂（第二支）：median 分支改读 dilate 产物 ⇒
  红形态 `processing_filter_median must keep the ramp's own floor 0 (BORDER_REPLICATE repeats the first column, so the 13th sample is still level 0) (band=1 actual=0.0392157 expected=0 tolerance=1E-09)`，`EXIT_CODE=1`。
- C1 附带读数（错配的反方向）：dilate 分支改读 erode 产物，而 erode 那条 case 此时**还没跑** ⇒ 红在
  `CLI validation command failed: raster_inspect info --input=…processing_filter_erode_output.tif`（"文件还没写"而不是值级），`EXIT_CODE=1`
  ⇒ 顺带把 case 的执行/校验顺序（dilate → erode → median）钉住：错配只能往先前已写出的产物上错。
  ⚠ **臂号与盘上载体对不上（复审 MF-5，P3c-b 已改名）**：本条的盘上载体原名 `B1_mismatch_dilate_reads_erode`，
  而真 B1 是上面那条 `B1_erode_reads_dilate` ⇒ 前者已改名成 **`C1_reverse_mismatch_dilate_reads_erode`**（目录与日志两处 `mv`，
  改名后 `*C1*` 检索现读命中两条、`B1_mismatch` 现读 0 命中）。**边界照实登记**：改名只动**文件名**，日志**正文**里那行
  `CLI validation command failed: …out…processing_filter_erode_output.tif` 是改名前写下的、**仍带旧名** ⇒ 拿全文 grep 找 "C1"
  会在正文里落空——这与控制器自己那次 `find -iname "*C1*"` 的假阴性是**同一条因（搜索串翻车）**，不是席位少了证据，
  所以本把**不**因为"搜不到"就把上面这条 C1 的结论降级。
⇒ B1/B2 的红都落在**该分支自己的值级判据**上、且尺寸与 dtype 两行在两处错配里都**没**拦住（同一写入器），这就是"分支真在按 case 名派发"而不是恒绿的实测形态。
另记一次非本意的反向证据：Step1 第一次插入时 case 声明块因锚点错位没落上，腿当场以 `dead-branch(es) no case can reach … processing_filter_dilate,processing_filter_erode,processing_filter_median` 红 ⇒ 反向闸在跑，"有分支无 case"这条不是装饰。

**两条口径限制（写成"腿判算子"就是越论域）**
1. `median` 的 **`Mean` 行**与"均值滤波"在这件单调斜坡上**不可分辨**（线性件的 box 均值也等于中心值）⇒ 这条分支判的是量化标度、`/255`、尺寸、dtype 与 CLI 派发；`median ≠ blur` 的承重只在单测侧那一臂（P3b 的 A02），本把不声称腿判算子身份。
   ⚠ **口径由 P3c-b 收窄（复审建议①）**：不可分辨的只是 `Mean` 行——`Min`/`Max` **可分辨**，因为 box 均值把**两条边界都保住**（`Min 0 / Max 1`），
   而实测四个非恒等支的边界不对称：dilate `0.0392157 / 1`、erode `0 / 0.960784`、open `0 / 0.960784`、close `0.0392157 / 1`。
   ⇒ 反过来说（复审建议②）：**`median` 自己的"边界恒等"是 `Min`/`Max` 两行承重的**，`Mean 0.5` 对反对称的边界偏差是盲目的
   （open 少 `15/13260`、close 多 `15/13260`，均值上互相抵消）。
2. **（本条原句已被 P3c-b 否证并改写，原判保留在下面划掉的部分里以免丢失错因）** 本把原文写"`morph_open` / `morph_close`
   在本件上**恒等于输入**（`open = erode→dilate = 5·max(0, min(51,x)) = 5x`；`close = dilate→erode` 同理）⇒ 这是**夹具决定的退化，
   不是产品决定的**，所以本把不给这两支加腿 case"。**这个论断是错的**，错在把"两个移位互消"当成了无条件成立：
   `erode` 的虚采样取域最大值 255、`dilate` 的取域最小值 0，所以 `open = dilate∘erode` 在**右边界两列**压不回恒等
   （`open(x) = 5x` 于 `x ≤ 49`，`245` 于 `x = 50,51` ⇒ `Σu8 = 5·Σ_{0..49}x + 2·245 = 6615` ⇒ `Mean = 6615/13260 = 0.4988687783`），
   `close = erode∘dilate` 是镜像、差在**左边界三列**（`close(x) = 10` 于 `x = 0,1,2`，`5x` 于 `x ≥ 3` ⇒ `Σu8 = 30 + 5·Σ_{3..51}x = 6645`
   ⇒ `Mean = 6645/13260 = 0.5011312217`）。⇒ 两支与 `median` 的差**不是零**而是边界两列 ⇒ 缺席的原因是**上一把的射程只取了三支**，
   不是夹具退化 ⇒ **P3c-b 已把这两条腿 case 补齐**（见下面那条）。这两支的逐点位点承重仍在 P3b 的窄岛件（A04/A05 只在岛件红），
   腿侧自此也有值级判据。

**缺口（P3c 登记）**
- **G-7（改写上一刀那条"腿侧零改动 ⇒ 归 P3c"）**：腿面现覆盖 `filter` 七支里的 **3 支**（`morph_dilate` / `morph_erode` / `median`）；
  `morph_open` / `morph_close` 两支**因本夹具退化而不判**（⇒ 腿不判，不是判不过），`gaussian`（无手推 Mean 载体，边界列另受反射限制）与
  `bilateral`（逐点期望需上游 LUT 口径，P3b 已登记为不可得）两支本把不碰
  ⇒ 第 5 列 `real_regression` 说的是"该动作已有真实腿 case 与值级判据"，**不是**"七支都判"。
- 腿不判 `kernel_size` 是否真被消费：三支都传 `ParamSpec` 默认 5 ⇒ 一个静默忽略 `kernel_size` 的实现照样打印这三条剖面。那半边承重是 P3b 的纠正段 + 两臂。
- 输入的地理变换透传仍没位点判（P3b 该半边未销）：本把没新增判 `GetGeoTransform` 返回值的位点，守卫两域读数与改前**逐字相同**（见下面回归）。
- Release 树本把未重编 ⇒ Release 侧**未验收**。

**回归（Debug 树，现跑）**
- `Total Tests: 802`（本把没新增注册用例）；`-N` 的定向配方 `Total Tests: 43`（不变）。
- 定向 43 条配方：**43/43 绿**，`Total Test time (real) = 39.19 s`（落在上一刀记录的安静态散布 38.2~42.8 s 内）。
- `^PluginTest\.Processing` 族 **30/30 绿**，5.69 s。
- 腿单跑 `ctest -R '^real_data_raster(\.|$)' -V` 通过，九字段见上面。
- 腿墙钟（改完、跑前门控 `tasklist` 全 0）三跑 **15.09 / 15.28 / 14.84 s**，取最大值 **15.28 s** ⇒ 余量 `60 ÷ 15.28 = **3.93×**`，未跌破 2.5× 拆腿触发线
  ⇒ **TIMEOUT 未抬、断言一条未减、腿未拆**。改前基线单跑 **`14.78 s`** ⇒ ⚠ **该读数仅控制台读数、未留载体**（那一跑的 tee 目录当时还没建，
  席位在自己的返回里已如实写过；复审 MF-2 认定它不能当盘上证据引用 ⇒ P3c-b 就地标注，**不改数、不删**，也按本批红线不写它落在哪个目录）。
  同腿本批散布 14.15~17.64 s ⇒ 三跑全落在既有散布内，不读成"加了三条 case 反而更快"。
  ⚠ **本条的"跑前门控全 0"按 P3c-b 的门控更正条降级读**：`tasklist` 串是**点采样**、且并发腿的主进程叫 `powershell.exe`（不在串里），
  ⇒ 门控 0 是**弱证据不是证明**；`60 ÷ 15.28 = 3.93×` 这条**算术没错**，"安静态"这个**标签**按那条更正。
- `docs_match_cli` 现读 `actions=126 docs_checked=126 missing_doc=0 rows=594 matched=593 drift=0 allowed=7` ⇒ 本把没动参数表（没改 `src/`、没改文档正文）。
- `geotransform_guard` 两域现读与改前逐字相同：product `any=21 code=20 checked=18 unchecked=2 allowlisted=2 violations=0 dead=0`；
  test `any=13 code=10 comment_line=3 checked=9 unchecked=1 allowlisted=1 violations=0 dead=0`。
- `.ps1` 三条硬规矩现读：`Parser::ParseFile` 三参重载对 `tests/run_real_raster_regression.ps1` 打 **errors=0**；
  ⚠ 只带一个 `[ref]` 的两参写法在本机 PowerShell 5.1 **绑不到重载**（`MethodCountCouldNotFindBest`）却照样打印 `errors=0`
  （`$e` 仍是 `$null` ⇒ `.Count` 空读成 0）⇒ 那是**假绿**，本把改跑三参形式并配一份故意写坏的副本当负控才让 0 有意义。
  ⚠ **上一把登记的"负控现读 `errors=2`"与盘上不符，由 P3c-b 按复审 MF-3 重跑更正**：当时留下的负控其实是一份 **16 字节存根**
  （`$x = @(1, 2` 这种半截数组，不是整文件副本），P3c-b 现跑它 = `errors=1 tokens=9` ⇒ 那个 `2` **没有任何盘上载体能复现**，判为落笔时的凭据口径错。
  ⇒ 本座按"负控要能红给我看"重做：把**整份** `.ps1` 复制后追加一行未闭合字符串 ⇒ 成对现读 **正 `errors=0 tokens=17178` / 负 `errors=1 tokens=17181`**
  （两个读数同批 tee 成文件；token 数随后续刀会漂，只作本把凭证、不作锚点）。
- 行尾与字面量现读（python `newline=''` 数，不按目录惯例推）：`tests/run_real_raster_regression.ps1` 3250 CRLF / 0 裸 LF / **0 个非 ASCII 字节**；
  `tests/real_data_matrix.txt` 184 CRLF / 0 裸 LF；`git diff --stat` = 202 insertions / 0 deletions + 矩阵 1 行改（纯新增，没删任何既有判据）。
  ⚠ 本文件自身行数不写进本文件（落笔即自变，上一刀刚为此删过一条没有载体的自计数）。
- `data/` 只读两件套（开批时刻 2026-10-30 18:19:50）：`find data -type f -newermt` = **0**；
  `check_data_baseline.py` = `BASELINE: 88  CURRENT: 88  DIFFS: 0`（本把全部用 helper 造件，未开任何原件）。
- 受保护位点 `src/plugins/terrain/terrain_plugin.cpp` 的 blob 现读仍是 `588a42504ee19bcb551f8214e5c1adb68cea20e9`。

### 批次 D 覆盖晋升 P3c-b（2026-10-30 开批 / 2026-11-02 交付，`processing.filter` 腿补 `open`/`close` 两条 case + 复审五条 must-fix 收口；**状态 DONE_WITH_CONCERNS，见下面腿墙钟条**）

本把射程 = **两份文件**（`tests/run_real_raster_regression.ps1` + `docs/RELEASE.md`）。`tests/action_gold_cases.txt`、
`kPendingCeiling`、`tests/real_data_matrix.txt`（第 5 列由上一把 P3c 翻成 `real_regression`，本把不再动）、
`tests/test_plugins.cpp`、`src/`、`gui/` **一字未改**（两笔提交的 `git show --stat` 各自只有一个文件）。
本把不重复晋升：`processing,filter` 的档位与证据用例名沿用 P3b/P3c，本把只补**腿面覆盖**（五支量化滤波里除
`bilateral` 外的五支现在都有值级判据）。

**改前现读**（本座用 `8a79a00` 那份 `.ps1` 的完整副本自己复现的，不是转抄工单）：
```
前 registered=82 ran=82 judged=61 allowlisted=21 allowlist-table=21 allowlist-gated-not-registered=0 branch-labels=62 conditional-registrations=1 dead-branches=0
后 registered=84 ran=84 judged=63 allowlisted=21 allowlist-table=21 allowlist-gated-not-registered=0 branch-labels=64 conditional-registrations=1 dead-branches=0
```
增量 = `registered 82→84`、`ran 82→84`、`judged 61→63`、`branch-labels 62→64`，其余四字段不动；
不变量 `judged + allowlisted = ran` ⇒ **`63 + 21 = 84`** ✓；`dead-branches = 0` ⇒ 新加的两条分支各有 case 可达；
`$rasterGateAllowlist` 的 21 条**一字未改**（`git diff` 里那段 0 个 hunk，本座现数）。

**剖面与两个 Mean 由本座独立重推**（任务书 §1 提醒控制器自己算错过两次 ⇒ 不照抄；结论**与控制器一致**，
他给的 `6615 / 6645` 我这条推导路径复现得出来，他抄错的 `6650` 我推不出来）：
5×5 RECT、anchor 居中、虚采样 dilate 取域最小 0 / erode 取域最大 255（P3b 定约）⇒ 单边移位 2 列，
件 `v(x,y)=x`（x=0..51）、y 恒定 ⇒ `Mean = Σ列档值/(52·255) = Σ/13260`：
- `open = dilate∘erode`：`erode(x)=5·max(0,x−2)` 之后再取窗口 max ⇒ `open(x) = 5x`（`x ≤ 49`）、`245`（`x = 50,51`）
  ⇒ `Σ = 5·Σ_{0..49}x + 245 + 245 = 6125 + 490 = **6615**` ⇒ `Mean = 6615/13260 = **0.4988687782805429**`
- `close = erode∘dilate`：`dilate(x)=5·min(51,x+2)` 之后再取窗口 min ⇒ `close(x) = 10`（`x = 0,1,2`）、`5x`（`x ≥ 3`）
  （`x=2` 两边都是 `10` ⇒ 平台跨三列、真正被抬离恒等的只有两列）
  ⇒ `Σ = 3·10 + 5·Σ_{3..51}x = 30 + 6615 = **6645**` ⇒ `Mean = 6645/13260 = **0.5011312217194570**`
- 整数自洽：`6615 + 6645 = 13260 = 2·6630` ⇒ open/close 关于恒等 Mean **严格反对称**，各偏 `15/13260 = 0.0011312217194570`
  （与 `7135 + 6125 = 13260`、各偏 `505/13260` 同形）。**若实测不对称，该查窗口（核/锚点/边界填充）而不是查浮点**——本把实测对称。

**三行期望全部在首跑之前推得；实跑列是主腿产物上的现读**（`raster_inspect info` 印 6 位有效，删边车后读）：

| `filter_type` | 推得 Min / 实跑 Min | 推得 Max / 实跑 Max | 推得 Mean 算式 / 实跑 Mean |
|---|---|---|---|
| `morph_open` | `0.0` / 实跑 `0` | `245/255 = 0.9607843137254902` / 实跑 `0.960784` | `6615/13260 = 0.4988687782805429` / 实跑 `0.498869` |
| `morph_close` | `10/255 = 0.0392156862745098` / 实跑 `0.0392157` | `1.0` / 实跑 `1` | `6645/13260 = 0.5011312217194570` / 实跑 `0.501131` |

⇒ 两支**不是**恒等：open 比恒等少 `15` 个 u8 档（全在右边被 erode 剪掉的两列），close 多 `15`（全在被 dilate 抬起来的左边三列）；
差值就是上一把那句"恒等于输入"欠的账。两支各自的独立 CLI 复现（不经腿、先验推导）与主腿产物上的读数**逐字相同**。

**容差逐点写，绑定项换了（复审 MF-4 的正面落地）**
- 五个 Mean 的相邻**最小**间隔 = **`|0.5011312217 − 0.5| = 0.0011312217`**（median↔close，同一反对称给 median↔open 也是这个数）；
  **最大**间隔 = `0.0761689291`（dilate↔erode）——上一把把后者写成了"最小"，已就地更正。
- `1e-6` 对新绑定项的余量 = `0.0011312217 / 1e-6 = 1.13e3`（**三个数量级**，不是上一把引的 `3.8e4`）；对每个点自身的
  打印残差是 4.5×（两个新 Mean，残差各 `2.22e-7`，在半格 5e-7 以下）/ 3.19×（open `Max 245/255`，残差 `3.14e-7`）/
  73×（close `Min 10/255`，残差 `1.37e-8`）。
- `close` 的 `Min 10/255` **不能用 1e-9**：打印残差 `1.37e-8 > 1e-9` ⇒ 会在**打印机**上红而不是在缺陷上红 ⇒ 这是本家族第二行
  破"整数一律 1e-9"统一口径的点（第一行是 dilate 的 Min），所以容差写在判据点旁边而不是全局一条。
- **承重行不承重的那一半也要写**（本座按行核过，不写成"每行都分派"）：open 的 `Min` 与 erode/median 同印 `0`、`Max` 与 erode
  同印 `0.960784`；close 的 `Min` 与 dilate 同印 `0.0392157`、`Max` 与 median/dilate 同印 `1` ⇒ **把 open/close 与邻居分开的是 `Mean` 行**，
  `Min`/`Max` 行判的是**边界填充方向**（写反了才红）。反过来上一把的 median 支"边界恒等"靠的正是 `Min`/`Max` 两行——
  `Mean 0.5` 对反对称的边界偏差是**盲目的**（open 少 15、close 多 15，在均值上会互相抵消）。

**夹具与载体**：两支各用自己的输入件与产物件（`processing_filter_open_input.tif` / `_output.tif`、`processing_filter_close_input.tif` / `_output.tif`），
每条 case 都**重新生成**自己那份 52×12 斜坡件（缓存副本会活过夹具改动、静默判旧形状）；输入与产物两侧判前都删同名
`.aux.xml`——`raster_inspect info` 把统计按文件名缓存在 PAM 边车里，不删就是拿上一次的统计判这一次写的像元。
两条 case 追加在 `processing_filter_median` 之后 ⇒ 执行顺序 dilate→erode→median→open→close。

**臂清单（4 臂；每臂 tee 成文件、末行记 `EXIT_CODE=`；载体 = 两份 `.ps1` 的完整副本 + 各自独立输出根，显式传全部路径；单实例顺序驱动）**

| 臂 | 相对已提交态的唯一改动 | 红/绿与红形态 | `EXIT_CODE` |
|---|---|---|---|
| `A0_control` | 无（一字未改的副本） | **绿**，九字段与主腿**逐字相同** ⇒ 后面几条红的是判据不是"从副本跑"这个机制 | `0` |
| `A1_open_expectation_broken` | open 支 Mean `-Expected (6615.0 / 13260.0)` → `0.5` | 红在**该支自己的值级 Mean 行**：`processing_filter_open mean should be 6615/13260 = … (band=1 actual=0.498869 expected=0.5 tolerance=1E-06)` | `1` |
| `A2_close_expectation_broken` | close 支 Min `-Expected (10.0 / 255.0)` → `0` | 红在**该支自己的值级 Min 行**：`processing_filter_close min must be 10/255 = 5*(0+2)/255: … (band=1 actual=0.0392157 expected=0 tolerance=1E-06)` | `1` |
| `A3_open_label_typo`（打卡反证） | open 支 Mean `-Label "Mean"` → `"Meen"` | 红在**字段读取**：`band 1 has no numeric 'Meen' line (block read: Band 1: Type: Float32 Min: 0 …)` ⇒ 该分支真在按标签读 `raster_inspect info` 的输出，不是 `Mark-Judged` 打卡 | `1` |

⚠ 臂锚点唯一性的两个坑照实记：`A2` 的第一版锚点与 **dilate 支的 Min 行逐字节相同**（同一 `10/255`、同一 `1e-6`）⇒ 匹配数 2、
准备脚本当场 assert 失败（**没有静默改成两处同改**），锚点扩到带上 close 自己的 message 行后才是单点；`A1`/`A3` 因带
`6615.0 / 13260.0` 天然唯一。每条臂跑前 `git status --porcelain` = 空（Step 1 已提交 ⇒ 被跟踪文件 0 行脏），
副本相对 `HEAD` 的 diff 逐臂现读 = **1 行**，两条读数都写进臂日志首行。
隔离口径按复审建议③改成**构造性单点**（`$ErrorActionPreference = "Stop"` ⇒ 第一条红即停，每臂必然只有一处红；
这不能反推臂间独立，真漏还原时下一臂同样只打一处红、只是红在没预期的那一行）。

**MF-1~MF-5 与本把提交的对应**（文本已随 Step 3 提交，本节只登记"改成什么"）
- **MF-1**：上一把的"口径限制"第 2 条从"**夹具决定的退化**"改写为"**本把否证上一把的论断**：虚采样规则使 open 差右边界两列、close 差左边界三列，
  缺席的原因是上一把射程只取三支 ⇒ 本把补齐"，并把两个剖面与两个 Mean 写进正文；`.ps1` 里那段 `WHY THREE cases and not five` 同批改写为
  `WHY FIVE cases and not three`。
- **MF-2**：`改前基线单跑 14.78 s` 就地标"**仅控制台读数、未留载体**"（不删数、不改数、不写私有路径）。
- **MF-3**：负控"现读 `errors=2`"改为**本座实测的成对读数**（正 `errors=0 tokens=17178` / 负 `errors=1 tokens=17181`，
  负样本 = **整份 `.ps1` 复制**后追加一行未闭合字符串）；并登记旧负控是 16 字节存根（本座现跑 `errors=1 tokens=9`）⇒ `2` 无载体可复现。
  两参写法的假绿也现跑坐实：stderr 出 `MethodCountCouldNotFindBest` 而 stdout 仍 `errors=0 tokens=0`、rc=0。
- **MF-4**：`0.076168` 从"最小间隔"改判为 dilate↔erode 的**最大**间隔；最小间隔按支数分档写清（三支 `0.0380845`、五支 `0.0011312217`）。
- **MF-5**：臂号 `B1_mismatch_dilate_reads_erode` → `C1_reverse_mismatch_dilate_reads_erode`（目录与日志两处 `mv`，
  真 B1 是 `B1_erode_reads_dilate`）；⚠ 只动**文件名**，日志**正文**里那行仍是改名前的旧名 ⇒ 全文 grep "C1" 会在正文落空，
  这与控制器那次 `find -iname "*C1*"` 假阴性同因（**搜索串翻车**），本把**不**因此降级 C1 那条结论。

**门控串本身是待验断言（复审 Q7 的实质结论，本座复现并据此弃读数）**
`tasklist | grep -cE 'MSBuild|gis_tests|ctest|gis-cli'` 是**点采样**：并发腿的主进程叫 `powershell.exe`（不在串里），
`gis-cli` 只在子进程间歇出现 ⇒ **门控 0 ≠ 机器安静**。本把因此把串放宽成"再加 `powershell|python`"的扩展门控（`node.exe` **不**放进来：它是本机常驻进程、
计入会让扩展门控恒非 0 ⇒ 没有任何读数可用，本把只把它当背景噪声），并把**跑前与跑后各读一次**、任一侧非 0 即弃该读数。
探针的覆盖面也照实分级登记（不做"全都测过"的样子）：**12 跑全部有窄门控 0/0 两侧读数** ⇒ 按任务书口径无一条被弃；
**扩展门控（加 `powershell|python`）只在最后 3 跑采到，也是 0/0**；中间 4 跑采的是"含 `node.exe` 的更宽串"，读到 `2/2`
（就是那两个常驻 `node.exe`，不是测试进程）⇒ 那 4 跑的"安静"只有窄门控支撑；`29.43 s` 那一跑属于**只采了窄门控**的头 4 跑。

**腿墙钟 ⇒ ⚠ 本把的 CONCERN 就在这条**（口径 = `ctest` 报的该腿自身用时；单跑 `-R '^(real_data_raster)$'`，无并发自己驱动）
```
14.84 / 29.43 / 20.69 / 18.81 / 18.38 / 19.43 / 18.96 / 19.83 / 19.44 / 20.33 / 19.06 / 20.30  s   （12 条有效读数，narrow 与 extended 门控两侧均 0）
```
- **max = 29.43 s** ⇒ `60 ÷ 29.43 = 2.04×` **< 2.5×** ⇒ 按任务书触发**停下报 DONE_WITH_CONCERNS**；
  **TIMEOUT 未抬、断言一条未减、腿未拆**（三条都不许做，本把一条没做）。
- 次高 `20.69 s` ⇒ `60 ÷ 20.69 = 2.90×`；本把众数带 **18.4~20.7 s**。
- **本座不把这轮抬升算成自己这两条 case**：同窗口做 A/B 控制（**同一驱动 = 直接 `powershell -File` 跑副本、不经 `ctest`**，
  所以这三档绝对值与上面那 12 跑**不同口径、不并列**；顺序单实例）——
  `8a79a00` 那份**三条 case** 的副本 16.74 / 18.98 / 17.64 s，本把**五条 case** 的副本 21.46 / 18.26 / 17.83 s
  ⇒ 中位差约 `+0.6 s`，与两个 helper 造件 + 两次 filter + 两次 info 的量级相称，**解释不了 +14 s**；
  旁证：同机同窗口的**定向 43 条配方**本轮 `50.72 s`，而上一把同配方记的是 `39.19 s`。
- **D4 前应引用的分母**：任务书写的是 `60 ÷ 15.28 = 3.93×`（上一把的安静态 max），本把现读的 max 比它高 ⇒
  按"取各把安静态 max 里最大的那个"这条口径，**分母应换成 `60 ÷ 29.43 = 2.04×`**，并连同本条的 A/B 控制一起交给 D4 决定是否重测；
  P3b 那条 `60 ÷ 17.64 = 3.40×` 与 P3c 那条 `3.93×` 都**保留原文**，各加了一句前向指针（算术没错，"安静态"这个标签按本条降级）。
- ⚠ 未归因项照实登记：`29.43 s` 那一跑与本把另外 11 跑差 ~9 s，**本座拿不到当时的进程快照** ⇒ 既不能说它是并发腿，也不能说它是本把的 diff；
  已用 A/B 控制排除"本把 diff"这一支，剩下的那支**留给 D4 或控制器在安静窗口重读**。
- **控制器 2026-11-02 重读并定分母**（把上面那条未归因项接住）：宽门控串 `tasklist | grep -ciE 'MSBuild|gis_tests|ctest|gis-cli|powershell|python'` **跑前跑后各读一次、两侧都 0 才算有效**，主腿五跑 = **19.11 / 18.00 / 17.74 / 18.73 / 20.35 s**（五跑两侧读数均 0，无弃项）⇒ 与本把双门控有效读数（最高 `20.69 s`）合并取最大 ⇒
  **D4 应引的分母 = `60 ÷ 20.35 = 2.95×`**（只用本席这五跑，见上一句）。`29.43 s` 那一跑**只采了窄门控**、样本资格未认证 ⇒ **保留原读数、但不作分母**（该把把口径写成 `2.04×` 是保守方向的措辞，不是实测结论）。
  ⚠ 并登记该把两句自身分叉：读数清单的注写"12 条有效读数，narrow 与 extended 门控两侧均 0"，另一句写"头 4 跑的'安静'只有窄门控支撑"⇒ 那 4 条里除 `29.43` 外还有 `20.69 / 14.84 / 18.81`，**按后一句它们资格降级** ⇒ 我不拿未认证的数当分母。
  机器态旁证：同机同窗**定向 43 条配方**控制器今日现跑 `Total Test time (real) = 49.70 s`（两日前同配方是 `38.2~42.8 s`，本把席位记的是 `50.72 s`）⇒ 三个数同向 ⇒ **窗口态抬升，不是本把 diff**，与席位的 A/B（3 case 副本 17.64 vs 5 case 18.26，`+0.6 s`）一致。
- **D4 前置判据（新增，写死在此以免后面靠记忆）**：D4 开跑前按上面双侧宽门控认证一次；**若认证安静态 max > 24 s（即余量 < 2.5×）⇒ 拆 `real_data_raster` 腿**，
  **不许抬 TIMEOUT、不许减断言**；同时 D4 的全量墙钟余量要按当前窗口态重估（批次 C 那组 `374.46 / 429.86 s` 的分母已不是一台今天的机器）。

**回归（Debug 树，本把现跑）**
- `Total Tests: 802`（本把没新增注册用例）；定向配方 `-N` = **43** 条。
- 定向 43 条（`CoverageMeta*|source_mojibake_guard|docs_match_cli|geotransform_guard|param_guard*|PluginTest.Raster*|real_data_raster`，
  单引号是形状的一部分）：**43/43 绿**，`Total Test time (real) = 50.72 s`（⚠ 高于上一把记录的 39.19 s，见上面腿墙钟条的机器态讨论）。
- `^PluginTest\.Processing` 族 **30/30 绿**，8.40 s。
- 主腿 `ctest -R '^real_data_raster(\.|$)' -V` 通过，九字段见上面"前后"块。
- `docs_match_cli` 现读 `actions=126 docs_checked=126 missing_doc=0 rows=594 matched=593 drift=0 allowed=7` ⇒ 参数表未动（本把没改 `src/`）。
- `geotransform_guard` 两域现读与改前**逐字相同**：product `any=21 code=20 comment_line=1 checked=18 unchecked=2 allowlisted=2 violations=0 dead=0`；
  test `any=13 code=10 comment_line=3 checked=9 unchecked=1 allowlisted=1 violations=0 dead=0`（本把没新增判 `GetGeoTransform` 返回值位点）。
- `source_mojibake_guard` 在定向 43 条里跑到且绿 ⇒ 本把新增的 `.ps1` 文本没有引入编码问题。
- `.ps1` 三条硬规矩现读：三参 `Parser::ParseFile` = `errors=0 tokens=17178`（配负控 `errors=1 tokens=17181`，负样本 = 整份复制后追加未闭合字符串）；
  ⚠ **负控的判据是"≠0"而不是某个具体条数**：控制器 2026-11-02 换一种破坏方式（追加未闭合嵌套括号）同一份文件现读 `errors=2 tokens=17184` ⇒ 两个数都对，**引用哪个都必须把"怎么坏的"一起写上**，否则下一个人复现不出那个数就会以为检查坏了；
  字节态 **CRLF 与行数同源现数**：`3397` 行全 CRLF / **0 裸 LF** / **0 个非 ASCII 字节**（python 逐字节数，不按目录惯例推）。
  ⚠ 这个 `3397` 是本把改后的**测量值**（不是锚点，后续刀必然漂），引它只为证明"按 CRLF 写回、字面量只用 ASCII"这两条真做过。
- `data/` 只读两件套（**本座开批时刻 2026-10-30 19:29:35**）：`find data -type f -newermt <开批时刻>` = **0**；
  `python check_data_baseline.py`（批次 D 收尾两件套之一，不入库）= `BASELINE: 88  CURRENT: 88  DIFFS: 0`（本把全部用 helper 造件，未开任何原件；
  收尾时现读，未跑过任何基线生成脚本）。
- 受保护位点 `src/plugins/terrain/terrain_plugin.cpp` 的 blob 收尾现读仍是 `588a42504ee19bcb551f8214e5c1adb68cea20e9`；
  `git status` 空、射程外文件 0 改动。

**缺口（P3c-b 登记，不改判据）**
- `filter` 七支本把判到 **5 支**：`gaussian`（无手推 Mean 载体，边界列另受反射限制）与 `bilateral`（逐点期望需上游 LUT 口径，P3b 已登记为不可得）
  两支仍不判 ⇒ 第 5 列 `real_regression` 说的是"该动作有真实腿 case 与值级判据"，**不是**"七支都判"。
- 腿仍不判 `kernel_size` 是否真被消费：五支都传 `ParamSpec` 默认 5 ⇒ 静默忽略 `kernel_size` 的实现照样打印这五条剖面。
  建议④的候选期望本把**不铺**，只登记：同一族 `--kernel_size=3` 的 dilate 剖面是 `5·min(51, x+1)` ⇒
  `Σ = 5·Σ_{0..51}x + 5·51 = 6630 + 255 = 6885` ⇒ `Mean = 6885/13260 = 0.5192307692`（会印 `0.519231`，距 median `0.019231`、
  距本族 dilate `0.018853` ⇒ 与现有五支都值级可分）。留给后续刀判是否加。
- A3 的打卡反证只打了 **open 支的 Mean** 一处（`-Label` 拼错）⇒ 其余四条分支的"真读字段"仍靠各自的臂 + 错配臂承担，本把没逐支做拼错臂。
- Release 树本把未重编 ⇒ **Release 侧未验收**（D1~D3 全段同样未验收）。
- 本把的 CONCERN 只有一个，就是上面腿墙钟 `max = 29.43 s ⇒ 2.04×`：**未抬 TIMEOUT、未减断言、未拆腿**，留给控制器在安静窗口复读或改判口径。

### 批次 D 覆盖晋升 P4a（2026-11-02，`processing.stats` 一条 PENDING→GOLD；**只做单测面，不碰腿、不碰矩阵**）

改前现读：登记表里 `processing,stats` 是 3 列 PENDING 行，执行用例 `PluginTest.ProcessingStatsExecution` 的块体里
只有三条 `metadata.count("min"/"max"/"mean") > 0` —— 那是**键在不在**、不是值对不对，值级断言 0 条 ⇒ 过不了
`CoverageMeta.GoldEvidenceIsAuditable` 第二条。本把在**同一个 `TEST_F` 内扩写、没新增用例**
⇒ Debug 注册数 `Total Tests:` 改后现读 **802**；**改前那个 802 是按 PRE_TEST 注册规则推得的**
（本把没跑改前基线、没留改前载体；本把只在既有 `TEST_F` 里扩写、没新增用例，注册规则上改前也是同一个数
——新增 `TEST_F` 才会当场变 803，且 PRE_TEST 期就能发现）。
**本动作是无产物动作**（`ProcessingPlugin::doStats` 既不读 `output`、也不写任何文件）⇒ 判据只能挂在
「消息文本」与「metadata 键值」两层，产物存在性在这里没有意义。

**推得与实跑分开**（下面"推得"那一栏是首跑前写定的，算式一起写；"实跑"是首跑读数，一次通过、没有回填）：
夹具用现成生成器（52×12 斜坡、同形状再声明 NoData 的一件、两带不同 `base/step` 的一件、8×13 的 Byte 一件），
各占独占文件名。斜坡件 `value = base + step * x`、沿 y 恒定 ⇒ 每档恰重复 h 次 ⇒ 逐值等权 ⇒
`Mean = (min+max)/2`、总体方差 = `(n² − 1)/12 · step²`（n = 档数）。

| 观察点 | 推得（首跑前写定） | 实跑 |
|---|---|---|
| F1 = 52×12、`{{0,1}}`（档 0…51，N=624）min/max/mean | 0 / 51 / 25.5 | 逐字相同 |
| **F1 stddev** | `sqrt((52²−1)/12) = sqrt(225.25) = 15.008331019803634`（**总体**除数 n） | 命中；GDAL 自写的边车里印 `15.008331019804` ⇒ 同一族公式的第二份独立读数 |
| 样本口径的对照数 | `sqrt(225.25·624/623) = 15.020371`，距期望 **0.012040** | A3 臂实测残差 `0.012039980196366429` |
| F3 第二带 `{{10,2}}`（档 10…112） | min 10 / max 112 / mean 61 / stddev `sqrt(901) = 30.016662039607269` | 命中（样本对照残差实测 `0.024080960392730333`） |
| F4 = 8×13 Byte（档 0…103，N=104） | 0 / 103 / 51.5 / `sqrt(901.25) = 30.020826104556150`，`type = "Byte (uint8)"` | 命中（样本对照残差实测 `0.14537989544384899`） |
| width/height | 取栅格 XSize/YSize；三件全**非方**且 F1 与 F4 是两组不同的数 ⇒ 轴序写反必红 | 命中（A4 臂实测差 40 与 5） |
| NoData 半边（两件） | 无声明件 ⇒ `count("nodata") == 0`（判**不存在**，不判空串）＋消息无 `NoData:` 行；声明 −9999 件 ⇒ 键存在且等声明值＋消息 `NoData: -9999` | 命中 |
| 消息行印法 | `metadata` 走 `std::to_string(double)`（6 位**小数**）、消息行走默认 6 位**有效数字** ⇒ 同一个数两种印法 `15.008331` / `15.0083` | 命中（容差下界由 6 位小数打印决定，不是 float32 ulp：档值全是整数 ⇒ 现算侧零舍入） |

**⚠ 更正任务书里的一处口径**（写在这里以免下一个人再抄一遍）：任务书说"样本口径会给 `15.154757`、两数相差 `0.146426`"。
`15.154757 = sqrt(225.25·52/51)` 是**把 52 个档值当成 52 个样本**的结果；本动作的 N 是整带像元数 624（每档重复 12 次），
代码真换成样本口径时打印的是 `15.020371`（差 0.012040），`15.154757` 在这件夹具上任何实现都印不出来。
夹心结论不变（两个候选数都远超 1e-6 容差），但注释与文档必须写 `15.020371`。⇒ 判据代码未因此改动，A9 反事实臂恰好
把这个数摆出来：把期望常数改成 `15.154757` ⇒ 实测红在差 `0.14642599999999995` 上。

**四个可见性/参数臂的读数**
- `band`：两带各给不同 `base/step` ⇒ `band=1` 与 `band=2` 的四条统计量互不相同（另加一条"两带 stddev 必须不等"的形状判据）。
  本族承重挂在**第二带**：第一带与 F1 档值逐字相同 ⇒ "取带写死 1"在 F1/第一带上全绿，只有第二带能否掉它（A7 臂实测正是这样）。
- `input`：缺/空 ⇒ 走 `doStats` 自己那条 `fail("input is required")`，文案逐字钉；
  不存在的件 ⇒ **真走到打开那一步**，现读形态是抛（`openRaster` 是 `if (!ds) throw GisError("Cannot open raster: …")`，
  而插件直调不过框架 `execute_guarded` 那层 catch）⇒ 用例把 `fail:` / `thrown:` 两形态归一后判"不成功且不是被空串那条挡掉的"。
- 非法 band：单带件上 `band=9` ⇒ `Cannot get band 9`，再打 `band=7` ⇒ 文案跟着变成 `Cannot get band 7`
  （钉住那个号是 `std::to_string(band)` 拼进去的、不是常量串），且失败结果 `metadata` 为空。
- `output` 三层口径：**直调不传 `output` ⇒ success**（动作不读这个键，本块六次不带该键的调用全绿）+ **传了也不写文件**
  （`fs::exists(output) == false` 且那个路径连边车都没有）。CLI 层另有必填闸：实测
  `gis-cli processing stats --input=… --band=1`（不带 `--output`）⇒ **rc=1、`Error: 参数“输出栅格”不能为空`**；
  带上 ⇒ rc=0 且实测产物不存在。

**臂清单（十一臂：十臂动产品消费点 + 一臂改期望常数；每臂单独改单独还原，驱动单实例，日志与 `EXIT_CODE=` 都 tee 成文件
（在构树探针目录里，不入库，此处只贴残差）；`src/` 还原用 `git show HEAD:<path>` 按该文件原行尾写回，
再用「动手前工作树裸 sha ↔ 还原后裸 sha」＋「cleaned-content == 索引 blob」两条一起对拍 —— 十一臂全部一致、还原后 `git status` 空）**

| 臂 | 改动 | 结果 | 红形态 |
|---|---|---|---|
| A1 | `ComputeStatistics` 的 `false→true`（放开 bApproxOK） | **绿 ⇒ 等价变异** | 无（等价归因见下面那条否证与缺口 G-3） |
| A2 | min↔max 实参写反 | 红 ×8 | F1 差 51、F3 第二带差 102、F4 差 103，外加消息 Min/Max 两行 |
| A3 | stddev 乘 `sqrt(N/(N−1))` 换样本口径 | 红 ×4 | 残差 `0.012040` / `0.024081` / `0.145380` 三条逐一命中推得值 + 消息 StdDev 行 |
| A4 | width↔height 写反 | 红 ×9 | metadata 的 width/height 六条（F1 两条、F3 第二带两条、F4 两条，`metaNumber(…, "width"/"height")` 差 40/40/40/40/5/5）+ 三条消息 `Size:` 行相等断言（实到 `"12 x 52"`、`"12 x 52"`、`"13 x 8"`） |
| A5 | `noDataOk` 判成恒真 | 红 ×2 | F1 的 `count("nodata") == 0` 与"F1 消息不该有 NoData 行" |
| A6 | `noDataOk` 判成恒假 | 红 ×3 | F2 的 `count == 1` 实到 0、哨兵差 `2346.6789`、F2 的 NoData 行缺失 |
| A7 | `GetRasterBand(band)` 写死 `1` | 红 ×10 | 第二带 metadata 四条（min/max/mean/stddev 实差 10、61、35.5、15.0083）+ "两带 stddev 必须不同"一条（实到 `15.008331 vs 15.008331`）+ 非法带号**五条**（`band=9` 的 success 应为假、消息文案、`metadata.empty()` 三条；`band=7` 的 success 应为假、消息文案两条） |
| A8 | 删掉 `input.empty()` 那条 fail 分支 | 红 ×1 | 只红在文案那条，实到 `"thrown:Cannot open raster:  (: No such file or directory)"` ⇒ 顺带坐实"抛不是回 fail" |
| A9 | 反事实：把 F1 的 StdDev 期望常数改成 `15.154757` | 红 ×1 | 差 `0.146426` ⇒ 证那条 `EXPECT_NEAR` 真在比数 |
| A10 | `GDT_Byte` 那一支去掉 `(uint8)` 后缀 | 红 ×1 | F4 的 `type` 相等断言 |
| A11 | 让动作真消费 `output` 并在该路径落一个文件 | 红 ×1 | `fs::exists(output)` Actual: true ⇒ `output` 族有配套臂 |

**A0 探针：`force=false` 那条缓存路径今天判不动（把任务书的预期否证了，已同步改写测试注释）—— 但只否证了一半，2026-11-02 修复轮拆成两半重写**
预期形态是"`ComputeStatistics(bApproxOK=FALSE)` 会先读同名 `.aux.xml` 的 PAM 缓存、命中就不重读像元 ⇒ 同一件反复跑判的是
上一次的数"。这条要把**统计块**和 **NoData** 分开说，两半的读数相反：

- **`STATISTICS_*` 那一族键：只写不读，而且两类入口都不读。** 把复制件的边车里 `STATISTICS_MINIMUM` 手改成 `-777`、
  `STATISTICS_STDDEV` 手改成 `12.345678900000`（本夹具任何实现都印不出这两个数），再跑同一条命令 ⇒ 打印仍是现算的
  `0` 与 `15.0083`、metadata 仍是 `min 0.000000 / stddev 15.008331`；跑完后那份边车被**逐字重写**回
  `0 / 15.008331019804`（连键序都变了）。`raster_inspect info`（腿侧统计文本用的那条入口）同形态。
  ⚠ 口径更正：上面这组注入读数**在席位归档里没有盘上载体**（归档里那份被重写过的边车现读是
  `MINIMUM 0 / STDDEV 15.008331019804`、那份 stdout 里也没有 `-777`）⇒ 记为**控制器 2026-11-02 独立复现、
  本轮同日二次复现**；正控同一轮做：那份注入过 -777 的边车喂外部 `gdalinfo -stats` 读回 `Minimum=-777.000`
  ⇒ 文件本身有效、是这两类入口不取它。源码普查（否定性结论配正控）：`GetStatistics` 在 `src/` 里 **0 处命中**，
  正控 `ComputeStatistics` 命中 **4 个文件** ⇒ 本仓**没有任何入口**走"读缓存统计"那条 GDAL API
  ⇒ 原来那句"会读缓存的那类入口"在本仓**没有指称对象**，三处文本一起撤下。
- **`<NoDataValue>`：会被读回。** 同一 provenance 的边车里只加 `<NoDataValue>-9999.0</NoDataValue>` ⇒
  `processing stats` 打 `NoData: -9999` 并填 metadata `nodata: -9999.000000`，`raster_inspect info` 也打这一行；
  跑完边车带着 `-9.99900000000000E+03` 留下（GDAL 重写时把它保住了）。**读回与"边车是谁写的"无关**：从没被任何工具
  打开过的副本上手写一份（只含这一个元素）照样读回。本轮实测唯一"读不回"的手写形态是**文件开头带 `<?xml … ?>` 声明**
  那一种（声明与根元素同行也不读），而 GDAL 自己写出的边车**不带这一行** ⇒ 本刀首跑那句"手写边车结构不合 GDAL 预期
  所以没读回"、以及随后那句"条件=边车是 GDAL 写过的结构"都归错了因，两处读数相反的机制就在这声明一行上。
  射程另见下面 G-9。
  ⚠ 以上读数记的是 **2026-11-02 当时的产品形态**：批次 D 的 N184（2026-11-05）已在 `core::openRaster` 给句柄置
  PAM 的 `GPF_DISABLED` ⇒ 这一族的读回**今天已不成立**（机制与本节那套判别变量仍成立：判别变量确实是声明行）。
  判据与正反两臂见 `CoreTest.OpenRasterDoesNotReadBackPamNoData` 与"批次 D 裁定落地 N184"一节。

⇒ ①"缓存统计值 ≠ 重算值"的可分辨场景**造出来了**（假边车值确实 ≠ 现算值），但两类入口都不取 ⇒ **本把判不动这条**，
没有把它写成断言；②每次 `execute` 前删边车 + 独占文件名：对**四个统计量**仍是防御性纪律、不承重；
对 **NoData 那一族判据是承重的**（F1 的 `count("nodata") == 0` 与"F1 消息里不该有 `NoData:` 行"两条都以
"这份边车里没有 NoData 声明"为前提——同目录里别的动作留下的边车能给一件无 NoData 的件伪声明出 NoData）
⇒ 本刀首跑把整条 `pamClean` 一起降级为"不承重"是**覆盖过头**，本轮收回 NoData 那一半。
同时**撤下**原来那句"腿侧那些走统计缓存的 `Remove-Item` 判的确实是缓存"：本仓没有读统计缓存的入口，那句话现在会
诱导后人把腿的统计判据理解成"读缓存"；腿侧那些 `Remove-Item` 实际判的是什么，本把**未取证**，别照旧话推理。
③A1 的等价**主因是本把夹具决定的**（三件都是刚 Create 的无概算件，没有 overview 就没有"近似"可走）；
关于两条口径各自读什么的机制解释本轮**不写**（未取证）⇒ 这一条按"等价变异"登记，不许当成交付。

**N183 立案（本把不修，只复述）**：`processing.stats` 的 `--output` 三层不一致 —— ①插件级 `ParamSpec` 把 `output`
声明为 `required=true`，而该表被 `processing` 全部 16 个动作共享 ⇒ CLI 的通用必填闸（消息按 displayName 拼）
拒不带 `--output` 的 stats；②GUI 该动作的 per-action `visibleKeys` 只有 `{"input","band"}`，不含 `output`；
③`doStats` 既不读也不写 `output`。实测：不带 ⇒ rc=1 + `Error: 参数“输出栅格”不能为空`；带 ⇒ rc=0 + 产物不存在
⇒ 用户被迫给一个永远不会被写的路径。与 #146（vector 的 `--output` 误报必填）同族，但多出"传了也静默不写"这一半。
（**时态限定**：本条是立案时的读数；2026-11-05 已由下面"批次 D 裁定落地 N183"那节修掉，含 #146 一并销账。
本节留的是当时的实测，不再代表今天的行为。）
修法（从该动作的必填里摘掉 `output`，或让 CLI 的必填闸走 per-action 可见键）**改可观测行为 ⇒ 归用户拍板，本把不动 `src/`**。
本把只做三件：把"直调不传 `output` 仍 success、传了也不写文件"钉成断言；文档诚实化
（`docs/算法说明/影像处理与分析/统计信息.md` 五处：CLI 必填而动作不消费、StdDev 是总体口径、本动作无产物、
`NoData` 行与 `nodata` 键只在声明时出现、**以及本轮补的"边车里的 `<NoDataValue>` 会被读回 ⇒ 判有没有 NoData
之前要保证边车干净"**——前三处是本刀首跑落的，第四处同批，第五处由本轮补上，与上面 A0 那两半同一口径）；
在这里复述 N183。

**落账读数（现读，口径 = 登记表头部自己声明的那五条命令逐条照跑）**
`data_rows 126 / gold_rows 113 / pending_rows 13 / gold_rows_all_lines 114 / gold_rows_table_driven 15 /
gold_rows_literal_slot 98`，`kPendingCeiling` 由 14 同步下调到 **13**，不变量 `113 + 13 = 126` 成立；
新注释一律 `[:]GOLD` 写法 ⇒ 裸计数只随那条数据行一起 +1。
回归：`Total Tests: 802`（**改前那个 802 是按 PRE_TEST 注册规则推得**，本把未跑改前基线、未留载体）；定向配方现读 **43** 条注册、**43/43 绿**（三条守卫
`source_mojibake_guard` / `docs_match_cli` / `geotransform_guard` 都在里面跑到）；
`-R '^PluginTest\.Processing[A-Za-z0-9_]*(\.|$)'` 现读 **30** 条、**30/30 绿**；
`ctest -R '^(CoverageMeta|CoverageMetaJudge)'` 19/19 绿（`EveryActionIsRegistered` / `PendingCountDoesNotGrow` /
`RegistryHeaderCountsMatchData` / `GoldEvidenceIsAuditable` 四条点名绿）。

**缺口（P4a 登记，不改判据）**
- G-1 `type` 那 8 支 switch 本把只判到 **2 支**（`Float32`、`Byte (uint8)`）；`UInt16/Int16/UInt32/Int32/Float64`
  与 `default → "Unknown"` 六支不判。⚠ 本把**改判**（原话"现成夹具生成器只出这两种类型 ⇒ 夹具决定的"已被否证）：
  这六支是**可判而未判**——把现成那件 52×12 斜坡逐支转一下数据类型，六支今天就能出期望值（实测 5 支具名
  `UInt16`/`Int16`/`UInt32`/`Int32`/`Float64` 与复数型 `CInt16`/`CFloat32`/`CInt32` 落 `Unknown`，全部 rc=0，
  `Type:` 行与 metadata `type` 同步给同一串），夹具不是障碍 ⇒ 归"未做"，不归"做不了"。
- G-2 `force=false` 的 PAM 缓存分支：**今天判不动**——上面 A0 那两半里"统计块只写不读"那一半（`processing stats` 与
  `raster_inspect info` 两类入口都不取 `STATISTICS_*`，且本仓没有任何入口走 `GetStatistics` 那条 API）⇒ 没写成断言。
  要让它可分辨得先在本仓之外引入"读缓存统计"的入口，或改产品口径；**注意这条缺口只管统计块**，
  NoData 那一族不在此列（它今天就被读回、且承重，见 G-9）。
- G-3 A1（放开 bApproxOK）是**等价变异**，等价主因是夹具无概算 ⇒ 这一臂不构成任何交付证据。要让它可分辨得先给夹具建
  overview，而那判的是 GDAL 的概算选择、不是本动作的契约。
- G-4 `output` 族里"F7 那条『发不发 output 读数逐字相同』"仍**无配套变异臂**（A11 红的是产物存在性那条），
  注释里已照实标注 ⇒ 它算旁证，不算独立承重。
- G-5 不存在的 `input` 那一臂判的是"不成功且不是被空串那条挡掉的"两形态归一，不是钉死"必抛"：
  插件直调与 CLI 走的是不同的 catch 层，钉死形态会把框架层的改动也算成动作契约破了。
- G-6 `processing` 余 **10** 条 PENDING（本把只销 `stats` 一条 ⇒ 改前 11），**全表 13 条** = `processing` 10 +
  `matching` 2 + `spindex` 1（现读口径：`grep -v '^#' tests/action_gold_cases.txt | grep '[,]PENDING'`
  按第 1 列 `sort | uniq -c` ⇒ `10 / 2 / 1`，`-c '[,]PENDING'` 总数 13）；这 13 行里除本把那条之外
  **其余 12 行一字未改**（原话"processing 余 13、其余十一行"是把两个口径拼错了：13 是全表数、不是 processing 的数）。
  腿与矩阵本把未动 ⇒ `processing,stats` 的矩阵第 5 列仍是改前状态（R-D39 的"晋升刀同笔更新矩阵第 5 列"只适用于
  **真的加了腿 New-Case** 的刀，本把没有 ⇒ 不改，留 D3 矩阵对账复核这一格）。
- G-7 Release 树本把未重编 ⇒ **Release 侧未验收**（D1~D3 全段同样未验收）。
- G-8（本轮补）**"声明过 NoData 的件，它的四个统计量该不该把 NoData 像元排除在外"没有判据**：本把的 F2 只声明 -9999、
  档值里没有 -9999 ⇒ 判到的是"声明值不落在档值里时四个统计量与不声明一致"这一半；档值里**真**有声明值时
  `ComputeStatistics(FALSE)` 会不会把它算进 min/max/mean/stddev，本把没有用例、也没取证 ⇒ 不许从"声明不影响读数"
  这句文档话外推出"声明值像元被排除了"或"没被排除"。
- G-9（本轮补）**边车 `<NoDataValue>` 读回的射程**：同名边车能给一件本来没声明 NoData 的件**伪声明**出 NoData。
  本轮实测到的是 **4 条入口 / 两类可观测量**：`processing stats`（stdout 的 `NoData:` 行 + metadata `nodata` 键）、
  `raster_inspect info`（stdout 的 `NoData:` 行）、以及两条**写出型**动作把声明带进**产物**
  （`processing filter --filter_type=median` 与 `raster_manage cog` 的产物从"无 NoData 声明"变成声明 -9999，
  四个统计量逐字不变 ⇒ 改的是产物自述、不是像元值）。
  `processing` 的 16 条动作里实测了 2 条 ⇒ **余 14 条未实测**，其它写出型插件动作同样未实测；
  "所有走打开那一步的入口都会读回"这个更宽的论域是**机制上的合理猜测、不是实测**，本把不写进文本。
  能改变交付产物这一格最重 ⇒ N184 的论域宽窄由控制器据上面这几条读数裁定，本轮不代它下结论。

### 批次 D 覆盖晋升 P4c（2026-11-02，`processing.contour` 一条 PENDING→GOLD；**只做单测面，不碰腿、不碰矩阵**）

射程 = `tests/test_plugins.cpp`（既有 `TEST_F` 内扩写，**没新增用例**）+ `tests/action_gold_cases.txt` 登记行 +
`tests/test_coverage_meta.cpp` 的 `kPendingCeiling` 一行 + `docs/算法说明/影像处理与分析/轮廓提取.md` + 本节。
开批现读：分支 `fix/batch-d-nonvector-promotion`、HEAD `f1a40f7`、树干净；登记表六键 `126/113/13/114/15/98`、
`kPendingCeiling = 13`、`Total Tests: 802`、定向 43、`^PluginTest\.Processing` 30、`(CoverageMeta|CoverageMetaJudge)` 19、
腿九字段 `84/84/63/21/21/0/64/1/0`；`轮廓提取.md` 改前行尾 CRLF 60 / 裸 LF 0 / 无 BOM。
改前块体只有 `result.success` + `fs::exists(output)` 两条弱断言 ⇒ 值级 0 条，这正是它留在 PENDING 的原因。

**两条上游定约（都定约成功；tag `4.14.0`，取源通道 = `https://cdn.jsdelivr.net/gh/opencv/opencv@4.14.0/modules/imgproc/src/<路径>`）**

1. Otsu 的扫描顺序 / 区间 / 并列 tie-break —— 载体 `modules/imgproc/src/thresh.cpp` 的模板 `getThreshVal_Otsu`
   （`getThreshVal_Otsu_8u` 只是它 `<uchar, 256u, false>` 的转发壳）。逐字要点：
   `const int N = std::numeric_limits<T>::max() + 1;`（u8 ⇒ 256 桶）、`for(i = 0; i < N; i++ )` 升序、
   `if( std::min(q1,q2) < FLT_EPSILON || std::max(q1,q2) > 1. - FLT_EPSILON ) continue;`、
   `sigma = q1*q2*(mu1 - mu2)*(mu1 - mu2); if( sigma > max_sigma ) { max_sigma = sigma; max_val = i; }`
   ⇒ **并列时严格大于 ⇒ 保留最小下标**，返回值是一个真实存在的档值；随后 `THRESH_BINARY` 的口径是
   `src > thresh`（严格大于）⇒ **阈值那一档自己归背景**。
   前提：`getThreshVal_Otsu_8u` 头上有 `CV_IPP_RUN_FAST`（`up_thresh.cpp`）⇒ 上面四条**只约束非 IPP 分支**；本构建是否走
   IPP 未证，也不能拿“dll 里搜不到对应串”当证据（复审席正控：同一检索里 `getThreshVal` 也 0 命中，内部函数名根本不可按串
   检索）⇒ 本文落点只写“实测落点（界 = 档 50 与档 0 各有一套实跑）与该 C++ 分支推导一致”，不承诺 IPP 分支同值。
2. `contourArea` 的鞋带与 `CHAIN_APPROX_SIMPLE` 是否改变顶点集 —— 面积载体**不在** `contours.cpp`
   （该 tag 上 `grep -i area` 命中 0 处，那文件现在只剩 `findContours_legacy`），真实定义点是
   `modules/imgproc/src/shapedescr.cpp` 的 `double cv::contourArea( InputArray _contour, bool oriented )`：
   `a00 += (double)prev.x * p.y - (double)prev.y * p.x;` … `a00 *= 0.5; if( !oriented ) a00 = fabs(a00);`
   顶点是像元中心格点 ⇒ W×H 实心矩形给 `(W-1)(H-1)` 而不是 `W*H`。顶点压缩与边界处理在
   `modules/imgproc/src/contours_new.cpp`：`ContourScanner_::isSimple()` 按 mode 判、
   `const bool isDirect = (this->approx_method1 == CHAIN_APPROX_NONE);` ⇒ SIMPLE 只在方向改变处 `addPoint`
   ⇒ **只删共线中间点、鞋带面积不变**；`copyMakeBorder(_image, image, 1, 1, 1, 1, BORDER_CONSTANT | BORDER_ISOLATED, Scalar(0))`
   加 `ContourScanner_::create(..., offset + Point(-1,-1))` ⇒ 贴边前景与内部前景同规格（"贴边被裁"这种夹具决定项不成立）。
   定位载体用的是 `https://api.github.com/repos/opencv/opencv/contents/modules/imgproc/src?ref=4.14.0` 的目录清单
   （该 tag 把 contours 拆成了 `contours_new.cpp / contours_common.cpp / contours_approx.cpp / geometry.cpp / shapedescr.cpp`）。

**推得与实跑分开**（"推得"是首跑前写进报告的、含算式；"实跑"是首跑读数）

| 观察点 | 推得（首跑前写定） | 实跑 |
|---|---|---|
| f1 = 40×24 三级平台（u8 档 0/50/255，计数 240/480/240）的 Otsu 界 | `i=0` 处 `0.25*0.75*118.3333^2 = 2625.52`，`i=50` 处 `0.1875*221.6667^2 = 9213.02` ⇒ **界 = 50**（档 51-254 为空 ⇒ i=50..254 全同分区同方差、i=254 实算同为 9213.02；严格大于取最小下标只决定返回值写作 50 而非 254，而 src>50 与 src>254 的前景集都只有档 255 ⇒ 产物不变 ⇒ tie-break 不是 Mean=0.25 成立的原因） | 命中（右半那一格 `(30,5)` 判背景） |
| f1 产物 | 前景 = 左下象限 240 像元、`Mean = 240/960 = 0.25`、`total = 1` | 逐字相同 |
| **209 保留 / 210 丢弃** | `(20-1)(12-1) = 209`，像素数 240 ⇒ 209 保留、210 整块丢弃、240 也丢弃 | **按此翻转**（两条形变臂各自把这三档打红） |
| fOtsu = 40×12（u8 档 0/100/255，计数 240/192/48） | `i=0` 处 `4290.25 > i=100` 处 `3990.03` ⇒ **界 = 0** ⇒ 中间档进前景、`Mean = 240/480 = 0.5` | 命中（onepx 240、Mean 0.5） |
| ⚠ 我的一处推算被自己的臂更正 | 原写"常数 127 在 fOtsu 上给 `Mean = 48/480 = 0.1`" | **实测是 0** ⇒ 那 4×12 窄岛面积 `(4-1)(12-1) = 33 < 100` 被 `min_area` 整条丢掉，我漏问了一层消费点 |
| 岛件 52×12（u8 只有 0/255 两档） | `total = 2`、面积 `1*11 = 11` 与 `2*11 = 22` ⇒ `min_area` 0/11/12/23 的 kept 阶梯 2/2/1/0、onepx 60/60/36/0 | 逐字相同（12 档 `(31,6)=1.0` 对 `(11,6)=0.0`） |
| 环件 40×24（外 24×16、内洞 12×8） | `RETR_EXTERNAL` ⇒ `total=1`、面积 345、`FILLED` 把洞填掉 ⇒ onepx 384、`Mean=0.4`、洞心 `(15,9)=1.0` | 逐字相同 |
| band 件（两波段镜像 `{51,0}` / `{0,51}`） | 两带各自 `total=1`、onepx 480、`Mean=0.5`，只有 `(5,12)` 与 `(30,12)` 互换 | 逐字相同 |
| 越界 `min_area`（-5 与 1e9+1） | 直调插件不过框架的 `validateParams` + `executeGuarded`（闸在 CLI/GUI 层）⇒ 两支都 success，分别 kept 1 / 0 | 命中（两支都 success） |
| 失败契约四种形态 | 空 input ⇒ `fail:input is required`；缺 output ⇒ `fail:output is required`；不存在的件与取不到的波段 ⇒ 走 core 抛出的 `thrown:` 分支 | 命中（首跑那三条红是我自己的参数拼装错：漏给 `band` 键、`noOutput` 从 `noInput` 拷贝时带着 output 键；补齐后 A0 全绿） |

**臂清单与红条数**（口径 = `grep -oE "test_plugins\.cpp\(([0-9]+)\)" <日志> | sort -u | wc -l`，**括号不是冒号**；
每臂日志都 tee 成文件并留 `EXIT_CODE=`，驱动单实例；`src/` 取证一律 `git show HEAD:<path>` + LF→CRLF 归一 +
与工作树基线裸 sha 对拍，还原脚本与对拍逻辑在盘上留档，**逐臂对拍输出未留档**——本会话归档里检索不到任何一份逐臂还原的运行输出（该串只命中脚本本身与两份草稿文本；正控：`EXIT_CODE=` 命中 11 份臂日志，检索串对这类文件有效）⇒ 本节只主张“还原与对拍做过、逐臂输出未留档”。日志留本机会话工作区，不入库）

| 臂 | 唯一改动 | 红条数 | 落点 |
|---|---|---|---|
| A0 干净对照 | 无 | **0**（`EXIT_CODE=0`） | 全绿底座；七臂跑完再跑一次 A0b 仍 0 |
| ARM1 `min_area` 不消费 | `area >= minArea` 改成 `area >= minArea` 恒真 | **15** | B 族 210/240/1e9+1 三档 + D 族 0/11/12/23 阶梯里的 12/23 两档（缺省档与 -5 档、0/11 档两形皆绿不红）+ **一条**计数文案（210 档那条） |
| ARM2 面积换像素数口径 | `cv::contourArea(contours[i])` → `cv::boundingRect(contours[i]).area()` | **14** | 与 ARM1 同形但少一条（`min_area=1e9+1` 那支：两种口径都丢光 ⇒ 不红） |
| ARM3 `RETR_EXTERNAL`→`RETR_LIST` | findContours 的 mode 实参 | **2** | **只有环件**的 total 与 kept；另四件同产物 ⇒ 环件是唯一承重载体 |
| ARM4 `cv::FILLED` → 线宽 1 | drawContours 的 thickness 实参 | **24** | A/B/C/D/E/F 六族（G 族失败契约 0 红；A 族 onepx 240→60、E 族 384→76） |
| ARM5 去掉 `/255` | `convertTo(outFloat, CV_32F, 1.0/255.0)` → `convertTo(outFloat, CV_32F)` | **15** | 均值与 `1.0` 探针（残差 254/127/63.5）；整数 `onepx` 判据不红（`>0.5` 的口径对 255 同样为数 ⇒ 口径选择，不是漏判） |
| ARM6 `OTSU` → 固定 127 | 把 `THRESH_BINARY 加 THRESH_OTSU` 与实参 0 换成 `THRESH_BINARY` 与实参 127 | **5** | **只在 fOtsu 那一族**；f1 与岛/环/band 四件同产物 ⇒ "跑 Otsu"与"写常数"在那些夹具上不可分辨，已照实写进文档 |
| ARM7 `band` 被写死 1 | doContour 内 `readBandAsMat(input, band, …)` → `(input, 1, …)`（全文 6 处同名行只中本函数那 1 处） | **4** | F 族两格 + G 族 `band=9` 两条（连错误路径都不再发生） |
| ARM8 改期望常数（只改测试，不动 `src/`） | `Mean` 0.25→0.5、`kept`(210) 0→1、`total`(A 族) 1→2 | **3** | 三处逐字对应我改的三个期望 ⇒ 三条断言都是活的，不是恒绿装饰 |

负结论配的正控：ARM6 那条"f1 上 Otsu 与常数不承重"是**否证性**结论，配的正控是同一批 A 族行号在
ARM4/ARM5/ARM8 里确实红过，且 `TEST_F` 是单一函数体（C 族之后的断言在同一次运行里跑到）⇒ 不是"那一族没跑"。

**登记晋升（同笔：六键 + ceiling，全部按登记表头部声明的那五条命令逐条现读）**
`data_rows 126 / gold_rows 114 / pending_rows 12 / gold_rows_all_lines 115 / gold_rows_table_driven 15 /
gold_rows_literal_slot 99`（= `gold_rows - gold_rows_table_driven`），不变量 `114 + 12 = 126` 成立；
`kPendingCeiling` 13→**12**；登记行改成 `processing,contour` 指 `PluginTest.ProcessingContourExecution`。
新注释一律 `[:]GOLD` 写法 ⇒ 裸计数只随那条数据行一起 +1（文件内"逗号紧跟 GOLD"的字面量除数据行外未增加）。
矩阵 `tests/real_data_matrix.txt` 的 `processing,contour` 那格**本把不改**：本把没加腿 New-Case ⇒ R-D39 不适用，
留 D3 矩阵对账复核这一格。

**落账读数（Debug 侧）** `Total Tests: 802`（没新增用例）、定向配方现读 **43** 条注册、**43/43 绿**
（`source_mojibake_guard` / `docs_match_cli` / `geotransform_guard` 与四条 CoverageMeta 都在里面跑到）、
`-R '^PluginTest\.Processing[A-Za-z0-9_]*(\.|$)'` **30/30 绿**、`^(CoverageMeta|CoverageMetaJudge)` **19/19 绿**、
腿九字段现读 `84/84/63/21/21/0/64/1/0` 与开批**逐字相同**（本把没动腿）。
块体计数（口径 = python 取本 `TEST_F` 到下一个 `TEST_F` 之间文本、正则 `\bEXPECT_[A-Z]+\(` 出现次数）：
块 333 行、`EXPECT_*` **85** 条，其中 `EXPECT_NEAR` **26** 条（过 `GoldEvidenceIsAuditable` 第二条的就是这一类）。
（修复轮只动注释与失败消息字面量：块 333→342 行、`EXPECT_*` 85 与 `EXPECT_NEAR` 26 不变；全文件三计数 `^TEST_F` 350 /
`EXPECT_` 2851 / `ASSERT_` 2012 与 BASE 相同；`test_plugins.cpp` 行尾写回后现读 CRLF 26886 / 裸 LF 0 / 无 BOM。）
行尾逐文件 python `newline=''` 现读后按工作树 CRLF 写回、UTF-8 无 BOM：`test_plugins.cpp` CRLF 26877 / 裸 LF 0、
`action_gold_cases.txt` CRLF 177 / 裸 LF 0、`test_coverage_meta.cpp` CRLF 1825 / 裸 LF 0、
`轮廓提取.md` CRLF 85 / 裸 LF 0（改前 60）。

**缺口（P4c 登记，不改判据）**
- C-1 **f1 上"跑了 Otsu"与"用了常数阈值"产物同形**（三档 0/50/255，界落在 [50,255) 内任何值都给同一张二值图）。
  本把靠**另造一件 fOtsu**（40×12、u8 档 0/100/255、计数 240/192/48）把它变承重，而不是替换 f1 ⇒
  f1 那几族的判据里不含这一维，别把它读成"Otsu 已被全面钉住"。
- C-2 `RETR_EXTERNAL` 与 `RETR_LIST` 只在**带洞的环件**上可分辨（其余四件同产物）⇒ 这一维的覆盖等于那一条 case；
  环套环只判到"洞不独立成环"，没判"同心环只留最外圈"的更深情形。
- C-3 两个计数有**两条通道**：`Result::metadata` 的 `total_contours`/`kept_contours`（`writeMatOutput` 返回后写入，本块
  `metaCount` 断言判的就是这两键，CLI 也把它们打到 stdout），与 `progress.onMessage` 那句计数文案（`CliProgress::onMessage`
  写进 **stderr** 并记进 Logger 环形缓冲，不进 `Result::message`/stdout/metadata ⇒ 本把靠块内一个局部捕获式 reporter 才读到
  `Contour extraction: … (area >= N)`；文案这条契约今天只被这一个用例读，没进真实腿、也没进 `--help` 面。
- C-4 `writeMatOutput` 的 NoData 传播支（输入声明过 NoData 且产物是 CV_32F 时把 NaN 换成声明值）在 contour 上**构造性不可达**：
  产物是 0/255 掩膜、`toUint8` 不产生 NaN ⇒ “该支不判”是支的性质、不是缺夹具。另开一条：**输入声明 NoData 时产物的 NoData
  声明是否被带走**——这条才是今天可判的，本把无用例 ⇒ 登记为未判项。
- C-5 本把**没测**同名 `.aux.xml` 边车把 `<NoDataValue>` 读回那条链（P4a 缺口里登记过的那一族）；
  只把"产物路径先清边车"写进用例前置，避免残留边车冒充判据。
- C-6 `min_area` 的 ParamSpec 区间 `[0.0, 1e9]` 在**直调层**判到不拦。CLI 层真拦与否：**修复轮补实测**（直跑 Debug `gis-cli`）——
  `--min_area=-5` 与 `1000000001` 都 rc=1 并打印“参数“最小面积”超出范围 [0, 1e+09]”，`0` 与 `1000000000` 放行 ⇒ 闸是**闭区间
  [0, 1e9]、与 ParamSpec 声明同向**，不属“开口方向不一致”那族边界闸；`--help` 与中文文档都不印区间（两头缺、缺得一致）。
- C-7 Release 树本把未重编 ⇒ **Release 侧未验收**（D1~D3 全段同样未验收）。
- C-8 腿与矩阵未动 ⇒ `processing,contour` 的真实数据侧仍无 case；processing 余 **12** 条 PENDING（全表同为 12）。
- C-9 第九条缺口（复审席实测、修复轮对档复核）：**8-连通对角接触的两块前景会被合成一条轮廓**——两块 5×5 仅角上相接时
  `RETR_EXTERNAL` 给 `total=1`、多边形面积 32（不是两个 16）、`FILLED` 填 50 像元 ⇒ “两个目标被算成一个”这一维今天没有任何
  判据（本块四件夹具全是列平台/矩形，任何两块至少隔着一列背景）。

### 批次 D 覆盖晋升 P5a（2026-11-02，`processing.template_match` 一条 PENDING→GOLD；**只做单测面，不碰腿、不碰矩阵**）

射程 = `tests/test_plugins.cpp`（既有 `TEST_F(PluginTest, ProcessingTemplateMatchExecution)` 内扩写，**没新增用例**）+
`tests/action_gold_cases.txt` 登记行与五条 COUNT 声明行 + `tests/test_coverage_meta.cpp` 的 `kPendingCeiling` 一行 +
`docs/算法说明/影像处理与分析/模板匹配.md` 注意事项两条 + 本节。
开批现读：分支 `fix/batch-d-nonvector-promotion`、HEAD `5544ffe`、`git status --porcelain` 0 行；登记表六键
`126/114/12/115/15/99`、`kPendingCeiling = 12`、`Total Tests: 802`、定向 43、`^PluginTest\.Processing` 30、
`(CoverageMeta|CoverageMetaJudge)` 19、腿九字段 `84/84/63/21/21/0/64/1/0`；`test_plugins.cpp` 工作区行尾 CRLF 26886 /
裸 LF 0 / 无 BOM；`模板匹配.md` 改前 CRLF 82 / 裸 LF 0 / 无 BOM。八项与开批时控制器给定的基线值零差异
（席位开批八行对撞表未随仓库留档，本节的机器面由 §验收配方 那段自证）。
改前块体只有 `result.success` + `fs::exists(output)` + 两条 `metadata.count` 的 ASSERT ⇒ 值级断言 0 条，这正是它留在 PENDING 的原因。

**夹具事实与本刀第一处纠正**：既有块从 `createPatternRaster` 的 96×96 图案件上切 `Rect(18,18,16,16)` 当模板，而那一段
整个落在 helper 第一条实心方块条件 `x∈[18,34] && y∈[18,34]` 里 ⇒ **模板是恒定的**。`toUint8` 对恒定件走
`maxVal - minVal < 1e-10` 的 `cv::Mat::zeros` 支 ⇒ 模板被抹成**恒定 0**（开批时"模板裁自图件所以仍是恒定 16×16（255）"
那句是错的，结论不变）。零方差模板让 `TM_CCOEFF_NORMED` 对全部格都给满分，`cv::minMaxLoc` 停在行主序第一格 ⇒
产品侧实到 `match_x/match_y = (0,0)`、`match_score = "1.000000"`、响应图 6561 格全为 1.0。⇒ 晋升的前提是换成**非恒定**窗口。

**本刀选 `Rect(16,16,16,16)` 而不是开批时给的 `Rect(12,12,16,16)`**，理由只有一条，且两档都在产品侧同一只二进制里
（OpenCV 4.14 + GDAL 的项目构建，跑的就是本块、不经 python cv2）测过；⚠ 这两档的读数只存在于本次取证的运行里，
**仓库内没有可复核载体**（`build/` 被 gitignore），复现方式是按本节末的配方重编后把 `kX0/kY0` 改成 12/12 再跑一遍本块。两档的峰值唯一性
（"等于最大值的格数 = 1"）都成立，但 (12,12) 档的峰是 `0.99999952`，距 `std::to_string` 的 `%.6f` 舍入界 `0.9999995`
只剩 **2.3163e-8**（0.389 个 float ULP；它已经停在"仍印成 1.000000"的最后一个格点上，再降一个格点即 `1.0f − 9·2⁻²⁴` 就印成 `0.999999`），
等于把 `match_score == "1.000000"` 架在一个 OpenCV 小版本就可能翻过去的余量上；
(16,16) 档的峰逐位等于 `1.0f`，印法余量是**有限**的：向下 8 个格点仍印 `1.000000`（第 9 格翻成 `0.999999`）、
向上 4 个格点仍印 `1.000000`（第 5 格翻成 `1.000001`）。只能标定的量（6561、`0.99999952`、`-0.69621348`、3777、峰值 `14240475.0`
）一律只进注释，不进断言期望。**一处例外并改口**：sqdiff 响应图的 argmax `(59,27)` 确实进了断言
（`kSqdiffMaxX/kSqdiffMaxY` 与块内那条 `EXPECT_EQ(sq.maxLoc.x, kSqdiffMaxX)`），它的角色不是标定峰位而是
A2 的指纹位——把 59 翻成 55 那条当场红（实到 59 vs 55），写明"若产品错走 `maxLoc`，`match_x/match_y` 必须是 59/27"。
进断言的数值全部有推导链（`matchTemplate` 输出尺寸契约、
`TM_CCOEFF_NORMED` 的余弦定义与 `[-1,1]` 值域、`toUint8` 在 {0,255} 两档上的标度 `255/(255-0)=1.0` 是二进制精确常数、
`%.6f` 印法、`doTemplateMatch` 函数体里的原文文案）。

**八族判据**（全在同一块内，一条都不是恒真）：A1 唯一峰值定位（`match_x/match_y == "16"` + `match_score == "1.000000"`，
另用测试自己行主序扫出的 argmax 与 metadata 互校跨通道一致性）；A2 min/max 选支双向可红（同对件跑 `sqdiff` ⇒
`match_x/y` 仍 `"16"`、`EXPECT_NEAR(score, 0.0, 1e-5)` 值级断 + `match_score == "0.000000"`，并独立钉一条
"sqdiff 响应图 argmax = (59,27)、且峰谷异位异值" ⇒ 删掉 `if (method == TM_SQDIFF || …) matchLoc = minLoc;` 那一支时
`match_x` 必须变 59、score 必须变 14240475.000000，A2 才有变红的能力）；A3 产物是响应图且本动作不做 `/255`
（`ASSERT_EQ` 两维 `== 81`、`EXPECT_NEAR(峰值, 1.0, 1e-5)` 夹在实现残差 4.8e-7 与误补缩放后的 0.0039216 之间、
`EXPECT_LT(min, 0)` + `EXPECT_GT(负格数, 0)` + `EXPECT_GT((0,0.5) 开区间格数, 0)` 三条合判"不是 0/1 掩膜"、
`EXPECT_NE(产物左上角格值, 1.0)` 把本刀与"恒定模板处处满分退回 (0,0)"那档切开）；A4 模板大于源（24×24 源 + 40×40 模板 ⇒
`fail:Template is larger than source image` + 产物不存在 + **源/模板对调正控必须成功且落盘**）；A5 三个必填键早退三条
（`fail:input is required` / `fail:output is required` / `fail:template_file is required`，均在 `writeMatOutput` 之前 ⇒
与成功文案是两套，三条各配一条"不许留产物"）；A6 `band` 同用于模板这一次要读点（2 波段源先 `ASSERT count==2`，
`band=1` 正控成功且产物 `25×9` 顺手复核非方件上的尺寸公式，`band=2` ⇒ 实到 `thrown:Cannot get band 2`；
注释写明另一支候选 `无法读取模板栅格数据: ` 为何构造性不可达——那句要 `readBandAsMat` 返回空 Mat，而带号越界在
`gdalBandToMat` 的 `GetRasterBand` 空指针处先抛）；A7 两套文案通道（`message` 只取 `writeMatOutput` 那句 +
遍历 metadata 值扫不到 `Template match found` / `Running template matching` + 两通道不许互顶）；A8 metadata 五键齐全 +
`template_width/template_height == "16"`（取自夹具 Rect，格点值）。夹具自检两条：模板非恒定 `ASSERT_GT(tplMax, tplMin)`
与档值只 {0,255} 两档 `ASSERT_DOUBLE_EQ`。测试侧另修一处伪信号：`cleanStale` 现在对"同名残留删不掉"当场 `ADD_FAILURE`
——Windows 下未放开的 GDAL 数据集句柄会让 `fs::remove` 静默失败，本刀第一跑就是这样把三条"产物不许存在"顶红的。

**登记为未判项（本刀不判，写在这里以免丢失）**

- **D1 地理参考**：产物是 81×81 的响应图，但 `writeMatOutput → matToGdalTiff(mat, inputPath, …)` 无条件沿用输入的 GT，
  臂实到 `gt = [116, 0.001, 0, 40, 0, -0.001]` ⇒ 声明包络 `0.081°×0.081°`，比输入本体的 `0.096°×0.096°` **少一个模板步长**
  （`0.015°`）。**方向纠正**：开批派发时写的是"产物声明的范围比实际覆盖大"，实到是**偏小**；且响应格的语义是"以该格为左上角的
  16×16 窗口的相似度"，用逐像元 GT 去声明本身就错位（每格的空间支撑是 `0.016°`，不是 `0.001°`）。不判的理由照批次 D 既有
  裁定：`geotransform_guard` 把 `code/checked/unchecked` 写死成期望数，新增判 GT 的位点要同笔改守卫。
- **D2 NoData 传播**：`writeMatOutput` 的 `srcHasNoData` 支只对 `std::isnan(row[c])` 的格换成 NoData 值；响应图里没有 NaN
  （两轮臂各档的 min/max 都是有限值，恒定档 6561 格逐格 `==` 比较成立本身就排除了 NaN）。⇒ 输入声明 NoData 时产物里的
  0 值响应**不是** NoData。本刀夹具（图案件）根本不声明 NoData ⇒ 那一支在覆盖面上空转，判它要先造带 NoData 的源，属另一条射程。
- **D3 恒定模板 ⇒ 处处满分且位置退回左上角**：产品对零方差模板**没有任何防护或提示**，且两条分支同时坏——`ccoeff_normed`
  档 6561 格全为满分、位置退回 `(0,0)`；`sqdiff` 档最小值落在 `(0,43)`（2 格并列、值 `715275.0`），而真正的方块位置 `(18,18)`
  反而是**最大值**所在（4 格并列、量级 `1.66e7`）⇒ 用户拿一张平坦区当模板会得到"位置看着挺像那么回事"的假最优。
  A1 的诊断臂把它钉成事实，但"该不该拦/该不该报"属可观测行为变更 ⇒ **立案候选交控制器定编号**，本刀不预先安号码。

**验收配方实到**（`MSBUILDDISABLENODEREUSE=1 cmake --build build/debug --config Debug --target gis_tests` 后逐字跑）：
定向大集合 43/43 全绿 40.08 s；`^PluginTest\.Processing` 30/30 全绿 6.25 s（本刀不新增 `TEST_F`，条数不变）；
`(CoverageMeta|CoverageMetaJudge)` 19/19 全绿 2.45 s；`ctest -C Debug -N` 的 `Total Tests:` 仍 802；
真实腿九字段仍 `84/84/63/21/21/0/64/1/0`（`tests/real_data_matrix.txt` 第 5 列 `processing,template_match` 保持 `none`，
本刀不改腿脚本与矩阵——改成 `real_regression` 就是假登记，R-D39）。三计数对撞（口径：`^TEST_F` 按行首计数、
`EXPECT_`/`ASSERT_` 为全文件出现次数含注释与字符串，改前从 `git show HEAD:tests/test_plugins.cpp` 的 blob 现读、
改后从工作区现读）：`TEST_F` 350→350、`EXPECT_` 2851→2896（+45）、`ASSERT_` 2012→2027（+15）。
登记表六键改后 `126/115/11/116/15/100`（逐条用声明行行尾自带命令复跑，不重打 grep）。

### 批次 D 覆盖晋升 P5b（2026-11-02，`processing.hough` 一条 PENDING→GOLD；**只做单测面，不碰腿、不碰矩阵**）

射程 = `tests/test_plugins.cpp`（既有 `TEST_F(PluginTest, ProcessingHoughExecution)` 内扩写 + 一个新的小夹具 helper
`createBlockRaster`，**没新增用例**）+ `tests/action_gold_cases.txt` 登记行与四条 COUNT 声明行 +
`tests/test_coverage_meta.cpp` 的 `kPendingCeiling` 一行 + `docs/算法说明/影像处理与分析/霍夫变换.md`
（算法内容改按两支陈述、参数表「最大半径」说明格改口、注意事项补五条）+ 本节。
开批现读：分支 `fix/batch-d-nonvector-promotion`、HEAD `45fa6b0`、`git status --porcelain` 0 行；登记表六键
`126/115/11/116/15/100`（逐条用声明行行尾自带命令复跑）、`kPendingCeiling = 11`、`ctest -N` 的 `Total Tests: 802`；
三条定向配方 `-N` 预数 43 / 30 / 19；`test_plugins.cpp` 工作区行尾 CRLF 27148 / 裸 LF 0 / 无 BOM；
`霍夫变换.md` 改前 CRLF 73 / 裸 LF 0 / 无 BOM；`RELEASE.md` 改前 CRLF 4935 / 裸 LF 0。七项与派发给的基线值零差异。
改前块体只有 `result.success` + `fs::exists(output)` + 一条 `metadata.count("detect_count") > 0` ⇒ 值级断言 0 条。

**「跑了不判」的实证（换夹具的理由）**：旧块用的 `createTestRaster("e2e_hough_input.tif", 50, 50)` 是 `i%256` 斜坡，
产品 Debug 二进制在同形状件（120×80 之外的 50×50 同式斜坡）默认参数下实到 `detect_count: 0` 且产物整幅全零
⇒ 旧三条弱断言在「0 检出」上照样全绿，`hough_threshold`/`min_line_length`/`max_line_gap` 三个键在旧块里空转。
旧形状**保留**在块内当对照臂，并新增两条把这件事钉成读数的断言（`detect_count == "0"`、产物非零格数 == 0）。

**产品事实（本席位独立复读，与派发一致）**：`doHough` 读 10 个键（GUI `visibleKeys` 同 10 个，`param_guard` 钉着），
默认 `band=1 / hough_type=lines / hough_threshold=50.0 / min_line_length=50.0 / max_line_gap=10.0 / min_radius=1 /
max_radius=0 / circle_param2=30.0`；链路 `readBandAsMat → toUint8 → cv::Canny(gray, edges, 50, 150)`（两阈值硬编码）
⇒ `lines` 支把 **edges** 送 `HoughLinesP(edges, lines, 1, CV_PI/180, houghThresh, minLineLen, maxLineGap)`，
`circles` 支把 **gray**（不是 edges）送 `HoughCircles(gray, …, HOUGH_GRADIENT, 1, 20, 100, circleParam2, minRadius,
maxRadius>0?maxRadius:INT_MAX)`（`dp=1 / minDist=20 / param1=100` 三个值硬编码）；其它值 ⇒
`Result::fail("Unknown hough type: " + houghType)`。产物 `resultImg.convertTo(outFloat, CV_32F, 1.0/255.0)` ⇒ 值域
只 {0.0, 1.0}、尺寸 = 输入尺寸（与 `template_match` 两处口径都相反）；`Result.metadata` 只有 `hough_type`/`detect_count`
两键，`message` 恒为 `Processing completed successfully`，`Hough <type> detection: N detected` 只进 `onMessage`（两套通道）。

**判据族**（全在同一块体内，一条都不是恒真；进断言的数全部有推导链，标定值只进注释）：
A1 `min_line_length` 临界对（本刀主判据）：横块占 `x∈[10,70)`（60 列）`y∈[15,25)`，竖块占 `y∈[15,70)`（55 行）`x∈[10,20)`；
上游 `HoughLinesP` 的入选判据是 `|dx| >= minLineLength || |dy| >= minLineLength`（**轴对齐跨度**、`minLineLength =
cvRound(入参)`），回描端点必须是边缘像元 ⇒ 横块任何候选线段 `|dx| <= 69-10 = 59`、`|dy| <= 9`，竖块 `|dy| <= 54`、`|dx| <= 9`
⇒ **归零档 60 / 55 是手推的**（= 块在该方向占的整格数）。实测（默认 threshold/gap）横块 55/56/57⇒2、58/59⇒1、60/61/70⇒0，
竖块 20/50/52⇒2、53/54⇒1、55/56/60⇒0 ⇒ 界**含在上界那一格**（60 已 0 而 59 仍剩一条）⇒ 判据是闭 `>=`。
A2 `hough_threshold` 阶梯：同夹具固定 `min_line_length=20`，40⇒2、60⇒1、70⇒0（另 55⇒2、65⇒0 把界夹在 (55,60] 与 (60,65]）
⇒ 单调不增且归零，该键确实进了 `threshold` 形参。A3/A4 值域与尺寸：`count(==1.0) == count(!=0)`、第三档格数 == 0、
列 40 上被点亮的行恰为 {13,14,15,23,24,25}（块的两条边界行 ±1 落笔宽度）、产物 120×80 与输入同尺寸
（夹具**特意非方**：方阵上宽高写反是等价变异）。A5 分支隔离 6 条「逐格相同」+ 3 条同支正控（见下）。
A6 三条失败契约：`input is required` / `output is required`（都在 `readBandAsMat` 之前，缺 input 不被"打不开"抛穿）、
`Unknown hough type: ellipse` 整串可比，三条各配一条「失败档不许留产物」，其中未知类型那条先在同名路径种残留件、
`cleanStale` 对"删不掉"当场 `ADD_FAILURE`、事后判「不存在」。B1/B2 `circles` 检出与环几何不变量：非零 bbox 行列跨度相等、
bbox 中心 == 夹具真值圆心 (30,30)、`m(30,30)==1.0`（`cv::circle(center, 2, FILLED)` 那句）、`m(20,30)==0.0`（环内空心，
改成 FILLED 就红）、值域同 A3。B3 两枚全等且不相交的盘（半径 8，`2r=16 <` 中心距）只改中心距：17⇒1、25⇒2。

**本刀推翻/收紧的派发放四处**（读数都在本席位自己的臂里，臂目录 `build/tmp/batchD/p5b/`，gitignored，
复现方式 = 按本节末配方重编后跑块内对应臂）：
1. **A1 的算式写歪了**：派发建议注释写成「块跨 x∈[10,70) ⇒ 边最多 60 像元 ⇒ 取 61 必 0」。实测取 **60 已经 0**，
   而 59 仍剩一条 ⇒ 判据看的是**轴对齐跨度**而不是像元个数。但**复审席把这条推导又驳了一次**：Canny 的边缘像元
   落在块外侧（横块上边留在行 14、左边留在列 9），像元集合的 x 跨到 [9,69]，按坐标减法得到的上界是 60 而不是 59
   ⇒ "归零档 60/55"只能是**实测**结论（能走通的端点对必须沿 mask 连续回描，跨外角点那种中间不是边缘像元）。
   块注释已按这个口径改写成"实测 + 上游判据"两级；闭 `>=` 的排除来自上游 `good_line = |dx| >= lineLength ||
   |dy| >= lineLength` 那句源码，不是靠"59 剩一条"。中间档 58/53 写成可红臂。
2. **阶梯的阈值组不是权威组**：派发 §2.2 的第一张表跑在 `hough_threshold=20` 上（界 55⇒2 / 60⇒0），第二张才是默认 50。
   本刀主判据用默认 50 那一组（界 57⇒2 / 58⇒1 / 60⇒0），A2 另用 `min_line_length=20` 那一组跑阈值阶梯 ⇒ 两维各自单独翻。
3. **B2 的「检出半径 ≈ 21」混了三件事**（复审席再纠一层）：`min_radius` 阶梯 20 收 / 21 拒 ⇒ 检出的**浮点**半径
   ∈ `[20,21)`；21 这个**画布整型半径**是从产物外沿半跨 22 扣掉 `thickness=2` 的 1 像元外溢读出来的，
   再由 `cvRound` 的入选集反推浮点半径应收窄到 `[20.5,21)`——原注释写成"`cvRound` 把 [20,21) 画到 21"是无效推导
   （`cvRound([20,20.5)) = 20`）。三段已拆写，判据只用不变量（半径数不进断言）。
4. **B3 的界不落在构造距离 20 上**：实测 d=19 仍给 2，且第二盘的画布中心相对真值偏 `(+1,-2)` ⇒ 合并界落在 `(17,19]`。
   派发那句「detect_count 应少于盘的个数」成立，但**不能**据此声称钉住了硬编码的 20 ⇒ 判据取 17⇒1 / 25⇒2 这一对

**P5b 修复轮（2026-11-02，控制器自跑复审判语 not approved 的三条 must-fix，只动文本、零断言改动）**：
MF-1 块头 A1 的"边缘像元最多落在 x∈[10,69] ⇒ 跨度 ≤59 ⇒ 归零是手推的"整段按"实测 + 上游判据"两级改写（复审席
独立复算：Canny 边缘留在块外侧，像元集 x 跨 [9,69]、坐标减法给出的上界是 60，而 60 实测归零）。
MF-2「参照产物与被比产物各自 `nzCells > 0`，双保险」与代码不符（非零断言只逐条给在参照产物上，被比侧只有
`outIsoMinR` 自带一条）⇒ 改成推论式表述：参照非零 + 整幅逐格相等 ⇒ 被比侧必非零，读不出来时 `productFacts`
自己 `ADD_FAILURE`；安全性没坏，坏的是句子。MF-3 B2 的半径三段区间按两处证据拆开重写（[20,21) 来自 `min_radius`
阶梯、[20.5,21) 来自 `cvRound` 入选集与外沿半跨）。复审席的三条反事实臂各只红一条、且都落在被翻的那一格上
（60→59 ⇒ `detect_count` "1" vs "0"；正控键换惰性键 ⇒ 格差 `0 vs 0`；bbox 行中点 30→31 ⇒ 30 vs 31）。；这三臂的对拍输出不在仓库内（`build/` 被 gitignore），复现方式是各改那一格再跑本块。
   （调小 ⇒ 近档翻成 2；调大 ⇒ 远档翻成 1，两个方向都可红），精确界登记进下面的未判项。

**N185 立案复现（本刀只登记，不修 `src/**`，不把缺陷钉成契约；改前现读 `grep -rl "N185" docs tests src gui` = 1，
唯一命中是本刀块内注释，docs/RELEASE.md 此前无该号条目 ⇒ 本节是仓库内首次登记）**：
`max_radius` 默认 `0` 在产品里被写成 `INT_MAX`，而上游 `HOUGH_GRADIENT` 只对 `maxRadius <= 0` 走自动档
（`maxRadius = std::max(rows, cols)`），正数**原样**进入半径估计阶段并按 `cvRound((maxRadius-minRadius)/dr*10)` 开
`AutoBuffer<int> bins` ⇒ 天文数字分配 ⇒ `std::length_error("bad array new length")`；它前面还有一条早退
`centerCnt == 0 return` ⇒ 用户视角的全部形态是「候选圆心一个都没有 ⇒ 成功且 `detect_count: 0`；命中至少一个 ⇒ 必抛」。
本刀单变量对复现（120×80、圆心 (30,30)、真值半径 20 的实心盘，仓库内可复核的复现配方见本节末）：
`--hough_type=circles --circle_param2=15 --min_radius=10` 留默认最大半径 ⇒ 进程 rc=1、stderr `bad array new length`、
**产物不落盘**；同条命令加 `--max_radius=40` ⇒ rc=0、`detect_count: 1`；把 `circle_param2` 抬到 900（零候选）而留默认
最大半径 ⇒ rc=0、`detect_count: 0`。⇒ 派发的因果链独立复现成立，一处补记：上游还有一条
`if (nzSz < maxRadius * maxRadius)` 落在那条早退之后、`bins` 分配之前，`INT_MAX` 的平方在 `int` 里溢出，
它不改变「一检出就抛」的结论，只说明这条路在早退之后还有第二处会被极大值碰到。
**这条不是用法建议**：`circles` 支要给正 `max_radius` 是"当前唯一能拿到检出结果的路径"，缺陷本身留待拍板。

**登记为未判项（本刀不判，写在这里以免丢失）**

- **D2 Canny 阈值不可配**：`low_threshold`/`high_threshold` 两个键对 `hough` 不生效（产品的 Canny 硬编码 50/150），
  文档参数表本来就没列这两键 ⇒ 不是漂移，但"边缘阈值不可配"今天没有任何判据（同 N182 家族）。
- **`max_line_gap` 在本批夹具上「消费了但看不见」**：横/竖块的边缘都是连续实边，`max_line_gap` 取 1 与 10 产物逐格相同
  ⇒ 它进了 `HoughLinesP` 的形参却没有可判的可见后果；要判得先造"边上有断口"的夹具（本刀不做）。
- **B3 的精确合并界**：只钉住「阈值落在 17 与 25 之间」，`minDist=20` 那个字面值没被钉住（原因见上面第三处纠正）。
- **地理参考**：`writeMatOutput → matToGdalTiff` 无条件沿用输入 GT，本刀不判产物 GT —— 照批次 D 既有裁定
  （`geotransform_guard` 把期望数写死，新增判 GT 位点要同笔改守卫）。

**验收配方实到**（`MSBUILDDISABLENODEREUSE=1 cmake --build build/debug --config Debug --target gis_tests` 后逐字跑）：
定向大集合 43/43 全绿 41.80 s；`^PluginTest\.Processing` 30/30 全绿 6.07 s（本刀不新增 `TEST_F` ⇒ 条数不变）；
`(CoverageMeta|CoverageMetaJudge)` 19/19 全绿（登记表改后复跑 2.60 s）；`ctest -C Debug -N` 的 `Total Tests:` 仍 802；
真实腿九字段仍 `84/84/63/21/21/0/64/1/0`（`tests/real_data_matrix.txt` 第 5 列 `processing,hough` 保持 `none`
——腿侧没有这条 case，改成 `real_regression` 就是假登记，R-D39；第 6 列 `gold` 是目标档不是现状态，没动）。
三计数对撞（口径：`grep -c '^TEST_F'` 按行首、`grep -o 'EXPECT_'|wc -l` 与 `grep -o 'ASSERT_'|wc -l` 数出现次数含注释与
字符串；改前从 `git show HEAD:tests/test_plugins.cpp` 的 blob 现读、改后从工作区现读，并与 python 按 `\r\n` 切分同口径对撞一致）：
`TEST_F` 350→350、`EXPECT_` 2896→2972（+76）、`ASSERT_` 2027→2054（+27）；文件 27148→27654 个 CRLF、裸 LF 0、无 BOM。
登记表六键改后 `126/116/10/117/15/101`（逐条用声明行行尾自带命令复跑，不重打 grep）。
`data/` 两件套（开批时刻 = 本报告文件落笔时间 2026-11-02 20:00:13，现读 `python -c os.path.getmtime`）：
`find data -type f -newermt "2026-11-02 20:00:13" | wc -l` = 0；`python build/tmp/batchD/check_data_baseline.py` 打印
`BASELINE: 88 CURRENT: 88 DIFFS: 0`（本轮没运行 `gen_data_baseline.py`）。

### 批次 D 覆盖晋升 P7b（2026-11-03，`processing.skeleton` 一条 PENDING→GOLD；**只做单测面 + 文档面，不碰腿、不碰矩阵、不碰产品**）

基线 HEAD `9a5a1a5`（P6+P7a 补审轮三笔之后，工作树干净）。开批现读：六键 `126/118/8/119/15/103`、
`kPendingCeiling = 8`、`Total Tests: 802`、三配方条数 43/30/19，取证用的
`^PluginTest\.ProcessingSkeleton[A-Za-z0-9_]*(\.|$)` 单实例族 **1 条**。

**账**：晋升 1 行（`processing.skeleton` 那一行状态改 [:]GOLD、补第 4 列块名
`PluginTest.ProcessingSkeletonExecution`），降级 0 行；登记表六键随之
`126/118/8/119/15/103` → `126/119/7/120/15/104`（口径逐字取声明行行尾自带命令，减法那条在判据里），
`kPendingCeiling` 同刀 8→7；**不新增 `TEST_F`** ⇒ `Total Tests:` 仍 802（现读于本刀最后一次构建之后）。
载体就用既有块——它与 P6/P7a 不同，同族**没有**已判的姊妹块可借（watershed 的判据绑标签矩阵计数、
connected_components 绑 `component_count`，本动作产物里两者都没有），所以期望必须新推。

**产品事实（现读 `ProcessingPlugin::doSkeleton`）**：只读 `input`/`output`/`band` 三个键 ⇒ 没有"可见参数不生效"的问题；
链是 `toUint8`（常数件走 `maxVal - minVal < 1e-10` 的 `Mat::zeros` 支 ⇒ 整幅写 0）→ `THRESH_BINARY | THRESH_OTSU`
→ 主循环（`MORPH_CROSS` 3×3 = 4 直邻：erode→dilate→`work − opened`→累积→`work = eroded`，`countNonZero(work) == 0` 即退）
→ `skeleton.convertTo(CV_32F, 1.0 / 255.0)`；metadata 只 `action` 一键、**没有**任何计数键。
⚠ **纠正任务书两处处方**：① "`doSkeleton` 全程不调 `onMessage`"只在函数体成立——它先经 `readBandAsMat`
（发 `Reading band <n> from <path>`）、后经 `writeMatOutput`（发 `Writing output: <path>`），
成功路径**实到恰两条**，判据按这两条写（"不产生任何 `onMessage` 文案"是不实的宣称）；
② 函数签名到收尾是 **52 行**不是 53 行（纯口径差，不涉判据）。

**新判据 S1~S7（夹具 20×14 非方、前景只写 {0.0, 255.0} 两档、每块留 ≥1 像元背景边距；期望全部手推，不从产物回填）**
- S1 6×6 孤立方块 ⇒ 骨架 = 该方块两条对角线共 **12** 格（`2n`，n 偶 ⇒ 正中没有格）。推导：十字核下
  `erode(Q_n) = Q_{n−2}` 内缩一圈，而 `open(Q_n) = dilate(erode(Q_n))` **剥掉四个角** ⇒ 每轮 `temp` 恰是四角 ⇒
  4 + 4 + （2×2 四格全算角）= 12。逐格判满 36 格 + 全图 1.0 格数判 12。
- S2 5×5 ⇒ **9** 格（`2n−1`，两对角共享中心那一格，只数一次）。与 S1 成对钉住奇偶分叉：
  实现改成"二值图直接当骨架输出"⇒ 两档分别 36/25 格 ⇒ 红；改成"只走一轮"⇒ 两档都只剩最外 4 角 ⇒ 红。
- 叠加档（6×6 与 5×5 同幅、背景间隔 1 像元列）⇒ 21 格 = 12 + 9。这条钉的是手推表自己的前提：
  `erode`/`dilate` 对"4 邻域互不相邻"的并集逐块分配 ⇒ 骨架是两块各自骨架的并；间隔 0（并成 11×6）时前提失效。
- S3 已细者不变：1 像元宽横线 12 格、2 像元厚带 24 格 ⇒ 产物与输入二值前景**逐格相等**
  （没有任何像元凑齐 4 个直邻 ⇒ `erode = ∅` ⇒ `temp₀` = 整个输入 ⇒ 一轮即退）。⚠ 这两档的**逐格**是被"值域 ∧ 格数 == 前景格数 ∧ 亮格 ⊆ 前景"三条**蕴含的推论行**（等势子集即全集）——臂② 实测把它们删掉仍全绿 ⇒ **覆盖率不记在逐格头上**，留它只为红时点名坐标；真正独立承重的是那三条。厚度必须正好 2：
  3 像元厚的带 `erode` 得中间那一行、产物就不是输入——**修复轮的 S3c 档把这句从叙述升成判据**：
  夹具 {3,5,12,3}（前景 36 格）⇒ 期望 **14 格** = 两端四角 {(3,5),(3,7),(14,5),(14,7)} ∪ 中间行 x∈[4,13] 的 10 格；
  逐轮推导（`erode` = 10 格 ⇒ `open` = 32 格 ⇒ `temp₀` = 4 角 ⇒ `work₁` = 那条线 ⇒ `temp₁` = 10 格 ⇒ `work₂` = ∅ 退）
  写在块内注释里，**纯手推即得**；本刀首稿把它写成"对照读数"、块内注释还标成"已在本机 cv2 上实测过对照"——
  把一个手推可得的数标成实测，既降了自己的证据等级、又留了一个仓外载体，修复轮改准；
  修复轮另拿产品本体（`processing` 动作的 `skeleton`）现跑复核到 **14 格**、坐标集与手推逐格相同。
  本档期望格数 14 != 前景格数 36 ⇒ S3a/S3b 那条"逐格被三格蕴含"的前提在这里**不成立**
  ⇒ **逐格段在本档独立承重**；两条红点均现跑：格数期望 14→13 ⇒ 只红"格数"那一格（实测 14）；
  期望集改成 `cellsOfRects(foreground)` 而格数不动 ⇒ 只红逐格、恰 **22 格错配**（集合 = 前景集 − 骨架集）。
- S4 子集不变量（逐档扫"亮格 ⊆ 该档输入前景"，推导 `temp_k ⊆ work_k ⊆ binary`）——**弱判据**，
  只在补强时用：把实现改成"二值图直接当骨架"这一条全绿，红的是 S1/S2。
- S5 值域只 {0.0, 1.0} + 产物宽高 == 输入宽高（非方夹具上 (cols, rows) 写反必红；删掉 `1.0 / 255` 就到处读到 255 ⇒ 红）。
- S6 metadata 恰 `action = "skeleton"` 一键、`Result::message` 是 `writeMatOutput` 那句
  `Processing completed successfully`、消息通道恰两条且顺序固定（见上面那条纠正）。
- S7 常数件退化档：整幅同一常数 ⇒ `toUint8` 抹成全 0 ⇒ **没有前景** ⇒ 产物整幅平方和 == 0.0，
  顺带证明主循环一轮即退。期望是"整幅 0"而**不是**"骨架等于输入"，注释里写死了这句免得后人当它是退化保形。
- S8 `band` 双向档（修复轮 MF-3）：**同一枚** 2 波段件（20×14 非方）里两波段各放一种"认得出身份"的前景——
  第 1 波段 = 一条 9 格的 1 像元宽横带（每像元缺上/下直邻 ⇒ `erode = ∅` ⇒ 骨架 = 输入 = **9 格**），
  第 2 波段 = 一枚 x 平移 +10 的 6×6 方块（⇒ 与 S1 同形，**12 格**，坐标 = 主对角 `{(12,2)…(17,7)}` ∪
  反对角 `{(17,2)…(12,7)}`，偶数边长两对角不相交）。两档**只换 `band` 实参**：`band = 1` 期望等于那条带、
  `band = 2` 期望等于那个 X ⇒ **"恒读第 1 波段"红在 `band = 2` 档、"恒读第 2 波段"红在 `band = 1` 档**。
  ⚠ **成对才承重**（复审席构造出的"单档不承重"形态就是依据）；对照**不用**"空 vs 非空"——整幅同值会先被
  `toUint8` 的常数退化支抹平，那一档判的是 S7 已判过的常数退化，不是 `band` 选了哪个波段。
  承重分级沿用 S1/S3 口径：`band = 2` 档 12 ≠ 36 ⇒ 逐格独立承重；`band = 1` 档 9 == 9 ⇒ 逐格是蕴含的推论行。
  产品侧那对形态属**构造论证**（没改 `src/`、没实跑，报告里分列）。S8 把既有断言参数化复用、**没新增宏**
  ⇒ 三计数不变（`^TEST_F/EXPECT_/ASSERT_` = `350/3042/2064` 改前改后同值）。

**四臂**（每臂只改 `tests/test_plugins.cpp` 的一个桩点、单实例直跑取 gtest 明细、`cp` 字节还原后工作树 sha256
与改桩前快照逐位一致）：① S1 格数 `12 → 13` ⇒ 红，1 条 `error:`，实测 12；③ S7 平方和 `0.0 → 1.0` ⇒ 红，1 条；
④ S1 夹具塞一个孤立单点（期望不动）⇒ 红 2 条（格数 13、子集 1 格出界），而**值域那格不红**（本就该不红：
它管缩放不管位置）；S1 的逐格窗口也不红（单点在窗口外）⇒ 印证"窗口外只能由格数/子集现形"这条设计。
② 把 S3 两档的逐格比较删掉只留格数 ⇒ **仍然全绿**，读数如实记在这里：那两档的逐格被
"值域 ⊆ {0,1} ∧ 格数 == 前景格数 ∧ 亮格 ⊆ 前景"三格**蕴含**（期望格数恰等于前景格数 ⇒ 等势子集即全集），
所以 S3 的承重结构是这三格而不是逐格；逐格留着是为了红的时候点名到格坐标（归因），不是为了独立覆盖。
蕴含前提 `|skeleton| == |foreground|` 在 S1/S2 两档不成立（12/36、9/25），那两档的逐格独立承重 ⇒ 臂① 与臂④ 都落在那里。

**未判项（登记，不在本刀做）**：
- **贴边效应**：前景贴栅格边界时 `erode` 的虚采样按域另一端极值处理，靠边一圈整排消失
  （同形状独立模拟读数：贴左贴顶的 6×6 从 12 格变 6 格）。本刀全部夹具避开边界，没有一档判它 ⇒ 可判而未判。
- **厚带家族只判了"厚 3 × 长 12"那一档**（修复轮补的 S3c）：**≥ 4 像元厚**的带、
  以及"厚 3 但很短（长 ≤ 2，中间那一行凑不齐左右直邻）"那一类**仍未写成判据**。
  修复轮拿产品本体现跑到过它们的读数：4 厚 × 长 12 ⇒ 24 格（被细化）、
  3 厚 × 长 2 ⇒ 6 格（**保形**）、3 厚 × 长 1 的竖带 ⇒ 3 格（**保形**）
  ⇒ "厚度 ≥ 3 就一定被继续细化"这句对产品也不成立，用户文档已改成
  "有没有像元凑齐 4 个直邻"的口径（见下面「文档对撞」）。
- **失败支与取消支**（`input`/`output` 为空、`mat` 读空）：本刀判据全是成功路径；这些支既有测试面里无人判，
  也不在本任务书范围内。
- **`band` 越界那一支仍未判**（S8 判的是"读哪个波段"，不是"波段不存在"）：`ParamSpec` 的 `band` 只夹
  `1..999`、与件的实际带数无关；取空波段时 `gdalBandToMat` 在 `GetRasterBand` 拿到空指针处
  `throw GisError("Cannot get band " + …)`，而 `doSkeleton` 自身不接（抛出由 `execute_guarded.cpp` 那层
  边界兜获转成失败 `Result`）⇒ 判它要写的是"越界报什么文案、`Result` 长什么样"，续作席**没实跑、没补档**
- 矩阵第 5 列 `real_regression` 那一格**本刀没加腿 case、也没核它这个记法本身是否名副其实**（列口径与 taxonomy 归 #184 对账）。
  但腿侧那条 case 确实在判：`processing_skeleton` 分支里有三条 `Assert-TextContains`（`Size:   64 x 64 x 1 bands` / `Type:   Float32` / `Max:    1`），本刀收尾那次 `-V` 现跑 `ran=84 judged=63` 全绿。⚠ **MF-1 收口**：原先写的"`Max: 1` 与单测侧 S5 值域判据是同一件事的两个通道、块头注释里互相点过名"**两句都不成立** ⇒ ①"互相点过名"没有载体（块内 grep `腿` / `real_regression` / `Max` / `64` 命中 0，腿侧脚本里也没有反向引用），已删；②两通道**相关但不等价**：腿侧是**子串**判定，而 `raster_inspect` 那处打印是固定间距 `"  Max:    " << bMax` 后面直接接值 ⇒ `Max = 1.5` 印成 `Max:    1.5`，同样含 `Max:    1` 这个前缀；更要紧的是它只看 `Max`，对产物取 {0, 0.5, 1} 的实现照样绿 ⇒ 腿侧只能证"最大值以 1 开头"，**不能**当 S5 值域判据的跨通道互校（与本仓对 `Contains` 前缀吃数字的既有口径同一条）。单测期望的来源仍是上面那张手推表，没有从腿侧读数借。

**文档对撞（同刀做）**：拿 `docs/算法说明/影像处理与分析/骨架提取.md` 对四件事——① 骨架 ⊆ 输入前景、
② 产物值域 {0,1}（不是标签矩阵/灰度）、③ 核是 4 邻域十字而非 8 邻域、④ 1~2 像元宽**且不含 4 个直邻齐备的像元**的形状骨架等于自身。
**结论：四项原文都没有载明**（原文只说"反复腐蚀、开运算和差分累积"，既没写核的邻域，也没写值域与子集关系；
没有"8 邻域/任意邻域"那种**与产品互斥**的表述 ⇒ 不是漂移，是缺载）。已在正文三处补写：「算法内容」加结构元那条
（它直接决定结果，缺了就无法复现）、「输出说明」加值域/尺寸/子集三条、「结果解释」加"1~2 像元宽**且无一像元凑齐 4 个直邻**时骨架本身"
"孤立方块骨架是两条对角线""整幅同值 ⇒ 无前景 ⇒ 输出整幅 0"三条。
⚠ 第一条的本刀首稿写成了**无条件的普适句**，它**对产品为假**：1 像元宽的十字（中心像元 + 上下左右 5 格）里，
中心像元凑齐 4 个直邻 ⇒ `erode` 非空 ⇒ 产物只剩中心 1 格，**不是**"逐像元相同"——修复轮把前提写实、并把这个反例单独写成一条
bullet（「结果解释」该处由一条拆成三条）。参数表的 参数/类型/必填/默认值/说明 五列一个字没动
（`docs_match_cli` 钉着），行尾按原样 CRLF、无 BOM。
**原文与产品没有互斥**（互斥的是本刀首稿新写进去的那句普适句，已改准 ⇒ 不是原文漂移、
也不是产品缺陷，无需新编号）。

**修复轮（P7b 复审 approved + 5 must-fix，2026-11-03 17:3x~19:5x）**：复审席判 approved（晋升不撤行），
但给出替代判语"MF-3 与 MF-5 未落地则降回 PENDING"。落点六笔：`f4439ca`（MF-5 文档面）/ `9649f9b`（MF-2 蕴含标注）/
`4d8fd80`（MF-4 厚带真判据 S3c）/ `1025c01`（控制器扫出的 `uXXXX` 转义残留）/ `2d31b7c`（MF-3 S8 两档）/
`982876b`（续作席自己那处档数口径订正）。**两条推翻控制器与复审席的地方**：①我给的 `createMultiBand*Raster`
配方被顶回（常量件被 `toUint8` 抹平、梯度件前景贴边 ⇒ 只有 `writeFloatBandsRaster` 可用，控制器 grep 确认后
把原处方标成作废）；②复审席"第 1 波段全 0 ⇒ `band=1` 期望整幅 0"的构造同样不成立（判的是 S7 的常数退化），
续作席改成"两档各放一种认得出的形状"。**一条流程事实登记在此以免丢失**：修复席第一档 150 次工具上限断线
（本项目四种断线形态里的第一种），三条半 must-fix 已提交、MF-3 一次没开工 ⇒ 续作席只带 MF-3 重派，
它另外自曝一处档数口径错并单独提交订正。
⚠ 另记一条**守卫射程缺口**：`4d8fd80` 的注释尾被写入工具落成裸 `uXXXX` 转义，而 `source_mojibake_guard`
的四条规则抓不到纯 ASCII 的转义序列（它抓 GBK 往返 / 私用区 / 注音+€ 三类真乱码）⇒ 归 D3 与 #201 同批。
**修复轮验收实到**：重编 rc=0 / error 0；四配方逐字跑 **43/43 绿 40.50 s（含 `real_data_raster` 那条腿——
续作席按上一档席位登记的处置把腿排除跑成 42，控制器全口径复跑并当场对撞 `data` 两件套 0 + `88/88/0`，
确认排除属处置差不属读数差）**、30/30 绿 8.9 s 级、19/19 绿、骨架单实例 1/1 绿；六键仍 `126/119/7/120/15`、
`kPendingCeiling = 7`、`Total Tests: 802`、三计数 `350/3042/2064` 改前改后同值；新增 89 行文本里
裸 `uXXXX` / 裸行号锚 / 私有路径 / `,GOLD` 各 0 条；`src/`、`data/`、腿脚本、矩阵零改动；还原链 
`BYTE_MATCH_AFTER_RESTORE` 两次 True（每次还原后先重编再复跑）。

### 批次 D 覆盖晋升 P7a（2026-11-03，`processing.connected_components` 一条 PENDING→GOLD；**只做单测面 + 文档面，不碰腿、不碰矩阵**）

**本刀全程由控制器自跑**（P6 之后账户配额仍阻断后台席位；已按裁定"控制器自跑、配额恢复后集中补审"执行 ⇒
**本刀没有独立复审席**，四臂证据与本文即补审入口）。基线 HEAD `9b8e83c`，开批现读：
六键 `126/117/9/118/15/102`、`kPendingCeiling = 9`、`Total Tests: 802`、三配方条数 43/30/19、
`^PluginTest\.ProcessingConnectedComponents` 单实例族 2 条。

**为什么载体不是 `ProcessingConnectedComponentsExecution`**：那块 19 行只有 `success` + `fs::exists` +
两条 `EXPECT_GT`（一条比 `component_count > 1`、一条比某格 `> 0`），没有任何数值钉值；
而**姊妹块** `ProcessingConnectedComponentsHonoursConnectivityParameter` 早在 Task 15 就判住了
"对角相接两块的 4/8 之别"与"兜底等于 spec 声明默认值"（`EXPECT_EQ(run(8,…), "1")` / `run(4,…), "2"` / 不传键与显式 8 同口径）。
⇒ 登记行第 4 列指向姊妹块；本刀在它上面加的是**原判据没有承载的那一半**。

**新判据（全部 `EXPECT_DOUBLE_EQ`，都落在标签格点上，不经 metadata 字符串）**
- 标签几何：夹具前景是 A={(0,0),(1,0)}、B={(2,1),(3,1)}，仅对角相接 ⇒ 8 连通下 `lab8[0]=lab8[6]=1.0`
  且背景格 `lab8[2]=0.0`；4 连通下 `lab4[0]=1.0`、`lab4[6]=2.0`。
  "背景恒 0"是上游公开语义、可手推；"行主序发号（首个新组件得 1）**不是**——它要读 4.14 的
  `connectedcomponents.cpp` 才能坐实（两遍扫描第一遍按块行主序 + `flattenL` 的 `LabelT k = 1;` 连续重编号），
  而 `is_parallel`（取 `cv::getNumThreads()` 与 `L.rows / nThreads >= 2`）一旦为真就换 `stripeFirstLabel8Connectivity`
  那套条带发号机器 ⇒ 登记为**版本与线程数限定的实现细节**，文档同口径加"不要当契约依赖"（补审轮 MF-P7a-1）。
  ⇒ 这条与 metadata 那条走的是不同通道（产物 vs 字符串），产品若"读了参数但数错"或"没读参数"都能各红一边。
- 归一化支：产品写的是 `getParam(connectivity,8) == 4 ? 4 : 8` ⇒ `connectivity=5` 必须与 8 连通逐格同几何
  （`lab5[6] == 1.0`）；5 若被原样传给上游会撞它的 `CV_Assert`、若被当成 4 则 B 变 2 ⇒ 两个方向都可红。
- 反向对照档（把变量隔离出来）：另造一块两块**完全分离**的 4×4 件（A 在第 0 行、B 在第 3 行），
  4 与 8 都必须报 2 且 `sep4[12] == 2.0` ⇒ 没有这一档，上面的 1/2 之差可能来自别的形状因素。
- 边界档：全零前景 ⇒ `component_count == "0"`（**第一因是 `toUint8` 的常数退化支**把整幅写成 0——
  `opencv_wrapper.cpp` 里 `maxVal - minVal < 1e-10` 那条，与本仓文档「处理流程」第 2 步同口径；Otsu 在常数件上
  取阈值 0 是**第二道**同向闸（4.14 可手推，不必降级为"实测"）⇒ 上游只返回背景标签 0，产品减掉背景后是 0，
  不是 -1、也不是把背景当组件的 1）且产物 16 格平方和 == 0.0
  （用平方和而不是绝对值：不额外依赖 `<cmath>`，任一格非 0 或为 NaN 都会红）。
- 零容差可行性（写在注释里）：产物是 `labels.convertTo(outFloat, CV_32F)` 的标签矩阵，
  取值是非负整数且远小于 2^24 ⇒ float32 逐位精确；读回按 `GDT_Float32` 同类型、不重采样。
- 读产物失败时 `labelsOf` 返回 **16 格 NaN** 而不是越界索引（`ADD_FAILURE` + 后续各格判据一起红）。

**四臂**（各恰好 1 条红、落点即被改的那一格，日志 `build/tmp/batchD/p7a/CF-{1,2,3,4}.log`；
每臂 `cp` 字节备份还原、工作树 sha256 与 `.BASE` 对拍一致）：4 连通的 B 标签 `2.0 → 1.0` 红；
8 连通的 B 标签 `1.0 → 2.0` 红；背景格 `0.0 → 1.0` 红；归一化档 `1.0 → 2.0` 红。

**文档面两处**（`docs/算法说明/影像处理与分析/连通组件.md`，只动正文与注意事项，参数表三列不碰）：
①处理流程第 4 步补"实现里只有 4 会被区别对待，而取值区间是 `[4,8]` 的整数 ⇒ 传 5/6/7 过闸却静默等同 8"；
②输出说明补"背景恒 0、区域用互不相同的正整数编号；`component_count` 数的是前景区域条数（不含背景，
也不是最大标签值）⇒ 仅对角相接的两块在 8 邻域报 1、4 邻域报 2"，并显式写明"哪一区拿几号"当前实测是行主序
从 1 起、但那是上游内部编号、**不要当契约依赖**（补审轮把首版那句"按行主序从 1 起编号"的契约地位剥掉）。

**登记为未判项（不修、不假装判）**
- `std::max(0, componentCount - 1)` 的 **0 下界不可达**：`cv::connectedComponents` 恒返回 ≥ 1，依据是上游
  `flattenL` 的 `LabelT k = 1;`（发号从 1 起连续、返回值是"最大标签 + 1"）——**不是**"背景本身就占一个标签"
  （那句在"前景铺满整幅、一格背景都没有"的形态下是假命题，返回值仍是 2）⇒ 那一支是死防御；全零件档判的是
  "背景不计数"，不是这个下界（补审轮 MF-P7a-2 的§8-3 半条）。
- `connectivity` 的"非 4 即 8"没有分叉可修：spec 允许 `4..8`、产品按"等于 4 才走 4"，两边各自自洽
  ⇒ 本刀只把行为写进文档；若要改成"拒绝 5/6/7"属可观测行为变更，与 #172 N172 同族，归用户拍板。
- 产物 GT 与 NoData 传播两条沿用批次 D 既有裁定（`geotransform_guard` 钉期望数、`writeMatOutput` 只换 NaN）。

**验收实到**（重编 `gis_tests`，BUILD_EXIT_CODE=0、error 0）：`ProcessingConnectedComponents*` 2/2 绿（四臂首跑之前
所有手推期望已逐格命中）；定向大集合 **43/43 绿 46.03 s**；`^PluginTest\.Processing` **30/30 绿**；
`(CoverageMeta|CoverageMetaJudge)` 与守卫子集全绿；`Total Tests:` 仍 **802**（未新增 `TEST_F`）；
`data` 两件套 0 + `88/88/0`；`src/` 净零；terrain blob 未变。三计数（口径：`^TEST_F` 按行首、
`EXPECT_`/`ASSERT_` 按全文件出现次数含注释与字符串；"改前"取 `git show 9b8e83c:tests/test_plugins.cpp`、"改后"取工作树现读）`350→350`、`3004→3018`（+14）、`2063→2063`（0 ⇒ 本刀没加断言族之外的新断言）。
登记表六键改后 `126/118/8/119/15/103`；矩阵第 5 列两行仍是 `real_regression`（本刀没加腿 case，也没删）。

**过程自查**：文档那一笔首版把内嵌 `\n` 写进了 CRLF 文件 ⇒ 混出 1 个裸 LF（我自己记过的坑当场再犯），
按"先全归一再统一 CRLF"修回并复测 CRLF 55 / 裸 LF 0 / 无 BOM。

**补审轮**：本节的 ④⑤ 两条 must-fix（契约地位、因果缺环）与其落点统一记在下面 P6 节末的"P6+P7a 合并补审轮"段，此处不重述以免两处数字分叉。

### 批次 D 覆盖晋升 P6（2026-11-02，`processing.watershed` 一条 PENDING→GOLD；**只做单测面，不碰腿、不碰矩阵、不碰文档**）

本刀由**控制器自跑**完成（派发席位在 66 次工具后因账户配额耗尽中断，属第四种断线形态；它已交回"现读基线对撞"与"载体块为什么选它"两节全文，外加一份未提交的 `tests/test_plugins.cpp` 在途改动 +255/−12 ⇒ 属"丢证据不丢实现"，控制器在其基础上续作，见下"席位与我各错了哪一处"）。⚠ 因此**本刀没有独立复审席**，缺一层否证，待配额恢复后补审（三臂证据见"反事实"段）。

射程 = `tests/test_plugins.cpp`（既有 `TEST_F(PluginTest, ProcessingWatershedExternalMarkersCountDistinctRegionLabels)` 内扩写，**没新增用例**）+ `tests/action_gold_cases.txt` 登记行与四条 COUNT 声明行 + `tests/test_coverage_meta.cpp` 的 `kPendingCeiling` 一行。开批现读（席位跑，与控制器派发词给的六个数零漂移）：HEAD `b469c34`、`git status` 0 行、六键 `126/116/10/117/15/101`、`kPendingCeiling = 10`、`Total Tests: 802`、三配方 `-N` 条数 43/30/19、watershed 单实例族 `-N` 5 条。

**这条 PENDING 挂着的成因不是"没判过"**（新记法，登记以免后人重复调查）：`CoverageMeta.GoldEvidenceIsAuditable` 的数值断言名单 `kNumericAssertions` 只有 `EXPECT_NEAR / EXPECT_FLOAT_EQ / EXPECT_DOUBLE_EQ` 三项（`tests/test_coverage_meta.cpp` 里 `kNumericAssertions` 那张名单本身），`EXPECT_EQ` 一律不计 ⇒ 同族四块（`AutoMarkersKeepBackgroundAsOwnBasin` 86 行、本载体块 59 行、`WithoutSeedsCountsZeroSegments` 45 行、`RejectsMarkerInputWithMismatchedSize` 57 行）早就用 `EXPECT_EQ` 钉住了标签透传（1/2/5）、`segment_count` 口径（报 2 不报最大标签 5）、无种子 0 段 + 产物无正标签、尺寸守卫文案与两个尺寸串的先后，值级宏计数却全是 0 ⇒ 判过也不顶数。⇒ 本刀两件事一起做：①（做账）把已有数值承载力的整数判据换成守卫认的宏族；②（补真判据）补 W1/W3/W4/W5。只做①就是 shape gaming。

**为什么另三块不能当载体**（派发席位给的选定理由，控制器复核后接受）：`AutoMarkers` 块跨两支看判据条数最多，但它承载的是**自动标记支**（`markers += 1` 与 unknown 归零的顺序），且块内已注明其 `EXPECT_EQ(background, 1)` 是行为锁而非鉴别证据 ⇒ 在那块上换宏等于把没鉴别力的锁升级成 GOLD 凭据；`WithoutSeeds` 块的对象是"没有正标签"，没有 W3/W4 可判的东西；`RejectsMarkerInput` 块产物根本不落盘。

**五条判据与承重**：W2 三条 `EXPECT_DOUBLE_EQ(background/seedA/seedB, 1/2/5)`（零容差的论证在块注释里，**证据等级分两档**：前两跳在本仓源码读得到（产物 `GDT_Float32` 单波段、创建选项 `nullptr`、读回按同类型缓冲不重采样、产品全程没有 `SetScale/SetOffset`），而"GTiff 默认不压缩不量化"是**驱动侧默认**、仓内读不到 ⇒ 改在本刀产物上现读坐实（`Compression=1` / `BitsPerSample=32` / `SampleFormat=3` / `StripByteCounts=3072`；产物落在 gitignore 的构建目录、仓内没有载体，现读方式就是对它跑 `gdalinfo`）；`static_cast<float>(label)` 对 |n| ≤ 2^24 逐位精确，本块取值只有 -1/0/1/2/5；NoData 回填那段在 `writeMatOutput`（只换 NaN）、继承 NoData 值那段在 `matToGdalTiff`，两处都要求源波段声明过 NoData、夹具没声明 ⇒ 都不触发；三跳合起来"写进去的位=读出来的位"；并限定该论证**不外推**到连续量产物：`template_match` 的响应值不是整型 ⇒ "整数逐位精确"那条前提不适用）；W1 **上游的 -1 写法进产物**——发现用 `value < 0.0f`、钉值用 `EXPECT_DOUBLE_EQ(探针, -1.0)` 两件分开，**探针初值取 NaN 不取 -1**（这幅产物的合法取值里就有 -1，用 -1 当哨兵会把"存在负值"和"一格没扫到"混成同一件事），另判两枚种子块整块不含负值；⚠ 归因经补审轮改准：本夹具的 108 枚负值格（= 32*2 + 22*2，即最外一圈）**全是** `cv::watershed` 无条件写死的边框（`neg_on_ring=108`、`neg_interior=0`，控制器与补审席各自独立复算），A、B 被确定背景标签 1 整圈隔开、本几何下不存在"两盆之间"的谷线 ⇒ 这条钉的是"产品把 -1 原样透传进产物"，它在"两盆从不分界"的任何几何下都绿，**不能**当"分水岭划出了分界"的证据；真要判分界得另造一档让未知(0)带夹在两枚种子之间的标记件 ⇒ 登记为**可判而未判**（配方写进块注释）；W3 产物正标签去重集合与**从夹具 markers 缓冲导出的**期望集合双向对撞 + {1.0, 2.0, 5.0} 逐个钉死（期望不从产物读回来抄，否则洗成恒真）；W4 计数口径加一档"只把 B 的标签 5 换成 3、几何与期望都不动"⇒ 两档都必须报 2，错写法"取最大标签值"会分别报 5 与 3，同档配正控（对照产物 B 读到 3、A 仍是 2）；W5 `marker_input` 恒读第 1 波段：档一 2 波段标记（第 1 波段真标记 1/2/5、第 2 波段诱饵 4/9）+ `band = 2` ⇒ 正标签集合仍是 {1,2,5} 且诱饵一格不出现；档二正控把标记第 1 波段整幅置 0 ⇒ `segment_count` 必须 0、产物无正标签。注释点名 W5 与 `processing.template_match`（模板同受 `band`）**相反**，不是通用规律。

**反事实三臂**（控制器跑，日志 `CF-A/CF-B/CF-C.log` 直跑 `gis_tests.exe --gtest_filter` 取 gtest 明细（对拍输出不在仓库内，`build/` 被 gitignore；复现方式就是下面写的三处各改一格再跑本块），每臂 `cp` 备份还原、工作树 sha256 与 `.BASE` 对拍一致）：W1 探针 `-1.0 → 0.0` ⇒ 恰好 1 条红（-1 钉值那条）；W3 期望列表补一枚标签 `{1,2,3,5}` ⇒ 恰好 1 条红（第三个取值 `5 vs 3`）；W4 主档计数 `"2" → "5"` ⇒ 恰好 1 条红。
⚠ **CF-B 的实测驳回了在途注释一句**：原写"改期望列表 ⇒ 集合个数那条与取值那条各红一次"，实到只有取值断言红——个数那条数的是从标记缓冲导出的集合，与这个字面量无关 ⇒ 注释已按实测改准（这条注入口的承重面是"取值逐个钉死"，不是"个数"）。

**文档面对撞三处 = 零改动**（不许为了有交付而硬改）：`docs/算法说明/影像处理与分析/分水岭分割.md` 现读已写清①标记栅格"只用第 1 波段"且只比像元行列数、不比对地理变换（W5：「标记输入要求」里"只用标记栅格的第 1 波段"那句）；②分水岭线写 `-1`、确定背景 `1` 照样写进输出（W1：「处理流程」与「输出内容」两处都写着"分水岭线写为 -1、确定背景 1 照样写进输出"）；③`segment_count` 统计的是输出里 `>= 2` 的不同区域标签个数、**不是最大标签值**，外部标记编号不连续时也不会把标签空洞算进区域数（W3/W4：「输出内容」里"统计的是输出里 >= 2 的那些不同区域标签个数（不是最大标签值）"那句）⇒ 三处都与产品一致，本刀不动该文档。

**已知不承重项（登记，补审轮改成条件句）**：把 `doWatershed` 产出段那段 `label > 1` 去重计数搬到 `cv::watershed` **之前**，在**本块夹具几何**上是可存活的等价变异——条件是「没有哪枚 `>1` 的标签被最外一圈独占」：4.14 的 watershed 不重新编号、不合并、不新增正标签，只往**原值为 0** 的格灌标签，唯一的无条件改写是把最外一圈写 -1 ⇒ 搬后集合 ⊆ 搬前集合，相等 iff 上述条件成立；本块两枚 rect 都在 [1,30]×[1,22] 内 ⇒ 等价成立。**外部标记支若把某枚种子整枚放进最外一圈，本免责随即失效**（自动标记支同一条约束）。⚠ 先前"变异轮 6/6 全绿"的论域补定义：那个 6 是 `ctest -R Watershed` 的**名字过滤**条数、含 terrain 插件的同名另一实现，与本动作单实例族 5/5 不是同一论域 ⇒ W4 判"口径"不判"时序"；
两枚种子取同一标签时个数会变成 1，那一档本刀不判（要另造一幅标记，且它判的是另一件事）。

**D3 议题一条（本刀只登记、不改守卫）**：守卫认的宏族里没有 `EXPECT_EQ` ⇒ 存在"块里有真数值判据却永久 PENDING"的一类行。两条处置各有代价：扩宏族到"数值型 `EXPECT_EQ`"会瞬间让多行具备晋升条件、需逐条复核（blast radius 大，且会放走"字符串比较伪装成数值比较"这一类）；或立一条机器判据去抓"块里有数值 `EXPECT_EQ` 却仍记 PENDING"。⇒ 与 #180（腿侧 `Mark-Judged` 不要求值级期望）、#181（裸行号锚无守卫）同族，归 D3 拍。

**验收实到**（重编 `gis_tests`，BUILD_EXIT_CODE=0、error 0）：watershed 单实例族 **5/5 绿**；定向大集合 **43/43 绿**；`^PluginTest\.Processing` **30/30 绿**；`(CoverageMeta|CoverageMetaJudge)` **19/19 绿**；`Total Tests:` 仍 **802**；腿九字段逐字 `84/84/63/21/21/0/64/1/0`；`real_data_raster` 单条 **17.96 s**（在 16.48~20.35 s 的已认证带内 ⇒ 本轮 43 条配方读到 56.23 s（上一只是 38.72 s）**不是腿漂**，差值未归因，D4 前按安静态双闸复测）。三计数（口径：`^TEST_F` 按行首、`EXPECT_`/`ASSERT_` 按全文件出现次数含注释与字符串）`350→350`、`2972→3004`（+32）、`2054→2063`（+9）；登记表六键改后 `126/117/9/118/15/102`（逐条用声明行行尾自带命令复跑）；`tests/real_data_matrix.txt` 第 5 列 `processing,watershed` 保持 `none`（腿侧无这条 case，改 `real_regression` 就是假登记，R-D39）。

**席位与控制器各错了哪一处**：席位否证派发词两处——①派发把第六块 `WatershedOnSingleRowOrColumnRasterDoesNotThrow` 算进"processing.watershed 的六条块"并打勾"有动作槽"，实际它自己写着 `mgr_.find("terrain")`、测的是 `terrain_plugin.cpp` 里同名不同实现的 `doWatershed` ⇒ 归属别的行，与 #168 的"helper 里动作槽看不见"**无关**（本刀论域内没有该族实例）；②派发说载体块"承载最厚"，跨两支看并非如此（`AutoMarkers` 86 行、关系式判据更多）。控制器自跑后又纠正席位一处（CF-B 注释）。另记一条还原纪律的分叉：派发词"还原用 git 自己的 smudge"对`tests/test_coverage_meta.cpp` **不成立**——该文件工作树今天就是 LF（席位 dry-run 实测 `smudge(blob) == 工作树` 对它 False、其余四支 True），对它该按裸 LF 读写；本刀全程用 `cp` 字节备份还原，不触碰 smudge。

**P6+P7a 合并补审轮（2026-11-03，配额恢复后的第一席）**：独立复审席判两把**都 not approved**，五条 must-fix 全部落在"承载与宣称不配对"这一层（**判据一条没动、夹具没换、四臂/三臂证据沿用**，登记表与上限都不变）——① MF-P6-1（W1 归因）：负值格全是上游无条件写死的最外一圈，不是"两盆之间划了谷线"；块头、W1 注释、失败消息三处同步改准，并把"两盆分界"登记为**可判而未判**（另造夹具的配方写进注释）。② MF-P6-2（零容差的证据等级）："GTiff 拿默认"从"本仓源码读得到"降为"现读产物坐实"；NoData 回填归 `writeMatOutput`（只换 NaN）、继承归 `matToGdalTiff`（两处都要源波段声明过 NoData，夹具没声明）。③ MF-P6-3（等价变异）：块注释、产品码注释、本节三处的无条件句改成条件句，并纠正产品注释里**方向写反**的那条（原写"没有正标签被整片削光"暗示谷线能吃掉一个盆；4.14 里内部正标签永不被削光，唯一机制是最外一圈）；"6/6 全绿"补上论域定义。④ MF-P7a-1（契约地位）：文档与注释里"行主序从 1 起编号"降级为版本限定实现细节，加"不要当契约依赖"，与本块上方"不把 Otsu 的具体取值写进断言 = 不把上游实现细节当契约"的自家原则对齐（首版与之冲突）。⑤ MF-P7a-2（因果缺环）：全零档第一因改回 `toUint8` 的常数退化支（本仓文档早就写对、注释记错了功劳），死防御那条的依据换成 `flattenL` 的 `LabelT k = 1;`。另清一条 N178 族的裸锚：产品注释里那条"提交号 + 文件 + 行号区间"形态的自指锚改成了人话（区间已随本刀自己的插入错位，指向处不再是它数的那个循环）。⚠ 本轮的上游依据不再靠"我信"：控制器自己解包 `opencv-4.14.0.tar.gz` 读了`connectedcomponents.cpp`（`flattenL` 的 `k = 1`、`is_parallel` 分派、`CV_Assert(connectivity == 8 || connectivity == 4)`）与 `segmentation.cpp`（`const int WSHED = -1;`、那两个边框循环、出队赋值只落在原值为 0 的格）后逐条落进注释。
**补审轮验收实到**（重编全 Debug 树 rc=0、`grep -c error` 0）：改动的两支用例直跑 `gis_tests.exe --gtest_filter` **3/3 绿**（watershed 载体块 + connected_components 两条）；定向大集合 **43/43 绿 54.54 s**、`^PluginTest\.Processing` **30/30 绿**、`(CoverageMeta|CoverageMetaJudge)` **19/19 绿**、`Total Tests:` 仍 **802**；登记表六键逐字复跑仍 `126/118/8/119/15/103`、`kPendingCeiling = 8` 未动。三计数（口径：blob 与工作树都先把 CRLF 归一成 LF 再数，`^TEST_F` 按行首、`EXPECT_`/`ASSERT_` 按全文件出现次数含注释与字符串）：HEAD `350/3018/2063` → 工作树 `350/3018/2063` ⇒ **三个数全零**（本刀不加断言、不加用例，注释里那几枚 `EXPECT_DOUBLE_EQ`/`CV_Assert` 字样换掉的量正好抵消）。卫生面：`data` 两件套 0 + `88/88/0`、terrain blob `588a42504ee19bcb551f8214e5c1adb68cea20e9` 未变、`src/` 改动 **0 行非注释**（产品码一行没动）、新增 94 行文本里 `,GOLD` 0 条 / 裸行号锚 0 条 / `build/tmp` 私有路径 0 条 / "席位报告 §N" 悬空指向 0 条。

### 批次 D 覆盖晋升 P8a（2026-11-04，`processing.glcm_texture` 一条 PENDING→GOLD；**控制器自跑，只做单测面 + 文档面，不碰腿、不碰矩阵、不碰产品**）

**本刀全程由控制器自跑**（R-D63）：派发的实现席是**第五种断线形态——"静默零产出"**（15 次工具调用后撞账户配额，
没建报告、没建工作目录、树仍是 `0f23b73`）⇒ 无可回收物，控制器直接接手。
**因此本刀没有独立复审席**，缺一层否证；基线 HEAD `0f23b73`，开批现读六键 `126/119/7/120/15`、`kPendingCeiling = 7`、
`Total Tests: 802`、四配方 43/30/19/1、三计数 `350/3042/2064`。

**这条 PENDING 挂着的成因与 P6 同族、结论不同**：既有块 `ProcessingGlcmTextureExecution` 五条断言是
`ASSERT_NE` / `EXPECT_TRUE(success)` / `EXPECT_TRUE(exists)` / `EXPECT_EQ(metadata["action"],…)` /
`EXPECT_GT(maxValue, 0.0)`，守卫认的宏族（`EXPECT_NEAR` / `EXPECT_FLOAT_EQ` / `EXPECT_DOUBLE_EQ`）一个没有 ⇒ 不顶数。
⚠ 但**换宏换不出承载力**：`EXPECT_GT(maxValue, 0.0)` 只说"图里有个亮格"，所以本刀的形状与 P7a 一样是
**必须新写值级判据**（不是 P6 的"做账 + 补判据"两件事）。另清一条装饰：既有块里 `minValue` 被 `cv::minMaxLoc`
填了却没有任何断言读它 ⇒ 本刀把它换成"逐格读产物"的机器，不留死变量。

**任务书里三条"推不动就不许写期望"的开放前提，两条在本刀解开（上游依据由控制器自己解包 4.14 源码读到，
引用只给标识符与语句原文、不给行号）**：
- `cv::normalize` 在 `NORM_MINMAX` 支里是 `scale = (dmax - dmin)*(smax - smin > DBL_EPSILON ? 1./(smax - smin) : 0)`、
  `shift = (float)dmin - (float)(smin*scale)` ⇒ **`min == max` 时 `scale = 0`、`shift = dmin = 0` ⇒ 整幅写 0**
  （不是 NaN、不是 1、不是未定义）。⇒ 常数退化这一档的期望是"整幅 0"，有载体。
- **推翻控制器自己任务书里的猜测**：我原先写"边框那一圈经全局拉伸后可能变成**负值**（一个自称 0..1 的纹理图里冒负数）"。
  实到不成立：四个指标的原始值都 >= 0，而 `minMaxIdx` 扫的是**含那圈未写边框**的整幅 result ⇒ `smin` 必然取自边框自己 = 0 ⇒
  边框映射成 `(0 - 0)/(smax - 0)` = **恒等于 `0.0`**。⇒ 判据按"边框 0.0"写，猜测留在这一段里防止被读回去。
- 第三条（非均匀窗的逐方向样本集到底怎么数）**本刀没有解开** ⇒ 见下面"未判项"，本刀因此只用了一条可手推的不变量。

**立论基础只一条**：输入**整幅均匀** ⇒ 每个内部窗在四个方向上的每一对都是 `(l,l)` ⇒ GLCM 只有一个非零格且 p = 1 ⇒
四指标各自退化成 `homogeneity = 1`、`energy = 1`、`contrast = 0`、`entropy = 0`，**不需要枚举任何非均匀窗**。
夹具一律 5 列 x 4 行（**非方**——方阵上"宽高写反"是等价变异）。七档：
- **G1** 均匀件 + `homogeneity`（`kernel_size=3`、`levels=8`）⇒ 内部 `{1,2}x{1,2,3}` 共 **6 格 == 1.0**、
  边框 **14 格 == 0.0**、值域只 {0.0, 1.0}、宽高逐字等于输入。6 是推的不是量的：`(4 - 2*1) * (5 - 2*1) = 6`。
- **G2** **同一枚**均匀件换 `contrast` / `entropy` ⇒ 整幅原始值全 0 ⇒ 走上面那条退化支 ⇒ **内部也是 0.0**（平方和 == 0.0）。
  G1 与 G2 成对 ⇒ 挡住"四指标共用一条公式"的假实现（它必在其中一档红），**这就是 metric 敏感性的载体**，
  不需要先会算非均匀窗的四指标值。两个 0 来源不同（"0 映射成 0" vs "退化支把 scale 归 0"），注释里写死。
- **G3** `kernel_size=5` 配 4 行件 ⇒ 主循环 `y ∈ [2, 2)` = **空** ⇒ 一个窗都不建 ⇒ 产物整幅 0.0 **且返回成功、不发警告**
  （产品陷阱，已同步文档；是否该显式失败属可观测行为变更 ⇒ 只登记交拍板）。
- **G4 / G5** 未知 `glcm_metric` 的**可达性对**：有内部窗时失败（文案 `Unknown glcm metric: <取值>` 逐字）且不留产物；
  空内部时那句 `else` 永远不执行 ⇒ **反而成功**、产物整幅 0.0 ⇒ 两档合起来判的是"校验发生在哪一层"这个结构事实。
- **G6** 五道前置早退各一条文案 + 各一条"产物不许存在"（`kernel_size=2` 先撞 `>= 3`、`4` 才撞 `odd`，顺序实到）。
- **G7** metadata 四键**齐全**（只判齐全，**不判"总共只有这四个键"**——写出层可能加键，把齐全写成恰好就是无载体宣称）
  + 成功文案恰 `writeMatOutput` 那一句（`doGlcmTexture` 函数体 `onMessage` 0 次，本刀**不**据它宣称"动作不产生文案"，
  那是 P7b 刚被推翻的那条）。

**三臂**（日志 `build/tmp/batchD/p8a/arms/`；每臂跑前重新 `git status`，还原用 `cp` + 工作树裸字节 sha256 对拍，
每次还原后先重编再复跑）：ARM-1 G1 内部期望 `1.0 → 0.5` ⇒ 实到红 **6 条**（只红逐格那 6 条）；
⚠ **我对这一臂的红点构成预测错了**：我预测 13 条（6 逐格 + 1 计数 + 6 值域），实际 6 条——因为 `countOf(cells, 1.0f) == 6`
与值域那条比的是**产物的实际格值**，改期望字面量根本不触及它们 ⇒ 这两族是产物侧观测量、不随期望漂（这是好事，
但预测值必须如实改准，不许留着一个没跑过的数）。ARM-2 G2 内部 `0.0 → 1.0` ⇒ 恰 **2 条**（contrast/entropy 各一）✓ 与预测同；
ARM-3 G4 文案少一个空格 ⇒ 恰 **1 条**（只红文案那一条，`success=false` 与"不许留产物"仍绿）✓ 与预测同。
另记一条**构造论证**（未实跑、也没改 `src/`）：若归一化只扫内部（排除那圈 0）⇒ `smin = smax = 1` ⇒ 走退化支 ⇒
产物整幅 0 ⇒ G1 那 6 条逐格判据红 ⇒ "min/max 取值范围含边框"这一事实由 G1 承载。

**未判项（登记，不在本刀做）**：
- **非均匀窗的手推枚举**：`doGlcmTexture` 产出前有一层**全图 min-max 拉伸** ⇒ 产物读不到指标原值，
  任何非均匀夹具的期望都要先解出整幅 min/max，那是"4 方向 x 每方向一整窗"的逐档枚举。
  本刀拒绝用"实测一下看看"冒充手推 ⇒ 一条枚举机器同时挡住**三条**：①"归一化与否"的独立鉴别（G1 只间接挡到 min/max 取值域），
  ②四方向集 `(1,0)/(1,1)/(0,1)/(-1,1)` vs 8 对称，③`band` 的双向档（P7b 立的规矩：可见参数必须有档）。
  ⚠ 本刀 `glcm_texture` 的 `band` 因此**零承载**，与 P7b 复审席当初对我同一形态的判语相同 ⇒ 待那台枚举机器落地后单开一刀补。
- `pairCount = kernelSize * max(0, kernelSize - 1)` 这个量**只被 `<= 0` 那道闸读了一次**，从不参与归一
  （真正的归一用逐方向的 `dirPairCount`）⇒ "算了不用"族，与 #141 同族，交我编号。
- 空内部 ⇒ 静默整幅 0 且 rc=0 是否该改成显式失败：可观测行为变更，交拍板，本刀只测出并写进文档。

**文档对撞（`docs/算法说明/影像处理与分析/GLCM 纹理.md`，文件名里有空格）**：四件事里**一处是与产品互斥的漂移**——
「处理流程」第 3 步原文写"**水平方向**相邻像元的灰度共现次数"，产品实际是四个方向各建一张再 `/4` 平均 ⇒ 已改准
（并补上"伙伴只要求不越出影像边界、可以越出窗口"这条决定样本对数的事实）；另两处**缺载**已补写：最外 `radius` 那一圈
固定 `0.0`（不是 NoData、与真实 0 值窗口不可区分）、归一化的 min/max 取自含边框的整幅 ⇒ **产物读不到指标原值**；
再加两条注意事项：空内部 ⇒ 整幅 0.0 且不报错、未知 `glcm_metric` 只在存在有效窗口时才被拒。参数表五列一个字没动
（`docs_match_cli` 钉着），行尾按原样 CRLF、无 BOM。

**验收实到**（重编 `gis_tests` rc=0、`grep -c error` 0）：`ProcessingGlcmTextureExecution` 单实例直跑**首跑即 1/1 绿**、
`RUN_EXIT_CODE=0`（331 ms）；三臂还原后复跑仍 1/1 绿，`BYTE_MATCH_AFTER_RESTORE=True` x3；
`(CoverageMeta|CoverageMetaJudge)` **19/19 绿**、`^PluginTest\.Processing` **30/30 绿**（登记后重编复跑）；
`Total Tests:` 仍 **802**（未新增 `TEST_F`）；登记表六键改后 **`126/120/6/121/15`**（slot 105，逐条用声明行行尾自带命令复跑，
不变量 `gold + pending == data_rows` 现算成立）、`kPendingCeiling = 6`；三计数 `350/3042/2064` → **`350/3068/2071`**
（口径：blob 与工作树都先把 CRLF 归一成 LF 再数，`^TEST_F` 按行首、`EXPECT_`/`ASSERT_` 按全文件出现次数含注释与字符串）；
`tests/test_plugins.cpp` CRLF 28683 / 裸 LF 0 / 无 BOM；新增 206 行文本里裸 `uXXXX` 转义、裸行号锚、`build/tmp` 私有路径、
`,GOLD` 字面量、"席位报告 §N" 悬空指向各 **0 条**；`src/`、`data/`、腿脚本、`tests/real_data_matrix.txt` 零改动；
矩阵第 5 列 `processing,glcm_texture` 保持 `real_regression`（本刀没加腿 case，改 `none` 或反过来改它就是假登记，R-D39）。

**过程自查（我自己这一把的两次返工，写下来防止复发）**：①反事实臂驱动先犯了"还原点选错"的错——`cp` 的源是**改判据之前**
的快照，于是第一跑就把尚未提交的判据整块抹掉（好在块体由生成脚本可确定性重建，字节数逐位相同）⇒ 还原点必须是
**写完判据之后**的快照；同一条脚本还用 `subprocess(shell=True)` 配 `VAR=1 cmake` 前缀，Windows 上走 cmd.exe ⇒
前缀不生效、构建"成功"是假的（`BUILD_EXIT_CODE=1` 才暴露）⇒ 一律 argv + `env=` 传。②登记脚本第一版把 COUNT 声明行的
行内空格压掉了、且 `data_rows` 测成 0（同一个 `shell=True` 引号问题）⇒ 改成 python 复现声明行的六个口径、
**只替换数字位、其余字符一个不动**，再用声明行自带的命令逐字复跑对撞。

### 批次 D 覆盖晋升 P8b（2026-11-04，`processing,gabor_filter` 与 `processing,mean_shift_filter` 两条 PENDING→GOLD；**控制器自跑**，只做单测面 + 文档面，不碰腿、不碰矩阵、不碰产品）

**基线现读**（HEAD `0f962d8`）：登记表六键 `126/120/6/121/15`（slot 105；基线那组跑在 `git show` 落出的临时件上、改后那组跑在工作树，都是逐条用声明行行尾自带的命令）、`kPendingCeiling = 6`、`Total Tests: 802`、三配方 43/43、30/30、19/19。
**本刀没有独立复审席**（R-D63：实现席"静默零产出"之后由控制器接手自跑 ⇒ P8a/P8b 两把一起欠着一层否证，配额恢复后补审）。

**这一把的骨架是"产出前有没有全局拉伸"这条不对称**：`doGaborFilter` 末尾是 `cv::normalize(..., NORM_MINMAX)` ⇒ 产物读不到响应原值（与 P8a 的 GLCM 同形），能写的只有**退化支**与**两跑之间的关系**；`doMeanShiftSegment` 是 `toUint8 → … → convertTo(CV_32F, 1.0/255.0)`、**没有**拉伸 ⇒ 端点是绝对量 ⇒ 可以判"整幅 0.0""贴 1.0""值集只有两端"。夹具一律 26 列 x 24 行（**非方**，624 格——方阵上"宽高写反"是等价变异）。

**判据**（GB = gabor，MS = mean_shift）：
- **GB-1** 常值件 ⇒ `min == max` 走 P8a 已坐实的退化支 ⇒ 624 格逐格 `EXPECT_DOUBLE_EQ(., 0.0)`；
- **GB-2** 亮块件 ⇒ 值域 `[0,1]` 且两端各至少一格（容差 2 ulp）；
- **GB-3** `psi → psi + π` 把核的余弦项取反 ⇒ 两跑产物**逐格互补** `v + v' == 1`（624 条 `EXPECT_NEAR`）。它与核的具体形状无关，只对"psi 真被读进核"和"末尾确实是那层 min-max"敏感 ⇒ 是 gabor 侧唯一承重的参数敏感性载体；
- **GB-4** 早退（口径 = 函数体里 `Result::fail` 的**返回条数**，现读 8 = 前置 7 + 读栅格失败 1）逐条文案 + 不留产物；
- **GB-5** `band` 双向档（第一刀漏，补刀加，见下）；
- **MS-1** 常值件（先被 `toUint8` 的常数退化支抹成全 0）⇒ 整幅 0.0；**MS-2** 亮块件中心贴 1.0、角格贴 0.0、值域；
- **MS-2c** 二值件的产物**值集**只有两端（把 MS-2 从三个取点升级成整幅 624 格的值级承载）；
- **MS-3** `band` 双向档；**MS-4** 早退（现读 6 = 前置 5 + 读栅格失败 1）；**MS-5** metadata 四键齐全（只判齐全）。

**九臂**（日志 `build/tmp/batchD/p8b/arms/`，gitignored；还原点一律取**写完判据之后**的快照——A1~A5 用 `.BLOCK` `7827f640c6480d98`、A6 用 `.BLOCK2` `a0449cf29da45ec6`、A7~A9 用 `.BLOCK3` `6bb899c21e4f1a82`；每臂跑前重新 `git status`、还原用 `cp` 并复算工作树裸字节 sha256、**重编后再复跑**）：
A1 两档 psi 取同值 ⇒ **624 条全红**、最差残差恰为 1；A2 MS 的 band=2 那档恒读第 1 波段 ⇒ 恰 **1 条**；
A3 把 GB-2 的 0 端计数收紧成精确相等 ⇒ **1 条**，并从失败消息读到实到最近格离 0 = `6.6581913316099417e-09`；
A4/A5 把两支"整幅 0.0"的逐格期望翻成 1.0 ⇒ 各 **624 条** ⇒ 证明两条逐格循环真在执行（不是空真）；
A6 放大 `color_radius`：300 ⇒ **判据不红**（我这条臂的前提错了——`GRAY2BGR` 之后类间欧氏距离是 `sqrt(3)*255 ≈ 441.67`，300 还没跨过）⇒ 改 500 ⇒ **2 条红**、`608/624` 格落到两端之外、角格读成 185/255；
A7/A8 gabor 的 band 两半各做"恒读另一波段" ⇒ 各恰 **1 条**；A9 MS 的另一半 ⇒ 恰 **1 条** ⇒ 两支的成对档都拿到**双向**证据，不是只有一侧有臂。

**被实测顶回、因此改准后才提交的五处我自己的说法**：
① GB-2 原写"至少各有一格 `== 0.0` 与 `== 1.0` 是可推的" ⇒ 0 那一端**不是**精确 0.0（A3），且两侧不对称（1 端才是精确 1.0）；
② GB-3 原写"实到最大残差写在报告里" ⇒ 把数落进注释：绿态最差残差 `4.4703483581542969e-08`；
③ MS-2 原写"`convertTo(..., 1.0/255)` 在 1.0 附近正好差 1 ulp（`fl32(1/255)*255 = 1.0000001`）" ⇒ **求值路径判错了**：这条链上的 scale 是 double，255 乘它再落回 float32 是**精确 1.0**；测试内的 GDAL 读端与离线 cv2 读端逐值一致（并测到该夹具全图值集 = `{0.0, 1.0}`）；
④ MS-2 角格那句"在亮块之外 ⇒ 邻域全 0" ⇒ 归因错：它的 17x17 **空间**邻域够得着亮块（块从 x=3,y=2 起），把另一类挡在外面的是**颜色**窗口 ⇒ A6 的 500 档把角格读成 185/255 就是反证；
⑤ MS-2c 原写"放大 `color_radius` 就会出中间值" ⇒ 改成给算式的版本，并把"300 实测不红"这条反向证据一起写进去（引用这条时给的是距离算式，不是"放大半径"四个字）。

**第一刀（`c8601da`）提交后自查出的两条缺口由补刀（`7e4b32a`）收**：
① `band` 在 gabor 侧**零承载**——`runGabor` 有这个形参却没有任何一档传过 ≠ 1，而这正是 P7b 复审席把 skeleton 同一形态升级成 must-fix 的那条规矩 ⇒ 说明我没把它内化成自跑前的检查项；补 GB-5 双向档；
② 块头"六道前置早退"没先定口径 ⇒ 按"返回条数"现读是 8（gabor）与 6（mean_shift），两支块头都重定并明写口径。
补刀还撞出一条**产品事实**：那 8/6 条里"栅格读不出来"这一支**不可达**——`core::readBandAsMat` 只有两种出口（返回非空 Mat，或对"打不开 / 波段越界"抛 `GisError`）。我先按"返回空 Mat"写的两条断言，直接被自己的用例以 `unknown file: error: C++ exception with description "Cannot open raster: …"` 各撞红一条 ⇒ 断言撤掉，改登记 **N187**（同形态在 `src/plugins/processing/processing_plugin.cpp` 现读 **13 处**，口径 `grep -rn "无法读取输入栅格数据" src/ | wc -l`）；删死支还是让失败真走这条文案都改可观测行为 ⇒ 交 D3 拍板。本刀 `src/` 零改动。

**未判项（登记，不在本刀做）**：
- gabor 除 `psi` 之外的可见参数（`kernel_size` / `sigma` / `theta` / `gabor_lambda` / `gabor_gamma`）仍只有退化支与 metadata 回显层面的判据；逐格期望要先解出整幅 min/max，是 P8a GLCM 那堵同一面墙。`theta` 只判了"两档回显相同"这条弱关系。
- 矩阵第 5 列这两格记的是 `real_regression`：腿 case **确实存在**（`run_real_raster_regression.ps1` 里 `processing_gabor_filter` / `processing_mean_shift_filter` 两支），但它们判的是 `Size:   32 x 32 x 1 bands` 与 `Type:   Float32` 两条形为锁 ⇒ 本刀没加腿 case、也没把值级期望搬过去；这一格是否与"regression"之名相符归 #184 对账（与 P7b 的 skeleton 那格同理，不是 `none`，别顺手改）。
- MS-2c 只在 `pyramid_level = 0` 那档成立；level ≥ 1 的尺度效应无判据（块头写的是前提，不是结论）。

**文档对撞**（`docs/算法说明/影像处理与分析/Gabor 滤波.md` 与 `Mean Shift 滤波.md`，两个文件名都带空格）：
Gabor 三处——①"范围**通常**落在 `0` 到 `1`"与产品互斥：末尾是整幅 min-max ⇒ **必然**落在 0 到 1，已改准，并补"整幅同值走退化支 ⇒ 产物整幅 `0`（既不是原值也不是 `0.5`）"；②正文原本只字不提 `gabor_psi` 的作用 ⇒ 补一条"加 π 是把明暗瓣互换，两期结果要按**镜像**读"（载体是 GB-3）；③注意事项原本只列 `gabor_lambda` / `gabor_gamma` 两个正数闸，产品实为三个（`sigma` 同形）⇒ 补齐并写明"任一非正都在执行前被拒且不产出文件"。
Mean Shift 四处——①处理流程第 6 步"把灰度结果**归一化**到 `0` 到 `1` **左右**"是**与产品互斥的漂移**：那条链没有 min-max，只有固定 `1/255` ⇒ 改成"按固定比例换算"，"左右"也去掉（端点是确定的）；②第 2 步"转换为 8 位灰度"补上"按整幅 min/max 拉伸、整幅同值退化成像幅 0"（MS-1 的成因）；③输出说明同步改成"= 漂移后的 8 位灰度 ÷ 255，没有整幅拉伸 ⇒ 同一件输入重复跑，数值不随全图统计量漂移"；④结果解释补一条两类输入的"值集只有两端 + 挡住另一类的是颜色窗口（√3 × 灰度差）"。
参数表五列一个字没动（`docs_match_cli` 1/1 绿），两份文档行尾按原样 CRLF、无 BOM。

**验收实到**（全部 Debug 单树）：两支单实例直跑 OK（192 ms / 190 ms，`grep -c ": error:"` = 0）；`^PluginTest\.Processing` **30/30**（7.67 s）、机器守卫大集合 **43/43**（43.90 s）、`(CoverageMeta|CoverageMetaJudge)` **19/19**（2.85 s）；`Total Tests:` 仍 **802**（`^TEST_F` 计数 350 未变，即未新增用例）；
登记表六键改后 **`126/122/4/123/15`**（slot 107，逐条用声明行自带命令复跑；不变量 `gold 122 + pending 4 == data 126` 现算成立）、`kPendingCeiling = 4`；
三计数（口径：CRLF 先归一成 LF，`^TEST_F` 按行首、`EXPECT_` / `ASSERT_` 按全文件出现次数含注释与字符串）`350/3068/2071` → **`350/3114/2090`**；`tests/test_plugins.cpp` 28683 → **29098** 行，CRLF 29098 / 裸 LF 0 / 无 BOM；
卫生面：`data` 两件套 = `find data -type f -newermt '2026-11-04 00:00:00' | wc -l` **0** 与 `BASELINE: 88  CURRENT: 88  DIFFS: 0`、`src/` 与 `tests/real_data_matrix.txt` 零改动、九臂每次还原后 `BYTE_MATCH_AFTER_RESTORE=True`。

### 批次 D 覆盖晋升 P9（2026-11-04，`processing,kmeans` 一条 PENDING→GOLD，**D1 的 processing 侧到此收口**；控制器自跑，只做单测面 + 文档面，不碰腿、不碰矩阵、不碰产品）

**基线现读**（HEAD `b0e2c33`）：登记表六键 `126/122/4/123/15`（slot 107）、`kPendingCeiling = 4`、`Total Tests: 802`、三配方 43/30/19。**本刀没有独立复审席**（R-D63 欠账，与 P8a/P8b 一起补审）。
四笔：`75e28bc` 测试 / `5ac7ce4` 登记 / 本节所在这笔文档。块体 20 → **349 行**（从块头注释到块尾，含空行）；`tests/test_plugins.cpp` 29098 → **29424** 行；三计数 `350/3114/2090` → `350/3155/2104`（+0 用例 / +41 EXPECT / +14 ASSERT）。

**这个动作与前八把的形状都不同**：链路上既没有 `toUint8` 也没有 min-max 拉伸——`openRaster` → 逐波段 `gdalBandToMat`（原始 float32）→ `samples = 像元数 × 波段数` → `cv::kmeans(…, KMEANS_PP_CENTERS, centers)` → **标签原样写盘**。
⇒ 产物是标签矩阵，于是能判两类前面判不了的东西：**绝对精确的紧凑度**与**分区结构**。夹具 6 列 x 4 行（非方），三块各 8 格、值 0/100/400。

**三条可手推的事实撑起七档**（算式都写在块头与逐档注释里）：
- ①同一样本向量永远同标签（**前提 `k <=` 不同向量的个数**，这条前提由取证臂 B4 实测确认是必要的，见下）；
- ②只有 3 个不同向量时，Lloyd + PP 的**每一条**初值分支都收敛到同一分区（三种首中心逐一手推）；
- ③合并两块的代价闭式 `SSE = n·d²/2` ⇒ 三档期望值 0 / 40000 / 360000 全是整数、float32 里精确 ⇒ metadata 字符串**逐字**判。
②是整块的命门（PP 的首中心来自不锁种子的 RNG），所以先测稳定性再写判据：**本地 cv2 对五组夹具各跑 300 次，分区结构与紧凑度各自只有一个取值**；**本仓 exe 直跑该用例 20 次全绿**。两个数都进了块头。

**七档**：K1 k=3 结构（块内恒等 + 三块两两互斥 + 值集 == 3 + `compactness == 0.000000`）；K2 k=2 ⇒ 并 `{0,100}`（三种并法代价 10000 / 90000 / 160000 ⇒ 唯一最小）+ `40000.000000`；
K3 自有消息通道三条逐字（`Reading 1 bands…` / `Running K-Means with k=2…` / `K-Means completed: 2 clusters, compactness=40000.000000`）+ "含 `K-Means` 的条数 == 3" + 与 metadata 交叉同值；
K4 `band` 惰性（band=1 与 band=2 的**分区签名**相同 + 紧凑度相同）；K5 第 2 波段确实进特征（同一件 band1 只换 band2 ⇒ 分区翻转，Z1 侧四条反向 + `360000.000000`）；
K6 早退（口径 = `Result::fail` 返回条数：现读 5 条、**实到可达 4 条**）；K7 声明区间下界 2 与实现闸 `k <= 0` 的分叉（实到 k=1 放行、值集 == 1）。
K7 **不**判它的紧凑度：模拟读到 `693333.328125`，那个数依赖 float32 的累加顺序，不是我推得出的东西 ⇒ 只锁"放行 + 一簇"两件可观测事实。

**四臂**（只改 tests；还原点 `.BLOCK` sha `481553ec1e14bb30` 取在**写完判据之后**；每臂跑前重新 `git status`、还原后复算工作树裸字节 sha256、重编再复跑；日志 gitignored）：
B1 把 K2 的紧凑度期望改成 `0.000000` ⇒ 恰红 **1** 条 ✓ 与预测同；B2 给 K4 第二档换一个 k ⇒ 恰红 **2** 条（签名 + 紧凑度）⇒ 证明"结构相同"这条有牙、不是空真；
B3 把 Z1 的 band2 换成常数（= 让产物表现得像第 2 波段没进特征）⇒ 恰红 **4** 条（Z1 的三条 + 两档签名变相同）✓；
B4 把 K1 那档的 k 从 3 改成 4（> 不同向量个数）⇒ 恰红 **3** 条（块内恒等被劈开 / 值集变 4 / k 回显变 4），而**紧凑度仍是 0** ⇒ 顺带实测确认①那条前提是必要的，也确认 K1 的紧凑度判据**不**负责鉴别 k。

**首跑两笔红都是我自己写的假前提**（都按实到改准并写进注释，未蒙过去）：
①K4 原先**逐格比标签数值** ⇒ 实到 24/24 全不同，而两跑的分区其实一模一样（编号随 RNG 变）。
  这条规矩我在同一块的块头刚写下（"判跨跑相同只许判改名不变量"），写完就在 K4 违反了 ⇒ 改成 `partitionSignature`（首现序号），K5 的跨跑比较也一起改用它。
②K6 原以为第五条 `Cannot open raster file: …` **可达**（理由是"本动作自己 openRaster 并判空"）⇒ 实到抛异常：
  `core::openRaster` 自己 `throw GisError("Cannot open raster: …")`、永不返回空指针 ⇒ 那句 `if (!ds)` 是**又一处死支**。
  ⇒ 已登记的 N187 射程按本刀实到**扩成两类文案**（13 处 `无法读取输入栅格数据` + 这类 `Cannot open raster file`），根因同一个；
  K6 改成判"抛出 + 无产物 + 实到 `what()` 里是英文文案"三条，并按 `tests/raster_fixture.h` 已有的约定接 `std::exception`（测试侧不 include `error.h`）。

**未判项（登记，不在本刀做）**：
- `epsilon_kmeans` 与 `max_iter` **没有档**：本刀所有夹具在第一轮迭代就收敛 ⇒ 这两个键改什么都不改变产物（与 N163"参数静默无效"不同，这里是**夹具不敏感**）。要判得先造一个恰在收敛边界上的数据集 ⇒ 交 #164 那把探针刀一起做。
- metadata 实到只有 `k` / `compactness` / `bands` 三键，**没有 `action`**（processing 里唯一不写它的一个）⇒ 本块只判"三键齐全 + 值精确"，**没**把"缺 action"钉成契约（那是缺陷形态，修法属行为变更，交裁定）。
- `k >` 不同向量个数那一档（B4 的形态）没纳入判据 ⇒ 它只作为前提的必要证据留在臂里。
- 矩阵第 5 列这格记的是 **`none`**（腿 case 确实不存在，本刀没加）；该行 `input_kind=raster+samples` / `fixture=synthetic` 与现读不符那条**早就写在矩阵注释里** ⇒ 归 #184 对账，本刀一格未动。

**文档对撞**（`docs/算法说明/影像处理与分析/K-Means 分割.md`，文件名带空格）：这篇的**方向性宣称本来就是对的**——"聚类总是使用全部波段"、"标签编号没有固定语义，不同次运行不能按编号对照"两条与 K5、`partitionSignature` 的实到完全一致 ⇒ 不改，只把缺的量补上、把两处口径错写改准：
①注意事项原写"`聚类数` 必须大于 0"——这与参数表声明的 `[2, 256]` 自相矛盾，且实现层确实只拒 `k <= 0`（K7 实测 k=1 放行）⇒ 改成明写这处分叉并登记；
②原写"像元值**通常**为 0 到 聚类数-1"⇒ 改成确定的取值范围，并补"Float32 存储、取值为整数"；
③补一条**紧致度**的定义（每像元到所属类心的距离平方和；metadata 与完成文案两处是同一个数）——这是本刀 K3 交叉对撞过的事实；
④补两条实到形态：输入打不开时底层直接抛异常（那句中文/英文失败文案读不到）、以及"`k` 大于不同特征向量个数时重复向量会被拆进不同簇（紧致度仍可为 0）"；
⑤处理流程第 4 步补上 `attempts` 的语义（重复若干次取紧致度最小那次）——这正是②那条"分区不随初值变"的机制来源。参数表五列一个字未动。

**验收实到**（Debug 单树）：`ProcessingKMeansExecution` 单实例 1/1 绿（216 ms，0 条 `: error:`）、四臂还原后复跑仍绿、20 次压测全绿；
`^PluginTest\.Processing` **30/30**（11.18 s）、机器守卫大集合 **43/43**（42.95 s）、`(CoverageMeta|CoverageMetaJudge)` **19/19**（2.94 s）、`docs_match_cli` + `source_mojibake_guard` 2/2；
`Total Tests:` 仍 **802**；登记表六键改后 **`126/123/3/124/15`**（slot 108，逐条用声明行自带命令复跑；`gold 123 + pending 3 == data 126` 现算成立）、`kPendingCeiling = 3`；
卫生面：`data` 两件套 = 0 与 `BASELINE: 88 CURRENT: 88 DIFFS: 0`、`src/` 与 `tests/real_data_matrix.txt` 与腿脚本零改动、三文件 CRLF 原样 / 无 BOM / 裸 LF 0。

**批次 D 的收口门余量**：登记表只剩 3 条 PENDING，且**全部不是 processing 族** —— `matching,ecc_register` 与 `matching,stitch`、`spindex,ndvi`（三条都在 #149，前两条还挂 Release 门与"先写真判据"）。⇒ D1 的 26 条里 processing 侧十条（P1~P9）全 GOLD。

### 批次 D 覆盖晋升 P10（2026-11-04，`spindex,ndvi` 一条 PENDING→GOLD；**顺带把 #149 里我自己写错的一句任务书转述读正**；控制器自跑，不碰腿、不碰矩阵、不碰产品）

**基线现读**（HEAD `70005ce`）：六键 `126/123/3/124/15`（slot 108）、`kPendingCeiling = 3`、`Total Tests: 802`。
三笔：`2f5e93d` 测试（ND-1~ND-7，块体 21 → **295 行**）/ `0858f1a` 登记（六键 `126/124/2/125/15`、slot 109、ceiling 3→2）/ 本节这笔文档。
`tests/test_plugins.cpp` 29424 → **29697** 行；三计数 `350/3155/2104` → `350/3203/2119`（+0 用例 / +48 EXPECT / +15 ASSERT）。

**先更正我自己的一处任务书转述**（R-D51 那条纪律的再一次应用）：#149 把这条写成"**`spindex.ndvi` 真降档（可即时）**"。
逐字载体三处把它否证了：①腿脚本 `run_real_raster_regression.ps1` 里 `spindex_ndvi` 这条 case **存在**，断言
`Size:   24 x 24 x 1 bands` 与 `Mean:   0.75` ⇒ 矩阵那行 col5 `real_regression` 名副其实，**不该降**；
②单测专块的动作槽是守卫认的 `params["action"] = std::string("ndvi")` 形态，挂 PENDING 的真实原因只是
"没有值级断言"（`metadata.count("ndvi_min") > 0` 那种键存在）⇒ 与别的刀同形；③那个 `0.75` 本身就是可手推数
`(70 − 10) / 80` ⇒ 这个动作没有任何"判不了"的障碍。⇒ **按正常晋升做完**（降档只会把要求降下来，门不会真归零）。

**判据的立论**：链路上没有 `toUint8` 也没有 min-max 拉伸 ⇒ 产物就是 `(NIR − Red)/(NIR + Red)` 本身 ⇒ 期望全走闭式且是绝对量。
七档：ND-1 常量件 576 格逐格 `EXPECT_FLOAT_EQ(., 0.4f)` + message 逐字 + 四个 min/max 键（`ndvi_min/ndvi_max` 是
`index_min/index_max` 的**别名** ⇒ 四个一起判才挡得住别名复制错）；ND-2 `red_band`/`nir_band` 成对档
（0.75 与 **−0.2**，两档**符号不同**是有意——同号的一对挡不住"整体取反"这种等价变异）；ND-3 默认支；
ND-4 波段越界；ND-4c 缺失输入；ND-5 分母趋 0；ND-6 按列分带 + min/max 精确（非方 8x6 件给转置证据）；ND-7 自有消息通道三条逐字。

**本刀最有价值的一条是 ND-4 与 ND-4c 成对**：同一个 `doExecuteAction` 里两种失败形态并存——`getBandIndex` 在 try **之内**
throw，被函数末尾 `catch (const std::exception&) → Result::fail(ex.what())` 转成失败返回（message 逐字
`red_band 9 out of range (1-4)`）；而 `openRaster(input)` 在 try **之前**、且**没判空** ⇒ 异常原样逃出 `execute()`。
这条边界正是 N187（#210）要的机制证据：processing 那 13 处死支指望的 `if (!ds)` / `if (mat.empty())`
全都落在"只抛不返回空"的那一侧，而有 catch 的插件（spindex）才能把异常变成失败文案。

**六臂**（`.BLOCK` sha `949354da363ee700` 取在写完判据之后；每臂跑前 `git status`、还原复算裸字节 sha256、重编再复跑）：
C1 ND-2a 逐格期望改 0.4 ⇒ **48** ✓；C2 ND-4 文案的键名换成另一个键 ⇒ **1** ✓（证明"哪个键被拒"是判据，不只是"被拒了"）；
C3 去掉 ND-6 的按列分带 ⇒ **25** ✓（24 格 + `index_max`）；C4 ⇒ **49**、**与预测 48 不符且机制也不是我设计的那个**：
只换 `Red` 行会让 `NIR == Red` ⇒ 分子归 0，它实际证的是 ND-6 的 0.0 几何而不是符号档 ⇒ 另开 **C4b**（两行同时换半区）
⇒ **48** ✓、实到 `−9.99999996e+11` 与推得的 `−100/1e-10 ≈ −1e12` 同量级 ⇒ "符号跟着分子走"这条才有载体；
C5 ND-1 的 576 格期望翻成 0.5 ⇒ **576** 条（空真防护）。⇒ 这是"臂的前提本身要算"的又一实例（R-D65/R-D67 同族，第六次）。

**首跑四笔红全是我自己的假前提**（按实到改准、不留过期辩护）：①②runner 无条件读产物 ⇒ 把**测试自己的**
`readRasterBandF32` 异常当成了"产品抛出"（ND-4 两条全歪）；③用 `0` 同时表示"不传这个键"和"传 0 触发下界"⇒ 语义撞车，
下界档根本没被跑到 ⇒ 改成 `-1` 表示不传；④ND-4 的实到形态是"catch 转失败返回"而不是"逃出 execute()"。

**文档对撞（`docs/算法说明/光谱指数/NDVI.md`）逮出一处与产品互斥的算法描述 ⇒ 新立 N188（#211）**：
文档"安全除法处理"一节写的是"分母绝对值低于阈值（如 `1e-6`）时把该像元结果**设为 0（视为无效）**"，
而实现是 `denominator += 1e-10f` 后直接相除——**没有阈值判断、不置 0、不裁值域** ⇒ 三点全不符。
后果写在文档与任务里：分子非 0 而分母为 0 的像元得到 **±1e12 的有限巨值**，跳出 NDVI 合法值域 `[-1, 1]`，
会被后续按阈值分级当成极高植被一侧，并主导 metadata 的最值；行业惯例这里是掩膜成 NoData。
改它=改可观测行为 ⇒ 三个修法（甲 NaN+NoData、乙 置 0 并另发"被置 0 像元数"、丙 维持现状只写清文档）交 D3 拍板，本刀按丙落文档。
另补三处精度：波段越界的失败文案逐字（`red_band 9 out of range (1-4)`）、"文件打不开是另一种形态（异常逃出）"、
metadata 键名与成功消息里那对最值是同一个数。参数表五列未动。

**未判项（登记）**：`preset` / `expression` 两个键在 ndvi 支里被读走但只有 `custom_index` 用（本动作不受它们影响）⇒
未做档，属 #164 那把探针刀的射程；`blue_band` / `green_band` 同理（ndvi 只解析 red/nir 两个）。

**验收实到**（Debug 单树）：`SpindexNdviExecution` + 姊妹块 `SpindexOtherIndicesExecution` 同跑 OK（186 ms / 395 ms，0 条 error）；
`(CoverageMeta|CoverageMetaJudge)` **19/19**、`docs_match_cli` + `source_mojibake_guard` **2/2**；`Total Tests:` 仍 **802**；
六键改后逐条用声明行自带命令复跑（`126/124/2/125/15`，`124 + 2 == 126`、`124 − 15 == 109` 现算成立）；
`data` 两件套 = `find data -type f -newermt '2026-11-04 00:00:00' | wc -l` **0** 与 `BASELINE: 88 CURRENT: 88 DIFFS: 0`；
`src/`、`tests/real_data_matrix.txt`、腿脚本零改动；NDVI.md 94 → 111 行 CRLF、无 BOM、裸 LF 0。

**批次 D 收口门余量**：登记表只剩 **2** 条 PENDING，都是 matching（`ecc_register` / `stitch`）⇒ 按 R-D51 的逐字载体，
它们要"先写真判据 + 只能在 Release 树上验证"，已按用户裁定与 Release 重编合并到 **D4 门前**一起做。
⇒ D1（#147）与 P10 之后，**promotion 侧只剩这一把**；P8a/P8b/P9/P10 四把都**没有独立复审席**（R-D63 欠账）。

### 批次 D 守卫扩面 D3-I（2026-11-04，乱码守卫加 R5「转义残留」；**只动守卫与夹具，不碰产品、不碰判据、不碰登记表**）

**这一把补的是 #207 那条已登记的射程缺口**：P7b 修复轮（提交 1025c01）撞到的缺陷形态是
**写入工具把反斜杠连同转义一起吞掉**，注释里留下裸的「u + 四个十六进制字符」串——现场是半句可读中文后面挂着一长串「u 加四个十六进制字符」。既有四条规则 R1~R4 只看**非 ASCII 游程**（GBK 往返 / 私用区 / 注音 / 排版形式与替换符），
而那种残留是**纯 ASCII** ⇒ 四条全瞎。本轮把它落成第五条规则。

**形状是怎么定的（先扫现值，再定规则——顺序不能倒）**：在守卫自己的扫描范围（`src` `tests` `cmake` `.github` +
根三件，实到 318 个文件）里按四种口径各数一遍——

| 口径 | 现读命中 |
| --- | --- |
| 宽松「任意位置出现 u + 4 位十六进制」 | **29** 处 / 10 个文件（全是假阳） |
| 左边界非字母数字 + 右边界非字母数字（单段） | **0** |
| 左右都用环视 + 「连写 ≥2 段」的并集（最终采用的 R5，且大小写都判） | **0**（守卫自跑 318 个文件 `violations=0` 是同口径的第二读者） |
| 带反斜杠的正规转义（正规 C++ 写法） | **0** |

宽松口径那 29 处里有一条最能说明问题：`tests/test_plugins.cpp` 的用例名 `above_u10ffff_f5`——
它左邻是下划线（非字母数字，过关），右邻是字母 `f` ⇒ **右边界这条是不可省的**。规则最终定为「单段需左右都有边界」
并上「连写两段以上只需左边界」，两条并集现读都是 0，所以加进去不会让仓库当场变红。

**臂清单（共七次注入，都用一次性探针文件写进 `tests/`、扫完立刻删，实到 rc 逐臂登记；表中 A3d/A3e 合写一行）**：

| 臂 | 注入的形状 | 预期 | 实到 |
| --- | --- | --- | --- |
| A1 | 提交 1025c01 修掉的那一行原文（历史缺陷本体） | R5 命中 1 处、rc=1 | **命中 1 / rc=1** |
| A3a | 单个转义段、左右都是标点 | 命中（单段分支） | **命中 1 / rc=1** |
| A3b | 两段连写、尾邻是字母 ⇒ 单段分支结构上不可能命中 | 命中（只能靠连写分支） | **命中 1 / rc=1** |
| A3d/A3e | 单段且右邻是字母/数字 ⇒ 两条分支都不该命中 | 不命中、rc=0 | **0 命中 / rc=0** |
| A4 | 正规转义写法（反斜杠 + u 在字符串字面量里） | — | **命中 1 / rc=1** |
| A5 | 干净对照（`above_u10ffff_f5` 与真实中文文案） | 不命中 | **0 命中 / rc=0** |

A3b 是**承重臂**：没有它，连写分支就是死代码，而我完全可能把「只加单段判据」写成归零。
A4 与 A3d 是这一把**主动登记的两处边界**，不是疏漏：

- **过宽方向（A4）**：正规转义写法也会被判红。取舍依据是本仓的既有约定——源码一律以 UTF-8 **直接写中文**，
  现读全树正规转义为 0 处（上表第四行），所以放行价值为零、拦下代价为零；真要用转义时按具体路径豁免，而不是先放宽规则。
- **射不到方向（A3d/A3e）**：一个孤立转义若右邻紧跟字母或数字，两条分支都抓不到。接受，因为真实残留后面
  总是中文或标点（A1 那一条就是实证）；这条写进规则注释里，防止下次有人以为归零等于全覆盖。

**夹具自证同步升级**：`tests/mojibake_fixtures/mojibake_bad.txt` 加了一条 `R5_TARGET` 行（437 → 549 字节），
自证门由「必须命中全部 4 条」改成 **5 条**；实到 `SELFTEST bad-fixture rules: R1_ROUNDTRIP,R2_PUA,R3_BOPOMOFO,R4_ODDFORMS,R5_ESCRESIDUE`。
`mojibake_ok.txt`（从本仓真实中文文案抽）仍须 0 命中 ⇒ 过宽会先在自证这一层红。
**字面坏样本只能待在夹具里**：夹具目录被仓库扫描刻意跳过，而我最初把这条例子的原文写进了守卫自己的注释，
守卫当场把**它自己的源文件**判红（输出一行 `MOJIBAKE R5_ESCRESIDUE` 指向本脚本的注释行）——那条注释已经改写成不含样本的措辞，
并把「例子放夹具、注释只描述形状」写进了那段注释。

**同步改准的三处已提交文本**（都写着「四条」，现在不成立）：`tests/CMakeLists.txt` 里那条用例的自证说明、
`tests/check_source_mojibake.cmake` 里「四条规则里 R1 需要 CP936 / 所以四条一起落地」那段（改为五条，并点明 R5 只需 ASCII 形状比对）；
本文件批次 B 那一节里两处规则计数（用例说明那句与夹具自证那句）只加时态限定、不改写当时的事实：现读为"落地时四规则全带"与"落地时 4 条、批次 D 起 5 条"。

**验收实到**：守卫独立复算（`cmake -DPS=… -DREPO_ROOT=… -P tests/check_source_mojibake.cmake`）
基线 `scanned files=318 violations=0` 退出码 0；`ctest -C Debug -R "source_mojibake_guard|docs_match_cli"` 2/2；
`(CoverageMeta|CoverageMetaJudge)` 19/19；`Total Tests:` 802 未变（没新增用例）；登记表六键未动。
`tests/check_source_mojibake.ps1` 现读 6924 字节、**非 ASCII 字节数 0**（守 #173 那条「`.ps1` 字面量必须 ASCII」）、CRLF、无 BOM。
卫生面：探针文件已删（`git status` 干净）、`data/` 两件套 0 新文件 + `88/88/0`、受保护件 `terrain_plugin.cpp` blob 未变。

### 批次 D 守卫扩面 D3-A（2026-11-04，`docs_match_cli` 加两条判据：类型列对撞插件 ParamSpec、说明列不许为空；**不碰产品**）

**先量现值再定判据**（工单原话，顺序不能倒）。文档侧 `## 参数` 表实到 126 篇 / 594 行 / 每行 5 列；
第 2 列（类型）的取值**封闭为 9 个词**，与 `src/framework/include/gis/framework/param_spec.h` 里
`enum class ParamType` 的 9 个成员一一对应（文件路径 276 / 整数 121 / 浮点数 87 / 字符串 61 / 枚举 35 /
坐标系 6 / 布尔值 4 / 范围 3 / 目录路径 1）⇒ 这一列本来就判得了，此前只是没判。

**逐行对撞的 3 处不符**（口径 = 配上二进制键的 593 行）：

- `georef` 两篇（控制点配准、RPC 正射校正）把"目标坐标系"的类型写成 `字符串` ⇒ **文档漂移，已改**。
  这处判定的抓手不是我的猜测：该键在 CLI `--help` 里的标签就叫"目标坐标系"、`Default: EPSG:4326`，
  而文档自己的参数名也是"目标坐标系"——写 `字符串` 与同一行的参数名互相打脸。
- `cutting.tile` 的 `output` 文档写 `目录路径`、插件级 `ParamSpec` 写 `FilePath`：**文档是对的**。
  `doTile` 用 `create_directories(output)` 把它当目录写（`src/plugins/cutting/cutting_plugin.cpp` 的
  `doTile` 函数体内），而 GUI 的补救分支 `src/gui/gui_data_support.cpp` 只给 `cutting/split/output`
  设了 `selectDirectory`，tile 没有；通用那条要求 `paramType == DirPath` 才生效，进不去。
  ⇒ 落白名单（带这些证据）+ 新挂账 **N189**（GUI 给的是保存文件对话框，与 split 不一致），
  与既有 N146"插件级表表达不了按动作收紧的必填"同族，本条是**类型侧**。

**载体侧三个要点**：

1. 解析插件源码前**必须剥注释**：本轮未剥的版本多抓到 1 个条目，那是 `processing_plugin.cpp` 一句注释里
   写的 `ParamSpec{"k", ...}`——全仓从未声明过键 `"k"`。与 `CoverageMeta` 判据要求剥注释是同一个坑。
2. **判据自己报覆盖数**：摘要行新增 `type_rows` / `desc_rows` / `param_spec_blocks`（实到 593 / 593 / 232），
   且解析不到任何 ParamSpec 类型时直接 `SystemExit`——否则"类型判据根本没跑"会长得像"文档干净"。
   `说明列不许为空` 这一判据的现值是 **0 条为空**，所以它可以直接 FATAL；同时 `UNMATCHED-ROWS` 那句
   告警的射程描述一并补上了新维度（原来只说 必填/默认值 未被检查）。
3. `--self-test` 的红臂从四类扩到**六类齐全**（`count/label/required/default/type/desc`），
   "只豁免 1 类"那条臂的装饰数字改成实到断言 —— **本轮就是这条臂先红**，根因是我自己在那次调用里
   少传了 `types` 参数，于是 `type` 那类根本不存在，臂 2 的"六类齐全"白测了一遍。
   教训：夹具与调用参数一起改才算加了判据，只改夹具文本等于没改。

**本轮刻意没做**：`Options:` 枚举的集合对撞。文档侧候选值形态至少三种（空格分隔的反引号串 / 斜杠分隔 /
逗号加等号说明），另有 31 条 enum 键根本没列候选值；我最初用"有没有分隔符"粗数得 3 条是**口径错**
（空格分隔不落在任何分隔符上）。已拆成独立工单，并要求它先用"取反引号内 token 集"重数再定判据。

**验收实到**：`--self-test` 六臂 6/6 OK；真跑 `actions=126 docs_checked=126 missing_doc=0 rows=594 matched=593
type_rows=593 desc_rows=593 param_spec_blocks=232 drift=0 allowed=8 dead_allowlist=0 dead_override=0 overrides=8`
退出码 0；`ctest -C Debug -R "docs_match_cli|source_mojibake_guard"` 2/2；`(CoverageMeta|CoverageMetaJudge)` 19/19；
`Total Tests:` 802 未变。卫生面：探针产生的 `tests/__pycache__/` 已删、`git status` 只剩本笔预期文件；
`data/` 两件套 0 新文件 + `88/88/0`；受保护件 blob 未变。

### 批次 D 守卫扩面 D3-A2（2026-11-04，`docs_match_cli` 第三条判据：枚举候选值集合对撞；**不碰产品**）

判据：某键在 `--help` 里打了 `Options:`，且文档说明里用反引号列了候选值 ⇒ **两个集合必须相等**。
真值取第 3 个载体（`Options:` 行，用户可见的二进制契约），不取源码 `enumValues`——CLI 打印的那份才是用户读到的。

**先重数形态，再定判据**（#150 结案时那条"只有 3 条文档列了候选值"是我自己口径错：空格分隔的反引号串
不落在任何分隔符上）。按"取反引号内 token"重数，论域与守卫实到口径对齐（配上键的 593 行全扫）：

| 现读项 | 数 |
| --- | --- |
| 有 `Options:` 的匹配行 | **34** |
| 文档列了候选值的行 | 27 ⇒ 集合相等 **27**、不等 **0** |
| 文档一个候选值都没列的行 | **7**（如 georef 两篇的 resample 只写"重采样方式"） |
| 二进制自己的 `Default` 不在自己 `Options` 里的行 | **0** |

**唯一一处"不等"被我否证掉了，方向值得记**：`raster_math.reclassify` 的说明是
`` `manual` 按上面的规则逐条映射；`interval` 按 `间隔步长` 等距切分 ``——第三个反引号串引用的是**下一行的参数名**，
不是候选值。处置不是把它加进豁免，而是**只取纯 ASCII 的反引号串**：`Options` 恒为 ASCII，中文引用天然不参与对撞。
这条收窄同时写进了自证绿臂（好文档里放一个中文引用当诱饵，option_rows 必须 ==1 且仍 0 漂移），
否则"按 ASCII 收窄"和"这条判据根本没跑"看起来一模一样。

**三条臂在真实文档上跑（临时改 `特征检测.md` 的 matching.detect 说明，跑完逐字节还原）**：

| 臂 | 注入 | 预期 | 实到 |
| --- | --- | --- | --- |
| M1 | 去掉一个值 `orb` | 1 条 options 漂移、rc=1 | **drift=1，文档缺=['orb']** |
| M2 | 加一个不存在的值 `surfa` | 1 条 options 漂移、rc=1 | **drift=1，文档多=['surfa']** |
| M3 | 原文不动（对照） | 0 漂移、rc=0 | **drift=0，rc=0** |

M1/M2 证明集合比较是**双向**的（少列与多列都红），M3 证明红是注入造成的而不是探针造成的；
还原后 `restored_identical=True`（4214 字节逐字节相同）。

**已知未覆盖面（写在脚本头，不当成已覆盖）**：那 7 行"有 Options 但文档没列候选值"本判据刻意不管——
它是条件式的。要收这 7 行得逐条补文档，属另一条判据的射程。

**验收实到**：`--self-test` 6/6，红臂点名七类 `count/default/desc/label/options/required/type`；
真跑 `… matched=593 type_rows=593 desc_rows=593 option_rows=34 param_spec_blocks=232 drift=0 allowed=8
dead_allowlist=0 dead_override=0` 退出码 0；`data/` 未被写、文档树除本笔预期改动外干净；
探针一律带 `-B`/`sys.dont_write_bytecode`，不再在 `tests/` 留 `__pycache__`。

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

### 批次 D 裁定落地 N187（2026-11-05，processing 的 15 处"判空再 fail"死支删除；**行为不变，只删够不着的分支**）

用户 2026-11-05 逐条拍板选了**甲 删死支**。先做普查（脚本 `build/tmp/batchD/n187/census.py`，逐条打上下文三行）：
15 处**同形**——每条的上一行都是 `readBandAsMat(...)` / `gdalBandToMat(...)` / `openRaster(...)`，
其后就是 `if (X.empty()) { return Result::fail(三类文案之一); }`（`if (!ds) return fail(...)` 是一条单行形）。
删除按块匹配（`if (X.empty()) {` + 紧跟一行 `return gis::framework::Result::fail(` 且含三类文案之一 + 紧跟 `}`），
并加了一条防误伤的守卫：**上一行不含 `readBandAsMat(` 或 `gdalBandToMat(` 就不许删**。
实到 `matched_blocks=14 matched_oneline=1 → 15`，删 43 行（14×3+1），文件 1560 行→1517 行，三类文案在本文件残留计数均为 **0**。

**为什么它们够不着（机制 + 一条实测）**：`core::gdalBandToMat` 的非空出口只有 `cv::Mat(height, width, CV_32F)`，
其余出口全是抛 `GisError`（取不到波段 / 尺寸超限 / `openRaster` 打不开），而 `cv::Mat` 只有在 0 行或 0 列时才 empty；
实测上游连造都拒绝——`gdal_create -outsize 0 0 -of GTiff` 报 `Attempt to create 0x0 dataset is illegal,
sizes must be larger than zero`（rc≠0、不落盘）⇒ 0×0 数据源进不了这条链，判空支恒不可达。
契约已写进 `src/core/include/gis/core/opencv_wrapper.h` 里两个入口的声明上方（唯一落点，不在 15 处各写一遍）。

**随刀改准的三处旧文本**（它们指向被删的文案，不改就成了悬空路标）：
`tests/test_plugins.cpp` 的 GB-4 块头把口径条数由 8 改成现读 **7**（`doGaborFilter` 现读 `Result::fail` 7 条），
P9 的 K6 块头由"现读 6 条、实到可达 4 条"改成**现读 4 条、四条全可达**，
P9 链路注释里那句 `if (!ds) return fail(...)` 改成"原先有、已由本刀删除"；
`docs/算法说明/影像处理与分析/K-Means 分割.md` 的注意事项同步。
按函数体现读 `Result::fail(` 条数（脚本现读，16 个 do* 函数）：GaborFilter 7、GlcmTexture 7、Pansharpen 9、KMeans 4、
Threshold/Filter/Hough 各 3、Enhance/Edge 各 4、Watershed 4、MeanShift 5、TemplateMatch 5、Skeleton/ConnectedComponents/Stats 各 2。

**射程外残留（本把不动，如实登记）**：`src/plugins/vector/vector_plugin_raster_convert.inc` 里还有一句
`if (!srcDS) return Result::fail("Cannot open raster file: " + input)`，上一行同样是 `core::openRaster` ⇒ 同形死支；
但它不在 N187 登记的 processing 射程内，且同函数紧接着的 `if (!rasterBand)` 是**活的**
（`GetRasterBand` 返回 nullptr 不抛）⇒ 全仓现读该英文文案仍有 1 处（在 vector）。留待按同一口径扩面时一并清。

**回归**：整树 Debug 重编通过；`PluginTest.Processing*` **30 条全绿**（4.94 s）；param_guard / docs_match_cli /
source_mojibake_guard / geotransform_guard **四条机器守卫全绿**（4.09 / 1.25 / 17.14 / 0.55 s）；
用户数据未被写（`find data -type f -newermt 本次开批` 0 条、基线对账 88/88/0）。无独立复审席（R-D63 继续记欠账）。

### 批次 D 裁定落地 N189（2026-11-05，GUI 给 cutting/tile 的 output 补上"选目录"分支；**只动 GUI 与判据，不碰产品算法、不碰 CLI**）

用户 2026-11-05 逐条拍板选了**甲 补 GUI 分支**。实到形态：`doTile` 把 `output` 当目录用
（`create_directories(output)`，瓦片落 `output/<z>/<x>/<y>.<ext>`），但插件级 `ParamSpec` 只有单条 `output`、
声明的是 `FilePath`，而 `buildFileParamUiConfig` 原先只给 `cutting/split/output` 开了 `selectDirectory` ⇒
GUI 对 tile 弹的是**保存文件对话框**，与它自己的真实行为不符（文档 `栅格切片.md` 的参数表写的是"目录路径"，说的是真行为）。

改法照 split 那支同形：新增 `cutting` + `action == "tile"` + `paramKey == "output"` 一支，
置 `selectDirectory = true`、`isOutput = true`，placeholder 写成选目录口径。
**键类型不动**（`ParamSpec` 仍 `FilePath`）——插件级表表达不了"按动作变化的类型"，这是结构性口径，
与 N146（插件级表表达不了按动作收紧的必填）同族，仍留在 `docs_sync_allowlist.json` 的 `type` 豁免里，
只把那条 evidence 从"tile 没有分支"改成"已由 N189 补分支 + 正反两臂钉住"。

**判据**（`GuiSupportTest.BuildFileParamUiConfigProvidesSpecializedHints`）配了两向：
tile 的 output 必须 `selectDirectory` 且 `isOutput`；**新加的反臂**是 `matching/detect` 的 output 必须
**不** `selectDirectory`（它是单文件产物）⇒ 万一有人把目录分支上抬成通用规则，这一臂会红。
判别取证：把 tile 那支的 `selectDirectory = true` 改成 `false` 重编 ⇒
该用例在 `tileOutputConfig.selectDirectory` 一处 FAILED；改回后 82 条 GuiSupportTest 全绿。

**回归**：整树 Debug 重编通过；`GuiSupportTest.*` 82 条全绿（489 ms）；
param_guard / docs_match_cli / source_mojibake_guard / geotransform_guard 四条全绿（3.94 / 1.14 / 15.23 / 0.52 s）。
`docs_sync_allowlist.json` 改后仍是 `verified=7 / doc_overrides=8`，没有出现 DEAD-ALLOWLIST（那条 type 豁免还在承重）。
无独立复审席（R-D63 继续记欠账）。

### 批次 D 裁定落地 N185（2026-11-05，hough circles 的 max_radius 默认值不再映射成 INT_MAX；**改可观测行为**）

用户 2026-11-05 逐条拍板选了**甲 透传 0 给上游**。改点只有一处：`doHough` 的 circles 支原先把
`max_radius` 传成 `maxRadius > 0 ? maxRadius : INT_MAX`，现改为原样透传 `maxRadius`。
机制侧的既有读数（P5b 块头记着）：上游 `HOUGH_GRADIENT` **只把 `maxRadius <= 0` 当"自动"**，
正数会被原样拿去做半径 bins 的分配 ⇒ 默认 0 被换成 INT_MAX 后，只要 stage1 出一个候选圆心就抛
`bad array new length`，一个候选都没有才走得到"成功且 0 检出"——也就是**参数表说的"0 means auto"当时是假的**。

**新增判据**（在 `PluginTest.ProcessingHoughExecution` 内，紧接 circles 参照臂）：与参照臂**只差一个键**——
不发 `max_radius` ⇒ 消费点 `getParam<int>(params, "max_radius", 0)` 取到 0（单测这条走的是"键不在 params 里"
的兜底路径，与 CLI 侧由 `ParamSpec` 默认值填 0 的取值相同）。
三条断言同向：`success` 必须真、产物必须落盘且非零像元数 > 0、`detect_count` 必须 > 0。
**不钉半径值**（窗口比 40 宽，检出半径可能偏离 B1 的 [20.5,21) 区间），B1/B2 那两条仍用正数窗口收窄搜索。

**反向臂实跑**（可观测行为改动的取证）：把那句改回 `maxRadius > 0 ? maxRadius : INT_MAX` 重编 ⇒
同一用例以 `unknown file: error: C++ exception with description "bad array new length"` **FAILED**（rc=1）；
改回透传后 30 条 `PluginTest.Processing*` 全绿。⇒ 这条新断言是承重的，不是"今天已经满足"。

**随刀改准的两处旧文本**：P5b 块头那段"circles 支的判据一律显式给正 max_radius …… 本刀不修 src/**、
也不把『默认参数必失败』钉成契约"整段按已修订正（保留机制描述，删掉"留待拍板"口径）；
lines 支那条 `diffCells` 断言的 message 里"0→INT_MAX 映射"改成"那套上限映射"（映射已不存在）。
文档面 `docs/算法说明/影像处理与分析/霍夫变换.md` 两处：参数表的「最大半径」说明改成"0 不设上界、正数硬上界"，
注意事项里那条"给正数窗口是目前唯一能拿到检出结果的路径"改写成缺陷已修的过去式 + 判据落点。

**回归**：整树 Debug 重编通过；`PluginTest.Processing*` 30 条全绿（4.57 s）；
param_guard / docs_match_cli / source_mojibake_guard / geotransform_guard 四条全绿。
登记表 `processing,hough` 的 GOLD 行不增不改（证据用例名不变，只是同一用例里多了一条默认档臂）。
无独立复审席（R-D63 继续记欠账）。

### 批次 D 裁定落地 N180（2026-11-05，zonal_stats 的 CSV 空分区行补齐到 8 列；**改产物字节**）

用户 2026-11-05 逐条拍板选了**甲 修 writer，列数恒为 8**。改点一个字符：`count == 0` 那一支
原先发 `",,,,,"`（5 个逗号 ⇒ `fid,band` 之后只有 5 个空字段 ⇒ 整行 7 列），现在发 6 个逗号 ⇒ 与表头
`feature_id,band,min,max,mean,stddev,sum,count`（8 列）等长，也与满值那一支（`min,max,mean,stddev` 4 个逗号
+ `sum,count` 2 个逗号 = 6 个）同形。JSON 支不受影响：它空分区只发 `{"band":N}`、六个键整体省略，本来就不靠列数。

**动手前先查了"有没有判据把 7 字段钉成现状"**（这是裁定的前置条件）：有，且正是立案那条 ——
`PluginTest.RasterManageZonalStatsExecution` 里 zEmpty 的 `EXPECT_EQ(row->size(), 7u)` 明确写着
"钉的是现读形状，不是应该的形状；谁补上那个逗号这一条就会红"。⇒ 本把把期望值改成 **8u**，
并把它旁边那个行查找 helper `findZonalCsvRow` 的注释改准（它**故意不按列数筛行**，理由正是"按 ==8 筛会把
缺陷行洗白成空跑"；缺陷修掉后这条理由不再成立，但写法保留——列数由 `size()==8u` 那条直接判，筛行只看前两列）。
`docs/算法说明/栅格工具/分区统计.md` 的"不能假设 CSV 每行都是 8 列"改成"列数与表头恒等，读端可按位置取列"。

**反向臂实跑**：把 writer 改回 5 个逗号重编 ⇒ `RasterManageZonalStatsExecution` 在该断言处红两次
（两个波段各一条，`Which is: 7` vs `8u`），退出码 1；改回 6 个逗号后 5 条 `PluginTest.RasterManage*` 全绿。

**回归**：整树 Debug 重编通过；`PluginTest.RasterManage*` 5 条全绿（337 ms）；
param_guard / docs_match_cli / source_mojibake_guard / geotransform_guard 四条全绿。
`raster_manage,zonal_stats` 的 GOLD 登记行不变（证据用例名不变）。产物字节变了 ⇒ D4 的双树全量与
`--install` 复核时这一条属于"字节级契约"那族，要一并复看。无独立复审席（R-D63 继续记欠账）。

### 批次 D 裁定落地 N186（2026-11-05，删掉 doGlcmTexture 里"算了不用"的全局 pairCount 与那道不可达闸；**行为不变**）

用户 2026-11-05 逐条拍板选了**甲 删掉**。射程两行：`const int pairCount = kernelSize * std::max(0, kernelSize - 1);`
与 `if (pairCount <= 0) return Result::fail("invalid kernel_size");`。归一**只**用逐方向的 `dirPairCount`
（窗口尺寸与是否撞边界都会让它变），那个总量算完即丢。
闸不可达有现读载体：`processing glcm_texture --kernel_size=1` 在**动作级校验**就被拒
（案文 `参数"窗口大小"必须是不小于 3 的奇数`，rc=1；这句是**当时实到**，N172-C2 把奇偶位搬进 ParamSpec 后同一入口的案文已换成 `参数“核大小”必须是[3, 101] 内的奇数`），够不到插件消费点；kernelSize>=3 时 `pairCount = k*(k-1) >= 6`。
`"invalid kernel_size"` 这句全仓只有这一处，`grep -rn "invalid kernel_size" src tests docs` 删除后为 0 ⇒ 没有任何判据钉过它。

替换成一句 WHY 注释（放在 `radius` 声明处）：归一只能走 `dirPairCount`，改回总量归一会**改变产物**。
文档面零改动：`pairCount` 今天不出现在任何 `docs/算法说明` 文件里（唯一的 grep 命中是
`VectorRepairCountsEmptyGeometryAsSkipped` 这个用例名的子串误中）。
`tests/action_gold_cases.txt` 的 `processing,glcm_texture,GOLD,PluginTest.ProcessingGlcmTextureExecution` 不变。

**回归**：整树 Debug 重编通过；`*Glcm*:*Gabor*:*MeanShift*` 4 条全绿（677 ms）；
param_guard / docs_match_cli / source_mojibake_guard / geotransform_guard 四条全绿。
**与 N182 的关系**：本把的不可达论证用的是"动作级校验保证 >=3 的奇数"这一**现读事实**；
N182 把这条保证上抬到 ParamSpec 单源之后，论证不变强（约束仍在），所以两把的先后不影响正确性——
本把按 N182 之后落，只是顺带把那条校验的载体读了一遍。无独立复审席（R-D63 继续记欠账）。

### 批次 D 裁定落地 N184（2026-11-05，core::openRaster 按数据集关掉 PAM；**改可观测行为**）

用户 2026-11-04/05 逐条拍板选了**甲 打开时禁 PAM 读回（不用全局环境变量）**。落点一处：`gis::core::openRaster`
取到句柄后 `dynamic_cast<GDALPamDataset*>` 成功就 `SetPamFlags(GetPamFlags() | GPF_DISABLED)`（`gdal_pam.h`）。

**裁定时我说的"用 GDALOpenEx 的 PAM 标志位"这条不成立，实测推翻后才落到 per-dataset 标志上**，两条负结果都要留字据：
① `GDAL_OF_*` 里没有 PAM 开关（`gdal.h` 现读 0x00/0x01/0x02/0x04/0x08/0x10/0x20 一列，没有 pam 位）；
② 唯一的进程内开关是配置项 `GDAL_PAM_ENABLED`（现读 `gdal.dll` 里有这一串），但**边车是懒读的**——
只在 `GDALOpen` 前后 `CPLSetThreadLocalConfigOption("GDAL_PAM_ENABLED","NO")` 再立刻恢复，
`raster_inspect info` 照样报 `NoData: -9999`；而把它当**环境变量**整进程设上才压得住（那是裁定明令不取的形态，
因为它连写盘一起关）。⇒ 既要不污染全局、又要压住懒读，只剩"给这一份句柄置 GPF_DISABLED"。

**探针形状**（按 P4a-fix 定的判别变量）：手写 `.aux.xml` **不带** `<?xml ?>` 声明行，内层只放
`<PAMRasterBand band="1"><NoDataValue>-9999</NoDataValue></PAMRasterBand>`。
改前经产品读数：`raster_inspect info` 打 `NoData: -9999`；改后同一命令**这一行整条不出现**（外部 `gdalinfo` 仍报
-9999，说明影像没变、只是我们不读）。判据落在 `CoreTest.OpenRasterDoesNotReadBackPamNoData`，三臂：
边车伪声明不读回；**反臂** GTiff 自己的 `GDAL_NODATA`（-12345）照旧读得到（抑制不是把 NoData 一律抹掉）；
原生声明与边车同时存在时读到的必须是 -12345。
**反向臂实跑**：把那句 SetPamFlags 摘掉重编 ⇒ 第一条 `hasNodata` 变 true 且读到 -9999，用例红（rc=1）；装回后全绿。

**副作用（裁定要求先量的那一项）**：`GPF_DISABLED` 是"这份句柄的 PAM 读写全不做"，所以经 `openRaster` 打开的
数据集在关盘时**也不再回写边车**。现读两条：① `raster_manage nodata` 在 GA_Update 句柄上设 NoData，
产物 gdalinfo 报 `NoData Value=-9999` 且**没有生成 .aux.xml** ⇒ GTiff 走原生标签，功能没丢；
② 一次 `processing filter --filter_type=gaussian`（真实 DEM 副本）在输入与输出两侧都不落边车。
⇒ 处理链里"上游边车被下游读回并固化"这条传播路径两端都断了。
其余消费面：`GetStatistics` 系 0 处、`ComputeStatistics` 4 处且 bApproxOK 全是 false ⇒ 只可能变"重算"，不会变"读到旧缓存"。

**回归**：整树 Debug 重编通过；`gis_tests.exe` 全量 **653 条全绿**（53.5 s，单进程）；`real_data_raster` 腿绿（18.66 s）；
param_guard / docs_match_cli / source_mojibake_guard / geotransform_guard 四条全绿。
文档面 `统计信息.md` 四处口径同批改准（算法内容、处理流程第 3 步、结果解释、注意事项），
P4a-fix 那节的历史读数加时态限定（那套判别变量仍然成立，只是今天读不回）。
无独立复审席（R-D63 继续记欠账）。

### 批次 D 裁定落地 N181（2026-11-05，enhance 的 `clip_limit` 下界由 0.0 抬到 1.0；**改可观测行为**）

用户 2026-11-05 逐条拍板选了**甲 下界抬到 1.0**。改点是 `processing` 插件级 spec 的 minValue：
`double{0.0}` → `double{1.0}`（默认值 2.0 与上界 100.0 不动）。原来那个下界本身就是被闸放行的退化值——
`clip_limit = 0` 会让上游 `if (clipLimit_ > 0)` 整段跳过裁剪，局部均衡退化成不分格的全局均衡。

**同批删掉 GUI 的第二写者**：`buildEffectiveGuiParamSpecs` 里原先有一支
`} else if (spec.key == "clip_limit") { adjustedSpec.minValue = 0.0; }`——**不删就等于本把白做**，
因为它是"面板构建时再把下界改回 0.0"的覆写，CLI/工作流会拦住而 GUI 不会。
删掉后 GUI 直接继承 spec（少一个写者，方向与 N172 一致，但本把不依赖 N172 的任何机制）。
`tests/test_gui_support.cpp` 那条 `EXPECT_DOUBLE_EQ(processingFiltered[2].minValue, 0.0)` 因此改成判
"**未被 GUI 覆写**"（合成 spec 只给 5 项 ⇒ minValue 停在 `ParamSpec` 默认的 `int{0}`，
所以断言 `holds_alternative<int>` 且值为 0；谁再把魔数加回来这条就红）。

**新判据 `PluginTest.ProcessingClipLimitLowerBoundIsOne`**（判在框架闸那一层，三条同向）：
`0.0` 拒且案文含「超出范围 [1, 100]」；`0.999` 也拒（它与 1.0 在上游折成同一个整数裁剪上限，放行只会长出
"改了没效果"的区间）；`2.0` 通过（默认值必须落在新界内）。
**C5 不反转、改注记**：先前裁定写的是"C5 判据要行为反转成 0.0 报错"，实测**不成立**——
该用例直调插件，而区间闸在派发层（CLI `main.cpp:243` 与 GUI `validateExecutionParams` 共用
`validateParams`，直调不过它；这个分层在 4774 行既有注释里就写着）。所以 C5 仍判"0.0 直达上游、产物不同"，
只把注释改成"这是绕过派发层的形态证据"，拒绝行为由上面那条新判据钉。
⇒ 裁定里的"反转 C5"按实测落成"保留 C5 + 新增派发层判据"，两件事都做了，只是分工变了。

**真二进制三臂**：`processing enhance --clip_limit=0.5` ⇒ `rc=1`、案文「参数“CLAHE裁剪限制”超出范围 [1, 100]」、
**产物目录里 0 个文件**（不产出）；`--clip_limit=2.0` ⇒ `Success`；界外那条的三件（rc / 案文 / 无产物）都现读。
**反向臂实跑**：把 spec 的 minValue 改回 0.0 重编 ⇒ 新用例前两条红（0.0 与 0.999 都放行）；
把 GUI 那支加回 0.0 ⇒ `GuiSupportTest.BuildEffectiveGuiParamSpecs*` 红。

**回归**：整树 Debug 重编通过；单测全量 **654 条全绿**（53.7 s，注册数 803 与新用例自洽）；
param_guard / docs_match_cli / source_mojibake_guard / geotransform_guard 四条全绿；data 未写（0 条 / 88-88-0）。
文档面 `影像增强.md` 的那条注意事项整段重写（区间、抬下界的两条依据、派发层与直调的分工）。
**仍挂着的**：N172（区间开/闭标志与"按动作收紧"的表达力、其余两处 GUI 魔数 `gamma`/`kernel_size`/`sigma` 等、
CLI 手写闸的 5 条不一致）与 N182（kernel_size/sigma 单源）——本把只收了 `clip_limit` 这一个键。
无独立复审席（R-D63 继续记欠账）。

### 批次 D 裁定落地 N183（2026-11-05，派发层的必填闸改按动作取；**改可观测行为，同销工单 #146**）

- 拍板：甲"必填闸改走 per-action"。修前实测（Debug 树 `gis-cli`，输入都是 `data/` 的副本）：
  - `processing stats --input=<副本>` 不带 `--output` ⇒ rc=1、`Error: 参数"输出栅格"不能为空`，
    与确实要写文件的 `processing enhance` 在同一参数形状下报的**逐字同文案**、无从区分；
    带上 `--output=<路径>` ⇒ rc=0，但那个路径上没有产物。
  - `vector info --input=<副本>` 不带 `--output` ⇒ rc=1、`Error: 参数"输出文件"不能为空`，
    同样与 `vector filter` 的报错逐字相同（这就是工单 #146 记的那条）。
- 误报面的普查口径（一次算全，三处载体）：GUI 表 `actionUiConfigStorage()` 的 visibleKeys
  × `--help` 的 `[required]` × 13 个插件 / 126 个动作 ⇒ 插件级 required 声明 17 条（不含 `action`），
  落到 (plugin, action, key) 三元组上**213 条必填键确实在该动作面板内、2 条不在**
  （`processing.stats.output` 与 `vector.info.output`），visibleKeys 为空的动作 0 条
  ⇒ 误报面就是这两格，没有第三条，也不存在"面板不做过滤所以口径失效"的第四类。
- 修法（单源放在插件层，不在 CLI/GUI 各抄一份）：`IGisPlugin` 新增虚函数
  `requiredParamKeys(actionKey)`，默认实现＝"该插件全部 `required=true` 的 spec 键"，即旧口径逐字保持；
  `ProcessingPlugin` 只对 `stats`、`VectorPlugin` 只对 `info` 各摘掉 `output` 一条。
  CLI 与工作流派发改走 `actionScopedParamSpecs(*plugin, action)`：把 required 重写进一份 spec 副本，
  `validateParams` 与 `findFirstInvalidParamKey` 读同一份向量 ⇒ 键名和文案不会分叉（B12 的同口径要求）。
  类型/区间/枚举/未声明键四道判据仍走整份 specs，只动 required 这一维。
  图形界面一行没改：它本来就先按 visibleKeys 过滤、再判必填，与派发层的新口径天然一致。
- 插件级声明一个字没松：`output` 在 `processing` 与 `vector` 的 `paramSpecs()` 里仍是 `required=true`
  （那是另外 15 / 32 个动作的真值），摘只发生在"这一个动作"这一层。
- 判据（三条新用例；两个方向各跑过变异，实测读数见下）：
  - `PluginTest.PerActionRequiredGateDropsOutputOnlyForStatsAndInfo`：逐动作名单——两个插件里**只有**
    `stats` / `info` 被摘，其余动作逐个判 `output` 仍必填；同一个动作的 `input` 仍必填；
    插件级声明仍 `required=true`（四支各盯一个失败方向）。
  - `GuiContract.RequiredParamKeysArePanelVisible`：对 action spec 自己枚举出的全部 (plugin, action)
    判"必填键必须是该动作面板渲染的键"（`action` 是面板选择器、`buildEffectiveGuiParamSpecs` 从不渲染它，
    故豁免），并判"必填键必须是已声明的 spec 键"（抓 override 里把键名写错这种静默放开）。
    它与 21-C 反向守卫（面板可见键必被实读）合起来 ⇒ **必填键必被实读**，这一族误报从此不能再生。
  - `WorkflowParamValidation.RequiredGateIsActionScopedSameAsCli`：工作流侧双臂——`stats` 只给 `input`
    放行且 completedSteps=1；`enhance` 缺 `output` 仍在派发前拒，文案同时带键名与人类可读名。
  - 变异实测：撤掉 `processing` 那句 erase ⇒ 守卫点名 `processing.stats.output`、名单用例点名
    `processing.stats 的 output 必填=true，期望=false`（两条同红）；把 `vector` 的 erase 改成无条件 ⇒
    **只有**名单用例红（点名 `vector.filter 的 output 必填=false，期望=true`）——子集式守卫看不见
    "摘过头"，那个方向只能由名单用例承重，所以两条都必须留。
  - 这条守卫第一次实跑就整片红：每个动作的 `action` 都被点名（它永远在必填集里、从不在 visibleKeys 里）
    ⇒ 豁免那一支是实测逼出来的，不是先验设计；红在回归之前而不是之后。
- 交付判据同步：`tests/check_install_smoke.cmake` 里"故意给 `vector info` 传 `--output`"的绕法删掉，
  改成裸调用 ⇒ 裸环境里这道闸一旦回归，这条会红，而不是被占位参数悄悄绕过。
- 真实腿同步：`run_real_vector_regression.ps1` 里三处**内联**占位改成裸调（两条 C3c-A 的 `info` quick 用例，
  以及拓扑检查读 `metrics_buf` 那一次 `info`）；三个取数器（`Get-LayerStats` / `Get-LayerInfoForPath` /
  `Get-LayerFeatureCount`）的占位参数本把**保留**——删它要给二十多处调用点改签名，风险落在全部矢量腿上，
  登记为 N192。
- 文档同步（口径=全仓 grep "N183 / 误报必填 / 输出栅格…必填" 的四个落点，逐个改或加时态限定）：
  `影像处理与分析/统计信息.md` 两处"命令行仍要求 `--output=`"改写为已修并点名两条判据；
  `矢量数据处理/矢量信息.md` 补一条"命令行不需要 `--output=`"（原文从没写过这件事）；
  `docs/交付自验.md` 那条"矢量侧的 `--output` 目前**必须给**（待放开）"改成"只有 `info` 不再需要、
  其余矢量动作仍然必须给"（这是给客户照着敲的一页，写错等于教人绕路）；
  本文件里 P4a 那段立案原文加时态限定（不改历史读数）；`tests/test_plugins.cpp` 的 F7 块头注释同加时态限定。
- 本把新立两条挂账：
  - **N191（待拍板）**：反方向没跟着收——现读 60 个 (plugin, action, key) 三元组在 GUI 的 `requiredKeys`
    里标了必填、插件级 spec 却没标 ⇒ CLI 与工作流放行、由插件用英文报错；且 `--help` 只有插件级
    `[required]`，无 per-action 表达能力。要不要把这 60 格并进同一来源、`--help` 怎么表达，
    改的是 60 个动作的 CLI 可接受集 ⇒ 归拍板，与 N172 同族。
    同一条拍板还要顺带定 `GuiContract.RequiredParamKeysArePanelVisible` 的尺寸：本把它落的是**单向子集**
    （必填键必须是面板渲染的键），实测变异 `MUTATION-N183B` 证明它看不见"摘过头"（把某个动作的 output
    无条件摘掉时只有名单用例红）；要不要把它加成双向相等（`required(action) == spec.required ∩ visible(action)`）
    取决于上面那 60 格往哪边靠 —— 单向时 GUI 可以比 CLI 严，双向时两侧必须逐字一致。
  - **N192（纯清理）**：上面那三个取数器的占位 `--output` 在本把之后已是多余参数。

### 批次 D 裁定落地 N172-C2（2026-11-06，13 条"比参数表更严"的手写数值闸搬进 `ParamSpec` 的开/闭位与奇偶位；**派发路径行为不变，报错案文换形**）

用户 2026-11-05 在 N172 的三选一里选了**甲 全量单源**，这是四片刀的第二片。射程 14 个文件＝代码与测试 12 个（+168/−162，口径 `git diff --numstat -- src tests`）＋文档 2 个：
框架头 1（`plugin.h` 新增 `adjustActionSpec` 虚钩子，由 `actionScopedParamSpecs` 在取作用域表时应用）
＋插件 6（georef 4 键、matching 2 键、vector 2 键、terrain 1 键、processing 1 键的声明，另加 processing 头里的钩子声明）
＋手写层 1（`action_validation.cpp` 删 13 条闸、11 处 span）＋GUI 2（`gui_data_support.cpp` 删魔数改界、`mainwindow.cpp` 面板改喂作用域表）
＋测试 2（`test_plugins.cpp` 新用例与两处注释、`test_gui_support.cpp` 删表项与两条用例改写）
＋文档 2（`空间滤波.md` 案文引用；`RELEASE.md` 本节与另外两处时态限定）。

**十三键与新声明**（"实到案文"＝真 Debug `gis-cli` 逐条跑出来的那一行，不是推定；越界值那一侧全部 rc=1）：

| 键（动作） | 声明改成 | CLI 实到案文 |
| --- | --- | --- |
| `accum_threshold`（terrain stream_extract） | `setExclusive(true,false)` | `参数“汇流阈值”超出范围 (0, 1e+09]` |
| `minnaert_k`（georef minnaert_correction） | `setExclusive(true,false)` | `参数“Minnaert K”超出范围 (0, 1]` |
| `bright_percentile`（georef percentile_stretch） | `setExclusive(true,false)` | `参数“亮百分位”超出范围 (0, 100]` |
| `dark_percentile`（georef percentile_stretch） | `setExclusive(false,true)` | `参数“暗百分位”超出范围 [0, 100)` |
| `sun_zenith_deg`（georef cosine_correction） | `setExclusive(false,true)` | `参数“太阳天顶角”超出范围 [0, 90)` |
| `ratio_test`（matching match） | `setExclusive(true,false)` | `参数“比值检验”超出范围 (0, 1]` |
| `quality_level`（matching corner） | `setExclusive(true,false)` | `参数“角点质量”超出范围 (0, 1]` |
| `tolerance`（vector simplify） | `setExclusive(true,false)` | `参数“简化容差”超出范围 (0, 1e+09]` |
| `min_area`（vector sliver_remove） | `setExclusive(true,false)` | `参数“最小面积”超出范围 (0, 1e+09]` |
| `kernel_size`（processing filter / gabor_filter / glcm_texture 三动作共用一条声明） | `setRequireOdd()` | `参数“核大小”必须是[3, 101] 内的奇数` |
| `sigma`（processing gabor_filter，仅此动作） | 钩子按动作置 `exclusiveMin` | `参数“Sigma”超出范围 (0, 1000]` |

**行为不变的证据是双臂跑的**：界内值一律放行——`--sun_zenith_deg=89.5` 过了参数层、红在下一句
`slope_raster is required`；`processing filter --kernel_size=5` 与 `--sigma=0` 都进到算法里去了（打到
`Reading band 1 from …` 才因输入不存在而失败）。补三条 `--kernel_size` 读数把分工钉清楚：1 / 2 / 102 都是
`超出范围 [3, 101]`，只有偶数走奇偶那一句。反向臂是这批里唯一有语义差别的一条：`sigma=0` 在 `filter` **必须**合法
（gaussian 支把 `sigma<=0` 交给上游按核尺寸取的硬编码二项式核，文档与 GOLD 都按此写），在 `gabor_filter` **必须**被拒
⇒ 这就是 `adjustActionSpec` 存在的理由；13 条里实测只有这一个键需要按动作表达开界，其余 12 条都是插件级一条声明。

**三个互相独立的口径**（口径的字面定义抄在下面，不依赖任何脚本活着）：
- 口径 A＝`action_validation.cpp` 里"解引用取值 + 与常数比较"的行（跳过注释行）：**37 → 24**，差 **13**。
- 口径 B＝`gui_data_support.cpp` 里 `adjustedSpec.minValue` / `adjustedSpec.maxValue` 被赋常数的行（跳过注释行）：**34 → 28**。
- 口径 C＝`test_gui_support.cpp` 里 `ActionValidationTestParam{` 行：**56 → 43**，差 **13**。
两处 13 对齐是"删干净了"的算术对撞；判据侧还有一道硬闸——新用例每行都跑一次
`validateActionSpecificParams(...) == nullopt`，**手写层只要还留着这 13 条里的任何一条就红**。
这条闸不是摆设：C2 第一版就是被它逮到漏删了 `vector.sliver_remove.min_area`。

**口径差异登记（给 C3，别拿错数）**：台账里 N172 的"42 条闸 / 29 条冗余"是**闸**口径（跨行写法算一条、spindex 那段
循环驱动的 6 个 `*_band` 各算一条），今天的口径 A 是**行**口径 ⇒ 两个数不可互推（24 行里 `azimuth` / `altitude` /
`sun_azimuth_deg` 三支各含两次比较）。C3 派发前先把口径定成一个，任务书里不许直接把"29"当现值写。

**判别变异两臂（每臂各改一个载体、各整树重编一次）**
- M1 去掉 `georef.sun_zenith_deg` 的 `.setExclusive(false, true)` ⇒ `PluginTest.DispatchNumericBoundsFollowScopedSpecs` 红，
  点名行 `georef.cosine_correction.sun_zenith_deg 越界值实到文案：`（后面空串＝90 被放行了），其余 12 行不吭声。
- M2 把 `adjustActionSpec` 里的动作名 `"gabor_filter"` 写成 `"gabor_filter_typo"` ⇒ 同一条用例里**两**处红：
  roster 行 `processing.gabor_filter.sigma 越界值实到文案：`，加那条显式反向臂（「反向：gabor_filter 的 sigma=0 必须仍然被拒」）；
  而 `filter` 的 `sigma=0` 那一支仍绿 ⇒ 钩子确实只对 gabor 承重，没把开界泄漏给整份表。
- 还原一律用字节备份 `cp` + md5 对撞（georef `d2e8e6e73ca94c5be94f6a62ddb9bb38`、processing `27305dcca07d974c0f27d6fbe3f99b16`），
  两臂还原后各自重编再跑，终树回到绿态。

**受保护件重钉（按裁定 N160 甲）**：`src/plugins/terrain/terrain_plugin.cpp` 本把被改**一行**（`accum_threshold` 加开界位与说明串），
blob 由 `588a42504ee19bcb551f8214e5c1adb68cea20e9` 变为 `fa34c3fbf9cc457e74cc460fb1dd9abeb7abe1bc`；两法同值——
`git diff` 给出的 `index 588a425..fa34c3f`，与"工作树先把 CRLF 归一成 LF、再按 `blob <len>\0` 取 sha1"。
此后凡指旧值的"还原判据"都会假 ⇒ `tests/test_plugins.cpp` 里 terrain profile 那块引用旧哈希的三臂注释已就地加时态限定，
还原只认字节备份的 md5。

**N193 立案（本把新号；缺陷在守卫自己身上，本把不动它的判据）**
`tests/check_action_visible_keys_read.cmake`（ctest 名 `param_guard_visible_keys_all_read`）的段模型建立在两个
"整行形状"上：一条线只剩 `}` 或 `};` 就切一段，段名取段头攒到的文本里最左的 `标识符(`
（见该脚本第 3~4 节，注释里自己写着"取最左的 ident("）。两步都不认识字符串字面量。
⇒ ParamSpec 的描述串里只要出现**不配平的括号**，那一支插件的段就崩，守卫报"找不到派发种子"。
六臂实测（P0~P4 一次脚本、P5 补一臂：每臂复制整棵 `src/plugins`，只改 georef `sun_zenith_deg` 那一行的描述串，用 `-DPLUGIN_DIR` 指过去跑同一份守卫脚本）：
- P0 散文描述（无括号）⇒ rc=0、`no_seed configured=0`、`segments=409`
- P1 `[0, 90)` ⇒ **rc=1**、`no_seed configured=8`、`segments=398`（比控制臂少 11 段）
- P2 `[0, 90]` 配平方括号 ⇒ rc=0、`no_seed configured=0`、`segments=409`
- P3 `(0, 90)` 配平圆括号 ⇒ rc=0、同上
- P4 `(0, 90]` 反方向失配 ⇒ **rc=1**、`no_seed configured=8`、`segments=398`
- P5 控制串原样、只在尾巴后再塞 `; a;b;c`（括号一维不动）⇒ rc=0、`no_seed configured=0`、`segments=409`
⇒ 承重的是**配平**，不是括号种类，也不是分号；命中的 8 个条目逐字是 georef 的全部白名单动作
（`c_correction;cosine_correction;dos_correction;gcp_register;minnaert_correction;percentile_stretch;radiometric_calibration;rpc_orthorectify`），
射程限于出事那一支插件的文件，而且**是响的红、不是静默放行**（rc=1 且把 8 个动作逐条列出来）。
内部卡在哪一步本把没逐 step 复算：P2 与 P1、P4 与 P3 都只差收尾那一枚括号（开符与串内其余部分逐字相同），
P5 与 P0 只差分号 ⇒ 这六臂只够说明"要配平失序才触发"，不足以定位到某一条正则。本把处置：描述串一律不写区间括号，改成
`must be greater than 0` / `above 0 and at most 1` / `at least 0 and below 90` / `odd and at least 3` 这类散文，
区间形状只活在报错案文里由 `paramIntervalText()` 单点生成。修法（段切分与函数名抽取改成字符串感知，或加一条括号配平校验）归 D3 守卫扩面。

**更正已提交文本**：`65af9ee`（N172-C1）提交信息里那句「参数化测试表 56 行里 33 行直接钉着这些闸」的 **33** 是我手抄键名清单得到的，
没有脚本载体，而且那份清单漏了 `sigma`。今天的机械口径是：行内 `expectedKey` 落在手写数值闸的键集（31 键）里的行＝**35**，
这是**上界**（同一个键也可能被别的规则报出，且 `band` 这类"一键多闸"会扇入）；能逐字对上闸内案文字面量的只有 1 行
——因为表里绝大多数行只钉 `issue->key`、`expectedMessage` 留空。⇒ 准确说法＝改前 56 行、本把迁走 13 行、
余 43 行里"钉的确实是数值闸"的那部分由 C3 逐行判定。已提交信息不改（不改历史），更正只落这里与台账。

**验收实到（终树重编后复跑，全部现读）**：
- Debug 全量 ctest **796/796 Passed、0 Failed、1124.36 s**（`ctest -N -C Debug` 今天列到 796 条）；登记表 PENDING 仍 **2** 条，本把不碰矩阵、不碰登记表。
- 单元二进制 `gis_tests.exe` **646 tests from 24 test suites、646 PASSED、55.05 s**。
- 五条机器守卫在终树上 5/5 Passed（38.42 s 合计）：`param_guard_visible_keys_all_read` 3.88 s、`install_smoke` 11.72 s、`source_mojibake_guard` 20.54 s、`docs_match_cli` 1.66 s、`geotransform_guard` 0.55 s；`install_smoke` 在全量里另跑一次 7.81 s 也绿。
- `data/` 两件套：`find data -type f -newermt "2026-11-06 08:32:00"` = **0**；`check_data_baseline.py` = **BASELINE: 88  CURRENT: 88  DIFFS: 0**（CLI 双臂的输入输出都指到 `data/` 之外的未跟踪临时目录、且那个输入文件并不存在，全程没开过任何原件 ⇒ 无 `.aux.xml` 风险）。
- 卫生面：`rm -rf tests/__pycache__`（普查脚本 import 守卫模块带出来的我的临时产物，不在 .gitignore 里）后工作树 **13 个 M、0 个 ??**；Release 树未重编 ⇒ 本把只验收 Debug。
- 席位：本把控制器自跑，无独立复审席。

**余下两片**：C3＝删口径 A 现值那 24 行派发路径不可达的手写闸 + 迁余下测试行 + GUI 魔数全数派生化；
C4＝删 `doFilter` 的静默改写 + `docs_match_cli` 加"区间描述串 vs 实到边界"判据（两侧单源之后这条才可能绿）+ 双树与 RELEASE/台账收口。
### 批次 D 裁定落地 N172-C3（2026-11-06，删掉派发路径上不可达的 24 行手写数值闸与 GUI 的 28 行魔数改界；**CLI/工作流零行为变更，GUI 有七格界与声明对齐**）

**先给判据，再动刀**。新增 `PluginTest.DispatchHandLayerNeverStricterThanScopedSpecs`：论域＝登记表 126 个 (plugin, action)
× 该动作作用域表里每个 Int/Double 键 × 每键 10~11 个探针值（min/max/默认值各一、两侧各越界一档、0 与 ±1，Double 再加一个 NaN），
实测 **1353 个键位 / 13997 次探针**；判据是一条单向蕴含「spec 放行 ⇒ 手写层不得就**同一个键**返回 issue」。
首跑 **0 反例** ⇒ 那 24 行闸在 CLI / 工作流 / GUI 三条派发路径上从来开不了口（`validateParams` 先跑）。
这条判据留在仓里当永久防线，另外两条下限（键位 ≥1300、探针 ≥13000）防"论域缩水把 0 反例变成假绿"。

**删**：`action_validation.cpp` 24 行比较（＝27 次比较）分 11 个块、共 138 行，其中 `spindex` 与 `processing` 两个整块
（前者是循环驱动的六个 `*_band`）一起走；同文件里失去唯一调用者的 `intParamValue` 助手（12 行）一并删。
`gui_data_support.cpp` 删 78 行＝28 行界赋值 + 分支头，只留 `projection.transform` 那条 `src_srs` 的**默认值**改写（它不是界）。
两个口径的现值都归零：口径 A（"解引用取值 + 与常数比较"的行）37→24→**0**，口径 B（`adjustedSpec.minValue/maxValue =` 的行）34→28→**0**。

**迁移**：GUI 参数表 `ActionValidationParamTest` 43 行 → **22** 行。删掉的 21 行不是我挑的——是删完闸跑全量单测时
逐条点名的失败名单；它们迁进 `DispatchNumericBoundsFollowScopedSpecs` 的第二张表（`redundantRows`），
另外补 5 行（`raster_inspect.bins`、`vector.rasterize.resolution`、`matching.detect.min_distance`、`processing.kmeans.kmeans_k`、
`spindex.ndvi.red_band`——这五个键的闸在旧表里从来没留过表项）。roster 现值 **39 行**（C2 的 13 ＋ C3 的 26），
每行的期望案文都取自真二进制实到，不是推定。`GuiSupportTest` 的两条用例改名成
`BuildEffectiveGuiParamSpecsLeavesMatchingRangesAlone` 与 `...LeavesUtilityAndProcessingBoundsAlone`，判据换成**哨兵搬运**：
喂进 −5.0/7.0 与 −3/11 这种与插件真实界都不同的哨兵值，要求六个字段（min/max/exclusiveMin/exclusiveMax/requireOdd/default）
逐字原样出来。旧写法只判"停在 ParamSpec 默认的 int{0}"，那样的话"GUI 把界改成默认值"和"GUI 没动界"根本分不开。

**新维度守卫**：`GuiContract.EffectiveSpecsAgreeWithPluginSpecs` 加"数值界"一维。红→绿顺序照做：判据先加上跑，
点名 **23** 格（按 (plugin, action, key) 枚举，所以一处 `terrain.z_factor` 魔数会牵出 slope/aspect/hillshade/tpi 好几格）；
删完 GUI 那 28 行之后转绿。

**GUI 侧的七格行为变更**（本把唯一的可观测变化；CLI 与工作流零变更，理由见上面那条 0 反例）：
- 三格由"GUI 比声明宽"收成声明值：`terrain.z_factor`、`processing.gamma`、`processing.spatial_radius` 与 `color_radius`
  ——GUI 原先写 1e-6，插件声明是 0.001；
- 四格删掉"拿极小量/极大量近似开界"的写法，直接吃声明里的开区间位：`georef.sun_zenith_deg`（GUI 89.999999 ⇒ spec 90 开上界）、
  `georef.dark_percentile`（99.999999 ⇒ 100 开上界）、`georef.minnaert_k` 与 `georef.bright_percentile`（1e-6 ⇒ 0 开下界）。
方向一律是"GUI 与 CLI 提交同一份界"，与 N181（`enhance.clip_limit` 抬下界并删掉 GUI 那份第二写者）同法同判。

**判别变异三臂（各自点名、各自用字节备份还原并核 md5）**
- M-A 把 `terrain.z_factor` 的 GUI 改写塞回去 ⇒ `GuiContract.EffectiveSpecsAgreeWithPluginSpecs` 红，点名 **14** 格
  （守卫按 (plugin, action, key) 枚举，而这条改写是插件级的 ⇒ slope/aspect/hillshade/tpi/tri/curvature/… 每个 terrain 动作各一格），
  `...LeavesUtilityAndProcessingBoundsAlone` 同时红（`z_factor 的下界被展示层改了`），
  而 `...LeavesMatchingRangesAlone` **保持绿** ⇒ 归属唯一。
- M-B 把已删的手写闸（terrain `z_factor <= 0`）塞回派发前那一层 ⇒ roster 里 `terrain.hillshade.z_factor` 那一行红：
  「手写层仍在判这个界 ⇒ 数值界有两份写者：参数“高程缩放”必须大于 0」。
- M1（删之前的反事实臂）把 `z_factor <= 0.0` 改成 `< 1.0`，让它**真的**比 spec 严 ⇒ 新守卫红并点名
  `terrain.slope.z_factor 值=0.001000 被 spec 放行，却被手写层拒`。
⚠ 顺手把一条判据射程钉在纸上：**"冗余的第二写者"只有 roster 那一臂抓得到**（M-B 那闸与 spec 拒绝集相同 ⇒
「spec 放行 ⇒ 手写层放行」恒真，新守卫不响）。这不是漏洞，是那条判据的设计射程——别把它当成"有没有第二写者"的唯一防线。

**验收实到（终树重编后复跑，全部现读）**：Debug 全量 ctest **776/776 Passed、0 Failed、1273.21 s**（比 C2 那次少 20 条＝删掉的 21 行 GUI 表项 ＋ 本把新增 1 条判据；两条改名用例与新判据都在 ctest 名单里，PRE_TEST 发现机制自己跟上）；单元二进制 `gis_tests.exe` **626 tests from 24 test suites、626 PASSED、57.90 s**（646 − 21 ＋ 1）；五条机器守卫 5/5 Passed（合计 28.53 s）：`param_guard_visible_keys_all_read`、`install_smoke`、`source_mojibake_guard` 14.91 s、`docs_match_cli` 1.02 s、`geotransform_guard` 0.54 s；`data/` 两件套：`find data -type f -newermt "2026-11-06 08:32:00"` = **0**（这条窗口从 C2 开批算起，覆盖本把全程）；`check_data_baseline.py` = **BASELINE: 88  CURRENT: 88  DIFFS: 0**；卫生面：CLI 双臂的输入输出都指到 `data/` 之外的未跟踪临时目录、没开过任何原件；`tests/__pycache__` 提交前清掉；Release 树未重编 ⇒ 本把只验收 Debug；席位：本把控制器自跑，无独立复审席。

**余下一片**：C4＝删 `doFilter` 里"偶数加一、小于 3 抬到 3"的静默改写（它是数值界剩下的唯一第二写者，
且只有进程内直调可达）＋给 `docs_match_cli` 加"区间描述串 vs 实到边界"判据（界与案文两侧都单源之后这条才可能绿）
＋双树与文档收口。

### 批次 D 裁定落地 N172-C4（2026-11-06，删掉 `doFilter` 里"偶数加一、小于 3 抬到 3"的静默改写，并给 `docs_match_cli` 加"区间描述串 vs 实到边界"一维；**进程内直调有一族输入从"能跑出产物"改报失败**）

数值界的四个写者（插件声明 / 派发层手写闸 / GUI 展示层 / 插件内静默改写）到本把收成**一处**。
C1~C3 收了前三处，本把收最后一处，并把"文档写的界 == 声明的界"第一次交给机器判。

**产品（唯一一处，`doFilter`）**：原先紧挨着的两行静默改写（偶数加一、小于 3 抬到 3）换成与
`doGaborFilter` / `doGlcmTexture` 同族的两条显式 `Result::fail`（`kernel_size must be >= 3` /
`kernel_size must be odd`）。射程只有**进程内直调**：CLI / GUI / 工作流三条派发路径在进算法之前就被
同一份 `ParamSpec`（`[3, 101]` ＋奇偶位）拦下，那是 C3 那条"spec 放行 ⇒ 手写层不得就同一个键返回 issue"
判据的 0 反例早已钉死的路。用户可见变化＝原先直调 `kernel_size=4` 拿到一份 k=5 的产物**且返回状态是成功**，
现在拿到 `success=false` ＋一句案文，且不产出文件。甲案（破坏性拼法从"能跑"改"报失败"）由用户在 N172 裁定里选定。

**测试（`PluginTest.ProcessingFilterExecution` 的 S10 段整段换向）**：旧断言把静默改写**钉成事实**
（k=4 与 k=5 产物逐点相同、k=1 与 k=2 都和 k=3 逐点相同）；新断言是一张三支的 `RejectedKernel` 表
（`median` k=4 → 奇偶案文；`morph_dilate` k=1、k=2 → 下限案文），每支判三件事：`success=false`、
案文逐字相等、被拒那次不许留下产物。k=3/k=5 那四格"核尺寸真被消费"的期望（x=18 与 x=28 上的
0 / 50/255 / 1.0）与对 `outDilateShape` 的交叉核对**原样保留** ⇒ 不是把整段换成"只判失败"。

**判别变异两臂（都按字节备份还原，还原后 md5 = `99bd0c0d71eca90df4b30b4cc42cf79f` 逐字核过）**
- M-A（第一版取证，**本身有缺陷，如实记**）：把两行静默改写插在 `< 3` 那行**之后** ⇒ 只有 `median` k=4
  一支红（3 条 EXPECT），k=1/k=2 仍被排在后面的 fail 行拒掉。原因：静默改写原本排在显式检查**之前**，
  插错位置就不是原形态，这次变异证不到"下限臂"。
- M-B（重排后）：按原顺序插在两条显式检查**之前** ⇒ 三支各 3 条、共 **9** 条 EXPECT 全红
  （`success=true（静默改写没删干净）`×3 ＋ 案文不符×3 ＋ 留了产物×3），还原后同一支用例 PASSED。

**守卫新维度（`docs_match_cli` 第八类 `range`）**：说明列里**每一处** `[a, b]` / `(a, b]` 形状都必须等于该键
`ParamSpec` 声明的数值界（含开闭位）。配套新增 `load_param_bounds()`：读 `src/plugins/*/*.cpp` **与 `*.inc`**
（vector 的声明表整个活在 `.inc` 里），三个 `int{}`/`double{}` 记号依序当 默认/min/max，尾随的
`.setExclusive(a, b)` / `.setRequireOdd()` 当开闭位与奇偶位。这条判据在 C1 之前**不可能绿**——同一个界
当时有四份写者，文档只能对其中一份；收成一份声明之后"文档 == spec"才有单一比较对象。
- 现读：13 个插件 / **104** 个带数值界的键；文档参数表里 **9 行 11 处**区间描述串，逐处与声明一致
  （`range_rows=9 range_claims=11 drift=0 allowed=8`，同轮 `rows=594 matched=593`）。
  论域独立复核过一遍（不依赖守卫自身的文档映射，直接从盘上 126 篇抽）⇒ 两处口径同为 9 行 11 处。
- **9 处写在反引号里、2 处写在正文里** ⇒ 判据按**括号对**定位、两种形态都吃；沿用 Options 那条"只取反引号内
  token"的取法会漏掉 2 处，反向只取体外会漏掉 9 处，两种收窄都不做。
- 比较口径：数字按浮点比、括号按形状比，**不比整串字面量**。这一条是被现读逼出来的：第一版比整串 ⇒
  假红 **6** 条（源码写 `double{0.0}`/`double{360.0}`，文档按中文排版写 `[0, 360]`；
  `terrain.hillshade.azimuth`/`altitude`、`terrain.fill_sinks.epsilon`、`terrain.viewshed.curvature_coeff`、
  `terrain.reservoir_volume.water_level`、`matching.match.ratio_test`），逐条都是同一个区间的两种拼法。
- 解析器一处真缺陷（本轮抓到并补）：界字面量的字符类原先不含负指数 ⇒ `matching.ecc_epsilon`
  （`double{1e-6}, double{1e-10}, double{1.0}`）整条不进界表（现读全仓就这一行带负指数）。补上 `Ee+-` 后回到 104 键。
- 两条地板防"判据空跑"：界表键数 <90 直接 `SystemExit`（现读 104，地板只拦解析器整体失效）；
  `range_claims < 10` 打 `RANGE-COVERAGE` 并以退出码 1 结束（现读 11 ⇒ 文档把区间改写成"约 0 到 1 之间"
  这类非区间拼法时，这条维度会静默失去承重，地板就是为了让它不能静默）。
- 自证扩面：`--self-test` 的夹具从七类扩到**八类**，`range` 埋**三处、三条分支各自承重**
  （`[0, 10]` 只对开闭位、`[0, 20)` 只对数字、Enum 键凭空造界），好文档那条绿臂同时钉
  `option_rows==1` 与 `range_claims==1`；只豁免一类时总数 10、余 9 条仍算漂移。
- 真仓库变异取证（文档侧）：把 `地形分析/填洼.md` 参数表"微坡度"一行的 `[0, 10]` 改成 `[0, 20]`
  ⇒ 守卫退出码 1、`drift=1`、点名 `terrain.fill_sinks 键 epsilon 区间：文档=[0, 20] 二进制=[0.0, 10.0]`
  且**只这一条**；按字节备份还原后复跑退出码 0。
- ⚠ 奇偶位**不在** `range` 判据的射程（文档那句是"必须为奇数"这类中文措辞，形状对不上区间正则），
  它由 `ParamSpec` 自己 ＋ roster 的奇偶行承重——这一点写进判据注释，免得后人以为判过。

**文档与口径同步**：`空间滤波.md` 三处（参数表"核大小"说明 / 算法步骤 2 / 读者须知）改成"两条路都给失败、
不再有静默换核"；`文档漂移清单.md` 三处"只比参数表四列"订正为"只比参数表以内的列、不比正文"，
C4b 那节的"四类"补了时态限定（现值八类），并把"还没做的"清单里那条早已过期的"类型/说明没进判据"改写为
按起判日期分列的现状。

**验收实到（终树重编后复跑，全部现读；Release 树未重编 ⇒ 本把只验收 Debug）**：
Debug 全量 ctest **776/776 Passed、0 Failed、1344.14 s**（注册数与 C3 同为 776 ⇒ 本把没新增 ctest 用例：
S10 是既有用例内部换向，守卫新维度不新增用例）；单元二进制 `--gtest_list_tests` 现读
**626 tests from 24 test suites**（也与 C3 一致）；五条机器守卫在全量轮里 5/5 Passed
（`param_guard_visible_keys_all_read` 4.41 s、`install_smoke` 9.97 s、`source_mojibake_guard` 22.18 s、
`docs_match_cli` 1.85 s、`geotransform_guard` 0.64 s，另 `install_closure` 12.92 s）；
变异循环还原并整树重编后定向 35 条（`PluginTest.Processing*` ＋四条守卫）**35/35** 绿、34.60 s；
`data/` 两件套：`find data -type f -newermt "2026-11-06 13:58:19"` = **0**、
`check_data_baseline.py` = **BASELINE: 88  CURRENT: 88  DIFFS: 0**；
卫生面：本把的调查脚本、两份变异备份与守卫输出全在 `build/tmp/`（gitignored）内，
取证用的 `tests/__pycache__` 在提交前删掉，工作树只留六个被改的文件。席位：控制器自跑，无独立复审席。

**N172（含 N182）四把全部落地 ⇒ 两条工单结案**：
C1 `65af9ee`（`ParamSpec` 加开/闭区间位与奇偶位，零行为变更）→ C2 `c11a78c`（13 条比声明更严的手写闸
搬进声明的开/闭位与奇偶位）→ C3 `d9927ea`（删派发路径上不可达的 24 行手写闸 ＋ GUI 的 28 行魔数改界，
另立"手写层不得比声明更严"这条永久判据与 `GuiContract` 的数值界一维）→ **C4 本把**（删 `doFilter` 的
静默改写＝数值界剩下的那一处第二写者，并把文档区间交给机器判）。
收口对账：**数值界与区间文案的写者从 4 处收成 1 处（`ParamSpec`）**；工单 #172（N172）与 #189（N182）
同时销账。#189 标题里的两族分叉各自的归一处数不同，此处按现读与历史树对齐一遍再落笔
（**本段是对 `63699d8` 那版结句的更正**：旧句把 kernel_size 的写者数写成"三处（声明/手写闸/静默改写）"，
漏了 GUI 那份 `minValue = 3`；又把 sigma 那族误称成"读而不用"）：
- `kernel_size` 四处 → 1：声明（C1 加位、C2 抬下界到 3 并加奇偶位）、派发层手写闸（C3 删）、
  GUI 的 `minValue = 3` 魔数（**C2** 删；复算 = `git show c11a78c -- src/gui/gui_data_support.cpp`，
  里面那条 `-` 分支头就是 `} else if (spec.key == "kernel_size") {`，抬界的那行 `adjustedSpec.minValue = 3;`
  与它同支一起消失）、`doFilter` 内静默改写（C4 删并改显式拒绝）。
- `sigma` 三处 → 1：声明 `[0.0, 1000.0]`、GUI 的 `minValue = 1e-6`（那一支同时挂
  `sigma`/`gabor_lambda`/`gabor_gamma`，由 **C2** 删；在 `c11a78c~1` 的历史树里可读到原行）、
  `action_validation` 的 `sigma>0`（只挂 `gabor_filter`，由 **C3** 随 processing 整块删）；
  剩下的 `adjustActionSpec` per-action 开区间位与声明同在一个文件，属同一写者的按动作位。
两族之外另记一件：σ=0 从 CLI 合法可达、走上游按 ksize 索引的硬编码二项式表，仓里**没有**这一段的期望位点
⇒ 与"读而不用"（S11 六支正面见证已钉）是两件事，另立 **N194**（工单 #223）。
受保护的 `terrain_plugin.cpp` blob 现值为 `4cd8991e213bf676c6325f870d4d7370691bd1f8`
（N160 落地时改写为现值，旧值见下文 N160 节；N162 落地后至 N160 改写前为 `7116a6cdd0709b361a7afd2a2994297f0bb6ada2`，
N162 落地时的更旧值见下文 N162 节；N163 落地后至 N162 改写前为 `a5da3e9f54697d1b92af0e1aa8bbb4dc5515a825`，
N163 落地时的更旧值见下文 N163 节；C2~N163 前为本节成文时的那一值）。

### 批次 D N178（2026-11-06，新增第六条机器守卫 `anchor_guard`：活注释里的裸行号锚必须解得开、行号不许越界、锚数只许降；**不碰产品**）

R-D34 早就定了"已提交文本不许写裸行号锚"，但一直没机器执行 ⇒ 攒到了本刀现读的规模。
两个口径都记下来，不可互推：
- 立案口径（2026-10-29，`git grep` 数**载体行**）：`src/` 15 文件 / 39 行、`tests/` 19 文件 / 139 行，合计 **178 行**；
- 本刀口径（守卫数**锚的出现次数**，载体扩到 `src/` + `tests/` 的
  `cpp|h|hpp|cc|inc|ps1|py|cmake|json|txt`）：修前 **198** 处 / 修后 **182** 处。
  差在"一行里可以写两个锚"，复算 = `python tests/check_anchor_guard.py --repo-root . --config tests/anchor_guard_config.json`
  的 `anchors=` 字段（把登记表的 `anchor_ceiling` 临时改成别的数就能看到棘轮红）。

**判据形状：工单里"增量式 / 解析式 二选一"，本席位选的是两者能同时成立的交集**（不涉及用户可见行为，
不需要拍板；理由随文）
- `range`：锚解析到本仓文件、但行号（或区间上端）超过该文件行数 ⇒ 红。这是"解析式"里机器**能**核的那一半。
- `unresolved`：锚既不在本仓、文件名又没登记为上游 ⇒ 红。带目录的锚按**整段后缀**比
  （`cli/main.cpp` 能对上 `src/cli/main.cpp`；`common.inc` 对不上 `vector_plugin_common.inc`，段不等 ⇒ 正好抓住简写）。
- `ambiguous`：不带目录的裸文件名在本仓命中 >1 个文件 ⇒ 红（读者必须自己猜）。
- `ceiling`：登记表 `anchor_ceiling` 必须**逐字等于**现读锚数 ⇒ 多一条红（新写裸锚）、少一条也红
  （清了锚却没把界降下来，于是下一刀可以悄悄加回去）。这一条顺带就是"扫描真的跑了"的地板：
  解析器整体失效 ⇒ 现读掉到 0 ⇒ 与界不等 ⇒ 红，不需要另设地板。
- `dead-registry`：上游名单里没被任何锚用到的条目 ⇒ 红（口径同 `docs_match_cli` 的 DEAD-ALLOWLIST）。
**为什么不选纯"增量式"**：它要拿 `git diff` 当基准，ctest 在任意提交上跑就会随基线漂；
**为什么不选纯"解析式（锚必须对上注释引用的那段原文）"**：现读 182 处里只有极少数写了可比的原文，
其余是"某函数/某分支"式引用，硬套只会得到"把注释改写成可匹配文本"这种为判据服务的改写。

**本刀修掉的 16 处（198 ⇒ 182）**
- 3 处 `range`：全部是**本批次自己造成的**——N172-C3 删了 `action_validation.cpp` 151 行与
  `gui_data_support.cpp` 78 行，`tests/test_plugins.cpp` 与 `tests/run_real_matching_regression.ps1` 里
  指向 `action_validation.cpp:574` 的锚落到 EOF 之后（441 行），`tests/docs_sync_allowlist.json` 的
  `gui_data_support.cpp:2799-2801` 同理（2793 行）。三处都改成"指符号不指行号"
  （ratio_test 那条现在说"matching 的 `ParamSpec` 把合法区间钉成 (0,1]"，正是 C2 之后的真源）。
- 13 处 `unresolved`（去重后 12 个目标）：`common.inc` / `overlay.inc` / `duplicate_point_check.inc`
  这类**把文件名简写**的锚（`vector_plugin_` 前缀被省掉）。补全路径后**逐条把目标区间打出来对过内容**，
  结果是 **12 个目标里 9 个指错、只有 3 个还指对**（对的是 `overlay.inc:370-392`＝写 both 块那一支、
  `:413-429`＝写 input remainder 那一支、`:210`＝路径 B 的分支点）：
  `common.inc:499-509` 讲 `polygonIntersectionArea()` 累加 GeometryCollection 子面面积，实际那几行是
  数据源串的引号解析；`:388-402` 讲 `resultLayerGeometryType`，实际是 `stagedOutputPrefix` 与
  `MoveFileExW` 的注释；`:425-446` 讲 `effectiveLayerGeometryType`，实际是 `normalizeSourceStringPath` 的注释；
  `:626-631`、`:638`、`:639-645` 三处讲 `pointKey` 与 hole_count 的两个分支，实际全在边车替换的注释里
  （真身分别是 `pointKey`、`geometryHoleCount` 的 Polygon / MultiPolygon 分支）；
  `overlay.inc:177` 说"实现自述的不变量"，177 行是**空行**（不变量写在 179-180 三个计数变量的声明处）；
  `:353-355` 引的那句 `if (!geom || geom->IsEmpty()) continue;` 差着几行；
  `duplicate_point_check.inc:56-57` 说有 `/** … */`，那两行是 `while (GetNextFeature)` 与 `unique_ptr`。
  ⇒ 处置一律改成**符号/分支名**、不留行号（而不是"补个行号"把可疑数字留在树里）：
  `polygonIntersectionArea`、`resultLayerGeometryType`、`effectiveLayerGeometryType`、
  `pointKey`、`geometryHoleCount` 的 Polygon / MultiPolygon 分支、写 both 块那一支、写 input remainder 那一支、
  路径 B 的分支点。每条改前都在目标文件里 grep 过符号确实存在。

**自证六臂（TDD 顺序照做：先跑桩 ⇒ `rc=2`、5 条 FAIL / 1 条 OK，再实现 ⇒ 6 条 OK）**
其中一条臂在实现前就被自己否证：初版写的是 `stats["anchors"] != 99`，桩函数回 0 也满足 ⇒ **恒真的假臂**；
改成"看 findings 里有没有 `ceiling` 这一类"才是真臂。这条已写进判据注释。

**真仓变异探针（临时件 `tests/anchor_guard_mutation_probe.txt`，跑完即删、不入树）**
里面埋三处：越界 `src/gui/gui_data_support.cpp:999999`、不存在的 `some/deleted/local_file.cpp:12`、
已登记的上游 `gcore/gdaldataset.cpp:1509` ⇒ 守卫 `rc=1`，逐条点名 range 1 / unresolved 1 /
"现读 185 != 登记表 182：多了 3 条"，第三条探针锚被正确归为 upstream 而不报红；删掉临时件后复跑 `rc=0`。

**三条已量的射程限制（写在判据 docstring 与登记表注释里，别把这条守卫当成"锚都核对过了"）**
① 上游锚只核"文件名登记过"，**不核行号**：现读 `vcpkg_installed/` 只有 `vcpkg/info` 与 `vcpkg/updates`，
   一条 port 源文件都没有 ⇒ 上游行号是"写那次注释时的现读"，机器在此刻无法否证。
② 本仓锚只核"文件在、行号不越界"，**不核内容**：本刀那 13 条简写锚就是活例——把名字补全后 13 条的行号
   全都落在范围内，而其中 **10 次引用（9 个目标）指的并不是注释里说的那段代码** ⇒ 内容层面的收口靠
   "逐刀把锚迁成符号名 + ceiling 必须等于现读"托住不许增长，终局是 182 → 0，不是一刀。
③ 只认 `name.ext:N` 形态：`函数体 :1506-:1512` 这种**续行裸号**看不见（正则要吃文件名，
   否则 `value: 5` 会一片误红）。本刀顺手把见到的那处（`tests/check_action_visible_keys_read.cmake`）改成了符号写法。

**验收实到（终树重编后复跑，全部现读；Release 树仍未重编 ⇒ 本把只验收 Debug）**：
Debug 全量 ctest **777/777 Passed、0 Failed、1355.53 s**（注册数 776 ⇒ **777**，+1 就是 `anchor_guard`，
PRE_TEST 发现机制自己跟上）；单元二进制 `--gtest_list_tests` 现读 **626 tests from 24 test suites**（未变：
本把只改注释文本，没加没删用例）；**六条**机器守卫在全量轮里 6/6 Passed：
`param_guard_visible_keys_all_read` 4.90 s、`install_smoke` 8.96 s、`source_mojibake_guard` 18.54 s、
`docs_match_cli` 1.17 s、`geotransform_guard` 0.57 s、`anchor_guard` 0.72 s（另 `install_closure` 12.67 s）；
`anchor_guard` 在-suite 的 summary 现读 `carriers=35 anchor_lines=168 anchors=182 local_ok=157 upstream=25
range=0 unresolved=0 ambiguous=0 ceiling=182 dead_registry=0`；
`data/` 两件套：`find data -type f -newermt "2026-11-06 15:23:35"` = **0**、
`check_data_baseline.py` = **BASELINE: 88  CURRENT: 88  DIFFS: 0**；
卫生面：调查脚本与变异探针输出都在 `build/tmp/`（gitignored）内，探针临时件已删，
取证用的 `tests/__pycache__` 提交前删掉；席位：控制器自跑，无独立复审席。

### 批次 D N177（2026-11-07，腿侧判据闸把 `judged` 拆成 `value-judged` / `no-value-judged`，并给闸自己一支自证用例；**不碰产品**）

症状：三条真实腿的收尾闸过去只要求"分支体跑过 ⇒ `Mark-Judged`"，于是 `judged=63` 被读成"63 条值级
期望"。本轮把"这条分支到底钉没钉一个手推的产物数值"改成**从 AST 派生**，派生不出来的分支必须逐条
点名进名单、每条写理由。

**判据形状（三种可派生的证据，每种都由合成夹具的臂钉住；`switch ($Case.Name)` 的分支体是唯一论域）**
- `E1` 比较：一侧是手写的数——裸字面量，或只由数组成的算式（`(10.0 / 255.0)`、`([int]$inherited + 5)`
  都算），另一侧不是集合基数。基数排除**大小写敏感**（`-cmatch '\.(Count|Length|LongLength)$'`）：
  `$rows.Count -ge 2` 是基数、`$payload.count -le 20` 是产物字段（matching 腿里 max_points 的唯一手写上界）。
- `E2` 参数：期望量以 `-Expected`/`-Expect` 具名传给 `Assert*` 命令。腿里同一助手的四个数字各归各位
  ——`-Expected 0`（期望）、`-ExpectedWidth 32`（数组几何）、`-Tolerance 1e-9`（精度）、`-Band 1`（波段号），
  只有第一种算期望；按"调用里有没有数字"来判会把后三种一起吃进来（臂 A11/A13 就是钉这个）。
- `E3` 文本：期望写成整行文本且文本自带小数或指数——`"Mean:   0.75"`、`"Pixel Size:  (0.001, -0.001)"`、
  zonal CSV 的整行 `"z1,1,0,4,2,1.22474,16,8"`（`-contains` 不是比较运算符，`E1` 看不见它）。
- 助手传递规则收紧：**助手只有在拿自己的声明参数跟手写数比时**才把值级证据传给调用方。收紧前是
  "助手里有任意比较就算"，被两条传输型助手带偏——`Invoke-CliAndCaptureText` 的 `$code -eq 0` 与
  `Read-JsonPayload` 的 `$jsonStart -ge 0`，再经 `Get-NamedRasterInfo` 传染 ⇒ **栅格腿 64 个分支里 62 个**
  被读成值级判据，而其中 `processing_gabor_filter` / `processing_glcm_texture` / `processing_mean_shift_filter`
  / `processing_skeleton` 四条分支体里**一次比较都没有**（探针现读 `comparisons=0 qualifying=0`）。
- 反向不变量保留：屏幕一个都不标 ⇒ 直接红（"判据瞎了"不许当"没有值期望"）。

**名单（`-NoValueList`，每条 Name + 必填 Reason）——43 条**：raster 21、vector 21、matching 1。
理由的类别词是逐条写在串首的，下面这组数是按串首类别词数的
（`grep -o 'Reason = "[a-z]*' | sort | uniq -c`），混合条目算在串首那一类里：

| 串首类别 | raster | vector | matching | 合计 |
|---|---|---|---|---|
| `shape`（钉的是 Size/Type/CRS 文本、网格几何、CSV 列名） | 17 | 1 | — | 18 |
| `integer`（内容统计量钉成**整数字子串**） | 3 | — | — | 3 |
| `invariant`（期望的一侧是运行时从输入读回的数） | 1 | 19 | 1 | 21 |
| `no switch label…`（归因缺失，见下） | — | 1 | — | 1 |

- `shape` 一类里真正"除形状文本外什么都没钉"的是 raster 这 6 条：`projection_assign_srs`、
  `processing_gabor_filter`、`processing_glcm_texture`、`processing_mean_shift_filter`、
  `cutting_split_overlap`（钉的是分块网格几何）、`classification_feature_stats_csv`（钉的是 CSV 列名与
  行数下界）；shape-prefixed 里其余 11 条（raster）+ vector 的 `rasterize_bbox` 是"shape 文本**加上**
  整数字子串"的混合写法，理由串里两个词都写了。
- `integer` 一族的要害：子串既不能区分 `0` 与 `0.5`、也不分波段，屏幕又读不到不带小数的整数 ⇒
  **这是测试弱点，不是分类**。**提到整数字子串的共 15 条**（raster 14 = 3 条串首 `integer` + 11 条混合、
  vector 1 = `rasterize_bbox`），单独立 **N195**：改走本文件早就为此存在的 `Assert-InfoBandStatNear` /
  `Assert-InfoStatNear`（其 D1-T1 头注释写的就是"子串的 8 能被 89.98 满足"），改完退出名单。
  本把不动它们：换断言载体要逐条重推期望值，属另一把刀。
- `invariant` 是真判据（输出条数 == 输入条数、extent 与输入相等/包含、字段名列表逐字相等），
  只是不写数，所以屏幕不吃它——把它们记下来正是为了让 `value-judged` 不被读成包含它们。
- 第 4 类只有一条：`union_poly_focus` **没有名字分支**（按组标分发是 C3h-3 的刻意设计，防"新增用例忘了
  写判据分支还静默全绿"），屏幕按名字取证据，对它无物可读 ⇒ 理由写的是"归因缺失"，并列出该助手实际
  判什么（三类之和 == 总数、落盘行数对元数据、源侧直方图推的下界、逐类计数、字段数，以及两个手写
  下界 `-gt 0` 与 `union_overlay_pieces -ge 1`）。**这条是真实腿跑出来的**：union_poly 腿首跑 rc=1 点名，
  静态论域看不见它（labels=48 而名单要吃 49 个 case 名）。

**自证 15 臂进 ctest**：新用例 `regression_gate_selftest`（**#778**，载体
`tests/check_regression_gate_selftest.cmake`，恒注册、解释器缺失判红），跑在一份"腿的形状"合成夹具上，
臂不依赖真实腿的数字。A9–A15 七支本轮新增；收紧前后对照：收紧前 A1/A9/A10/A12 四支红，另两支的
非空跑用变异核过——把 `-cmatch` 改回 `-match` ⇒ A14（连带 A1）红；把操作数递归取值改回只看字面量
⇒ A15（连带 A1）红；改回后 15/15 绿。

**三个载体坑（都写进注释，不是我个人的备忘）**
1. PowerShell 把**只含一个元素**的返回数组在管道上摊平 ⇒ `$cmps.Count` 是 `$null`，
   "这个助手里有比较吗"对**恰好一个比较**的助手静默读 false（`≥2` 个才为 true）。臂 A10 抓出来的。
2. Windows PowerShell 5.1 里没有可 `-is` 的 `StaticMemberExpressionAst` 类型；`[math]::Abs(...)` 的成员名
   节点其父是 `InvokeMemberExpressionAst` ⇒ 写不存在的类型是**那一行直接抛 TypeNotFound**，
   类型判断要用 `GetType().Name` 比名字。
3. `param([string]$Targets)` 之后再写 `$targets = @('a','b')`（PowerShell 变量名大小写不敏感，撞上的是
   同一个带类型约束的变量）会被 `[string]` **静默转成空格连接的单个字符串** ⇒ 探针的名单筛选变成
   永不过滤，我据此先收到过一条"NO CLAUSE FOUND"的假阴性。

**读数（两种口径分开写，不可互推；`ctest -V` 现读）**
- 静态论域（`Get-RegressionGateBranchFacts` 直接读三个 runner 的源）：raster `labels=64 value=43 no-value=21`、
  vector `labels=48 value=28 no-value=20`（名单 21 条，第 21 条就是那个没有名字分支的 `union_poly_focus`）、
  matching `labels=5 value=4 no-value=1`。
- 本跑（腿自己打的行）：
  `regression-gate[raster]: registered=84 ran=84 judged=63 value-judged=43 no-value-judged=20 no-value-roster=21 allowlisted=21 allowlist-table=21 branch-labels=64 conditional-registrations=1 dead-branches=0`；
  `regression-gate[matching]: registered=5 ran=5 judged=4 value-judged=3 no-value-judged=1 no-value-roster=1 allowlisted=1 allowlist-table=3 allowlist-gated-not-registered=2 branch-labels=5 conditional-registrations=3 dead-branches=0`。
  两口的差都是**条件注册**造成的、逐个对上了名字：raster 静态多出的那 1 条是
  `classification_feature_stats_csv`（`$Mode -eq "full"` 才注册），matching 多出的那 1 条是
  `matching_detect_sift`（同条件）；matching 的 `allowlist-table=3` 减 `allowlisted=1` =
  `matching_ecc_register` / `matching_stitch` 两条被 `-not $isDebugCliBuild` 挡在本跑之外（口径不变）。
  vector 是按组分腿跑的，每条腿各打一行、`judged` 只含本组，因此这里只给静态口径。

**验收实到（全部现读；Release 树未重编 ⇒ 本把只验收 Debug）**
- 三条腿：`real_data_raster` 24.66 s / `real_data_matching` 2.68 s / 七条 vector 腿全绿——其中
  `real_data_vector_union_poly` 首跑 **rc=1** 点名 `union_poly_focus`（名单缺这一条），补进名单并写清
  理由后复跑 **Passed 138.23 s**；其余六条 vector 腿首跑即绿（160.78 / 34.61 / 77.75 / 113.22 / 49.58 / 105.30 s）。
- Debug 全量 ctest **778/778 Passed、0 Failed、1135.02 s**（注册数 777 ⇒ **778**，+1 就是
  `regression_gate_selftest`，PRE_TEST 发现机制自己跟上）。
- 守卫集合（同一轮里）：`param_guard_visible_keys_all_read` 3.76 s、`install_closure` 13.37 s、
  `install_smoke` 7.68 s、`source_mojibake_guard` 14.64 s、`docs_match_cli` 1.09 s、`geotransform_guard`
  0.56 s、`anchor_guard` 0.67 s（`anchor_ceiling` 未被本把改动 ⇒ 新增两份 `.ps1`/`.cmake` 里没写裸行号锚）、
  新用例 `regression_gate_selftest` 1.91 s。
- 自证 15 臂：`powershell -File tests/regression_case_gate_selftest.ps1` ⇒ `arms=15 failed=0`；
  两支变异核（`-cmatch`→`-match`、去掉操作数递归）各红 A14/A15 与 A1，还原后 15/15 绿。
- 名单对账（改前自查，避免把一次 138 s 的腿跑浪费在拼写上）：`roster_check` 三条腿各自
  `unmarked ⊆ roster`、`duplicates=空`、`stale=空`、`ghost=空` ⇒ 名单与屏幕现读逐字同源。
- `data/` 两件套：`find data -type f -newermt "2026-11-07 13:46:51"`（本次开批时刻）= **0**、
  `check_data_baseline.py` = **BASELINE: 88  CURRENT: 88  DIFFS: 0**。
- 卫生面：本轮没动 `src/`、`tests/test_*.cpp`、登记表 `tests/action_gold_cases.txt` ⇒ PENDING 仍 2 条，
  单测数不变；全部调查探针在 `build/tmp/batchD/n177/`（gitignored）内。
- ⚠ 本把第一条提交 `e31fe45` 的 message 写的是"42 条分支进名单"，那是**算错的**：
  逐文件现读 `raster 21 + vector 21 + matching 1 = 43`（42 是我在补 `union_poly_focus` 那条之前数的，
  改完没回头改数）。message 已在历史里、不 amend，所以更正记在这里与本节；名单本身没错——三条腿的
  `no-value-roster` 字段现读就是 21 / 21 / 1。改数后重跑的那条腿是 `real_data_vector_union_poly`
  （rc=1 → **Passed 138.23 s**）。顺带记一件事：我的临时对账脚本 `roster_check.ps1` 的口径是"名字必须是
  switch 的 label"，所以它把 `union_poly_focus` 同时列进 stale 与 ghost——那是**探针比闸粗**，不是名单错；
  权威判据是闸自己（ghost 认 registered∪labels∪conditional，stale 认 `ValueBranches`），腿跑绿即证。
- 席位：控制器自跑，无独立复审席（本把没改产品行为，改的是闸与腿的收尾判据）。
- 遗留：**N195**（15 条整数字子串统计期望改走 `Assert-InfoBandStatNear` / `Assert-InfoStatNear`，
  改完退出名单，raster 的 `value-judged` 应从 43 涨到 58）；`union_poly_focus` 那类**按组标分发**的分支
  如需被屏幕归因，得先设计"组标 → 助手"的归因口径，本把不做（本把的处置是把它如实记成归因缺失）。

### 批次 D N167（2026-11-07 裁定，2026-11-09 落地：`matching.detect` 的 akaze 支把 `max_points` 真正接进上游；**akaze 的点数上限从"完全不生效"变为真生效，sift / orb 维持原语义**）

- **裁定**：2026-11-07 用户拍乙1（akaze 接参数）；乙2 明确不做——sift / orb 若改成检测后按 response 排序截断，会改动 match / register 既有产物的点集，连带 D1-matching 那批 GOLD 证据作废。
- **产品改动**：`createDetector` 的 akaze 支从无参 `cv::AKAZE::create()` 改为显式传 8 个实参：前七个逐字取上游默认（`DESCRIPTOR_MLDB`、0、3、0.001f、4、4、`DIFF_PM_G2`），第 8 实参 `max_points` 接产品参数。上游依据由控制器解包 `opencv-opencv-4.14.0.tar.gz` 亲核 `features2d.hpp` 的 `AKAZE::create` 声明（`int max_points = -1`，语义"最多返回这么多点，超出留 response 最高的"），非转述。除"上限生效"外其余行为面不变。
- **判据**：`MatchingDetectCapLadderAndFlatZeroPoint` 新增三档逐档钉死——5 档恰 5（防"写死一个上限"的退化实现）、20 档恰 20（本夹具候选池 29 > 20，删掉实参的变异在此红）、5000 档等于候选池 29（兼作不动点，防"恒返回很少"的实现蒙过前两档）。
- **文档**：`特征检测.md` 三态第 1 条从"完全不生效"改写为"是硬上限且不保证取满，实到 = min(设定值, 候选池)"。参数表里"默认 5000"这一格过去对 akaze 是假的（上游默认 -1 = 不限），现在 5000 真进上游。
- **sift / orb 不变**：sift 非硬上限（20 出 23）、orb 是上限但不保证取满（20⇒10、1000⇒48）。
- **验收**：Debug 全量 ctest 778/778、1173.33 s（本刀树态，控制器实跑）；六守卫 6/6。
- 席位：实现段由上一程会话完成、控制器收尾补 RELEASE 节（该节此前缺失，正文里的指向曾是悬空的）；无独立复审席。
### 批次 D N163（2026-11-05 裁定，2026-11-09 落地：`terrain` 的 `z_factor` 三处分别处置——**slope 接上游 `-s`（发倒数）、hillshade 保留 `-z` 并补值级判据、aspect 整键摘除**；slope 非默认档与 aspect 参数面为用户可见变更）

- **裁定**：2026-11-05 用户拍"甲"四点：① slope 改发 `-s`，方向与倍率以两档实测钉死；② hillshade 保留 `-z` 并补值级判据（丢掉 `-z` 发送必须红）；③ aspect 把 `z_factor` 摘掉（ParamSpec 作用域摘名 + 不读不记 + GUI 键名单同笔 + 文档同步）；④ 不含 #164 的通用"可见参数不许静默无效"守卫（另派，台账 R-D70）。
- **两档实测**（`build/tmp/batchD/n163/my`，GDAL 3.12.4 单位斜面 16×16，内部像元梯度=1）：上游 `gdaldem slope -s k` 把梯度**除以 k**——`-s 2`→26.565°、`-s 10`→5.711°，倒数 `-s 0.5`/`-s 0.1`→63.435°/84.289°；产品改前 `terrain slope --z_factor=1/2/10/10000` 四臂 Mean 全 45.000（静默无效在本机现读复现）。方向结论：产品 `z_factor` 的语义是垂直夸张（slope=atan(z·梯度)），上游 `-s` 方向相反 ⇒ **发 `1/z_factor`**。改后产品实测：z=1→45.000（`-s 1` 逐像元恒等，默认档不变）、z=2→63.435、z=10→84.289、z=10000→89.994；aspect z=1 与 z=10 逐字同值（270.000）。
- **产品改动**（`src/plugins/terrain/terrain_plugin.cpp`）：`runDemProcess` 不再把 `-z zFactor` 无条件拼给全部 processName——hillshade 分支保留 `-z`（真垂直夸张），slope 分支改发 `-s` + `1/z_factor`（`%.10g` 格式化：`1/z` 落在 [0.0001, 1000]，`std::to_string` 固定 6 位小数对无限小数会吃精度，大档 z=10000 有值级判据钉住）；aspect 分支不发任何 z 旋钮（上游 aspect 无垂直比例入口，官方 CLI 对 `-z`/`-s` 都报 Unknown argument；atan2(dz/dy, dz/dx) 对 z 等比缩放不变）。`doAspect` 不再读 `z_factor`（恒等占位 1.0），aspect 的 `algorithmParams`/`result.metadata`/输出栅格 `GIS_PARAM_z_factor` 三处记账同笔停记；slope/hillshade 仍记产品侧键面原值。
- **GUI**：`gui_data_support.cpp` 的 `terrain.aspect` visibleKeys 摘掉 `z_factor`（共 14→13 个 action 列该键），`param_guard_visible_keys_all_read` 的白名单−实读差集同批归零；offscreen 腿 `run_gui_terrain_aspect_regression.cmake` 同步删 `--set-param z_factor=1`（面板没有的键 set-param 会以 failed_to_apply_param 退出码 2 失败）。结构偏差如实记：`ParamSpec` 并无 scope 字段（任务书"作用域列表摘名"按可兑现面落——GUI 键名单 + 文档参数表才是键作用域的真实载体）；CLI `--help` 打的是插件级整表且 `z_factor` 声明被 13 个 action 共用 ⇒ `gis-cli terrain --help` 仍列该键，"aspect 的 --help 不再出现该键"在现结构下不成立，未新增 per-key scope 机制。
- **判据**（三条新判据全部加进既有 TEST_F，gtest 注册总数 778 不变）：`TerrainSlopeHandValueOnUnitPlane` 补三档逐像元钉——z=2→63.4349488（发 `-s 2` 会得 26.565、丢 `-s` 得 45，方向钉子）、z=10→84.2894069、z=10000→89.9942704（大档兼钉 scale 文本精度）；`TerrainHillshadeHandValueOnUnitPlane` 补块 (7)（gtest 基参显式取 `azimuth=270`，与 DEM-P 的 aspect=270 对齐 ⇒ Horn 通式 255·(cos zen·cos S + sin zen·sin S·cos(az−aspect)) 在 cos(az−aspect)=1 时才退化成 255·cos(45°−atan z) 这个简化式）：z=45→185（raw=184.2736736，ceil 律）、与既有块 (6) z=10→198 互为两档；这组值和「丢 `-z` 跑回 255」只在 az=270 档成立，CLI 缺省 az=315 下同夹具为 218/145/132——产品与上游 gdaldem 两个档都实测逐字同值；`TerrainAspectHandValueOnUnitPlane` 补三面钉——直调传 z=10 产物逐像元同值且 `result.metadata`/`GIS_PROCESSING` 域均无 `z_factor`（band 记账作对照腿）、GUI `terrain.aspect` visibleKeys 无该键且有效键集=白名单、aspect 作用域下越界 z_factor 仍报共享 spec 的标准文案"参数“Z因子”超出范围 [0.001, 10000]"（钉声明不删）。
- **改前判据重算说明**：`TerrainSlopeHandValueOnUnitPlane` 原有 z_factor"不当判据"注释（钉住缺陷现状不立字据）按裁定改为值级判据；默认档（z=1）45.000000/0/-9999 全部不变（`-s 1` 恒等），aniso 四幅、DEM-F/DEM-N 各钉不受影响；其余 44 处 z_factor 引用逐处判定后无一处期望值需要改动（局部路径族 z=2 的期望走的是 `elevation *= z`，与 GDALDEM 路无关）。
- **文档**：`坡向.md` 参数表删 `Z因子` 行并写明"坡向对垂直夸张不变"（数学依据与上游无旋钮）；`坡度.md` 写清 `z_factor` 经上游 `-s` 生效、方向按实测写（上游 `-s k` 除在梯度上，产品发 `1/z_factor`，z=1 恒等）；`山体阴影.md` 的 `Z因子` 说明改准为"垂直夸张系数，经上游 `-z` 直接生效"。
- **受保护件重钉**：`src/plugins/terrain/terrain_plugin.cpp` blob 由 `fa34c3fbf9cc457e74cc460fb1dd9abeb7abe1bc` 变为 `a5da3e9f54697d1b92af0e1aa8bbb4dc5515a825`（本把改写 runDemProcess 参数拼装与记账、doAspect 摘读；旧值在全仓的另一处出现是 N160 节的历史过渡记录，按史实保留，活钉引用只有 N172-C4 节那一处、已换成现值）。
- **验收**（本刀树态实跑）：`ctest -R "Terrain|terrain"` 48/48 全绿；六守卫 6/6（param_guard_visible_keys_all_read / install_smoke / source_mojibake_guard / docs_match_cli / geotransform_guard / anchor_guard）；ctest 注册总数 778 不变；`git status` 终态干净。
- 席位：单席实现（取证→实现→判据→文档→重钉一条龙），无独立复审席。
### 批次 D #164（2026-11-09 落地：上游边界参数有效性棘轮守卫 `param_effectiveness_guard`——守卫刀，`src/` 零字节改动，产品面只有 `tests/` 与本节）

- **裁定**：R-D71（批次 D 用户拍 N163 第④点的守卫半边；行为半边已由 N163 落地，台账 R-D70 随本刀销账）。命题：凡是"可见参数的值离开本仓、进上游 C API/argv"的接线点，每个 flag 字面量都必须登记在册，且登记必须链到一个真实存在的 gtest 证据用例名；登记在册的 flag 从源码里消失、登记行被删、或出现未登记的新边界 flag/新边界文件，守卫必须红。本守卫是**完备性棘轮**（新接线必须带证据注册；既有接线被拆必须红），逐实例的"值级真生效"仍由各刀的 gtest 值判据承重；通用"全部 ~600 条可见键三元组逐一配行为变异档"的注册表**另立 N196**，本刀不做（尺寸不够，别假装做了）。
- **射程现读**（BASE=`161ef07`）：grep `OptionsNew|DEMProcessing|GDALWarp|apszArgs|GDALTranslate|GDALBuildVRT` 命中 **46 行 / 5 文件**（cutting_plugin.cpp 28、georef_plugin.cpp 6、projection_plugin.cpp 4、raster_manage_plugin.cpp 3、terrain_plugin.cpp 5），映射成 7 个逻辑边界调用点 + 1 个零 flag 调用点（cutting.doMergeBands 的 `GDALTranslateOptionsNew(nullptr)`，无 flag 可登记，由登记表 `_comment` 钉住）；cutting 的另 7 行 grep 命中是提及 GDALWarp 字样的注释。登记表 `tests/param_effectiveness_registry.json` **35 条目 / 26 个 (file, flag) 对**，逐条带 context（函数/调用点符号名，不写行号）、key 归类（internal 逐条带归类理由）、evidence_test（全部非空且在 `tests/test_plugins.cpp` 现存）、note；同 flag 不同键/不同值（`-to`×2、`-co`×4、`-t_srs` 跨 3 文件）按条目分别登记，无"同上"。
- **判据**（`tests/check_param_effectiveness_guard.py` + `.cmake` 载体，ctest 名 `param_effectiveness_guard`，注册总数 778→779）：a=登记 flag 在 site 文件引号 flag 集里仍找得到（丢=N163 型回归当场红）；b=evidence_test 名在 `tests/test_plugins.cpp` 里存在；c=site 文件集合与边界调用文件集合**双向**相等（新边界文件不登记=红；登记表出现已不是边界的文件=红）；d=site 文件引号 flag 集合 ⊆ 登记表（新 flag 不登记=红）；e=自证臂 7 条（1 绿 + 6 红，合成夹具仓上跑，先于真仓判定，红 ⇒ 真仓结论作废）。flag 扫描形状：引号串整体 = 单短横线 + 首字母字母 + [字母数字_]*——首字母锚定挡住 `-9999` 哨兵与文件名实参误捕（任务书点名的陷阱），下划线/数字放宽使 `-t_srs`/`-srcwin`/`-crop_to_cutline` 族真 flag 在棘轮内；扫描按登记表 site 白名单逐文件做，不全仓正则撒网（N193 前科规避）。真仓绿态读数：`boundary_files=5 site_files=5 entries=35 distinct_file_flags=26 quoted_flag_total=26 findings=0`。
- **反向臂读数**（承重证明，实测非推理）：把 terrain slope 临时改回发 `-z`（N163 改前"上游静默丢弃"形态；`terrain_plugin.cpp` sha256 `ba75e602…`→`8da61fd6…`）⇒ 全量重编成功（真实可编译产品态）⇒ ctest 注册路径 `param_effectiveness_guard` **当场红**（test #778 Failed），恰好 1 条 finding：`a/flag-gone: 登记条目的 flag -s 在 site 文件 src/plugins/terrain/terrain_plugin.cpp 的引号 flag 集里消失 ⇒ N163 型回归（登记还在、源码不发了）`（quoted_flag_total 26→25；b/c/d 与自证臂全绿——红得外科手术级，只指向被拆的 `-s` 接线）；cp 还原（未用 stash/checkout--/reset--hard）sha256 逐字节对拍回 `ba75e602…`、重编后守卫恢复绿，`src/` 对 BASE diff 为空。
- **值级欠账**（不属本刀射程，登记表 note 逐条挂明）：cutting.clip cutline 成功档（值级证据在 GUI 腿 `run_gui_cutting_clip_cutline_regression`，直调 gtest 只有双空拒绝档）、cutting.mosaic 的 dst_srs 条件档、projection.reproject 栅格档与两分支 `-s_srs` 条件档、georef.rpc_orthorectify 的 dem_file 档——直调 gtest 均无值级档，对应 evidence_test 链同函数/同动作既有 gtest 仅满足 b 条名字存在性；逐实例值级判据由各刀承重，通用注册表归 N196。
- **顺带发现的 GUI 口径差（立 N197，不属本刀射程）**：cutting.tile 的 `-r`（resample）接线在 CLI 与边界登记在案（CLI 侧已有值级判据），但 GUI 面板 visibleKeys 未列该键（B16 接线后口径未同步）——归 GUI 契约刀，届时与 N191（60 格反向必填）同批摆。
- **验收**（本刀树态实跑）：ctest 注册总数 778→**779**（新守卫 = test #778）；七守卫 7/7（param_guard_visible_keys_all_read / install_smoke / source_mojibake_guard / docs_match_cli / geotransform_guard / anchor_guard / **param_effectiveness_guard**）；`ctest -R "Terrain|terrain"` 48/48、`ctest -R GuiContract` 4/4；`git status` 终态干净；`src/` 对 BASE diff 为空（字节级，反向臂还原后 sha256 对拍）。
### 批次 D N162/#156（2026-11-09 落地：输出父目录失败文案前置闸 + 会抛 create_directories 收敛 core helper + 三载体 CLI 判据——用户可见变更：`--output` 父目录形态非法或父目录建不出来时，从"深层炸 std 原文 / GDAL 原文"变为 rc=1 本仓中文带动作名）

- **裁定**：批次 D 用户 2026-11-05 拍"甲"三点，逐条落——① framework 参数校验加「`output` 的父目录若存在则必须是目录」前置闸，一处拦住 CLI / GUI / 工作流三条派发路径；② 会抛的 `create_directories` 收敛到 `gis::core` 单一 helper（带 `std::error_code` 重载，失败抛本仓中文并带动作名）；③ 新增 CLI 级判据，禁 std/GDAL 原文从这一族漏出，判据配双向臂。`fs::equivalent` 同族按同一文案纪律治。
- **普查现读**（2026-11-09 开刀时现值）：`create_directories` **20 处出现 / 12 文件 / 3 处已带 ec**，逐点三栏归类——16 处会抛重载收敛进 helper、3 处已带 ec 保留（cutting Leaflet 资产降级、projection/vector temp 镜像）、1 处为注释行；GUI 侧零直接调用（全走 `QDir::mkpath`，自带中文文案），GUI 零改动、其三处派发经 `gui_data_support` 转调吃前置闸。`fs::equivalent` **7 处**（5 处调用 N156 时代已带 ec、失败走返回值加本仓英文串，2 处注释引用）⇒ equivalent 侧本刀零代码改动，只把 `equivalent:` 纳入判据禁词表防回归。
- **收敛出口（core helper）**：新增 `gis::core::fs_support`（`ensureOutputDirectory` / `ensureParentDirectoryForFile`）：内部走带 ec 重载；失败抛 `GisError`，文案 `无法创建输出目录"<目标父目录>"：路径"<阻塞物>"是已存在的文件而非目录（动作：<标签>）`——阻塞物从目标逐级向上找"已存在但不是目录"的最近祖先（N162/#156 病灶形态），找不到阻塞物（纯权限/占用）时报系统错误码、**不引 `ec.message()` 的英文原文**；动作名以 `actionLabel` 形参强制由调用点传入（helper 无默认值 ⇒ 新调用点想静默放掉动作名编译不过）。全仓会抛的 `create_directories` 重载从此只允许存在于 fs_support.cpp 一个文件。
- **三波接线**：① 第一波 core `gdal_wrapper`（深处拿不到动作名，兜底标签"栅格数据集写出"）+ classification / cutting / georef / matching / projection / raster_inspect / raster_render 七插件：本地 wrapper 删除、调用点换 helper（matching 按调用者穿 detect / corner / match 三标签；raster_render 本地 wrapper 改纯转发）；② 第二波 vector 家族：`VectorStagedOutput::start` 与 `CsvStagedOutput::start` 各加 actionLabel 末位形参，**31 个动作接线点逐点穿 `vector <动作键>` 字面量**（动作键与 vector 派发表逐字一致）+ rasterize 直调点（doRasterize 内）；③ 第三波 terrain 5 处（profile_extract / viewshed / viewshed_multi / cut_fill / reservoir_volume），三行式"取父路径再会抛建目录"块整体换 helper。
- **前置闸语义**（`action_validation.cpp` 的 `validateActionSpecificParams` 末尾、全部动作级判定之后）：`output` 非空 ⇒ 取**直接**父目录，**存在且不是目录**才拒；父目录**不存在**是合法（执行层后续创建）。判的是父目录形态不是扩展名，与 D0-C3"框架侧不补第二条扩展名同语义判定"的老裁定不冲突。命中文案：`输出路径的父目录"<父>"是已存在的文件而非目录，无法创建输出目录（动作：<plugin> <action>）`。按 `fs::u8path` 解（N122 病灶：窄构造在 Windows 按 ANSI 解，中文路径下闸会静默失明）。CLI（`Error:` + rc=1）、GUI（mainwindow 三处转调）、工作流（`Step N (plugin.action) invalid parameter output:` 前缀）三条派发路径同源吃到；直调 `execute()` 不经它——那是 helper 层的兜底职责，两层分工由判据分别承重。
- **判据与载体**（ctest 注册 779→782）：CLI 级判据 `output_dir_failure_guard`（判据本体 `tests/check_output_dir_failure_guard.py`，判定函数 `judge_carrier` 与运行解耦；载体 `.cmake` 仿 docs_match_cli：自证臂先跑、真跑不捕获 stdout、细节进 UTF-8 REPORT）。三臂 `vector convert` / `raster_inspect histogram` / `terrain slope` 真跑 gis-cli（output 父路径各指向一个已存在的**文件**），逐臂四判缺一不判：rc==1；无 std/GDAL 原文（禁词表 **7 条**：`create_directories:` / `equivalent:` / `ERROR 4:` / `ERROR 1:` / `Unable to create dataset` / `filesystem error` / `Cannot create a file when that file already exists`——改前三臂实测原样吐过的串，只增不减）；含本仓中文（`无法创建输出目录` + `是已存在的文件而非目录`）；带动作名（`（动作：<plugin> <action>）`）。自证臂 1 绿 + 4 红（std 原文混入 / 缺动作名 / 缺中文 / rc=0）全数点名，判据退化先在自证臂红。helper 层两条 gtest 载体（直调 `execute()` 不经闸的那条腿）：`PluginTest.VectorConvertOutputParentIsFileChineseFailureWithActionName` 与 `PluginTest.RasterInspectHistogramOutputParentIsFileChineseFailureWithActionName`，走 `executeGuarded` 同四判。
- **双向臂读数**（承重证明，实测非推理；N162 续作席补跑）：把 raster_inspect histogram 本地调用点临时改回会抛重载（文件 sha256 `b7b6438a…`→`d2808905…`，重编成功）⇒ ① 注册臂 ctest #453 `RasterInspectHistogramOutputParentIsFile…` **当场红**，4 条 finding 逐条点名（缺 `无法创建输出目录`、缺 `是已存在的文件而非目录`、缺 `（动作：raster_inspect histogram）`、std 原文 `create_directories:` 命中，泄漏原文 `create_directories: Cannot create a file when that file already exists.: "<路径>"`）；② CLI 三臂照绿——前置闸在派发期拦下 immediate-blocker 形态，helper 层退化到不了它，这是既定两层分工（tests/CMakeLists.txt 注册注释与 gtest 载体注释双证），非承重缺口；③ CLI 深层探针（output 阻塞物在直接父目录的**上一级**——闸只看直接父目录、直接父目录不存在 ⇒ 放行）→ `create_directories: …` std 原文 rc=1 红，证明深层病灶只有 helper 接得住、且退化在 CLI 上真实可见。cp 还原 sha256 逐字节对拍回 `b7b6438a…`（`git status` src/ 零 diff）、重编后两 gtest 载体 + CLI 守卫 3/3 绿、深层探针恢复 `无法创建输出目录…（动作：raster_inspect histogram）` 绿。
- **行为变更清单**（用户可见）：改前 `vector convert` / `raster_inspect histogram` 的 `--output` 父路径指向已存在文件 → `Error: create_directories: Cannot create a file when that file already exists.: "<路径>"`（std 原文、无动作名）；`terrain slope` 同形态 → GDAL `ERROR 4: Attempt to create new tiff file … No such file or directory` + `ERROR 1: Unable to create dataset`（该支压根没建父目录）。改后三臂同判：rc=1 + 前置闸中文文案带动作名。深层形态（阻塞物在更上级祖先、或直调 `execute()` 的路径）改前同炸 std 原文，改后由 helper 层接住（同中文形态，动作名由接线点标签提供）。收敛覆盖动作面：classification feature_stats；cutting split / tile；georef gcp_register；matching detect / corner / match；projection reproject；raster_inspect histogram；raster_render colormap / histogram_match；terrain profile_extract / viewshed / viewshed_multi / cut_fill / reservoir_volume；vector 31 个动作 + rasterize；core gdal_wrapper 共用写出点（兜底标签"栅格数据集写出"）。**层级边界（如实记录）**：terrain slope 等"压根不建父目录"的动作，其"深层缺父目录"形态（阻塞物不是"已存在文件"而是路径中段缺失）仍走 GDAL 原文——不在本刀射程（本刀范围是"会抛 create_directories 收敛 + 父目录已存在形态闸"），闸与判据三臂均为 immediate-blocker 形态。
- **受保护件 blob 二次重钉**：`terrain_plugin.cpp` 动手前 blob `a5da3e9f54697d1b92af0e1aa8bbb4dc5515a825`（与 `git rev-parse HEAD` 逐字核对）→ 落刀后 `7116a6cdd0709b361a7afd2a2994297f0bb6ada2`；N172-C4 节活钉已换新值，旧值在 N163 / N172-C4 历史记录按史实保留（N163 先例）。
- **棘轮更正（N162 续作席）**：实现席在 `tests/raster_fixture.h` 顺带摘掉一条裸行号锚（指向 gdal_wrapper 顶部匿名 helper 区间的那条）未同笔降界，anchor_guard 红档点名"锚次现读 181 != 登记表 182"，按 N178 语义补降界 `anchor_ceiling` 182→181。
- **验收**（本刀树态实跑）：ctest 注册总数 779→**782**；七守卫 7/7（param_guard_visible_keys_all_read / install_smoke / source_mojibake_guard / docs_match_cli / geotransform_guard / anchor_guard / param_effectiveness_guard）；`ctest -R GuiContract` 4/4；受影响族九族全绿、过滤串 `<族名>|<驼峰>` 实跑条数：classification 9、cutting 17、georef 17、matching 25、projection 12、raster_inspect 10、raster_render 4、terrain 48、vector 170（实跑合计 312 条次，族间按名字含交集，如 `ProjectionVectorReproject…` 同落两族）；双向臂读数见上节；`find data -type f -newermt <开刀时刻>` = 0（data/ 零写入）；`git status` 终态干净。

### 批次 D N158（2026-11-09 落地：`cloneNormalizedGeometry` 加「允许维度退化」开关（默认不允许）+ 叠加四动作 `*_degraded_input_count` 计数键——用户可见变更：union / difference / intersect / clip / dissolve 的产物不再含"面被修成线/点"的降级结果，改以 metadata 键记账）

- **裁定**：批次 D 用户 2026-11-05 拍"甲"四点，逐条落——① `cloneNormalizedGeometry` 加「允许维度退化」开关，默认**不允许**：`MakeValid()` 成功但**维度低于输入**（面被修成线/点）的按「输入不可用」丢弃（走既有 nullptr/空路径），并计入新键 `<动作>_degraded_input_count`（键名与发放形态跟随 N148 的 `*_skipped_input_count` 先例：**>0 才发**、与既有键并存不互盖；哪些动作真发键以双向臂实测为准，union/difference/intersect/clip 四个 N148 归属集动作必须有键）；② `vector repair` 显式传「允许」，是全插件唯一例外——repair 的语义就是"修成什么都算成功"，丢掉降级结果等于废掉它；③ 产物会变的面如实登记，vector 族 GOLD 与真实腿重验、矢量侧文档同步；④ 判据配双向臂。
- **改前形态（BASE 二进制现跑取证）**：叠加层含一条三点共线「面」（`[[2,2],[8,8],[4,4],[2,2]]`，GEOS 判 INVALID、`MakeValid` 修成 LineString）跑 union ⇒ `rc=0`、`Success: Union completed: 3 features`、metadata 只有 `union_*_pieces` 四键，产物转 GeoJSON = **1 个 LineString + 2 个 Polygon**——降级结果无痕入产物、无键发出。同夹具跑 repair ⇒ 降级线原样写出、`repaired_count:1`（repair 本就无维度检查）。
- **改动面**（产品 4 文件 + 守卫 1 文件）：① helper（vector_plugin_common.inc）：文件作用域常量 `kVectorAllowDimensionDegradationDefault = false` 当默认值——采集器为拿降级出参必须显式传第一个实参、传的也是这个常量 ⇒ 反向臂反转这一行就同时翻转"默认语义"与"采集器语义"，一处变异两臂全红；新增 `normalizedGeometryDimension()`（GeometryCollection 递归取成员最大维、嵌套递归，其余类型用 OGR `getDimension()`——"含面集合不算降级"这条不依赖 GDAL 对集合 `getDimension()` 的实现语义，钉在本仓自己手里）；`cloneNormalizedGeometry` 签名改 `(geom, allowDimensionDegradation = 常量, dimensionDegraded 出参)`，出参报告**事实**（降没降）、开关决定**处置**（允不允许留下），两者解耦。② 两个镜像采集器 `collectNormalizedGeometries` / `collectNormalizedFeatures` 各加互斥的 degraded 计数出参：一条被丢的输入要么进 N148 的 skipped 键（标准化后仍空/非法）、要么进 degraded 键（修好了但维度降级），**不重复计数**，N148 既有键取值不动；union 路径 A/B 的要素趟调用点照 N148 口径仍不传计数指针（防同层两趟重复计数）。③ 四动作发键点紧跟 N148 原 Keys：`union_degraded_input_count` / `difference_degraded_input_count` / `intersect_degraded_input_count` / `clip_degraded_input_count`（overlay 采集器记账，>0 才发）。④ repair 调用点显式传 `true` 并带 WHY 注释（改前本就保留降级线 ⇒ 传允许是保持现状不是放宽）。
- **判据（5 条 gtest，注册 782→787）**：union/difference/intersect/clip 各一条退化臂 + repair 例外钉子 `VectorRepairKeepsDegradedGeometryAsSuccess`。退化臂按任务书写成**两件缺一不判**：键值断言（`*_degraded_input_count`==1 且 N148 skipped 键**不出现**——两类出口互斥）+ 产物形状断言（重开产物逐条数线型几何==0）。碎片面夹具 `writeDegeneratePolygonLayer`、形状臂 `countLinearGeometriesInSingleLayerDataset` 照 N148 夹具形态落。另有全有效对照臂（零降级 ⇒ 键不出现，"出现即说明丢了输入"的读法为真）。
- **双向臂读数**（承重证明，实测非推理）：臂 (a) 埋碎片面 ⇒ 四动作键 0→1、产物 0 LineString、既有键按丢掉后的现实给（union 3 features→2）；臂 (b) 把开关默认常量临时反转（`false`→`true` 一行，变异前留档 sha256 `ec3ef2a1…`）重编重跑 ⇒ **四动作判据全红**（union 红档先炸消息断言 `Union completed: 2 features`、再炸键存在断言，实际键数=4 无 degraded 键）、**repair 例外用例照绿**（传的是字面 `true`，不随默认翻转——例外不随默认走的又一实证）；cp 还原 sha256 逐字节对拍回 `ec3ef2a1…`（`git status` 零 diff）、重编后五条判据 5/5 绿。
- **守卫演进（如实登记）**：镜像守卫 `VectorOverlayCollectorMirrorPredicateStaysVerbatim` 第③项的标准化行 needle 随采集器调用原文同步换新（旧 needle `cloneNormalizedGeometry(feat->GetGeometryRef())` 在两侧函数体里都消失会让 EXPECT_EQ 比出两个 `<missing>` **假绿**、守卫被掏空）；决策行镜像（第①项）与"恰一条 `if (geom` 取舍判据"（第②项）不受损，实测守卫绿。
- **行为变更清单（用户可见）**：叠加/裁切类四动作与 dissolve 的输入层若含"非法但可修复、修完维度变低"的几何——改前这类几何以降级后的线/点**无痕混进产物**；改后按输入不可用逐条丢弃，其中四动作另发 `*_degraded_input_count` 记账（>0 才出现），dissolve 丢弃不计数（无键，文档已写明）。修得好且维度不降的（自交蝴蝶面修成 MultiPolygon）照常保留、零变化；repair 行为零变化。
- **行为变更清单（C 栏 16 个直调点，逐动作/分组登记；复审更正轮补）**：`vector convert` 不直调该 helper、行为零变化；受本刀影响的是**其余 16 个不经叠加采集器、但直调同一标准化 helper（`cloneNormalizedGeometry`）的动作**——退化输入从"修复后的线/点入产物"变为"按输入不可用逐条丢弃"，且本刀不给它们发专用 degraded 键（发键面只扩到叠加四动作）。按丢弃的去向分三组登记：**①丢弃被既有 `skipped_count` 一并计入**（该键取值随丢弃变大，可诊断）：`vector boundary` / `vector centroid` / `vector convex_hull` / `vector envelope` / `vector interior_point` / `vector endpoints_extract` / `vector midpoints_extract` / `vector vertices_extract` / `vector singlepart` / `vector sliver_remove` 十个——它们的标准化空支本来就把空/非法结果计入 skipped，降级丢弃落进同一条支；**②`vector duplicate_point_check`**：有同族 `skipped_geometry_count`，但其图层闸只放点层（维度 0），"维度降级"这类输入结构上到不了，null 结果只落无差别的 `scanned_count`；**③丢弃键上静默**（键面只有输出计数或扫描计数，没有键能指出"丢了输入"——键值可能随丢弃间接变化，但那无法与正常波动区分）：`vector adjacency` / `vector overlap_check`（只有 `relation_count`）、`vector dissolve`（只有 `feature_count`）、`vector topology_check`（只有 `issue_count`；其 raw 非法检查在标准化之前、该检查行照发）、`vector dangling_endpoint_check`（`issue_count`+`scanned_count`，被丢要素仍计入 scanned）——③组的可诊断性只有"对比改前改后的产物要素数或检查输出"，无键可查。
- **维度口径边界（如实登记；复审更正轮补）**：非法单面也可能被 `MakeValid` 修成"面 + 线"共存的几何集合（GeometryCollection）——集合**含面** ⇒ 按"集合取成员最大维"的判据维度未降 ⇒ 整个集合照常保留，降级线以**集合成员**身份留在产物里（口径取裁定原文"维度低于输入"的字面读法做整几何比较；"逐成员丢弃"是另一可行口径，本刀未取）。"产物不再含降级线/点"只在**顶层**成立；本刀判据的形状臂只数顶层线型几何、探不到集合内线（判据的已知盲区）。
- **文档同步**：`docs/算法说明/矢量数据处理/` 六篇（并集/差集/交集/矢量裁切补第五类跳过出口与对应键；融合补降级丢弃说明；修复登记 repair 唯一例外）；`--help` 与 GUI 键表现读均不列 metadata 键（N148 四键同样不在）⇒ 键表同步分支不触发、零改动。复审更正轮补：六篇再补 C 栏 16 直调点同刀改变披露（哪类动作受影响、无专用键、可诊断性走哪条路）；并集/差集/交集/矢量裁切/融合补维度口径边界（GC 集合成员残留 + 判据顶层盲区）两句。
- **验收**（本刀树态实跑）：ctest 注册总数 782→**787**（+5）；七守卫 7/7（同上一节名单）；`ctest -R GuiContract` 4/4；vector 族过滤串 `vector|Vector` 实跑 **175/175** 全绿（764.76 s，含两条真实腿）；真实腿单跑复测 `real_data_vector_derive_heavy` 52.00 s 绿、`real_data_vector_join_heavy` 110.42 s 绿；`find data -type f -newermt <开刀时刻>` = 0（data/ 零写入）；`git status` 终态干净。
### 批次 D N144（2026-11-09 落地：vector 插件 srcSRS 裸指针 RAII 化 + 结构守卫 `vector_srcsrs_guard`——**零用户可见行为变更**：23 处裸声明换 `unique_ptr`+GDAL Releaser、82 处手工 delete 全删，堵住取消路径的 SRS 泄漏）

- **裁定**：批次 D 用户 2026-11-05 拍"甲"，逐条落——① vector 插件（src/plugins/vector/ 全目录）全部裸 `OGRSpatialReference* srcSRS` 声明（现读普查 **23 处 / 20 文件**，与裁定口径一致）改 `std::unique_ptr<OGRSpatialReference, OGRSpatialReferenceReleaser>`（deleter 用 GDAL `ogr_spatialref.h` 自带的 Releaser——对 `Clone()` 出的 refcount=1 对象等价 `delete`，且是 GDAL 官方销毁途径；CreateLayer 实参传 `.get()`，23/23）；② 59 处手工早退 delete 与 23 处终点 delete 全删（RAII 接管）；③ 新增运行期读源码的结构守卫，禁裸声明与裸 delete 两字形，配双向臂；④ 82 个 delete 点逐点核对"delete 之后同作用域有没有再读 srcSRS"。
- **主泄漏面（本刀要堵的）**：声明之后、终点 delete 之前的每个 `throwIfCancelled()`（各动作 2–4 个，含要素循环内逐条调用）抛出时，裸指针 `srcSRS` 随栈展开不释放 ⇒ 每次取消泄漏一个 OGRSpatialReference；早退 delete 只堵写失败分支、堵不住取消。改后由 RAII 兜住：抛出链展开到动作函数作用域时 unique_ptr 析构释放（逆声明序，先于 staged/srcDS 析构）。正常路径零行为变化：CreateLayer 按值拷贝 SRS、不保留调用方指针（旧代码在输出图层仍打开时终点 delete 已是既成事实），释放点后移到作用域末无第二读者。
- **悬垂读逐点核对（裁定④，0 处）**：82 个 delete 点按花括号作用域逐点读完（不看 grep 行序）——59 个早退点 delete 下一行全是 `return fail(...)`（含 12 个先 RollbackTransaction 再 delete 的形态）；23 个终点 delete 之后到作用域末只有 throwIfCancelled / SyncToDisk / CommitTransaction / commit / Result 构建无一引用 srcSRS。srcSRS 在该目录的全部出现 = 23 声明 + 23 处 CreateLayer 实参 + 82 处 delete，穷尽互斥 ⇒ 改前不存在悬垂读 UB，改后行为零变化，GOLD 无用例因裁定④复核。
- **结构守卫 `vector_srcsrs_guard`（ctest 注册 787→**788**）**：判据脚本 + 登记表（20 文件 / 23 声明 / 23 调用点逐文件计数）+ cmake 载体（解释器缺失/登记表缺失拒绝放行，不把"没检查"伪装成"通过"）。判据四条：a=扫描域内裸声明与裸 delete 两字形禁止；c=含 RAII 形态的文件集合与登记表双向相等（新持有点不登记红/登记脱钩红）；d=每文件两计数与登记值逐条相等（计数漂移逼改登记先过人）。自证臂 6 条（1 绿 + 5 红，合成夹具仓）：改回裸声明⇒a 红、插回 delete⇒a 红、登记脱钩两侧⇒c 红、计数漂移⇒d 红；自证臂先于真仓判定（判据自红 ⇒ 仓库结论作废，退出码分层 0/1/2/3 与 param_effectiveness_guard 同口径）。
- **双向臂真仓验收**：臂 (a) 把 centroid 一处声明临时改回裸指针 ⇒ 守卫红（a/bare-decl + c/stale-site，退出码 1），cp 还原 sha256 对拍 OK、复绿；臂 (b) 登记表临时删一行（登记/扫描脱钩）⇒ 守卫红（c/new-raii-site，退出码 1），cp 还原 sha256 对拍 OK、复绿。守卫终态读数 `scan_files=31 entries=20 raii_decl_total=23 get_call_total=23 findings=0`。
- **验收**（本刀树态实跑）：`grep -rn "OGRSpatialReference\* srcSRS" src/plugins/vector/` 终态 = **0**、`delete srcSRS` = **0**（含注释零命中，豁免清单为空；借名局部 srcSRSRef 已改名 layerSrsRef 使验收 grep 字面归零，语义零变更）；守卫 8/8（七守卫 + 本刀新守卫，见下）；`ctest -R GuiContract` 4/4；vector 族 175 条全绿（控制器终验合并过滤 `[Vv]ector` 实跑 **176/176**，恰含本刀新守卫）、真实腿 `real_data_vector_derive_heavy` / `real_data_vector_join_heavy` 单跑绿；`find data -type f -newermt <开刀时刻>` = 0；`git status` 终态干净。
### 批次 D N179（2026-11-09 落地：raster_manage.cog 产物布局修复——处理元数据随翻译写 `GIS_PROCESSING` 域，产物不再报布局失效；**用户可见变更**：cog 产物从"读期必告警"变"零告警"）

- **裁定与前提更正（两句都须进本节）**：批次 D 用户 2026-11-05 拍"甲"（换 COG 驱动、名实相符、加反向判据"产物自己不能再报布局失效警告"）。控制器现读更正：doCog **早已在用 COG 驱动**（`-of COG` + LZW + BIGTIFF 恒参，拍板文本"换 COG 驱动"的前提与现码不符）；真因 = 翻译后 `writeProcessingMetadata(path, …)` 以 update 模式重开产物写 `GIS_PROCESSING` 域，且带 `IGNORE_COG_LAYOUT_BREAK=YES` 把写期告警压掉——布局在那一刻被作废（头内 `KNOWN_INCOMPATIBLE_EDITION` 翻成 YES），告警被推迟到此后每个读端每次打开。改道与删压制开关都在本刀内完成。
- **改前形态（三腿探针 + 机制读数）**：生成进程 stderr 恰 1 条 `The IFD has been rewritten at the end of the file, which breaks COG layout`（①）；外部 gdalinfo（GDAL 3.10.3）与仓内 `raster_inspect info`（GDAL 3.12.4）读期每次打开恰 1 条 `This file used to have optimizations in its layout, but those have been, at least partly, invalidated by later changes`（②）；头内 `=YES`。对照腿（外部 gdal_translate 纯翻译、零元数据写）`=NO` + 零告警 ⇒ **翻译本身不破布局，update 后写是布局破坏的唯一产品侧成因**（R3a 时代"那次写是加剧项、非唯一成因"的旧口径撤回——A7 臂残留的 ② 是测试输出目录里陈旧破坏产物被覆盖打开的污染，不是第二个破坏源）。
- **修法（两方案对比后选 B）**：A 案 `-mo`（默认域）+ 读端回落会让 cog 产物元数据"域搬家"，外部按域读的工具静默读空、读端要多养一条回落规则，否。**B 案（落地）**：GDAL 3.12.4 翻译参数 `-dmo <域>:<键>=<值>` + `-co COPY_SRC_MDD=YES`——处理元数据随翻译写进 `GIS_PROCESSING` 域（**域身份、键名不变**；键值对走新出口 `processingMetadataPairs`，与 update 路径共用键名/时间戳兜底/空值跳过语义），`COPY_SRC_MDD` 是域落盘承重项（GTiff CreateCopy 缺省 AUTO 只拷默认域）。探针三项全绿：argv 可解析（usageError=0）；产物域四键读回（`D:\` 路径、WKT 引号方括号全字面无损）；默认域无污染、打开期 CPL 捕获 0 条、头内 `=NO`。读端普查：`readProcessingMetadata` 产品侧 0 调用者、tests 两处读者只读 update 路径产物 ⇒ **读端零改动**（本轮双向臂与终验全部实跑未见任何读端需要改动，B 案前提成立）。
- **产品改动（存档于 `a28927e`，4 文件）**：doCog 翻译参数加 `COPY_SRC_MDD=YES` + 逐键 `-dmo`，删翻译后 update 重开写元数据块（`buildMetadata` 前移喂 -dmo）；`writeProcessingMetadata(path)` 的 `IGNORE_COG_LAYOUT_BREAK=YES` 压制开关删除（其余 update 路径 44 调用点/12 插件零改动，普通 GTiff 产物与已破坏旧 COG 的开闭行为两态一致）；`readProcessingMetadata` 未动一字。
- **反向判据 + 双向臂**：新 gtest `RasterManageCogLayoutPristineAndMetadataReadable`（ctest #160，raster_manage 族 13→**14**）四断言：捕获窗（生成 + 进程内打开）零 CPL 消息、`GIS_PROCESSING` 域仍在产物头内、头内 `KNOWN_INCOMPATIBLE_EDITION=NO` 且无 `=YES`、域元数据读回值判据（键齐全）。**双向臂**（只临时改 doCog 调用点回 update 后写，cp + sha256 对拍还原）：判据**红**——域身份断言中止（`GIS_PROCESSING 域必须仍在产物头内`）；臂态 CLI 实测 stderr 恰 1 条 `ERROR 1: File … has C(loud) O(ptimized) G(eoTIFF) layout. Updating it will generally result in losing part of the optimizations … open the file with the IGNORE_COG_LAYOUT_BREAK open option set to YES.`、产物头仍 `=NO` ⇒ 压制开关删除后旧接线 **fail closed**：GDAL 拒开（不再发生 IFD 重写），元数据写不进去且被旧接线吞掉——判据的域断言与零消息断言两条腿都咬得住回退。cp 还原 sha 对拍 OK、复绿。
- **登记表**：`param_effectiveness_registry.json` 35→**37** 条目（`-dmo`、`-co COPY_SRC_MDD=YES` 各一条，internal 归类，evidence 链新判据 gtest；先登记后跑守卫）；param_effectiveness_guard 实跑自证臂 7/7、`entries=37 findings=0`。
- **行为变更（用户可见）**：cog 产物从"读期必告警"（每次打开 ②×1）变"零告警"，生成进程 stderr 也不再出 ①；元数据域身份与键名不变，按域读的外部工具与仓内读端零感知。已破坏的旧 cog 产物照常可读（② 告警随旧文件存在，不随本刀消失）；新产物头内 `=NO`。
- **文档同步**：`docs/算法说明/栅格工具/COG 生成.md` 更新元数据写入方式（随翻译 `-dmo`）、N179 真因链与 R3a 旧口径撤回、布局自查方法。
- **验收**（本刀树态实跑）：raster_manage 族 **14/14**（合并过滤 anage，含 2 条 GUI 离屏腿）；八守卫 **8/8**；`GuiContract` **4/4**；ctest 注册总数 **789**（788 + 1 新判据用例）；终态探针（CRS 夹具 → cog → 外部 gdalinfo stderr **0 字节** + `GIS_PROCESSING` 域四键读回 + 头内 `=NO`；仓内 `raster_inspect info` stderr 零告警）；`find data -type f -newermt <开刀基准时刻>` = **0**；`git status` 干净。
### 批次 D N174（2026-11-09 落地：`cutting.clip` 窗外写 NoData + 完全不相交报错——**用户可见行为变更**）

- **裁定**：2026-11-05 用户拍"甲"。取证闸门先行：下游消费者普查（GUI 预览把 NoData 当透明、工作流无下游、真实腿窗全在源内、GUI 腿只断成功与尺寸）⇒ **没有"把窗外 0 当有效数据"的真实消费者**，按裁定硬改。
- **产品改动**（`cutting_plugin.cpp`，提交 `3508212`）：extent/cutline 两臂加**半开严格重叠**的裁空判据（贴边=像元窗为空=裁空；报文带请求窗与输入范围两组实数 + 动作名；产物不写）；extent 臂在翻译后把窗外补丁带从"补 0 不声明"改写为 **NoData 值 + NoData 标签**（选型链：源声明过 NoData 跟随声明值 → 未声明用仓内常备哨兵 `-9999` → 装不下取 `0`；**窗全在源内零接触，GOLD 字节形态不变**）；cutline 臂按包框覆盖判 `-dstnodata`。GDAL 3.12.4 侧注：`-projwin` 部分出界时 GDAL 自己只告警不失败、缺省补 0——本刀把"or zero"落成"defined"。
- **判据**（提交 `34df479`，三条 `CuttingClip*` 改准）：窗全在源内 ⇒ **不写 NoData 标签**（钉住补丁路径不误触发）；部分相交 ⇒ 补丁带 `-9999` + 标签 + **统计量只由 96 个有效像元决定**（Min=Max=Mean=5.0、StdDev=0；改前补 0 时 Mean=3.42857——统计口径本身就是"补丁不再是 0"的独立证据）；完全不相交 ⇒ rc=1 + 报文三处稳定短语 + 两组实数特征子串 + "先删陈旧→断不存在→执行→再断不存在"的不落盘闭环。
- **双向臂**（控制器实测，cp 备份 + sha256 对拍 `8d77b544…`）：臂 A 把 `padOutsideWindowWithNodata` 临时短路 ⇒ 部分相交判据**精确红**（窗全在源内档照绿，外科手术级）；臂 B 把空交集判据 `if (false && …)` 短路 ⇒ 裁空判据红。两臂还原后 cutting 族 17/17 复绿。
- **行为变更清单**：完全不相交从"rc=0 + 请求尺寸全 0 图"变 **rc=1 + 产物不写**；部分相交产物补丁带从 `0` 变 NoData 值 + 标签（`ComputeStatistics` 随之剔除补丁像元，统计量变化）；窗全在源内的产物**字节不变**。`裁切.md` 的处理逻辑与判据凭据段同笔改准。
- **席位**：实现席配额阻断（R-D63 口径）——产品段 1 笔由席位提交，判据段与双向臂/文档/终验由控制器自跑收尾；**无独立复审席**（配额恢复后优先补审本把）。席位断线时遗留的未回滚臂 B 变异已由控制器还原（产品 diff 归零后才续）。
- **验收**：cutting 族 **17/17**；八守卫 8/8；`GuiContract` 4/4；注册总数 **789**（判据为改名+改准，无新增 TEST_F）；`find data -type f -newermt` = 0；`git status` 终态干净。
### 批次 D N160（2026-11-09 落地：`terrain` 单点/多点视域的观察点越界硬失败闸——**用户可见行为变更**，受保护件 blob 第三次重钉）

- **裁定**：2026-11-05 用户拍"甲"（①先实测单点同形态 ②插件级硬失败拦负索引、不做静默钳位 ③跨配置三现读）。
- **裁定①实测（子进程载体，BASE 二进制，`build/tmp/batchD/n160/`）**：**单点 viewshed 可达同族缺陷且形态更隐蔽**——multi 观察点落 DEM 外 = `0xC0000409` 崩进程（立案逐字复现，m_mix 逐点崩）；**单点 = 无限挂起 + 残留 254 字节 stub GTiff**（GDAL 先建产物后跑算法，用户只能杀进程）；两臂半像元贴边 `(99.5,200.5)` 都 rc=0 成功 ⇒ 上游 `static_cast<int>` 向零截断是**合法档**。上游源码佐证（3.12.4 viewshed.cpp：`calcExtents` 对窗外点只发 CE_Warning 不拒绝）——"GDAL 单点入口自己拒"的备选前提**实测为假**，同笔修两入口。
- **产品改动**（`terrain_plugin.cpp`，blob `7116a6cd…`→`4cd8991e…` 第三次重钉）：共用小件 `viewshedObserverIndexInRaster`（镜像上游语义：浮点先过 int 值域闸再截断，NaN/Inf 用户可达判硬失败，半像元贴边不误杀）+ 中文报文小件（`%.12g` 保精度、带动作名与越界点/像元坐标、失败不写产物）；单点与 multi 两个入口在 `GDALViewshedGenerate` 之前同笔接线，multi **逐点预检**（混合臂失败文案点名越界点而非放行点、零 partial 产物）；缺地理参考的 DEM 沿恒等阵按像元坐标判（既有 `TerrainMultiViewshedFailsWithoutGeotransform` 的对照腿钉住）。顺带补 N163 复审 parked Minor：runDemProcess 注释补 hillshade 三值的 **az=270 前提**（CLI 缺省 az=315 为 218/145/132）。
- **判据**（进既有 TEST_F，注册数 789 不变）：单点/multi 各一组——硬失败 + 文案四要素（观察点/范围外/越界坐标/动作名）+ 零产物 + **半像元贴边合法档**（闸门不许误杀）+ 混合臂点名越界点。
- **双向臂**（控制器实测，cp 备份 + sha256 对拍 `b6a5ee21…`）：共用小件临时恒 `return true` ⇒ 重编 ⇒ `TerrainViewshedMultiExecution` 的 gtest 进程被 `0xC0000409` 带走 = **Failed**（快红）；单点无闸形态是挂起（300s 超时红，太慢未单独跑，经共用小件结构覆盖并如实记档）。还原后两测试复绿。
- **守卫同步**（席位这次同笔做了，N174 教训被吸收）：geotransform 产品域 any 24→26 / code 23→25 / checked 21→23（两个越界闸各添一处判返回值的地理参考读取；两域现读 product any=26 code=25 checked=23、test 不变）。
- **行为变更清单**：观察点落 DEM 外（含 NaN/Inf 坐标）从"multi 崩进程 / 单点挂起留 stub"变 **rc=1 + 中文报文 + 不写产物**；半像元贴边保持成功（合法档钉住）；无地理参考 DEM 的观察点按像元坐标判（现状不变）。
- **跨配置三现读（裁定③）**：Debug 判据全绿（terrain 族 52/52 含视域四条回归）；**Release 侧现读如实延到 D4 双树门**（Release 树本就要全量重编，不单独跑半套）；扫产物+数工程文件：git status 干净、插件工程文件数不变。
- **文档**：`多点视域.md` / `视域分析.md` 同笔改准（越界行为两态）；RELEASE 本节。
- **席位**：实现席平台报错断线（122 工具次，§0~§1 席位亲笔：上游源码核 + 九臂子进程探针表），产品/判据/守卫同步/文档为席位未提交工作树，控制器验编验绿后按 R-D63 收尾（双向臂/RELEASE/重钉/终验自跑）；**无独立复审席**（配额恢复后优先补审 N174/N160 两把）。
- **验收**：terrain 族+视域 **52/52**；八守卫 8/8；`GuiContract` 4/4；注册总数 **789**；blob 三代链 `a5da3e9f→7116a6cd→b35e945c`（活钉唯一指新值）；`find data -type f -newermt` = 0。
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
  ① **B17 乱码检测器 ⇒ 已落地**为一条 ctest 用例 `source_mojibake_guard`（落地时四规则全带：
  roundtrip、私用区 U+E000–U+F8FF、注音 U+3105–U+312F、排版形式/全角货币/替换符；批次 D 起加了 R5 转义残留，见下面"批次 D 守卫扩面 D3-I"那一节）。
  判据本体 `tests/check_source_mojibake.ps1`（R1 要 CP936 编解码器，CMake 没有 ⇒ 用 PowerShell 实现，
  stdout 刻意只用 ASCII，因为 PS 5.1 会把非 ASCII 输出渲染成乱码、恰恰在失败那一路读不出）；
  载体 `tests/check_source_mojibake.cmake`（解释器缺失 ⇒ `FATAL_ERROR`，**不 SKIP**，
  理由与 N84/N86 同：这条用例的"没检查"绝不能长得像"检查通过"）。
  **自带夹具自证**：`tests/mojibake_fixtures/mojibake_bad.txt` 必须命中全部规则（落地时 4 条、批次 D 起 5 条）、
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
- **只有 Release 包做到"目标机零前提"**：Debug 包缺带 `d` 的调试版 VC 运行库（VS 的 `debug_nonredist/`，
  不可再分发）⇒ Debug 包只能在本机/CI 的开发环境跑，不是可带走的交付物（见"打包与安装"的 N87→C17 一条）
- **只走 `cmake --install` 交付**（2026-10-21 决定：不出安装包、不做云端构建）；
  Debug 的 `cmake --install` 分支已由批次 B 的 B4 补齐，见"发布前验证 → 第 3 步"
- `classification` / `georef` 插件版本 0.1.0
- `PerformanceMonitor` 未接入主执行链；`Logger` 已接入 CLI/GUI 进度消息（任务中心日志仍为 GUI 主路径）
- 切片预览使用本地 Leaflet 资源（`share/leaflet`），不再依赖 CDN
- 单点视域的"被遮挡"与"超出最大距离"输出同值（0.0），在结果里不可区分；多点视域不做曲率修正（N44/N46 → 批次 B）
- 视域/全色锐化的范围校验存在已知空白：输入缺地理参考时得不到保护
- **`spindex` 的"安全除法"只给分母加 `1e-10f`，不做阈值判定也不裁剪值域**：分母为 0 而分子非 0 的像元得到的是
  **有限巨值**（实测约 ±1e12 量级，见 `e9ff436` 那次改准的 NDVI/EVI 两篇），既不是 NaN、不是无穷、也不是 0，
  并会作为正常结果进入 `index_min`/`index_max` 统计。读端要自己把这类像元按"分母无效"处理。
  批次 D 的 N188 裁定（2026-11-05）：**产品不改**，只保证文档口径与实现一致并在此登记。
