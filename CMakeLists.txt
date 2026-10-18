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
#   而不是取决于仓库配置。本机（Git Bash / 普通 pwsh）PATH 里没有 MSVC 工具链 ⇒ 找不到 ⇒
#   旧脚本静默回落到写死 release 的 36 项清单 ⇒ 包内缺 35 个真实导入依赖
#   （Lerc / zstd / archive / libxml2 / libcurl 都在 gdal.dll 自己的导入表里），
#   `gis-cli.exe --list` 实测 rc=-1073741515（STATUS_DLL_NOT_FOUND），而 `cmake --install` 仍 rc=0。
#   同一个包换个环境装出来就能跑，这种不可复现的打包必须消灭：解析挪到配置期，
#   绝对路径烘进脚本，配置日志里一眼可见（红了能直接定位是"没找到扫描器"而不是"包坏了"）。
set(GIS_RUNTIME_DEP_SCANNER "")
set(GIS_RUNTIME_DEP_SCANNER_TOOL "")

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

    # 手段二：vswhere.exe。它的安装路径是 VS Installer 的固定契约，不需要任何 VS 环境，
    #   因此 Git Bash、普通 pwsh、GitHub windows runner 三种现场都能命中（本机已实测存在）。
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

    # 手段三：vswhere 也不在的极端环境（裁剪过的 runner / 非默认安装），按常见根目录兜一次 glob。
    #   仍然只写盘上绝对路径，绝不依赖 PATH。
    if(GIS_RUNTIME_DEP_SCANNER STREQUAL "")
        file(GLOB _gis_dumpbin_found
            "C:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"
            "C:/Program Files (x86)/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"
            "D:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"
            "D:/Program Files (x86)/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe")
        if(_gis_dumpbin_found)
            list(SORT _gis_dumpbin_found)
            list(GET _gis_dumpbin_found -1 _gis_dumpbin_latest)
            set(GIS_RUNTIME_DEP_SCANNER "${_gis_dumpbin_latest}")
            set(GIS_RUNTIME_DEP_SCANNER_TOOL "dumpbin")
        endif()
    endif()

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

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/install_runtime_deps.cmake.in"
    "${CMAKE_BINARY_DIR}/install_runtime_deps.cmake"
    @ONLY
)

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
