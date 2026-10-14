# Debug-only GUI regressions assert Debug lightweight failure paths.
macro(gis_gui_skip_unless_debug)
    set(_gis_test_config "")
    if(DEFINED GIS_TEST_BUILD_CONFIG)
        set(_gis_test_config "${GIS_TEST_BUILD_CONFIG}")
    elseif(DEFINED CTEST_CONFIGURATION_TYPE)
        set(_gis_test_config "${CTEST_CONFIGURATION_TYPE}")
    endif()
    if(_gis_test_config AND NOT _gis_test_config STREQUAL "Debug")
        # Hard failure, not "print a line and return() success": skipping a debug-only
        # regression used to look exactly like passing one. The exclusion belongs to
        # add_test(... CONFIGURATIONS Debug) in tests/CMakeLists.txt; this macro is the
        # defence-in-depth behind it, so a future debug-only case that forgets that option
        # fails here instead of faking a green in a non-Debug configuration.
        message(FATAL_ERROR
            "Debug-only GUI regression reached a non-Debug configuration (${_gis_test_config}). "
            "This case asserts Debug lightweight failure paths, so it must not run - and it must "
            "not pass - here. Register the test with add_test(... CONFIGURATIONS Debug) instead "
            "of letting it pass.")
    endif()
    # Unknown configuration (_gis_test_config empty) keeps the previous behaviour and falls
    # through to the Debug path: ctest invoked without -C already refuses to run any case of
    # this multi-config tree and counts it as Not Run, so failing here would add noise
    # instead of signal.
endmacro()
