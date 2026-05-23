set(GIS_RUNTIME_DLLS
    double-conversion.dll
    gdal.dll
    geos.dll
    geos_c.dll
    geotiff.dll
    jpeg62.dll
    json-c.dll
    libexpat.dll
    liblzma.dll
    libpng16.dll
    md4c.dll
    md4c-html.dll
    opencv_calib3d4.dll
    opencv_core4.dll
    opencv_features2d4.dll
    opencv_flann4.dll
    opencv_imgcodecs4.dll
    opencv_imgproc4.dll
    opencv_ml4.dll
    opencv_stitching4.dll
    opencv_video4.dll
    pcre2-16.dll
    pcre2-32.dll
    pcre2-8.dll
    pcre2-posix.dll
    proj_9.dll
    Qt6Core.dll
    Qt6Gui.dll
    Qt6Sql.dll
    Qt6Svg.dll
    Qt6Widgets.dll
    Qt6Xml.dll
    sqlite3.dll
    tiff.dll
    turbojpeg.dll
    z.dll
)

set(GIS_RUNTIME_DLLS_DEBUG
    double-conversion.dll
    gdald.dll
    geos.dll
    geos_c.dll
    geotiff_d.dll
    jpeg62.dll
    json-c.dll
    libexpatd.dll
    liblzma.dll
    libpng16d.dll
    md4c.dll
    md4c-html.dll
    opencv_calib3d4d.dll
    opencv_core4d.dll
    opencv_features2d4d.dll
    opencv_flann4d.dll
    opencv_imgcodecs4d.dll
    opencv_imgproc4d.dll
    opencv_ml4d.dll
    opencv_stitching4d.dll
    opencv_video4d.dll
    pcre2-16d.dll
    pcre2-32d.dll
    pcre2-8d.dll
    pcre2-posixd.dll
    proj_9_d.dll
    Qt6Cored.dll
    Qt6Guid.dll
    Qt6Sqld.dll
    Qt6Svgd.dll
    Qt6Widgetsd.dll
    Qt6Xmld.dll
    sqlite3.dll
    tiffd.dll
    turbojpeg.dll
    zd.dll
)

function(gis_copy_minimal_runtime target_name)
    cmake_parse_arguments(ARG "" "DEST_DIR" "" ${ARGN})

    if(ARG_DEST_DIR)
        set(_dest "${ARG_DEST_DIR}")
    else()
        set(_dest "$<TARGET_FILE_DIR:${target_name}>")
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_dest}"
    )

    foreach(_dll IN LISTS GIS_RUNTIME_DLLS)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -D_SRC="${GIS_VCPKG_RELEASE_BIN_DIR}/${_dll}"
                -D_DST="${_dest}/${_dll}"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
        )
    endforeach()

    foreach(_dll IN LISTS GIS_RUNTIME_DLLS_DEBUG)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -D_SRC="${GIS_VCPKG_DEBUG_BIN_DIR}/${_dll}"
                -D_DST="${_dest}/${_dll}"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
        )
    endforeach()
endfunction()
