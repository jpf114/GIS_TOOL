if(NOT DEFINED GUI_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI raster manage nodata regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(SCREENSHOT_DIR "${SCREENSHOT_PATH}" DIRECTORY)
set(OUTPUT_DIR "${SCREENSHOT_DIR}")
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
set(INPUT_PATH "${OUTPUT_DIR}/nodata_input.tif")
gis_gui_prepare_aux_paths("${SCREENSHOT_PATH}" "${STATUS_PATH}")
gis_gui_generate_test_tiff("${INPUT_PATH}")

if(NOT DEFINED GUI_PLATFORM)
    set(GUI_PLATFORM offscreen)
endif()

execute_process(
    COMMAND "${GUI_PATH}"
        -platform ${GUI_PLATFORM}
        --select-plugin raster_manage
        --select-action nodata
        --set-param "input=${INPUT_PATH}"
        --set-param "band=1"
        --set-param "nodata_value=255"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_status_only_success(
    "GUI raster manage nodata regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}"
    "${INPUT_PATH}")

# Task 3 / R-T3-3 的端到端波段锁。
#
# 插件在 band<=0 时回 "NoData set to <v> for all <n> bands"（全波段分支），
# 在 band>=1 时回 "NoData set to <v> for band <n>"（单波段分支），两条分支在消息里可区分。
# 上面那次显式 band=1 必须落在单波段分支：
gis_gui_read_status_file("GUI raster manage nodata regression"
    "${STATUS_PATH}" reg_success reg_cancelled reg_message reg_raw_message)
if(NOT reg_raw_message MATCHES "for band 1")
    message(FATAL_ERROR
        "显式 band=1 的 nodata 必须只作用于第 1 波段，实际消息: ${reg_raw_message}")
endif()

# 再跑一次"GUI 全默认提交"（只给 input 与 nodata_value，band 留给界面默认值）：
# 它必须落到全波段分支，也就是 ParamSpec 声明的默认 0 真正被提交出去。
# 波段数不写成常量：输入图由 System.Drawing 生成，条数不是脚本能假设的，
# 所以锁的是"走了全波段分支"这件可观察的事实。
set(DEFAULT_INPUT_PATH "${OUTPUT_DIR}/nodata_default_input.tif")
set(DEFAULT_SCREENSHOT_PATH "${OUTPUT_DIR}/nodata_default_screenshot.png")
set(DEFAULT_STATUS_PATH "${OUTPUT_DIR}/nodata_default_status.json")
gis_gui_prepare_aux_paths("${DEFAULT_SCREENSHOT_PATH}" "${DEFAULT_STATUS_PATH}")
gis_gui_generate_test_tiff("${DEFAULT_INPUT_PATH}")

execute_process(
    COMMAND "${GUI_PATH}"
        -platform ${GUI_PLATFORM}
        --select-plugin raster_manage
        --select-action nodata
        --set-param "input=${DEFAULT_INPUT_PATH}"
        --set-param "nodata_value=255"
        --auto-execute
        --quit-on-finish
        --screenshot "${DEFAULT_SCREENSHOT_PATH}"
        --status-file "${DEFAULT_STATUS_PATH}"
    RESULT_VARIABLE DEFAULT_EXIT_CODE
    OUTPUT_VARIABLE DEFAULT_STDOUT
    ERROR_VARIABLE DEFAULT_STDERR
)

gis_gui_assert_status_only_success(
    "GUI raster manage nodata default band regression"
    "${DEFAULT_EXIT_CODE}"
    "${DEFAULT_STDOUT}"
    "${DEFAULT_STDERR}"
    "${DEFAULT_SCREENSHOT_PATH}"
    "${DEFAULT_STATUS_PATH}"
    "${DEFAULT_INPUT_PATH}")

gis_gui_read_status_file("GUI raster manage nodata default band regression"
    "${DEFAULT_STATUS_PATH}" default_success default_cancelled default_message default_raw_message)
if(NOT default_raw_message MATCHES "for all [0-9]+ bands")
    message(FATAL_ERROR
        "GUI 全默认提交 nodata 必须走全波段分支（band 默认值 0 = 全波段），实际消息: ${default_raw_message}")
endif()




