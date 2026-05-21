if(NOT DEFINED GUI_PATH OR NOT DEFINED INPUT_PATH OR NOT DEFINED OUTPUT_PATH OR
   NOT DEFINED SCREENSHOT_PATH OR NOT DEFINED GUI_TEST_DATA_HELPER_PATH)
    message(FATAL_ERROR "Missing required GUI vector filter extent regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")

if(NOT DEFINED GUI_PLATFORM)
    set(GUI_PLATFORM minimal)
endif()

execute_process(
    COMMAND "${GUI_PATH}"
        -platform ${GUI_PLATFORM}
        --select-plugin vector
        --select-action filter
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "extent=116.10,39.50,117.00,39.95"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_regression_result(
    "GUI vector filter extent regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")

if(NOT DEFINED GUI_PLATFORM)
    set(GUI_PLATFORM minimal)
endif()

execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" vector-feature-count "${OUTPUT_PATH}"
    RESULT_VARIABLE FEATURE_COUNT_EXIT_CODE
    OUTPUT_VARIABLE FEATURE_COUNT_STDOUT
    ERROR_VARIABLE FEATURE_COUNT_STDERR
)

if(NOT "${FEATURE_COUNT_EXIT_CODE}" STREQUAL "0")
    message(FATAL_ERROR
        "GUI vector filter extent regression failed to inspect output feature count\n"
        "stdout:\n${FEATURE_COUNT_STDOUT}\n"
        "stderr:\n${FEATURE_COUNT_STDERR}")
endif()

string(STRIP "${FEATURE_COUNT_STDOUT}" FEATURE_COUNT)
if(NOT FEATURE_COUNT STREQUAL "1")
    message(FATAL_ERROR
        "GUI vector filter extent regression expected 1 feature, got ${FEATURE_COUNT}")
endif()



