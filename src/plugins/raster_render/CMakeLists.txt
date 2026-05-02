add_library(plugin_raster_render SHARED
    raster_render_plugin.cpp
)

target_include_directories(plugin_raster_render PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${CMAKE_SOURCE_DIR}/src/framework/include
)

target_link_libraries(plugin_raster_render PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
)

if(TARGET opencv_world)
    target_link_libraries(plugin_raster_render PRIVATE opencv_world)
endif()

set_target_properties(plugin_raster_render PROPERTIES
    CXX_STANDARD 17
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
