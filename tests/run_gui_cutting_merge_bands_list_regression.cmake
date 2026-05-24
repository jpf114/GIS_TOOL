if(NOT DEFINED GUI_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH OR
   NOT DEFINED GUI_TEST_DATA_HELPER_PATH)
    message(FATAL_ERROR "Missing required GUI cutting merge-bands list regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_artifact_paths("${OUTPUT_PATH}" "${SCREENSHOT_PATH}" "${STATUS_PATH}")
set(INPUT_PATH_A "${OUTPUT_DIR}/merge_list_band_a.tif")
set(INPUT_PATH_B "${OUTPUT_DIR}/merge_list_band_b.tif")
set(INPUT_PATH_C "${OUTPUT_DIR}/merge_list_band_c.tif")

if(NOT DEFINED GUI_PLATFORM)
    set(GUI_PLATFORM offscreen)
endif()

execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" class-raster "${INPUT_PATH_A}"
    RESULT_VARIABLE PREP_A_EXIT_CODE
    OUTPUT_VARIABLE PREP_A_STDOUT
    ERROR_VARIABLE PREP_A_STDERR
)
if(NOT DEFINED GUI_PLATFORM)
    set(GUI_PLATFORM offscreen)
endif()

execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" class-raster "${INPUT_PATH_B}"
    RESULT_VARIABLE PREP_B_EXIT_CODE
    OUTPUT_VARIABLE PREP_B_STDOUT
    ERROR_VARIABLE PREP_B_STDERR
)
if(NOT DEFINED GUI_PLATFORM)
    set(GUI_PLATFORM offscreen)
endif()

execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" class-raster "${INPUT_PATH_C}"
    RESULT_VARIABLE PREP_C_EXIT_CODE
    OUTPUT_VARIABLE PREP_C_STDOUT
    ERROR_VARIABLE PREP_C_STDERR
)

if(NOT "${PREP_A_EXIT_CODE}" STREQUAL "0" OR
   NOT "${PREP_B_EXIT_CODE}" STREQUAL "0" OR
   NOT "${PREP_C_EXIT_CODE}" STREQUAL "0")
    message(FATAL_ERROR
        "GUI cutting merge-bands list regression failed to prepare single-band inputs\n"
        "A stdout:\n${PREP_A_STDOUT}\nA stderr:\n${PREP_A_STDERR}\n"
        "B stdout:\n${PREP_B_STDOUT}\nB stderr:\n${PREP_B_STDERR}\n"
        "C stdout:\n${PREP_C_STDOUT}\nC stderr:\n${PREP_C_STDERR}")
endif()

if(NOT DEFINED GUI_PLATFORM)
    set(GUI_PLATFORM offscreen)
endif()

execute_process(
    COMMAND "${GUI_PATH}"
        -platform ${GUI_PLATFORM}
        --select-plugin cutting
        --select-action merge_bands
        --set-param "input=${INPUT_PATH_A}"
        --set-param "bands=${INPUT_PATH_B},${INPUT_PATH_C}"
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
    "GUI cutting merge-bands list regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${OUTPUT_PATH}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}")

if(NOT DEFINED GUI_PLATFORM)
    set(GUI_PLATFORM offscreen)
endif()

execute_process(
    COMMAND "${GUI_TEST_DATA_HELPER_PATH}" raster-band-count "${OUTPUT_PATH}"
    RESULT_VARIABLE BAND_COUNT_EXIT_CODE
    OUTPUT_VARIABLE BAND_COUNT_STDOUT
    ERROR_VARIABLE BAND_COUNT_STDERR
)

if(NOT "${BAND_COUNT_EXIT_CODE}" STREQUAL "0")
    message(FATAL_ERROR
        "GUI cutting merge-bands list regression failed to inspect output band count\n"
        "stdout:\n${BAND_COUNT_STDOUT}\n"
        "stderr:\n${BAND_COUNT_STDERR}")
endif()

string(STRIP "${BAND_COUNT_STDOUT}" BAND_COUNT)
if(NOT BAND_COUNT STREQUAL "3")
    message(FATAL_ERROR
        "GUI cutting merge-bands list regression expected 3 bands, got ${BAND_COUNT}")
endif()



