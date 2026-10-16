if(NOT DEFINED GUI_PATH OR NOT DEFINED GUI_TEST_DATA_HELPER_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI spindex EVI regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")
set(INPUT_PATH "${OUTPUT_DIR}/evi_input.tif")
if(NOT DEFINED GUI_PLATFORM)
    set(GUI_PLATFORM offscreen)
endif()

execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" ndvi-raster "${INPUT_PATH}"
    RESULT_VARIABLE HELPER_EXIT_CODE
)
if(NOT "${HELPER_EXIT_CODE}" STREQUAL "0")
    message(FATAL_ERROR "Failed to generate EVI GUI test raster.")
endif()

if(NOT DEFINED GUI_PLATFORM)
    set(GUI_PLATFORM offscreen)
endif()

execute_process(
    COMMAND "${GUI_PATH}"
        -platform ${GUI_PLATFORM}
        --select-plugin spindex
        --select-action evi
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "blue_band=1"
        --set-param "red_band=3"
        --set-param "nir_band=4"
        --set-param "evi_l=1"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_regression_result(
    "GUI spindex EVI regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")

# Task 9 / R-T9-4 的数值锁。
#
# WHY：这个脚本过去只断"成功 + 产物存在"，所以 EVI 的 L 被 GUI 默认值灌成 SAVI 的 0.5
# 时它照样一路绿灯 —— 批次 A 反复出现的"只断存在性"正是这个形状。
# gis_gui_assert_regression_result 本身不接受数值比较，这里沿用 run_gui_raster_manage_nodata_regression.cmake
# 已有的 gis_gui_read_status_file + MATCHES 写法（不新造机制），把像元值钉进消息里。
#
# 输入由 gui_test_data_helper 的 makeNdviRaster 生成，六波段常量 {10,20,30,70,90,110}；
# 本脚本提交 blue=1/red=3/nir=4/evi_l=1，界面把其余 spec 默认值原样带上（g=2.5、c1=6、c2=7.5）：
#   EVI = 2.5*(70-30)/(70+6*30-7.5*10+1) = 100/176 = 0.568182（std::to_string 取 6 位）
# 缺陷版本（L=0.5）给的是 100/175.5 = 0.569801，与本断言可区分。
#
# fix round 1（F2）WHY 显式提交 evi_l=1：这条 --set-param 走的判据是 mainwindow.cpp 的 hasParam，
# 键没被渲染（= paramSpecs 里整条 evi_l 被删）时 main.cpp 以 exit 2 + raw_message=failed_to_apply_param:evi_l
# 结束，于是"spec 被删"这种形态在 GUI 侧也会响亮变红，而不是静默成功。
# 提交的 1 与 spec 默认值同值，所以期望消息仍是 100/176 ⇒ 数值断言不因这条而改变。
#
# fix round 1（F4）WHY 模式里锚定 "EVI computed"：原来只匹配 "0\.568182, 0\.568182"，
# 而 "SAVI computed: range [0.568182, 0.568182]" 同样命中 —— 数值锁挂在别的动作上也照绿。
# 注意 CMake 的 MATCHES 里 \[ 匹配不到字面方括号，所以用 .* 跨过 "range ["。
gis_gui_read_status_file("GUI spindex EVI regression"
    "${STATUS_PATH}" evi_success evi_cancelled evi_message evi_raw_message)
if(NOT evi_raw_message MATCHES "EVI computed.*0\\.568182, 0\\.568182")
    message(FATAL_ERROR
        "GUI 提交的 EVI 必须是标准 L=1 的 100/176（0.568182），实际消息: ${evi_raw_message}")
endif()


