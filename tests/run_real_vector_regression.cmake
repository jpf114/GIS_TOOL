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
#   判据本体、return() 的理由与标记字符串 `GIS_SKIP_TESTDATA_ABSENT: ` 的取径都写在
#   tests/run_real_raster_regression.cmake 的文件内注释（三份包装同一套语义，不重复论证）。
#   本腿为什么最该拦：四条里只有它判的是 data\vector 的真实道路（现 22836 要素），
#   而 data\vector 缺失时脚本会退回 roads_sample.geojson / china_sample.geojson 两个合成样本，
#   期望值又已改成"由输入图层自己的要素数推导"（e54f7d6）⇒ 合成样本下 15 条全绿的含金量
#   远低于真实数据，绝不能让它冒充"真实回归通过"。⇒ 本腿的门控收到 data\vector 这一级
#   （raster / matching 两条**不能**照抄这个收紧：本仓根本没有 data\raster 与 data\matching，
#   一收就永久 Skipped；那两条的实情由 tests/real_data_matrix.txt 的 not-consumed 列承担）。
#   复算三臂（每臂的根不一样，混用必得错误结论——本仓实测过：臂 2 用臂 1 的根会得到 rc=0 带标记）：
#     臂 1 缺数据 => Skip：根用**含 tests/ 而无 data/**（vector 腿：有 data/ 但无 data\vector）的目录
#       cmake -DCLI_PATH=<gis-cli> -DGUI_TEST_DATA_HELPER_PATH=<helper> \
#         -DWORKSPACE_ROOT=<该目录> -DPOWERSHELL_EXECUTABLE=<powershell> -DMODE=quick \
#         -P tests/run_real_vector_regression.cmake            ⇒ 打印标记、rc=0，ctest 侧 ***Skipped***
#     臂 2 腿真失败 => 红：根换成**真实仓库根**（数据齐），只把 -DMODE 换成 zzz
#         ... -DWORKSPACE_ROOT=<仓库根> ... -DMODE=zzz -P tests/run_real_vector_regression.cmake
#                                                          ⇒ rc=1、输出无标记，ctest 侧 ***Failed***
#                                                          （三条 .ps1 的 param 都带 [ValidateSet]，
#                                                           zzz 在参数绑定期就被拒，不会打开 data/）
#     臂 3 缺解释器 => 红：根同臂 1，把 -DPOWERSHELL_EXECUTABLE 留空
#                                                          ⇒ rc=1（FATAL_ERROR）、无标记
#   实测于 2026-10-21：三臂分别以出栈根与真实仓库根跑过（隔离 ctest 工程 `build/tmp/r1probe/ctest`），
#   臂 1 ***Skipped*** 并进 "did not run"、臂 2 与臂 3 均 ***Failed***。
# WORKSPACE_ROOT 拼错时**必须判红**，不许顺着下面的 data 检查静默滑成 Skip。
#   成因实测于 2026-10-21 控制器自己的探针：同一条参数写成 -DWORKSPACE_ROOT="${X}"（引号在值里）时
#   下面的 EXISTS 认不出该路径，去掉引号即正常 => 拼错根目录的表现不是报错，而是整批静默走 Skip 分支，
#   四条真用例整批 ***Skipped*** 而全量照绿 —— 正是本任务要消灭的"没检查长得像检查通过"。
#   复算：把 -DWORKSPACE_ROOT 指到一个不含 tests/ 的目录，应看到 FATAL_ERROR 而不是 GIS_SKIP 标记。
if(NOT EXISTS "${WORKSPACE_ROOT}/tests/run_real_vector_regression.ps1")
    message(FATAL_ERROR "WORKSPACE_ROOT 不是可用的仓库根：找不到 tests/run_real_vector_regression.ps1（当前值 ${WORKSPACE_ROOT}）")
endif()

# 解释器缺失时由这里判红，**并且必须判在数据检查之前**（这四条恒注册，见 tests/CMakeLists.txt 里 real_data_* 那节）。
#   顺序反过来的后果实测过：fresh clone 时 data/ 本来就被 gitignore 不存在，若先判数据就打印 Skip 标记并 return()，
#   连"本机根本没有 powershell"都被记成 Skipped ⇒ Skipped 不参与失败判定，恒注册的设计被绕过。
if(POWERSHELL_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "POWERSHELL_EXECUTABLE 为空：本机找不到 powershell / pwsh，真实矢量回归无法执行")
endif()

if(NOT EXISTS "${WORKSPACE_ROOT}/data/vector")
    message(STATUS "GIS_SKIP_TESTDATA_ABSENT: data/vector not present (${WORKSPACE_ROOT}/data/vector)")
    return()
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
