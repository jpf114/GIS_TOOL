if(NOT DEFINED GUI_PATH OR NOT DEFINED GUI_TEST_DATA_HELPER_PATH OR NOT DEFINED ACTION OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI georef topographic regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
set(INPUT_PATH "${OUTPUT_DIR}/topographic_input.tif")
set(SLOPE_PATH "${OUTPUT_DIR}/topographic_slope.tif")
set(ASPECT_PATH "${OUTPUT_DIR}/topographic_aspect.tif")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")

execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" georef-topographic-inputs "${INPUT_PATH}" "${SLOPE_PATH}" "${ASPECT_PATH}"
    RESULT_VARIABLE PREP_EXIT_CODE
    OUTPUT_VARIABLE PREP_STDOUT
    ERROR_VARIABLE PREP_STDERR
)
if(NOT "${PREP_EXIT_CODE}" STREQUAL "0" OR NOT EXISTS "${INPUT_PATH}" OR NOT EXISTS "${SLOPE_PATH}" OR NOT EXISTS "${ASPECT_PATH}")
    message(FATAL_ERROR
        "Failed to prepare topographic correction inputs\n"
        "exit: ${PREP_EXIT_CODE}\nstdout:\n${PREP_STDOUT}\nstderr:\n${PREP_STDERR}")
endif()

set(EXTRA_PARAM_NAME "")
set(EXTRA_PARAM_VALUE "")
if(ACTION STREQUAL "minnaert_correction")
    set(EXTRA_PARAM_NAME "minnaert_k")
    set(EXTRA_PARAM_VALUE "0.5")
elseif(ACTION STREQUAL "c_correction")
    set(EXTRA_PARAM_NAME "c_value")
    set(EXTRA_PARAM_VALUE "0.1")
endif()

set(COMMAND_ARGS
    "${GUI_PATH}"
    -platform offscreen
    --select-plugin georef
    --select-action "${ACTION}"
    --set-param "input=${INPUT_PATH}"
    --set-param "output=${OUTPUT_PATH}"
    --set-param "band=1"
    --set-param "slope_raster=${SLOPE_PATH}"
    --set-param "aspect_raster=${ASPECT_PATH}"
    --set-param "sun_zenith_deg=30"
    --set-param "sun_azimuth_deg=180")
if(NOT EXTRA_PARAM_NAME STREQUAL "")
    list(APPEND COMMAND_ARGS --set-param "${EXTRA_PARAM_NAME}=${EXTRA_PARAM_VALUE}")
endif()
list(APPEND COMMAND_ARGS
    --auto-execute
    --quit-on-finish
    --screenshot "${SCREENSHOT_PATH}"
    --status-file "${STATUS_PATH}")

execute_process(
    COMMAND ${COMMAND_ARGS}
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_regression_result(
    "GUI georef topographic regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")
