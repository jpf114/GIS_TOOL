if(NOT DEFINED CLI_PATH)
    message(FATAL_ERROR "未提供 CLI_PATH")
endif()

if(NOT DEFINED WORKSPACE_ROOT)
    message(FATAL_ERROR "未提供 WORKSPACE_ROOT")
endif()

if(NOT DEFINED POWERSHELL_EXECUTABLE)
    message(FATAL_ERROR "未提供 POWERSHELL_EXECUTABLE")
endif()

if(NOT DEFINED GUI_TEST_DATA_HELPER_PATH)
    message(FATAL_ERROR "未提供 GUI_TEST_DATA_HELPER_PATH")
endif()

if(NOT DEFINED MODE)
    set(MODE quick)
endif()

# N112（批次 C / Task C2）：data/ 缺失 ⇒ 显式 Skip，不许"退出码 0 ⇒ Passed"。
#   WHY 判在包装里而不是判在 .ps1 里：三条腿的 .ps1 在真实数据文件找不到时会各自兜底现生成
#   （本腿：data\raster 里的 pansharpen / classification 输入缺了就用 gui_test_data_helper 生成到
#   tmp\raster_benchmark\generated_data；matching 腿用 data\matching、vector 腿用 data\vector，同样兜底），
#   用例照样跑完且全绿，但判的已经不是真实数据链路 —— 而那正是本任务要兜的东西。
#   所以缺 data 必须在调 .ps1 之前就拦下。
#   ⚠ 现读事实（本批不修，只登记）：本仓 data/ 只有 dem、image、vector 三个子目录，**没有** data\raster
#   与 data\matching ⇒ 这两条腿的"真实数据"取径一直走的是兜底分支，Skip 门控只能保证"data/ 整目录
#   不在时不装绿"，保证不了这两条腿判的是真图（要改判据归 C5/C10 那类批次）。
#   WHY return() 而不是 FATAL_ERROR：命中下面这条标记后，用例侧的 SKIP_REGULAR_EXPRESSION
#   会把结果认成 Skipped（本机 ctest 4.3.0-rc2 端到端实测（2026-10-21 控制器复跑）：退出码 0 + 命中标记 => ***Skipped***，
#   而 MODE 传非法值（腿真失败）=> ***Failed*** ⇒ 用例确实在判腿的退出码，不是空转），不需要把退出码拧成非 0 去撞"失败"那一侧。
#   标记字符串 `GIS_SKIP: ` 只有这里会打印，三条腿的正常输出里没有 ⇒ 不会把真失败误判成 Skip。
#   复算（不动 data/ 内容，把 WORKSPACE_ROOT 指到一个不含 data 的空目录即可让它 Skip）：
#     cmake -DCLI_PATH=<gis-cli> -DGUI_TEST_DATA_HELPER_PATH=<gui_test_data_helper> \
#       -DWORKSPACE_ROOT=<空目录> -DPOWERSHELL_EXECUTABLE=<powershell> \
#       -DMODE=quick -P tests/run_real_raster_regression.cmake
# WORKSPACE_ROOT 拼错时**必须判红**，不许顺着下面的 data 检查静默滑成 Skip。
#   成因实测于 2026-10-21 控制器自己的探针：同一条参数写成 -DWORKSPACE_ROOT="${X}"（引号在值里）时
#   下面的 EXISTS 认不出该路径，去掉引号即正常 => 拼错根目录的表现不是报错，而是整批静默走 Skip 分支，
#   四条真用例整批 ***Skipped*** 而全量照绿 —— 正是本任务要消灭的"没检查长得像检查通过"。
#   复算：把 -DWORKSPACE_ROOT 指到一个不含 tests/ 的目录，应看到 FATAL_ERROR 而不是 GIS_SKIP 标记。
if(NOT EXISTS "${WORKSPACE_ROOT}/tests/run_real_raster_regression.ps1")
    message(FATAL_ERROR "WORKSPACE_ROOT 不是可用的仓库根：找不到 tests/run_real_raster_regression.ps1（当前值 ${WORKSPACE_ROOT}）")
endif()

if(NOT EXISTS "${WORKSPACE_ROOT}/data")
    message(STATUS "GIS_SKIP: data/ not present (${WORKSPACE_ROOT}/data)")
    return()
endif()

# 解释器缺失时由这里判红，而不是"用例不注册"（这四条恒注册，见 tests/CMakeLists.txt 里 real_data_* 那节）。
if(POWERSHELL_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "POWERSHELL_EXECUTABLE 为空：本机找不到 powershell / pwsh，真实栅格回归无法执行")
endif()

execute_process(
    COMMAND
        "${POWERSHELL_EXECUTABLE}"
        -ExecutionPolicy Bypass
        -File "${WORKSPACE_ROOT}/tests/run_real_raster_regression.ps1"
        -CliPath "${CLI_PATH}"
        -WorkspaceRoot "${WORKSPACE_ROOT}"
        -OutputRoot "${WORKSPACE_ROOT}/tmp/raster_regression"
        -DataHelperPath "${GUI_TEST_DATA_HELPER_PATH}"
        -Mode "${MODE}"
    COMMAND_ECHO STDOUT
    RESULT_VARIABLE regression_result
)

if(NOT regression_result EQUAL 0)
    message(FATAL_ERROR "真实栅格回归执行失败，退出码: ${regression_result}")
endif()
