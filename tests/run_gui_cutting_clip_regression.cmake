if(NOT DEFINED GUI_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI cutting clip regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")
set(INPUT_PATH "${OUTPUT_DIR}/cutting_clip_input.tif")
set(PREP_STATUS_PATH "${OUTPUT_DIR}/prep_status.json")
file(REMOVE "${PREP_STATUS_PATH}")

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin vector
        --select-action rasterize
        --set-param "input=${CMAKE_CURRENT_LIST_DIR}/data/gui_vector_overlay_input.geojson"
        --set-param "output=${INPUT_PATH}"
        --set-param "resolution=50"
        --auto-execute
        --quit-on-finish
        --status-file "${PREP_STATUS_PATH}"
    RESULT_VARIABLE PREP_EXIT_CODE
    OUTPUT_VARIABLE PREP_STDOUT
    ERROR_VARIABLE PREP_STDERR
)
if(NOT "${PREP_EXIT_CODE}" STREQUAL "0" OR NOT EXISTS "${INPUT_PATH}")
    message(FATAL_ERROR
        "Failed to prepare raster input for GUI cutting clip regression\n"
        "exit: ${PREP_EXIT_CODE}\nstdout:\n${PREP_STDOUT}\nstderr:\n${PREP_STDERR}")
endif()

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin cutting
        --select-action clip
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "extent=12935050,4852050,12935250,4852250"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_regression_result(
    "GUI cutting clip regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")
