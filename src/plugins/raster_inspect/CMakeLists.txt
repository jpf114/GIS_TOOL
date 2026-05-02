add_library(plugin_raster_inspect SHARED
    raster_inspect_plugin.cpp
)

target_include_directories(plugin_raster_inspect PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/include
    ${CMAKE_SOURCE_DIR}/src/framework/include
)

target_link_libraries(plugin_raster_inspect PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
)

if(TARGET opencv_world)
    target_link_libraries(plugin_raster_inspect PRIVATE opencv_world)
endif()

set_target_properties(plugin_raster_inspect PROPERTIES
    CXX_STANDARD 17
    OUTPUT_NAME "plugin_raster_inspect"
)

add_custom_command(TARGET plugin_raster_inspect POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_raster_inspect>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_raster_inspect>
)

install(TARGETS plugin_raster_inspect
    RUNTIME DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
    LIBRARY DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
)
