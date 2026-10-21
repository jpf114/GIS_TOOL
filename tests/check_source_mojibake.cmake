# Task B21 / B17：源码乱码守卫的 CMake 载体（判据本体在 check_source_mojibake.ps1）。
#
# 用法（复算口径，与 tests/CMakeLists.txt 里注册的用例同一件事）：
#   cmake -DPS=<powershell 绝对路径> -DREPO_ROOT=<仓库根> -P tests/check_source_mojibake.cmake
#
# WHY 判据不在 CMake 里自己写：四条规则里 R1（GBK roundtrip）需要 CP936 编解码器，
#   CMake 没有编解码器（同族限制见"CMake 无 zlib ⇒ 像素级独立判定不可行"）。
#   而 R2-R4 虽然只比码点，也**不能**只用人能写的那部分先落地：B17 的教训正是"规则只有两条就报归零"，
#   缺一条就是窄规则的 0。所以四条一起、放同一个脚本里，载体只负责调用与判定退出码。
# WHY 解释器缺失报红而不是 SKIP：本用例的"没有违规"就是绿 ⇒ "没人检查过"绝不能长得像"检查通过"
#   （N84/N86 那一族：check_install_closure.cmake 对扫描器缺失同样是 FATAL_ERROR 而不是跳过）。

if(NOT DEFINED PS OR PS STREQUAL "")
    message(FATAL_ERROR
        "源码乱码守卫：-DPS 为空 ⇒ 配置期没解析出 PowerShell 解释器（find_program NAMES powershell pwsh）。"
        "这里拒绝放行：四规则里 R1 需要 CP936 编解码器，没有解释器就等于**没人检查过**，"
        "而本用例的\"没问题\"与\"没检查\"都是退出码 0 以外的东西才分得开。")
endif()
if(NOT DEFINED REPO_ROOT OR REPO_ROOT STREQUAL "")
    message(FATAL_ERROR "源码乱码守卫：必须传 -DREPO_ROOT=<仓库根>")
endif()
if(NOT EXISTS "${REPO_ROOT}")
    message(FATAL_ERROR "源码乱码守卫：REPO_ROOT 不存在（${REPO_ROOT}）")
endif()

set(_ps1 "${CMAKE_CURRENT_LIST_DIR}/check_source_mojibake.ps1")
if(NOT EXISTS "${_ps1}")
    message(FATAL_ERROR "源码乱码守卫：找不到判据脚本 ${_ps1}")
endif()

execute_process(
    COMMAND "${PS}" -NoProfile -ExecutionPolicy Bypass -File "${_ps1}" -Root "${REPO_ROOT}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
)

# 判据脚本的 stdout 刻意只用 ASCII（Windows PowerShell 5.1 会把非 ASCII 输出渲染成乱码，
# 恰恰在失败那一路读不出来），所以这里原样转述，不再加工。
string(STRIP "${_out}" _out)
string(STRIP "${_err}" _err)
if(NOT _out STREQUAL "")
    message(STATUS "[mojibake-guard] 输出：\n${_out}")
endif()

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "源码乱码守卫失败（退出码 ${_rc}）。上面列出的每一行形如 "
        "\"MOJIBAKE <规则> <相对路径>:<行号>\"。"
        "复原办法：按版本库里**未损坏的那一版**逐行重建（B17 用的就是这条路：git show <修复前的最后一个好版本>），"
        "不要凭中文语感猜原文——猜出来的字与原文不同也算文案失实。"
        "\nstderr：${_err}")
endif()
