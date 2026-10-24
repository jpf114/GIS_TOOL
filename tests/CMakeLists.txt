find_package(GTest REQUIRED)

add_executable(gis_tests
    test_main.cpp
    test_core.cpp
    test_framework.cpp
    test_plugins.cpp
    test_cli_parser.cpp
    test_gui_support.cpp
    test_mainwindow.cpp
    test_support.cpp
    test_fixtures.cpp
    test_coverage_meta.cpp
    test_app_settings.cpp
    ../src/gui/app_settings.cpp
    ../src/gui/custom_index_preset_store.cpp
    ../src/gui/gui_data_support.cpp
    ../src/gui/gdal_config.cpp
    ../src/gui/mainwindow.cpp
    ../src/gui/nav_panel.cpp
    ../src/gui/param_card_widget.cpp
    ../src/gui/param_widget.cpp
    ../src/gui/crs_dialog.cpp
    ../src/gui/execute_worker.cpp
    ../src/gui/qt_progress_reporter.cpp
    ../src/gui/icon_manager.cpp
    ../src/gui/settings_manager.cpp
    ../src/gui/task_center_page.cpp
    ../src/gui/task_manager.cpp
    ../src/gui/task_database.cpp
    ../src/gui/task_runner.cpp
    ../src/gui/result_preview_page.cpp
    ../src/gui/raster_preview_widget.cpp
    ../src/gui/vector_preview_widget.cpp
    ../src/gui/stats_preview_widget.cpp
    ../src/gui/workflow_page.cpp
    ../src/gui/welcome_dialog.cpp
    ../src/cli/cli_parser.cpp
)

target_link_libraries(gis_tests PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
    opencv_core
    opencv_imgproc
    PROJ::proj
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
    Qt6::Svg
    Qt6::Widgets
    GTest::gtest
)

# The coverage meta-test reads tests/action_gold_cases.txt from the source tree, not from the
# build tree, so the registry can be versioned and reviewed next to the tests it describes.
# Without this define the fallback in test_coverage_meta.cpp resolves to "." and the
# registry lookup silently depends on the current working directory.
target_compile_definitions(gis_tests PRIVATE GIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(MSVC)
    target_compile_options(gis_tests PRIVATE /utf-8)
endif()

set_target_properties(gis_tests PROPERTIES
    AUTOMOC ON
)

add_library(plugin_test_lifecycle SHARED
    plugins/test_lifecycle_plugin.cpp
)

add_executable(gui_test_data_helper
    gui_test_data_helper.cpp
)

target_link_libraries(plugin_test_lifecycle PRIVATE
    gis_framework
)

target_link_libraries(gui_test_data_helper PRIVATE
    gis_core
    GDAL::GDAL
)

set_target_properties(plugin_test_lifecycle PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_test_lifecycle"
)

if(MSVC)
    target_compile_options(plugin_test_lifecycle PRIVATE /utf-8)
    target_compile_options(gui_test_data_helper PRIVATE /utf-8)
endif()

add_dependencies(gis_tests
    copy_runtime_dlls
    gui_test_data_helper
    plugin_test_lifecycle
    ${GIS_PLUGIN_TARGETS}
)

add_custom_command(TARGET gis_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_test_lifecycle>
        $<TARGET_FILE_DIR:gis_tests>/$<TARGET_FILE_NAME:plugin_test_lifecycle>
)

set(GIS_TEST_QT_PLATFORMS_DIR "$<$<CONFIG:Debug>:${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}>$<$<NOT:$<CONFIG:Debug>>:${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}>")
set(GIS_TEST_QT_WINDOWS_DLL "$<$<CONFIG:Debug>:qwindowsd.dll>$<$<NOT:$<CONFIG:Debug>>:qwindows.dll>")
set(GIS_TEST_QT_HEADLESS_PLATFORM "offscreen")
if(EXISTS "${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}/qminimald.dll" OR
   EXISTS "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/qminimal.dll")
    set(GIS_TEST_QT_HEADLESS_PLATFORM "minimal")
endif()
if(EXISTS "${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}/qoffscreend.dll" OR
   EXISTS "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/qoffscreen.dll")
    set(GIS_TEST_QT_HEADLESS_PLATFORM "offscreen")
endif()
set(GIS_TEST_QT_HEADLESS_DLL "$<$<CONFIG:Debug>:q${GIS_TEST_QT_HEADLESS_PLATFORM}d.dll>$<$<NOT:$<CONFIG:Debug>>:q${GIS_TEST_QT_HEADLESS_PLATFORM}.dll>")

add_custom_command(TARGET gis_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:gis_tests>/platforms
    COMMAND ${CMAKE_COMMAND}
        -D_SRC="${GIS_TEST_QT_PLATFORMS_DIR}/${GIS_TEST_QT_WINDOWS_DLL}"
        -D_DST="$<TARGET_FILE_DIR:gis_tests>/platforms/${GIS_TEST_QT_WINDOWS_DLL}"
        -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
    COMMAND ${CMAKE_COMMAND}
        -D_SRC="${GIS_TEST_QT_PLATFORMS_DIR}/${GIS_TEST_QT_HEADLESS_DLL}"
        -D_DST="$<TARGET_FILE_DIR:gis_tests>/platforms/${GIS_TEST_QT_HEADLESS_DLL}"
        -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
    COMMAND_EXPAND_LISTS
)

set(GIS_TEST_QT_SQLDRIVERS_DIR "$<$<CONFIG:Debug>:${GIS_VCPKG_DEBUG_QT_DIR}/plugins/sqldrivers>$<$<NOT:$<CONFIG:Debug>>:${GIS_VCPKG_RELEASE_QT_DIR}/plugins/sqldrivers>")
set(GIS_TEST_QT_SQLITE_DLL "$<$<CONFIG:Debug>:qsqlited.dll>$<$<NOT:$<CONFIG:Debug>>:qsqlite.dll>")

add_custom_command(TARGET gis_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:gis_tests>/sqldrivers
    COMMAND ${CMAKE_COMMAND}
        -D_SRC="${GIS_TEST_QT_SQLDRIVERS_DIR}/${GIS_TEST_QT_SQLITE_DLL}"
        -D_DST="$<TARGET_FILE_DIR:gis_tests>/sqldrivers/${GIS_TEST_QT_SQLITE_DLL}"
        -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
    COMMAND_EXPAND_LISTS
)

gis_copy_minimal_runtime(gis_tests DEST_DIR "$<TARGET_FILE_DIR:gis_tests>")

include(GoogleTest)
# The single definition of the blanket per-test timeout. The gtest-discovered leg below and
# the configure-time leg at the bottom of this file must carry the same number, so neither
# one writes a literal - the sync that used to be held together by two "keep 300 in sync"
# comments is now held by this variable.
set(GIS_TEST_TIMEOUT 300)
# DISCOVERY_MODE PRE_TEST means the discovered gtest cases do not exist at configure time,
# so the blanket timeout at the bottom of this file - which reads the TESTS directory
# property, i.e. only cases already registered by add_test - cannot reach them. Their
# timeout has to travel through PROPERTIES, which GoogleTest.cmake forwards to every test
# produced by this invocation.
# TIMEOUT is the only timeout that exists here: PROCESS_TIMEOUT is not a CMake property
# (cmake --help-property-list has TIMEOUT, TIMEOUT_AFTER_MATCH, TIMEOUT_SIGNAL_NAME and
# TIMEOUT_SIGNAL_GRACE_PERIOD), and an accepted-but-ignored property would only mislead
# the reader into believing there is a second safety layer. The real outer backstop is the
# CI job timeout-minutes.
gtest_discover_tests(gis_tests
    WORKING_DIRECTORY $<TARGET_FILE_DIR:gis_tests>
    DISCOVERY_TIMEOUT 60
    DISCOVERY_MODE PRE_TEST
    # 21-FixA part2 C-1：这一条改由下面的 add_test 亲自注册，好带上 RESOURCE_LOCK。
    #   发现侧的 PROPERTIES 是"整套用例共用"的，把 RESOURCE_LOCK 写在这里会连带发现侧全部用例一起串行
    #   （条数见下面那两条现跑命令，别在这一行记账），
    #   而 PRE_TEST 模式下 configure-time 的 set_tests_properties 又够不着还没诞生的用例
    #   ⇒ 唯一能只给这一条上锁的办法就是把它从发现集合里摘出来、自己注册。
    #   用例名与 gtest 全名逐字一致（GuiContract.ExportActionVisibleKeys），
    #   ctest -R、CI 的名单断言与台账里的引用都不受影响。两侧条数不在这里记账 —— 写死的数会随用例
    #   增减失真（本行原来那两个数就是这么过期的），现跑命令：
    #     总数：      ctest --test-dir build/debug -C Debug -N | tail -n 1
    #     configure-time 侧（gui_）：ctest --test-dir build/debug -C Debug -N -R "^gui_" | tail -n 1
    #     gtest 发现侧 = 总数 − gui_ 侧（减出来的，不是数出来的；要逐条核对就用上面两条的名单做差集）
    TEST_FILTER "-GuiContract.ExportActionVisibleKeys"
    PROPERTIES TIMEOUT ${GIS_TEST_TIMEOUT}
)

if(TARGET gis-gui)
    add_test(
        NAME gui_smoke_startup
        COMMAND $<TARGET_FILE:gis-gui> -platform ${GIS_TEST_QT_HEADLESS_PLATFORM} --self-test
    )
    # No PASS_REGULAR_EXPRESSION here on purpose: when that property holds a non-empty
    # pattern and the pattern matches the output, ctest ignores the process exit code, so a
    # crashing gis-gui could be recorded as Passed. The empty string this property used to
    # carry is the exception - it behaves like an unset property and falls back to the exit
    # code (verified locally) - which makes the old line harmless but not a check of
    # anything. The exit code must be the verdict. Content validation of --self-test is a
    # Plan 3 (C1/D1) item and needs the expected string added together with the output it
    # checks.
    set_tests_properties(gui_smoke_startup PROPERTIES
        TIMEOUT 60
        # N101 + N102：这一条是**唯一**由 ctest 直接 spawn gis-gui.exe 的用例（不经过任何 .cmake
        # driver）⇒ helpers 的 include 期 set(ENV{…}) 载体够不到它，只能走 ENVIRONMENT 属性。
        # 与 driver 腿一样，两个落点变量必须一起给：src/gui/main.cpp 的守卫在回归式运行下
        # 少任一个都非零退出，这是所有 spawn 腿共同的兜底。落点仍是该用例自己的产物目录。
        # 现跑命令（行首锚定 ⇒ 注释行不会被数进去；数字归报告，不进注释）：
        #   直接 spawn 的用例数：grep -cE "^[[:space:]]*COMMAND \$<TARGET_FILE:gis-gui>" tests/CMakeLists.txt
        #   用此属性的用例数：grep -cE "^[[:space:]]*ENVIRONMENT" tests/CMakeLists.txt
        #   注册 gui_ 用例总数：ctest --test-dir build/debug -C Debug -N -R "^gui_" | tail -n 1
        ENVIRONMENT "GIS_TOOL_SETTINGS_INI=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/smoke_startup/gis_gui_settings.ini;GIS_CUSTOM_INDEX_PRESET_FILE=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/smoke_startup/spindex_presets.json"
    )

    add_test(
        NAME gui_vector_convert_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_input.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/convert/output.geojson
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/convert/gui_convert.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_regression.cmake
    )

    add_test(
        NAME gui_invalid_param_fast_fail_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_input.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/invalid_param/output.geojson
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/invalid_param/gui_invalid_param.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_invalid_param_regression.cmake
    )

    add_test(
        NAME gui_vector_buffer_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_projected_input.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/buffer/output.gpkg
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/buffer/gui_buffer.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_buffer_regression.cmake
    )

    add_test(
        NAME gui_vector_buffer_geographic_failure_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_input.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/buffer_geographic_failure/output.gpkg
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/buffer_geographic_failure/gui_buffer_failure.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_buffer_failure_regression.cmake
    )

    add_test(
        NAME gui_vector_dissolve_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_dissolve_input.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/dissolve/output.geojson
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/dissolve/gui_dissolve.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_dissolve_regression.cmake
    )

    add_test(
        NAME gui_vector_union_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DACTION=union
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_overlay_input.geojson
            -DOVERLAY_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_overlay_mask.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/union/output.geojson
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/union/gui_union.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_overlay_regression.cmake
    )

    add_test(
        NAME gui_vector_difference_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DACTION=difference
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_overlay_input.geojson
            -DOVERLAY_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_overlay_mask.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/difference/output.geojson
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/difference/gui_difference.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_overlay_regression.cmake
    )

    add_test(
        NAME gui_vector_intersect_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DACTION=intersect
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_overlay_input.geojson
            -DOVERLAY_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_overlay_mask.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/intersect/output.geojson
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/intersect/gui_intersect.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_overlay_regression.cmake
    )

    add_test(
        NAME gui_vector_filter_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_input.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/vector_filter/output.geojson
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/vector_filter/gui_vector_filter.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_filter_regression.cmake
    )

    add_test(
        NAME gui_vector_filter_extent_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_input.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/vector_filter_extent/output.geojson
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/vector_filter_extent/gui_vector_filter_extent.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_filter_extent_regression.cmake
    )

    add_test(
        NAME gui_vector_clip_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_overlay_input.geojson
            -DCLIP_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_overlay_mask.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/vector_clip/output.geojson
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/vector_clip/gui_vector_clip.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_clip_regression.cmake
    )

    add_test(
        NAME gui_cutting_clip_cutline_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/cutting_clip_cutline/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/cutting_clip_cutline/gui_cutting_clip_cutline.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_cutting_clip_cutline_regression.cmake
    )

    add_test(
        NAME gui_vector_rasterize_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_overlay_input.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/vector_rasterize/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/vector_rasterize/gui_vector_rasterize.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_rasterize_regression.cmake
    )

    add_test(
        NAME gui_vector_polygonize_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/vector_polygonize/output.geojson
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/vector_polygonize/gui_vector_polygonize.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_polygonize_regression.cmake
    )

    add_test(
        NAME gui_vector_info_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_input.geojson
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/vector_info/gui_vector_info.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_info_regression.cmake
    )

    add_test(
        NAME gui_projection_reproject_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_input.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/projection_reproject/output.gpkg
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/projection_reproject/gui_projection_reproject.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_projection_reproject_regression.cmake
    )

    add_test(
        NAME gui_projection_info_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/projection_info/gui_projection_info.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_projection_info_regression.cmake
    )

    add_test(
        NAME gui_projection_transform_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/projection_transform/gui_projection_transform.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_projection_transform_regression.cmake
    )

    add_test(
        NAME gui_projection_assign_srs_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/projection_assign_srs/gui_projection_assign_srs.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_projection_assign_srs_regression.cmake
    )

    add_test(
        NAME gui_raster_inspect_histogram_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_inspect_histogram/output.json
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_inspect_histogram/gui_raster_inspect_histogram.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_raster_inspect_histogram_regression.cmake
    )

    add_test(
        NAME gui_raster_inspect_histogram_invalid_band_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_inspect_histogram_invalid_band/output.json
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_inspect_histogram_invalid_band/gui_raster_inspect_histogram_invalid_band.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_raster_inspect_histogram_invalid_band_regression.cmake
    )

    add_test(
        NAME gui_raster_render_colormap_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_render_colormap/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_render_colormap/gui_raster_render_colormap.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_raster_render_colormap_regression.cmake
    )

    add_test(
        NAME gui_spindex_ndvi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_ndvi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_ndvi/gui_spindex_ndvi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_ndvi_regression.cmake
    )

    add_test(
        NAME gui_spindex_ndwi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_ndwi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_ndwi/gui_spindex_ndwi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_ndwi_regression.cmake
    )

    add_test(
        NAME gui_spindex_ndmi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_ndmi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_ndmi/gui_spindex_ndmi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_ndmi_regression.cmake
    )

    add_test(
        NAME gui_spindex_evi2_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_evi2/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_evi2/gui_spindex_evi2.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_evi2_regression.cmake
    )

    add_test(
        NAME gui_spindex_bsi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_bsi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_bsi/gui_spindex_bsi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_bsi_regression.cmake
    )

    add_test(
        NAME gui_spindex_custom_index_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_custom_index/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_custom_index/gui_spindex_custom_index.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_custom_index_regression.cmake
    )

    add_test(
        NAME gui_spindex_evi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_evi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_evi/gui_spindex_evi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_evi_regression.cmake
    )

    add_test(
        NAME gui_spindex_savi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_savi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_savi/gui_spindex_savi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_savi_regression.cmake
    )

    add_test(
        NAME gui_spindex_osavi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_osavi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_osavi/gui_spindex_osavi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_osavi_regression.cmake
    )

    add_test(
        NAME gui_spindex_gndvi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_gndvi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_gndvi/gui_spindex_gndvi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_gndvi_regression.cmake
    )

    add_test(
        NAME gui_spindex_mndwi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_mndwi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_mndwi/gui_spindex_mndwi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_mndwi_regression.cmake
    )

    add_test(
        NAME gui_spindex_ndbi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_ndbi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_ndbi/gui_spindex_ndbi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_ndbi_regression.cmake
    )

    add_test(
        NAME gui_spindex_arvi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_arvi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_arvi/gui_spindex_arvi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_arvi_regression.cmake
    )

    add_test(
        NAME gui_spindex_nbr_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_nbr/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_nbr/gui_spindex_nbr.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_nbr_regression.cmake
    )

    add_test(
        NAME gui_spindex_awei_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_awei/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_awei/gui_spindex_awei.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_awei_regression.cmake
    )

    add_test(
        NAME gui_spindex_ui_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_ui/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_ui/gui_spindex_ui.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_ui_regression.cmake
    )

    add_test(
        NAME gui_spindex_bi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_bi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_bi/gui_spindex_bi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_bi_regression.cmake
    )

    add_test(
        NAME gui_raster_manage_nodata_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_manage_nodata/gui_raster_manage_nodata.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_raster_manage_nodata_regression.cmake
    )

    add_test(
        NAME gui_raster_manage_overviews_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_manage_overviews/gui_raster_manage_overviews.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_raster_manage_overviews_regression.cmake
    )

    add_test(
        NAME gui_raster_tools_overviews_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_tools_overviews/gui_raster_tools_overviews.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_raster_tools_overviews_regression.cmake
    )

    add_test(
        NAME gui_raster_inspect_info_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_inspect_info/gui_raster_inspect_info.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_raster_inspect_info_regression.cmake
    )

    add_test(
        NAME gui_terrain_slope_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_slope/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_slope/gui_terrain_slope.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_slope_regression.cmake
    )

    add_test(
        NAME gui_terrain_aspect_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_aspect/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_aspect/gui_terrain_aspect.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_aspect_regression.cmake
    )

    add_test(
        NAME gui_terrain_hillshade_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_hillshade/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_hillshade/gui_terrain_hillshade.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_hillshade_regression.cmake
    )

    add_test(
        NAME gui_terrain_tpi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_tpi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_tpi/gui_terrain_tpi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_tpi_regression.cmake
    )

    add_test(
        NAME gui_terrain_curvature_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_curvature/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_curvature/gui_terrain_curvature.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_curvature_regression.cmake
    )

    add_test(
        NAME gui_terrain_profile_curvature_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_profile_curvature/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_profile_curvature/gui_terrain_profile_curvature.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_profile_curvature_regression.cmake
    )

    add_test(
        NAME gui_terrain_plan_curvature_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_plan_curvature/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_plan_curvature/gui_terrain_plan_curvature.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_plan_curvature_regression.cmake
    )

    add_test(
        NAME gui_terrain_tri_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_tri/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_tri/gui_terrain_tri.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_tri_regression.cmake
    )

    add_test(
        NAME gui_terrain_roughness_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_roughness/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_roughness/gui_terrain_roughness.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_roughness_regression.cmake
    )

    add_test(
        NAME gui_terrain_fill_sinks_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_fill_sinks/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_fill_sinks/gui_terrain_fill_sinks.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_fill_sinks_regression.cmake
    )

    add_test(
        NAME gui_terrain_flow_direction_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_flow_direction/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_flow_direction/gui_terrain_flow_direction.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_flow_direction_regression.cmake
    )

    add_test(
        NAME gui_terrain_flow_accumulation_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_flow_accumulation/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_flow_accumulation/gui_terrain_flow_accumulation.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_flow_accumulation_regression.cmake
    )

    add_test(
        NAME gui_terrain_watershed_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_watershed/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_watershed/gui_terrain_watershed.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_watershed_regression.cmake
    )

    add_test(
        NAME gui_terrain_stream_extract_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_stream_extract/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_stream_extract/gui_terrain_stream_extract.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_stream_extract_regression.cmake
    )

    add_test(
        NAME gui_terrain_profile_extract_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_profile_extract/output.csv
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_profile_extract/gui_terrain_profile_extract.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_profile_extract_regression.cmake
    )

    add_test(
        NAME gui_terrain_viewshed_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_viewshed/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_viewshed/gui_terrain_viewshed.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_viewshed_regression.cmake
    )

    add_test(
        NAME gui_terrain_viewshed_multi_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_viewshed_multi/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_viewshed_multi/gui_terrain_viewshed_multi.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_viewshed_multi_regression.cmake
    )

    add_test(
        NAME gui_terrain_cut_fill_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_cut_fill/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_cut_fill/gui_terrain_cut_fill.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_cut_fill_regression.cmake
    )

    add_test(
        NAME gui_terrain_reservoir_volume_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_reservoir_volume/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/terrain_reservoir_volume/gui_terrain_reservoir_volume.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_terrain_reservoir_volume_regression.cmake
    )

    add_test(
        NAME gui_classification_feature_stats_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/classification_feature_stats/output.json
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/classification_feature_stats/gui_classification_feature_stats.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_classification_feature_stats_regression.cmake
    )

    add_test(
        NAME gui_classification_svm_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DACTION=svm_classify
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/classification_svm/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/classification_svm/gui_classification_svm.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_classification_supervised_regression.cmake
    )

    add_test(
        NAME gui_classification_random_forest_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DACTION=random_forest_classify
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/classification_random_forest/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/classification_random_forest/gui_classification_random_forest.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_classification_supervised_regression.cmake
    )

    add_test(
        NAME gui_classification_max_likelihood_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DACTION=max_likelihood_classify
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/classification_max_likelihood/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/classification_max_likelihood/gui_classification_max_likelihood.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_classification_supervised_regression.cmake
    )

    add_test(
        NAME gui_processing_threshold_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_threshold/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_threshold/gui_processing_threshold.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_threshold_regression.cmake
    )

    add_test(
        NAME gui_processing_threshold_invalid_band_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_threshold_invalid_band/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_threshold_invalid_band/gui_processing_threshold_invalid_band.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_threshold_invalid_band_regression.cmake
    )

    add_test(
        NAME gui_processing_filter_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_filter/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_filter/gui_processing_filter.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_filter_regression.cmake
    )

    add_test(
        NAME gui_processing_filter_invalid_band_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_filter_invalid_band/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_filter_invalid_band/gui_processing_filter_invalid_band.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_filter_invalid_band_regression.cmake
    )

    add_test(
        NAME gui_processing_enhance_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_enhance/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_enhance/gui_processing_enhance.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_enhance_regression.cmake
    )

    add_test(
        NAME gui_processing_edge_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_edge/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_edge/gui_processing_edge.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_edge_regression.cmake
    )

    add_test(
        NAME gui_processing_contour_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_contour/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_contour/gui_processing_contour.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_contour_regression.cmake
    )

    add_test(
        NAME gui_processing_stats_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_stats/gui_processing_stats.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_stats_regression.cmake
    )

    add_test(
        NAME gui_raster_math_band_math_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_math_band_math/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_math_band_math/gui_raster_math_band_math.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_raster_math_band_math_regression.cmake
    )

    add_test(
        NAME gui_raster_tools_band_math_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_tools_band_math/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_tools_band_math/gui_raster_tools_band_math.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_raster_tools_band_math_regression.cmake
    )

    add_test(
        NAME gui_processing_hough_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_hough/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_hough/gui_processing_hough.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_hough_regression.cmake
    )

    add_test(
        NAME gui_processing_watershed_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_watershed/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_watershed/gui_processing_watershed.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_watershed_regression.cmake
    )

    add_test(
        NAME gui_processing_kmeans_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_kmeans/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_kmeans/gui_processing_kmeans.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_kmeans_regression.cmake
    )

    add_test(
        NAME gui_processing_gabor_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_gabor/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_gabor/gui_processing_gabor.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_gabor_regression.cmake
    )

    add_test(
        NAME gui_processing_glcm_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_glcm/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_glcm/gui_processing_glcm.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_glcm_regression.cmake
    )

    add_test(
        NAME gui_processing_mean_shift_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_mean_shift/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_mean_shift/gui_processing_mean_shift.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_mean_shift_regression.cmake
    )

    foreach(processing_action IN ITEMS skeleton connected_components)
        add_test(
            NAME gui_processing_${processing_action}_offscreen
            COMMAND ${CMAKE_COMMAND}
                -DGUI_PATH=$<TARGET_FILE:gis-gui>
                -DACTION=${processing_action}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_${processing_action}/output.tif
                -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_${processing_action}/gui_processing_${processing_action}.png
                -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_binary_regression.cmake
        )
    endforeach()

    add_test(
        NAME gui_raster_tools_cog_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_tools_cog/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_tools_cog/gui_raster_tools_cog.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_raster_tools_cog_regression.cmake
    )

    add_test(
        NAME gui_raster_tools_histogram_match_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_tools_histogram_match/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/raster_tools_histogram_match/gui_raster_tools_histogram_match.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_raster_tools_histogram_match_regression.cmake
    )

    set(GUI_VECTOR_ADVANCED_GPKG_SPECS
        "gui_vector_simplify_offscreen|simplify|projected_touching_polygons|vector_simplify|output.gpkg|tolerance=5"
        "gui_vector_repair_offscreen|repair|projected_invalid_polygon|vector_repair|output.gpkg|"
        "gui_vector_geom_metrics_offscreen|geom_metrics|projected_touching_polygons|vector_geom_metrics|output.gpkg|"
        "gui_vector_nearest_offscreen|nearest|projected_nearest_source_points|vector_nearest|output.gpkg|nearest_field=name|projected_nearest_target_points"
        "gui_vector_spatial_join_offscreen|spatial_join|projected_touching_polygons|vector_spatial_join|output.gpkg|join_field=name|projected_overlapping_polygons"
        "gui_vector_convex_hull_offscreen|convex_hull|projected_touching_polygons|vector_convex_hull|output.gpkg|"
        "gui_vector_centroid_offscreen|centroid|projected_touching_polygons|vector_centroid|output.gpkg|"
        "gui_vector_envelope_offscreen|envelope|projected_touching_polygons|vector_envelope|output.gpkg|"
        "gui_vector_boundary_offscreen|boundary|projected_touching_polygons|vector_boundary|output.gpkg|"
        "gui_vector_singlepart_offscreen|singlepart|projected_multipart_polygons|vector_singlepart|output.gpkg|"
        "gui_vector_vertices_extract_offscreen|vertices_extract|projected_touching_polygons|vector_vertices_extract|output.gpkg|"
        "gui_vector_endpoints_extract_offscreen|endpoints_extract|projected_lines|vector_endpoints_extract|output.gpkg|"
        "gui_vector_midpoints_extract_offscreen|midpoints_extract|projected_lines|vector_midpoints_extract|output.gpkg|"
        "gui_vector_interior_point_offscreen|interior_point|projected_touching_polygons|vector_interior_point|output.gpkg|"
        "gui_vector_sliver_remove_offscreen|sliver_remove|projected_overlapping_polygons|vector_sliver_remove|output.gpkg|min_area=100"
    )

    foreach(spec IN LISTS GUI_VECTOR_ADVANCED_GPKG_SPECS)
        string(REPLACE "|" ";" fields "${spec}")
        list(GET fields 0 test_name)
        list(GET fields 1 action_name)
        list(GET fields 2 dataset_name)
        list(GET fields 3 output_dir_name)
        list(GET fields 4 output_file_name)
        list(GET fields 5 extra_params)
        set(target_dataset_name "")
        list(LENGTH fields field_count)
        if(field_count GREATER 6)
            list(GET fields 6 target_dataset_name)
        endif()

        set(test_command
            ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DACTION=${action_name}
            -DDATASET=${dataset_name}
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/${output_dir_name}/${output_file_name}
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/${output_dir_name}/${test_name}.png
        )
        if(NOT extra_params STREQUAL "")
            list(APPEND test_command -DEXTRA_PARAMS=${extra_params})
        endif()
        if(NOT target_dataset_name STREQUAL "")
            list(APPEND test_command -DTARGET_DATASET=${target_dataset_name})
        endif()
        list(APPEND test_command -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_advanced_regression.cmake)
        add_test(NAME ${test_name} COMMAND ${test_command})
    endforeach()

    set(GUI_VECTOR_ADVANCED_CSV_SPECS
        "gui_vector_adjacency_offscreen|adjacency|projected_touching_polygons|vector_adjacency|output.csv|"
        "gui_vector_overlap_check_offscreen|overlap_check|projected_overlapping_polygons|vector_overlap_check|output.csv|"
        "gui_vector_topology_check_offscreen|topology_check|projected_invalid_polygon|vector_topology_check|output.csv|"
        "gui_vector_multipart_check_offscreen|multipart_check|projected_multipart_polygons|vector_multipart_check|output.csv|"
        "gui_vector_duplicate_point_check_offscreen|duplicate_point_check|projected_points_duplicate|vector_duplicate_point_check|output.csv|"
        "gui_vector_hole_check_offscreen|hole_check|projected_hole_polygon|vector_hole_check|output.csv|"
        "gui_vector_dangling_endpoint_check_offscreen|dangling_endpoint_check|projected_lines|vector_dangling_endpoint_check|output.csv|"
    )

    foreach(spec IN LISTS GUI_VECTOR_ADVANCED_CSV_SPECS)
        string(REPLACE "|" ";" fields "${spec}")
        list(GET fields 0 test_name)
        list(GET fields 1 action_name)
        list(GET fields 2 dataset_name)
        list(GET fields 3 output_dir_name)
        list(GET fields 4 output_file_name)
        list(GET fields 5 extra_params)

        set(test_command
            ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DACTION=${action_name}
            -DDATASET=${dataset_name}
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/${output_dir_name}/${output_file_name}
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/${output_dir_name}/${test_name}.png
        )
        if(NOT extra_params STREQUAL "")
            list(APPEND test_command -DEXTRA_PARAMS=${extra_params})
        endif()
        list(APPEND test_command -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_advanced_regression.cmake)
        add_test(NAME ${test_name} COMMAND ${test_command})
    endforeach()

    add_test(
        NAME gui_georef_dos_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_dos/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_dos/gui_georef_dos.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_georef_dos_regression.cmake
    )

    add_test(
        NAME gui_georef_radiometric_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_radiometric/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_radiometric/gui_georef_radiometric.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_georef_radiometric_regression.cmake
    )

    add_test(
        NAME gui_georef_gcp_register_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_gcp_register/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_gcp_register/gui_georef_gcp_register.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_georef_gcp_register_regression.cmake
    )

    add_test(
        NAME gui_georef_cosine_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DACTION=cosine_correction
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_cosine/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_cosine/gui_georef_cosine.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_georef_topographic_regression.cmake
    )

    add_test(
        NAME gui_georef_minnaert_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DACTION=minnaert_correction
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_minnaert/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_minnaert/gui_georef_minnaert.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_georef_topographic_regression.cmake
    )

    add_test(
        NAME gui_georef_c_correction_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DACTION=c_correction
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_c_correction/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_c_correction/gui_georef_c_correction.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_georef_topographic_regression.cmake
    )

    add_test(
        NAME gui_georef_quac_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_quac/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_quac/gui_georef_quac.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_georef_quac_regression.cmake
    )

    add_test(
        NAME gui_georef_rpc_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_rpc/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/georef_rpc/gui_georef_rpc.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_georef_rpc_regression.cmake
    )

    add_test(
        NAME gui_processing_template_match_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_template_match/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_template_match/gui_processing_template_match.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_template_match_regression.cmake
    )

    add_test(
        NAME gui_processing_pansharpen_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_pansharpen/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/processing_pansharpen/gui_processing_pansharpen.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_processing_pansharpen_regression.cmake
    )

    add_test(
        NAME gui_matching_corner_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_corner/output.json
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_corner/gui_matching_corner.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_matching_corner_regression.cmake
    )

    add_test(
        NAME gui_matching_corner_invalid_band_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_corner_invalid_band/output.json
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_corner_invalid_band/gui_matching_corner_invalid_band.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_matching_corner_invalid_band_regression.cmake
    )

    add_test(
        NAME gui_matching_detect_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_detect/output.json
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_detect/gui_matching_detect.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_matching_detect_regression.cmake
    )

    add_test(
        NAME gui_matching_detect_invalid_band_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_detect_invalid_band/output.json
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_detect_invalid_band/gui_matching_detect_invalid_band.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_matching_detect_invalid_band_regression.cmake
    )

    add_test(
        NAME gui_matching_match_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_match/output.json
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_match/gui_matching_match.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_matching_match_regression.cmake
    )

    add_test(
        NAME gui_matching_change_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_change/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_change/gui_matching_change.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_matching_change_regression.cmake
    )

    add_test(
        NAME gui_matching_register_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_register/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_register/gui_matching_register.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_matching_register_regression.cmake
    )

    add_test(
        NAME gui_matching_ecc_register_debug_failure_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGIS_TEST_BUILD_CONFIG=$<CONFIG>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_ecc_register_debug_failure/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_ecc_register_debug_failure/gui_matching_ecc_register_debug_failure.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_matching_ecc_register_debug_failure_regression.cmake
        CONFIGURATIONS Debug
    )

    add_test(
        NAME gui_matching_stitch_debug_failure_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGIS_TEST_BUILD_CONFIG=$<CONFIG>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_stitch_debug_failure/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_stitch_debug_failure/gui_matching_stitch_debug_failure.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_matching_stitch_debug_failure_regression.cmake
        CONFIGURATIONS Debug
    )
    # One shared properties block for both debug-only cases: a second set_tests_properties
    # call would overwrite the first. The per-configuration exclusion has to be done with
    # add_test's CONFIGURATIONS option, not as a test property - CONFIGURATIONS is not a
    # test property in CMake, so setting it here would be silently ignored and the cases
    # would keep fake-passing in Release. Not DISABLED either: DISABLED is not per
    # configuration and would also kill these cases in Debug, where they must really run.
    set_tests_properties(
        gui_matching_ecc_register_debug_failure_offscreen
        gui_matching_stitch_debug_failure_offscreen
        PROPERTIES LABELS "debug_only"
    )

    add_test(
        NAME gui_cutting_clip_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/cutting_clip/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/cutting_clip/gui_cutting_clip.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_cutting_clip_regression.cmake
    )

    add_test(
        NAME gui_cutting_mosaic_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/cutting_mosaic/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/cutting_mosaic/gui_cutting_mosaic.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_cutting_mosaic_regression.cmake
    )

    add_test(
        NAME gui_cutting_split_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/cutting_split/output_tiles
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/cutting_split/gui_cutting_split.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_cutting_split_regression.cmake
    )

    add_test(
        NAME gui_cutting_merge_bands_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/cutting_merge_bands/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/cutting_merge_bands/gui_cutting_merge_bands.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_cutting_merge_bands_regression.cmake
    )

    add_test(
        NAME gui_cutting_merge_bands_list_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/cutting_merge_bands_list/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/cutting_merge_bands_list/gui_cutting_merge_bands_list.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_cutting_merge_bands_list_regression.cmake
    )

    add_test(
        NAME gui_matching_match_content_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_match_content/output.json
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_match_content/gui_matching_match_content.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_matching_match_content_regression.cmake
    )
endif()

# ---------------------------------------------------------------- Task 21-C 反向参数守卫
# 命题与 #334 相反：某 action 的 visibleKeys 里不得出现该 action 根本不读的键，
# 判据是"白名单 − 闭包内实读 非空即红"，不设阈值（阈值＝自我缴械，Task 10 的教训）。
# 白名单侧由 gis_tests 的 GuiContract.ExportActionVisibleKeys 导出、实读侧由脚本静态扫
# src/plugins 得出；脚本自己跑一次 gis_tests 生成产物，所以本用例不依赖 ctest 的执行顺序。
# WHY 放在 if(TARGET gis-gui) 之外：守卫读的是 gis_tests 里编进来的 gui_data_support.cpp
#   与插件**源码文本**，不需要 gis-gui 可执行文件；放进去会让不带 GUI 的构型静默失去守卫。
add_test(
    NAME param_guard_visible_keys_all_read
    COMMAND ${CMAKE_COMMAND}
        -DGIS_TESTS_PATH=$<TARGET_FILE:gis_tests>
        -DWHITELIST_FILE=$<TARGET_FILE_DIR:gis_tests>/gui_action_visible_keys.txt
        -DPLUGIN_DIR=${CMAKE_SOURCE_DIR}/src/plugins
        -DREGISTRY_FILE=${CMAKE_CURRENT_SOURCE_DIR}/action_gold_cases.txt
        -DREPORT_FILE=${CMAKE_CURRENT_BINARY_DIR}/artifacts_21C/action_visible_keys_report.txt
        -DGIS_21C_SHAPE_PROBE=${CMAKE_CURRENT_SOURCE_DIR}/reader_shape_probe_sample.txt
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_action_visible_keys_read.cmake
)

# ------------------------------------------------------------------ 21-FixA part2 C-1：导出产物的并发写
# GuiContract.ExportActionVisibleKeys 与上面的守卫用例写/读的是**同一个固定路径**
#   $<TARGET_FILE_DIR:gis_tests>/gui_action_visible_keys.txt
#   （写侧：`tests/test_coverage_meta.cpp` 里那行 `std::ofstream ofs(outPath, … trunc)`；
#    读侧：`tests/check_action_visible_keys_read.cmake` 里那行 `file(STRINGS "${WHITELIST_FILE}" …)`。
#    两处都按代码文本找、不挂裸行号——同批已裁定过一次（`c0573ba`）。
# 全仓此前没有任何 RUN_SERIAL / RESOURCE_LOCK（grep -rcE "RESOURCE_LOCK|RUN_SERIAL" tests/CMakeLists.txt = 0），
# 而这条导出用例**同时**是 gtest 发现出来的独立用例 ⇒ `ctest -j≥2` 下两者可以在两个进程里
# 同时打开同一个文件。
# WHY 这是**假绿方向**（不是"只是避免竞争"、也不是无害的假红）：
#   守卫的判据是"白名单 − 闭包内实读 非空即红"，白名单侧完全来自这份产物的行数与内容。
#   若读侧读到的是另一进程刚 trunc、还没写完的半份文件：
#     visible_rows 变少 ⇒ 参与比对的 (plugin,action) 键集合变小 ⇒ 差集跟着变小甚至为空 ⇒
#     **本该报出的冗余键报不出来**，而脚本自己校验的两道闸（EXPORT_RC==0、stdout 有 [21C-EXPORT]）
#     全都不会响——写它的那个进程确实成功了。方向与 21-C 立项命题（"每条守卫各自可杀"）正相反。
#   另一侧才是假红：后到的进程撞 Windows 共享冲突 ⇒ ASSERT_TRUE(static_cast<bool>(ofs)) 红。
# WHY 选 RESOURCE_LOCK 而不是 RUN_SERIAL：RUN_SERIAL 的语义是"这条用例执行期间整个 ctest 不许有别人"，
#   锁粒度是全局 ⇒ 只要这两条被调度，同一时刻注册进来的其余全部用例都被迫排队，等于把并行度清零。
#   RESOURCE_LOCK 只在"声明了同一把锁名"的用例之间互斥，别的用例照旧并行 ⇒ 代价精确等于必要。
#   （要那一分母就跑现跑命令，别在这里记账：ctest --test-dir build/release -C Release -N | tail -n 1
#     把 build/release/-C Release 换成 build/debug/-C Debug 即另一棵树；条数随用例增减，K-21 纪律。）
add_test(
    NAME GuiContract.ExportActionVisibleKeys
    COMMAND $<TARGET_FILE:gis_tests> --gtest_filter=GuiContract.ExportActionVisibleKeys
)
set_tests_properties(GuiContract.ExportActionVisibleKeys PROPERTIES
    WORKING_DIRECTORY $<TARGET_FILE_DIR:gis_tests>
    RESOURCE_LOCK gis_action_visible_keys
    # 与 gtest_discover_tests 给其余全部 gtest 用例设的那道跳过识别保持一致（条数会随用例增减，
    # 故不在此抄写分母 —— K-21 纪律：结论入库、当时的条数不入库，要复算就跑 ctest -N），
    # 否则这条用例一旦被
    # GTEST_SKIP 就变成"退出码 0 ⇒ Passed"的假绿（发现侧靠这条正则把 skipped 认出来）。
    SKIP_REGULAR_EXPRESSION "\\[  SKIPPED \\]"
)
set_tests_properties(param_guard_visible_keys_all_read PROPERTIES
    RESOURCE_LOCK gis_action_visible_keys
)

# 跨配置运行库残留审计（终审第 1 席 I-7 + 控制器存在性审计；判据与理由见该脚本文件头）。
# WHY 用 $<CONFIG> 而不是写死 Debug：本树是多配置生成器，同一棵 build 树里两配置的产物并存，
#   判"某个 dll 是不是属于本配置"必须按**当前被测配置**选源目录，否则 Debug 树里的 release 件
#   会在 Release 视角下被判正常（反之亦然），审计就永远查不到自己那半边的污染。
add_test(
    NAME runtime_dll_cross_config_audit
    COMMAND ${CMAKE_COMMAND}
        -DCONFIG=$<CONFIG>
        -DRELEASE_BIN=${GIS_VCPKG_RELEASE_BIN_DIR}
        -DDEBUG_BIN=${GIS_VCPKG_DEBUG_BIN_DIR}
        -DPLUGINS_DIR=${CMAKE_BINARY_DIR}/plugins
        -DEXE_DIR=$<TARGET_FILE_DIR:gis-cli>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_runtime_dll_config.cmake
)

# ---------------------------------------------------------- Task B5 / N86：包内闭包断言
# 判据本体与"为什么必须有两半（存在性 + 导入闭包）"写在 tests/check_install_closure.cmake 文件头。
# 这里只登记注册层面的四个决定，理由不重复：
# WHY 用例自己跑 `cmake --install`（传 -DBUILD_DIR 就是让脚本这么干）而不是依赖"别人已经装过"：
#   判的是**装出来的包**，而 ctest 之前没有任何一步保证这个包装过（CI 的 Install Release 甚至排在
#   两个测试步之后）。自带安装 ⇒ 这条用例任何时刻单跑都成立；前缀不存在时脚本 FATAL_ERROR
#   点名"安装步没产出"，绝不把"没装过"读成"闭包通过"（N86 的失败方向就是"绿着发坏包"）。
# WHY 前缀钉在 <构树>/../tmp/install_closure_<CONFIG> 而不是仓库根的 install/：
#   脚本每次先整体删除再装这个目录 —— 残留文件会把"包是齐的"假绿成上一次装的东西还在。
#   交付目录 install/ 是 NSIS 与 zip 的取材地，不能当测试耗材；这条约束由脚本自己校验
#   （它按 BUILD_DIR 反推唯一合法前缀，指错就拒绝执行），不是只在注释里写写。
# WHY $<CONFIG>：本树是多配置生成器，"哪些 dll 该在包里"按配置不同（vcpkg 用带 d 的名字区分），
#   写死 Debug 会让 Release 视角永远在判 Debug 的包，反之亦然。
# WHY 传 ${GIS_RUNTIME_DEP_SCANNER}（根 CMakeLists.txt 的**普通变量**，与本树烘进
#   install_runtime_deps.cmake 的那条绝对路径同源）：没有扫描器就不知道包里该有哪些 DLL，
#   这时脚本 FATAL_ERROR 而不是跳过。解析挪到脚本里去重做一遍会造出第二套扫描器取径
#   （N84 修的正是"装哪些 DLL 取决于当场 PATH"），必须禁止。
# WHY -DPLUGIN_DLLS 传 ${GIS_PLUGIN_DLL_NAMES} 而不是在脚本里再写一遍插件名（G-B5-1）：
#   那条名单是根 CMakeLists.txt 在配置期从 GIS_PLUGIN_TARGETS（cmake/PluginTargets.cmake，
#   本来也就是 gis-cli/gis-gui/gis_tests 的依赖表）按目标属性派生的，与安装脚本烘进去的那份同源；
#   在这里抄一份就等于把 N88 的手抄名单换个地方再种一次。CMake 会把含分号的变量整体作为**一个**
#   参数写进 CTestTestfile（生成物里是 `-DPLUGIN_DLLS=a\;b\;…`），复算口径：
#     grep -o '\-DPLUGIN_DLLS=[^"]*' tests/CTestTestfile.cmake
#   取到的应当是一条以分号分隔的完整名单，而不是若干条被拆开的 -D。
# WHY RESOURCE_LOCK 而不是 RUN_SERIAL：这条用例会删装一个固定名字的目录，同树并发跑两次会互相
#   删掉对方的现场；RUN_SERIAL 的锁粒度是整个 ctest（会把其余全部用例拖成串行），
#   RESOURCE_LOCK 只在同名锁之间互斥 ⇒ 代价精确等于必要。
get_filename_component(GIS_INSTALL_CLOSURE_PREFIX "${CMAKE_BINARY_DIR}/../tmp/install_closure" ABSOLUTE)
# ⚠ 这里必须先把分号**转义**再传：add_test 会把 `${GIS_PLUGIN_DLL_NAMES}` 这种含分号的变量
#   当成列表拆成**多个参数**（本任务实测的生成物形态：
#   `"-DPLUGIN_DLLS=plugin_projection.dll" "plugin_cutting.dll" …` 共 13 个 arg），
#   脚本只会看见第一条 ⇒ 其余 12 个 dll 装了却没人断言存在 —— 静默退回 N86 那个"绿着发残包"。
#   转义后生成物是一条 `-DPLUGIN_DLLS=a\;b\;…`，cmake -P 收到时又还原成分号分隔的列表。
#   复算口径（不要数插件名，数参数）：
#     grep -o '"-DPLUGIN_DLLS=[^"]*"' build/debug/tests/CTestTestfile.cmake | head -1
#   取到的应当是一条**含 \;** 的参数；这条判据本身也写在脚本里（见 _gis_unasserted_plugins 那段：
#   名单没覆盖包内实际存在的 plugin_*.dll 就红），不靠注释自觉。
string(REPLACE ";" "\\;" GIS_PLUGIN_DLL_NAMES_FOR_TEST "${GIS_PLUGIN_DLL_NAMES}")
add_test(
    NAME install_closure
    COMMAND ${CMAKE_COMMAND}
        -DPREFIX=${GIS_INSTALL_CLOSURE_PREFIX}_$<CONFIG>
        -DCONFIG=$<CONFIG>
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DSCANNER=${GIS_RUNTIME_DEP_SCANNER}
        -DSCANNER_TOOL=${GIS_RUNTIME_DEP_SCANNER_TOOL}
        -DPLUGIN_DLLS=${GIS_PLUGIN_DLL_NAMES_FOR_TEST}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_install_closure.cmake
)
# 这条用例比别的脚本用例多做一次完整 `cmake --install` 加上一轮逐个 PE 的扫描器调用，所以在
# add_test 附近就把上界写明，不靠文件末尾那条 blanket 兜底。数值读的是同一个 GIS_TEST_TIMEOUT
# 而不是另抄一遍字面量：blanket 稍后会对本目录全部用例再设一次同一个变量，
# 两处若各写各的，改动画像上界的人只会改到一处 ⇒ 另一处静默失效。
set_tests_properties(install_closure PROPERTIES
    TIMEOUT ${GIS_TEST_TIMEOUT}
    RESOURCE_LOCK gis_install_closure_prefix
)

find_program(GIS_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
if(GIS_POWERSHELL_EXECUTABLE)
    add_custom_target(real_raster_regression
        COMMAND ${CMAKE_COMMAND}
            -DCLI_PATH=$<TARGET_FILE:gis-cli>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
            -DPOWERSHELL_EXECUTABLE=${GIS_POWERSHELL_EXECUTABLE}
            -DMODE=quick
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_raster_regression.cmake
        DEPENDS gis-cli gui_test_data_helper plugin_processing plugin_classification plugin_spindex plugin_raster_math plugin_raster_inspect plugin_raster_manage plugin_raster_render plugin_terrain plugin_projection plugin_cutting plugin_georef copy_runtime_dlls
        USES_TERMINAL
        COMMENT "Run real raster regression (quick)"
    )

    add_custom_target(real_raster_regression_full
        COMMAND ${CMAKE_COMMAND}
            -DCLI_PATH=$<TARGET_FILE:gis-cli>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
            -DPOWERSHELL_EXECUTABLE=${GIS_POWERSHELL_EXECUTABLE}
            -DMODE=full
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_raster_regression.cmake
        DEPENDS gis-cli gui_test_data_helper plugin_processing plugin_classification plugin_spindex plugin_raster_math plugin_raster_inspect plugin_raster_manage plugin_raster_render plugin_terrain plugin_projection plugin_cutting plugin_georef copy_runtime_dlls
        USES_TERMINAL
        COMMENT "Run real raster regression (full)"
    )

    add_custom_target(real_matching_regression
        COMMAND ${CMAKE_COMMAND}
            -DCLI_PATH=$<TARGET_FILE:gis-cli>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
            -DPOWERSHELL_EXECUTABLE=${GIS_POWERSHELL_EXECUTABLE}
            -DMODE=quick
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_matching_regression.cmake
        DEPENDS gis-cli gui_test_data_helper plugin_matching copy_runtime_dlls
        USES_TERMINAL
        COMMENT "Run real matching regression (quick)"
    )

    add_custom_target(real_matching_regression_full
        COMMAND ${CMAKE_COMMAND}
            -DCLI_PATH=$<TARGET_FILE:gis-cli>
            -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
            -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
            -DPOWERSHELL_EXECUTABLE=${GIS_POWERSHELL_EXECUTABLE}
            -DMODE=full
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_matching_regression.cmake
        DEPENDS gis-cli gui_test_data_helper plugin_matching copy_runtime_dlls
        USES_TERMINAL
        COMMENT "Run real matching regression (full)"
    )

    add_custom_target(real_vector_regression
        COMMAND ${CMAKE_COMMAND}
            -DCLI_PATH=$<TARGET_FILE:gis-cli>
            -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
            -DPOWERSHELL_EXECUTABLE=${GIS_POWERSHELL_EXECUTABLE}
            -DMODE=quick
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_vector_regression.cmake
        USES_TERMINAL
        DEPENDS gis-cli plugin_vector plugin_projection copy_runtime_dlls
        COMMENT "Run real vector regression (quick)"
    )

    add_custom_target(real_vector_regression_full
        COMMAND ${CMAKE_COMMAND}
            -DCLI_PATH=$<TARGET_FILE:gis-cli>
            -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
            -DPOWERSHELL_EXECUTABLE=${GIS_POWERSHELL_EXECUTABLE}
            -DMODE=full
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_vector_regression.cmake
        USES_TERMINAL
        DEPENDS gis-cli plugin_vector plugin_projection copy_runtime_dlls
        COMMENT "Run real vector regression (full)"
    )
endif()

# ── Task C2 / N112：四条真实数据回归纳入 ctest ──────────────────────────────────
# WHY 必须进 ctest（N112 本体）：上面那六个 add_custom_target 不在 ctest 注册数里 ⇒ 判据写坏了
#   没有任何机器级兜底，"改完判据后是否还跑"全靠人记得。2026-10-21 这批真实回归首跑就抓到 4 条
#   判据缺陷（期望值是合成样本标定的常量、china_bbox 窗口退化、`Contains("feature_count: 2")` 被真实
#   输出 `feature_count: 22836` 当**前缀**吃掉的假绿、PowerShell 5.1 按 ANSI 解 UTF-8 JSON 报的假产品
#   缺陷；判据正文见 docs/RELEASE.md"收尾二轮：真实矢量回归暴露的四条判据缺陷"一节），其中三条的方向
#   都是"绿着什么都没判"⇒ 只修判据本体不等于销账，进了 ctest 才有"下次改坏当场红/跳"这一层。
# WHY 用例名走 real_data_*（不沿用 custom target 的 real_*_regression）：同名会让
#   `ctest -R real_vector_regression` 一次筛出两条语义不同的东西（构建期目标 vs 测试期用例），
#   红了也分不清是哪一侧；分开后 `-R "^real_data_"` 只认这一批（批次 C C3k 把 vector 的 full 档
#   拆成 checks/overlay 两条之后是 5 条，C3h-3 加 union 路径 A、C3k-2 再把 overlay 一分为二之后是 7 条）。custom target 原样保留，
#   人工入口与 docs/RELEASE.md 第 1 节那条 `cmake --build --target …` 的引用都不受影响。
# WHY 恒注册（写在 if(GIS_POWERSHELL_EXECUTABLE) 之外，与下面 source_mojibake_guard 同一形态）：
#   躲进那条 if 里 ⇒ 找不到解释器的机器上这一批**静默不注册**（ctest -N 少 7 条、全量照绿），
#   正是本批反复在消灭的"没检查长得像检查通过"。解释器缺失由包装脚本 FATAL_ERROR 判红。
# WHY data 缺失报 Skip 而不是 Passed：本文件既有的那条 SKIP_REGULAR_EXPRESSION（搜
#   `SKIP_REGULAR_EXPRESSION "\\[  SKIPPED \\]"`）的注释点名批评过"退出码 0 ⇒ Passed 就是假绿"，
#   而这三条腿的 .ps1 在真实数据缺失时会兜底现生成合成样本 ⇒ "没有真实数据"会跑成"全绿"。
#   所以缺 data 时包装打印独占一行的标记 `GIS_SKIP_TESTDATA_ABSENT: `（vector 腿精确到 data/vector）并 return()，
#   用例侧按 **per-test** 的 SKIP_REGULAR_EXPRESSION "GIS_SKIP_TESTDATA_ABSENT: " 认领（per-test 是本仓既有形态，
#   目录级没有这个机制可抄）。⚠ 这个 token 只允许三份包装打印；它被刻意取成不可能出现在 CLI/GDAL/.ps1 输出里的
#   形状，原因见上面"Skip 优先于退出码"那条。
#   检查顺序也有讲究：**先判解释器缺失（红），再判数据缺失（跳）**——反过来的话，
#   "fresh clone（data/ 本就被 gitignore）+ 没有 powershell"这个组合会被记成 Skipped，
#   而 Skipped 不参与失败判定 ⇒ "恒注册以便判红"这条设计就被绕过去了（复审 D 条实测）。
#   实测（本机 ctest 4.3.0-rc2 端到端三臂；`data/` 未被就地打开，但**本轮之前 info 探针已在 data/ 下
#   留下 15 个 .aux.xml 边车** ⇒ 不能再写"一字节未动"，见挂账 N115）：把同一份 run_real_matching_regression.cmake
#   挂进隔离最小工程 `build/tmp/r1probe/ctest` 当真用例跑 ⇒ ① 根含 tests/ 而无 data/ = `***Skipped***`
#   并进 "did not run"；② **换成真实仓库根**（数据齐）+ -DMODE=zzz（腿真失败）= `***Failed***`；
#   ③ -DPOWERSHELL_EXECUTABLE 留空 = `***Failed***`。⇒ 用例不空转。
#   ⚠ 臂 2 必须换根：沿用臂 1 那个"无 data"的根会得到 rc=0 + 标记（门控在 execute 之前），
#   上一版注释就是这么写的，被复审实测推翻后本轮改正。
#   ⚠ ⚠ Skip 的认领**优先于退出码**（隔离探针：先打标记再 FATAL_ERROR(rc=1) 的用例仍报 ***Skipped***、
#   "100% tests passed"）⇒ 认领串必须是别人打印不出来的 token。这就是标记从 `GIS_SKIP: `
#   改成 `GIS_SKIP_TESTDATA_ABSENT: ` 的原因：前缀那种写法的安全性只建立在"目前没有别的输出含它"上，
#   不是机制。复算：`grep -rn "GIS_SKIP_TESTDATA_ABSENT" tests/ | grep -v run_real_ | grep -v CMakeLists` 应为空。
#   复算命令与逐臂结果见三份 run_real_*.cmake 头部注释；另见下面 WORKSPACE_ROOT 判红那段的成因。
# WHY RESOURCE_LOCK real_data_regression（这一批 7 条共用一把**新**锁）：这几条腿都往
#   <仓库根>/tmp/*_benchmark 与 <仓库根>/tmp/*_regression 写产物（复算：
#   `grep -rl 'WorkspaceRoot.*tmp' tests/*.ps1` 恰好只有这三份脚本，仓库根 tmp/ 没有别的 ctest 写者），
#   vector 腿还会把 plugin_*.dll 覆盖到 gis-cli 旁边的 plugins/ ⇒ 并发跑就是两个进程同时改同一批耗材，
#   假绿假红两个方向都出得起。不复用 gis_install_closure_prefix：那条的前缀由
#   ${CMAKE_BINARY_DIR}/../tmp 反推 ⇒ 落在 **build/tmp**/install_closure_<CONFIG>（现读：
#   `ls build/tmp | grep install_closure`），与这几条写的 <仓库根>/tmp 不共享路径，
#   共用一把锁只会把两件不相干的事绑成串行。
#   ⚠ 本批不扩口的残留重叠：Sync-PluginDlls 覆盖的 gis-cli/plugins 目录，读它的是构建期的
#   copy_runtime_dlls（不在 ctest 里）⇒ `-j1` 下无争用；真要开 `-j` 时这一条得单独进
#   docs/RELEASE.md 那张"开 -j 前必须改什么"表。
# WHY 不用 RUN_SERIAL：锁粒度是整个 ctest ⇒ 这一批一被调度就拖其余全部用例排队（同上面
#   GuiContract.ExportActionVisibleKeys 那处已经定过的判断）。
# WHY TIMEOUT 各写各的、不进 GIS_TEST_TIMEOUT：GIS_TEST_TIMEOUT=300 是给"单条用例"定的上界，
#   真实回归这几条里 overlay 那一半（现读 Debug 265.29 s，见下表）本来就压在它之上，
#   raster/matching 两条又是空头上界 ⇒ 各写各的。
#   ⚠ 文件末尾那条 blanket `set_property(TEST ${GIS_TIMED_TEST_NAMES} PROPERTY TIMEOUT …)` 会把这里写的
#   值整体重写回 300 ⇒ 这七个名字必须同时进下面 `list(REMOVE_ITEM GIS_TIMED_TEST_NAMES …)` 的豁免名单
#   （名单走变量 GIS_REAL_DATA_TEST_NAMES，不在两处各抄一遍名字）。不豁免的后果不是"上界宽松"，
#   而是 **vector 两条腿在 Debug 树随时假 Timeout** —— 本仓已实测过并发把 300 s 线撞出 3 条假 Timeout。
# 定值规则（批次 C C3k 起，只对拆分出来的新腿生效）：TIMEOUT = ceil(实测墙钟 × 2.5 / 10) × 10。
#   2.5× 同时是**触发线**：再加真实腿把余量压到 2.5× 以下时，处理方式是**按"消费哪批派生件"拆腿**，
#   不许抬数字 —— TIMEOUT 是本仓唯一的挂起执法者（CMake 的 PROCESS_TIMEOUT 无效，已证），
#   抬 TIMEOUT 等于把挂起的检测窗口一起拉长，那就把这条机制本身废掉了。
#   C3h-3 这条新腿按**另一条**规则定值：TIMEOUT = ceil(最大现读 × 4 / 10) × 10 —— 4× 是 Task C2 就给
#   "新建的真实腿"定的起步口径（quick/raster/matching 三条都是 ≥4× 定的），2.5× 只是**触发拆腿**的线、
#   不是设计目标；把 2.5× 当设计目标会让新腿一开始就贴着线，下一条腿只能立刻再拆。
#   raster/matching/quick 三条沿用 Task C2 那批按"≥4× 向上取整到 60 s 的倍数"定的值，本轮不重定；
#   quick 本轮现读 Debug 236.47 s ⇒ 720 ÷ 236.47 = 3.05×，仍在 2.5× 之上，不需要跟着拆。
# 实测于 2026-10-22（批次 C C3k 拆腿轮；本机安静态、跑前硬门控 `tasklist | grep -cE 'MSBuild|gis_tests|ctest'` = 0，
#   `ctest -R "^real_data_vector"` -j1 串行现读，Debug 树与 Release 树各一轮；
#   取**整条腿墙钟**= ctest 报的每条用时，含 `cmake -P` 包装自身与 .ps1 里 Sync-PluginDlls /
#   生成 tmp/*_benchmark 的开销，**不是**用例内计时合计。
#   ⚠ 表内数字是**控制器自己在工作树上复跑**得到的；实现该刀的代理在撞每日额度上限前也报过一组
#     （quick 236.35 / checks 37.47 与 33.60 / overlay 265.29，Release 128.20/19.53/133.97），
#     两组差在噪声内，但本表只认控制器这组）：
#     腿                        Debug 墙钟     Release 墙钟   取值                                   ⇒ TIMEOUT
#     real_data_raster          （本轮未重测）  （本轮未重测）  Task C2 的 ≥4× 上界                      ⇒ 60 s
#     real_data_matching        （本轮未重测）  （本轮未重测）  Task C2 的 ≥4× 上界                      ⇒ 60 s
#     real_data_vector_quick    236.47 s       126.39 s       Task C2 的 ≥4× 上界，现读余量 3.05×       ⇒ 720 s
#     real_data_vector_checks   34.47 s        20.20 s        ceil(34.47 × 2.5 / 10) × 10 = 86.2→90，取 100 ⇒ 100 s（2.90×）
#     real_data_vector_overlay  255.98 s       130.05 s       ceil(255.98 × 2.5 / 10) × 10 = 640.0→640，取 670 ⇒ 670 s（2.62×）
#   ── C3h-3 新腿自己的一栏（实测于 2026-10-23，作者＝实现本刀的代理，不是控制器复跑）──
#     算法：TIMEOUT = ceil(最大现读 × 4 / 10) × 10 —— 4× 是 Task C2 给"新建真实腿"定的起步口径，
#           2.5× 只是**触发拆腿**的线、不是设计目标；取值取**两棵树里更慢的那一棵**的最大现读，
#           因为 TIMEOUT 是一个对两棵树同时生效的字面量（VS 多配置生成器，set_tests_properties 不分配置）。
#     测法：本机安静态、每条腿**至少两跑取最大值**（本腿另在全组轮里读了第三跑，见下）、
#           跑前硬门控 `tasklist | grep -cE 'MSBuild|gis_tests|ctest'` = 0、
#           单跑用 `ctest -R "^real_data_vector_union_poly$" -C <Debug|Release>`（不带 -j ⇒ 本就串行），
#           全组轮用 `ctest -R "^real_data_vector" -C <Debug|Release>`（四条 vector 腿同锁串行），
#           取整条腿墙钟 = ctest 报的每条用时（含 cmake -P 包装、Sync-PluginDlls、本腿判据的 3×filter+info 复算）。
#     腿                        Debug 墙钟              Release 墙钟        取值                                          ⇒ TIMEOUT
#     real_data_vector_union_poly  143.39 / 144.46 /   24.71 / 23.24 /     ceil(148.97 × 4 / 10) × 10 = 59.588→60→600     ⇒ 600 s
#                                  148.97（全组轮）      22.42（全组轮）
#           Debug 余量 600 ÷ 148.97 = 4.03×；Release 余量 600 ÷ 24.71 = 24.3×（同一个字面量，按更慢的 Debug 树定）。
#           ⚠ 取最大值时把**全组轮**的读数也算进来（148.97 是最大现读），但**不要**把它读成
#             "在全组里就更慢"：控制器 12:2x 的分组轮同一命令读到 Debug 143.22 s / Release 23.85 s，
#             Debug 比两次单跑（143.39 / 144.46）还快 ⇒ Debug 五跑散布 143.22~148.97（约 4%）、
#             Release 四跑 22.42~24.71，成因未证；按本仓既有口径"这一栏只现读不外推"处理——
#             上界贴最慢一跑是给余量留空间，不是给某个机制背书。
#           对照：同一条命令里 union 本体 CLI 现读 Debug 135.20 s / Release 17.67 s ⇒ 整腿开销 8~14 s / 5~7 s
#           （Sync-PluginDlls + Ensure-BenchmarkData 命中缓存 + C3f 复制件现读 + 本腿 3×(filter+info) 复算 1.69 s）。
#           ⚠ Release 侧 7.6 倍差是 GEOS 几何运算的量级差，不是"Release 更快一点"：这条腿的判据与
#             分块数在两棵树逐字相同（现读 4/628/9、落盘 641 行、rtree 641 行）。
#   ⚠ 两条新腿的 TIMEOUT 都比公式算出来的整十值再高 10~30 s：定值规则写的是"**至少** 2.5×"，
#     多留的空间是给冷机器与 file cache 空态用的；代价只是挂起时多等几十秒，不是判据放宽。
#   ⚠ 这七个 TIMEOUT 是**余量设计**，不是性能结论：本机同套用例 run-to-run 波动几十秒级
#     （quick 本轮 236.47 s 与同日收口轮的 285.38 s 差 48.91 s）⇒ 这一栏只可现读不可外推，
#     也不许拿它论证某个改动变慢了；定上界取同一条腿的现读值乘 2.5 起。
#   ⚠ 拆分前后对照：原 `real_data_vector_full` 一条现读 378.52 s（780 ÷ 378.52 = 2.06×，跌破触发线），
#     拆出的两半本轮现读 **34.47 + 255.98 = 290.45 s** —— 比整条**少了** 88.07 s，原因是结构性的：
#     两半各造各的派生件，谁也无需再跑另一半那批（原整条腿在一次进程里把 C3a 与 C3b-A 两套派生件
#     全造了一遍）。checks 那条的挂起检测窗口从 780 s 收到 100 s（7.8 倍收紧），overlay 从 780 s
#     收到 670 s；两半各自的上界都更贴自己的现读（2.90× 与 2.62×，而拆分前整条只剩 2.06×），
#     之后再加真实腿只会先把**它自己那一半**的余量压到 2.5× 以下，不会再一脚踩过整条腿的上界。
#   ── C3m 全索引轮的六条腿现读（实测于 2026-10-23，作者＝实现本刀的代理，不是控制器复跑）──
#     为什么这张表要整张重测：本刀把剩下 11 个 .gpkg 写出点也交给驱动建空间索引 ⇒ 六条真实腿写出的
#     每一个产物都多了一张 rtree 表和它的回填行（任务书 §3 点名"本刀会改产物形态，必须重测"）。
#     测法与上面 C3h-3 那栏同形：本机安静态、跑前硬门控 `tasklist | grep -cE 'MSBuild|gis_tests|ctest'` = 0、
#     `ctest --test-dir build/<树> -C <Debug|Release> -R "^real_data" -j1` 串行整组（六条腿共用
#     RESOURCE_LOCK real_data_regression），每棵树两跑，取**整条腿墙钟**（ctest 报的每条用时，含
#     cmake -P 包装自身、Sync-PluginDlls 与派生件生成的开销，不是用例内计时合计）。
#     腿                          Debug 两跑          Release 两跑        最大值    ⇒ 余量（TIMEOUT ÷ 两棵树里更慢的那一跑）
#     real_data_raster            13.25 / 11.02       6.57 / 6.03         13.25     ⇒ 60 ÷ 13.25   = 4.53×
#     real_data_matching          1.86 / 1.69         1.88 / 1.50         1.88      ⇒ 60 ÷ 1.88    = 31.9×
#     real_data_vector_quick      215.28 / 215.32     97.91 / 101.29      215.32    ⇒ 720 ÷ 215.32 = 3.34×
#     real_data_vector_checks     35.40 / 33.39       17.96 / 16.19       35.40     ⇒ 100 ÷ 35.40  = 2.82×
#     real_data_vector_overlay    251.73 / 243.10     103.72 / 127.42     251.73    ⇒ 670 ÷ 251.73 = 2.66×
#     real_data_vector_union_poly 140.20 / 137.28     21.72 / 22.62       140.20    ⇒ 600 ÷ 140.20 = 4.28×
#     两跑全组总墙钟：Debug 657.81 / 641.92 s，Release 249.80 / 275.08 s；十二跑六条腿 100% Passed。
#     ⇒ 六条腿全部仍在 2.5× 触发线之上（最紧的是 overlay 2.66× 与 checks 2.82×）
#       ⇒ **本轮不拆腿，六个 TIMEOUT 一个字都没抬**（抬 TIMEOUT 不是选项，见上面"定值规则"那段第 3 行）。
#     ⚠ 上面 C3k 那三行（236.47 / 34.47 / 255.98 与 Release 126.39/20.20/130.05）和 C3h-3 那一栏
#       （148.97）是**本刀改前的旧现读**，留着只做对照，不要拿它们算余量。
#     ⚠ 不许把 quick 从改前 236.47 读到改后 215.32 当成"补索引之后反而更快"，也不许把 overlay 的
#       Release 两跑（103.72 与 127.42，差 22.8%）读成任何趋势：同一条腿本机散布就是这个量级。
#   ── C3k-2 两条新腿的现读与定值（实测于 2026-10-24，作者＝实现本刀的代理，不是控制器复跑）──
#     为什么拆：2026-10-23 收口轮 real_data_vector_overlay 现读 Debug 273.20 / 265.40 s（取最慢 273.20）
#       ⇒ 670 ÷ 273.20 = 2.45×，跌破本仓 2.5× 的拆腿触发线；规矩是"不许抬 TIMEOUT、拆腿"。
#     拆法（按"重活一分为二"，不是按文件名切）：26 条按 tmp/vector_regression/summary_full_overlay.csv 的
#       逐用例现读（合计 256.93 s）分成 15 条 + 11 条；两组的名单以 .ps1 的 GIS_CASE_ROSTER 行现读回贴。
#     测法：本机安静态、跑前硬门控 `tasklist | grep -cE 'MSBuild|gis_tests|ctest'` = 0、
#       单跑 `ctest --test-dir build/<树> -C <Debug|Release> -R "^real_data_vector_overlay_<组>$" -V`
#       （不带 -j ⇒ 本就串行），取整条腿墙钟 = ctest 报的每条用时（含 cmake -P 包装、Sync-PluginDlls
#       与派生件生成，不是用例内计时合计）。跑后各腿 100% Passed。
#     腿                                    Debug 三跑（含合并轮）        Release 三跑（含合并轮）      取值（两棵树更慢一跑）  ⇒ TIMEOUT（>=4×）
#     real_data_vector_overlay_buffer_join  150.75 / 155.30 / 141.16  116.49 / 122.70 / 106.89  155.30  ⇒ ceil(155.30 × 4 / 10) × 10 = ceil(62.12) × 10 = 630  ⇒ 630 s（630 ÷ 155.30 = 4.06×）
#     real_data_vector_overlay_ops          103.70 / 103.36 / 105.25  13.34 / 13.54 / 14.73     105.25  ⇒ ceil(105.25 × 4 / 10) × 10 = ceil(42.10) × 10 = 430  ⇒ 430 s（430 ÷ 105.25 = 4.09×）
#       合并轮 = `ctest -R "^real_data_vector_overlay_(buffer_join|ops)$"`：两条腿同锁串行、一把跑完。
#       ⚠ 它的读数也算进最大值：ops 的第三次 Debug 跑 105.25 s 比前两跑（103.70 / 103.36）略高 ⇒ 若按前两跑定 420 s
#         余量只有 3.99×（420 ÷ 105.25），恰好跌破 4× 起步口径，所以最终按 105.25 定 430 s。
#     ⚠ Release 侧 ops 13.34 s 与 Debug 103.70 s 差 7.8 倍是 GEOS 几何运算的量级差（与 union_poly 那条腿
#       同形：两棵树的判据与产物逐字相同），不是"Release 更快一点"；TIMEOUT 是对两棵树同时生效的一个字面量，
#       所以按更慢的 Debug 树定。
#     ⚠ 新腿的上界按 Task C2 给"新建真实腿"的 >=4× 起步口径定，2.5× 只是拆腿触发线、不是设计目标；
#       本刀没有抬 quick/checks/union_poly/raster/matching 任何一条的上界。
#     ⚠ 用例数对撞：拆前 `grep -c 'Group "overlay"'` = 26，拆后 = 0；两条新组标 15 + 11 = 26。
#       `grep -c 'New-Case'` 不能当计数（它把 function New-Case 与 New-DerivedCase 内部那两行也算进来 = 23）。
#     ⚠ 名单不变量第 1/2/3 条在两条新腿里都按"四组之并 == full"判绿：C3k-2 时 full 45 条 =
#       checks 18 + overlay_buffer_join 15 + overlay_ops 11 + union_poly 1；C3c-A 加 info_province /
#       info_roads / repair_province 三条后 full 48 = checks 18 + overlay_buffer_join 18 +
#       overlay_ops 11 + union_poly 1；第 6 条（组标 vs union_poly 名单）
#       在两条新腿里照旧判 1 == 1（注册是全量的），第 7 条（跑过数 == 判过数）在两腿里判 0 == 0、
#       在 union_poly 腿里判 1 == 1。
#   ── C3c-A 在 overlay_buffer_join 上新增 info_province / info_roads / repair_province（2026-10-24）──
#     改动：overlay_buffer_join 15 → 18 条，full 45 → 48 条，quick 24 → 27 条；TIMEOUT **不动**（630）。
#     读数（按工单 §3"真实腿若新增，跑一次记读数即可"，只跑了 Debug 一次）：
#       `ctest --test-dir build/debug -C Debug -R "^real_data_vector_overlay_buffer_join$" -V`
#       ⇒ **Passed 188.07 s**（整条腿墙钟，含包装、Sync-PluginDlls 与派生件生成）。
#       用例内计时：info_province 0.44 / info_roads 0.14 / repair_province 1.10；
#       convert_roads_geojson 4.23（输入换成 build/tmp/c3cA/ 复制件并加 info 输入/输出对撞后，
#       原先 0.5 s 级的纯文本判据变成 4.23 s）。
#     余量：630 ÷ 188.07 = 3.35×（> 2.5× 拆腿触发线；C3k-2 时的旧最大读数是 155.30 s，留作对照）。
#     ⚠ 只跑了这一遍（最终版 runner），不把 188.07 与旧的 155.30 读成趋势（同腿散布几十秒级）。
#   ── C3c-B 新增 derive_heavy 一档（simplify 的真实数据腿，2026-10-24）──────────────────
#     改动：overlay_buffer_join 18 → 18（simplify_roads 一度挂进来、实测后挪走），
#       full 48 → 49（checks 18 + overlay_buffer_join 18 + overlay_ops 11 + union_poly 1 + derive_heavy 1），
#       quick 27 → 27；c3cA 那条腿的 TIMEOUT **不动**（630）。
#     读数（`ctest --test-dir build/debug -C Debug -R "^real_data_vector_derive_heavy$" -V`，Debug 三跑）：
#       **85.84 / 86.69 / 84.47 s** ⇒ 取值 ceil(86.69 × 4 / 10) × 10 = ceil(34.676) × 10 = 35 × 10 = **350** ⇒ 350 ÷ 86.69 = 4.04×。
#       用例内计时 simplify_roads 54.22 s（同一条用例在 buffer_join 里那一跑的现读）；其余约 32 s 是
#       包装开销（Sync-PluginDlls + Ensure-C3cA/C3cB 三份复制件与两次 info 现读）。
#     ⚠ Release 侧本刀**没有现读**：Release 树要到收口轮全目标重建才配得上这条新档（CMakeLists 变了 ⇒
#       必须重新 configure）。按 C3h-3 那栏的量级差（同一条腿 Debug 140 s / Release 22 s）估 Release 更快，
#       350 对 Release 是 >=4× 的更宽上界；收口轮现读后回填。
#     为什么单独成档而不是挂进 buffer_join：**实测**挂进去那一跑整腿 250.11 s ⇒ 630 ÷ 250.11 = 2.52×，
#       贴着 2.5× 的拆腿触发线 ⇒ 按"跌破就拆、不抬 TIMEOUT"挪出来单独成条。
# WHY 命令里用 $<TARGET_FILE:gis-cli> 而不写死 build/<树>/… 路径：与既有全部 configure-time 用例同形态，
#   代价也是那条已知事实 —— `ctest -N` 不带 `-C` 时它展开成空串（docs/RELEASE.md 第 1 节有实测原文）。
#   本批按预检裁定 R-C2c **不加兜底**，只在发布前验证清单里点明"这一批也必须带 -C"。
if(GIS_POWERSHELL_EXECUTABLE)
    set(GIS_REAL_DATA_POWERSHELL "${GIS_POWERSHELL_EXECUTABLE}")
else()
    # 空串而不是 ...-NOTFOUND：让包装脚本那条"为空即判红"的检查看到明确的缺失。
    set(GIS_REAL_DATA_POWERSHELL "")
endif()

set(GIS_REAL_DATA_TEST_NAMES
    real_data_raster
    real_data_matching
    real_data_vector_quick
    real_data_vector_checks
    real_data_vector_overlay_buffer_join
    real_data_vector_overlay_ops
    real_data_vector_union_poly
    real_data_vector_derive_heavy
)

add_test(
    NAME real_data_raster
    COMMAND ${CMAKE_COMMAND}
        -DCLI_PATH=$<TARGET_FILE:gis-cli>
        -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
        -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
        -DPOWERSHELL_EXECUTABLE=${GIS_REAL_DATA_POWERSHELL}
        -DMODE=quick
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_raster_regression.cmake
)
set_tests_properties(real_data_raster PROPERTIES
    TIMEOUT 60
    RESOURCE_LOCK real_data_regression
    SKIP_REGULAR_EXPRESSION "GIS_SKIP_TESTDATA_ABSENT: "
)

add_test(
    NAME real_data_matching
    COMMAND ${CMAKE_COMMAND}
        -DCLI_PATH=$<TARGET_FILE:gis-cli>
        -DGUI_TEST_DATA_HELPER_PATH=$<TARGET_FILE:gui_test_data_helper>
        -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
        -DPOWERSHELL_EXECUTABLE=${GIS_REAL_DATA_POWERSHELL}
        -DMODE=quick
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_matching_regression.cmake
)
set_tests_properties(real_data_matching PROPERTIES
    TIMEOUT 60
    RESOURCE_LOCK real_data_regression
    SKIP_REGULAR_EXPRESSION "GIS_SKIP_TESTDATA_ABSENT: "
)

add_test(
    NAME real_data_vector_quick
    COMMAND ${CMAKE_COMMAND}
        -DCLI_PATH=$<TARGET_FILE:gis-cli>
        -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
        -DPOWERSHELL_EXECUTABLE=${GIS_REAL_DATA_POWERSHELL}
        -DMODE=quick
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_vector_regression.cmake
)
set_tests_properties(real_data_vector_quick PROPERTIES
    TIMEOUT 720
    RESOURCE_LOCK real_data_regression
    SKIP_REGULAR_EXPRESSION "GIS_SKIP_TESTDATA_ABSENT: "
)

# ── 批次 C C3k：原 real_data_vector_full 拆成 checks / overlay 两条（C3h-3 再加第三条 union_poly；
#    C3k-2 再把 overlay 一分为二 ⇒ 四条分组腿）──
# WHY 按"消费哪批派生件"拆而不是按条数均分：真实腿的依赖就是这么分的 ——
#   checks = C3b-A 那 11 个动作的 18 条腿 + 只有它们消费的 w212/buf30/prov3857/province_boundary/
#     ring_endpoints（脚本里 Ensure-TopologyCheckInputs 造的那批）；
#   overlay_buffer_join / overlay_ops（C3k-2）= 原 overlay 的 26 条按"重活一分为二"：
#     buffer_quick + spatial_join_focus + 几何派生/转换小用例 + vertices_line 一边，
#     clip/difference/union/intersect 的 focus/far/partial/stress 另一边；
#     两组都消费 roadsCore + allWindow 这批**早于**上面那批就造好的件，外加 C3a 派生 8 条与 C3f 的中文属性腿；
#   union_poly（C3h-3）= 只有 union 路径 A 那 1 条腿，消费 build/tmp/c3h3/ 里的省/市两个复制件。
#   各档只造自己消费的派生件（.ps1 的 -Group），名单对撞的不变量在每一档里都算完整的全套（full +
#   四个分组共五份）⇒ 任何一档单独跑都能判出"档被拆坏了"；细节与判据见 tests/run_real_vector_regression.ps1
#   里 GIS_CASE_ROSTER 那一段。
# WHY 四条分组腿都挂 RESOURCE_LOCK real_data_regression：它们共用 <仓库根>/tmp/vector_benchmark 与
#   tmp/vector_regression（overlay_buffer_join 造的 bj_roads_core_3857 就是 checks 的输入），派生件目录必须串行。
add_test(
    NAME real_data_vector_checks
    COMMAND ${CMAKE_COMMAND}
        -DCLI_PATH=$<TARGET_FILE:gis-cli>
        -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
        -DPOWERSHELL_EXECUTABLE=${GIS_REAL_DATA_POWERSHELL}
        -DMODE=full
        -DGROUP=checks
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_vector_regression.cmake
)
set_tests_properties(real_data_vector_checks PROPERTIES
    TIMEOUT 100
    RESOURCE_LOCK real_data_regression
    SKIP_REGULAR_EXPRESSION "GIS_SKIP_TESTDATA_ABSENT: "
)

# ── 批次 C C3k-2：原 overlay 一档按"重活一分为二"拆成两条 ────────────────────────────
# WHY 拆：2026-10-23 收口轮 overlay 现读 Debug 273.20 / 265.40 s ⇒ 670 ÷ 273.20 = 2.45×，跌破本仓
#   2.5× 的拆腿触发线；规矩是"不许抬 TIMEOUT、拆腿"，所以按逐用例现读把 26 条一分为二：
#   overlay_buffer_join = buffer_quick(64.54) + spatial_join_focus(55.66) + 所有 <=5 s 的几何派生/
#     转换小用例(17.70) + vertices_line(14.63)，15 条现读合计 152.53 s（C3c-A 再加 3 条 ⇒ 18 条）；
#   overlay_ops = clip/difference/union/intersect 的 focus/far/partial/stress，11 条现读合计 104.40 s。
#   两条腿都消费 roadsCore/allWindow 与 C3a 派生件（.ps1 的 $buildOverlayInputs 对两组建真），
#   判据内容一字不动，只挪组标；两半之并由脚本内的名单不变量对撞。
# TIMEOUT 的定值见上面测量表 C3k-2 那一栏（按 >=4× 起算，不是 2.5×）。
add_test(
    NAME real_data_vector_overlay_buffer_join
    COMMAND ${CMAKE_COMMAND}
        -DCLI_PATH=$<TARGET_FILE:gis-cli>
        -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
        -DPOWERSHELL_EXECUTABLE=${GIS_REAL_DATA_POWERSHELL}
        -DMODE=full
        -DGROUP=overlay_buffer_join
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_vector_regression.cmake
)
set_tests_properties(real_data_vector_overlay_buffer_join PROPERTIES
    TIMEOUT 630
    RESOURCE_LOCK real_data_regression
    SKIP_REGULAR_EXPRESSION "GIS_SKIP_TESTDATA_ABSENT: "
)

add_test(
    NAME real_data_vector_overlay_ops
    COMMAND ${CMAKE_COMMAND}
        -DCLI_PATH=$<TARGET_FILE:gis-cli>
        -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
        -DPOWERSHELL_EXECUTABLE=${GIS_REAL_DATA_POWERSHELL}
        -DMODE=full
        -DGROUP=overlay_ops
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_vector_regression.cmake
)
set_tests_properties(real_data_vector_overlay_ops PROPERTIES
    TIMEOUT 430
    RESOURCE_LOCK real_data_regression
    SKIP_REGULAR_EXPRESSION "GIS_SKIP_TESTDATA_ABSENT: "
)

# ── 批次 C C3h-3：union 路径 A（面 × 面）的真实数据腿单独成条 ─────────────────────
# WHY 必须单独成条而不是挂进 real_data_vector_overlay：overlay 那一半现读 Debug 244.04 s / 670 = 2.75×，
#   把这条 union 现读 135 s 级（整腿 200 s 级）的腿塞进去 ⇒ 该半约 378 s ⇒ 670 ÷ 378 = 1.77×，
#   当场跌破本仓 2.5× 的拆腿触发线；而**抬 TIMEOUT 不是选项**（同上那条"TIMEOUT 是唯一挂起执法者"）。
#   也不是挂进 quick：real_data_vector_quick 现读 236.47 s / 720 = 3.05×，加这条会压到 1.94×，同样跌破。
# WHY 分组名 union_poly：与既有 checks/overlay 同形状（.ps1 的 -Group 取值、按"消费哪批派生件"分档），
#   这条腿只消费 build/tmp/c3h3/ 里那两个省市复制件，与 checks/overlay 两套派生件互不相干。
# WHY RESOURCE_LOCK real_data_regression（第三把 vector 腿共用同一把锁）：它与另两条 vector 腿同样
#   写 <仓库根>/tmp/vector_regression（产物 union_poly_focus.gpkg 落在这里）、同样经 Sync-PluginDlls
#   覆盖 gis-cli 旁边的 plugin_*.dll ⇒ 并发跑就是两个进程同时改同一批耗材。
# 门控口径与既有 vector 腿逐字同形（tests/run_real_vector_regression.cmake）：
#   先判"缺解释器 = FATAL_ERROR 红"，再判"缺 data/vector = 打印独占 token GIS_SKIP_TESTDATA_ABSENT: 并 Skip"，
#   顺序反过来会让 fresh clone（data/ 本被 gitignore）+ 无 powershell 的组合记成 Skipped，绕过"恒注册以便判红"。
add_test(
    NAME real_data_vector_union_poly
    COMMAND ${CMAKE_COMMAND}
        -DCLI_PATH=$<TARGET_FILE:gis-cli>
        -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
        -DPOWERSHELL_EXECUTABLE=${GIS_REAL_DATA_POWERSHELL}
        -DMODE=full
        -DGROUP=union_poly
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_vector_regression.cmake
)
set_tests_properties(real_data_vector_union_poly PROPERTIES
    TIMEOUT 600
    RESOURCE_LOCK real_data_regression
    SKIP_REGULAR_EXPRESSION "GIS_SKIP_TESTDATA_ABSENT: "
)

# ── 批次 C C3c-B：单条就几十秒的几何派生重活单独成档（derive_heavy）──────────────────
# WHY 不挂进 real_data_vector_overlay_buffer_join：C3c-A 收口轮那条腿现读 Debug 188.07 s，加上
#   simplify_roads 用例内计时 54.22 s 后**实测整腿 250.11 s**（2026-10-24 现读，见下面测量表）
#   ⇒ 630 ÷ 250.11 = 2.52×，贴着 2.5× 的拆腿触发线；同腿 Debug 散布几十秒级，再有一次就跌破。
# WHY 也不挂进 overlay_ops / quick：overlay_ops 现读 105.25 s ⇒ 加这条 159 s ⇒ 430 ÷ 159 = 2.70×；
#   quick 现读余量 2.81×，同样压不起。抬 TIMEOUT 不是选项（TIMEOUT 是本仓唯一的挂起执法者，
#   PROCESS_TIMEOUT 无效已证），规矩是"新腿按 >=4× 起步、跌破就拆"⇒ 单独成条。
# WHY 分组名 derive_heavy：与 checks/overlay_*/union_poly 同形状按"消费哪批派生件"分档 —— 这条腿
#   只消费 build/tmp/c3cB/ 里那份 roads 复制件，与另三批派生件互不相干。
# RESOURCE_LOCK real_data_regression：与另三条 vector 腿同一把锁（同样写 <仓库根>/tmp/vector_regression、
#   同样经 Sync-PluginDlls 覆盖 gis-cli 旁边的 plugin_*.dll ⇒ 并发跑就是两个进程同时改同一批耗材）。
# 门控口径与既有 vector 腿逐字同形（tests/run_real_vector_regression.cmake）：缺解释器 = 红，缺 data/vector = Skip。
add_test(
    NAME real_data_vector_derive_heavy
    COMMAND ${CMAKE_COMMAND}
        -DCLI_PATH=$<TARGET_FILE:gis-cli>
        -DWORKSPACE_ROOT=${CMAKE_SOURCE_DIR}
        -DPOWERSHELL_EXECUTABLE=${GIS_REAL_DATA_POWERSHELL}
        -DMODE=full
        -DGROUP=derive_heavy
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_real_vector_regression.cmake
)
set_tests_properties(real_data_vector_derive_heavy PROPERTIES
    TIMEOUT 350
    RESOURCE_LOCK real_data_regression
    SKIP_REGULAR_EXPRESSION "GIS_SKIP_TESTDATA_ABSENT: "
)

# ── Task B21 / B17：源码乱码守卫落成用例（用户 2026-10-21 拍板：四规则全带）──────────
# WHY 判据不在 CMake 里写：四规则里 R1（GBK roundtrip）要 CP936 编解码器，CMake 没有编解码器
#   （同族限制："CMake 无 zlib ⇒ 像素级独立判定不可行"）。R2-R4 只比码点，但**不能**因为
#   "CMake 写得出来"就先只落这两条 —— B17 的教训正是规则只有两条就报"归零"，缺一条就是窄规则的 0。
# WHY 注册在 if(GIS_POWERSHELL_EXECUTABLE) 之外：那条 if 只管 real_*_regression 的 custom target。
#   本用例如果也躲进 if 里，解释器找不到的机器上它会**静默不注册**（ctest -N 少一条、CI 照绿），
#   正是本批反复消灭的"没检查长得像检查通过"。所以这里恒注册，解释器缺失由脚本判红。
# 判据本身带自证：先跑 tests/mojibake_fixtures/{bad,ok} —— bad 必须命中全部 4 条规则、
#   ok（真实中文文案）必须 0 命中 ⇒ 规则退化或过宽都会先在这里红，而不是放过仓库里的真乱码。
# 复算：cmake -DPS=<powershell> -DREPO_ROOT=<仓库根> -P tests/check_source_mojibake.cmake
#   （退出码 0 = 干净；非 0 时 stdout 每行形如 "MOJIBAKE <规则> <相对路径>:<行号>"）
if(GIS_POWERSHELL_EXECUTABLE)
    set(_gis_mojibake_ps "${GIS_POWERSHELL_EXECUTABLE}")
else()
    # 空串而不是 ...-NOTFOUND：让脚本那侧的"为空即拒绝放行"判据能读到明确的缺失。
    set(_gis_mojibake_ps "")
endif()
add_test(
    NAME source_mojibake_guard
    COMMAND ${CMAKE_COMMAND}
        -DPS=${_gis_mojibake_ps}
        -DREPO_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_source_mojibake.cmake
)
# Hard timeout for every test registered at configure time: a hanging test must fail, not
# hang the pipeline. TESTS is the documented read-only directory property that lists the
# tests add_test() has defined in this directory so far, so reading it at the very bottom
# covers every configure-time case, including the ones expanded by the foreach loops.
# The gtest cases found by gtest_discover_tests are not in it - PRE_TEST discovery happens
# at ctest time - they get TIMEOUT through PROPERTIES instead. Both legs read the same
# GIS_TEST_TIMEOUT set above, so the two cannot drift apart.
get_property(GIS_TIMED_TEST_NAMES DIRECTORY PROPERTY TESTS)
# gui_smoke_startup already has the tighter 60s bound set above; the blanket value must not
# overwrite it, so it is excluded here instead of being re-set afterwards.
# GIS_REAL_DATA_TEST_NAMES（Task C2 / N112 那批 real_data_*，C3k 拆腿后 5 条、C3h-3 再加 union_poly
#   那条腿、C3k-2 再拆 overlay 之后 7 条、C3c-B 再加 derive_heavy 之后 8 条）是同一个坑的反方向：它们按
#   "实测墙钟 × 余量倍数"算出的上界**高于** 300（vector 那条最重的腿现读 Debug 265.29 s ⇒ 300 ÷ 265.29 = 1.13×），
#   blanket 会把那几个写好的数悄悄改回 300 ⇒ 注释里的算式当场作废、上界失真。名单走变量
#   GIS_REAL_DATA_TEST_NAMES 传进来，不在这里手抄第二遍名字（两处各改各的正是本文件反复在防的漂移）。
#   复算（现值 60/60/720/100/630/430/600/350，按上面名单里 real_data_* 的书写顺序）。⚠ 必须读**生成物**而不是读源码：源码侧那条
#     grep -A3 "set_tests_properties(real_data" tests/CMakeLists.txt | grep TIMEOUT
#   即使有人把豁免名单 GIS_REAL_DATA_TEST_NAMES 里的名字删掉、blanket 把值改回 300，它照样打印那几个数。
#     grep 'TIMEOUT "300"' build/debug/tests/CTestTestfile.cmake | grep -c real_data   ⇒ 必须为 0
list(REMOVE_ITEM GIS_TIMED_TEST_NAMES gui_smoke_startup ${GIS_REAL_DATA_TEST_NAMES})
# The guard exists only because set_property(TEST) with no names is a configure error.
# Note the premise this used to state ("every configure-time case in this file is registered
# inside the if(TARGET gis-gui) block above") is FALSE, and has been since before this round:
# param_guard_visible_keys_all_read is deliberately registered outside that block (see the WHY
# comment there), and GuiContract.ExportActionVisibleKeys now is too (it needs a RESOURCE_LOCK,
# which the discovery-side PROPERTIES cannot express per-test). So an empty GIS_TIMED_TEST_NAMES
# would NOT prove the gis-gui block was skipped - the non-empty check only guards against this
# file registering nothing at all. Conclusion unchanged ("nothing is silently skipped here"),
# argument corrected. Do not move this block into if(TARGET gis-gui): these two cases would
# lose their outer TIMEOUT.
if(GIS_TIMED_TEST_NAMES)
    set_property(TEST ${GIS_TIMED_TEST_NAMES} PROPERTY TIMEOUT ${GIS_TEST_TIMEOUT})
endif()
