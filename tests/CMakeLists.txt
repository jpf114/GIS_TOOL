find_package(GTest REQUIRED)

add_executable(gis_tests
    test_main.cpp
    test_core.cpp
    test_framework.cpp
    test_plugins.cpp
    test_gui_support.cpp
    test_support.cpp
    ../src/gui/gui_data_support.cpp
)

target_link_libraries(gis_tests PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
    opencv_core
    opencv_imgproc
    GTest::gtest
)

if(MSVC)
    target_compile_options(gis_tests PRIVATE /utf-8)
endif()

add_library(plugin_test_lifecycle SHARED
    plugins/test_lifecycle_plugin.cpp
)

target_link_libraries(plugin_test_lifecycle PRIVATE
    gis_framework
)

set_target_properties(plugin_test_lifecycle PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_test_lifecycle"
)

if(MSVC)
    target_compile_options(plugin_test_lifecycle PRIVATE /utf-8)
endif()

# Copy vcpkg runtime DLLs next to test executable so plugin DLLs can find dependencies
add_custom_command(TARGET gis_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${GIS_VCPKG_RUNTIME_BIN_DIR}
        $<TARGET_FILE_DIR:gis_tests>
)

include(GoogleTest)
gtest_discover_tests(gis_tests DISCOVERY_MODE PRE_TEST)

find_program(GIS_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
if(GIS_POWERSHELL_EXECUTABLE)
    add_custom_target(real_vector_regression
        COMMAND ${CMAKE_COMMAND}
            -DCLI_PATH=$<TARGET_FILE:gis-cli>
            -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
            -DPOWERSHELL_EXECUTABLE=${GIS_POWERSHELL_EXECUTABLE}
            -DMODE=quick
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_vector_regression.cmake
        DEPENDS gis-cli plugin_vector copy_runtime_dlls
        USES_TERMINAL
        COMMENT "运行真实矢量回归脚本"
    )
endif()
