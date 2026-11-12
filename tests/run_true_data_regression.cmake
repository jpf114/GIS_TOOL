# Track B true-data leg wrapper.
#
# Fixtures come from make_true_data_fixtures.py (GDAL CLI only, no GIS_TOOL algorithm) and
# independent expectations from data/true_expected.json. A missing real-data directory is an
# explicit Skip, never a fake green; a missing oracle/provenance is a hard failure.

if(NOT DEFINED PY OR PY STREQUAL "")
    message(FATAL_ERROR "true-data guard: -DPY is required")
endif()
if(NOT DEFINED CLI_PATH OR CLI_PATH STREQUAL "" OR NOT EXISTS "${CLI_PATH}")
    message(FATAL_ERROR "true-data guard: -DCLI_PATH must point to an existing gis-cli")
endif()
if(NOT DEFINED REPO_ROOT OR REPO_ROOT STREQUAL "")
    message(FATAL_ERROR "true-data guard: -DREPO_ROOT is required")
endif()
if(NOT DEFINED OUTPUT_ROOT OR OUTPUT_ROOT STREQUAL "")
    message(FATAL_ERROR "true-data guard: -DOUTPUT_ROOT is required")
endif()
if(NOT DEFINED GROUP OR GROUP STREQUAL "")
    message(FATAL_ERROR "true-data guard: -DGROUP is required")
endif()

if(GROUP STREQUAL "raster")
    set(_fixture_dir "${REPO_ROOT}/data/true_raster")
elseif(GROUP STREQUAL "matching")
    set(_fixture_dir "${REPO_ROOT}/data/true_matching")
else()
    message(FATAL_ERROR "true-data guard: unknown GROUP '${GROUP}'")
endif()

# Use a track-B-only Skip token: the existing GIS_SKIP_TESTDATA_ABSENT token is deliberately
# restricted to the three run_real_*.cmake wrappers (see tests/CMakeLists.txt gate notes), so a
# fourth printer there would widen that contract. Missing interpreter/CLI stays FATAL, not Skip.
if(NOT EXISTS "${_fixture_dir}")
    message("GIS_SKIP_TRUE_DATA_ABSENT: ${_fixture_dir}")
    return()
endif()

foreach(_required "${REPO_ROOT}/data/true_expected.json"
                  "${REPO_ROOT}/data/provenance.json"
                  "${REPO_ROOT}/tests/run_true_data_regression.py")
    if(NOT EXISTS "${_required}")
        message(FATAL_ERROR "true-data guard: missing ${_required}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT_ROOT}")

execute_process(
    COMMAND "${PY}" "${REPO_ROOT}/tests/run_true_data_regression.py"
        --cli "${CLI_PATH}"
        --repo "${REPO_ROOT}"
        --out-root "${OUTPUT_ROOT}"
        --group "${GROUP}"
    RESULT_VARIABLE _true_rc
)
if(NOT _true_rc EQUAL 0)
    message(FATAL_ERROR
        "true_data_${GROUP} failed (rc=${_true_rc}); look for TRUE_DATA_FAIL in the output above")
endif()
