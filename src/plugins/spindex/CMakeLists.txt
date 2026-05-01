add_library(plugin_spindex SHARED
    spindex_plugin.cpp
)

target_include_directories(plugin_spindex PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plugin_spindex PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
    opencv_core
    opencv_imgproc
)

set_target_properties(plugin_spindex PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_spindex"
)

add_custom_command(TARGET plugin_spindex POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_spindex>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_spindex>
)

install(TARGETS plugin_spindex
    RUNTIME DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
    LIBRARY DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
)
