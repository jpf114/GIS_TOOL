cmake_minimum_required(VERSION 3.20)

# gis-gui executable target
add_executable(gis-gui
    main.cpp
    mainwindow.cpp
    param_widget.cpp
    progress_dialog.cpp
    crs_dialog.cpp
    execute_worker.cpp
    qt_progress_reporter.cpp
)

# Link against our framework and Qt
target_link_libraries(gis-gui PRIVATE
    gis_framework
    gis_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    PROJ::proj
)

# Enable AUTOMOC for signal/slot processing
set_target_properties(gis-gui PROPERTIES
    AUTOMOC ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Copy plugins directory next to executable (same as CLI)
add_custom_command(TARGET gis-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${GIS_VCPKG_RUNTIME_BIN_DIR}
        $<TARGET_FILE_DIR:gis-gui>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/plugins
        $<TARGET_FILE_DIR:gis-gui>/plugins
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:gis-gui>/platforms
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${GIS_VCPKG_QT_PLATFORMS_DIR}/qwindows.dll
        $<TARGET_FILE_DIR:gis-gui>/platforms/qwindows.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${GIS_VCPKG_QT_PLATFORMS_DIR}/qwindows.pdb
        $<TARGET_FILE_DIR:gis-gui>/platforms/qwindows.pdb
    COMMAND_EXPAND_LISTS
)
