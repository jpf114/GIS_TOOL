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
跑全量前先用 `-N` 记注册数：**当前基线 Debug 730 / Release 728**（2026-10-23 批次 C C3l 收口轮现读；上一档 724 / 722 是同日 C3h-2/C3h-1b，再上一档 723 / 721 是 N122，两棵树各跑
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

- **现行基线**：`ctest -N` 注册数 **Debug 730 / Release 728**（2026-10-23 批次 C C3l 收口轮现读；相比上一档 724 / 722 **+6** = C3l 的六条
  `PluginTest.Vector{Buffer,Clip,Dissolve,Union,Difference,Intersect}GpkgSpatialIndexIsPopulated`（逐名比对，删除 0 条，
  两树名单差集仍恰为下面那两条 Debug 轻量闸门、`comm -13` 空，快照 `build/tmp/c3l2_names_{debug,release}.txt`）；
  上一档 724 / 722 是 C3h-2 收口轮（C3h-1b 复验同值——那一刀只翻登记表一行的状态列，不增删用例）；
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
| Debug | **730** | **939.02 s**（批次 C C3l 收口轮现跑，`build/tmp/c3l2_close_dbg.log` 不入库；跑前硬门控 = 0、串行 `-j1`、期间无并发构建） | 730 条**首档**；同一批用例在 C3l 首轮（含 `param_guard` 一红）是 **982.18 s**（`build/tmp/c3l_close_dbg.log`，729/730）；再往前十二档（697~724 各计数）**702.01 / 741.52 / 761.78 / 732.32 / 761.30 / 754.82 / 851.06 / 936.24 / 1245.47 / 916.43 / 861.11 / 875.64 / 865.12 s** | 最慢 `real_data_vector_overlay` **244.04 s** ⇒ 670 ÷ 244.04 = **2.75×**；次 `real_data_vector_quick` **215.11 s** ⇒ 720 ÷ 215.11 = **3.35×**；`real_data_vector_checks` **35.72 s** ⇒ 100 ÷ 35.72 = **2.80×**；`source_mojibake_guard` **12.74 s** ⇒ 300 ÷ 12.74 = **23.5×**（上界取 `GIS_TEST_TIMEOUT`，`tests/CMakeLists.txt:157`） |
| Release | **728** | **436.46 s**（同一轮紧随其后跑，`build/tmp/c3l2_close_rel.log`；**728/728 全绿**） | 728 条**首档**；C3l 首轮（含 `param_guard` 一红）是 **479.64 s**（727/728）；再往前十一档（695~722 各计数）**392.67 / 367.32 / 361.72 / 372.09 / 365.34 / 471.77 / 420.27 / 737.79 / 469.43 / 562.99 / 449.02 / 463.39 s** | 最慢 `real_data_vector_overlay` **99.54 s** ⇒ 670 ÷ 99.54 = **6.73×**；`real_data_vector_quick` **87.34 s** ⇒ 720 ÷ 87.34 = **8.24×**；`real_data_vector_checks` **19.21 s** ⇒ 100 ÷ 19.21 = **5.21×**；`source_mojibake_guard` **12.30 s** ⇒ **24.4×** |
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
  - **批次 C N122 收口轮**（本表现值，产品修复 N122）：树上多了 `10fe182`（helper + 8 内联 + 24 处扩展名判定
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
  - **批次 C C3l 收口轮**（本表现值，产品修复 N138）：树上多了 `d583c9b`（六个 `.gpkg` 写出路径把索引交给驱动 +
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
  **但路径 A 的真实数据腿仍然没有**：现有 `union_focus` 是线 × 面 ⇒ 走路径 B，路径 A 在真实回归里仍是 0 覆盖
  ⇒ 排在 C3h-3（面 × 面真实腿，挂 `overlay` 那一半）。
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
  而输出文件在动作开头就被 `drv->Create()` 截断 ⇒ **失败时旧产物已经没了，新产物又不可用**。这是契约问题，要裁定不是补断言就能了。
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
  事务批量化一字未动 ⇒ **批量写与索引正确性不冲突**。另外 11 个不建索引的调用点源码与产物形态逐字不变
  （boundary/centroid/convex_hull/endpoints_extract/envelope/filter/interior_point/midpoints_extract/singlepart/
  sliver_remove/vertices_extract；改前改后"无 rtree"的图层名单一致）。
  判据是六条 `PluginTest.Vector*GpkgSpatialIndexIsPopulated`，按**读端性质**写：主判据 =
  `SetSpatialFilterRect(夹具全 extent)` 后 `GetFeatureCount()` == 由夹具坐标手算的期望数
  （buffer 200 / clip 1 / dissolve 1 / union 3 / difference 1 / intersect 1），索引行数只作解释性判据；
  旧态 5 条红、新态 6 条绿（两棵树），注册数 730 / 728。
  **真实数据侧的收口证明**：`tmp/vector_regression/union_focus.gpkg` 由 C3l 收口轮重跑生成，
  表 **22 855** 行 / rtree **22 855** 行（文件从 5 582 848 B 增至 8 085 504 B，多出的就是索引），
  官方 `ogrinfo -so -spat <全 extent>` 读回 **22 855** 个要素（改前该产物索引 0 行）。
  ⚠ **评审席把这条做宽了**：磁盘上同时留着改前（`build/tmp/c3a/legtest`，10-21）与改后（`tmp/vector_regression`，10-23）
  两轮真实腿产物 ⇒ **5 个动作都有同腿 A/B**（union 22 855 行，其余量级 15 077 / 22 836 一类）；
  本条最硬的证据是这张 5 动作表，不是上面任何单条。评审席同时判定**"对照②与对照③同机理、属重复"**、
  "对照①归档产物的行数（1 行）与本条目引用的 3 行不同源" ⇒ 结论不靠这三条撑（读端源码 + 5 动作真实腿撑住），
  台账目前写法**偏保守而非偏乐观**。
  ⚠ 评审席另给一条**比本刀论证更强的机制事实**：GDAL 的延迟建索引把"回填 / 登记扩展项 / 装触发器"放在同一段执行、
  任一步失败整段回滚 ⇒ 走驱动这条路**结构上不可能**再产出"登记了但是空的"rtree（旧写法是三段分开，才会留下半截状态）。
  **buffer 那条的证据折扣已销**（免构建复验，控制器 11:15 现跑）：复制改后产物 `e2e_buffer_index_output.gpkg`、
  `DELETE FROM "rtree_e2e_buffer_input_buffer_geom"` 清空索引 ⇒ `ogrinfo -so -spat <全 extent>` 读 **0**、
  不加过滤读 **200** ⇒ 主判据④在 buffer 上有自己的红态，不必再等"旧态红"。
  ⚠ **本条最该沉淀的方法**：31 个产物、两棵树几百条用例连续几周全绿，缺陷却在每一个受影响的 `.gpkg` 里——
  因为所有断言都是"我们写什么就读什么"（要素数、面积、字段名），**没有一条按读端会怎么用这个文件去查**。
  这次是靠引入仓库外的第二个客户端（vcpkg 的官方 `ogr2ogr`/`ogrinfo`）才打出来的；
  以后"产物格式合规"类判据一律配一个独立客户端，别再加第十条自产自读的断言。
  ⚠ 顺带修正一条扫描口径：**"索引行数 == 要素数"对含 NULL/空几何的图层天然不成立**——GDAL 触发器的条件是
  `WHEN new."geom" NOT NULL AND NOT ST_IsEmpty(new."geom")`，实测
  `e2e_singlepart_input`（3 行含 1 个 NULL ⇒ 索引 2）与 `t20_empty_point_input`（4 行含 2 个空点 ⇒ 索引 2）
  都是**正确状态** ⇒ 比的是"非空几何数"，不是行数；否则会把合规产物读成缺陷（我自己差点把它记成第 2、3 条红）。
  ⚠ 仍待裁（不在本刀）：要不要给那 11 条路径也配索引——今天是"合法但无索引"，读端靠全表扫描，慢但结果对。
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
