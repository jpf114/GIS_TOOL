add_library(plugin_terrain SHARED
    terrain_plugin.cpp
)

target_include_directories(plugin_terrain PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plugin_terrain PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
    opencv_core
    opencv_imgproc
)

set_target_properties(plugin_terrain PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_terrain"
)

add_custom_command(TARGET plugin_terrain POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_terrain>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_terrain>
)

install(TARGETS plugin_terrain
    RUNTIME DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
    LIBRARY DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
)
