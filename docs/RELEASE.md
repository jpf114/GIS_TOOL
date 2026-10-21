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
跑全量前先用 `-N` 记注册数：**当前基线 Debug 692 / Release 690**（2026-10-21 现读，本机两棵树各跑
`ctest --test-dir build/<树> -C <Config> -N | tail -n 1`）。Release 少的那 2 条**必须按名字核、不能只看总数**
（只看数会漏"某条在 Release 静默消失"）：`comm -23` 实测差集恰为
`gui_matching_stitch_debug_failure_offscreen` 与 `gui_matching_ecc_register_debug_failure_offscreen`，
反向 `comm -13` 为空 ⇒ 这两条是 Debug 轻量闸门用例（见"批次 A 发布说明 → 基线口径"）。

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
#   注意：这只影响本机。CI 的 Install Release 步骤不带 --prefix ⇒ 写到它自己的默认安装目录，
#   与本仓库根的 install/ 不是同一个地方，别拿本机的 install/ 去推断 CI 装了什么。
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

## NSIS 安装包

前置：

1. 已完成上面**第 2 节步骤 2.0**（配置日志里有 `Runtime dep scanner: <绝对路径>`）——没有它 `cmake --install` 会拒绝安装，`makensis` 拿到的 `install/` 是空的或旧的。
2. 已安装 [NSIS](https://nsis.sourceforge.io/)，且 `makensis` 在 PATH 中。

```powershell
cmake --install build/release --config Release --prefix install
makensis installer\installer.nsi
```

产物：`installer/gis-toolkit-1.0.0-win64-setup.exe`（约 64MB，zlib 压缩）。

⚠ 安装包**不含 VC++ 运行库**（`MSVCP140.dll` 等被 `POST_EXCLUDE` 按设计排除）⇒
**目标机需自行安装 VC++ Redistributable**，见"批次 A 发布说明 → 打包与安装"。

CI 参考：[.github/workflows/windows-build.yml](../.github/workflows/windows-build.yml) 中 `Build installer` 步骤。

## GitHub Release

1. 推送 `master` 并打标签：`git tag v1.0.0`
2. 在 GitHub 创建 Release，关联标签 `v1.0.0`
3. 附资产：
   - `gis-toolkit-1.0.0-win64-setup.exe`（NSIS）
   - `gis-tool-win64.zip`（CI artifact，含 `install/` 树）
4. 发布说明正文：**以本页"批次 A 发布说明"为最新口径**，可与之合并 [v1.0.0发布说明.md](v1.0.0发布说明.md)、
   [CHANGELOG.md](../CHANGELOG.md) 或 [v1.0.0_GITHUB_RELEASE.md](v1.0.0_GITHUB_RELEASE.md)；
   那两份文件写于批次 A 之前，其中的行为描述凡与本页冲突者以本页为准。

---

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
  CI 的 `Install Release` / `Verify` / `makensis` / `Compress-Archive` 四步已**逐条断言退出码**。
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

- **现行基线**：`ctest -N` 注册数 **Debug 692 / Release 690**（2026-10-21 现读；批次 A 收尾时是 635 / 633，
  见下一条认证记录——本条只认现读值）；`comm -23` 差集仍恰为
  `gui_matching_ecc_register_debug_failure_offscreen`、`gui_matching_stitch_debug_failure_offscreen`（Release 侧无独有项）。
  ⇒ Release **恒少 2** 是注册数口径，不是"少通过 2 条"；两侧各自 100% 通过。
- **批次 A 最终基线认证（21-D 收尾现跑，两棵树重编后各跑一次 `-j1` 全量）**：
  Debug `100% tests passed, 0 tests failed out of 635`（382.54 s）、
  Release `100% tests passed, 0 tests failed out of 633`（188.41 s）；
  两棵树的构建日志均 **0 warning / 0 error**；日志 `build/t21D_build_debug.log`、`build/t21D_build_release.log`、
  `build/t21D_ctest_debug_full.log`、`build/t21D_ctest_release_full.log`。
- **凡引用 `GOLD` 覆盖数，必须带 N90 限定。** 登记表的判据 `blockHasNumericAssertion`
  （`tests/test_coverage_meta.cpp:284-291`）是**子串匹配**，块体**注释里**出现 `EXPECT_NEAR` 字样也会被算作"有数值断言"
  ⇒ 在判据修好之前，"GOLD 覆盖 N 个 action"这类句子**不代表 N 个 action 真被数值钉住**。
  引用时请写成**登记表自报口径**：`tests/action_gold_cases.txt` 126 个数据行自报 **GOLD 52 / PENDING 74**（文件另有 23 行注释，`wc -l` 得 149）。
  计数请用 `grep -c ',PENDING$'`（=74）与"排除注释行后再数 `,GOLD`"（=52）；直接 `grep -c ',GOLD'` 会得 **53**（第 2 行注释含 `,GOLD`）。
  上限断言现值 `EXPECT_LE(pending, 74)`（`tests/test_coverage_meta.cpp:402`），只允许下降。N90 归批次 B。
- **冻结清单复核结论（21-D，只核不改）**：批次 A 全程 `tests/action_gold_cases.txt` 只有 Task 3 的两个提交动过它
  （`f1bb676` 建档、`987eaa9` fix round 1 只**加第 4 列证据用例名**）；
  两版的 GOLD/PENDING 都是 **52 / 74**，`diff` 只显示列数变化 ⇒ **没有任何 action 从 PENDING 变 GOLD**（"不反向扩标"守住了），
  **不需要动上限断言**。另核：`EXPECT_LE(pending, 96)` 那一版从未进过代码
  （`git log -S'EXPECT_LE(pending, 96)' -- tests/test_coverage_meta.cpp` 零命中），96 只是计划期的估算。

### CI 超时余量（本机实测；远端待首跑验证）

- **本机 `gh auth status` 未登录 ⇒ 拿不到任何真实 CI 时长**，本节全部数字来自本地同机串行实测；
  凡涉及 CI 超时的说法都要写"**待远端首跑验证**"。
- **`TIMEOUT`（测试属性）是本工具链唯一的执法者**；`PROCESS_TIMEOUT` **无效** —— 它不是 CMake 属性
  （`cmake --help-property-list` 里有 `TIMEOUT`、`TIMEOUT_AFTER_MATCH`、`TIMEOUT_SIGNAL_NAME`、`TIMEOUT_SIGNAL_GRACE_PERIOD`，没有 `PROCESS_TIMEOUT`），
  接受了也不执行。现值：`GIS_TEST_TIMEOUT = 300`，configure 期用例经 `set_property(TEST …)`、
  gtest 发现用例经 `PROPERTIES`，两侧读同一个变量不会漂移。取证请用文本锚点（行号会随本文件增删失真，
  上一版这里写的 `:155-170,1449-1461` 就是漂了的）：`tests/CMakeLists.txt` 里搜
  `set(GIS_TEST_TIMEOUT 300)`、`gtest_discover_tests(gis_tests` 块内的 `PROPERTIES TIMEOUT`、
  文件末尾的 `set_property(TEST ${GIS_TIMED_TEST_NAMES}`。
- **余量算式**（`timeout-minutes: 20` = 1200 s 对本地全量墙钟；job 级 90 min 为整体上限）：

| 配置 | 注册数 | 本地全量墙钟（`-j1`、无并发构建） | 步骤超时 | 余量 |
|---|---|---|---|---|
| Debug | 692 | **346.11 s**（批次 B 全部落定后的最后一轮：`build/tmp/b20v_final2_debug.log`） | 1200 s | **1200 ÷ 346.11 = 3.47×** |
| Release | 690 | **174.16 s**（同一轮：`build/tmp/b20v_final2_release.log`） | 1200 s | **1200 ÷ 174.16 = 6.89×** |

  上表是**批次 B 全部修复落定后**那一轮的现跑（跑前两棵树都完整重建、跑前硬门控
  `tasklist | grep -cE 'MSBuild|gis_tests|ctest'` = 0，串行 `-j1`）：
  Debug `100% tests passed, 0 tests failed out of 692`（346.11 s），
  Release `100% tests passed, 0 tests failed out of 690`（174.16 s）。
  对账用的更早两轮：B20b 定稿那轮（`4591ee7` 的代码态、机器安静）Debug 347.66 s → 3.45×、
  Release 178.91 s → 6.71×；批次 A 收尾 21-D（635/633 条）Debug 382.54 s → 3.14×、Release 188.41 s → 6.37×。
  ⇒ 各轮 Debug 侧都在 3.1×~3.5×，**取最慢一轮也不改变结论**：`timeout-minutes: 20` 对 Debug 有 3 倍以上余量。
  ⚠ 还有一轮 **371.56 s**（B17 修复后那次全量）**没有**进上表，因为当时四个终审评审席正在并发读盘
  ⇒ 按本文件"全量耗时会被并发活动污染"那条纪律，它只能算**下限样本**，不能当安静值引用。
  `param_guard_visible_keys_all_read` 现跑 **3.44 s**（Debug）/ 3.42 s（Release），
  距 300 s 的 `TIMEOUT` 线有 **300 ÷ 3.44 = 87×** 余量；`install_closure` 现跑 11.62 s / 8.91 s（安装审计类，
  它自带 RESOURCE_LOCK、不会与别的用例抢同一个前缀），同样远低于 300 s。
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
- **N104（审计覆盖面，B6 取证时实测出）**：`runtime_dll_cross_config_audit` 只扫
  `${CMAKE_BINARY_DIR}/plugins` 与 `$<TARGET_FILE_DIR:gis-cli>` 两个目录，而
  `src/cli/CMakeLists.txt:21` 的 `gis_copy_minimal_runtime(gis-cli DEST_DIR $<TARGET_FILE_DIR:gis-cli>/plugins)`
  **每次构建都在往第三个目录写**，那个目录不在射程内。现读（`python` 递归比 vcpkg 两侧目录）：
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
- **N90**：`blockHasNumericAssertion` 可被注释满足（见上面"GOLD 限定"）。
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
- **待拍板、本批未实施**：① B17 乱码检测器未落库、未注册成 ctest（含正例夹具与 U+FFFD 规则的建议见决策稿 §6）；
  ② 仓库仍无 `.gitattributes`，8 个受版本控制的文件仍带 UTF-8 BOM（加 `* text=auto` 后下一次检出
  会有 **74 个**工作区文件换行尾 = 66 个 `w/lf` + 8 个 `w/mixed`，索引 0 条变化；
  见决策稿 §5）。

---

## 已知限制

- Windows 代码签名未配置（SmartScreen 可能提示）
- **安装包不含 VC++ 运行库**：目标机需自行安装 VC++ Redistributable（N87，见"打包与安装"）
- **NSIS 打包与 GitHub Release 出包只走 Release**（交付形态约束；Debug 的 `cmake --install` 分支已由批次 B 的 B4 补齐，见"发布前验证 → 第 3 步"）
- `classification` / `georef` 插件版本 0.1.0
- `PerformanceMonitor` 未接入主执行链；`Logger` 已接入 CLI/GUI 进度消息（任务中心日志仍为 GUI 主路径）
- 切片预览使用本地 Leaflet 资源（`share/leaflet`），不再依赖 CDN
- 单点视域的"被遮挡"与"超出最大距离"输出同值（0.0），在结果里不可区分；多点视域不做曲率修正（N44/N46 → 批次 B）
- 视域/全色锐化的范围校验存在已知空白：输入缺地理参考时得不到保护
