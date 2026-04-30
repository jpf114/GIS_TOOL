if(NOT DEFINED GUI_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI cutting mosaic regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")
set(INPUT_PATH_A "${OUTPUT_DIR}/mosaic_input_a.tif")
set(INPUT_PATH_B "${OUTPUT_DIR}/mosaic_input_b.tif")
set(PREP_STATUS_A "${OUTPUT_DIR}/prep_a_status.json")
file(REMOVE "${PREP_STATUS_A}")

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin vector
        --select-action rasterize
        --set-param "input=${CMAKE_CURRENT_LIST_DIR}/data/gui_vector_overlay_input.geojson"
        --set-param "output=${INPUT_PATH_A}"
        --set-param "resolution=50"
        --auto-execute
        --quit-on-finish
        --status-file "${PREP_STATUS_A}"
    RESULT_VARIABLE PREP_A_EXIT_CODE
    OUTPUT_VARIABLE PREP_A_STDOUT
    ERROR_VARIABLE PREP_A_STDERR
)
if(NOT "${PREP_A_EXIT_CODE}" STREQUAL "0" OR NOT EXISTS "${INPUT_PATH_A}")
    message(FATAL_ERROR
        "Failed to prepare raster input A for GUI cutting mosaic regression\n"
        "exit: ${PREP_A_EXIT_CODE}\nstdout:\n${PREP_A_STDOUT}\nstderr:\n${PREP_A_STDERR}")
endif()
file(COPY_FILE "${INPUT_PATH_A}" "${INPUT_PATH_B}")

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin cutting
        --select-action mosaic
        --set-param "input=${INPUT_PATH_A},${INPUT_PATH_B}"
        --set-param "output=${OUTPUT_PATH}"
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
    "GUI cutting mosaic regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")
