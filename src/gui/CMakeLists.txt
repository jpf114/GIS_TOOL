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
    result_preview_page.cpp
    raster_preview_widget.cpp
    vector_preview_widget.cpp
    stats_preview_widget.cpp
    workflow_page.cpp
    welcome_dialog.cpp
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
set(GIS_QT_WINDOWS_PDB "$<$<CONFIG:Debug>:qwindowsd.pdb>$<$<NOT:$<CONFIG:Debug>>:qwindows.pdb>")

set(GIS_QT_HEADLESS_PLATFORM "offscreen")
if(EXISTS "${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}/qminimald.dll" OR
   EXISTS "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/qminimal.dll")
    set(GIS_QT_HEADLESS_PLATFORM "minimal")
endif()
if(EXISTS "${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}/qoffscreend.dll" OR
   EXISTS "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/qoffscreen.dll")
    set(GIS_QT_HEADLESS_PLATFORM "offscreen")
endif()

set(GIS_QT_HEADLESS_DLL "$<$<CONFIG:Debug>:q${GIS_QT_HEADLESS_PLATFORM}d.dll>$<$<NOT:$<CONFIG:Debug>>:q${GIS_QT_HEADLESS_PLATFORM}.dll>")
set(GIS_QT_HEADLESS_PDB "$<$<CONFIG:Debug>:q${GIS_QT_HEADLESS_PLATFORM}d.pdb>$<$<NOT:$<CONFIG:Debug>>:q${GIS_QT_HEADLESS_PLATFORM}.pdb>")
set(GIS_COPY_QT_PLATFORM_DLL OFF)
set(GIS_COPY_QT_PLATFORM_PDB OFF)

if(EXISTS "${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}/qwindowsd.dll" OR
   EXISTS "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/qwindows.dll")
    set(GIS_COPY_QT_PLATFORM_DLL ON)
endif()
if(EXISTS "${GIS_VCPKG_DEBUG_QT_PLATFORMS_DIR}/qwindowsd.pdb" OR
   EXISTS "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/qwindows.pdb")
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

gis_copy_plugins_to_target_dir(gis-gui $<TARGET_FILE_DIR:gis-gui>/plugins)

if(GIS_COPY_QT_PLATFORM_DLL)
    add_custom_command(TARGET gis-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D_SRC="${GIS_VCPKG_QT_PLATFORMS_DIR}/${GIS_QT_WINDOWS_DLL}"
            -D_DST="$<TARGET_FILE_DIR:gis-gui>/platforms/${GIS_QT_WINDOWS_DLL}"
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
        COMMAND ${CMAKE_COMMAND}
            -D_SRC="${GIS_VCPKG_QT_PLATFORMS_DIR}/${GIS_QT_HEADLESS_DLL}"
            -D_DST="$<TARGET_FILE_DIR:gis-gui>/platforms/${GIS_QT_HEADLESS_DLL}"
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
        COMMAND_EXPAND_LISTS
    )
else()
    message(WARNING "在 manifest vcpkg 安装目录中未找到 Qt 平台插件 DLL，GUI 运行时可能缺少 qwindows。")
endif()

if(GIS_COPY_QT_PLATFORM_PDB)
    add_custom_command(TARGET gis-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D_SRC="${GIS_VCPKG_QT_PLATFORMS_DIR}/${GIS_QT_WINDOWS_PDB}"
            -D_DST="$<TARGET_FILE_DIR:gis-gui>/platforms/${GIS_QT_WINDOWS_PDB}"
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
        COMMAND ${CMAKE_COMMAND}
            -D_SRC="${GIS_VCPKG_QT_PLATFORMS_DIR}/${GIS_QT_HEADLESS_PDB}"
            -D_DST="$<TARGET_FILE_DIR:gis-gui>/platforms/${GIS_QT_HEADLESS_PDB}"
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
        COMMAND_EXPAND_LISTS
    )
endif()

set(GIS_VCPKG_QT_SQLDRIVERS_DIR "$<$<CONFIG:Debug>:${GIS_VCPKG_DEBUG_QT_DIR}/plugins/sqldrivers>$<$<NOT:$<CONFIG:Debug>>:${GIS_VCPKG_RELEASE_QT_DIR}/plugins/sqldrivers>")
set(GIS_QT_SQLITE_DLL "$<$<CONFIG:Debug>:qsqlited.dll>$<$<NOT:$<CONFIG:Debug>>:qsqlite.dll>")

add_custom_command(TARGET gis-gui POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D_SRC="${GIS_VCPKG_QT_SQLDRIVERS_DIR}/${GIS_QT_SQLITE_DLL}"
            -D_DST="$<TARGET_FILE_DIR:gis-gui>/sqldrivers/${GIS_QT_SQLITE_DLL}"
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
        COMMAND_EXPAND_LISTS
)

install(TARGETS gis-gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(GIS_COPY_QT_PLATFORM_DLL)
    install(FILES
        "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/qwindows.dll"
        CONFIGURATIONS Release
        DESTINATION ${GIS_INSTALL_PLATFORM_DIR}
    )

    if(EXISTS "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/q${GIS_QT_HEADLESS_PLATFORM}.dll")
        install(FILES
            "${GIS_VCPKG_RELEASE_QT_PLATFORMS_DIR}/q${GIS_QT_HEADLESS_PLATFORM}.dll"
            CONFIGURATIONS Release
            DESTINATION ${GIS_INSTALL_PLATFORM_DIR}
        )
    endif()
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
