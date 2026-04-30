if(NOT DEFINED GUI_PATH OR NOT DEFINED OUTPUT_DIR OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI cutting split regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
set(INPUT_PATH "${OUTPUT_DIR}/split_input.tif")
file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
gis_gui_prepare_aux_paths("${SCREENSHOT_PATH}" "${STATUS_PATH}")
gis_gui_generate_test_tiff("${INPUT_PATH}")

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin cutting
        --select-action split
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_DIR}"
        --set-param "tile_size=8"
        --set-param "overlap=0"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_directory_success(
    "GUI cutting split regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_DIR}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")
