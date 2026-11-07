# 批次 D N177：腿侧值级判据闸（regression_case_gate）的自证载体。
#
# 用法（复算口径，与 tests/CMakeLists.txt 里注册的用例同一件事）：
#   cmake -DPS=<powershell 绝对路径> -DREPO_ROOT=<仓库根> -P tests/check_regression_gate_selftest.cmake
#   直跑判据本体：powershell -NoProfile -ExecutionPolicy Bypass -File tests/regression_case_gate_selftest.ps1
#
# WHY 要有一个"只测闸自己"的用例：闸本身是三条真实腿（real_data_raster/vector/matching）的收尾判据，
#   它退化时三条腿一起变红，读起来像"三条腿的期望都坏了"；而自证跑在合成夹具上，红的分布直接指向闸。
#   N177 立竿见影的一例：收紧 helper 传递规则时我把一条"一个比较都没有"的判据写成假绿（$cmps.Count
#   在单元素结果上是 $null），三条腿那时全是绿的，13 支臂里 A9/A10 两支当场红。
# WHY 自证跑在**合成夹具**上而不跑真实腿：真实腿给的数会随每一次晋升漂（43/20 这种数没法写死），
#   而合成夹具的分支形状是这份文件自己抄的，arms=15 failed=0 是一个不随仓库漂的断言。
# WHY 解释器缺失报红而不是 SKIP：本用例"没问题"与"没检查"都只以退出码 0 出现；闸瞎掉的时候它只会
#   让三条腿少打一行数字，不会有人去查（口径同 source_mojibake_guard / docs_match_cli）。

if(NOT DEFINED PS OR PS STREQUAL "")
    message(FATAL_ERROR
        "腿侧判据闸自证：-DPS 为空 ⇒ 配置期没解析出 PowerShell 解释器（find_program NAMES powershell pwsh）。"
        "这里拒绝放行：闸的判据是 PowerShell AST 解析，没有解释器就等于**没人检查过**。")
endif()
if(NOT DEFINED REPO_ROOT OR REPO_ROOT STREQUAL "")
    message(FATAL_ERROR "腿侧判据闸自证：必须传 -DREPO_ROOT=<仓库根>")
endif()
if(NOT EXISTS "${REPO_ROOT}")
    message(FATAL_ERROR "腿侧判据闸自证：REPO_ROOT 不存在（${REPO_ROOT}）")
endif()

set(_ps1 "${CMAKE_CURRENT_LIST_DIR}/regression_case_gate_selftest.ps1")
set(_gate "${CMAKE_CURRENT_LIST_DIR}/regression_case_gate.ps1")
if(NOT EXISTS "${_ps1}")
    message(FATAL_ERROR "腿侧判据闸自证：找不到自证脚本 ${_ps1}")
endif()
# WHY 还要核闸本体文件存在：自证脚本点源（dot-source）它，文件缺失时 PowerShell 会先报"找不到文件"
#   再退出，那条错误读起来像脚本自己的 bug，不像"这个守卫没法跑"。
if(NOT EXISTS "${_gate}")
    message(FATAL_ERROR "腿侧判据闸自证：找不到判据本体 ${_gate}")
endif()

execute_process(
    COMMAND "${PS}" -NoProfile -ExecutionPolicy Bypass -File "${_ps1}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
)

# 自证脚本的 stdout 刻意只用 ASCII（.ps1 字面量禁非 ASCII，见 N173），原样转述。
string(STRIP "${_out}" _out)
string(STRIP "${_err}" _err)
if(NOT _out STREQUAL "")
    message(STATUS "[regression-gate-selftest] 输出：\n${_out}")
endif()

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "腿侧判据闸自证失败（退出码 ${_rc}）。上面每一行形如 \"FAIL <臂名> [threw=… msg=…]\"，"
        "最后一行是 arms=<总数> failed=<红数>。红的臂名直接指向退化的是哪一条规则："
        "A6=基数不算值、A9=传输型 helper 不传递值、A10=-Expected 整数必须被看到、"
        "A11=几何参数不算值、A12=文本里的带小数期望算值、A13=运行时不变量不算值、"
        "A14=产物字段 count 不是集合 Count、A15=嵌在操作数里的数字算值。"
        "\nstderr：${_err}")
endif()
