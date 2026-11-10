# docs_prose_claims：对外文档关键事实守卫的 CMake 载体。
# 结构与 check_docs_match_cli.cmake 一致：解释器/仓库根缺失一律 FATAL，
# 先跑 --self-test 证明判据能变红，再跑真检查；不捕获真检查的 stdout（中文码页二次编码）。

if(NOT DEFINED PY OR PY STREQUAL "")
    message(FATAL_ERROR
        "docs_prose_claims: -DPY is empty => no usable Python was resolved at configure time. "
        "A guard that cannot run must not look green.")
endif()
if(NOT DEFINED REPO_ROOT OR REPO_ROOT STREQUAL "")
    message(FATAL_ERROR "docs_prose_claims: -DREPO_ROOT=<repo root> is required")
endif()

set(_py "${CMAKE_CURRENT_LIST_DIR}/check_docs_prose_claims.py")
if(NOT EXISTS "${_py}")
    message(FATAL_ERROR "docs_prose_claims: missing judge script ${_py}")
endif()

execute_process(
    COMMAND "${PY}" "${_py}" --self-test
    RESULT_VARIABLE _rc_self
    OUTPUT_VARIABLE _out_self
    ERROR_VARIABLE _err_self
)
if(NOT _rc_self EQUAL 0)
    message(FATAL_ERROR
        "docs_prose_claims SELF-TEST failed (rc=${_rc_self}): a judge that cannot go red must not be "
        "trusted when it is green.\nstdout: ${_out_self}\nstderr: ${_err_self}")
endif()

execute_process(
    COMMAND "${PY}" "${_py}" --root "${REPO_ROOT}"
    RESULT_VARIABLE _rc
)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "docs_prose_claims failed (rc=${_rc}). stdout lines are prefixed DRIFT / MISSING: "
        "stale facts, hardcoded registration counts, missing current-status section or broken links.")
endif()
