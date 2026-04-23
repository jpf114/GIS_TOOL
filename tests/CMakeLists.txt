find_package(GTest REQUIRED)

add_executable(gis_tests
    test_core.cpp
    test_framework.cpp
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

include(GoogleTest)
gtest_discover_tests(gis_tests)
