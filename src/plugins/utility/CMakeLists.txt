add_library(plugin_utility SHARED
    utility_plugin.cpp
)

target_include_directories(plugin_utility PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plugin_utility PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
    opencv_core
    opencv_imgproc
)

set_target_properties(plugin_utility PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_utility"
)

add_custom_command(TARGET plugin_utility POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_utility>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_utility>
)