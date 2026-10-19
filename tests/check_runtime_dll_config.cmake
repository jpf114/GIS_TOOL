# 跨配置运行库残留审计（Task 21 收尾：终审第 1 席 I-7 + 控制器存在性审计）
#
# WHY：`cmake/gis_runtime_dlls.cmake` 的拷贝命令**只写不删**，且 `copy_if_exists.cmake` 对
#   "本配置不该存在的名字"找不到源就静默跳过 ⇒ 一次普通构建**只能自愈"两份同名、来源配置错"那一类**，
#   而对"只该属于对面配置"的名字（vcpkg 用带 `d` 的名字区分配置，如 `gdal.dll` / `gdald.dll`）
#   永远不会清理。实测：修好拷贝逻辑（`a10c05c`）之后，两棵老树里仍留着
#   **Debug 树 46 个 release 件、Release 树 45 个带 `d` 件**（mtime 早于该修复提交）。
#   md5 对账抓不到这一类（它只比"同名项等于哪一份"），所以判据必须换成**存在性**：
#   本配置有源 = 正常；只对面有源 = 跨配置污染（红）；两边都无源 = 来源不可判（只报告，
#   因为可能是更早的 vcpkg 布局留下的，删它需要额外证据，不在本判据射程）。
#
# 用法（由 tests/CMakeLists.txt 注册成用例）：
#   cmake -DCONFIG=Debug -DRELEASE_BIN=<vcpkg>/bin -DDEBUG_BIN=<vcpkg>/debug/bin \
#         -DPLUGINS_DIR=<bin>/plugins -DEXE_DIR=<bin>/src/cli/Debug -P check_runtime_dll_config.cmake

if(NOT DEFINED CONFIG OR CONFIG STREQUAL "")
    message(FATAL_ERROR "check_runtime_dll_config.cmake: 必须传 -DCONFIG=<Debug|Release>")
endif()
foreach(_v RELEASE_BIN DEBUG_BIN PLUGINS_DIR)
    if(NOT DEFINED ${_v} OR ${_v} STREQUAL "")
        message(FATAL_ERROR "check_runtime_dll_config.cmake: 缺少 -D${_v}")
    endif()
endforeach()

if(CONFIG STREQUAL "Debug")
    set(OWN_DIR "${DEBUG_BIN}")
    set(OTHER_DIR "${RELEASE_BIN}")
else()
    set(OWN_DIR "${RELEASE_BIN}")
    set(OTHER_DIR "${DEBUG_BIN}")
endif()

foreach(_d OWN_DIR OTHER_DIR)
    if(NOT IS_DIRECTORY "${${_d}}")
        message(FATAL_ERROR "vcpkg 运行库目录不存在（${_d}=${${_d}}）⇒ 判据无法成立，拒绝以'没有残留'放行")
    endif()
endforeach()

set(_dirs "${PLUGINS_DIR}")
if(DEFINED EXE_DIR AND NOT EXE_DIR STREQUAL "")
    string(APPEND _dirs ";${EXE_DIR}")
endif()

set(RESIDUE "")
set(UNRESOLVED "")
set(CHECKED 0)
foreach(_dir IN LISTS _dirs)
    if(NOT IS_DIRECTORY "${_dir}")
        message(STATUS "[runtime-audit] 目录尚未生成，跳过：${_dir}")
        continue()
    endif()
    file(GLOB _dlls "${_dir}/*.dll")
    foreach(_dll IN LISTS _dlls)
        get_filename_component(_name "${_dll}" NAME)
        math(EXPR CHECKED "${CHECKED} + 1")
        set(_in_own FALSE)
        set(_in_other FALSE)
        if(EXISTS "${OWN_DIR}/${_name}")
            set(_in_own TRUE)
        endif()
        if(EXISTS "${OTHER_DIR}/${_name}")
            set(_in_other TRUE)
        endif()
        if(NOT _in_own AND _in_other)
            list(APPEND RESIDUE "${_dir}/${_name}")
        elseif(NOT _in_own AND NOT _in_other)
            list(APPEND UNRESOLVED "${_name}")
        endif()
    endforeach()
endforeach()

list(LENGTH RESIDUE NRES)
list(REMOVE_DUPLICATES UNRESOLVED)
list(LENGTH UNRESOLVED NUNRES)

message(STATUS "[runtime-audit] CONFIG=${CONFIG} 本配置源目录=${OWN_DIR}")
message(STATUS "[runtime-audit] 审计 dll 数=${CHECKED} 跨配置残留=${NRES} 来源不可判=${NUNRES}")
if(NUNRES GREATER 0)
    list(JOIN UNRESOLVED " " _unres_text)
    message(STATUS "[runtime-audit] 来源不可判（只报告，不判红）：${_unres_text}")
endif()

if(NRES GREATER 0)
    list(JOIN RESIDUE "\n  " _res_text)
    message(FATAL_ERROR
        "构树里存在只属于对面配置的运行库 dll（跨配置污染残留，共 ${NRES} 个）：\n  ${_res_text}\n"
        "WHY 不会自愈：gis_copy_minimal_runtime 的 POST_BUILD 拷贝只写不删，且 vcpkg 用带 d 的名字区分配置"
        "（gdal.dll / gdald.dll 等），本配置下找不到同名源 ⇒ copy_if_exists 静默跳过、旧文件永久留。\n"
        "补救（二选一）：①删掉上面列出的文件后重跑一次构建（会按本配置拷回该有的那份）；"
        "②整树重新配置构建。")
endif()
