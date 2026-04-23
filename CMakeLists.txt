cmake_minimum_required(VERSION 3.20)
project(gis-tool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

option(GIS_BUILD_GUI "Build Qt GUI" OFF)
option(GIS_BUILD_TESTS "Build tests" ON)

# vcpkg toolchain integration
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
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
add_subdirectory(src/plugins/vector)
add_subdirectory(src/plugins/utility)
add_subdirectory(src/cli)

if(GIS_BUILD_GUI)
    find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)
    add_subdirectory(src/gui)
endif()

if(GIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
