add_library(plugin_raster_math SHARED
    raster_math_plugin.cpp
)

target_include_directories(plugin_raster_math PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plugin_raster_math PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
    opencv_core
    opencv_imgproc
)

set_target_properties(plugin_raster_math PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_raster_math"
)

add_custom_command(TARGET plugin_raster_math POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_raster_math>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_raster_math>
)

install(TARGETS plugin_raster_math
    RUNTIME DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
    LIBRARY DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
)
