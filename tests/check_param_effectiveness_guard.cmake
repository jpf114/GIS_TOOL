# 批次 D / #164：上游边界参数有效性棘轮守卫的 CMake 载体（判据本体在 check_param_effectiveness_guard.py）。
#
# 复算（与 tests/CMakeLists.txt 注册的那条完全同形）：
#   cmake -DPY=<python 绝对路径> -DREPO_ROOT=<仓库根> -P tests/check_param_effectiveness_guard.cmake
#
# WHY 判据不在 CMake 里自己写：判据要按登记表逐文件提取引号 flag 集合、扫 src/ 边界调用符号、
#   解析 tests/test_plugins.cpp 的 gtest 用例名、再跑 7 条自证臂——cmake 的字符串正则没有
#   逐字符状态机（本仓已证它连 `\(` 都不认），硬写只能得到被注释污染的错数。
#   先例：check_geotransform_guard.cmake / check_anchor_guard.cmake（载体只管取解释器、调用、判退出码）。
# WHY 解释器缺失报红而不是 SKIP：本用例的"棘轮绿"就是绿 ⇒ "没人检查过"绝不能长得像"检查通过"
#   （同 check_source_mojibake.cmake / check_geotransform_guard.cmake 的口径）。

if(NOT DEFINED PY OR PY STREQUAL "")
    message(FATAL_ERROR
        "param_effectiveness 守卫：-DPY 为空 ⇒ 配置期没解析出可用的 Python（find_program NAMES python python3 py "
        "并且要真的能回显）。这里拒绝放行：本机 python3 是 WindowsApps 空壳（静默零输出），"
        "只 find_program 到路径就等于把'检查不了'伪装成'检查通过'。")
endif()
if(NOT DEFINED REPO_ROOT OR REPO_ROOT STREQUAL "")
    message(FATAL_ERROR "param_effectiveness 守卫：必须传 -DREPO_ROOT=<仓库根>")
endif()
if(NOT EXISTS "${REPO_ROOT}")
    message(FATAL_ERROR "param_effectiveness 守卫：REPO_ROOT 不存在（${REPO_ROOT}）")
endif()

set(_py "${CMAKE_CURRENT_LIST_DIR}/check_param_effectiveness_guard.py")
set(_registry "${CMAKE_CURRENT_LIST_DIR}/param_effectiveness_registry.json")
if(NOT EXISTS "${_py}")
    message(FATAL_ERROR "param_effectiveness 守卫：找不到判据脚本 ${_py}")
endif()
# 登记表缺失同样拒绝放行（而不是按"零登记"跑）：登记表不在 = 棘轮没有界，任何 flag 都会被
# 当成未登记刷出来，或者更糟——c 判据的 site 集合变空、"没有边界文件"长得像"检查通过"。
# 缺失即红，理由与上面解释器那一段同源。
if(NOT EXISTS "${_registry}")
    message(FATAL_ERROR "param_effectiveness 守卫：找不到登记表 ${_registry}")
endif()

execute_process(
    COMMAND "${PY}" "${_py}" --repo-root "${REPO_ROOT}" --registry "${_registry}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
)

# 判据脚本的 stdout 刻意只用 ASCII（见 .py 里 main() 的 WHY），这里原样转述。
string(STRIP "${_out}" _out)
string(STRIP "${_err}" _err)
if(NOT _out STREQUAL "")
    message(STATUS "[param-effectiveness-guard]\n${_out}")
endif()

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "param_effectiveness 守卫失败（退出码 ${_rc}）。"
        "退出码口径：1=真仓判定红（a=登记 flag 从 site 文件消失 / b=evidence_test 名不在 test_plugins.cpp / "
        "c=site 文件集合与边界文件集合不双向相等 / d=site 文件出现未登记新 flag），"
        "2=自证臂红（判据自己坏了，仓库结论不予发布），3=用法/环境错误。\n"
        "补救：①新边界接线 ⇒ 按登记表条目形态补登记（flag+context+key 归类+真实存在的 evidence_test+理由），"
        "别绕过棘轮；②接线确实拆了 ⇒ 同步删登记行（c/d 两侧都要过）；③自证臂红先修判据与夹具，"
        "不要去放宽登记表。值级真生效不归本守卫：那是各刀 gtest 值判据的承重面（通用注册表另立 N196）。"
        "\nstderr：${_err}")
endif()
