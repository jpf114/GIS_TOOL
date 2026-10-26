cmake_minimum_required(VERSION 3.21)

# vcpkg 全局模式：使用 VCPKG_ROOT 指向的全局 vcpkg 安装，vcpkg.json 仅作依赖清单
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    endif()
endif()

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE OR NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg\\.cmake$")
    message(FATAL_ERROR
        "未检测到 vcpkg toolchain。\n"
        "请确保已设置 VCPKG_ROOT 环境变量指向 vcpkg 安装目录，\n"
        "或通过 -DCMAKE_TOOLCHAIN_FILE=<path>/vcpkg.cmake 显式指定。\n"
    )
endif()

set(VCPKG_MANIFEST_MODE OFF CACHE BOOL "Use global vcpkg installation instead of manifest mode" FORCE)

project(gis-tool VERSION 1.0.0 LANGUAGES CXX)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Default install path" FORCE)
endif()

include(GNUInstallDirs)

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

option(GIS_BUILD_GUI "Build Qt GUI" OFF)
option(GIS_BUILD_TESTS "Build tests" ON)

if(NOT DEFINED VCPKG_INSTALLED_DIR OR "${VCPKG_INSTALLED_DIR}" STREQUAL "")
    message(FATAL_ERROR "未检测到 VCPKG_INSTALLED_DIR。请通过 vcpkg toolchain 配置工程。")
endif()

message(STATUS "vcpkg installed dir: ${VCPKG_INSTALLED_DIR}")
message(STATUS "vcpkg target triplet: ${VCPKG_TARGET_TRIPLET}")

set(GIS_VCPKG_TRIPLET "${VCPKG_TARGET_TRIPLET}")
if(GIS_VCPKG_TRIPLET STREQUAL "")
    message(FATAL_ERROR "未检测到 VCPKG_TARGET_TRIPLET。请通过 vcpkg toolchain 配置工程。")
endif()

set(GIS_VCPKG_INSTALLED_ROOT "${VCPKG_INSTALLED_DIR}")
set(GIS_VCPKG_INSTALLED_DIR "${GIS_VCPKG_INSTALLED_ROOT}")
if(EXISTS "${GIS_VCPKG_INSTALLED_ROOT}/${GIS_VCPKG_TRIPLET}")
    set(GIS_VCPKG_INSTALLED_DIR "${GIS_VCPKG_INSTALLED_ROOT}/${GIS_VCPKG_TRIPLET}")
endif()

if(NOT EXISTS "${GIS_VCPKG_INSTALLED_DIR}")
    message(FATAL_ERROR "未找到 vcpkg 安装目录: ${GIS_VCPKG_INSTALLED_DIR}")
endif()

set(GIS_VCPKG_RELEASE_BIN_DIR "${GIS_VCPKG_INSTALLED_DIR}/bin")
set(GIS_VCPKG_DEBUG_BIN_DIR "${GIS_VCPKG_INSTALLED_DIR}/debug/bin")
set(GIS_VCPKG_SHARE_DIR "${GIS_VCPKG_INSTALLED_DIR}/share")
set(GIS_VCPKG_PROJ_DATA_DIR "${GIS_VCPKG_SHARE_DIR}/proj")
set(GIS_VCPKG_GDAL_DATA_DIR "${GIS_VCPKG_SHARE_DIR}/gdal")
set(GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR "${GIS_VCPKG_INSTALLED_DIR}/Qt6/plugins/platforms")
set(GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR "${GIS_VCPKG_INSTALLED_DIR}/debug/Qt6/plugins/platforms")
set(GIS_VCPKG_RELEASE_QT_DIR "${GIS_VCPKG_INSTALLED_DIR}/Qt6")
set(GIS_VCPKG_DEBUG_QT_DIR "${GIS_VCPKG_INSTALLED_DIR}/debug/Qt6")
set(GIS_INSTALL_PLUGIN_DIR "${CMAKE_INSTALL_BINDIR}/plugins")
set(GIS_INSTALL_PLATFORM_DIR "${CMAKE_INSTALL_BINDIR}/platforms")
set(GIS_INSTALL_SQLDRIVERS_DIR "${CMAKE_INSTALL_BINDIR}/sqldrivers")

file(TO_CMAKE_PATH "${GIS_VCPKG_INSTALLED_DIR}" GIS_VCPKG_INSTALLED_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_RELEASE_BIN_DIR}" GIS_VCPKG_RELEASE_BIN_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_DEBUG_BIN_DIR}" GIS_VCPKG_DEBUG_BIN_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_SHARE_DIR}" GIS_VCPKG_SHARE_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_PROJ_DATA_DIR}" GIS_VCPKG_PROJ_DATA_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_GDAL_DATA_DIR}" GIS_VCPKG_GDAL_DATA_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}" GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}" GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_RELEASE_QT_DIR}" GIS_VCPKG_RELEASE_QT_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_DEBUG_QT_DIR}" GIS_VCPKG_DEBUG_QT_DIR)

set(GIS_VCPKG_RUNTIME_BIN_DIR "$<$<CONFIG:Debug>:${GIS_VCPKG_DEBUG_BIN_DIR}>$<$<NOT:$<CONFIG:Debug>>:${GIS_VCPKG_RELEASE_BIN_DIR}>")
set(GIS_VCPKG_QT_PLATFORMS_DIR "$<$<CONFIG:Debug>:${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}>$<$<NOT:$<CONFIG:Debug>>:${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}>")

# ── 运行库扫描器：配置期解析成绝对路径，再由 configure_file 烘进安装脚本 ──────────────
# WHY（N84）：这段探测原先写在 install_runtime_deps.cmake.in 里，靠 `find_program(dumpbin)`
#   在 `cmake --install` 当场找 ⇒ "包里装的是哪些 DLL"取决于**打包者那一刻的 PATH**，
#   而不是取决于仓库配置。本机（Git Bash 与普通 powershell.exe 5.1；pwsh 7 本机未装）PATH 里没有 MSVC
#   工具链 ⇒ 找不到 ⇒
#   旧脚本静默回落到写死 release 的 36 项清单 ⇒ `cmake --install` 自己仍 rc=0，
#   而 `gis-cli.exe --list` 实测 rc=-1073741515（STATUS_DLL_NOT_FOUND）。
#   那份旧清单到底漏了多少 —— 两个口径各自的集合与算式都写出来，它们互相推不出来：
#     口径 A「缺 35」：对旧 36 项逐个 `dumpbin //dependents` 取**直接**导入得 100 个 DLL 名，
#       ∩ vcpkg x64-windows/bin 的 147 个 ⇒ 58；58 − 其中已在清单内的 23 ⇒ **漏 35 个**
#       （Lerc / zstd / archive / libxml2 / libcurl 就在 gdal.dll 自己的导入表里，gdal.dll 因此加载不了）。
#     口径 B「漏 47 / 多塞 5」：与扫描分支实测闭包比 —— 闭包 78 ∩ 清单 36 = 31，
#       故 78 − 31 = **47 个被真实导入的 DLL 清单里没有**，36 − 31 = **5 个清单里有但没人导入**
#       （Qt6Xml / md4c-html / pcre2-32 / pcre2-posix / turbojpeg）。
#   注意 78 − 36 = 42 不是任何口径的结果（两个集合的差集不等于"缺"），别拿它当"缺几个"引用。
#   同一个包换个环境装出来就能跑，这种不可复现的打包必须消灭：解析挪到配置期，
#   绝对路径烘进脚本，配置日志里一眼可见（红了能直接定位是"没找到扫描器"而不是"包坏了"）。
set(GIS_RUNTIME_DEP_SCANNER "")
set(GIS_RUNTIME_DEP_SCANNER_TOOL "")
# 这两个是**普通变量**（不是 cache 变量），上面的无条件 `set(... "")` 意味着
#   `-DGIS_RUNTIME_DEP_SCANNER=<路径>` 会被静默忽略、且不报错 —— 这是故意的：
#   缓存一个绝对路径会在 VS 升级/换盘后变成陈旧值，让"重新 configure 一次就好了"失效。
#   扫描器只由下面的配置期解析决定；确实要手工指定，只能改构树里的生成物
#   `${CMAKE_BINARY_DIR}/install_runtime_deps.cmake`（改完它就在树里，重配会覆盖）。
# 下面的 `file(GLOB)` 不需要 CONFIGURE_DEPENDS：它跑在**配置期**，每次 `cmake --preset` 都重算
#   （CI 还额外带 `--fresh`）；新装一个工具集却不重配的话，这份生成脚本本身也是旧的，
#   所以"看不见新工具集"并不引入额外失效面。真出现路径过期 ⇒ 安装期红（见 install_runtime_deps.cmake.in）。

if(WIN32)
    # 手段一：VS 开发者命令提示符 / VS 内构建会导出 VCToolsInstallDir，直接拼绝对路径。
    #   注意它只在"开发者环境"里存在，普通 shell 与 CI 默认没有，所以不是唯一手段。
    if(NOT "$ENV{VCToolsInstallDir}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{VCToolsInstallDir}" _gis_vc_tools_dir)
        set(_gis_scanner_candidate "${_gis_vc_tools_dir}/bin/Hostx64/x64/dumpbin.exe")
        if(EXISTS "${_gis_scanner_candidate}")
            set(GIS_RUNTIME_DEP_SCANNER "${_gis_scanner_candidate}")
            set(GIS_RUNTIME_DEP_SCANNER_TOOL "dumpbin")
        endif()
    endif()

    # 手段二：vswhere.exe。它的安装路径是 VS Installer 的固定契约，不需要任何 VS 开发者环境。
    #   本机实测（Git Bash、PATH 里没有 MSVC 工具链）：`$ENV{VCToolsInstallDir}` 为空 ⇒ 手段一不命中；
    #   vswhere 在场（469456 字节）且 `-latest -products * -requires …VC.Tools.x86.x64 -property installationPath`
    #   rc=0 返回一个 VS 安装根 ⇒ **现在烘进生成脚本的那个绝对路径就是这一支给的**。
    #   本机另测 powershell.exe 5.1 同形（`$env:VCToolsInstallDir` 空、vswhere 在场）。
    #   GitHub windows runner 侧**本机不可核**：CI 用的是 `shell: pwsh`（PS 7），本机没装 pwsh 7，
    #   且 gh 未登录 ⇒ runner 上 vswhere 的在场性与返回内容一律记"待远端验证"，不写成已证。
    if(GIS_RUNTIME_DEP_SCANNER STREQUAL "")
        set(_gis_vswhere "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
        if(EXISTS "${_gis_vswhere}")
            execute_process(
                COMMAND "${_gis_vswhere}" -latest -products *
                        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
                        -property installationPath
                OUTPUT_VARIABLE _gis_vs_install_path
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _gis_vswhere_rc
            )
            if(_gis_vswhere_rc EQUAL 0 AND NOT _gis_vs_install_path STREQUAL "")
                file(TO_CMAKE_PATH "${_gis_vs_install_path}" _gis_vs_install_path)
                # 一台机器可并存多个 MSVC 工具集版本，取版本号最大的那份 dumpbin。
                file(GLOB _gis_dumpbin_found
                    "${_gis_vs_install_path}/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe")
                if(_gis_dumpbin_found)
                    list(SORT _gis_dumpbin_found)
                    list(GET _gis_dumpbin_found -1 _gis_dumpbin_latest)
                    set(GIS_RUNTIME_DEP_SCANNER "${_gis_dumpbin_latest}")
                    set(GIS_RUNTIME_DEP_SCANNER_TOOL "dumpbin")
                endif()
            endif()
        endif()
    endif()

    # （这里原先是"手段三"：对 C:/D: 盘的常见 VS2022 根目录直接 glob，21-E 修复轮 1 删除。
    #   删的理由：① 它把某台打包机的盘符烤进人人共享的构建脚本；② 它是上面手段二那段
    #   glob + list(SORT) + 取最大 的重复实现；③ 本机实测它是死代码（手段二先命中）。
    #   vswhere 真不在的机器：在 VS 开发者环境里 configure 一次让手段一命中；再不行就吃安装期
    #   FATAL（见 install_runtime_deps.cmake.in）—— 响的失败，不是静默坏包。不再补第三种取径。）

    # 次选：objdump 一族（llvm-objdump 优先，它是 LLVM 官方构建、对 PE 的解析比 MSYS 版可靠）。
    #   这里用 find_program 是**配置期**的，结果同样烘成绝对路径 ⇒ 安装期不再看 PATH。
    if(GIS_RUNTIME_DEP_SCANNER STREQUAL "")
        find_program(_gis_llvm_objdump_command llvm-objdump)
        find_program(_gis_objdump_command objdump)
        if(_gis_llvm_objdump_command)
            file(TO_CMAKE_PATH "${_gis_llvm_objdump_command}" _gis_objdump_norm)
            set(GIS_RUNTIME_DEP_SCANNER "${_gis_objdump_norm}")
            set(GIS_RUNTIME_DEP_SCANNER_TOOL "objdump")
        elseif(_gis_objdump_command)
            file(TO_CMAKE_PATH "${_gis_objdump_command}" _gis_objdump_norm)
            set(GIS_RUNTIME_DEP_SCANNER "${_gis_objdump_norm}")
            set(GIS_RUNTIME_DEP_SCANNER_TOOL "objdump")
        endif()
    endif()

    if(GIS_RUNTIME_DEP_SCANNER)
        message(STATUS "Runtime dep scanner: ${GIS_RUNTIME_DEP_SCANNER}")
    else()
        # 找不到**不能**在配置期 FATAL：本机 Git Bash 这类"configure 得到、扫描器找不到"是常态，
        #   配置期 FATAL 会让所有人连 configure 都做不了。拒绝发生在安装期
        #   （install_runtime_deps.cmake 里 message(FATAL_ERROR)），见该文件的 N84 注释。
        message(WARNING
            "未能在配置期定位运行库依赖扫描器（dumpbin / llvm-objdump / objdump）。"
            "配置照常继续，但 `cmake --install` 会拒绝安装而不是打包出一个缺 DLL 的坏包。"
            "补救：在\"Developer Command Prompt for VS 2022\"（或 VS 开发者环境）里重新 configure 一次，"
            "让上面的 `Runtime dep scanner:` 行出现绝对路径。")
    endif()
endif()

# ── C17（拍板 2，2026-10-26）：VC++ 运行库随包 ⇒ 目标机零前提（N87 销账）──────────────
# 为什么用 InstallRequiredSystemLibraries 而不是自己列一份 CRT 名单：
#   ① 名单会漂。N88 那一族的失效方式从来不是"名单变红"，而是"新条目不被断言"。
#   ② 也试过第三条路 —— 让现成的 `file(GET_RUNTIME_DEPENDENCIES)` 按导入表自然解析：把
#      `VC/Redist/MSVC/*/x64/Microsoft.VC143.CRT`（与 debug_nonredist 的 DebugCRT）加进它的
#      DIRECTORIES，重配后生成的脚本里那两行目录确实在场，**但装出来的包里一个 CRT 都没有，
#      且未解析列表是空的** ⇒ 那些名字被扫描器按系统件静默丢弃了。这条实测结论记在
#      cmake/install_runtime_deps.cmake.in 的 C17 注释里，别再照着"加目录就会带"设计第二遍。
# 分工：本段只负责**随包**（带哪几件由工具集算）；"该带的有没有真带上"由
#   tests/check_install_closure.cmake 从**包内每个 PE 的直接导入名**判 —— 那一侧已把
#   `^msvcp140.* / ^vcruntime140.* / ^concrt140.* / ^vccorlib140.*` 从系统白名单里删掉，
#   只留带 d 的调试变体（来自 Redist 的 debug_nonredist，目录名即官方口径"不可再分发"，
#   且交付形态是 Release）。⇒ 少带一件被真实导入的运行库 ⇒ install_closure 红，
#   而不是装出一个干净机器上 0xC0000135 起不来的包。
#   本机实测被导入的 CRT 是 **7 个**名字（msvcp140 / vcruntime140 / vcruntime140_1 /
#   msvcp140_1 / msvcp140_2 / msvcp140_atomic_wait / concrt140），不是工单 C17 里写的三件套
#   ⇒ 只带三件套仍然起不来；这也是不把名单抄进任何一处的另一个理由。
if(WIN32 AND MSVC)
    include(InstallRequiredSystemLibraries)
    set(GIS_VC_RUNTIME_LIBS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
    set(GIS_VC_RUNTIME_LIBS_NAMES "")
    foreach(_gis_vc_lib IN LISTS GIS_VC_RUNTIME_LIBS)
        get_filename_component(_gis_vc_name "${_gis_vc_lib}" NAME)
        list(APPEND GIS_VC_RUNTIME_LIBS_NAMES "${_gis_vc_name}")
    endforeach()
    list(LENGTH GIS_VC_RUNTIME_LIBS GIS_VC_RUNTIME_LIBS_COUNT)
    if(GIS_VC_RUNTIME_LIBS_COUNT GREATER 0)
        install(PROGRAMS ${GIS_VC_RUNTIME_LIBS} DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()
    message(STATUS "[C17] VC++ 运行库随包 ${GIS_VC_RUNTIME_LIBS_COUNT} 件：${GIS_VC_RUNTIME_LIBS_NAMES}")
    # 安装期两道闸（与 N84 同口径：宁可装不出来，也不要静默少带）。
    # WHY 不在配置期 FATAL：只装了 BuildTools 而无 Redist 组件的机器会连 configure 都做不了
    #   —— 与运行库扫描器同一处置（见上面 GIS_RUNTIME_DEP_SCANNER 那段"找不到不能配置期红"）。
    install(CODE "
        if(NOT ${GIS_VC_RUNTIME_LIBS_COUNT} GREATER 0)
            message(FATAL_ERROR
                \"安装中止（C17 / 拍板 2）：InstallRequiredSystemLibraries 在本配置下没解析出任何 VC++ 运行库。\"
                \"交付口径是目标机零前提 ⇒ 没有 CRT 可带就不许装出一个缺 CRT 的包。\"
                \"处置：给这套 VS 安装补 VC++ Redistributable 生成组件（Microsoft.VisualStudio.Component.VC.Redist.14.Latest），\"
                \"或换一台带该组件的打包机重跑 cmake --install。\")
        endif()
        set(_gis_c17_bin \"\${CMAKE_INSTALL_PREFIX}/@CMAKE_INSTALL_BINDIR@\")
        set(_gis_c17_missing \"\")
        foreach(_gis_c17_name IN LISTS \"${GIS_VC_RUNTIME_LIBS_NAMES}\")
            if(NOT EXISTS \"\${_gis_c17_bin}/\${_gis_c17_name}\")
                list(APPEND _gis_c17_missing \"\${_gis_c17_name}\")
            endif()
        endforeach()
        if(_gis_c17_missing)
            list(JOIN _gis_c17_missing \", \" _gis_c17_missing_text)
            message(FATAL_ERROR
                \"安装中止（C17 / 拍板 2）：VC++ 运行库应随包，但 \${_gis_c17_bin} 里找不到 \"
                \"\${_gis_c17_missing_text}。配置期算出的名单：${GIS_VC_RUNTIME_LIBS_NAMES}\")
        endif()
        message(STATUS \"[C17] VC++ 运行库已随包：${GIS_VC_RUNTIME_LIBS_COUNT} 件\")
    ")
endif()

if(NOT EXISTS "${GIS_VCPKG_PROJ_DATA_DIR}")
    message(FATAL_ERROR "未找到 PROJ 数据目录: ${GIS_VCPKG_PROJ_DATA_DIR}")
endif()

if(NOT EXISTS "${GIS_VCPKG_GDAL_DATA_DIR}")
    message(FATAL_ERROR "未找到 GDAL 数据目录: ${GIS_VCPKG_GDAL_DATA_DIR}")
endif()

find_package(GDAL REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc features2d flann calib3d ml imgcodecs)
find_package(PROJ REQUIRED)

include(${CMAKE_SOURCE_DIR}/cmake/PluginTargets.cmake)

add_subdirectory(src/core)
add_subdirectory(src/framework)
add_subdirectory(src/plugins/projection)
add_subdirectory(src/plugins/cutting)
add_subdirectory(src/plugins/matching)
add_subdirectory(src/plugins/processing)
add_subdirectory(src/plugins/raster_math)
add_subdirectory(src/plugins/raster_inspect)
add_subdirectory(src/plugins/raster_manage)
add_subdirectory(src/plugins/raster_render)
add_subdirectory(src/plugins/georef)
add_subdirectory(src/plugins/terrain)
add_subdirectory(src/plugins/classification)
add_subdirectory(src/plugins/spindex)
add_subdirectory(src/plugins/vector)

# ── 插件 dll 名单：由 GIS_PLUGIN_TARGETS 派生（Task B6 / G-B5-1，与 N88 同族）────────
# 原先有**两处**手抄的 13 个插件 dll 名：cmake/install_runtime_deps.cmake.in 的安装期扫描根、
#   tests/check_install_closure.cmake 的包内存在性断言。两处互为对照却没人保证同步 ⇒
#   加一个插件要记得改两处，忘改闭包那一侧的失败方向是"新增插件不被断言"（漏判、绿着发残包），
#   正是 N86 那一类。现在两侧都从本表取：安装脚本烘进去、闭包脚本由 ctest 注册与 CI 各自传进来，
#   三个消费点同一份真源（cmake/PluginTargets.cmake 的 GIS_PLUGIN_TARGETS，
#   它本来已经是 gis-cli / gis-gui / gis_tests 的依赖表）。
# 名字按 CMake 自身的产物命名规则从**目标属性**算（PREFIX + OUTPUT_NAME + 共享库后缀），
#   不是"目标名拼上 .dll"的第二次抄写；带 debug/release 后缀的目标会让这条推导失效，所以直接拒。
if(NOT CMAKE_SHARED_LIBRARY_SUFFIX STREQUAL ".dll")
    message(FATAL_ERROR
        "插件 dll 名单推导假设共享库后缀是 .dll（收到 '${CMAKE_SHARED_LIBRARY_SUFFIX}'）。"
        "本仓是 Windows/MSVC-only，真要出别的平台得连带重做安装期扫描根与闭包断言（挂账 S-5），"
        "不要在这里改成通用推导把它读成'对任何平台都安全'。")
endif()
set(GIS_PLUGIN_DLL_NAMES "")
foreach(_gis_plugin_target IN LISTS GIS_PLUGIN_TARGETS)
    if(NOT TARGET ${_gis_plugin_target})
        message(FATAL_ERROR
            "GIS_PLUGIN_TARGETS 里的 '${_gis_plugin_target}' 到这一步还不是已定义目标 ⇒ 名字算不出来。"
            "本段必须放在所有插件 add_subdirectory 之后（它读的是目标属性）。")
    endif()
    foreach(_gis_plugin_prop IN LISTS "DEBUG_POSTFIX;RELEASE_POSTFIX;RELWITHDEBINFO_POSTFIX;MINSIZEREL_POSTFIX")
        get_target_property(_gis_plugin_postfix ${_gis_plugin_target} ${_gis_plugin_prop})
        if(_gis_plugin_postfix)
            message(FATAL_ERROR
                "插件 ${_gis_plugin_target} 设了 ${_gis_plugin_prop}='${_gis_plugin_postfix}' ⇒"
                "按配置不同的实际文件名不再是本段算出的那一个，安装扫描根与闭包断言会双双判错对象。"
                "要加后缀就得先把这条派生改成按配置两份名单（N88 的运行库名单已有同款处理）。")
        endif()
    endforeach()
    get_target_property(_gis_plugin_out ${_gis_plugin_target} OUTPUT_NAME)
    if(NOT _gis_plugin_out)
        set(_gis_plugin_out "${_gis_plugin_target}")
    endif()
    get_target_property(_gis_plugin_prefix ${_gis_plugin_target} PREFIX)
    if(NOT _gis_plugin_prefix)
        set(_gis_plugin_prefix "${CMAKE_SHARED_LIBRARY_PREFIX}")
    endif()
    list(APPEND GIS_PLUGIN_DLL_NAMES "${_gis_plugin_prefix}${_gis_plugin_out}${CMAKE_SHARED_LIBRARY_SUFFIX}")
endforeach()
# 归零自守（与 N88 同一条纪律）：名单空了 ⇒ 安装扫描根只剩 exe、闭包断言一条插件都不判，
#   两侧都退化成"没什么可判 ⇒ 通过"，这种现场必须红在配置期。
list(LENGTH GIS_PLUGIN_DLL_NAMES _gis_plugin_dll_count)
if(_gis_plugin_dll_count LESS 1)
    message(FATAL_ERROR
        "插件 dll 名单归零（GIS_PLUGIN_TARGETS=${GIS_PLUGIN_TARGETS}）⇒ 派生改坏了（G-B5-1 自守）。")
endif()
set(_gis_plugin_dll_uniq "${GIS_PLUGIN_DLL_NAMES}")
list(REMOVE_DUPLICATES _gis_plugin_dll_uniq)
list(LENGTH _gis_plugin_dll_uniq _gis_plugin_dll_uniq_count)
if(NOT _gis_plugin_dll_uniq_count EQUAL _gis_plugin_dll_count)
    message(FATAL_ERROR
        "插件 dll 名单里有重名（${_gis_plugin_dll_count} 个名字去重后只剩 ${_gis_plugin_dll_uniq_count} 个）"
        "⇒ 两个插件目标会往同一目录写同一个文件名，安装扫描根与闭包断言都只看得见其中一份。")
endif()
message(STATUS
    "[plugin-dlls] 由 GIS_PLUGIN_TARGETS 派生 ${_gis_plugin_dll_count} 个插件 dll："
    "${GIS_PLUGIN_DLL_NAMES}")

# 安装脚本必须在上面那段之后生成：它靠 @GIS_PLUGIN_DLL_NAMES@ 把这份名单烘进去
#   （原先写在本文件更早处的 configure_file 会被搬到这里的唯一原因）。
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/install_runtime_deps.cmake.in"
    "${CMAKE_BINARY_DIR}/install_runtime_deps.cmake"
    @ONLY
)

include("${CMAKE_SOURCE_DIR}/cmake/gis_runtime_dlls.cmake")

add_subdirectory(src/cli)

add_custom_target(copy_runtime_dlls ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/share/proj
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/share/gdal
)

gis_copy_minimal_runtime(copy_runtime_dlls DEST_DIR "${CMAKE_BINARY_DIR}/plugins")

add_custom_command(TARGET copy_runtime_dlls POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${GIS_VCPKG_PROJ_DATA_DIR}
        ${CMAKE_BINARY_DIR}/share/proj
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${GIS_VCPKG_GDAL_DATA_DIR}
        ${CMAKE_BINARY_DIR}/share/gdal
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources/leaflet
        ${CMAKE_BINARY_DIR}/share/leaflet
    COMMENT "Copying runtime DLLs and data to plugins directory"
)

install(DIRECTORY
    "${GIS_VCPKG_PROJ_DATA_DIR}/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/proj"
)

install(DIRECTORY
    "${GIS_VCPKG_GDAL_DATA_DIR}/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/gdal"
)

install(DIRECTORY
    "${CMAKE_SOURCE_DIR}/resources/icons/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/icons"
)

install(DIRECTORY
    "${CMAKE_SOURCE_DIR}/resources/leaflet/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/leaflet"
)

if(GIS_BUILD_GUI)
    find_package(Qt6 COMPONENTS Core Gui Widgets Svg Sql REQUIRED)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
    add_subdirectory(src/gui)
endif()

if(GIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(SCRIPT "${CMAKE_BINARY_DIR}/install_runtime_deps.cmake")
