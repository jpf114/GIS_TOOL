add_library(plugin_vector SHARED
    vector_plugin.cpp
)

target_include_directories(plugin_vector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plugin_vector PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
)

set_target_properties(plugin_vector PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_vector"
)

add_custom_command(TARGET plugin_vector POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_vector>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_vector>
)

install(TARGETS plugin_vector
    RUNTIME DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
    LIBRARY DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
)
