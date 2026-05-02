if(NOT DEFINED GUI_PATH OR NOT DEFINED GUI_TEST_DATA_HELPER_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI terrain cut fill regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
set(INPUT_PATH "${OUTPUT_DIR}/terrain_cut_fill_input.tif")
set(REFERENCE_PATH "${OUTPUT_DIR}/terrain_cut_fill_reference.tif")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")

execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" terrain-raster "${INPUT_PATH}"
    RESULT_VARIABLE PREP_INPUT_EXIT_CODE
    OUTPUT_VARIABLE PREP_INPUT_STDOUT
    ERROR_VARIABLE PREP_INPUT_STDERR
)
if(NOT "${PREP_INPUT_EXIT_CODE}" STREQUAL "0" OR NOT EXISTS "${INPUT_PATH}")
    message(FATAL_ERROR
        "Failed to prepare terrain cut fill input\n"
        "exit: ${PREP_INPUT_EXIT_CODE}\nstdout:\n${PREP_INPUT_STDOUT}\nstderr:\n${PREP_INPUT_STDERR}")
endif()

execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" terrain-raster "${REFERENCE_PATH}"
    RESULT_VARIABLE PREP_REFERENCE_EXIT_CODE
    OUTPUT_VARIABLE PREP_REFERENCE_STDOUT
    ERROR_VARIABLE PREP_REFERENCE_STDERR
)
if(NOT "${PREP_REFERENCE_EXIT_CODE}" STREQUAL "0" OR NOT EXISTS "${REFERENCE_PATH}")
    message(FATAL_ERROR
        "Failed to prepare terrain cut fill reference\n"
        "exit: ${PREP_REFERENCE_EXIT_CODE}\nstdout:\n${PREP_REFERENCE_STDOUT}\nstderr:\n${PREP_REFERENCE_STDERR}")
endif()

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin terrain
        --select-action cut_fill
        --set-param "reference=${REFERENCE_PATH}"
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
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
    "GUI terrain cut fill regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")
