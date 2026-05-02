if(NOT DEFINED GUI_PATH OR NOT DEFINED OUTPUT_PATH OR NOT DEFINED SCREENSHOT_PATH)
    message(FATAL_ERROR "Missing required GUI raster-tools cog regression arguments.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/gui_regression_helpers.cmake")
get_filename_component(OUTPUT_DIR "${OUTPUT_PATH}" DIRECTORY)
set(STATUS_PATH "${OUTPUT_DIR}/status.json")
gis_gui_prepare_aux_paths("${SCREENSHOT_PATH}" "${STATUS_PATH}")
file(REMOVE "${OUTPUT_PATH}")
set(INPUT_PATH "${OUTPUT_DIR}/raster_tools_cog_input.tif")
gis_gui_generate_test_tiff("${INPUT_PATH}")

execute_process(
    COMMAND "${GUI_PATH}"
        -platform offscreen
        --select-plugin raster_tools
        --select-action cog
        --set-param "input=${INPUT_PATH}"
        --auto-execute
        --quit-on-finish
        --screenshot "${SCREENSHOT_PATH}"
        --status-file "${STATUS_PATH}"
    RESULT_VARIABLE GUI_EXIT_CODE
    OUTPUT_VARIABLE GUI_STDOUT
    ERROR_VARIABLE GUI_STDERR
)

gis_gui_assert_status_only_success(
    "GUI raster-tools cog regression"
    "${GUI_EXIT_CODE}"
    "${GUI_STDOUT}"
    "${GUI_STDERR}"
    "${SCREENSHOT_PATH}"
    "${STATUS_PATH}"
    "${INPUT_PATH}")

file(GLOB generated_tifs "${OUTPUT_DIR}/*.tif" "${OUTPUT_DIR}/*.tiff")
set(found_output "")
foreach(candidate IN LISTS generated_tifs)
    if(NOT candidate STREQUAL INPUT_PATH)
        set(found_output "${candidate}")
        break()
    endif()
endforeach()

if(found_output STREQUAL "")
    message(FATAL_ERROR "GUI raster-tools cog regression did not produce any derived COG output file.")
endif()

file(SIZE "${found_output}" output_size)
if(output_size EQUAL 0)
    message(FATAL_ERROR "GUI raster-tools cog regression produced an empty output file: ${found_output}")
endif()
