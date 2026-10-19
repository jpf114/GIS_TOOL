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

# ══ 唯一真源：一条 `(release 名|debug 名样式)` 表，两条拷贝计划由它派生（Task B6 / N88）══
# 形态由**实测的命名约定分布**决定（2026-10-19 逐行复算，两名单各 36 项、一一对齐，无重名）：
#   suffix_d      24 项  release 名去掉 .dll 后加 `d`（gdal→gdald、libexpat→libexpatd、
#                        libpng16→libpng16d、9 个 opencv_*4、4 个 pcre2-*、6 个 Qt6*、tiff、z）
#   underscore_d   2 项  在 .dll 前插 `_d`（geotiff→geotiff_d、proj_9→proj_9_d）
#   same          10 项  两配置同名（double-conversion / geos / geos_c / jpeg62 / json-c /
#                        liblzma / md4c / md4c-html / sqlite3 / turbojpeg）
# ⇒ **不是** "两份名单互为 +d"，所以派生规则必须逐条登记，不能一句 string(REPLACE) 糊过去。
# ⚠ 那 10 项同名的就是 N76 跨配置污染那一类：实测同一文件名**同时存在**于 vcpkg 的 bin 与 debug/bin
#   （字节不同、CRT 不同），名字层面无法区分配置，只有"从哪个目录拷"能区分 ⇒ 单源化后
#   同名项仍必须按配置各自取源目录（见下面 GIS_VCPKG_RUNTIME_BIN_DIR 那条断言），
#   并且拷贝计划里两配置的名字都必须进同一次 foreach（批次 A 合并成一个 foreach 正是为了这件事，
#   不许简化回"只留一份名字表"）。
set(GIS_RUNTIME_DLLS_BASE
    "double-conversion.dll|same"
    "gdal.dll|suffix_d"
    "geos.dll|same"
    "geos_c.dll|same"
    "geotiff.dll|underscore_d"
    "jpeg62.dll|same"
    "json-c.dll|same"
    "libexpat.dll|suffix_d"
    "liblzma.dll|same"
    "libpng16.dll|suffix_d"
    "md4c.dll|same"
    "md4c-html.dll|same"
    "opencv_calib3d4.dll|suffix_d"
    "opencv_core4.dll|suffix_d"
    "opencv_features2d4.dll|suffix_d"
    "opencv_flann4.dll|suffix_d"
    "opencv_imgcodecs4.dll|suffix_d"
    "opencv_imgproc4.dll|suffix_d"
    "opencv_ml4.dll|suffix_d"
    "opencv_stitching4.dll|suffix_d"
    "opencv_video4.dll|suffix_d"
    "pcre2-16.dll|suffix_d"
    "pcre2-32.dll|suffix_d"
    "pcre2-8.dll|suffix_d"
    "pcre2-posix.dll|suffix_d"
    "proj_9.dll|underscore_d"
    "Qt6Core.dll|suffix_d"
    "Qt6Gui.dll|suffix_d"
    "Qt6Sql.dll|suffix_d"
    "Qt6Svg.dll|suffix_d"
    "Qt6Widgets.dll|suffix_d"
    "Qt6Xml.dll|suffix_d"
    "sqlite3.dll|same"
    "tiff.dll|suffix_d"
    "turbojpeg.dll|same"
    "z.dll|suffix_d"
)

# ── 派生两条拷贝计划 + 归零自守 ────────────────────────────────────────────────────
set(_gis_derived_release "")
set(_gis_derived_debug "")
set(_gis_derived_same_name "")
set(_gis_style_suffix_d 0)
set(_gis_style_underscore_d 0)
set(_gis_style_same 0)

foreach(_gis_dll_entry IN LISTS GIS_RUNTIME_DLLS_BASE)
    # 字段数用 REGEX MATCHALL 数分隔符，不能用 string(REPLACE) 再计数：CMake 的 REPLACE 是
    #   **全局**替换，"逐个替换再数次数"永远数出 1 ⇒ 那是条假守卫（本项目实测过的坑）。
    string(REGEX MATCHALL "[|]" _gis_sep_hits "${_gis_dll_entry}")
    list(LENGTH _gis_sep_hits _gis_sep_count)
    if(NOT _gis_sep_count EQUAL 1)
        message(FATAL_ERROR
            "GIS_RUNTIME_DLLS_BASE 条目 '${_gis_dll_entry}' 不是 '<release 名>|<样式>'（分隔符 ${_gis_sep_count} 个）。"
            "格式错的条目会被派生循环静默丢掉 ⇒ 名单变短但配置照样绿，所以这里直接拒绝配置（N88 自守）。")
    endif()
    string(REPLACE "|" ";" _gis_dll_parts "${_gis_dll_entry}")
    list(GET _gis_dll_parts 0 _gis_rel_name)
    list(GET _gis_dll_parts 1 _gis_style)
    if(NOT _gis_rel_name MATCHES "\\.dll$")
        message(FATAL_ERROR "GIS_RUNTIME_DLLS_BASE 条目 '${_gis_dll_entry}' 的 release 名不以 .dll 结尾")
    endif()
    get_filename_component(_gis_stem "${_gis_rel_name}" NAME_WE)
    if(_gis_style STREQUAL "suffix_d")
        set(_gis_dbg_name "${_gis_stem}d.dll")
        math(EXPR _gis_style_suffix_d "${_gis_style_suffix_d} + 1")
    elseif(_gis_style STREQUAL "underscore_d")
        set(_gis_dbg_name "${_gis_stem}_d.dll")
        math(EXPR _gis_style_underscore_d "${_gis_style_underscore_d} + 1")
    elseif(_gis_style STREQUAL "same")
        set(_gis_dbg_name "${_gis_rel_name}")
        list(APPEND _gis_derived_same_name "${_gis_rel_name}")
        math(EXPR _gis_style_same "${_gis_style_same} + 1")
    else()
        message(FATAL_ERROR
            "GIS_RUNTIME_DLLS_BASE 条目 '${_gis_dll_entry}' 的样式 '${_gis_style}' 不是三种之一"
            "（suffix_d / underscore_d / same）。加第四种要先确认 vcpkg 端真有这种名字，别凭想象加。")
    endif()
    list(APPEND _gis_derived_release "${_gis_rel_name}")
    list(APPEND _gis_derived_debug "${_gis_dbg_name}")
endforeach()

# 归零自守（必须有：本项目被"解析出 0 项仍然报绿"咬过不止一次）。
list(LENGTH GIS_RUNTIME_DLLS_BASE _gis_base_count)
if(_gis_base_count LESS 1)
    message(FATAL_ERROR "运行库名单归零 ⇒ 单源化改坏了（N88 自守）")
endif()
list(LENGTH _gis_derived_release _gis_n_release)
list(LENGTH _gis_derived_debug _gis_n_debug)
if(NOT _gis_n_release EQUAL _gis_base_count OR NOT _gis_n_debug EQUAL _gis_base_count)
    message(FATAL_ERROR
        "派生丢项：表 ${_gis_base_count} 项，但 release 计划 ${_gis_n_release} 项 / debug 计划 ${_gis_n_debug} 项。")
endif()
# 重名检查：某配置里出现两次同名，会被下面的 list(REMOVE_DUPLICATES) 合并成一条 POST_BUILD，
#   少拷一项没人报 ⇒ 派生阶段就必须红。（CMake 正则不认 \( \)，要数括号请写 [(]。）
foreach(_gis_probe release debug)
    set(_gis_uniq "${_gis_derived_${_gis_probe}}")
    list(REMOVE_DUPLICATES _gis_uniq)
    list(LENGTH _gis_uniq _gis_uniq_n)
    if(NOT _gis_uniq_n EQUAL _gis_base_count)
        message(FATAL_ERROR
            "派生的 ${_gis_probe} 名单里有重名（${_gis_base_count} 项去重后只剩 ${_gis_uniq_n} 项）"
            "⇒ 表里同一条约定被写了两遍，拷贝集合会少一项。")
    endif()
endforeach()

list(LENGTH _gis_derived_same_name _gis_n_same)
message(STATUS
    "[runtime-dlls] 单源表 ${_gis_base_count} 项（suffix_d=${_gis_style_suffix_d} "
    "underscore_d=${_gis_style_underscore_d} same=${_gis_n_same}）⇒ 派生 release 名单 ${_gis_n_release} 项、"
    "debug 名单 ${_gis_n_debug} 项")

# 拷贝源目录必须是**按配置二选一**的生成器表达式：上面那 ${_gis_n_same} 个同名项全靠它分配置。
# WHY 在配置期断言而不是只在注释里写：GIS_VCPKG_RUNTIME_BIN_DIR 一旦被人改成普通变量（固定某个目录），
#   同名项就会"后一轮覆盖前一轮"，回到 N76 那个 /MD 与 /MDd 互踩的未定义行为
#   （Release 下 cv::imwrite(\".jpg\") 永不返回），而**名字审计看不见它**（两配置目录都有同名文件）。
if(NOT GIS_VCPKG_RUNTIME_BIN_DIR MATCHES "\\$<CONFIG:")
    message(FATAL_ERROR
        "GIS_VCPKG_RUNTIME_BIN_DIR 不是按配置展开的生成器表达式（收到 '${GIS_VCPKG_RUNTIME_BIN_DIR}'）。"
        "运行库的 debug/release 同名项（jpeg62.dll 等 ${_gis_n_same} 个）只能靠源目录区分配置，"
        "固定成一个目录 = N76 跨配置污染复发。")
endif()

# ── 过渡期对照断言（N88 的保险带，第二提交删手抄名单时一并删这里）─────────────────
# WHY 要这一条而不是"改完直接跑测试看绿"：派生规则一旦写错（样式标错、正则吃掉字符），
#   错的是**名单本身**，构建不会报错、测试也可能照旧绿（缺的那个 dll 只是没被拷进来，
#   而残留审计只看"有没有对面配置的件"）。所以先用还留在文件里的手抄名单当地面真相做逐项同序
#   对账，红在配置期；对账通过、两棵树都验过之后，第二提交才把地面真相删掉。
# WHY 比"同序字符串相等"而不是"集合相等"：foreach 是按这份顺序生成 POST_BUILD 命令的，
#   顺序变了就不是"拷贝集合不变"而是"拷贝顺序变了"，Step 3 的判据（集合不变）就不成立。
list(JOIN _gis_derived_release "\n" _gis_derived_release_text)
list(JOIN GIS_RUNTIME_DLLS "\n" _gis_hand_release_text)
list(JOIN _gis_derived_debug "\n" _gis_derived_debug_text)
list(JOIN GIS_RUNTIME_DLLS_DEBUG "\n" _gis_hand_debug_text)
if(NOT _gis_derived_release_text STREQUAL _gis_hand_release_text)
    message(FATAL_ERROR
        "单源派生的 release 名单与手抄名单 GIS_RUNTIME_DLLS 逐项不一致（N88 对照断言）。\n"
        "派生（${_gis_n_release} 项）:\n${_gis_derived_release_text}\n"
        "手抄（现读 ${GIS_RUNTIME_DLLS} 展开）:\n${_gis_hand_release_text}\n"
        "⇒ 表里的样式标错、或 vcpkg 端名字变了；先查清是哪一项，再谈删手抄名单。")
endif()
if(NOT _gis_derived_debug_text STREQUAL _gis_hand_debug_text)
    message(FATAL_ERROR
        "单源派生的 debug 名单与手抄名单 GIS_RUNTIME_DLLS_DEBUG 逐项不一致（N88 对照断言）。\n"
        "派生（${_gis_n_debug} 项）:\n${_gis_derived_debug_text}\n"
        "手抄（现读 ${GIS_RUNTIME_DLLS_DEBUG} 展开）:\n${_gis_hand_debug_text}")
endif()

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
