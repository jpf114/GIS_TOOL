add_library(plugin_feature_stats SHARED
    feature_stats_plugin.cpp
)

target_include_directories(plugin_feature_stats PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(plugin_feature_stats PRIVATE
    gis_core
    gis_framework
    GDAL::GDAL
)

set_target_properties(plugin_feature_stats PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin_feature_stats"
)

add_custom_command(TARGET plugin_feature_stats POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:plugin_feature_stats>
        ${CMAKE_BINARY_DIR}/plugins/$<TARGET_FILE_NAME:plugin_feature_stats>
)

install(TARGETS plugin_feature_stats
    RUNTIME DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
    LIBRARY DESTINATION ${GIS_INSTALL_PLUGIN_DIR}
)

