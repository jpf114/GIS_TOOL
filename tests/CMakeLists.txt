find_package(GTest REQUIRED)

add_executable(gis_tests
    test_core.cpp
    test_framework.cpp
    test_plugins.cpp
)

target_link_libraries(gis_tests PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
    opencv_core
    opencv_imgproc
    GTest::gtest
    GTest::gtest_main
)

if(MSVC)
    target_compile_options(gis_tests PRIVATE /utf-8)
endif()

# Copy vcpkg runtime DLLs next to test executable so plugin DLLs can find dependencies
add_custom_command(TARGET gis_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin
        $<TARGET_FILE_DIR:gis_tests>
)

include(GoogleTest)
gtest_discover_tests(gis_tests)
