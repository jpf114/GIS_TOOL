if(NOT DEFINED GUI_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI matching match regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")
set(REFERENCE_PATH "${OUTPUT_DIR}/matching_ref.bmp")
set(INPUT_PATH "${OUTPUT_DIR}/matching_input.bmp")
gis_gui_generate_feature_bmp("${REFERENCE_PATH}" 128 128)
file(COPY_FILE "${REFERENCE_PATH}" "${INPUT_PATH}")

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin matching
        --select-action match
        --set-param "reference=${REFERENCE_PATH}"
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "method=orb"
        --set-param "match_method=bf"
        --set-param "max_points=100"
        --set-param "ratio_test=0.9"
        --set-param "band=1"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_regression_result(
    "GUI matching match regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")
