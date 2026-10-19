if(NOT DEFINED GUI_PATH OR NOT DEFINED INPUT_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_defaults.cmake")
# N101 + N102：注入载体（set(ENV{GIS_TOOL_SETTINGS_INI} / GIS_CUSTOM_INDEX_PRESET_FILE)）做在
# gui_regression_helpers.cmake 的 **include 期顶层**，而 execute_process 没有 ENVIRONMENT 参数
# ⇒ 本文件必须在**第一次 execute_process 之前** include 它，否则被 spawn 的 gis-gui 拿不到私有落点。
# 这条"include 早于第一次 spawn"的纪律是本 driver 自己必须守的。上面那行 defaults 是显式冗余
# （helpers 第一行就 include 它），留着是因为本文件自己就要用 defaults 的产出：第一次
# execute_process 的 `-platform ${GUI_PLATFORM}`。B7 之后 defaults 里那条"按平台名跳过截图判定"
# 已经不存在了，直接把依赖写在用它的文件里，而不是转手交给 include 链。
# 现跑命令（行首锚定 ⇒ 注释行不会被数进去；数字归报告，不进注释）：
#   还没 include 载体的 driver：grep -rLE "^[^#]*gui_regression_helpers" tests/run_*.cmake
#   已经 include 的 driver 数：  grep -rlE "^[^#]*gui_regression_helpers" tests/run_*.cmake | wc -l
include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")

get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
# B7：这条腿原来只交一张截图、没有任何 status 通道 ⇒ 内容层无从判起。现在把 status 一起带上，
#   截图判据才能拿"渲染器自报的帧宽高/色数"去和文件自己的 IHDR 交叉比对。
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")

execute_process(
    COMMAND "${GUI_PATH}"
        -platform ${GUI_PLATFORM}
        --select-plugin vector
        --select-action convert
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "format=GeoJSON"
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
        "GUI regression failed with exit code ${GUI_EXIT_CODE}\n"
        "stdout:\n${GUI_STDOUT}\n"
        "stderr:\n${GUI_STDERR}")
endif()

if(NOT EXISTS "${OUTPUT_PATH}")
    message(FATAL_ERROR "GUI regression did not produce output: ${OUTPUT_PATH}")
endif()

file(SIZE "${OUTPUT_PATH}" OUTPUT_SIZE)
if(OUTPUT_SIZE EQUAL 0)
    message(FATAL_ERROR "GUI regression produced an empty output file: ${OUTPUT_PATH}")
endif()

# 截图两层判据（N98 / 批次 B 的 B7）：结构层独立解 IHDR 宽高，内容层判自报色数并与结构层交叉比对。
gis_gui_assert_screenshot_rendered("GUI regression" "${SCREENSHOT_PATH}" "${STATUS_PATH}")




