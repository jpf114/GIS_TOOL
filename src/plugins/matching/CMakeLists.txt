add_library(plugin_matching SHARED
    matching_plugin.cpp
)

target_include_directories(plugin_matching PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plugin_matching PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
    opencv_core
    opencv_features2d
    opencv_imgproc
    opencv_flann
    opencv_calib3d
)

set_target_properties(plugin_matching PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_matching"
)

add_custom_command(TARGET plugin_matching POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_matching>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_matching>
)