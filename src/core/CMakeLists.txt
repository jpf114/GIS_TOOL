add_library(gis_core
    src/types.cpp
    src/progress.cpp
    src/gdal_wrapper.cpp
    src/opencv_wrapper.cpp
    src/runtime_env.cpp
    src/spindex_presets.cpp
)

target_include_directories(gis_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(gis_core PUBLIC
    GDAL::GDAL
    opencv_core
    opencv_imgproc
)

set_target_properties(gis_core PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

install(TARGETS gis_core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
