# 批次 D N178：裸行号锚点守卫（anchor_guard）的 CMake 载体。
#
# 用法（复算口径，与 tests/CMakeLists.txt 里注册的用例同一件事）：
#   cmake -DPY=<python 绝对路径> -DREPO_ROOT=<仓库根> -P tests/check_anchor_guard.cmake
#   直跑判据本体：python tests/check_anchor_guard.py --self-test（合成输入的自证，不读仓库）
#
# WHY 判据不在 CMake 里写：要按"锚写的路径能不能在受控索引里唯一解析 + 行号是否越界"分类，
#   还得区分上游文件与本仓文件；CMake 的字符串正则做不了逐文件索引与行计数（先例：
#   check_docs_match_cli.py / check_geotransform_guard.py 都是"判据在 py，cmake 只管取解释器与判退出码"）。
# WHY 解释器缺失报红而不是 SKIP：本用例"锚都核过"和"根本没扫"在 CMake 侧都只会以退出码 0 出现，
#   而锚的漂移恰恰是在没人扫的那几天里攒出来的（N172-C3 一把删了 151 行，本守卫现读到 3 条指到 EOF 之后）。
# WHY 探测要用"跑起来并回显"：本机 python3 是 WindowsApps 空壳（静默零输出），找得到路径不等于能用。

if(NOT DEFINED PY OR PY STREQUAL "")
    message(FATAL_ERROR
        "锚点守卫：-DPY 为空 ⇒ 配置期没解析出可用的 Python（find_program NAMES python python3 py，"
        "且必须能真的执行 print）。")
endif()
if(NOT DEFINED REPO_ROOT OR REPO_ROOT STREQUAL "")
    message(FATAL_ERROR "锚点守卫：必须传 -DREPO_ROOT=<仓库根>")
endif()

set(_py "${CMAKE_CURRENT_LIST_DIR}/check_anchor_guard.py")
set(_cfg "${CMAKE_CURRENT_LIST_DIR}/anchor_guard_config.json")
foreach(_f "${_py}" "${_cfg}")
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "锚点守卫：找不到文件 ${_f}")
    endif()
endforeach()

execute_process(
    COMMAND "${PY}" "${_py}" --self-test
    RESULT_VARIABLE _rc_self
    OUTPUT_VARIABLE _out_self
    ERROR_VARIABLE _err_self
)
if(NOT _rc_self EQUAL 0)
    message(FATAL_ERROR
        "anchor-guard SELF-TEST failed (rc=${_rc_self}): a judge that cannot go red must not be "
        "trusted when it is green.\nstdout: ${_out_self}\nstderr: ${_err_self}")
endif()

# 真跑这一路**不捕获** stdout（捕获后 message() 会按本地码页再编一次，中文变双编码乱码）。
#   Python 侧 stdout 刻意只用 ASCII；逐行原文写进 REPORT 指向的 UTF-8 文件，失败时按路径去看。
set(_report_args "")
if(DEFINED REPORT AND NOT REPORT STREQUAL "")
    set(_report_args "--report" "${REPORT}")
endif()
execute_process(
    COMMAND "${PY}" "${_py}" --repo-root "${REPO_ROOT}" --config "${_cfg}" ${_report_args}
    RESULT_VARIABLE _rc
)

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "anchor-guard failed (rc=${_rc}). stdout 里每行 DRIFT 属于四类之一："
        "range = 行号超过目标文件行数（目标被删过）；unresolved = 路径解不开又没登记上游"
        "（典型是把 vector_plugin_common.inc 简写成 common.inc）；ambiguous = 裸文件名命中多个文件；"
        "ceiling/dead-registry = 登记表与现读不符（新增裸锚，或清了锚没把界降下来）。"
        "逐行原文：${REPORT}")
endif()
