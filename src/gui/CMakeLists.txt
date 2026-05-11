cmake_minimum_required(VERSION 3.20)

# gis-gui executable target
add_executable(gis-gui
    main.cpp
    custom_index_preset_store.cpp
    gui_data_support.cpp
    gdal_config.cpp
    mainwindow.cpp
    nav_panel.cpp
    param_card_widget.cpp
    param_widget.cpp
    progress_dialog.cpp
    crs_dialog.cpp
    execute_worker.cpp
    qt_progress_reporter.cpp
    icon_manager.cpp
    settings_manager.cpp
    task_manager.cpp
    task_database.cpp
    task_center_page.cpp
    task_runner.cpp
)

set(GIS_PLUGIN_TARGETS
    plugin_projection
    plugin_cutting
    plugin_matching
    plugin_processing
    plugin_raster_math
    plugin_raster_inspect
    plugin_raster_manage
    plugin_raster_render
    plugin_georef
    plugin_terrain
    plugin_classification
    plugin_spindex
    plugin_vector
)

add_dependencies(gis-gui ${GIS_PLUGIN_TARGETS})

# Link against our framework and Qt
target_link_libraries(gis-gui PRIVATE
    gis_framework
    gis_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::Sql
    unofficial::sqlite3::sqlite3
    PROJ::proj
)

# Enable AUTOMOC for signal/slot processing
set_target_properties(gis-gui PROPERTIES
    AUTOMOC ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

set(GIS_QT_WINDOWS_DLL "$<$<CONFIG:Debug>:qwindowsd.dll>$<$<NOT:$<CONFIG:Debug>>:qwindows.dll>")
set(GIS_QT_OFFSCREEN_DLL "$<$<CONFIG:Debug>:qoffscreend.dll>$<$<NOT:$<CONFIG:Debug>>:qoffscreen.dll>")
set(GIS_QT_WINDOWS_PDB "$<$<CONFIG:Debug>:qwindowsd.pdb>$<$<NOT:$<CONFIG:Debug>>:qwindows.pdb>")
set(GIS_QT_OFFSCREEN_PDB "$<$<CONFIG:Debug>:qoffscreend.pdb>$<$<NOT:$<CONFIG:Debug>>:qoffscreen.pdb>")
set(GIS_COPY_QT_PLATFORM_DLL OFF)
set(GIS_COPY_QT_PLATFORM_PDB OFF)

if(EXISTS "${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}/qwindowsd.dll" OR
   EXISTS "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/qwindows.dll" OR
   EXISTS "${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}/qoffscreend.dll" OR
   EXISTS "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/qoffscreen.dll")
    set(GIS_COPY_QT_PLATFORM_DLL ON)
endif()

if(EXISTS "${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}/qwindowsd.pdb" OR
   EXISTS "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/qwindows.pdb" OR
   EXISTS "${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}/qoffscreend.pdb" OR
   EXISTS "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/qoffscreen.pdb")
    set(GIS_COPY_QT_PLATFORM_PDB ON)
endif()

# Copy plugins directory next to executable
add_custom_command(TARGET gis-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:gis-gui>/platforms
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:gis-gui>/plugins
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:gis-gui>/sqldrivers
)

gis_copy_minimal_runtime(gis-gui)

foreach(GIS_PLUGIN_TARGET IN LISTS GIS_PLUGIN_TARGETS)
    add_custom_command(TARGET gis-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${GIS_PLUGIN_TARGET}>
            $<TARGET_FILE_DIR:gis-gui>/plugins/$<TARGET_FILE_NAME:${GIS_PLUGIN_TARGET}>
    )
endforeach()

if(GIS_COPY_QT_PLATFORM_DLL)
    add_custom_command(TARGET gis-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${GIS_VCPKG_QT_PLATFORMS_DIR}/${GIS_QT_WINDOWS_DLL}
            $<TARGET_FILE_DIR:gis-gui>/platforms/${GIS_QT_WINDOWS_DLL}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${GIS_VCPKG_QT_PLATFORMS_DIR}/${GIS_QT_OFFSCREEN_DLL}
            $<TARGET_FILE_DIR:gis-gui>/platforms/${GIS_QT_OFFSCREEN_DLL}
        COMMAND_EXPAND_LISTS
    )
else()
    message(WARNING "未在全局 vcpkg 中找到 Qt 平台插件 DLL，构建将跳过 qwindows 插件拷贝。")
endif()

if(GIS_COPY_QT_PLATFORM_PDB)
    add_custom_command(TARGET gis-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${GIS_VCPKG_QT_PLATFORMS_DIR}/${GIS_QT_WINDOWS_PDB}
            $<TARGET_FILE_DIR:gis-gui>/platforms/${GIS_QT_WINDOWS_PDB}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${GIS_VCPKG_QT_PLATFORMS_DIR}/${GIS_QT_OFFSCREEN_PDB}
            $<TARGET_FILE_DIR:gis-gui>/platforms/${GIS_QT_OFFSCREEN_PDB}
        COMMAND_EXPAND_LISTS
    )
endif()

set(GIS_VCPKG_QT_SQLDRIVERS_DIR "$<$<CONFIG:Debug>:${GIS_VCPKG_DEBUG_QT_DIR}/plugins/sqldrivers>$<$<NOT:$<CONFIG:Debug>>:${GIS_VCPKG_RELEASE_QT_DIR}/plugins/sqldrivers>")
set(GIS_QT_SQLITE_DLL "$<$<CONFIG:Debug>:qsqlited.dll>$<$<NOT:$<CONFIG:Debug>>:qsqlite.dll>")

add_custom_command(TARGET gis-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${GIS_VCPKG_QT_SQLDRIVERS_DIR}/${GIS_QT_SQLITE_DLL}
        $<TARGET_FILE_DIR:gis-gui>/sqldrivers/${GIS_QT_SQLITE_DLL}
    COMMAND_EXPAND_LISTS
)

install(TARGETS gis-gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(GIS_COPY_QT_PLATFORM_DLL)
    install(FILES
        "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/qwindows.dll"
        "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/qoffscreen.dll"
        CONFIGURATIONS Release
        DESTINATION ${GIS_INSTALL_PLATFORM_DIR}
    )
endif()

set(GIS_VCPKG_RELEASE_QT_SQLDRIVERS_DIR "${GIS_VCPKG_INSTALLED_DIR}/Qt6/plugins/sqldrivers")

install(FILES
    "${GIS_VCPKG_RELEASE_QT_SQLDRIVERS_DIR}/qsqlite.dll"
    CONFIGURATIONS Release
    DESTINATION ${GIS_INSTALL_SQLDRIVERS_DIR}
)

add_custom_command(TARGET gis-gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/icons"
        "$<TARGET_FILE_DIR:gis-gui>/icons"
    COMMENT "Copying icon resources"
)
