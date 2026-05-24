# Default Qt platform for GUI regression scripts.
# Must match the headless plugin copied beside gis_tests (see tests/CMakeLists.txt).
if(NOT DEFINED GUI_PLATFORM)
    set(GUI_PLATFORM offscreen)
endif()

function(gis_gui_platform_skips_screenshot out_var)
    if(DEFINED GUI_PLATFORM AND (GUI_PLATFORM STREQUAL "minimal" OR GUI_PLATFORM STREQUAL "offscreen"))
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()
