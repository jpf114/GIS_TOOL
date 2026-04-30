if(NOT DEFINED GUI_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI matching stitch debug failure regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")
set(INPUT_PATH_A "${OUTPUT_DIR}/stitch_a.bmp")
set(INPUT_PATH_B "${OUTPUT_DIR}/stitch_b.bmp")
gis_gui_generate_test_bmp("${INPUT_PATH_A}")
gis_gui_generate_test_bmp("${INPUT_PATH_B}")

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin matching
        --select-action stitch
        --set-param "input=${INPUT_PATH_A},${INPUT_PATH_B}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "stitch_confidence=0.5"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_failure_result(
    "GUI matching stitch debug failure regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}"
    "stitch is unavailable in Debug lightweight mode")
