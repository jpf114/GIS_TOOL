add_library(plugin_raster_render SHARED
    raster_render_plugin.cpp
)

target_include_directories(plugin_raster_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plugin_raster_render PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
    opencv_core
    opencv_imgproc
)

set_target_properties(plugin_raster_render PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_raster_render"
)

add_custom_command(TARGET plugin_raster_render POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_raster_render>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_raster_render>
)

install(TARGETS plugin_raster_render
    RUNTIME DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
    LIBRARY DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
)
