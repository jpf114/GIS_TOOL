# N162/#156 三载体 CLI 判据（output_dir_failure_guard）的 CMake 载体。
#
# 用法（复算口径，与 tests/CMakeLists.txt 里注册的用例同一件事）：
#   cmake -DPY=<python 绝对路径> -DCLI=<gis-cli 路径> -DREPO_ROOT=<仓库根> -P tests/check_output_dir_failure_guard.cmake
#   直跑判据本体：python tests/check_output_dir_failure_guard.py --self-test（合成输入的自证，不读仓库）
#
# 判据本体不在 CMake 里写：要跑 gis-cli 子进程并对其 UTF-8 输出做中文包含/禁词判定，
# CMake 侧没有可靠的子进程码点处理（同族限制：docs_match_cli 的载体注释里有先例）。
# WHY 解释器/二进制缺失报红而不是 SKIP：本用例"没问题"和"没检查"都只会以退出码 0 出现，
#   而文案回归恰恰在"没人跑"的那几天里攒出来。
# WHY 自证臂先跑：判据自己红 ⇒ 对仓库的结论一律不作数（与 docs_match_cli / param_effectiveness_guard 同一顺序）。

if(NOT DEFINED PY OR PY STREQUAL "")
    message(FATAL_ERROR
        "输出失败文案守卫：-DPY 为空 ⇒ 配置期没解析出可用的 Python（find_program NAMES python python3 py，"
        "且必须能真的执行 print）。本机 python3 是 WindowsApps 空壳（静默零输出），"
        "所以探测必须是『跑起来并回显』，不是『找得到文件』。")
endif()
if(NOT DEFINED CLI OR CLI STREQUAL "")
    message(FATAL_ERROR "输出失败文案守卫：必须传 -DCLI=<gis-cli 可执行文件>")
endif()
if(NOT EXISTS "${CLI}")
    message(FATAL_ERROR "输出失败文案守卫：CLI 不存在（${CLI}）⇒ 先构建 gis-cli 再跑本用例")
endif()
if(NOT DEFINED REPO_ROOT OR REPO_ROOT STREQUAL "")
    message(FATAL_ERROR "输出失败文案守卫：必须传 -DREPO_ROOT=<仓库根>")
endif()

set(_py "${CMAKE_CURRENT_LIST_DIR}/check_output_dir_failure_guard.py")
if(NOT EXISTS "${_py}")
    message(FATAL_ERROR "输出失败文案守卫：找不到判据脚本 ${_py}")
endif()

execute_process(
    COMMAND "${PY}" "${_py}" --self-test
    RESULT_VARIABLE _rc_self
    OUTPUT_VARIABLE _out_self
    ERROR_VARIABLE _err_self
)
if(NOT _rc_self EQUAL 0)
    message(FATAL_ERROR
        "output-dir-failure SELF-TEST failed (rc=${_rc_self}): a judge that cannot go red must not be "
        "trusted when it is green.\nstdout: ${_out_self}\nstderr: ${_err_self}")
endif()

# 真跑这一路**不捕获** stdout（捕获后 message() 会按本地码页再编一次，中文变双编码乱码）。
#   Python 侧的 stdout 因此刻意只用 ASCII；逐行原文写进 REPORT 指向的 UTF-8 文件，失败时按路径去看。
set(_report_args "")
if(DEFINED REPORT AND NOT REPORT STREQUAL "")
    set(_report_args "--report" "${REPORT}")
endif()
execute_process(
    COMMAND "${PY}" "${_py}" --cli "${CLI}" --root "${REPO_ROOT}" ${_report_args}
    RESULT_VARIABLE _rc
)

if(NOT _rc EQUAL 0)
    if(DEFINED REPORT AND NOT REPORT STREQUAL "")
        message(FATAL_ERROR "output-dir-failure guard FAILED (rc=${_rc}); see UTF-8 report: ${REPORT}")
    else()
        message(FATAL_ERROR "output-dir-failure guard FAILED (rc=${_rc})")
    endif()
endif()
