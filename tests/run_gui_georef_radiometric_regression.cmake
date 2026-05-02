if(NOT DEFINED GUI_PATH OR NOT DEFINED GUI_TEST_DATA_HELPER_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI georef radiometric regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
set(INPUT_PATH "${OUTPUT_DIR}/radiometric_input.tif")
set(METADATA_PATH "${OUTPUT_DIR}/radiometric_metadata.txt")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")

execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" georef-radiometric-inputs "${INPUT_PATH}" "${METADATA_PATH}"
    RESULT_VARIABLE PREP_EXIT_CODE
    OUTPUT_VARIABLE PREP_STDOUT
    ERROR_VARIABLE PREP_STDERR
)
if(NOT "${PREP_EXIT_CODE}" STREQUAL "0" OR NOT EXISTS "${INPUT_PATH}" OR NOT EXISTS "${METADATA_PATH}")
    message(FATAL_ERROR
        "Failed to prepare radiometric inputs\n"
        "exit: ${PREP_EXIT_CODE}\nstdout:\n${PREP_STDOUT}\nstderr:\n${PREP_STDERR}")
endif()

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin georef
        --select-action radiometric_calibration
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "band=1"
        --set-param "metadata_file=${METADATA_PATH}"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_regression_result(
    "GUI georef radiometric regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")
