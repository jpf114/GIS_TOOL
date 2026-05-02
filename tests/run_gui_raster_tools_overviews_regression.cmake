if(NOT DEFINED GUI_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI raster-tools overviews regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(SCREENSHOT_DIR "${SCREENSHOT_PATH}" DIRECTORY)
set(OUTPUT_DIR "${SCREENSHOT_DIR}")
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
set(INPUT_PATH "${OUTPUT_DIR}/raster_tools_overviews_input.tif")
gis_gui_prepare_aux_paths("${SCREENSHOT_PATH}" "${STATUS_PATH}")
gis_gui_generate_test_tiff("${INPUT_PATH}")

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin raster_tools
        --select-action overviews
        --set-param "input=${INPUT_PATH}"
        --set-param "levels=2 4"
        --set-param "resample=nearest"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_status_only_success(
    "GUI raster-tools overviews regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}"
    "${INPUT_PATH}")
