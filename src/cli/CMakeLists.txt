add_executable(gis-cli
    main.cpp
    cli_parser.cpp
)

set(GIS_PLUGIN_TARGETS
    plugin_projection
    plugin_cutting
    plugin_matching
    plugin_processing
    plugin_classification
    plugin_spindex
    plugin_utility
    plugin_vector
)

add_dependencies(gis-cli ${GIS_PLUGIN_TARGETS})

target_link_libraries(gis-cli PRIVATE
    gis_framework
    gis_core
)

add_custom_command(TARGET gis-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${GIS_VCPKG_RUNTIME_BIN_DIR}
        $<TARGET_FILE_DIR:gis-cli>
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:gis-cli>/plugins
    COMMAND_EXPAND_LISTS
)

foreach(GIS_PLUGIN_TARGET IN LISTS GIS_PLUGIN_TARGETS)
    add_custom_command(TARGET gis-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${GIS_PLUGIN_TARGET}>
            $<TARGET_FILE_DIR:gis-cli>/plugins/$<TARGET_FILE_NAME:${GIS_PLUGIN_TARGET}>
    )
endforeach()

install(TARGETS gis-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
