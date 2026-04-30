add_library(plugin_classification SHARED
    classification_plugin.cpp
)

target_include_directories(plugin_classification PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plugin_classification PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
)

set_target_properties(plugin_classification PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_classification"
)

add_custom_command(TARGET plugin_classification POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_classification>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_classification>
)

install(TARGETS plugin_classification
    RUNTIME DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
    LIBRARY DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
)
