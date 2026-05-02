if(NOT DEFINED GUI_PATH OR NOT DEFINED ACTION OR NOT DEFINED DATASET OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI vector advanced regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")

set(INPUT_PATH "${OUTPUT_DIR}/input.geojson")
gis_gui_write_geojson_dataset("${INPUT_PATH}" "${DATASET}")

if(DEFINED TARGET_DATASET)
    set(TARGET_PATH "${OUTPUT_DIR}/target.geojson")
    gis_gui_write_geojson_dataset("${TARGET_PATH}" "${TARGET_DATASET}")
endif()

set(command_args
    "${GUI_PATH}"
    -platform offscreen
    --select-plugin vector
    --select-action "${ACTION}"
    --set-param "input=${INPUT_PATH}"
    --set-param "output=${OUTPUT_PATH}"
)

if(DEFINED TARGET_DATASET)
    if(ACTION STREQUAL "nearest")
        list(APPEND command_args --set-param "nearest_vector=${TARGET_PATH}")
    elseif(ACTION STREQUAL "spatial_join")
        list(APPEND command_args --set-param "join_vector=${TARGET_PATH}")
    endif()
endif()

if(DEFINED EXTRA_PARAMS AND NOT EXTRA_PARAMS STREQUAL "")
    string(REPLACE "|" ";" extra_param_list "${EXTRA_PARAMS}")
    foreach(param_entry IN LISTS extra_param_list)
        list(APPEND command_args --set-param "${param_entry}")
    endforeach()
endif()

list(APPEND command_args
    --auto-execute
    --quit-on-finish
    --screenshot "${SCREENSHOT_PATH}"
    --status-file "${STATUS_PATH}"
)

execute_process(
    COMMAND ${command_args}
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_regression_result(
    "GUI vector advanced regression (${ACTION})"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")
