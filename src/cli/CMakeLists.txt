add_executable(gis-cli
    main.cpp
    cli_parser.cpp
)

add_dependencies(gis-cli ${GIS_PLUGIN_TARGETS})

target_link_libraries(gis-cli PRIVATE
    gis_framework
    gis_core
)

add_custom_command(TARGET gis-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:gis-cli>/plugins
)

gis_copy_minimal_runtime(gis-cli)
gis_copy_minimal_runtime(gis-cli DEST_DIR $<TARGET_FILE_DIR:gis-cli>/plugins)

gis_copy_plugins_to_target_dir(gis-cli $<TARGET_FILE_DIR:gis-cli>/plugins)

install(TARGETS gis-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
