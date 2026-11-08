# 批次 D N144：vector srcSRS 结构守卫的 CMake 载体（判据本体在 check_vector_srcsrs_guard.py）。
#
# 复算（与 tests/CMakeLists.txt 注册的那条完全同形）：
#   cmake -DPY=<python 绝对路径> -DREPO_ROOT=<仓库根> -P tests/check_vector_srcsrs_guard.cmake
#
# WHY 判据不在 CMake 里自己写：判据要按登记表逐文件数 RAII 声明与 .get() 调用点、做双向集合相等、
#   再跑 6 条自证臂——cmake 的字符串正则没有词边界（本仓已证它连 `\(` 都不认），硬写只能得到
#   被注释污染的错数。先例：check_param_effectiveness_guard.cmake / check_geotransform_guard.cmake
#   （载体只管取解释器、调用、判退出码）。
# WHY 解释器缺失报红而不是 SKIP：本用例的"棘轮绿"就是绿 ⇒ "没人检查过"绝不能长得像"检查通过"
#   （同 check_param_effectiveness_guard.cmake 的口径）。
# WHY 登记表缺失同样拒绝放行（而不是按"零登记"跑）：登记表不在 = 棘轮没有界，扫描域里所有 RAII
#   持有点都会被当成未登记新站点刷出来，或者更糟——"site 集合变空"长得像"检查通过"。
#   缺失即红，理由与 param_effectiveness_guard.cmake 的登记表段同源。

if(NOT DEFINED PY OR PY STREQUAL "")
    message(FATAL_ERROR
        "vector srcSRS 守卫：-DPY 为空 ⇒ 配置期没解析出可用的 Python（find_program NAMES python python3 py "
        "并且要真的能回显）。这里拒绝放行：本机 python3 是 WindowsApps 空壳（静默零输出），"
        "只 find_program 到路径就等于把'检查不了'伪装成'检查通过'。")
endif()
if(NOT DEFINED REPO_ROOT OR REPO_ROOT STREQUAL "")
    message(FATAL_ERROR "vector srcSRS 守卫：必须传 -DREPO_ROOT=<仓库根>")
endif()
if(NOT EXISTS "${REPO_ROOT}")
    message(FATAL_ERROR "vector srcSRS 守卫：REPO_ROOT 不存在（${REPO_ROOT}）")
endif()

set(_py "${CMAKE_CURRENT_LIST_DIR}/check_vector_srcsrs_guard.py")
set(_registry "${CMAKE_CURRENT_LIST_DIR}/vector_srcsrs_guard_registry.json")
if(NOT EXISTS "${_py}")
    message(FATAL_ERROR "vector srcSRS 守卫：找不到判据脚本 ${_py}")
endif()
if(NOT EXISTS "${_registry}")
    message(FATAL_ERROR "vector srcSRS 守卫：找不到登记表 ${_registry}")
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
    message(STATUS "[vector-srcsrs-guard]\n${_out}")
endif()

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "vector srcSRS 守卫失败（退出码 ${_rc}）。"
        "退出码口径：1=真仓判定红（a=扫描域出现裸 OGRSpatialReference* srcSRS 声明或 delete srcSRS / "
        "c=RAII 文件集合与登记表脱钩（新站点不登记或登记已失效）或登记指向扫描域之外 / "
        "d=每文件 RAII 声明数与 .get() 调用点数和登记值不符），"
        "2=自证臂红（判据自己坏了，仓库结论不予发布），3=用法/环境错误。\n"
        "补救：①新增 srcSRS 持有点 ⇒ 按 unique_ptr<OGRSpatialReference, OGRSpatialReferenceReleaser> 形态写、"
        "同步改登记表（file+两计数）；②确实拆掉持有点 ⇒ 同步删登记行（c 两侧都要过）；"
        "③自证臂红先修判据与夹具，不要去放宽登记表。运行期等价性不归本守卫：那是 vector 族 ctest "
        "与两条真实腿的承重面。\nstderr：${_err}")
endif()
