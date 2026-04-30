if(NOT DEFINED GUI_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI matching ECC-register debug failure regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")
set(REFERENCE_PATH "${OUTPUT_DIR}/ecc_ref.bmp")
set(INPUT_PATH "${OUTPUT_DIR}/ecc_input.bmp")
gis_gui_generate_test_bmp("${REFERENCE_PATH}")
gis_gui_generate_test_bmp("${INPUT_PATH}")

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin matching
        --select-action ecc_register
        --set-param "reference=${REFERENCE_PATH}"
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "ecc_motion=affine"
        --set-param "ecc_iterations=50"
        --set-param "ecc_epsilon=0.0001"
        --set-param "band=1"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_failure_result(
    "GUI matching ECC-register debug failure regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}"
    "ecc_register is unavailable in Debug lightweight mode")
