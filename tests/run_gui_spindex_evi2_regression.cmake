if(NOT DEFINED GUI_PATH OR NOT DEFINED GUI_TEST_DATA_HELPER_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI spindex EVI2 regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")
set(INPUT_PATH "${OUTPUT_DIR}/evi2_input.tif")
execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" ndvi-raster "${INPUT_PATH}"
    RESULT_VARIABLE HELPER_EXIT_CODE
)
if(NOT "${HELPER_EXIT_CODE}" STREQUAL "0")
    message(FATAL_ERROR "Failed to generate EVI2 GUI test raster.")
endif()

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin spindex
        --select-action evi2
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "red_band=1"
        --set-param "nir_band=4"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_regression_result(
    "GUI spindex EVI2 regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")
