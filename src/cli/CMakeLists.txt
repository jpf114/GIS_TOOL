add_executable(gis-cli
    main.cpp
    cli_parser.cpp
)

target_link_libraries(gis-cli PRIVATE
    gis_framework
    gis_core
)

add_custom_command(TARGET gis-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/plugins
        $<TARGET_FILE_DIR:gis-cli>/plugins
    COMMAND_EXPAND_LISTS
)