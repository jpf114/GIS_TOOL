add_library(plugin_cutting SHARED
    cutting_plugin.cpp
)

target_include_directories(plugin_cutting PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plugin_cutting PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
)

set_target_properties(plugin_cutting PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_cutting"
)

add_custom_command(TARGET plugin_cutting POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_cutting>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_cutting>
)

install(TARGETS plugin_cutting
    RUNTIME DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
    LIBRARY DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
)
