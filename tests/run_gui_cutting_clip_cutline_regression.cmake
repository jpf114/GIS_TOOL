if(NOT DEFINED GUI_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH OR
   NOT DEFINED GUI_TEST_DATA_HELPER_PATH)
    message(FATAL_ERROR "Missing required GUI cutting clip cutline regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
set(INPUT_PATH "${OUTPUT_DIR}/cutting_clip_cutline_input.tif")
set(PREP_STATUS_PATH "${OUTPUT_DIR}/prep_status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")
file(REMOVE "${INPUT_PATH}" "${INPUT_PATH}.aux.xml")
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
        "Failed to prepare raster input for GUI cutting clip cutline regression\n"
        "exit: ${PREP_EXIT_CODE}\nstdout:\n${PREP_STDOUT}\nstderr:\n${PREP_STDERR}")
endif()

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin cutting
        --select-action clip
        --set-param "input=${INPUT_PATH}"
        --set-param "cutline=${CMAKE_CURRENT_LIST_DIR}/data/gui_vector_overlay_mask.geojson"
        --set-param "output=${OUTPUT_PATH}"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_regression_result(
    "GUI cutting clip cutline regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")

execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" raster-size "${INPUT_PATH}"
    RESULT_VARIABLE INPUT_SIZE_EXIT_CODE
    OUTPUT_VARIABLE INPUT_SIZE_STDOUT
    ERROR_VARIABLE INPUT_SIZE_STDERR
)
execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" raster-size "${OUTPUT_PATH}"
    RESULT_VARIABLE OUTPUT_SIZE_EXIT_CODE
    OUTPUT_VARIABLE OUTPUT_SIZE_STDERR_OUT
    ERROR_VARIABLE OUTPUT_SIZE_STDERR
)

if(NOT "${INPUT_SIZE_EXIT_CODE}" STREQUAL "0" OR NOT "${OUTPUT_SIZE_EXIT_CODE}" STREQUAL "0")
    message(FATAL_ERROR
        "GUI cutting clip cutline regression failed to inspect raster size\n"
        "input stdout:\n${INPUT_SIZE_STDOUT}\ninput stderr:\n${INPUT_SIZE_STDERR}\n"
        "output stdout:\n${OUTPUT_SIZE_STDERR_OUT}\noutput stderr:\n${OUTPUT_SIZE_STDERR}")
endif()

string(STRIP "${INPUT_SIZE_STDOUT}" INPUT_SIZE)
string(STRIP "${OUTPUT_SIZE_STDERR_OUT}" OUTPUT_SIZE)
if(INPUT_SIZE STREQUAL OUTPUT_SIZE)
    message(FATAL_ERROR
        "GUI cutting clip cutline regression expected output raster size to change, but both are ${INPUT_SIZE}")
endif()
