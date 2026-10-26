# Task C21 / C24（批次 C 阶段三）：文档参数表 ↔ 二进制契约守卫的 CMake 载体。
#
# 用法（复算口径，与 tests/CMakeLists.txt 里注册的用例同一件事）：
#   cmake -DPY=<python 绝对路径> -DCLI=<gis-cli 路径> -DREPO_ROOT=<仓库根> -P tests/check_docs_match_cli.cmake
#   直跑判据本体：python tests/check_docs_match_cli.py --self-test（合成输入的自证，不读仓库）
#
# WHY 判据不在 CMake 里写：要比"文档默认值 == `--help` 打出来的 Default"就得跑二进制并解析它的
#   UTF-8 stdout，CMake 侧没有可靠的子进程码点处理（同族限制：`GIS_POWERSHELL_EXECUTABLE` 缺失时
#   乱码守卫也只能拒放行，不在 CMake 里重发明规则）。
# WHY 解释器/二进制缺失报红而不是 SKIP：本用例"没问题"和"没检查"都只会以退出码 0 出现，
#   而文档漂移恰恰是在"没人跑"的那几天里攒出来的（阶段三实测：92 条漂移里 82 条是标签口径、
#   10 条是事实错，全部在无人检查的状态下活了好几周）。
# WHY 还要核 CLI 文件存在：`$<TARGET_FILE:gis-cli>` 展开出来的路径在未构建时不存在，
#   判据脚本自己会 rc=2 报"文件不存在" ⇒ 这里提前拒绝，别让它长得像"文档没问题"。

if(NOT DEFINED PY OR PY STREQUAL "")
    message(FATAL_ERROR
        "文档同步守卫：-DPY 为空 ⇒ 配置期没解析出可用的 Python（find_program NAMES python python3 py，"
        "且必须能真的执行 print）。本机 python3 是 WindowsApps 空壳（静默零输出），"
        "所以探测必须是『跑起来并回显』，不是『找得到文件』。")
endif()
if(NOT DEFINED CLI OR CLI STREQUAL "")
    message(FATAL_ERROR "文档同步守卫：必须传 -DCLI=<gis-cli 可执行文件>")
endif()
if(NOT EXISTS "${CLI}")
    message(FATAL_ERROR "文档同步守卫：CLI 不存在（${CLI}）⇒ 先构建 gis-cli 再跑本用例")
endif()
if(NOT DEFINED REPO_ROOT OR REPO_ROOT STREQUAL "")
    message(FATAL_ERROR "文档同步守卫：必须传 -DREPO_ROOT=<仓库根>")
endif()

set(_py "${CMAKE_CURRENT_LIST_DIR}/check_docs_match_cli.py")
if(NOT EXISTS "${_py}")
    message(FATAL_ERROR "文档同步守卫：找不到判据脚本 ${_py}")
endif()

execute_process(
    COMMAND "${PY}" "${_py}" --self-test
    RESULT_VARIABLE _rc_self
    OUTPUT_VARIABLE _out_self
    ERROR_VARIABLE _err_self
)
if(NOT _rc_self EQUAL 0)
    message(FATAL_ERROR
        "docs-match-cli SELF-TEST failed (rc=${_rc_self}): a judge that cannot go red must not be "
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
    message(FATAL_ERROR
        "docs-match-cli failed (rc=${_rc}). stdout lines are prefixed DRIFT / DEAD-ALLOWLIST / "
        "DEAD-OVERRIDE: first = doc vs binary mismatch, last two = allowlist entries that no longer "
        "fire (a stale exemption would swallow the next real drift). Full text: ${REPORT}")
endif()
