if(NOT DEFINED GUI_PATH OR NOT DEFINED INPUT_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI vector buffer regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_defaults.cmake")
# N101：本 driver 属于"不 include helpers"的那 4 个之一，而注入载体做在 helpers 的 include 期顶层
# ⇒ 必须在第一次 execute_process 之前 include 它（helpers 会自己再 include defaults）。
include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")

get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
# B7：这条腿原来只交一张截图、没有 status 通道 ⇒ 内容层无从判起；现在把 status 一起带上，
#   截图判据才能拿"渲染器自报的帧宽高/色数"与文件自己的 IHDR 交叉比对。
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")

if(NOT DEFINED GUI_PLATFORM)
    set(GUI_PLATFORM offscreen)
endif()

execute_process(
    COMMAND "${GUI_PATH}"
        -platform ${GUI_PLATFORM}
        --select-plugin vector
        --select-action buffer
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "distance=50"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

if(NOT GUI_EXIT_CODE EQUAL 0)
    message(FATAL_ERROR
        "GUI vector buffer regression failed with exit code ${GUI_EXIT_CODE}\n"
        "stdout:\n${GUI_STDOUT}\n"
        "stderr:\n${GUI_STDERR}")
endif()

if(NOT EXISTS "${OUTPUT_PATH}")
    message(FATAL_ERROR "GUI vector buffer regression did not produce output: ${OUTPUT_PATH}")
endif()

file(SIZE "${OUTPUT_PATH}" OUTPUT_SIZE)
if(OUTPUT_SIZE EQUAL 0)
    message(FATAL_ERROR "GUI vector buffer regression produced an empty output file: ${OUTPUT_PATH}")
endif()

# 截图两层判据（N98 / 批次 B 的 B7）：结构层独立解 IHDR 宽高，内容层判自报色数并与结构层交叉比对。
gis_gui_assert_screenshot_rendered("GUI vector buffer regression" "${SCREENSHOT_PATH}" "${STATUS_PATH}")




