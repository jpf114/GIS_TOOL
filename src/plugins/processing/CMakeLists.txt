add_library(plugin_processing SHARED
    processing_plugin.cpp
)

target_include_directories(plugin_processing PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plugin_processing PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
    OpenCV::Core
    OpenCV::ImgProc
)

set_target_properties(plugin_processing PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_processing"
)

add_custom_command(TARGET plugin_processing POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_processing>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_processing>
)