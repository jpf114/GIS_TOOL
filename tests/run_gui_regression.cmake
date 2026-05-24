if(NOT DEFINED GUI_PATH OR NOT DEFINED INPUT_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_defaults.cmake")

get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
get_filename_component(SCREENSHOT_DIR "${SCREENSHOT_PATH}" DIRECTORY)

file(REMOVE "${OUTPUT_PATH}" "${SCREENSHOT_PATH}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${SCREENSHOT_DIR}")

execute_process(
    COMMAND "${GUI_PATH}"
        -platform ${GUI_PLATFORM}
        --select-plugin vector
        --select-action convert
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "format=GeoJSON"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

if(NOT GUI_EXIT_CODE EQUAL 0)
    message(FATAL_ERROR
        "GUI regression failed with exit code ${GUI_EXIT_CODE}\n"
        "stdout:\n${GUI_STDOUT}\n"
        "stderr:\n${GUI_STDERR}")
endif()

if(NOT EXISTS "${OUTPUT_PATH}")
    message(FATAL_ERROR "GUI regression did not produce output: ${OUTPUT_PATH}")
endif()

file(SIZE "${OUTPUT_PATH}" OUTPUT_SIZE)
if(OUTPUT_SIZE EQUAL 0)
    message(FATAL_ERROR "GUI regression produced an empty output file: ${OUTPUT_PATH}")
endif()

set(_skip_screenshot FALSE)
gis_gui_platform_skips_screenshot(_skip_screenshot)
if(NOT _skip_screenshot)
    if(NOT EXISTS "${SCREENSHOT_PATH}")
        message(FATAL_ERROR "GUI regression did not produce screenshot: ${SCREENSHOT_PATH}")
    endif()

    file(SIZE "${SCREENSHOT_PATH}" SCREENSHOT_SIZE)
    if(SCREENSHOT_SIZE EQUAL 0)
        message(FATAL_ERROR "GUI regression produced an empty screenshot: ${SCREENSHOT_PATH}")
    endif()
endif()




