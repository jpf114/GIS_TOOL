add_library(plugin_raster_manage SHARED
    raster_manage_plugin.cpp
)

target_include_directories(plugin_raster_manage PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${CMAKE_SOURCE_DIR}/src/framework/include
)

target_link_libraries(plugin_raster_manage PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
)

set_target_properties(plugin_raster_manage PROPERTIES
    CXX_STANDARD 17
    OUTPUT_NAME "plugin_raster_manage"
)

add_custom_command(TARGET plugin_raster_manage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_raster_manage>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_raster_manage>
)

install(TARGETS plugin_raster_manage
    RUNTIME DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
    LIBRARY DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
    ARCHIVE DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
)
