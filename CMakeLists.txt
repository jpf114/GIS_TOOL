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
