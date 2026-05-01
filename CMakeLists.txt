cmake_minimum_required(VERSION 3.21)

# vcpkg toolchain integration must happen before project()
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()
set(VCPKG_MANIFEST_MODE OFF CACHE BOOL "Use shared global vcpkg installation" FORCE)

project(gis-tool VERSION 0.1.3 LANGUAGES CXX)

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

set(GIS_VCPKG_ROOT "")
if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    set(GIS_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
elseif(DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    get_filename_component(GIS_VCPKG_ROOT "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
    get_filename_component(GIS_VCPKG_ROOT "${GIS_VCPKG_ROOT}/../.." ABSOLUTE)
endif()

if(GIS_VCPKG_ROOT STREQUAL "")
    message(FATAL_ERROR "未设置环境变量 VCPKG_ROOT，且无法从 CMAKE_TOOLCHAIN_FILE 推断全局 vcpkg 目录。")
endif()

set(GIS_VCPKG_TRIPLET "x64-windows" CACHE STRING "全局 vcpkg triplet")
set(GIS_VCPKG_INSTALLED_DIR "${GIS_VCPKG_ROOT}/installed/${GIS_VCPKG_TRIPLET}")
set(GIS_VCPKG_RELEASE_BIN_DIR "${GIS_VCPKG_INSTALLED_DIR}/bin")
set(GIS_VCPKG_DEBUG_BIN_DIR "${GIS_VCPKG_INSTALLED_DIR}/debug/bin")
set(GIS_VCPKG_SHARE_DIR "${GIS_VCPKG_INSTALLED_DIR}/share")
set(GIS_VCPKG_PROJ_DATA_DIR "${GIS_VCPKG_SHARE_DIR}/proj")
set(GIS_VCPKG_GDAL_DATA_DIR "${GIS_VCPKG_SHARE_DIR}/gdal")
set(GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR "${GIS_VCPKG_INSTALLED_DIR}/Qt6/plugins/platforms")
set(GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR "${GIS_VCPKG_INSTALLED_DIR}/debug/Qt6/plugins/platforms")
set(GIS_INSTALL_PLUGIN_DIR "${CMAKE_INSTALL_BINDIR}/plugins")
set(GIS_INSTALL_PLATFORM_DIR "${CMAKE_INSTALL_BINDIR}/platforms")

file(TO_CMAKE_PATH "${GIS_VCPKG_INSTALLED_DIR}" GIS_VCPKG_INSTALLED_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_RELEASE_BIN_DIR}" GIS_VCPKG_RELEASE_BIN_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_DEBUG_BIN_DIR}" GIS_VCPKG_DEBUG_BIN_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_SHARE_DIR}" GIS_VCPKG_SHARE_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_PROJ_DATA_DIR}" GIS_VCPKG_PROJ_DATA_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_GDAL_DATA_DIR}" GIS_VCPKG_GDAL_DATA_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}" GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR)
file(TO_CMAKE_PATH "${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}" GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR)

set(GIS_VCPKG_RUNTIME_BIN_DIR "$<$<CONFIG:Debug>:${GIS_VCPKG_DEBUG_BIN_DIR}>$<$<NOT:$<CONFIG:Debug>>:${GIS_VCPKG_RELEASE_BIN_DIR}>")
set(GIS_VCPKG_QT_PLATFORMS_DIR "$<$<CONFIG:Debug>:${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}>$<$<NOT:$<CONFIG:Debug>>:${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}>")

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/install_runtime_deps.cmake.in"
    "${CMAKE_BINARY_DIR}/install_runtime_deps.cmake"
    @ONLY
)

if(NOT EXISTS "${GIS_VCPKG_INSTALLED_DIR}")
    message(FATAL_ERROR "未找到全局 vcpkg 安装目录: ${GIS_VCPKG_INSTALLED_DIR}")
endif()

if(NOT EXISTS "${GIS_VCPKG_PROJ_DATA_DIR}")
    message(FATAL_ERROR "未找到 PROJ 数据目录: ${GIS_VCPKG_PROJ_DATA_DIR}")
endif()

if(NOT EXISTS "${GIS_VCPKG_GDAL_DATA_DIR}")
    message(FATAL_ERROR "未找到 GDAL 数据目录: ${GIS_VCPKG_GDAL_DATA_DIR}")
endif()

find_package(GDAL REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc features2d flann calib3d highgui)
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
add_subdirectory(src/plugins/classification)
add_subdirectory(src/plugins/spindex)
add_subdirectory(src/plugins/vector)
add_subdirectory(src/plugins/utility)
add_subdirectory(src/cli)

# Copy vcpkg runtime DLLs to plugins directory so plugin DLLs can find their dependencies
add_custom_target(copy_runtime_dlls ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/share/proj
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/share/gdal
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${GIS_VCPKG_RUNTIME_BIN_DIR}
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${GIS_VCPKG_PROJ_DATA_DIR}
        ${CMAKE_BINARY_DIR}/share/proj
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${GIS_VCPKG_GDAL_DATA_DIR}
        ${CMAKE_BINARY_DIR}/share/gdal
    COMMENT "Copying runtime DLLs to plugins directory"
)

install(DIRECTORY
    "${GIS_VCPKG_PROJ_DATA_DIR}/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/proj"
)

install(DIRECTORY
    "${GIS_VCPKG_GDAL_DATA_DIR}/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/gdal"
)

if(GIS_BUILD_GUI)
    find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)
    add_subdirectory(src/gui)
endif()

if(GIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(SCRIPT "${CMAKE_BINARY_DIR}/install_runtime_deps.cmake")
