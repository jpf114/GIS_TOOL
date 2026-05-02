find_package(GTest REQUIRED)

add_executable(gis_tests
    test_main.cpp
    test_core.cpp
    test_framework.cpp
    test_plugins.cpp
    test_gui_support.cpp
    test_support.cpp
    ../src/gui/custom_index_preset_store.cpp
    ../src/gui/gui_data_support.cpp
)

target_link_libraries(gis_tests PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
    opencv_core
    opencv_imgproc
    Qt6::Core
    GTest::gtest
)

if(MSVC)
    target_compile_options(gis_tests PRIVATE /utf-8)
endif()

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
    gui_test_data_helper
    plugin_test_lifecycle
    plugin_projection
    plugin_cutting
    plugin_matching
    plugin_processing
    plugin_raster_math
    plugin_raster_inspect
    plugin_raster_manage
    plugin_raster_render
    plugin_georef
    plugin_terrain
    plugin_classification
    plugin_spindex
    plugin_vector
)

add_custom_command(TARGET gis_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_test_lifecycle>
        $<TARGET_FILE_DIR:gis_tests>/$<TARGET_FILE_NAME:plugin_test_lifecycle>
)

# Copy vcpkg runtime DLLs next to test executable so plugin DLLs can find dependencies
add_custom_command(TARGET gis_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${GIS_VCPKG_RUNTIME_BIN_DIR}
        $<TARGET_FILE_DIR:gis_tests>
)

include(GoogleTest)
gtest_discover_tests(gis_tests DISCOVERY_MODE PRE_TEST)

if(TARGET gis-gui)
    add_test(
        NAME gui_smoke_startup
        COMMAND $<TARGET_FILE:gis-gui> -platform offscreen --self-test
    )
    set_tests_properties(gui_smoke_startup PROPERTIES
        PASS_REGULAR_EXPRESSION ""
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
        NAME gui_vector_filter_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/data/gui_vector_input.geojson
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/vector_filter/output.geojson
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/vector_filter/gui_vector_filter.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_vector_filter_regression.cmake
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
        NAME gui_spindex_custom_index_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_custom_index/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/spindex_custom_index/gui_spindex_custom_index.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_spindex_custom_index_regression.cmake
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
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_ecc_register_debug_failure/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_ecc_register_debug_failure/gui_matching_ecc_register_debug_failure.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_matching_ecc_register_debug_failure_regression.cmake
    )

    add_test(
        NAME gui_matching_stitch_debug_failure_offscreen
        COMMAND ${CMAKE_COMMAND}
            -DGUI_PATH=$<TARGET_FILE:gis-gui>
            -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_stitch_debug_failure/output.tif
            -DSCREENSHOT_PATH=${CMAKE_CURRENT_BINARY_DIR}/gui_regression/nested/matching_stitch_debug_failure/gui_matching_stitch_debug_failure.png
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_gui_matching_stitch_debug_failure_regression.cmake
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
endif()

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
        DEPENDS gis-cli gui_test_data_helper plugin_processing plugin_classification plugin_spindex plugin_raster_math plugin_raster_inspect plugin_raster_manage plugin_raster_render plugin_terrain copy_runtime_dlls
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
        DEPENDS gis-cli gui_test_data_helper plugin_processing plugin_classification plugin_spindex plugin_raster_math plugin_raster_inspect plugin_raster_manage plugin_raster_render plugin_terrain copy_runtime_dlls
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
