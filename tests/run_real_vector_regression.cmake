if(NOT DEFINED CLI_PATH)
    message(FATAL_ERROR "未提供 CLI_PATH")
endif()

if(NOT DEFINED WORKSPACE_ROOT)
    message(FATAL_ERROR "未提供 WORKSPACE_ROOT")
endif()

if(NOT DEFINED POWERSHELL_EXECUTABLE)
    message(FATAL_ERROR "未提供 POWERSHELL_EXECUTABLE")
endif()

if(NOT DEFINED MODE)
    set(MODE quick)
endif()

# N112（批次 C / Task C2）：data/ 缺失 ⇒ 显式 Skip，不许"退出码 0 ⇒ Passed"。
#   判据本体、return() 的理由与标记字符串 `GIS_SKIP: ` 的取径都写在
#   tests/run_real_raster_regression.cmake 的文件内注释（三份包装同一套语义，不重复论证）。
#   本腿为什么最该拦：四条里只有它判的是 data\vector 的真实道路（现 22836 要素），
#   而 data\vector 缺失时脚本会退回 roads_sample.geojson / china_sample.geojson 两个合成样本，
#   期望值又已改成"由输入图层自己的要素数推导"（e54f7d6）⇒ 合成样本下 15 条全绿的含金量
#   远低于真实数据，绝不能让它冒充"真实回归通过"。
#   复算（不动 data/ 内容）：
#     cmake -DCLI_PATH=<gis-cli> -DWORKSPACE_ROOT=<不含 data 的空目录> \
#       -DPOWERSHELL_EXECUTABLE=<powershell> -DMODE=quick -P tests/run_real_vector_regression.cmake
# WORKSPACE_ROOT 拼错时**必须判红**，不许顺着下面的 data 检查静默滑成 Skip。
#   成因实测于 2026-10-21 控制器自己的探针：同一条参数写成 -DWORKSPACE_ROOT="${X}"（引号在值里）时
#   下面的 EXISTS 认不出该路径，去掉引号即正常 => 拼错根目录的表现不是报错，而是整批静默走 Skip 分支，
#   四条真用例整批 ***Skipped*** 而全量照绿 —— 正是本任务要消灭的"没检查长得像检查通过"。
#   复算：把 -DWORKSPACE_ROOT 指到一个不含 tests/ 的目录，应看到 FATAL_ERROR 而不是 GIS_SKIP 标记。
if(NOT EXISTS "${WORKSPACE_ROOT}/tests/run_real_vector_regression.ps1")
    message(FATAL_ERROR "WORKSPACE_ROOT 不是可用的仓库根：找不到 tests/run_real_vector_regression.ps1（当前值 ${WORKSPACE_ROOT}）")
endif()

if(NOT EXISTS "${WORKSPACE_ROOT}/data")
    message(STATUS "GIS_SKIP: data/ not present (${WORKSPACE_ROOT}/data)")
    return()
endif()

# 解释器缺失时由这里判红，而不是"用例不注册"（这四条恒注册，见 tests/CMakeLists.txt 里 real_data_* 那节）。
if(POWERSHELL_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "POWERSHELL_EXECUTABLE 为空：本机找不到 powershell / pwsh，真实矢量回归无法执行")
endif()

execute_process(
    COMMAND
        "${POWERSHELL_EXECUTABLE}"
        -ExecutionPolicy Bypass
        -File "${WORKSPACE_ROOT}/tests/run_real_vector_regression.ps1"
        -CliPath "${CLI_PATH}"
        -WorkspaceRoot "${WORKSPACE_ROOT}"
        -OutputRoot "${WORKSPACE_ROOT}/tmp/vector_regression"
        -Mode "${MODE}"
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE regression_result
)

if(NOT regression_result EQUAL 0)
    message(FATAL_ERROR "真实矢量回归执行失败，退出码: ${regression_result}")
endif()
