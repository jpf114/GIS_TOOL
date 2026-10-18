# ==============================================================================
# 21-C 反向参数守卫的扫描侧（cmake -P 用例）
#
# 命题：某 action 的 visibleKeys 里**不得**出现该 action 根本不读的键。
#   方向与既有的 #334（实读 ⊆ 可见）相反，此前无人把守 —— Task 10 实测过：
#   从 raster_math.reclassify 白名单删掉插件实读的 default_value ⇒ 548/548 全绿。
# 判据：**白名单 − 闭包内实读 非空即红**。不设阈值、不设"警告档"（阈值＝自我缴械）。
#
# 调用形状见 tests/CMakeLists.txt 里的 add_test。
#
# WHY 脚本自己跑 gis_tests 生成产物，而不是"依赖另一个用例先跑"：ctest 的默认执行序不是
#   契约（-R 子集、并行、单独复现都会打乱顺序），把守卫押在顺序上 ⇒ 单跑守卫就红，
#   那种红会被当成环境问题而非违规，最终被人 --ignore 掉。产物由本用例自己重新生成。
#
# WHY 一切键集合都按"同文件内的调用闭包"算，而不是按 do<Action> 的函数名：
#   doCutFill 只是五行转发，实读在 runCutFillProcess。按名字切 ⇒ 每个包装型 action 都被
#   误判成零实读 ⇒ 守卫一上线满屏红。
# ==============================================================================

# 允许"第一个形参是 params、第二个形参是键"的读参函数全集。新增第四个必须在这里登记，
# 否则本脚本会红 —— 这条比守卫本身更脆，因为漏登记是**静默少读**（假绿），不是假红。
set(GIS_21C_READER_FUNCS "getParam;getBandIndex;resolveOptionalAliasBand")

# 显式豁免表：形如 "<插件名>|<调用所在的函数名>|<说明>"。
# WHY 按"插件+所属函数"而不是行号：行号会随无关改动漂移，漂移后的条目既命中不了任何真读点、
# 又留下一笔"已经失效还挂在表里"的假账。本表由脚本末尾两条断言把守：
#   (a) 动态键实读点数量必须 == 本表条目数 ⇒ 多一处就红（不许默默增长）；
#   (b) 本表每条都必须被实际命中        ⇒ 少一处也红（账清了必须销账）。
set(GIS_21C_EXEMPTIONS
    "spindex|getBandIndex|键来自形参 const char* key，43 个调用点全是字面量（本片实测唯一一处）")

foreach(_req GIS_TESTS_PATH WHITELIST_FILE PLUGIN_DIR REGISTRY_FILE REPORT_FILE)
  if(NOT ${_req})
    message(FATAL_ERROR "缺少 -D${_req}")
  endif()
endforeach()
if(NOT EXISTS "${PLUGIN_DIR}")
  message(FATAL_ERROR "插件源码目录不存在：${PLUGIN_DIR}")
endif()
if(NOT EXISTS "${REGISTRY_FILE}")
  message(FATAL_ERROR "动作登记册不存在：${REGISTRY_FILE}")
endif()

# ------------------------------------------------------------------ 0. 重新生成白名单产物
get_filename_component(GIS_TESTS_DIR "${GIS_TESTS_PATH}" DIRECTORY)
if(NOT EXISTS "${GIS_TESTS_PATH}")
  message(FATAL_ERROR "gis_tests 可执行文件不存在：${GIS_TESTS_PATH}（构树没建？）")
endif()
execute_process(
    COMMAND "${GIS_TESTS_PATH}" "--gtest_filter=GuiContract.ExportActionVisibleKeys"
    WORKING_DIRECTORY "${GIS_TESTS_DIR}"
    RESULT_VARIABLE EXPORT_RC
    OUTPUT_VARIABLE EXPORT_OUT
    ERROR_VARIABLE EXPORT_ERR
)
if(NOT EXPORT_RC EQUAL 0)
  message(FATAL_ERROR
      "导出用例 GuiContract.ExportActionVisibleKeys 运行失败 rc=${EXPORT_RC}\n${EXPORT_OUT}\n${EXPORT_ERR}")
endif()
if(NOT EXPORT_OUT MATCHES "21C-EXPORT")
  message(FATAL_ERROR "导出用例没有打出 [21C-EXPORT] 汇总行，产物不可信：${EXPORT_OUT}")
endif()
if(NOT EXISTS "${WHITELIST_FILE}")
  message(FATAL_ERROR "导出用例跑完却没有产物：${WHITELIST_FILE}")
endif()

# ------------------------------------------------------------------ 1. 读白名单产物
file(STRINGS "${WHITELIST_FILE}" WL_LINES ENCODING UTF-8)
set(WL_PAIRS "")
set(WL_CONFIGURED 0)
set(WL_NOCONFIG 0)
set(WL_UNPARSED "")
foreach(line IN LISTS WL_LINES)
  if(line MATCHES "^#" OR line STREQUAL "" OR line MATCHES "^plugin[|]action[|]key$")
    continue()
  endif()
  string(REPLACE "|" ";" parts "${line}")
  list(LENGTH parts nparts)
  if(NOT nparts EQUAL 3)
    list(APPEND WL_UNPARSED "${line}")
    continue()
  endif()
  list(GET parts 0 p)
  list(GET parts 1 a)
  list(GET parts 2 k)
  set(pair "${p}.${a}")
  list(FIND WL_PAIRS "${pair}" seen)
  if(seen EQUAL -1)
    list(APPEND WL_PAIRS "${pair}")
  endif()
  if(k MATCHES "^@")
    if(k STREQUAL "@NO_ACTION_UI_CONFIG")
      set(NOCFG_${pair} TRUE)
      math(EXPR WL_NOCONFIG "${WL_NOCONFIG} + 1")
      continue()
    endif()
    list(APPEND WL_UNPARSED "${line}")
    continue()
  endif()
  if(NOT k MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
    list(APPEND WL_UNPARSED "${line}")
    continue()
  endif()
  list(FIND WL_${p}_${a} "${k}" keySeen)
  if(keySeen EQUAL -1)
    list(APPEND WL_${p}_${a} "${k}")
    math(EXPR WL_CONFIGURED "${WL_CONFIGURED} + 1")
  endif()
endforeach()
if(WL_UNPARSED)
  message(FATAL_ERROR "白名单产物有无法解析的行：${WL_UNPARSED}")
endif()
if(WL_CONFIGURED EQUAL 0)
  message(FATAL_ERROR "白名单产物一条可见键都没有（导出用例坏了吗？）")
endif()
foreach(pair IN LISTS WL_PAIRS)
  if(NOT NOCFG_${pair})
    set(HASCFG_${pair} TRUE)
  endif()
endforeach()

# ------------------------------------------------------------------ 2. 动作登记册全集
file(STRINGS "${REGISTRY_FILE}" REG_LINES ENCODING UTF-8)
set(REG_PAIRS "")
foreach(line IN LISTS REG_LINES)
  if(line MATCHES "^#" OR line STREQUAL "")
    continue()
  endif()
  string(REPLACE "," ";" fields "${line}")
  list(LENGTH fields nf)
  if(nf LESS 3)
    message(FATAL_ERROR "登记册行字段不足：${line}")
  endif()
  list(GET fields 0 p)
  list(GET fields 1 a)
  list(APPEND REG_PAIRS "${p}.${a}")
endforeach()
list(REMOVE_DUPLICATES REG_PAIRS)
list(LENGTH REG_PAIRS REG_COUNT)
list(LENGTH WL_PAIRS WL_PAIR_COUNT)
if(NOT WL_PAIR_COUNT EQUAL REG_COUNT)
  message(FATAL_ERROR
      "产物里的 action 数（${WL_PAIR_COUNT}）与登记册（${REG_COUNT}）不符，比对失去意义")
endif()

# ------------------------------------------------------------------ 3. 切段
# 约定：本仓插件源码的顶层函数体，闭花括号一定在行首（列 0），函数体内部的闭花括号一律带缩进。
# 于是"以行首 } 或 }; 为界切片"就切出顶层段。假设破了的表现由第 8 节的段数下限自检兜住。
# 只扫 .cpp/.inc（插件的头文件只有声明，扫进来会把整张类声明变成一个巨大段 ⇒ 静默放大闭包）。
file(GLOB PLUGIN_SUBDIRS LIST_DIRECTORIES true "${PLUGIN_DIR}/*")
list(SORT PLUGIN_SUBDIRS)

set(SEG_COUNT 0)
set(FILE_COUNT 0)
set(LINE_TOTAL 0)
set(PLUGIN_NAMES "")
set(KEY_READ_TOTAL 0)
set(DYNAMIC_READ_HITS "")
set(UNKNOWN_READER_HITS "")

foreach(pdir IN LISTS PLUGIN_SUBDIRS)
  if(NOT IS_DIRECTORY "${pdir}")
    continue()
  endif()
  get_filename_component(pname "${pdir}" NAME)
  list(APPEND PLUGIN_NAMES "${pname}")
  file(GLOB pfiles "${pdir}/*.cpp" "${pdir}/*.inc")
  list(SORT pfiles)
  foreach(f IN LISTS pfiles)
    math(EXPR FILE_COUNT "${FILE_COUNT} + 1")
    file(STRINGS "${f}" FLINES ENCODING UTF-8)
    file(RELATIVE_PATH frel "${PLUGIN_DIR}" "${f}")
    get_filename_component(fplug "${frel}" DIRECTORY)
    set(SEGACC "")
    set(BLOCK_COMMENT FALSE)
    set(SEG_HAS_LINE FALSE)
    foreach(l IN LISTS FLINES)
      math(EXPR LINE_TOTAL "${LINE_TOTAL} + 1")
      set(clean "${l}")
      # 注释口径：**只**剥 // 到行尾。
      # WHY 不做 /* */ 状态机：src/plugins 全树实测只有 2 处 /*，且没有一处是块注释——
      #   vector_plugin_info_filter.inc:41 是 where.find("/*") 这个**字符串字面量**，
      #   vector_plugin_duplicate_point_check.inc:56 在一行 // 注释里。
      #   用状态机就会被前者把整篇文件当成注释吃掉：实测该文件切出 0 段、
      #   vector.doFilter/doInfo 的全部实读消失 ⇒ 5 条假红，而且是**静默**发生的。
      # WHY 剥 // 是安全的：grep -rn '"[^"]*//' src/plugins/ 命中 0，
      #   即没有任何 // 出现在字符串字面量里，剥它不会截断任何真代码。
      string(REGEX REPLACE "//.*$" "" clean "${clean}")
      if(NOT clean STREQUAL "")
        set(SEG_HAS_LINE TRUE)
        # 前提哨兵：行注释剥完还剩下 /* 或 */ ⇒ 有人新写了块注释，本口径不再充分。
        # 必须红，不许静默把块注释里的文字当代码读。
        if(clean MATCHES "/[*]" OR clean MATCHES "[*]/")
          set(BLOCK_COMMENT TRUE)
          set(GIS_21C_BLOCKHIT "${frel}: ${clean}")
        endif()
      endif()
      list(APPEND SEGACC "${clean}")

      if(clean MATCHES "^[}][;]?[ \t]*$")
        if(SEG_HAS_LINE)
          math(EXPR sid "${SEG_COUNT} + 1")
          set(SEG_COUNT ${sid})
          set(SEGLINES_${sid} ${SEGACC})
          set(SEGFILE_${sid} "${frel}")
          set(SEGPLUGIN_${sid} "${fplug}")
        endif()
        set(SEGACC "")
        set(SEG_HAS_LINE FALSE)
      endif()
    endforeach()
  endforeach()
endforeach()

# ------------------------------------------------------------------ 4. 逐段解析
foreach(sid RANGE 1 ${SEG_COUNT})
  set(_plug "${SEGPLUGIN_${sid}}")

  # -- 定义名：从头到第一个 {，取最左的 ident(
  set(_defname "")
  set(_hdrtxt "")
  set(_inhdr TRUE)
  foreach(_l IN LISTS SEGLINES_${sid})
    if(_inhdr)
      set(_hdrtxt "${_hdrtxt} ${_l}")
      if(_l MATCHES "[{]")
        string(REGEX MATCH "[A-Za-z_][A-Za-z0-9_]*[ \t]*[(]" _hm "${_hdrtxt}")
        if(NOT _hm STREQUAL "")
          string(REGEX REPLACE "[ \t]*[(].*" "" _defname "${_hm}")
          set(_inhdr FALSE)
        endif()
      endif()
    endif()
  endforeach()
  set(SEGDEF_${sid} "${_defname}")
  list(LENGTH SEGLINES_${sid} _dbg_nlines)
  if(NOT _defname STREQUAL "")
    set(SEGBYNAME_${_plug}_${_defname} "${SEGBYNAME_${_plug}_${_defname}},${sid}")
  endif()

  set(_keys "")
  set(_actkeys "")
  set(_calls "")
  set(_actcalls "")
  set(_actseed "")
  set(_blkcnt 0)
  set(_pendacts "")
  set(_pendindent "-1")
  foreach(_l IN LISTS SEGLINES_${sid})
    # ---- 行首带缩进的 } 关闭该缩进层级上打开的 action 块
    if(_l MATCHES "^[ \t]*[}]")
      string(REGEX MATCH "^[ \t]*" _ind "${_l}")
      string(LENGTH "${_ind}" _indlen)
      set(_b ${_blkcnt})
      while(_b GREATER 0)
        if(_BLKINDENT_${sid}_${_b} STREQUAL "${_indlen}" AND _BLKOPEN_${sid}_${_b} STREQUAL "TRUE")
          set(_BLKOPEN_${sid}_${_b} FALSE)
        endif()
        math(EXPR _b "${_b} - 1")
      endwhile()
    endif()

    # ---- 本行的 action == "x" 集合
    # WHY 用 MATCHALL 而不是 "while + REGEX REPLACE 成占位符"：string(REPLACE) 是**全局**替换，
    #   一趟就把整行的 action == "x" 全换成哨兵，while 因此只转一圈 ⇒ 每行只有第一个动作被记下来。
    #   spindex 的多行 || 条件正是这个形状，实测 17 个动作里只剩每行第 1 个（6 个）拿到种子。
    set(_lineacts "")
    string(REGEX MATCHALL "action[ \t]*[=][=][ \t]*\"[A-Za-z_0-9]+\"" _aeq "${_l}")
    foreach(_e IN LISTS _aeq)
      if(_e MATCHES "\"([A-Za-z_0-9]+)\"$")
        list(APPEND _lineacts "${CMAKE_MATCH_1}")
      endif()
    endforeach()
    list(LENGTH _lineacts _nacts)
    list(LENGTH _pendacts _np)
    if(_nacts GREATER 0)
      string(REGEX MATCH "^[ \t]*" _thisindm "${_l}")
      string(LENGTH "${_thisindm}" _thisindlen)
      if(_np EQUAL 0)
        set(_pendindent "${_thisindlen}")
      endif()
      list(APPEND _pendacts ${_lineacts})
      list(REMOVE_DUPLICATES _pendacts)
      list(LENGTH _pendacts _np)
      if(_l MATCHES "[{]")
        math(EXPR _blkcnt "${_blkcnt} + 1")
        set(_BLKINDENT_${sid}_${_blkcnt} "${_pendindent}")
        set(_BLKACTS_${sid}_${_blkcnt} ${_pendacts})
        set(_BLKOPEN_${sid}_${_blkcnt} TRUE)
        set(_pendacts "")
        list(LENGTH _pendacts _np)
      endif()
    elseif(_np GREATER 0 AND _l MATCHES "[{]")
      math(EXPR _blkcnt "${_blkcnt} + 1")
      set(_BLKINDENT_${sid}_${_blkcnt} "${_pendindent}")
      set(_BLKACTS_${sid}_${_blkcnt} ${_pendacts})
      set(_BLKOPEN_${sid}_${_blkcnt} TRUE)
      set(_pendacts "")
      list(LENGTH _pendacts _np)
    endif()

    set(_curacts "")
    set(_b 1)
    while(_b LESS_EQUAL _blkcnt)
      if(_BLKOPEN_${sid}_${_b} STREQUAL "TRUE")
        list(APPEND _curacts ${_BLKACTS_${sid}_${_b}})
      endif()
      math(EXPR _b "${_b} + 1")
    endwhile()
    list(REMOVE_DUPLICATES _curacts)
    list(LENGTH _curacts _nca)

    # ---- 读键（字面量第二实参）
    string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_]*(<[^()]*>)?[ \t]*[(][ \t]*[A-Za-z_]*[Pp]arams[ \t]*,[ \t]*\"[A-Za-z_0-9]+\"" _mm "${_l}")
    foreach(_m IN LISTS _mm)
      if(_m MATCHES "^([A-Za-z_][A-Za-z0-9_]*)[<(]")
        set(_fn "${CMAKE_MATCH_1}")
        if(NOT ";${GIS_21C_READER_FUNCS};" MATCHES ";${_fn};")
          list(APPEND UNKNOWN_READER_HITS "${_fn}@${_plug}")
        endif()
      endif()
      if(_m MATCHES "\"([A-Za-z_0-9]+)\"$")
        math(EXPR KEY_READ_TOTAL "${KEY_READ_TOTAL} + 1")
        if(_nca EQUAL 0)
          list(APPEND _keys "${CMAKE_MATCH_1}")
        else()
          foreach(_a IN LISTS _curacts)
            set(_actkeys "${_actkeys};${_a}=${CMAKE_MATCH_1}")
          endforeach()
        endif()
      endif()
    endforeach()

    # ---- 动态键实读（豁免表核对对象）：getParam<…>(params, 变量, …)
    if(_l MATCHES "getParam(<[^()]*>)?[(][ \t]*[A-Za-z_]*[Pp]arams[ \t]*,[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*[,)]")
      list(APPEND DYNAMIC_READ_HITS "${_plug}|${_defname}")
    endif()

    # ---- 调用名
    string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_]*[ \t]*[(]" _cm "${_l}")
    set(_rc "")
    foreach(_c IN LISTS _cm)
      string(REGEX REPLACE "[ \t]*[(].*" "" _cn "${_c}")
      list(APPEND _rc "${_cn}")
    endforeach()
    list(REMOVE_DUPLICATES _rc)
    list(LENGTH _rc _nrc)
    if(_nrc GREATER 0)
      if(_nca EQUAL 0)
        list(APPEND _calls ${_rc})
      else()
        string(REPLACE ";" "," _rcj "${_rc}")
        foreach(_a IN LISTS _curacts)
          set(_actcalls "${_actcalls};${_a}=${_rcj}")
        endforeach()
      endif()
    endif()

    # ---- 种子：该 action 的 return <fn>(
    string(REGEX MATCHALL "return[ \t]+[A-Za-z_][A-Za-z0-9_:]*[ \t]*[(]" _rm "${_l}")
    foreach(_r IN LISTS _rm)
      if(_r MATCHES "return[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*[(]")
        set(_rfn "${CMAKE_MATCH_1}")
        set(_sinks "${_lineacts}")
        list(LENGTH _sinks _nsk)
        if(_nsk EQUAL 0 AND _nca GREATER 0)
          set(_sinks "${_curacts}")
          list(LENGTH _sinks _nsk)
        endif()
        if(_nsk EQUAL 0 AND _np GREATER 0)
          set(_sinks "${_pendacts}")
          list(LENGTH _sinks _nsk)
        endif()
        foreach(_a IN LISTS _sinks)
          set(_actseed "${_actseed};${_a}=${_rfn}")
        endforeach()
      endif()
    endforeach()
  endforeach()

  set(SEGKEYS_${sid} ${_keys})
  if(GIS_21C_DEBUG_FILE AND SEGFILE_${sid} MATCHES "${GIS_21C_DEBUG_FILE}")
    message(STATUS "[filedbg] sid=${sid} def=${_defname} lines=${_dbg_nlines} keys=${_keys}")
  endif()
  if(GIS_21C_DEBUG_DEFNAMES AND NOT _defname STREQUAL ""
     AND ";${GIS_21C_DEBUG_DEFNAMES};" MATCHES ";${_defname};")
    message(STATUS "[dfgdbg] sid=${sid} plug=${_plug} def=${_defname} file=${SEGFILE_${sid}} lines=${_dbg_nlines} keys=${_keys} actkeys=${_actkeys}")
  endif()
  set(SEGACTKEYS_${sid} ${_actkeys})
  set(SEGCALLS_${sid} ${_calls})
  set(SEGACTCALLS_${sid} ${_actcalls})
  list(LENGTH _actseed _nas)
  if(GIS_21C_DEBUG AND _defname MATCHES "execute$")
    message(STATUS "[dbg] seg=${sid} plug=${_plug} def=${_defname} nactseed=${_nas} keys=${_keys} calls=${_calls}")
  endif()
  if(_defname MATCHES "execute$" AND _nas GREATER 0)
    set(EXECSEED_${_plug} "${EXECSEED_${_plug}};${_actseed}")
  endif()
endforeach()

list(REMOVE_DUPLICATES PLUGIN_NAMES)
list(REMOVE_DUPLICATES UNKNOWN_READER_HITS)
list(REMOVE_DUPLICATES DYNAMIC_READ_HITS)

# ------------------------------------------------------------------ 5. 逐 action 求闭包
set(VIOLATIONS "")
set(REPORT "")
set(SCANNED_ACTIONS 0)
set(NO_SEED_CONFIGURED "")
set(NO_SEED_UNCONFIGURED "")

foreach(pair IN LISTS WL_PAIRS)
  string(REPLACE "." ";" ps "${pair}")
  list(GET ps 0 P)
  list(GET ps 1 A)
  math(EXPR SCANNED_ACTIONS "${SCANNED_ACTIONS} + 1")

  set(_frontier "")
  foreach(_as IN LISTS EXECSEED_${P})
    if(_as MATCHES "^${A}=(.*)$")
      list(APPEND _frontier "${CMAKE_MATCH_1}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _frontier)
  list(LENGTH _frontier _nfr)
  set(GIS_21C_SEEDDBG "${_frontier}")
  if(_nfr EQUAL 0)
    if(DEFINED HASCFG_${pair})
      list(APPEND NO_SEED_CONFIGURED "${pair}")
    else()
      list(APPEND NO_SEED_UNCONFIGURED "${pair}")
    endif()
    continue()
  endif()

  set(_readkeys "")
  set(_visited "")
  set(_guard 0)
  while(TRUE)
    list(LENGTH _frontier _nfr)
    if(_nfr EQUAL 0)
      break()
    endif()
    math(EXPR _guard "${_guard} + 1")
    if(_guard GREATER 4000)
      message(FATAL_ERROR "闭包展开不收敛：${pair}")
    endif()
    list(GET _frontier 0 _fn)
    list(REMOVE_AT _frontier 0)
    list(FIND _visited "${_fn}" _v)
    if(NOT _v EQUAL -1)
      continue()
    endif()
    list(APPEND _visited "${_fn}")
    string(REPLACE "," ";" _cands "${SEGBYNAME_${P}_${_fn}}")
    set(_nextcalls "")
    foreach(_cand IN LISTS _cands)
      if(_cand STREQUAL "")
        continue()
      endif()
      list(APPEND _readkeys ${SEGKEYS_${_cand}})
      list(APPEND _nextcalls ${SEGCALLS_${_cand}})
      foreach(_ac IN LISTS SEGACTKEYS_${_cand})
        if(_ac MATCHES "^${A}=(.*)$")
          list(APPEND _readkeys "${CMAKE_MATCH_1}")
        endif()
      endforeach()
      foreach(_ac IN LISTS SEGACTCALLS_${_cand})
        if(_ac MATCHES "^${A}=(.*)$")
          string(REPLACE "," ";" _cv "${CMAKE_MATCH_1}")
          list(APPEND _nextcalls ${_cv})
        endif()
      endforeach()
    endforeach()
    foreach(_nc IN LISTS _nextcalls)
      if(DEFINED SEGBYNAME_${P}_${_nc})
        list(APPEND _frontier "${_nc}")
      endif()
    endforeach()
  endwhile()

  list(REMOVE_DUPLICATES _readkeys)
  set(READKEYS_${pair} ${_readkeys})
  if(GIS_21C_TRACE_PAIR AND GIS_21C_TRACE_PAIR STREQUAL pair)
    message(STATUS "[trace] pair=${pair} seed=${GIS_21C_SEEDDBG} visited=${_visited}")
    message(STATUS "[trace] readkeys=${_readkeys}")
    message(STATUS "[trace] whitelist=${WL_${P}_${A}}")
  endif()

  set(_extra "")
  foreach(_k IN LISTS WL_${P}_${A})
    list(FIND _readkeys "${_k}" _at)
    if(_at EQUAL -1)
      list(APPEND _extra "${_k}")
    endif()
  endforeach()
  list(LENGTH _readkeys _nrk)
  list(LENGTH WL_${P}_${A} _nwk)
  string(REPLACE ";" "," _extraj "${_extra}")
  string(APPEND REPORT "${pair}|visible=${_nwk}|read=${_nrk}|extra=${_extraj}\n")
  foreach(_k IN LISTS _extra)
    list(APPEND VIOLATIONS "${pair}|${_k}")
  endforeach()
endforeach()

# ------------------------------------------------------------------ 6. 只报告列：Int 区间声明
# 口径来自 src/framework/include/gis/framework/param_spec.h 的 validate：
# Int 只有 min/max **都**是 int 重载且 *max > *min 时才拦 ⇒ 没声明区间的 Int 规格完全不校验。
# 这里同口径：一条 Int 规格里 int{} 出现 3 次（默认+min+max）才算"声明了区间"。
# 本列**只报告、不修改**：补声明区间是行为变更，批次 A 参数面已停止漂移 ⇒ 交批次 B 立项。
set(INT_DECL_TOTAL 0)
set(INT_DECL_WITH_RANGE 0)
set(INT_DECL_WITHOUT_RANGE 0)
foreach(P IN LISTS PLUGIN_NAMES)
  file(GLOB pspecfiles "${PLUGIN_DIR}/${P}/*.cpp")
  foreach(f IN LISTS pspecfiles)
    file(READ "${f}" TXT ENCODING UTF-8)
    string(REGEX REPLACE "(int|double|float|std::string|bool)[ \t]*[{][^}]*[}]" "@BV@" TXT "${TXT}")
    string(REGEX MATCHALL "ParamSpec[{][^}]*[}]" SPECS "${TXT}")
    foreach(s IN LISTS SPECS)
      if(NOT s MATCHES "ParamType::Int")
        continue()
      endif()
      if(NOT s MATCHES "\"([A-Za-z_][A-Za-z0-9_]*)\"")
        continue()
      endif()
      set(_key "${CMAKE_MATCH_1}")
      # 数 @BV@ 的个数只能用 MATCHALL：string(REPLACE) 是**全局**替换，
      # "逐个替换再计数"的循环会把剩下的标记一次清光而永远数出 1；
      # 用长度差除以标记长度又会把转义/编码算进去（实测差 3 个字符），两者都不可信。
      string(REGEX MATCHALL "@BV@" _ml "${s}")
      list(LENGTH _ml _nbv)
      math(EXPR INT_DECL_TOTAL "${INT_DECL_TOTAL} + 1")
      if(_nbv GREATER_EQUAL 3)
        math(EXPR INT_DECL_WITH_RANGE "${INT_DECL_WITH_RANGE} + 1")
        set(INTSPEC_${P}_${_key} "range")
      else()
        math(EXPR INT_DECL_WITHOUT_RANGE "${INT_DECL_WITHOUT_RANGE} + 1")
        set(INTSPEC_${P}_${_key} "norange")
      endif()
    endforeach()
  endforeach()
endforeach()

set(INT_REPORT "")
foreach(pair IN LISTS WL_PAIRS)
  string(REPLACE "." ";" ps "${pair}")
  list(GET ps 0 P)
  list(GET ps 1 A)
  set(_with 0)
  set(_without 0)
  set(_without_keys "")
  foreach(_k IN LISTS READKEYS_${pair})
    if(DEFINED INTSPEC_${P}_${_k})
      if(INTSPEC_${P}_${_k} STREQUAL "range")
        math(EXPR _with "${_with} + 1")
      else()
        math(EXPR _without "${_without} + 1")
        list(APPEND _without_keys "${_k}")
      endif()
    endif()
  endforeach()
  string(REPLACE ";" "," _wkj "${_without_keys}")
  string(APPEND INT_REPORT "${pair}|int_declared=${_with}|int_undeclared=${_without}|${_wkj}\n")
endforeach()

# ------------------------------------------------------------------ 7. 汇总输出
list(LENGTH VIOLATIONS NVIOL)
list(LENGTH NO_SEED_CONFIGURED NNOSEEDC)
list(LENGTH NO_SEED_UNCONFIGURED NNOSEEDU)
list(LENGTH DYNAMIC_READ_HITS Ndyn)
list(LENGTH GIS_21C_EXEMPTIONS NEX)

file(WRITE "${REPORT_FILE}"
    "# 21-C 反向参数守卫报告\n"
    "# 逐 action：plugin.action|visible=白名单条数|read=闭包内实读键数|extra=白名单−实读\n"
    "${REPORT}"
    "# Int 区间列（只报告不修改）：plugin.action|int_declared|int_undeclared|未声明区间的键\n"
    "${INT_REPORT}"
    "# 汇总\n"
    "# actions=${SCANNED_ACTIONS} visible_rows=${WL_CONFIGURED} no_ui_config=${WL_NOCONFIG}\n"
    "# literal_read_sites=${KEY_READ_TOTAL} files=${FILE_COUNT} lines=${LINE_TOTAL} segments=${SEG_COUNT}\n"
    "# int_specs total=${INT_DECL_TOTAL} with_range=${INT_DECL_WITH_RANGE} without_range=${INT_DECL_WITHOUT_RANGE}\n"
    "# dynamic_key_reads=${Ndyn} exemptions=${NEX}\n"
)

message(STATUS "[21C] actions=${SCANNED_ACTIONS} visible_rows=${WL_CONFIGURED} no_ui_config=${WL_NOCONFIG}")
message(STATUS "[21C] files=${FILE_COUNT} lines=${LINE_TOTAL} segments=${SEG_COUNT}")
message(STATUS "[21C] int_specs total=${INT_DECL_TOTAL} with_range=${INT_DECL_WITH_RANGE} without_range=${INT_DECL_WITHOUT_RANGE}")
message(STATUS "[21C] no_seed configured=${NNOSEEDC} unconfigured=${NNOSEEDU}")
message(STATUS "[21C] violations=${NVIOL} list=${VIOLATIONS}")
message(STATUS "[21C] dynamic_read_hits=${DYNAMIC_READ_HITS}")
message(STATUS "[21C] no_seed_configured_list=${NO_SEED_CONFIGURED}")

# ------------------------------------------------------------------ 8. 自检与红判
if(BLOCK_COMMENT)
  message(FATAL_ERROR
      "插件源码里出现了块注释（${GIS_21C_BLOCKHIT}）。本扫描器的注释口径是\"只剥 // 到行尾\"，"
      "依据是 src/plugins 全树 0 处真块注释（2 处 /* 分别在字符串字面量和 // 注释里）。"
      "前提变了就必须先扩口径再放宽这条哨兵，不许让它静默把注释当代码或把文件当注释。")
endif()

if(UNKNOWN_READER_HITS)
  message(FATAL_ERROR
      "发现 GIS_21C_READER_FUNCS 之外的读参函数（新增读参函数必须同步登记，否则字面量实读会被静默漏掉）：\n  ${UNKNOWN_READER_HITS}")
endif()

if(NOT Ndyn EQUAL NEX)
  message(FATAL_ERROR
      "键来自变量的实读点有 ${Ndyn} 处，豁免表登记了 ${NEX} 条 ⇒ 豁免表与真值不符（不许默默增长）")
endif()
foreach(_e IN LISTS GIS_21C_EXEMPTIONS)
  string(REGEX REPLACE "\\|[^|]*$" "" _esig "${_e}")
  list(FIND DYNAMIC_READ_HITS "${_esig}" _hit)
  if(_hit EQUAL -1)
    message(FATAL_ERROR
        "豁免表条目 \"${_esig}\" 已经没有对应的动态键实读点了（账清了没销账），请删除该条目")
  endif()
endforeach()

if(NNOSEEDC GREATER 0)
  message(FATAL_ERROR
      "以下**有白名单条目**的 action 找不到派发种子，扫描器不能把它当成零实读：\n  ${NO_SEED_CONFIGURED}")
endif()

# 形状自检：顶层段的数量下限。缩进假设被破坏（有人把顶层闭花括号挪了）时，段会并成几个巨段，
# 闭包因此被放大 ⇒ 静默漏报。这里用"段数不得低于每文件行数的 1/60"这种粗下限把它抓住。
math(EXPR _segfloor "${LINE_TOTAL} / 60")
if(SEG_COUNT LESS _segfloor)
  message(FATAL_ERROR
      "切段数 ${SEG_COUNT} 低于源码行数 ${LINE_TOTAL} 的下限 ${_segfloor}：插件源码的顶层闭花括号缩进约定变了，扫描器的段模型不再可信")
endif()

if(NVIOL GREATER 0)
  string(REPLACE ";" "\n  " _vtext "${VIOLATIONS}")
  message(FATAL_ERROR
      "反向参数守卫变红：以下 visibleKeys 在该 action 的调用闭包里没有任何实读点（白名单−实读 非空即红）：\n  ${_vtext}\n详见 ${REPORT_FILE}")
endif()

message(STATUS "[21C] 白名单 − 实读 = 空，守卫通过")
