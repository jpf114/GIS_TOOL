add_library(plugin_georef SHARED
    georef_plugin.cpp
)

target_include_directories(plugin_georef PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plugin_georef PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
    opencv_core
)

set_target_properties(plugin_georef PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_georef"
)

add_custom_command(TARGET plugin_georef POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_georef>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_georef>
)

install(TARGETS plugin_georef
    RUNTIME DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
    LIBRARY DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
)
