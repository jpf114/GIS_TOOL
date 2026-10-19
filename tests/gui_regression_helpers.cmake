include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_defaults.cmake")

# N101 + N102：这条腿 spawn 的是真 gis-gui.exe（独立进程），不注入就会把用户设置写进真实
#   AppConfigLocation（N101），并把"保存自定义指数预设"的落点（custom_index_preset_store.cpp，
#   AppConfigLocation 的第二个写者）留在守卫管辖外（N102）。两个变量必须**成对**给：
#   src/gui/main.cpp 的落点守卫在回归式运行下要求两个都非空才放行 ⇒ 少一条腿的变量会表现为
#   非零退出而红，不会悄悄写用户状态。
# 落点取"该用例自己的产物目录"⇒ 天然每用例一份，且与 build/ 同生命周期（gitignore 已覆盖）。
# 主判据用 SCREENSHOT_PATH：并非每条腿都带 OUTPUT_PATH（inspect/status 类不产出新文件）
#   ⇒ 拿它当锚会留下静默不注入的腿（正是本任务要消灭的形态）。
# WHY 做在 include 期顶层：execute_process 没有 ENVIRONMENT 参数，set(ENV{…}) 改的是本脚本进程的
#   环境、子进程继承 ⇒ 一处覆盖全部 driver 腿，不必逐条给 add_test 抄 ENVIRONMENT；
#   ctest 直接 spawn 的那条（gui_smoke_startup）走 tests/CMakeLists.txt 的 ENVIRONMENT 属性。
#   前提是每个 driver 的 include 早于它第一次 execute_process，这一点按文件逐个核过（不是假设）。
# 覆盖面现跑命令（行首锚定，注释行不会被数进去；数字归报告与台账，不进注释）：
#   include 本载体的 driver 文件数：grep -rlE "^[^#]*gui_regression_helpers" tests/run_*.cmake | wc -l
#   注册 gui_ 用例总数：ctest --test-dir build/debug -C Debug -N -R "^gui_" | tail -n 1
if(DEFINED SCREENSHOT_PATH AND NOT SCREENSHOT_PATH STREQUAL "")
    get_filename_component(_gis_case_dir "${SCREENSHOT_PATH}" DIRECTORY)
    set(ENV{GIS_TOOL_SETTINGS_INI} "${_gis_case_dir}/gis_gui_settings.ini")
    set(ENV{GIS_CUSTOM_INDEX_PRESET_FILE} "${_gis_case_dir}/spindex_presets.json")
elseif(DEFINED OUTPUT_PATH AND NOT OUTPUT_PATH STREQUAL "")
    get_filename_component(_gis_case_dir "${OUTPUT_PATH}" DIRECTORY)
    set(ENV{GIS_TOOL_SETTINGS_INI} "${_gis_case_dir}/gis_gui_settings.ini")
    set(ENV{GIS_CUSTOM_INDEX_PRESET_FILE} "${_gis_case_dir}/spindex_presets.json")
else()
    message(FATAL_ERROR "driver 既无 SCREENSHOT_PATH 也无 OUTPUT_PATH ⇒ 无法给出每用例私有落点，拒绝继续（N101/N102）")
endif()

function(gis_gui_prepare_output_paths output_path screenshot_path)
    get_filename_component(output_dir "${output_path}" DIRECTORY)
    get_filename_component(screenshot_dir "${screenshot_path}" DIRECTORY)

    file(REMOVE "${output_path}" "${screenshot_path}")
    file(MAKE_DIRECTORY "${output_dir}")
    file(MAKE_DIRECTORY "${screenshot_dir}")
endfunction()

function(gis_gui_prepare_artifact_paths output_path screenshot_path status_path)
    get_filename_component(output_dir "${output_path}" DIRECTORY)
    get_filename_component(screenshot_dir "${screenshot_path}" DIRECTORY)
    get_filename_component(status_dir "${status_path}" DIRECTORY)

    file(REMOVE "${output_path}" "${screenshot_path}" "${status_path}")
    file(MAKE_DIRECTORY "${output_dir}")
    file(MAKE_DIRECTORY "${screenshot_dir}")
    file(MAKE_DIRECTORY "${status_dir}")
endfunction()

function(gis_gui_prepare_aux_paths screenshot_path status_path)
    get_filename_component(screenshot_dir "${screenshot_path}" DIRECTORY)
    get_filename_component(status_dir "${status_path}" DIRECTORY)

    file(REMOVE "${screenshot_path}" "${status_path}")
    file(MAKE_DIRECTORY "${screenshot_dir}")
    file(MAKE_DIRECTORY "${status_dir}")
endfunction()

function(gis_gui_read_status_file test_name status_path out_success out_cancelled out_message out_raw_message)
    if(NOT EXISTS "${status_path}")
        message(FATAL_ERROR "${test_name} did not produce status file: ${status_path}")
    endif()

    file(READ "${status_path}" status_json)
    string(JSON status_success GET "${status_json}" success)
    string(JSON status_cancelled GET "${status_json}" cancelled)
    string(JSON status_message GET "${status_json}" message)
    string(JSON status_raw_message GET "${status_json}" raw_message)

    set(${out_success} "${status_success}" PARENT_SCOPE)
    set(${out_cancelled} "${status_cancelled}" PARENT_SCOPE)
    set(${out_message} "${status_message}" PARENT_SCOPE)
    set(${out_raw_message} "${status_raw_message}" PARENT_SCOPE)
endfunction()

function(gis_gui_assert_status_bool actual_value expected_value description)
    string(TOLOWER "${actual_value}" actual_normalized)
    string(TOLOWER "${expected_value}" expected_normalized)

    if(expected_normalized STREQUAL "true")
        if(NOT actual_normalized STREQUAL "true" AND NOT actual_normalized STREQUAL "on" AND NOT actual_normalized STREQUAL "1")
            message(FATAL_ERROR "${description}: expected true, got ${actual_value}")
        endif()
    else()
        if(NOT actual_normalized STREQUAL "false" AND NOT actual_normalized STREQUAL "off" AND NOT actual_normalized STREQUAL "0")
            message(FATAL_ERROR "${description}: expected false, got ${actual_value}")
        endif()
    endif()
endfunction()

# ── 截图判据（批次 B / B7，N98）：两层，谁都不许单独缺席 ──────────────────────────
#   层 1 结构层 gis_gui_parse_png_ihdr_size()：只读文件自己头里的 IHDR 解宽高，**不读被测程序产出的
#     任何字段** ⇒ 渲染器报什么都骗不过它，它也不依赖渲染器（所以它自己就能红，见反向证明）。
#   层 2 内容层 gis_gui_assert_screenshot_rendered()：读 gis-gui 在 grab() 之后写进既有 status 文件的
#     screenshot_distinct_colors 判"确实画出了内容"，并把它自报的宽高与层 1 的独立值交叉比对
#     —— 没有这条交叉比对，层 2 就是"渲染器自报自证"。
# 原来那条"按平台名跳过"（gis_gui_platform_skips_screenshot）已整个删除，理由见
#   gui_regression_defaults.cmake 顶部：它对我们实际用的平台恒为真，等于把截图判定写成死代码。
# 阈值现跑命令（对当前树已有产物重算最低值；行首锚定 ⇒ 注释不会被数进去）：
#   最低色数：grep -rhoE '"screenshot_distinct_colors": *[0-9]+' build/debug/tests/gui_regression | grep -oE '[0-9]+' | sort -n | head -1
#   最低宽高：grep -rhoE '"screenshot_(width|height)": *[0-9]+'  build/debug/tests/gui_regression | grep -oE '[0-9]+' | sort -n | head -1
#   带统计的 status 文件数（与 gui_ 截图腿数对账）：
#     grep -rlE '"screenshot_distinct_colors": *[0-9]+' build/debug/tests/gui_regression | wc -l

# 结构层的宽高下限。WHY：一帧退化到几个像元以内不可能是这个应用的窗口，而 IHDR 是文件自己的字节、
#   与被测程序的自报无关 ⇒ 留着这条，结构层才是**不靠内容层也能红**的一层。
set(GIS_GUI_SCREENSHOT_MIN_DIMENSION 8)

# 内容层的色数下限。WHY 16：空白/纯色帧按定义只有 1 色，而最差合法产物（低保真平台抓的帧）实测仍是
#   数百色 ⇒ 16 离"空白"留了一个数量级以上余量、离"最差合法值"同样留了一个数量级以上 ⇒ 杀得掉空白
#   又不会因平台/字体/抗锯齿差异误杀。
#   不许换成像素哈希或基线图比对：实测同一条命令连跑两遍就有数百个像元与 md5 差异（色数只抖 ±4）
#   ⇒ 聚合的"不同颜色数"是稳定量，逐像元不是。
#   也不许换成文件大小阈值：实测有内容的低保真产物可以比空白产物还小一个数量级 ⇒ 体积与"画没画"不同向。
set(GIS_GUI_SCREENSHOT_MIN_DISTINCT_COLORS 16)

function(gis_gui_parse_png_ihdr_size png_path out_width out_height out_error)
    set(${out_width} "" PARENT_SCOPE)
    set(${out_height} "" PARENT_SCOPE)
    set(${out_error} "" PARENT_SCOPE)

    if(NOT EXISTS "${png_path}")
        set(${out_error} "file does not exist" PARENT_SCOPE)
        return()
    endif()

    file(SIZE "${png_path}" _png_file_size)
    if(_png_file_size LESS 24)
        set(${out_error} "file is ${_png_file_size} bytes, too small to hold a PNG signature plus IHDR" PARENT_SCOPE)
        return()
    endif()

    # 头 24 字节 = 8 签名 + 4 IHDR 段长 + 4 段名 + 4 宽 + 4 高（大端 u32）；HEX 模式下正好 48 个字符。
    file(READ "${png_path}" _png_head_hex HEX LIMIT 24)
    string(LENGTH "${_png_head_hex}" _png_head_hex_length)
    if(NOT _png_head_hex_length EQUAL 48)
        set(${out_error} "read ${_png_head_hex_length} hex chars instead of 48" PARENT_SCOPE)
        return()
    endif()
    if(NOT _png_head_hex MATCHES "^89504e470d0a1a0a")
        set(${out_error} "not a PNG (signature is ${_png_head_hex})" PARENT_SCOPE)
        return()
    endif()
    string(SUBSTRING "${_png_head_hex}" 24 8 _png_chunk_type_hex)
    if(NOT _png_chunk_type_hex STREQUAL "49484452")
        set(${out_error} "first chunk is not IHDR (0x${_png_chunk_type_hex})" PARENT_SCOPE)
        return()
    endif()

    string(SUBSTRING "${_png_head_hex}" 32 8 _png_width_hex)
    string(SUBSTRING "${_png_head_hex}" 40 8 _png_height_hex)
    math(EXPR _png_width "0x${_png_width_hex}")
    math(EXPR _png_height "0x${_png_height_hex}")
    set(${out_width} "${_png_width}" PARENT_SCOPE)
    set(${out_height} "${_png_height}" PARENT_SCOPE)
endfunction()

function(gis_gui_read_screenshot_stats test_name status_path out_width out_height out_distinct_colors)
    if(NOT EXISTS "${status_path}")
        message(FATAL_ERROR
            "${test_name} did not produce the status file that carries the screenshot stats: ${status_path}")
    endif()

    file(READ "${status_path}" _stats_json)
    foreach(_key IN ITEMS screenshot_width screenshot_height screenshot_distinct_colors)
        string(JSON _value ERROR_VARIABLE _lookup_error GET "${_stats_json}" ${_key})
        if(_lookup_error)
            message(FATAL_ERROR
                "${test_name} status file has no ${_key}: the GUI side never reported the screenshot content, "
                "so the content layer has nothing to judge.\n  status file: ${status_path}")
        endif()
        if(NOT _value MATCHES "^[0-9]+$")
            message(FATAL_ERROR
                "${test_name} reported a non-numeric ${_key}: '${_value}'\n  status file: ${status_path}")
        endif()
    endforeach()

    string(JSON _reported_width GET "${_stats_json}" screenshot_width)
    string(JSON _reported_height GET "${_stats_json}" screenshot_height)
    string(JSON _reported_distinct GET "${_stats_json}" screenshot_distinct_colors)

    set(${out_width} "${_reported_width}" PARENT_SCOPE)
    set(${out_height} "${_reported_height}" PARENT_SCOPE)
    set(${out_distinct_colors} "${_reported_distinct}" PARENT_SCOPE)
endfunction()

function(gis_gui_assert_screenshot_rendered test_name screenshot_path status_path)
    # 内容层要有"渲染器自报"的数据源，而数据源只有既有 status 文件这一条通道 ⇒ 通道断了宁可红，
    #   不许静默降级（那正是 N98 的形态）。
    if(NOT status_path)
        message(FATAL_ERROR
            "${test_name} calls the screenshot judgement without a status channel: the content layer needs "
            "gis-gui's own screenshot_width/height/distinct_colors to be cross-checked against the file's IHDR. "
            "Give this leg a --status-file instead of leaving the render layer unjudged.")
    endif()

    # 层 1：结构。独立于被测程序，先把"这张 PNG 有一帧真实的画面"钉住。
    gis_gui_parse_png_ihdr_size("${screenshot_path}" _ihdr_width _ihdr_height _ihdr_error)
    if(_ihdr_error)
        message(FATAL_ERROR
            "${test_name} screenshot failed the structural check: ${_ihdr_error}\n  screenshot: ${screenshot_path}")
    endif()
    if(NOT _ihdr_width GREATER 0 OR NOT _ihdr_height GREATER 0)
        message(FATAL_ERROR
            "${test_name} produced a degenerate screenshot frame: ${_ihdr_width} x ${_ihdr_height}\n"
            "  screenshot: ${screenshot_path}")
    endif()
    if(_ihdr_width LESS GIS_GUI_SCREENSHOT_MIN_DIMENSION OR _ihdr_height LESS GIS_GUI_SCREENSHOT_MIN_DIMENSION)
        message(FATAL_ERROR
            "${test_name} screenshot frame ${_ihdr_width} x ${_ihdr_height} is below the structural floor "
            "${GIS_GUI_SCREENSHOT_MIN_DIMENSION} x ${GIS_GUI_SCREENSHOT_MIN_DIMENSION}: no application window "
            "is that small.\n  screenshot: ${screenshot_path}")
    endif()

    # 层 2：内容 + 交叉比对。自报值必须与层 1 的独立值一致，否则"报了个大帧、实际存了张小图"照样红。
    gis_gui_read_screenshot_stats("${test_name}" "${status_path}" _reported_width _reported_height _distinct_colors)
    if(NOT _reported_width EQUAL _ihdr_width OR NOT _reported_height EQUAL _ihdr_height)
        message(FATAL_ERROR
            "${test_name} screenshot self-report disagrees with the file's own IHDR: status says "
            "${_reported_width} x ${_reported_height}, IHDR says ${_ihdr_width} x ${_ihdr_height}\n"
            "  screenshot: ${screenshot_path}\n  status: ${status_path}")
    endif()
    if(NOT _distinct_colors GREATER_EQUAL GIS_GUI_SCREENSHOT_MIN_DISTINCT_COLORS)
        message(FATAL_ERROR
            "${test_name} screenshot only has ${_distinct_colors} distinct colors (< "
            "${GIS_GUI_SCREENSHOT_MIN_DISTINCT_COLORS}): the render layer produced a frame, but nothing was "
            "drawn into it.\n  screenshot: ${screenshot_path}\n  status: ${status_path}")
    endif()
endfunction()

function(gis_gui_assert_regression_result test_name exit_code stdout_text stderr_text output_path screenshot_path status_path)
    if(NOT "${exit_code}" STREQUAL "0")
        message(FATAL_ERROR
            "${test_name} failed with exit code ${exit_code}\n"
            "stdout:\n${stdout_text}\n"
            "stderr:\n${stderr_text}")
    endif()

    if(NOT EXISTS "${output_path}")
        message(FATAL_ERROR "${test_name} did not produce output: ${output_path}")
    endif()

    file(SIZE "${output_path}" output_size)
    if(output_size EQUAL 0)
        message(FATAL_ERROR "${test_name} produced an empty output file: ${output_path}")
    endif()

    gis_gui_assert_screenshot_rendered("${test_name}" "${screenshot_path}" "${status_path}")

    gis_gui_read_status_file("${test_name}" "${status_path}"
        status_success status_cancelled status_message status_raw_message)

    gis_gui_assert_status_bool("${status_success}" "true"
        "${test_name} reported unsuccessful status")
    gis_gui_assert_status_bool("${status_cancelled}" "false"
        "${test_name} unexpectedly reported cancellation")
endfunction()

function(gis_gui_assert_failure_result test_name exit_code stdout_text stderr_text output_path screenshot_path status_path expected_raw_message)
    if(NOT "${exit_code}" STREQUAL "0")
        message(FATAL_ERROR
            "${test_name} failed with exit code ${exit_code}\n"
            "stdout:\n${stdout_text}\n"
            "stderr:\n${stderr_text}")
    endif()

    if(EXISTS "${output_path}")
        file(SIZE "${output_path}" output_size)
        if(NOT output_size EQUAL 0)
            message(FATAL_ERROR
                "${test_name} unexpectedly produced output: ${output_path}")
        endif()
    endif()

    gis_gui_assert_screenshot_rendered("${test_name}" "${screenshot_path}" "${status_path}")

    gis_gui_read_status_file("${test_name}" "${status_path}"
        status_success status_cancelled status_message status_raw_message)

    gis_gui_assert_status_bool("${status_success}" "false"
        "${test_name} unexpectedly reported success")
    gis_gui_assert_status_bool("${status_cancelled}" "false"
        "${test_name} unexpectedly reported cancellation")

    string(FIND "${status_raw_message}" "${expected_raw_message}" expected_index)
    if(expected_index EQUAL -1)
        message(FATAL_ERROR
            "${test_name} reported unexpected raw failure message.\n"
            "expected substring: ${expected_raw_message}\n"
            "actual raw_message: ${status_raw_message}\n"
            "localized message: ${status_message}")
    endif()
endfunction()

function(gis_gui_assert_status_only_success test_name exit_code stdout_text stderr_text screenshot_path status_path subject_path)
    if(NOT "${exit_code}" STREQUAL "0")
        message(FATAL_ERROR
            "${test_name} failed with exit code ${exit_code}\n"
            "stdout:\n${stdout_text}\n"
            "stderr:\n${stderr_text}")
    endif()

    if(NOT EXISTS "${subject_path}")
        message(FATAL_ERROR "${test_name} did not preserve expected file: ${subject_path}")
    endif()

    file(SIZE "${subject_path}" subject_size)
    if(subject_size EQUAL 0)
        message(FATAL_ERROR "${test_name} preserved an empty file: ${subject_path}")
    endif()

    gis_gui_assert_screenshot_rendered("${test_name}" "${screenshot_path}" "${status_path}")

    gis_gui_read_status_file("${test_name}" "${status_path}"
        status_success status_cancelled status_message status_raw_message)

    gis_gui_assert_status_bool("${status_success}" "true"
        "${test_name} reported unsuccessful status")
    gis_gui_assert_status_bool("${status_cancelled}" "false"
        "${test_name} unexpectedly reported cancellation")
endfunction()

function(gis_gui_assert_status_success_no_output test_name exit_code stdout_text stderr_text screenshot_path status_path)
    if(NOT "${exit_code}" STREQUAL "0")
        message(FATAL_ERROR
            "${test_name} failed with exit code ${exit_code}\n"
            "stdout:\n${stdout_text}\n"
            "stderr:\n${stderr_text}")
    endif()

    gis_gui_assert_screenshot_rendered("${test_name}" "${screenshot_path}" "${status_path}")

    gis_gui_read_status_file("${test_name}" "${status_path}"
        status_success status_cancelled status_message status_raw_message)

    gis_gui_assert_status_bool("${status_success}" "true"
        "${test_name} reported unsuccessful status")
    gis_gui_assert_status_bool("${status_cancelled}" "false"
        "${test_name} unexpectedly reported cancellation")
endfunction()

function(gis_gui_assert_directory_success test_name exit_code stdout_text stderr_text output_dir screenshot_path status_path)
    if(NOT "${exit_code}" STREQUAL "0")
        message(FATAL_ERROR
            "${test_name} failed with exit code ${exit_code}\n"
            "stdout:\n${stdout_text}\n"
            "stderr:\n${stderr_text}")
    endif()

    if(NOT IS_DIRECTORY "${output_dir}")
        message(FATAL_ERROR "${test_name} did not produce output directory: ${output_dir}")
    endif()

    file(GLOB output_entries "${output_dir}/*")
    list(LENGTH output_entries output_count)
    if(output_count EQUAL 0)
        message(FATAL_ERROR "${test_name} produced an empty output directory: ${output_dir}")
    endif()

    gis_gui_assert_screenshot_rendered("${test_name}" "${screenshot_path}" "${status_path}")

    gis_gui_read_status_file("${test_name}" "${status_path}"
        status_success status_cancelled status_message status_raw_message)

    gis_gui_assert_status_bool("${status_success}" "true"
        "${test_name} reported unsuccessful status")
    gis_gui_assert_status_bool("${status_cancelled}" "false"
        "${test_name} unexpectedly reported cancellation")
endfunction()

function(gis_gui_generate_test_bmp image_path)
    find_program(GIS_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
    if(NOT GIS_POWERSHELL_EXECUTABLE)
        message(FATAL_ERROR "PowerShell is required to generate GUI raster regression input.")
    endif()

    get_filename_component(image_dir "${image_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${image_dir}")

    set(ps_command
        "[void][Reflection.Assembly]::LoadWithPartialName('System.Drawing');"
        "$bmp = New-Object System.Drawing.Bitmap 32, 32;"
        "for ($y = 0; $y -lt 32; $y++) {"
        "  for ($x = 0; $x -lt 32; $x++) {"
        "    $v = ($x * 8 + $y * 3) % 256;"
        "    $c = [System.Drawing.Color]::FromArgb($v, $v, $v);"
        "    $bmp.SetPixel($x, $y, $c);"
        "  }"
        "}"
        "$bmp.Save('${image_path}', [System.Drawing.Imaging.ImageFormat]::Bmp);"
        "$bmp.Dispose();")

    execute_process(
        COMMAND "${GIS_POWERSHELL_EXECUTABLE}" -NoProfile -Command "${ps_command}"
        RESULT_VARIABLE ps_exit_code
        OUTPUT_VARIABLE ps_stdout
        ERROR_VARIABLE ps_stderr
    )
    if(NOT ps_exit_code EQUAL 0)
        message(FATAL_ERROR
            "Failed to generate GUI raster regression input: ${image_path}\n"
            "stdout:\n${ps_stdout}\n"
            "stderr:\n${ps_stderr}")
    endif()
endfunction()

function(gis_gui_generate_test_tiff image_path)
    find_program(GIS_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
    if(NOT GIS_POWERSHELL_EXECUTABLE)
        message(FATAL_ERROR "PowerShell is required to generate GUI raster regression input.")
    endif()

    get_filename_component(image_dir "${image_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${image_dir}")

    set(ps_command
        "[void][Reflection.Assembly]::LoadWithPartialName('System.Drawing');"
        "$bmp = New-Object System.Drawing.Bitmap 32, 32;"
        "for ($y = 0; $y -lt 32; $y++) {"
        "  for ($x = 0; $x -lt 32; $x++) {"
        "    $r = ($x * 7 + $y * 3) % 256;"
        "    $g = ($x * 5 + $y * 11) % 256;"
        "    $b = ($x * 13 + $y * 2) % 256;"
        "    $c = [System.Drawing.Color]::FromArgb($r, $g, $b);"
        "    $bmp.SetPixel($x, $y, $c);"
        "  }"
        "}"
        "$bmp.Save('${image_path}', [System.Drawing.Imaging.ImageFormat]::Tiff);"
        "$bmp.Dispose();")

    execute_process(
        COMMAND "${GIS_POWERSHELL_EXECUTABLE}" -NoProfile -Command "${ps_command}"
        RESULT_VARIABLE ps_exit_code
        OUTPUT_VARIABLE ps_stdout
        ERROR_VARIABLE ps_stderr
    )
    if(NOT ps_exit_code EQUAL 0)
        message(FATAL_ERROR
            "Failed to generate GUI raster regression input: ${image_path}\n"
            "stdout:\n${ps_stdout}\n"
            "stderr:\n${ps_stderr}")
    endif()
endfunction()

function(gis_gui_generate_test_bmp_with_size image_path width height)
    find_program(GIS_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
    if(NOT GIS_POWERSHELL_EXECUTABLE)
        message(FATAL_ERROR "PowerShell is required to generate GUI raster regression input.")
    endif()

    get_filename_component(image_dir "${image_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${image_dir}")

    set(ps_command
        "[void][Reflection.Assembly]::LoadWithPartialName('System.Drawing');"
        "$bmp = New-Object System.Drawing.Bitmap ${width}, ${height};"
        "for ($y = 0; $y -lt ${height}; $y++) {"
        "  for ($x = 0; $x -lt ${width}; $x++) {"
        "    $v = ($x * 9 + $y * 5) % 256;"
        "    $c = [System.Drawing.Color]::FromArgb($v, $v, $v);"
        "    $bmp.SetPixel($x, $y, $c);"
        "  }"
        "}"
        "$bmp.Save('${image_path}', [System.Drawing.Imaging.ImageFormat]::Bmp);"
        "$bmp.Dispose();")

    execute_process(
        COMMAND "${GIS_POWERSHELL_EXECUTABLE}" -NoProfile -Command "${ps_command}"
        RESULT_VARIABLE ps_exit_code
        OUTPUT_VARIABLE ps_stdout
        ERROR_VARIABLE ps_stderr
    )
    if(NOT ps_exit_code EQUAL 0)
        message(FATAL_ERROR
            "Failed to generate GUI raster regression input: ${image_path}\n"
            "stdout:\n${ps_stdout}\n"
            "stderr:\n${ps_stderr}")
    endif()
endfunction()

function(gis_gui_generate_feature_bmp image_path width height)
    find_program(GIS_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
    if(NOT GIS_POWERSHELL_EXECUTABLE)
        message(FATAL_ERROR "PowerShell is required to generate GUI raster regression input.")
    endif()

    get_filename_component(image_dir "${image_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${image_dir}")

    set(ps_command
        "[void][Reflection.Assembly]::LoadWithPartialName('System.Drawing');"
        "$bmp = New-Object System.Drawing.Bitmap ${width}, ${height};"
        "$g = [System.Drawing.Graphics]::FromImage($bmp);"
        "$g.Clear([System.Drawing.Color]::Black);"
        "$brushW = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::White);"
        "$brushG = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::Gray);"
        "for ($y = 0; $y -lt ${height}; $y += 16) {"
        "  for ($x = 0; $x -lt ${width}; $x += 16) {"
        "    if ((($x + $y) / 16) % 2 -eq 0) { $g.FillRectangle($brushW, $x, $y, 8, 8) }"
        "    else { $g.FillRectangle($brushG, $x, $y, 8, 8) }"
        "  }"
        "}"
        "$pen = New-Object System.Drawing.Pen([System.Drawing.Color]::White, 2);"
        "$g.DrawLine($pen, 0, 0, ${width}, ${height});"
        "$g.DrawLine($pen, 0, ${height}, ${width}, 0);"
        "$g.DrawEllipse($pen, 20, 20, 24, 24);"
        "$g.DrawRectangle($pen, 52, 18, 28, 20);"
        "$g.FillEllipse($brushW, 90, 90, 18, 18);"
        "$bmp.Save('${image_path}', [System.Drawing.Imaging.ImageFormat]::Bmp);"
        "$g.Dispose();"
        "$pen.Dispose();"
        "$brushW.Dispose();"
        "$brushG.Dispose();"
        "$bmp.Dispose();")

    execute_process(
        COMMAND "${GIS_POWERSHELL_EXECUTABLE}" -NoProfile -Command "${ps_command}"
        RESULT_VARIABLE ps_exit_code
        OUTPUT_VARIABLE ps_stdout
        ERROR_VARIABLE ps_stderr
    )
    if(NOT ps_exit_code EQUAL 0)
        message(FATAL_ERROR
            "Failed to generate GUI raster regression input: ${image_path}\n"
            "stdout:\n${ps_stdout}\n"
            "stderr:\n${ps_stderr}")
    endif()
endfunction()

function(gis_gui_write_geojson_dataset output_path dataset_kind)
    get_filename_component(output_dir "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_dir}")

    if(dataset_kind STREQUAL "projected_touching_polygons")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_touching_polygons",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "poly_a" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[[0,0],[100,0],[100,100],[0,100],[0,0]]]
      }
    },
    {
      "type": "Feature",
      "properties": { "id": 2, "name": "poly_b" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[[100,0],[200,0],[200,100],[100,100],[100,0]]]
      }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_overlapping_polygons")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_overlapping_polygons",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "poly_a" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[[0,0],[120,0],[120,120],[0,120],[0,0]]]
      }
    },
    {
      "type": "Feature",
      "properties": { "id": 2, "name": "poly_b" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[[80,40],[200,40],[200,160],[80,160],[80,40]]]
      }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_invalid_polygon")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_invalid_polygon",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "bowtie" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[[0,0],[120,120],[120,0],[0,120],[0,0]]]
      }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_hole_polygon")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_hole_polygon",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "with_hole" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [
          [[0,0],[200,0],[200,200],[0,200],[0,0]],
          [[60,60],[140,60],[140,140],[60,140],[60,60]]
        ]
      }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_multipart_polygons")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_multipart_polygons",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "multi_a" },
      "geometry": {
        "type": "MultiPolygon",
        "coordinates": [
          [[[0,0],[60,0],[60,60],[0,60],[0,0]]],
          [[[100,0],[160,0],[160,60],[100,60],[100,0]]]
        ]
      }
    },
    {
      "type": "Feature",
      "properties": { "id": 2, "name": "poly_b" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[[0,100],[80,100],[80,180],[0,180],[0,100]]]
      }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_lines")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_lines",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "line_a" },
      "geometry": {
        "type": "LineString",
        "coordinates": [[0,0],[100,50],[200,50]]
      }
    },
    {
      "type": "Feature",
      "properties": { "id": 2, "name": "line_b" },
      "geometry": {
        "type": "LineString",
        "coordinates": [[40,120],[160,120],[220,180]]
      }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_points_duplicate")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_points_duplicate",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "p1" },
      "geometry": { "type": "Point", "coordinates": [10, 10] }
    },
    {
      "type": "Feature",
      "properties": { "id": 2, "name": "p2" },
      "geometry": { "type": "Point", "coordinates": [10, 10] }
    },
    {
      "type": "Feature",
      "properties": { "id": 3, "name": "p3" },
      "geometry": { "type": "Point", "coordinates": [50, 20] }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_nearest_source_points")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_nearest_source_points",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 1, "name": "src_a" },
      "geometry": { "type": "Point", "coordinates": [20, 20] }
    },
    {
      "type": "Feature",
      "properties": { "id": 2, "name": "src_b" },
      "geometry": { "type": "Point", "coordinates": [180, 180] }
    }
  ]
}
]=])
    elseif(dataset_kind STREQUAL "projected_nearest_target_points")
        set(content [=[
{
  "type": "FeatureCollection",
  "name": "projected_nearest_target_points",
  "crs": { "type": "name", "properties": { "name": "EPSG:3857" } },
  "features": [
    {
      "type": "Feature",
      "properties": { "id": 101, "name": "target_a" },
      "geometry": { "type": "Point", "coordinates": [40, 40] }
    },
    {
      "type": "Feature",
      "properties": { "id": 102, "name": "target_b" },
      "geometry": { "type": "Point", "coordinates": [210, 200] }
    }
  ]
}
]=])
    else()
        message(FATAL_ERROR "Unsupported GeoJSON dataset kind: ${dataset_kind}")
    endif()

    file(WRITE "${output_path}" "${content}")
endfunction()
