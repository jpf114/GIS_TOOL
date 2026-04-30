if(NOT DEFINED GUI_PATH OR NOT DEFINED INPUT_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI vector buffer regression arguments.")
endif()

get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
get_filename_component(SCREENSHOT_DIR "${SCREENSHOT_PATH}" DIRECTORY)

file(REMOVE "${OUTPUT_PATH}" "${SCREENSHOT_PATH}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${SCREENSHOT_DIR}")

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin vector
        --select-action buffer
        --set-param "input=${INPUT_PATH}"
        --set-param "output=${OUTPUT_PATH}"
        --set-param "distance=50"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

if(NOT GUI_EXIT_CODE EQUAL 0)
    message(FATAL_ERROR
        "GUI vector buffer regression failed with exit code ${GUI_EXIT_CODE}\n"
        "stdout:\n${GUI_STDOUT}\n"
        "stderr:\n${GUI_STDERR}")
endif()

if(NOT EXISTS "${OUTPUT_PATH}")
    message(FATAL_ERROR "GUI vector buffer regression did not produce output: ${OUTPUT_PATH}")
endif()

file(SIZE "${OUTPUT_PATH}" OUTPUT_SIZE)
if(OUTPUT_SIZE EQUAL 0)
    message(FATAL_ERROR "GUI vector buffer regression produced an empty output file: ${OUTPUT_PATH}")
endif()

if(NOT EXISTS "${SCREENSHOT_PATH}")
    message(FATAL_ERROR "GUI vector buffer regression did not produce screenshot: ${SCREENSHOT_PATH}")
endif()

file(SIZE "${SCREENSHOT_PATH}" SCREENSHOT_SIZE)
if(SCREENSHOT_SIZE EQUAL 0)
    message(FATAL_ERROR "GUI vector buffer regression produced an empty screenshot: ${SCREENSHOT_PATH}")
endif()
