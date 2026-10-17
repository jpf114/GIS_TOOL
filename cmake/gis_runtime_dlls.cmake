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

    # 源目录必须**按配置二选一**，不能"release 目录拷一轮、debug 目录再拷一轮"（Task 18 修复轮 F1）。
    # WHY：libjpeg-turbo 端口的 debug 与 release DLL **同名**（jpeg62.dll / turbojpeg.dll，没有 "d" 后缀），
    #   改前两个 foreach 都无条件执行 ⇒ Release 构树里后跑的 debug 轮把 release 那份**原地覆盖**，
    #   于是 /MD 版 opencv_imgcodecs4.dll 与 gdal.dll 直接调用 /MDd 版 libjpeg
    #   （跨 CRT 传 FILE*、跨 CRT malloc/free）⇒ 未定义行为：Release 下 cv::imwrite(".jpg") 永不返回
    #   （挂死取证 build/t18f1_1_release_iso.log，仅把 jpeg62.dll 换成 release 那份就 0.67s 过：
    #   build/t18f1_3_release_swappedjpeg.log；同一 UB 的另一表现是 0xc0000409 fastfail）。
    #   Debug 构树两份都是 debug ⇒ 自洽 ⇒ 只在 Release 侧暴露，所以这条缺陷在 17 轮任务里从未被踩到。
    # HOW：统一走根 CMakeLists.txt 已经算好、但本函数此前没用到的 GIS_VCPKG_RUNTIME_BIN_DIR
    #   （$<CONFIG:Debug> 生成器表达式，多配置生成器下按配置各自展开）。
    #   两份名单里只有当前配置那一份真实存在，copy_if_exists.cmake 对不存在的源静默跳过，
    #   因此合并成一个 foreach 即可，不新增按配置的判断分支；重复项先去重以免同一条命令排两遍。
    set(_gis_runtime_dll_names "${GIS_RUNTIME_DLLS};${GIS_RUNTIME_DLLS_DEBUG}")
    list(REMOVE_DUPLICATES _gis_runtime_dll_names)
    foreach(_dll IN LISTS _gis_runtime_dll_names)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -D_SRC="${GIS_VCPKG_RUNTIME_BIN_DIR}/${_dll}"
                -D_DST="${_dest}/${_dll}"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
        )
    endforeach()
endfunction()
