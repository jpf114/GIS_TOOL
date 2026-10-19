# 包内闭包断言（Task B5 / N86）
#
# WHY（这一判为什么必须存在）：批次 A 复审实测，把装好的包里的 Qt sqldrivers 插件删掉后，
#   CI 的 "Verify installed CLI and GUI" 仍然 OUTER_RC=0。原因是插件类/边车类资源
#   （Qt 平台插件、sqldrivers、自家 plugin_*.dll、PROJ/GDAL 数据）**不在任何 exe/dll 的导入表里**，
#   它们是运行时按名字加载的，所以"exe 起得来"这条判据在结构上就看不见它们的缺失。
#   本脚本因此有两半，缺一不可：
#     ① 存在性硬断言 —— 按 CMakeLists.txt / src/gui/CMakeLists.txt 的 install() 规则列出的关键资源；
#     ② 导入闭包断言 —— bin/ 下每个 PE 的每条直接导入必须"包内有同名文件"或"命中系统 DLL 白名单"。
#   ①管运行时加载的东西，②管链接期硬依赖的东西；只做②会漏 ①（正是 N86），只做①会漏"装了个起不来的 dll"。
#
# 用法（ctest 用例与 CI 调的是同一条命令，判据不漂移）：
#   cmake -DPREFIX=<安装前缀> -DCONFIG=<Debug|Release> -DSCANNER=<扫描器绝对路径> \
#         -DPLUGIN_DLLS=<插件 dll 名，分号分隔> \
#         [-DSCANNER_TOOL=<dumpbin|objdump>] [-DBUILD_DIR=<构树>] \
#         -P tests/check_install_closure.cmake
#   带 -DBUILD_DIR ⇒ 本用例**自己**先 `cmake --install` 到它专属的临时前缀（见 _own_prefix 那段），
#     这样"闭包是否成立"不再取决于别人有没有跑过安装步；
#   不带 ⇒ 只判已经装好的 PREFIX（CI 用它刚装出的交付前缀）。两种模式下 PREFIX 缺失都判红，
#     绝不把"没装过"读成"闭包没问题"。
#   -DPLUGIN_DLLS 必给（Task B6 / G-B5-1）：名单的唯一真源是 cmake/PluginTargets.cmake 的
#     GIS_PLUGIN_TARGETS，由根 CMakeLists.txt 在配置期派生；ctest 注册直接传该变量，
#     CI 从生成物 <build>/install_runtime_deps.cmake 里读烘好的同一条 set 行 —— 两个调用方
#     都不再自己抄一份。脚本这里只判"给没给、给的像不像插件名"，判不出插件少没少（那归派生处）。

foreach(_gis_arg PREFIX CONFIG SCANNER PLUGIN_DLLS)
    if(NOT DEFINED ${_gis_arg} OR "${${_gis_arg}}" STREQUAL "")
        message(FATAL_ERROR
            "闭包检查拒绝执行：缺少 -D${_gis_arg}。没有前缀/配置/扫描器/插件名单就无从判断，"
            "这里不允许退化成'没什么可判 ⇒ 通过'。")
    endif()
endforeach()

if(NOT CONFIG STREQUAL "Debug" AND NOT CONFIG STREQUAL "Release")
    message(FATAL_ERROR "闭包检查：-DCONFIG 只能是 Debug 或 Release（收到 '${CONFIG}'）—— "
        "两配置下 vcpkg 的运行库名字不同（带 d / 不带 d），判错配置等于没判。")
endif()

# ── 扫描器 ────────────────────────────────────────────────────────────────────────
# WHY 缺失即 FATAL 而不是跳过：与 21-E 的"无扫描器 ⇒ 安装期拒绝"同一纪律。
#   闭包检查的全部价值就在于"没人导入的 dll 也没被漏装"，而这句话只有能读 PE 导入表才判得出；
#   扫描器不在的时候绿一次，等于把"N86 这类残缺 CI 仍绿"的口子重新打开。
file(TO_CMAKE_PATH "${SCANNER}" _gis_scanner)
if(NOT EXISTS "${_gis_scanner}")
    message(FATAL_ERROR
        "闭包检查无法执行：扫描器不存在 '${_gis_scanner}'。\n"
        "取径（不要在本脚本里重新探测 —— 那会把'包里该有哪些 DLL'交给当场 PATH，正是 N84 修掉的东西）：\n"
        "  · configure 日志里的 `Runtime dep scanner: <绝对路径>` 那一行；或\n"
        "  · 构树生成物 <build>/install_runtime_deps.cmake 里 set(_gis_scanner_command \"...\") 烘进去的同一条路径。")
endif()

# 输出格式由扫描器种类决定；调用方能显式告知，未告知时按可执行文件名判定。
set(_gis_tool "${SCANNER_TOOL}")
if(_gis_tool STREQUAL "")
    get_filename_component(_gis_scanner_we "${_gis_scanner}" NAME_WE)
    if(_gis_scanner_we MATCHES "^dumpbin")
        set(_gis_tool "dumpbin")
    elseif(_gis_scanner_we MATCHES "objdump$")
        set(_gis_tool "objdump")
    else()
        message(FATAL_ERROR "闭包检查：无法从 '${_gis_scanner}' 判定导入表输出格式，请补 -DSCANNER_TOOL=<dumpbin|objdump>")
    endif()
endif()
if(NOT _gis_tool MATCHES "^(dumpbin|objdump)$")
    message(FATAL_ERROR "闭包检查：-DSCANNER_TOOL 只支持 dumpbin / objdump（收到 '${_gis_tool}'）")
endif()

# ── 自带安装（ctest 用例走这条路）─────────────────────────────────────────────────
# 两侧都按"先 ABSOLUTE 再 TO_CMAKE_PATH"同一顺序归一，否则一个带 `..`、一个不带时
# STREQUAL 会比不过（比不过的后果是拒绝执行，不是误装到别处）。
file(TO_CMAKE_PATH "${PREFIX}" _gis_prefix)
get_filename_component(_gis_prefix "${_gis_prefix}" ABSOLUTE)
file(TO_CMAKE_PATH "${_gis_prefix}" _gis_prefix)

if(DEFINED BUILD_DIR AND NOT BUILD_DIR STREQUAL "")
    file(TO_CMAKE_PATH "${BUILD_DIR}" _gis_build_dir)
    get_filename_component(_gis_build_dir "${_gis_build_dir}" ABSOLUTE)
    file(TO_CMAKE_PATH "${_gis_build_dir}" _gis_build_dir)
    # 只允许写进本用例专属的临时前缀。WHY 要钉死：这个前缀每次都被整体删除重装，
    #   指错地方就等于拿交付目录当测试耗材；仓库根的 install/ 是 NSIS/zip 的取材地，绝不能进这里。
    get_filename_component(_gis_own_prefix "${_gis_build_dir}/../tmp/install_closure_${CONFIG}" ABSOLUTE)
    file(TO_CMAKE_PATH "${_gis_own_prefix}" _gis_own_prefix)
    if(NOT _gis_prefix STREQUAL _gis_own_prefix)
        message(FATAL_ERROR
            "闭包检查：带 -DBUILD_DIR 时 PREFIX 必须是本用例专属前缀 '${_gis_own_prefix}'（收到 '${_gis_prefix}'）。"
            "本模式会先删再装该目录，任何别的目录（尤其仓库根的 install/）都不允许被这么对待。")
    endif()
    if(NOT EXISTS "${_gis_build_dir}/install_runtime_deps.cmake")
        message(FATAL_ERROR
            "闭包检查：构树 '${_gis_build_dir}' 里没有生成 install_runtime_deps.cmake ⇒ 这棵树根本没配置过，"
            "先跑 cmake --preset <debug|release>。")
    endif()
    file(REMOVE_RECURSE "${_gis_prefix}")
    # 清理结果必须判（Task B6 / Step 4c②）：`file(REMOVE_RECURSE)` 对被占用/只读/无权限的文件是
    #   **部分失败也不报错**的，而本用例的判据是"包里缺东西才红"。上一轮红过之后如果没真删干净，
    #   残留文件会替本轮"顶罪"（本轮该缺的那个 dll 上一次装着呢 ⇒ 存在性断言全过、闭包全过 ⇒ 绿），
    #   这是"红过之后下一轮误导性变绿"的唯一通道，所以这里拒绝带着残留继续判，而不是往下走。
    if(EXISTS "${_gis_prefix}")
        message(FATAL_ERROR
            "闭包检查：清理本用例专属前缀失败，'${_gis_prefix}' 仍然存在（多半是里面的文件正被别的进程占用）。"
            "⇒ 判红而不是接着判：残留的上一轮产物会让这一轮的'包内该有的东西都在'变成假绿，"
            "上一轮的真失败也会被顶罪成这一轮的通过。请先关掉占用该目录的进程（编辑器/杀毒/正在跑的 gis-cli）再重跑。")
    endif()
    message(STATUS "[install-closure] 本用例自行执行 cmake --install --config ${CONFIG} --prefix ${_gis_prefix}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --install "${_gis_build_dir}" --config "${CONFIG}" --prefix "${_gis_prefix}"
        OUTPUT_VARIABLE _gis_install_out
        ERROR_VARIABLE _gis_install_err
        RESULT_VARIABLE _gis_install_rc
    )
    if(NOT _gis_install_rc EQUAL 0)
        message(FATAL_ERROR
            "闭包检查：本用例自带的 cmake --install 失败（rc=${_gis_install_rc}）：\n${_gis_install_err}\n"
            "⇒ 先修安装步再谈闭包（N84 之后没有扫描器就是在这里拒绝安装的）。")
    endif()
endif()

if(NOT IS_DIRECTORY "${_gis_prefix}/bin")
    message(FATAL_ERROR
        "闭包检查：${_gis_prefix}/bin 不存在 ⇒ 安装步根本没产出。"
        "带 -DBUILD_DIR 时本用例会自己装；不带时请先跑安装步。"
        "'目录不在'永远不等于'闭包通过'。")
endif()

# ── ① 存在性硬断言 ────────────────────────────────────────────────────────────────
# 清单按**包的实际布局**列，每条对应一条 install() 规则（不是任何人的记忆）：
#   CMakeLists.txt 的 install(DIRECTORY proj/gdal/icons/leaflet)、install(TARGETS gis-cli/gis-gui)、
#   src/gui/CMakeLists.txt 的 platforms/sqldrivers install(FILES)、
#   cmake/install_runtime_deps.cmake.in 的插件清单 + 末尾把 bin/*.dll 复制进 bin/plugins 的那段。
# ⚠ PROJ 数据在 **share/proj/proj.db**（CMAKE_INSTALL_DATADIR），不是 bin/proj.db。
set(_gis_must_exist
    "bin/gis-cli.exe|交付的 CLI 本体（install(TARGETS gis-cli)）"
    "bin/gis-gui.exe|交付的 GUI 本体（install(TARGETS gis-gui)；两 preset 都 GIS_BUILD_GUI=ON，缺它就是残包）"
    "share/proj/proj.db|PROJ 投影数据库：缺 ⇒ 投影/坐标变换静默错，且没有任何导入表能反映它"
    )

# 自家插件 dll：运行时由插件加载器按名字 load，**不出现在任何导入表里**（N86 就是这一类）。
# 逐个点名而不是只判"目录非空"：只判非空的话，删到目录里还剩一个 dll 就仍然是绿的。
# 名单来源（Task B6 / G-B5-1，改掉了原先"照着 cmake/install_runtime_deps.cmake.in 手抄一遍"的形态）：
#   -DPLUGIN_DLLS 由调用方从根 CMakeLists.txt 的派生结果传入，而那份派生的唯一真源是
#   cmake/PluginTargets.cmake 的 GIS_PLUGIN_TARGETS —— 与安装期扫描根同源，两侧不再各自记一遍。
#   本脚本因此**不判**"名单是否等于安装脚本里的扫描根"：那是一条两边同源的恒等式（伪断言，
#   同源时永远绿、不同源时才红，而不同源已经在配置期被派生处拦住），判了也不会多抓住什么。
#   这里只判进来的是不是一份**可用的**插件名单（非空、每条形如 plugin_*.dll、无重名）：
#   传空 ⇒ 存在性断言直接少一整类（正是 N86 的失败方向），必须红而不是"没什么可判"。
set(_gis_plugin_dlls "${PLUGIN_DLLS}")
list(LENGTH _gis_plugin_dlls _gis_plugin_dll_count)
if(_gis_plugin_dll_count LESS 1)
    message(FATAL_ERROR
        "闭包检查：-DPLUGIN_DLLS 是空的 ⇒ 一条插件 dll 都不会被断言存在。"
        "名单取根 CMakeLists.txt 配置期从 GIS_PLUGIN_TARGETS 派生的 GIS_PLUGIN_DLL_NAMES："
        "  · ctest 用例走 tests/CMakeLists.txt 里 add_test 的那条 -DPLUGIN_DLLS=；"
        "  · CI 走 <build>/install_runtime_deps.cmake 里烘好的 set(_gis_plugin_dll_names \"…\") 那一行。")
endif()
set(_gis_plugin_dll_unique "${_gis_plugin_dlls}")
list(REMOVE_DUPLICATES _gis_plugin_dll_unique)
list(LENGTH _gis_plugin_dll_unique _gis_plugin_dll_unique_count)
if(NOT _gis_plugin_dll_unique_count EQUAL _gis_plugin_dll_count)
    message(FATAL_ERROR
        "闭包检查：-DPLUGIN_DLLS 里有重名（${_gis_plugin_dll_count} 条去重后只剩 ${_gis_plugin_dll_unique_count} 条）"
        "⇒ 传进来的名单被拼接坏了（参数被 shell 切过 / 派生处被写了两次），判红而不是照着半份名单判。")
endif()
foreach(_gis_plugin IN LISTS _gis_plugin_dlls)
    if(NOT _gis_plugin MATCHES "^plugin_[A-Za-z0-9_]+\\.dll$")
        message(FATAL_ERROR
            "闭包检查：-DPLUGIN_DLLS 里的 '${_gis_plugin}' 不像插件 dll 名（应为 plugin_<名字>.dll）"
            "⇒ 参数在传递路上被切坏或来源错了（名单以分号分隔传进来，被 shell 拆成多个 -D 时"
            "只会拿到第一截），拒绝带着残缺名单判。")
    endif()
endforeach()
# 名单必须覆盖包内**实际存在**的插件 dll。WHY 这不是伪断言：比较的另一侧不是"另一份同名清单"，
#   而是刚装出来的包（对 bin/plugins 的实测 glob），与参数的来源完全无关。
#   它能抓住两类真事故：① 参数在传递路上被切坏（CMake 的 add_test 会把含分号的变量拆成多个参数，
#   脚本只收到第一条 ⇒ 其余插件装了却没人断言，正是 N86 的失败方向）；
#   ② 包里出现一个没进名单的插件 dll（名单侧漏项，同样漏判）。
#   另一侧的方向（名单要求的东西包里必须有）由下面的存在性断言判；两条方向合起来才把
#   "名单 ↔ 包内容"这条边钉住，单靠任何一边都会漏掉另一类残缺。
file(GLOB _gis_plugins_in_package "${_gis_prefix}/bin/plugins/plugin_*.dll")
set(_gis_unasserted_plugins "")
foreach(_gis_installed IN LISTS _gis_plugins_in_package)
    get_filename_component(_gis_installed_name "${_gis_installed}" NAME)
    if(NOT _gis_installed_name IN_LIST _gis_plugin_dlls)
        list(APPEND _gis_unasserted_plugins "${_gis_installed_name}")
    endif()
endforeach()
list(LENGTH _gis_unasserted_plugins _gis_n_unasserted)
if(_gis_n_unasserted GREATER 0)
    list(JOIN _gis_unasserted_plugins " " _gis_unasserted_text)
    message(FATAL_ERROR
        "闭包检查：包里有 ${_gis_n_unasserted} 个 plugin_*.dll 不在 -DPLUGIN_DLLS 名单内（${_gis_unasserted_text}）"
        "⇒ 它们不被任何存在性断言覆盖，删掉也不会变红。取径见本文件用法段的 -DPLUGIN_DLLS 说明。")
endif()

foreach(_gis_plugin IN LISTS _gis_plugin_dlls)
    list(APPEND _gis_must_exist "bin/plugins/${_gis_plugin}|插件动作的载体：运行时按名字加载，导入表里查不到（N86 类）")
endforeach()

# 按前缀 glob 的类：Debug 带 d、Release 不带（qwindowsd.dll / qwindows.dll），
# WHY 用 glob 而不是写死名字：写死哪一侧都会在另一侧变成假红或假绿。
set(_gis_glob_classes
    "bin/platforms|qwindows*.dll|Qt windows 平台插件：缺 ⇒ 包里的 GUI 起不来（N85 回归）"
    "bin/platforms|qoffscreen*.dll|Qt offscreen 平台插件：CI 的 Verify 就是用 -platform offscreen 跑的"
    "bin/sqldrivers|qsqlite*.dll|Qt SQLite 驱动：缺 ⇒ QSqlDatabase 的 sqlite 连接静默不可用；复审实测这种残缺 CI 仍绿（N86）"
    )

# 非空目录类：install(DIRECTORY) 整体拷过去的东西，缺一个文件不影响目录存在，所以判"目录里得有东西"。
set(_gis_nonempty_dirs
    "share/gdal|GDAL 数据（投影定义/CSV 字典）：缺 ⇒ 部分格式与坐标操作静默降级"
    "share/icons|GUI 图标资源：缺 ⇒ 界面图标全丢"
    "share/leaflet|离线底图资源：缺 ⇒ 地图视图无底图"
    )

set(_gis_missing "")
foreach(_gis_item IN LISTS _gis_must_exist)
    string(REPLACE "|" ";" _gis_parts "${_gis_item}")
    list(GET _gis_parts 0 _gis_rel)
    list(GET _gis_parts 1 _gis_why)
    if(NOT EXISTS "${_gis_prefix}/${_gis_rel}")
        list(APPEND _gis_missing "${_gis_rel}（${_gis_why}）")
    endif()
endforeach()

foreach(_gis_item IN LISTS _gis_glob_classes)
    string(REPLACE "|" ";" _gis_parts "${_gis_item}")
    list(GET _gis_parts 0 _gis_dir)
    list(GET _gis_parts 1 _gis_pat)
    list(GET _gis_parts 2 _gis_why)
    file(GLOB _gis_hits "${_gis_prefix}/${_gis_dir}/${_gis_pat}")
    list(LENGTH _gis_hits _gis_n)
    if(_gis_n LESS 1)
        list(APPEND _gis_missing "${_gis_dir}/${_gis_pat}（找到 ${_gis_n} 个，${_gis_why}）")
    endif()
endforeach()

foreach(_gis_item IN LISTS _gis_nonempty_dirs)
    string(REPLACE "|" ";" _gis_parts "${_gis_item}")
    list(GET _gis_parts 0 _gis_rel)
    list(GET _gis_parts 1 _gis_why)
    file(GLOB_RECURSE _gis_children LIST_DIRECTORIES false "${_gis_prefix}/${_gis_rel}/*")
    list(LENGTH _gis_children _gis_n)
    if(NOT IS_DIRECTORY "${_gis_prefix}/${_gis_rel}" OR _gis_n LESS 1)
        list(APPEND _gis_missing "${_gis_rel}（目录内 ${_gis_n} 个文件，${_gis_why}）")
    endif()
endforeach()

list(LENGTH _gis_missing _gis_n_missing_exist)
if(_gis_n_missing_exist GREATER 0)
    list(JOIN _gis_missing "\n  " _gis_missing_text)
    message(FATAL_ERROR
        "闭包检查：包内关键资源残缺（共 ${_gis_n_missing_exist} 项，PREFIX=${_gis_prefix}）：\n  ${_gis_missing_text}\n"
        "WHY 这一判必须存在：这些资源是运行时按名字加载的边车/插件，不在任何 PE 的导入表里，"
        "'装出来的包能跑' 对它们是否在场一无所知（N86）。")
endif()

# ── ② 导入闭包 ────────────────────────────────────────────────────────────────────
# 系统 DLL 白名单：**逐条来自实测**，不是照抄任何人的清单（推导过程、复算命令与逐条 System32
#   核对结果都登记在 .superpowers/sdd/…/task-B5-report.md，那里的数字才随证据一起维护）。
#   取径一句话：对现装的 Debug 与 Release 两份包，取 bin/ 下每个 PE 的直接导入名，
#   减去"包内已有同名文件"的，剩下的未满足集逐个核 C:/Windows/System32 是否有实体文件；
#   核得上实体文件的才进名单，其余只有 api-ms-win-* 一族（由加载器解析，磁盘上本就没有）。
# WHY 用显式名单而不是"System32 里有就算合法"：后者会把"恰好装在系统目录里的第三方 dll"
#   也判合法（换台机器就漏），名单制才保证**新增**一个未打包的导入名时立刻变红。
# 每条一行理由；名单一律小写，因为比对前导入名已 string(TOLOWER)（dumpbin 打的是 KERNEL32.dll，
#   vcpkg 打的是 advapi32.dll，不统一大小写就会有名字靠运气命中）。
set(_gis_system_allowlist
    "^api-ms-win-.*\\.dll$"     # ApiSet 契约名：由加载器的 ApiSet schema 解析，System32 里本就没有实体文件（install_runtime_deps 也 PRE_EXCLUDE 同族）
    "^ext-ms-win-.*\\.dll$"     # 同上族的 downlevel 契约
    "^ntdll\\.dll$"             # NT 内核用户层入口，OS 自带
    "^kernel32\\.dll$"          # OS 核心：内存/句柄/模块加载
    "^user32\\.dll$"            # OS 窗口与消息（Qt Gui/Widgets）
    "^userenv\\.dll$"           # 用户环境目录（Qt 解析 APPDATA/LOCALAPPDATA 的下游；Qt6Core 的直接导入）
    "^gdi32\\.dll$"             # OS 绘图（Qt Gui）
    "^shell32\\.dll$"           # OS 外壳（Qt / GDAL 路径处理）
    "^ole32\\.dll$"             # COM 运行时（Qt / arrow）
    "^oleaut32\\.dll$"          # COM 变体类型（部分驱动的 COM 入口）
    "^comdlg32\\.dll$"          # 通用对话框（Qt Widgets）
    "^advapi32\\.dll$"          # 注册表/服务/安全（GDAL、Qt）
    "^shlwapi\\.dll$"           # 轻量外壳工具函数（路径/注册表）
    "^version\\.dll$"           # GetFileVersionInfo（Qt 插件版本探测）
    "^winmm\\.dll$"             # 高精度计时器（Qt）
    "^imm32\\.dll$"             # 输入法（Qt 平台层）
    "^uxtheme\\.dll$"             # 视觉主题绘制（Qt widgets）
    "^dwmapi\\.dll$"            # 桌面窗口管理器合成（Qt）
    "^dwrite\\.dll$"            # DirectWrite 文本排版（Qt Gui）
    "^uiautomationcore\\.dll$"  # UI 自动化/无障碍（Qt accessibility）
    "^d3d9\\.dll$"              # Direct3D 9（OS 自带图形栈）
    "^d3d11\\.dll$"             # Direct3D 11（Qt RHI / ANGLE 侧）
    "^d3d12\\.dll$"             # Direct3D 12（Qt6 图形探测）
    "^dxgi\\.dll$"              # DXGI 交换链（同族 OS 图形组件）
    "^ws2_32\\.dll$"            # Winsock（GDAL 网络格式 / curl）
    "^iphlpapi\\.dll$"          # 网络接口辅助信息（arrow/curl）
    "^secur32\\.dll$"           # SSPI 认证（curl/GDAL 网络鉴权）
    "^crypt32\\.dll$"           # 证书与编解码（curl/openssl 下游）
    "^bcrypt\\.dll$"            # CNG 原语（curl/openssl 下游）
    "^netapi32\\.dll$"          # 网络管理 API（GDAL SMB/proxy 侧）
    "^mpr\\.dll$"               # 多重提供者/网络连接枚举（ODBC 路径）
    "^authz\\.dll$"             # 授权框架（ODBC/database 驱动的 OS 依赖）
    "^setupapi\\.dll$"          # 设备信息（驱动枚举，ODBC/串口类）
    "^wtsapi32\\.dll$"          # 终端会话 API（Qt 会话感知）
    "^dbghelp\\.dll$"           # 符号与转储（Release 侧崩溃栈；OS 组件）
    "^xmllite\\.dll$"           # 轻量 XML 读取器（libxml2 的 OS 下游）
    # ── VC++ 运行库族：**有意不打包**，与 cmake/install_runtime_deps.cmake.in 的
    #    POST_EXCLUDE 决定同源（终审第 1 席 I-8）：它属于系统级可再分发包，随包分发有授权与
    #    版本冲突问题 ⇒ 交付要求目标机自装 VC++ Redistributable（docs/RELEASE.md，挂账 N87）。
    #    带 d 的那批是 VS 自带的**调试**运行库，Redistributable 里根本没有：Debug 只在本机/CI
    #    的开发环境里跑，交付形态是 Release，所以这一族在 Debug 下同样只能白名单。
    "^msvcp140.*\\.dll$"        # MSVC C++ 标准库（含 _1/_2/_atomic_wait 与 Debug 的 d 变体）
    "^vcruntime140.*\\.dll$"    # MSVC C 运行时异常/杂项支持（含 _1 与 d 变体）
    "^concrt140.*\\.dll$"       # PPL 并发运行时（OpenCV/GDAL 并行分支引用）
    "^vccorlib140.*\\.dll$"     # C++/CX 支持（当前包未导入，同族留一并白，避免换 VS 版本即假红）
    "^ucrtbase.*\\.dll$"        # UCRT（含 Debug 的 ucrtbased.dll；Windows 10+ 由 OS 自带）
    )

file(GLOB_RECURSE _gis_pe_files LIST_DIRECTORIES false
    "${_gis_prefix}/bin/*.dll" "${_gis_prefix}/bin/*.exe")
list(LENGTH _gis_pe_files _gis_n_roots)
if(_gis_n_roots LESS 1)
    message(FATAL_ERROR "闭包检查：${_gis_prefix}/bin 下没有任何 dll/exe 可扫 ⇒ 这个包是空的，判红而不是判绿。")
endif()

# 可满足集：bin/ 下所有文件的"目录/文件名"（全小写）。
# WHY 判"同目录或 bin/ 里有没有"而不是把 bin/ 整个摊平：Windows 的默认搜索序是
#   被加载模块自己的目录 → 应用目录(=bin) → System32 → PATH。摊平会把只存在于
#   bin/plugins 副本目录里的名字算作"满足了 bin/gis-cli.exe 的导入"，而那在台面上根本不参与解析
#   ⇒ 删掉 bin/gdald.dll 时闭包检查会假绿（注入实验专门验证这一点）。
set(_gis_path_set "")
foreach(_gis_f IN LISTS _gis_pe_files)
    get_filename_component(_gis_d "${_gis_f}" DIRECTORY)
    get_filename_component(_gis_n "${_gis_f}" NAME)
    string(TOLOWER "${_gis_d}/${_gis_n}" _gis_key)
    list(APPEND _gis_path_set "${_gis_key}")
endforeach()
file(TO_CMAKE_PATH "${_gis_prefix}/bin" _gis_app_dir)
string(TOLOWER "${_gis_app_dir}" _gis_app_dir)

set(_gis_unresolved "")
set(_gis_edges 0)
# 关键根的解析条数（见下面的"格式漂移"断言）。
set(_gis_anchor_edges 0)
foreach(_gis_pe IN LISTS _gis_pe_files)
    if(_gis_tool STREQUAL "dumpbin")
        # 注意是单斜杠 `/dependents`：execute_process 不经 shell，没有 MSYS 那条
        #   "把 /xxx 当路径转换" 的规则，写成 //dependents 会被 dumpbin 当成未知选项整条忽略，
        #   输出还在、rc 还是 0，但一条依赖都不会出现 ⇒ 判据静默失效。
        #   （本机 Git Bash 里手工跑必须写 //dependents，那是 shell 的转换规则，两者不冲突。）
        set(_gis_scan_args "/dependents" "/NOLOGO")
    else()
        # objdump 族（根 CMakeLists.txt 选不到 VS 的 dumpbin 时会退到它，见那段扫描器解析次序）。
        # 诚实登记：这一支**没有实测覆盖** —— 本仓的两棵构树与 CI 都解析到 dumpbin，objdump 只在
        #   "那台机器没有 VS 工具链"的配置期分支上才会被选中，而那种现场不在本仓的可跑范围内。
        #   这里取的是 `-p` 私有头里 `DLL Name:` 这条对外部实现的公开格式约定，属于**照文档写的假设**。
        #   正因为没有实测覆盖，下面的 _gis_anchor_edges 断言必须留着：猜错格式的后果是
        #   "0 条导入 ⇒ 0 条违例"的假绿，而那正是 N86 的失败形状，不能靠"当前取径走的是 dumpbin"过去。
        set(_gis_scan_args "-p")
    endif()
    execute_process(
        COMMAND "${_gis_scanner}" ${_gis_scan_args} "${_gis_pe}"
        OUTPUT_VARIABLE _gis_out
        ERROR_QUIET
        RESULT_VARIABLE _gis_rc
    )
    if(NOT _gis_rc EQUAL 0)
        message(FATAL_ERROR
            "闭包检查：扫描器读不动 '${_gis_pe}'（${_gis_tool} rc=${_gis_rc}）⇒ 无法判断它的导入是否齐备，"
            "这里不退化成'当它没导入'。")
    endif()
    string(REGEX REPLACE "\r" "" _gis_out "${_gis_out}")
    string(REGEX REPLACE "\n" ";" _gis_lines "${_gis_out}")

    get_filename_component(_gis_pe_dir "${_gis_pe}" DIRECTORY)
    string(TOLOWER "${_gis_pe_dir}" _gis_pe_dir)
    string(TOLOWER "${_gis_pe}" _gis_pe_lower)
    set(_gis_pe_is_anchor FALSE)
    if(_gis_pe_lower MATCHES "/gis-cli\\.exe$")
        set(_gis_pe_is_anchor TRUE)
    endif()

    set(_gis_in_list FALSE)
    foreach(_gis_line IN LISTS _gis_lines)
        string(STRIP "${_gis_line}" _gis_tok)
        if(_gis_tool STREQUAL "dumpbin")
            # 依赖块 = 表头行之后的**一行一个名字**，到第一个"非空且不是裸模块名"的行为止。
            # WHY 不能拿空行当结束标记：dumpbin 在表头与名字之间就夹了一个空行，
            #   用它收尾会一条也取不到 ⇒ 0 条违例的假绿（本脚本对这种现场另有"不许归零"断言兜着）。
            # WHY 块尾判定必须认"带扩展名的模块名"而不是只认 .dll：PE 的导入表里模块名的扩展名
            #   是任意的（WINSPOOL.DRV、*.OCX 这类 OS 模块是真的存在这一族的），只认 .dll 会把它们
            #   当成块尾 ⇒ **该行之后的全部导入被静默丢掉**，而"丢掉之后 0 条违例"是报绿的
            #   （Task B6 Step 4c①；判据形状见 _gis_tok 的 MATCHES）。
            # ⚠ 这一族的写法**不能**直接抄成 `^[^ ]+\.[A-Za-z][A-Za-z0-9]{1,4}$`：CMake 的正则引擎
            #   不认 `{m,n}` 区间量词（实测：带 `{1,4}` 的那一条整体匹配不中任何东西）
            #   ⇒ 每条依赖都成了块尾 ⇒ 0 条边（这种现场被下面的"不许归零"断言接住、判红，不是假绿）。
            #   这里用等价写法：首字母 + 1 个必选 + 3 个可选 = 扩展名 2..5 个字符，与 `{1,4}` 同集合
            #   （逐 token 对过 19 个形状，见任务报告的对照表）。
            # WHY "裸模块名"要带空格检查：`Dump of file …\archive.dll` 这类行也以 .dll 结尾，
            #   但它含空格，`^[^ ]+` 那一段就能把它挡在块外。
            if(NOT _gis_in_list)
                if(_gis_tok MATCHES "Image has the following dependencies")
                    set(_gis_in_list TRUE)
                endif()
                continue()
            endif()
            if(_gis_tok STREQUAL "")
                continue()
            endif()
            if(NOT _gis_tok MATCHES "^[^ ]+\\.[A-Za-z][A-Za-z0-9][A-Za-z0-9]?[A-Za-z0-9]?[A-Za-z0-9]?$")
                set(_gis_in_list FALSE)
                continue()
            endif()
        else()
            if(NOT _gis_tok MATCHES "^DLL Name:")
                continue()
            endif()
            string(REGEX REPLACE "^DLL Name:[ \t]*" "" _gis_tok "${_gis_tok}")
            # objdump 族没有"块结束"这个概念，所以取到的每条名字要自己再过一遍与 dumpbin 同一道格式闸
            #   （同一道闸 = 同一个模块名形状，不另写一份只认 .dll 的，否则 dumpbin 那侧收进来的
            #    `WINSPOOL.DRV` 会在这里被判成"格式不符"而假红）：
            #   过不了就说明解析假设和真实输出不符（这一支没有实测覆盖，见上面 _gis_scan_args 的登记），
            #   那种现场必须判红，而不是把这条丢掉 —— 丢掉就是"0 条违例"的假绿。
            if(NOT _gis_tok MATCHES "^[^ ]+\\.[A-Za-z][A-Za-z0-9][A-Za-z0-9]?[A-Za-z0-9]?[A-Za-z0-9]?$")
                message(FATAL_ERROR
                    "闭包检查：'${_gis_tool}' 在 '${_gis_pe}' 里给出的导入名不像 PE 模块名（'${_gis_tok}'）"
                    "⇒ 输出格式与本脚本的解析规则不符，判红而不是丢弃。")
            endif()
        endif()
        string(TOLOWER "${_gis_tok}" _gis_dep)
        math(EXPR _gis_edges "${_gis_edges} + 1")
        if(_gis_pe_is_anchor)
            math(EXPR _gis_anchor_edges "${_gis_anchor_edges} + 1")
        endif()
        if("${_gis_pe_dir}/${_gis_dep}" IN_LIST _gis_path_set)
            continue()
        endif()
        if("${_gis_app_dir}/${_gis_dep}" IN_LIST _gis_path_set)
            continue()
        endif()
        set(_gis_allowed FALSE)
        foreach(_gis_rx IN LISTS _gis_system_allowlist)
            if(_gis_dep MATCHES "${_gis_rx}")
                set(_gis_allowed TRUE)
                break()
            endif()
        endforeach()
        if(_gis_allowed)
            continue()
        endif()
        list(APPEND _gis_unresolved "${_gis_pe}|${_gis_dep}")
    endforeach()
endforeach()

# 解析器自带的"不许归零"断言：对包内每一个 PE 都取了导入却一条都没解析到，只能是解析坏了或扫描器换了格式，
# 这种情况**必须红**，否则整条闭包判据会退化成人畜无害的"0 条违例"。
if(_gis_edges LESS 1)
    message(FATAL_ERROR
        "闭包检查：对 ${_gis_n_roots} 个 PE 取导入，一共解析到 0 条依赖边 ⇒ 解析器与 '${_gis_tool}' 的输出格式对不上。"
        "0 条边在这里不是'闭包干净'，是'判据失效'，按红处理。")
endif()
# 只判总边数还不够：格式变了但**只解析出一部分**时同样是假绿，所以要钉一个已知必有大导入表的根。
# WHY 锚点选 gis-cli.exe：它是包内一定存在的根（上面的存在性断言已保证），且必然直接导入 GDAL/OpenCV，
#   取到 0 条只能是"扫描器输出与解析规则不符"，不可能是"这个包真的干净"。
#   这一条同时也是 objdump 那一支的兜底：那一支的格式假设没有实测覆盖（见上面 _gis_scan_args 的登记）。
if(_gis_anchor_edges LESS 1)
    message(FATAL_ERROR
        "闭包检查：'${_gis_prefix}/bin/gis-cli.exe' 解析到 0 条直接导入，但它必然导入 GDAL/OpenCV 一族 ⇒"
        " '${_gis_tool}' 的输出格式与本脚本的解析规则不符（参数或标记行变了），判红而不是'没有违例'。")
endif()

list(REMOVE_DUPLICATES _gis_unresolved)
list(LENGTH _gis_unresolved _gis_n_unresolved)
message(STATUS "[install-closure] PREFIX=${_gis_prefix} 扫描 PE 数=${_gis_n_roots} 导入边数=${_gis_edges} 未满足名字数=${_gis_n_unresolved} 存在性断言=全通过")

if(_gis_n_unresolved GREATER 0)
    set(_gis_gap_text "")
    foreach(_gis_pair IN LISTS _gis_unresolved)
        string(REPLACE "|" " ← " _gis_pair_text "${_gis_pair}")
        string(APPEND _gis_gap_text "  ${_gis_pair_text}\n")
    endforeach()
    message(FATAL_ERROR
        "闭包检查：包内 PE 有 ${_gis_n_unresolved} 条直接导入既不在包里也不在系统白名单里（PREFIX=${_gis_prefix}）：\n${_gis_gap_text}"
        "WHY 红：这些是链接期硬依赖，缺一个就是 0xC0000135（STATUS_DLL_NOT_FOUND）起不来，"
        "而 `cmake --install` 自己 rc=0 不会报。\n"
        "补救二选一：①它本该被打包 ⇒ 查 cmake/install_runtime_deps.cmake.in 的扫描根/DIRECTORIES；"
        "②它是 OS 组件 ⇒ 加进本脚本的 _gis_system_allowlist，并写清一行理由。")
endif()

message(STATUS "[install-closure] 闭包成立：存在性断言全在位，${_gis_edges} 条导入边全部可满足（包内或系统白名单）")
