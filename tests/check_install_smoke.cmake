# ══ C19：裸环境"带走"验收（install_smoke）════════════════════════════════════════
# 判据本体。WHY 写成 cmake -P 脚本而不是工单里点名的 tests/check_install_smoke.ps1：
#   ① 本仓另外三条交付/文档判据（install_closure、docs_match_cli，以及 mojibake 的 cmake 包装）
#      都是 `cmake -P`，"归零闸 / 参数缺失即红 / 失败文案"这套规矩只有一份；再开一支 PowerShell
#      就要同时维护两套"0 条到底算绿还是算红"的语义。
#   ② 本机 PowerShell 是 5.1：按 ANSI 解无 BOM 的 UTF-8 ⇒ .ps1 里写中文会整脚本 ParserError，
#      而 Get-Content 不带 -Encoding 会把 UTF-8 读坏（本仓因此报过假产品缺陷）。
#      本判据要**逐字节**比 CLI 输出，PowerShell 的字符串/编码语义正是要避开的坑位。
# 工单 C19 的五项要求（裸环境、--list 逐字节、每插件至少一条、数据命中、GUI 起得来、全落 ctest）
#   一条没减，只换载体；差异记在 docs/superpowers/plans/2026-10-21-批次C…md 的 C19 条目。
#
# 复算（与 ctest 注册的那条完全同形）：
#   cmake -DPREFIX=build/tmp/c19_pkg -DCONFIG=Release -DBUILD_DIR=build/release \
#         -DREPO_ROOT=. -DCLI_BUILD=build/release/src/cli/Release/gis-cli.exe \
#         -DPLUGIN_DLLS="<分号分隔的 13 个插件 dll 名>" -P tests/check_install_smoke.cmake

foreach(_gis_req PREFIX CONFIG BUILD_DIR REPO_ROOT CLI_BUILD PLUGIN_DLLS)
    if(NOT "${${_gis_req}}" STREQUAL "")
        continue()
    endif()
    message(FATAL_ERROR
        "install_smoke：必需参数 -D${_gis_req} 缺失或为空 ⇒ 本判据压根没跑，判红而不是判绿。"
        "用法见本文件头部。")
endforeach()

file(TO_CMAKE_PATH "${PREFIX}" _gis_prefix)
file(TO_CMAKE_PATH "${BUILD_DIR}" _gis_build_dir)
file(TO_CMAKE_PATH "${REPO_ROOT}" _gis_repo)
file(TO_CMAKE_PATH "${CLI_BUILD}" _gis_cli_build)
# scratch 由 PREFIX 派生（不是固定目录名）：两棵树各自的前缀 ⇒ 各自的 scratch，
#   先后跑或并行跑都不会读到对方残留的 .aux.xml / 日志。
set(_gis_scratch "${_gis_prefix}_scratch")
set(_gis_bin "${_gis_prefix}/bin")

# ── ⓪ 自己装一遍，不吃别人的现成包 ────────────────────────────────────────────
# WHY：残留文件会替本轮顶罪（上一轮装好的东西这轮即使规则已不该让它进包也还在 ⇒ 假绿）。
#   与 install_closure 同一做法、同一理由。
file(REMOVE_RECURSE "${_gis_prefix}")
file(REMOVE_RECURSE "${_gis_scratch}")
file(MAKE_DIRECTORY "${_gis_scratch}")
execute_process(
    COMMAND ${CMAKE_COMMAND} --install "${_gis_build_dir}" --config "${CONFIG}" --prefix "${_gis_prefix}"
    RESULT_VARIABLE _gis_rc OUTPUT_VARIABLE _gis_log ERROR_VARIABLE _gis_log)
if(NOT _gis_rc EQUAL 0)
    message(FATAL_ERROR "install_smoke：cmake --install 自己就失败了（rc=${_gis_rc}）：\n${_gis_log}")
endif()

set(_gis_cli "${_gis_bin}/gis-cli.exe")
set(_gis_gui "${_gis_bin}/gis-gui.exe")
if(NOT EXISTS "${_gis_cli}")
    message(FATAL_ERROR "install_smoke：包里没有 '${_gis_cli}' ⇒ 交付形态不完整，后面无从判起。")
endif()
if(NOT EXISTS "${_gis_gui}")
    message(FATAL_ERROR "install_smoke：包里没有 '${_gis_gui}' ⇒ 交付形态不完整，后面无从判起。")
endif()

# ── ① 裸环境：只给包自己 + Windows 目录 ────────────────────────────────────────
# WHY 显式清环境而不是继承当前进程：本判据要回答的是"换台机器还动不动"。
#   继承 PATH 就是让开发机上的 vcpkg / VS / 已装的 Redistributable 替包顶罪
#   —— N84 那批"装出来了但起不来"的包正是在这种环境下绿过去的。
# WHY 用 set(ENV{…}) 而不是 execute_process(ENVIRONMENT …)：**execute_process 没有 ENVIRONMENT 这个
#   关键字**（本轮第一次跑就撞上了：`unknown argument "ENVIRONMENT"`）。本脚本跑在自己的
#   `cmake -P` 进程里，改自身环境不外溢，因此直接清 + 重设就是正确做法（与本仓给被 spawn 进程
#   注入环境只能用 set(ENV{}) 这条既有结论一致）。
set(_gis_bare_clear
    PATH GDAL_DATA PROJ_LIB PROJ_DATA
    GIS_TOOL_SETTINGS_INI GIS_CUSTOM_INDEX_PRESET_FILE
    QT_PLUGIN_PATH QT_QPA_PLATFORM QT_QPA_PLATFORM_PLUGIN_PATH
    VCToolsInstallDir VSINSTALLDIR PYTHONHOME PYTHONPATH)
foreach(_gis_v IN LISTS _gis_bare_clear)
    unset(ENV{${_gis_v}})
endforeach()
set(ENV{PATH} "${_gis_bin};C:/Windows/system32;C:/Windows")
set(ENV{SYSTEMROOT} "C:/Windows")
set(ENV{TEMP} "${_gis_scratch}")
set(ENV{TMP} "${_gis_scratch}")

# ── 判据 A：裸环境下包内 --list 与构树 --list 逐字节相同 ───────────────────────
set(_gis_list_pkg "${_gis_scratch}/list_package.txt")
set(_gis_list_build "${_gis_scratch}/list_build.txt")
execute_process(COMMAND "${_gis_cli}" --list WORKING_DIRECTORY "${_gis_repo}" OUTPUT_FILE "${_gis_list_pkg}" ERROR_FILE "${_gis_list_pkg}"
    RESULT_VARIABLE _gis_rc_pkg)
execute_process(COMMAND "${_gis_cli_build}" --list WORKING_DIRECTORY "${_gis_repo}" OUTPUT_FILE "${_gis_list_build}" ERROR_FILE "${_gis_list_build}"
    RESULT_VARIABLE _gis_rc_build)
if(NOT _gis_rc_pkg EQUAL 0)
    file(READ "${_gis_list_pkg}" _gis_pkg_text)
    message(FATAL_ERROR "install_smoke：裸环境里包内 gis-cli --list rc=${_gis_rc_pkg}：\n${_gis_pkg_text}")
endif()
if(NOT _gis_rc_build EQUAL 0)
    message(FATAL_ERROR "install_smoke：裸环境里构树 gis-cli --list rc=${_gis_rc_build} ⇒ 对照物本身不可用。")
endif()
file(SIZE "${_gis_list_pkg}" _gis_pkg_bytes)
file(SIZE "${_gis_list_build}" _gis_build_bytes)
if(_gis_pkg_bytes LESS 1)
    message(FATAL_ERROR "install_smoke：包内 --list 是 0 字节 ⇒ 一个插件都没枚举出来，归零闸。")
endif()
file(SHA256 "${_gis_list_pkg}" _gis_sha_pkg)
file(SHA256 "${_gis_list_build}" _gis_sha_build)
if(NOT _gis_sha_pkg STREQUAL _gis_sha_build)
    file(READ "${_gis_list_build}" _gis_build_text)
    file(READ "${_gis_list_pkg}" _gis_pkg_text)
    message(FATAL_ERROR
        "install_smoke：包内与构树的 --list 不逐字节相同（${_gis_pkg_bytes} B vs ${_gis_build_bytes} B）。"
        "\n\n【构树侧】\n${_gis_build_text}\n【包内侧】\n${_gis_pkg_text}")
endif()

# ── 判据 A2：名单里每个插件都必须真的被枚举到 ─────────────────────────────────
# WHY 光"A 相同"不够：A 只保证两侧一致，不保证**两侧都全**。
#   反向实测过：把包内 plugin_spindex.dll 改名后 --list 从 1852 B 缩到 1648 B、
#   且不再出现 spindex，而 gis-cli 仍 rc=0 ⇒ "rc=0 + 两侧一致"可以盖住"少装了一个插件"。
#   名单由 ctest 从 GIS_PLUGIN_TARGETS 派生后传进来（与安装期扫描根同源），这里不另抄一份。
list(LENGTH PLUGIN_DLLS _gis_plugin_count)
if(_gis_plugin_count LESS 1)
    message(FATAL_ERROR "install_smoke：-DPLUGIN_DLLS 为空 ⇒ 插件覆盖闸退化成什么都不判，归零闸。")
endif()
file(READ "${_gis_list_pkg}" _gis_pkg_text)
set(_gis_missing "")
foreach(_gis_dll IN LISTS PLUGIN_DLLS)
    string(REGEX REPLACE "^plugin_" "" _gis_key "${_gis_dll}")
    string(REGEX REPLACE "\\.[Dd][Ll][Ll]$" "" _gis_key "${_gis_key}")
    if(NOT _gis_pkg_text MATCHES "(^|\n)  ${_gis_key} - ")
        list(APPEND _gis_missing "  ${_gis_dll} -> 期望 --list 里有 '  ${_gis_key} - …'")
    endif()
endforeach()
if(_gis_missing)
    list(LENGTH _gis_missing _gis_missing_n)
    list(JOIN _gis_missing "\n" _gis_missing_text)
    message(FATAL_ERROR
        "install_smoke：有 ${_gis_missing_n}/${_gis_plugin_count} 个插件没出现在包内 --list 里：\n${_gis_missing_text}\n"
        "包内 --list 全文：\n${_gis_pkg_text}")
endif()

# ── 判据 B：三种数据模态各跑一条真动作 ────────────────────────────────────────
# 输入一律先复制进 scratch。WHY 不就地读 data/：raster_inspect info 会把统计写进输入旁边的
#   .aux.xml（N115 实测），"data/ 只读"只能靠"根本不指向 data/"来保证。
set(_gis_dem_src "${_gis_repo}/data/dem/srtm_59_05_20260424205042_preview8.tiff")
set(_gis_vec_src "${_gis_repo}/data/vector/中国_省.geojson")
if(NOT EXISTS "${_gis_dem_src}")
    message(FATAL_ERROR "install_smoke：栅格夹具 '${_gis_dem_src}' 不存在 ⇒ 判据 B 没有载体，判红而不是跳过。")
endif()
if(NOT EXISTS "${_gis_vec_src}")
    message(FATAL_ERROR "install_smoke：矢量夹具 '${_gis_vec_src}' 不存在 ⇒ 判据 B 没有载体，判红而不是跳过。")
endif()
file(COPY "${_gis_dem_src}" DESTINATION "${_gis_scratch}")
file(RENAME "${_gis_scratch}/srtm_59_05_20260424205042_preview8.tiff" "${_gis_scratch}/c19_dem.tif")
file(COPY "${_gis_vec_src}" DESTINATION "${_gis_scratch}")
set(_gis_dem "${_gis_scratch}/c19_dem.tif")
set(_gis_vec "${_gis_scratch}/中国_省.geojson")
set(_gis_reproj_out "${_gis_scratch}/c19_dem_4326.tif")

set(_gis_action_fail "")
set(_gis_action_ran 0)

# 两条 info 动作的判据是"rc=0 且 stdout 有内容"。WHY 用 OUTPUT_FILE 而不是 OUTPUT_VARIABLE：
#   判据量是**字节数**，而 cmake -P 的 message() 会把子进程的 UTF-8 经本地代码页重编码一遍
#   （本仓踩过：打出来是乱码、还会让人误判成产品缺陷）⇒ 内容落文件、判据只看字节，日志留在 scratch 给人看。
set(_gis_raster_log "${_gis_scratch}/raster_info.stdout")
execute_process(COMMAND "${_gis_cli}" raster_inspect info --input "${_gis_dem}"
    WORKING_DIRECTORY "${_gis_repo}"
    OUTPUT_FILE "${_gis_raster_log}" ERROR_FILE "${_gis_raster_log}.err"
    RESULT_VARIABLE _gis_rc)
math(EXPR _gis_action_ran "${_gis_action_ran} + 1")
if(NOT _gis_rc EQUAL 0)
    list(APPEND _gis_action_fail "raster_inspect.info rc=${_gis_rc} ⇒ 详见 '${_gis_raster_log}' / '${_gis_raster_log}.err'")
else()
    file(SIZE "${_gis_raster_log}" _gis_raster_bytes)
    if(_gis_raster_bytes LESS 1)
        list(APPEND _gis_action_fail "raster_inspect.info 的 stdout 是 0 字节 ⇒ 只 rc=0 不算跑通")
    endif()
endif()

# ⚠ 这里**故意给 vector info 传 --output**：vector 插件级把 `output` 标成必填（漂移清单 D1，
#   工单 Task C23 要放开的那条）⇒ 裸环境里 `vector info` 不传 output 会 rc=1。
#   本判据测的是"包能不能独立跑"，不是 D1 ⇒ 不把 D1 的修复混进交付判据；D1 放开后这个多余参数也无害。
#   实测 `vector info` **从不写 --output**（只往 stdout 打图层信息）⇒ 它的判据只能是 rc=0 + stdout 非空，
#   不能拿"产物文件存在"去判，否则判的是 D1 而不是交付。
set(_gis_vec_log "${_gis_scratch}/vector_info.stdout")
execute_process(COMMAND "${_gis_cli}" vector info --input "${_gis_vec}" --output "${_gis_scratch}/vector_info.unused.out"
    WORKING_DIRECTORY "${_gis_repo}"
    OUTPUT_FILE "${_gis_vec_log}" ERROR_FILE "${_gis_vec_log}.err"
    RESULT_VARIABLE _gis_rc)
math(EXPR _gis_action_ran "${_gis_action_ran} + 1")
if(NOT _gis_rc EQUAL 0)
    list(APPEND _gis_action_fail "vector.info rc=${_gis_rc} ⇒ 详见 '${_gis_vec_log}' / '${_gis_vec_log}.err'")
else()
    file(SIZE "${_gis_vec_log}" _gis_vec_stdout_bytes)
    if(_gis_vec_stdout_bytes LESS 1)
        list(APPEND _gis_action_fail "vector.info 的 stdout 是 0 字节 ⇒ 只 rc=0 不算跑通")
    endif()
endif()

execute_process(COMMAND "${_gis_cli}" projection reproject
    --input "${_gis_dem}" --output "${_gis_reproj_out}" --dst_srs EPSG:4326
    WORKING_DIRECTORY "${_gis_repo}"
    OUTPUT_VARIABLE _gis_out ERROR_VARIABLE _gis_out RESULT_VARIABLE _gis_rc)
math(EXPR _gis_action_ran "${_gis_action_ran} + 1")
if(NOT _gis_rc EQUAL 0)
    file(WRITE "${_gis_scratch}/fail_projection_reproject.log" "${_gis_out}")
    list(APPEND _gis_action_fail "projection.reproject rc=${_gis_rc} ⇒ 详见 '${_gis_scratch}/fail_projection_reproject.log'")
endif()
# 投影产物：reproject 是要落盘的 ⇒ 这条按"产物存在且非 0 字节"判（上面两条 info 动作判 stdout，不混用）。
if(EXISTS "${_gis_reproj_out}")
    file(SIZE "${_gis_reproj_out}" _gis_bytes)
    if(_gis_bytes LESS 1)
        list(APPEND _gis_action_fail "投影产物 '${_gis_reproj_out}' 是 0 字节")
    endif()
else()
    list(APPEND _gis_action_fail "投影产物 '${_gis_reproj_out}' 不存在")
endif()
# 三条动作的判据各就各位（info 两条判 stdout 字节、reproject 判落盘产物）⇒ 这里不再另设产物闸。
if(NOT _gis_action_ran EQUAL 3)
    message(FATAL_ERROR "install_smoke：判据 B 只跑了 ${_gis_action_ran}/3 条 ⇒ 归零闸。")
endif()
if(_gis_action_fail)
    list(JOIN _gis_action_fail "\n---\n" _gis_fail_text)
    message(FATAL_ERROR "install_smoke：裸环境里有动作没跑通：\n---\n${_gis_fail_text}")
endif()

# ── 判据 C：PROJ 数据必须来自包内（藏起来就必须失败）───────────────────────────
# 这条是判据 C 的全部价值：没有反臂，"reproject 成功"完全可能只是在用开发机上那份 PROJ。
#   本机实测藏起 <prefix>/share/proj ⇒ `PROJ: proj_create_from_database: Cannot find proj.db` ⇒ 失败。
# ⚠ GDAL 自己那份 share/gdal **没有**本机可证的廉价载体：藏掉它之后 reproject 与 raster_inspect info
#   仍然 rc=0（本轮实测）⇒ 这里不写"GDAL 数据命中"的断言，只在判据 E 里要求它随包在位。
#   把"存在"写成"被使用"就是本仓反复踩的那类假绿。
set(_gis_proj_dir "${_gis_prefix}/share/proj")
if(NOT EXISTS "${_gis_proj_dir}/proj.db")
    message(FATAL_ERROR "install_smoke：包内没有 share/proj/proj.db ⇒ PROJ 数据没随包，交付不完整。")
endif()
set(_gis_proj_hidden "${_gis_prefix}/share/proj_c19_hidden")
file(RENAME "${_gis_proj_dir}" "${_gis_proj_hidden}")
execute_process(COMMAND "${_gis_cli}" projection reproject
    --input "${_gis_dem}" --output "${_gis_scratch}/c19_dem_noproj.tif" --dst_srs EPSG:4326
    WORKING_DIRECTORY "${_gis_repo}"
    OUTPUT_VARIABLE _gis_hid_out ERROR_VARIABLE _gis_hid_out RESULT_VARIABLE _gis_hid_rc)
file(RENAME "${_gis_proj_hidden}" "${_gis_proj_dir}")
if(NOT EXISTS "${_gis_proj_dir}/proj.db")
    message(FATAL_ERROR
        "install_smoke：恢复 share/proj 后 proj.db 不在原位 ⇒ 本轮把包弄坏了，先修再谈判据。")
endif()
if(_gis_hid_rc EQUAL 0)
    message(FATAL_ERROR
        "install_smoke：藏掉包内 share/proj 后 reproject 仍然成功 ⇒ 它用的是**机器上的** PROJ，"
        "不是随包那份 ⇒'数据随包'不成立。输出：\n${_gis_hid_out}")
endif()

# ── 判据 D：GUI 在裸环境里起得来 ───────────────────────────────────────────────
# 注入两个环境变量是 N101/N102 那道守卫要求的：直接跑 --self-test 会 rc=3（守卫拒绝对真实
# 注册表/用户目录动手），那不是"包装坏"。把两个落点都指进 scratch ⇒ rc=0 只可能来自包能跑。
file(WRITE "${_gis_scratch}/c19_presets.json" "[]\n")
set(ENV{QT_QPA_PLATFORM} "offscreen")
set(ENV{GIS_TOOL_SETTINGS_INI} "${_gis_scratch}/c19_settings.ini")
set(ENV{GIS_CUSTOM_INDEX_PRESET_FILE} "${_gis_scratch}/c19_presets.json")
execute_process(COMMAND "${_gis_gui}" --self-test
    WORKING_DIRECTORY "${_gis_repo}"
    OUTPUT_VARIABLE _gis_gui_out ERROR_VARIABLE _gis_gui_out
    RESULT_VARIABLE _gis_gui_rc TIMEOUT 120)
unset(ENV{QT_QPA_PLATFORM})
unset(ENV{GIS_TOOL_SETTINGS_INI})
unset(ENV{GIS_CUSTOM_INDEX_PRESET_FILE})
if(NOT _gis_gui_rc EQUAL 0)
    message(FATAL_ERROR "install_smoke：裸环境里 gis-gui --self-test rc=${_gis_gui_rc}：\n${_gis_gui_out}")
endif()

# ── 判据 E：两类数据目录与 VC 运行库都在包里（存在性，不宣称"被用到"）───────────
foreach(_gis_dir share/proj share/gdal)
    if(NOT IS_DIRECTORY "${_gis_prefix}/${_gis_dir}")
        message(FATAL_ERROR "install_smoke：包里没有 ${_gis_dir}/ ⇒ 数据目录没随包。")
    endif()
    file(GLOB_RECURSE _gis_probe "${_gis_prefix}/${_gis_dir}/*")
    list(LENGTH _gis_probe _gis_probe_n)
    if(_gis_probe_n LESS 1)
        message(FATAL_ERROR "install_smoke：${_gis_dir}/ 是空目录 ⇒ 数据随包这条只到目录名，判红。")
    endif()
endforeach()
file(GLOB _gis_crt "${_gis_bin}/msvcp140*.dll" "${_gis_bin}/vcruntime140*.dll" "${_gis_bin}/concrt140*.dll")
list(LENGTH _gis_crt _gis_crt_n)
if(_gis_crt_n LESS 1)
    message(FATAL_ERROR
        "install_smoke：bin/ 下没有任何 VC++ 运行库 ⇒ C17（拍板 2）的随包没生效，"
        "干净机器会 0xC0000135。")
endif()

message(STATUS "[install_smoke] 裸环境通过：--list 两侧逐字节相同（${_gis_pkg_bytes} B，sha256=${_gis_sha_pkg}）")
message(STATUS "[install_smoke] 插件覆盖 ${_gis_plugin_count}/全部名单；动作 ${_gis_action_ran}/3 通过；PROJ 反臂 rc=${_gis_hid_rc}；VC 运行库 ${_gis_crt_n} 件随包；gis-gui --self-test rc=0")
# ⚠ 这条绿对两棵树的**含义不一样**，别读成"Debug 包也能带走"：
#   Debug 包的 PE 导入的是带 d 的调试运行库，那批文件不在包里（不可再分发），本机之所以还跑得起来
#   是因为这台机器装了 VS ⇒ 正是本脚本上面反复警告的"开发机替包顶罪"。
#   ⇒ "目标机零前提"这个结论只对 Release 成立；Debug 这轮绿只证明"包内自洽 + 插件与数据随包"。
if(CONFIG STREQUAL "Debug")
    message(STATUS "[install_smoke] 配置=Debug ⇒ 只判'包内自洽'；调试版 CRT 来自本机 VS，不算可带走（见上）")
else()
    message(STATUS "[install_smoke] 配置=${CONFIG} ⇒ 交付形态：目标机零前提这一条由本判据 + install_closure 共同承重")
endif()
