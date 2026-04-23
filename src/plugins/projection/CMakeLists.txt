add_library(plugin_projection SHARED
    projection_plugin.cpp
)

target_include_directories(plugin_projection PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plugin_projection PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
)

set_target_properties(plugin_projection PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_projection"
)

add_custom_command(TARGET plugin_projection POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_projection>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_projection>
)
