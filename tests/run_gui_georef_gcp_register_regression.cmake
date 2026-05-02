if(NOT DEFINED GUI_PATH OR NOT DEFINED GUI_TEST_DATA_HELPER_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI georef GCP regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
set(INPUT_PATH "${OUTPUT_DIR}/gcp_input.tif")
set(GCP_PATH "${OUTPUT_DIR}/gcps.csv")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")

execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" georef-gcp-inputs "${INPUT_PATH}" "${GCP_PATH}"
    RESULT_VARIABLE PREP_EXIT_CODE
    OUTPUT_VARIABLE PREP_STDOUT
    ERROR_VARIABLE PREP_STDERR
)
if(NOT "${PREP_EXIT_CODE}" STREQUAL "0" OR NOT EXISTS "${INPUT_PATH}" OR NOT EXISTS "${GCP_PATH}")
    message(FATAL_ERROR
        "Failed to prepare GCP inputs\n"
        "exit: ${PREP_EXIT_CODE}\nstdout:\n${PREP_STDOUT}\nstderr:\n${PREP_STDERR}")
endif()

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin georef
        --select-action gcp_register
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "gcp_file=${GCP_PATH}"
        --set-param "dst_srs=EPSG:4326"
        --set-param "resample=nearest"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_regression_result(
    "GUI georef GCP regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")
