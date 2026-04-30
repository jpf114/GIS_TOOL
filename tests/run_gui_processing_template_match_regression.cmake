if(NOT DEFINED GUI_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI processing template-match regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")
set(INPUT_PATH "${OUTPUT_DIR}/template_source.bmp")
set(TEMPLATE_PATH "${OUTPUT_DIR}/template_patch.bmp")
gis_gui_generate_test_bmp_with_size("${INPUT_PATH}" 64 64)
gis_gui_generate_test_bmp_with_size("${TEMPLATE_PATH}" 16 16)

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin processing
        --select-action template_match
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "band=1"
        --set-param "template_file=${TEMPLATE_PATH}"
        --set-param "match_method=ccoeff_normed"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_regression_result(
    "GUI processing template-match regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")
