# ══ vcpkg 运行库拷贝名单（Task B6 / N88：名单单源化）═══════════════════════════════
# 本文件只有**一份**名单：下面的 GIS_RUNTIME_DLLS_BASE 那条 `(release 名|debug 名样式)` 表。
# GIS_RUNTIME_DLLS / GIS_RUNTIME_DLLS_DEBUG 这两个变量名仍然保留（根 CMakeLists.txt 与
#   gis_copy_minimal_runtime 都按这两个名字读），但它们是**派生结果**，不再手抄。
# WHY 必须派生而不是两条各抄一遍（N88）：两条手抄名单互为对照却没有人保证同步 ——
#   加一项要记得改两处、样式标错一处不报错；本项目已经因此踩过跨配置污染（N76）。
#   单源化后新增一个库只改表里一行，两条拷贝计划（release 名 + debug 名）由同一处约定算出。
# 复算口径（改名单后自查用的命令，务必**限定在 set() 块内**：本文件别处的 .dll 是散文里的举例，
#   整文件 grep 会把它们也数进去）：
#     awk '/^set\(GIS_RUNTIME_DLLS_BASE$/,/^\)/' cmake/gis_runtime_dlls.cmake | grep -c '\.dll|'
#     两条派生名单的项数与样式分布：看配置日志里那行 `[runtime-dlls] 单源表 …`（现算，不抄数）。
#   派生名单与历史手抄名单是否逐项一致，不在这里留结论：那件事由**引入本表那一次提交**
#   的对照断言 + 两棵构树生成物里逐条拷贝命令的源路径比对给出（见派生段末尾）。

# ══ 唯一真源：一条 `(release 名|debug 名样式)` 表，两条拷贝计划由它派生（Task B6 / N88）══
# 形态由**实测的命名约定分布**决定（2026-10-19 逐行复算：两条手抄名单一一对齐、各自无重名，
#   但不是"两份互为 +d"，而是三种约定混在一张表里 ⇒ 分布数字由配置期那行 [runtime-dlls] 现算，
#   样式名直接写在表项里，这里都不抄数，免得注释变成日后数数的靶子）：
#   · 去掉 .dll 再加一个 d 的（gdal→gdald、libexpat→libexpatd、libpng16→libpng16d、
#     9 个 opencv_*4、4 个 pcre2-*、6 个 Qt6*、tiff、z）；
#   · 在 .dll 前插 _d 的（geotiff→geotiff_d、proj_9→proj_9_d）；
#   · 两配置**同名**的（double-conversion / geos / geos_c / jpeg62 / json-c / liblzma /
#     md4c / md4c-html / sqlite3 / turbojpeg）。
# ⇒ 派生规则必须逐条登记在表里，不能一句全局替换糊过去。
# ⚠ 最后那一族同名的就是 N76 跨配置污染那一类：实测同一文件名**同时存在**于 vcpkg 的 bin 与
#   debug/bin（字节不同、CRT 不同），名字层面无法区分配置，只有"从哪个目录拷"能区分 ⇒ 单源化后
#   同名项仍必须按配置各自取源目录（见下面 GIS_VCPKG_RUNTIME_BIN_DIR 那条断言），
#   并且两条名单都必须进同一次 foreach（批次 A 合并成一个 foreach 正是为了这件事，
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

# ── 派生结果落到两个既有变量名上 ─────────────────────────────────────────────────
# 这里**不再**与手抄名单对账：手抄名单已随本提交删除，地面真相在它还在的时候已经用完
#   （上一提交 e726710 的对照断言逐字符串比过两条名单，两棵构树的 POST_BUILD 拷贝计划
#    也逐源路径比对过一致）。此后守住名单正确性的三件事，全部是本文件里的硬闸：
#     ① 归零 / 条目格式 / 派生项数 / 重名 四条自守（错在配置期，不靠测试）；
#     ② 同名项的源目录必须是 $<CONFIG:> 生成器表达式（N76 那条防线）；
#     ③ 装出来的包由 tests/check_install_closure.cmake 逐 PE 判导入闭包 ——
#        样式标错会让某个配置的 dll 静默缺席，闭包断言是唯一能看见"少了一个"的判据
#        （runtime_dll_cross_config_audit 只看得见"多了对面配置的残留"，看不见"少了"）。
set(GIS_RUNTIME_DLLS ${_gis_derived_release})
set(GIS_RUNTIME_DLLS_DEBUG ${_gis_derived_debug})

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
    #   两条派生名单里只有当前配置那一份真实存在，copy_if_exists.cmake 对不存在的源静默跳过，
    #   因此合并成一个 foreach 即可，不新增按配置的判断分支；重复项先去重以免同一条命令排两遍。
    #   （同名那几项合并后只剩一条命令，但那条命令的**源目录**按配置展开 ⇒ 拷到的就是本配置那份。）
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
