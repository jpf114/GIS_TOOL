# 批次 D / Task 8：GetGeoTransform 防回潮守卫的 CMake 载体（判据本体在 check_geotransform_guard.py）。
#
# 复算（与 tests/CMakeLists.txt 注册的那条完全同形）：
#   cmake -DPY=<python 绝对路径> -DREPO_ROOT=<仓库根> -P tests/check_geotransform_guard.cmake
#
# WHY 判据不在 CMake 里自己写：本判据要按 C++ 词法把行注释 / 块注释 / 字符串字面量里的同形态
#   文本从 CODE 位点数里分出去，还要拼跨行语句（terrain_plugin.cpp:1318 那行注释、
#   `if (ds->GetGeoTransform(gt)` 换行 `!= CE_None)` 两种污染都实测过）。
#   cmake 的字符串正则不认 `\(`（本仓已证），也没有逐字符状态机 —— 硬写就只能得到被注释污染的错数。
#   先例：check_docs_match_cli.cmake（载体只管取解释器、调用、判退出码，判据在 .py）。
# WHY 解释器缺失报红而不是 SKIP：本用例的"没有回潮"就是绿 ⇒ "没人检查过"绝不能长得像"检查通过"
#   （同 check_source_mojibake.cmake / check_install_closure.cmake 的口径）。

if(NOT DEFINED PY OR PY STREQUAL "")
    message(FATAL_ERROR
        "geotransform 守卫：-DPY 为空 ⇒ 配置期没解析出可用的 Python（find_program NAMES python python3 py "
        "并且要真的能回显）。这里拒绝放行：本机 python3 是 WindowsApps 空壳（静默零输出），"
        "只 find_program 到路径就等于把'检查不了'伪装成'检查通过'。")
endif()
if(NOT DEFINED REPO_ROOT OR REPO_ROOT STREQUAL "")
    message(FATAL_ERROR "geotransform 守卫：必须传 -DREPO_ROOT=<仓库根>")
endif()
if(NOT EXISTS "${REPO_ROOT}")
    message(FATAL_ERROR "geotransform 守卫：REPO_ROOT 不存在（${REPO_ROOT}）")
endif()

set(_py "${CMAKE_CURRENT_LIST_DIR}/check_geotransform_guard.py")
set(_allowlist "${CMAKE_CURRENT_LIST_DIR}/geotransform_guard_allowlist.json")
if(NOT EXISTS "${_py}")
    message(FATAL_ERROR "geotransform 守卫：找不到判据脚本 ${_py}")
endif()
# 白名单缺失同样拒绝放行（而不是按"零豁免"跑）：那样任何未判位点都会被当成豁免之外的违规刷出来，
# 或者更糟 —— 被当成"没有位点要判"。缺失即红，理由与上面解释器那一段同源。
if(NOT EXISTS "${_allowlist}")
    message(FATAL_ERROR "geotransform 守卫：找不到白名单 ${_allowlist}")
endif()

execute_process(
    COMMAND "${PY}" "${_py}" --repo-root "${REPO_ROOT}" --allowlist "${_allowlist}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
)

# 判据脚本的 stdout 刻意只用 ASCII（见 .py 里 classify_code_site 的 WHY），这里原样转述。
string(STRIP "${_out}" _out)
string(STRIP "${_err}" _err)
if(NOT _out STREQUAL "")
    message(STATUS "[geotransform-guard]\n${_out}")
endif()

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "geotransform 守卫失败（退出码 ${_rc}）。"
        "退出码口径：1=位点回潮或白名单/期望数不符，2=自证臂红（解析器自己坏了，仓库结论不予发布），"
        "3=用法/环境错误。\n"
        "补救：①新位点判了返回值就把 tests/CMakeLists.txt 与本脚本 DOMAINS 里的期望数按现读改过去；"
        "②位点确实该豁免就在 geotransform_guard_allowlist.json 点名（file + 逐字 signature + 兜底理由），"
        "点名不到活位点的条目会以 DEAD_ALLOWLIST 判红；③自证臂红先修解析器与夹具，不要去放宽期望数。"
        "\nstderr：${_err}")
endif()
