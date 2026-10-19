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
# WHY 一切键集合都按"同插件目录内的调用闭包"算，而不是按 do<Action> 的函数名：
#   doCutFill 只是五行转发，实读在 runCutFillProcess。按名字切 ⇒ 每个包装型 action 都被
#   误判成零实读 ⇒ 守卫一上线满屏红。
#   口径注意：切段是**逐文件**的，但名字索引键带的是**插件目录名**（SEGBYNAME_<插件>_<定义名>），
#   所以闭包天然跨同目录内的 .cpp / .inc（实测 vector.adjacency 的 visited 跨 5 个片段）；
#   反过来它**不跨插件目录** —— 今天全仓 0 处跨目录读参（复审 §4.2 现跑核实），
#   若哪天出现跨目录 helper，方向是**漏收 ⇒ 吵的红**，可接受。
# ==============================================================================

# 允许"第一个形参是 params、第二个形参是键"的读参函数全集。新增第四个必须在这里登记，
# 否则本脚本会红 —— 这条比守卫本身更脆，因为漏登记是**静默少读**（假绿），不是假红。
set(GIS_21C_READER_FUNCS "getParam;getBandIndex;resolveOptionalAliasBand")

# 允许"以成员函数形态读键"的**点式**读参全集：`params.at("key")` / `params.find("key")`。
# WHY 单列一张表：`std::get<double>(params.at("azimuth"))` 与 `params.find("altitude")->second`
#   同样是"这个键确实被读了"，但函数式匹配式看不见它 ⇒ 白名单里的键会被判成"没人读" ⇒ **误报红**，
#   而红字给的解释是"这条白名单是冗余的"，方向完全相反（复审实测：
#   violations=2 list=terrain.hillshade|altitude;terrain.hillshade|azimuth，rc=8）。
# 出现这张表之外的第四种点式读法（如 params.count("k")）同样必须红，不许静默放过。
# 证据指针更正（21-FixA C-2）：上面那句"violations=2 …rc=8"**不是盘上事实**，
#   它来自复审的一次性注入实验，盘上任何提交态都不可再现
#   （`git log -S'params.at(' -- src/plugins` 与 `-S'params.find('` 各 0 条命中；
#   产物见 build/t21C_rev_readshape_paramsat.log）。
#   ⇒ terrain.hillshade 的 altitude/azimuth 今天走的是**函数式** getParam<double>(params, "azimuth", 315.0)，
#     不加这张表也不会红。这张表因此是**防御**（为将来有人把 getParam 重写成 params.at 而留），
#     不是对现状的修补。它的收录能力不靠那句复审数字自证，而靠第 3.5 节的**形状探针自检**：
#     tests/reader_shape_probe_sample.txt 里逐形状写死一份样例，本脚本每次运行都先扫它一遍，
#     所得键集合必须逐字等于表面对应的期望集（过收、漏收两个方向都有牙）。
set(GIS_21C_DOT_READERS "at;find")

# 形状探针资产（不参与编译的 .txt，见 tests/reader_shape_probe_sample.txt 的文件头说明）。
# 由 tests/CMakeLists.txt 的 param_guard_visible_keys_all_read 用 -DGIS_21C_SHAPE_PROBE 传入；
# 传了但文件不存在 ⇒ 红（资产不许被删掉后探针静默空转）。
# -D GIS_21C_PROBE_ONLY=TRUE 时只做探针就退出，便于手工复现探针的红。
set(GIS_21C_SHAPE_PROBE_RE "/[*]")
set(GIS_21C_SHAPE_PROBE_CLOSERE "[*]/")

# ---------------------------------------------- 块注释豁免表（21-FixA C-1，见第 8 节三条断言）
# 本扫描器的注释口径是"只剥 // 到行尾"，所以**剥完还含 /* 或 */ 的行**必须是已知形状。
# 与 GIS_21C_EXEMPTIONS 同一套自守思路：命中集合与这张表**双向**比对（多一条红、少一条也红），
# 且条数相等 ⇒ 同一条字面量在同一个文件里被复制第二遍同样会红（不 dedup 计数）。
# 键 = "<相对插件目录的文件路径>|<剥 // 并折叠空白后的整行>"；不用行号（行号会随无关改动漂移成假账）。
# 今天全树的**一处**合法命中：vector_plugin_info_filter.inc:41 的 where.find("/*") —— 它是
#   **字符串字面量**（接收者是 std::string where），不是块注释起始符。另外两处候选都不命中：
#   :40 的 where.find("--") 不含 /* 或 */；duplicate_point_check.inc:56-57 的 /** … */
#   整行都在 // 注释里，剥 // 后只剩空白 ⇒ 被 if(NOT clean STREQUAL "") 挡在命中之外。
#   实测取证：build/t21Fix_item12_green.log 的 block_comment_hits 与 build/t21Fix_blockscan.log。
# ⚠ 这条表被 ③/② 抓过一次真红（本片的自查）：豁免表初版按"全树 2 处 /*"登记了 :40 与 :41 两条，
#   而 :40 那行在盘上根本不命中 ⇒ ②（表里每条都必须有命中）当场 FATAL。
#   在那之前它还躲在一层假相等后面：CMake 的引号参数**不保护** `;`（"a;b" 是两条元素），
#   而命中行都以 `;` 收尾 ⇒ 1 处命中劈成 2 个元素、1 条豁免也劈成 2 个元素，①②③ 全部"因为两侧
#   劈得一样"而成立，等于没有断言。修法：命中侧把 `;` 统一折成 <SEMI>，本表照 <SEMI> 写，
#   两侧都是不含分号的单元素，比对才真的是比对内容。
set(GIS_21C_BLOCK_COMMENT_ALLOWLIST
    "vector/vector_plugin_info_filter.inc|if (where.find(\"/*\") != std::string::npos) return false<SEMI>")

# 显式豁免表：形如 "<插件名>|<调用所在的函数名>|<说明>"。
# WHY 按"插件+所属函数"而不是行号：行号会随无关改动漂移，漂移后的条目既命中不了任何真读点、
# 又留下一笔"已经失效还挂在表里"的假账。本表由脚本末尾两条断言把守：
#   (a) 动态键实读点数量必须 == 本表条目数 ⇒ 多一处就红（不许默默增长）；
#   (b) 本表每条都必须被实际命中        ⇒ 少一处也红（账清了必须销账）。
set(GIS_21C_EXEMPTIONS
    "spindex|getBandIndex|键来自形参 const char* key，43 个调用点全是字面量（本片实测唯一一处）")

# ---------------------------------------------- 免检桶冻结名单（K-14，见第 8 节的双向断言）
# 背景：没有 ActionUiConfig 条目的 action，导出侧只有 plugin|action|@NO_ACTION_UI_CONFIG 一行，
#   本守卫对它求的是**未定义**的 WL_<插件>_<action> ⇒ 空列表 ⇒ 差集恒空 ⇒ **一条都不判**。
#   2026-10-19 实测：126 个 action 里 39 个落进这个桶（vector 30 / processing 5 / classification 4），
#   而这三个插件都**不在** tests/test_gui_support.cpp 的 kMustBeConfigured（只含 7 个插件）里，
#   Task 3 的动作登记册又只管 GOLD/PENDING 覆盖、与 ActionUiConfig 无关
#   ⇒ "新增一个 action 但不写 actionUiConfig"这条路**三条既有守卫全绿**，无人把守（复审 §2.1，Critical）。
# 本名单**不是**给这 39 个补配置（那是 K-14 的活、批次 B 的射程，本片补它等于偷偷做 Task 10 的续集），
# 而是把桶**冻住**：条目数、条目内容与名单逐条相等才算绿 ——
#   多一条（有人新增 action 却不写 actionUiConfig，或把既有条目的 actionUiConfig 删掉）⇒ 红；
#   少一条（有人补齐了某个 actionUiConfig）⇒ 也红，逼他显式把这条从名单里销掉。
# 红字点名的三选一处置：①补 actionUiConfig ②把这条登记进本名单并写明理由 ③按 K-14 挂批次 B 的账。
# 名单按字典序；格式 "<插件名>|<action 名>"，与第 1 节收集的 NOCFG_PAIRS 同形，便于逐条比对。
# 迁移状态（K-14/N91 挂在批次 B 的账）：登记时（2026-10-19）桶里是 vector 30 / processing 5 /
#   classification 4；processing 与 classification 两族已逐条补齐 actionUiConfig 并从本名单销掉
#   —— 注意是**销掉**而不是留着"已配置但仍在名单里"，那样断言②（表里每条都必须仍是免检项）会立刻红，
#   双向冻结本就为此设计。剩下的都是 vector 那一族。
# WHY 这里不写当前条数：条数是上面三条断言的判据本身，抄进注释就成了一份会漂移的假账。
#   要读数就跑 `ctest --test-dir build/debug -C Debug -R param_guard_visible_keys_all_read
#   --output-on-failure` 看 [21C] 的 no_ui_config= 与 no_ui_config_allowlist= 两行。
set(GIS_21C_NO_UI_CONFIG_ALLOWLIST
    "vector|simplify"
    "vector|singlepart"
    "vector|sliver_remove"
    "vector|spatial_join"
    "vector|topology_check"
    "vector|union"
    "vector|vertices_extract"
)

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

# 两条读参匹配式**只在这里出现一次**：主扫描（第 4 节）与形状探针（第 3.5 节）用的是同一对变量。
# WHY 提出来：探针的意义是"证明主扫描的匹配面确实收/不收某些形状"。如果探针抄一份自己的正则，
#   有人改了主扫描的正则而没改探针 ⇒ 探针继续绿着为一份已经不存在的匹配说话（新的自证）。
#   共用变量 ⇒ 改主扫描 = 同时改探针的判据对象，探针必然当场把差异暴露成红。
set(GIS_21C_FUNC_READ_RE "[A-Za-z_][A-Za-z0-9_]*(<[^()]*>)?[ \t]*[(][ \t]*[A-Za-z_]*[Pp]arams[ \t]*,[ \t]*\"[A-Za-z_0-9]+\"")
set(GIS_21C_DOT_READ_RE "[A-Za-z_]*[Pp]arams[ \t]*[.][ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*[(][ \t]*\"[A-Za-z_0-9]+\"")

# ------------------------------------------------------------------ 3.5 形状探针自检（21-FixA C-2）
# 判据：对 tests/reader_shape_probe_sample.txt 逐形状扫一遍，
#   所得键集合 / 未知形态集合 / 块注释命中数 / 实读点总数 **逐字**等于下面四组字面期望。
# 四条期望各钉一个方向：
#   键集合 {a,b,c}   ⇒ 函数式收（a）、点式 at 收（b）、点式 find 收（c）＝**漏收方向有牙**；
#                       其中 b/c 正是"退回 fb8d205 形态就会消失"的两条 ⇒ 删点式分支必红；
#                       "f"（params->at，箭头接收者）不在集合里 ⇒ 今天不收的形态不许被悄悄算成收；
#                       "d"（真块注释里的 getParam）不在集合里 ⇒ 第 1 节块注释口径有牙；
#                       "w"（where.find("--")，接收者不是 params）不在集合里 ⇒ **过收方向有牙**；
#                       "n"（// 行注释里的 getParam）不在集合里 ⇒ 剥注释口径有牙。
#   未知集合 {params.count@probe-sample} ⇒ GIS_21C_DOT_READERS 的登记门那支（平时零覆盖）真的会响。
#   块注释命中数 == 1 ⇒ 块注释哨兵在探针里也真的会响（第 8 节主扫描侧另有三条断言）。
#   实读点总数 == 4 ⇒ 精确到"每个形状贡献几次"，去重后的键集合抓不到的重复计入它抓得到。
function(gis_21c_shape_probe sample)
  if(NOT EXISTS "${sample}")
    message(FATAL_ERROR
        "形状探针资产不存在：${sample}（传了 -DGIS_21C_SHAPE_PROBE 却找不到文件 ⇒ 探针会静默空转，禁止放行）")
  endif()
  file(STRINGS "${sample}" PLINES ENCODING UTF-8)
  set(pkeys "")
  set(punknown "")
  set(pblock "")
  set(pblocktotal 0)
  set(preadtotal 0)
  foreach(l IN LISTS PLINES)
    set(clean "${l}")
    string(REGEX REPLACE "//.*$" "" clean "${clean}")
    set(_isBlock FALSE)
    if(clean MATCHES "${GIS_21C_SHAPE_PROBE_RE}" OR clean MATCHES "${GIS_21C_SHAPE_PROBE_CLOSERE}")
      set(_isBlock TRUE)
      math(EXPR pblocktotal "${pblocktotal} + 1")
      string(REGEX REPLACE "[ \t]+" " " _bs "${clean}")
      string(REGEX REPLACE "^ | $" "" _bs "${_bs}")
      string(REPLACE ";" "<SEMI>" _bs "${_bs}")
      list(APPEND pblock "${_bs}")
    endif()
    # 与第 4 节同口径：块注释行的文字一律不当代码读（主扫描侧靠第 8 节的红兜住）。
    if(NOT _isBlock)
      string(REGEX MATCHALL "${GIS_21C_FUNC_READ_RE}" _fm "${clean}")
      foreach(_m IN LISTS _fm)
        if(_m MATCHES "^([A-Za-z_][A-Za-z0-9_]*)[<(]")
          set(_fn "${CMAKE_MATCH_1}")
          if(NOT ";${GIS_21C_READER_FUNCS};" MATCHES ";${_fn};")
            list(APPEND punknown "${_fn}@probe-sample")
          endif()
        endif()
        if(_m MATCHES "\"([A-Za-z_0-9]+)\"$")
          math(EXPR preadtotal "${preadtotal} + 1")
          list(APPEND pkeys "${CMAKE_MATCH_1}")
        endif()
      endforeach()
      string(REGEX MATCHALL "${GIS_21C_DOT_READ_RE}" _dm "${clean}")
      foreach(_m IN LISTS _dm)
        if(_m MATCHES "[A-Za-z_]*[Pp]arams[ \t]*[.][ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*[(]")
          set(_dmfn "${CMAKE_MATCH_1}")
          if(NOT ";${GIS_21C_DOT_READERS};" MATCHES ";${_dmfn};")
            list(APPEND punknown "params.${_dmfn}@probe-sample")
          endif()
        endif()
        if(_m MATCHES "\"([A-Za-z_0-9]+)\"$")
          math(EXPR preadtotal "${preadtotal} + 1")
          list(APPEND pkeys "${CMAKE_MATCH_1}")
        endif()
      endforeach()
    endif()
  endforeach()
  list(REMOVE_DUPLICATES pkeys)
  list(SORT pkeys)
  string(REPLACE ";" "," pkeysc "${pkeys}")
  list(LENGTH punknown _pnU)
  string(REPLACE ";" "," punknownc "${punknown}")

  if(NOT pkeysc STREQUAL "a,b,c")
    message(FATAL_ERROR
        "形状探针红：扫 ${sample} 所得键集合是 [${pkeysc}]，字面期望是 [a,b,c]。\n"
        "  少键 ⇒ 某个真实读参形态被漏收（假绿方向：白名单里的冗余键就此免检）；\n"
        "  多键 ⇒ 匹配面过收（把存在性探测/非 params 接收者/注释里的文字当成实读），同样是假绿方向。\n"
        "  样例文件的每种形状都在自己的行上标了名（a/b/c/d/f/w/n），照上面判据改**匹配式或表**，不许改样例。")
  endif()
  if(NOT _pnU EQUAL 1 OR NOT punknownc STREQUAL "params.count@probe-sample")
    message(FATAL_ERROR
        "形状探针红：未知读参形态集合是 [${punknownc}]（${_pnU} 条），"
        "字面期望是 [params.count@probe-sample]（1 条）。\n"
        "空集 ⇒ GIS_21C_READER_FUNCS / GIS_21C_DOT_READERS 的登记门已经不再响（静默漏收）；\n"
        "非期望值 ⇒ 匹配面动了，样例与期望必须一起重核。")
  endif()
  if(NOT pblocktotal EQUAL 1)
    message(FATAL_ERROR
        "形状探针红：样例里的块注释命中 ${pblocktotal} 处，字面期望 1 处（只有形状 d 那一行）。\n"
        "0 ⇒ 块注释口径失效（第 1 节哨兵抓不到东西）；>1 ⇒ 样例被写进了第二种含 /* 的形状。命中清单：${pblock}")
  endif()
  list(GET pblock 0 _pbsig)
  if(NOT _pbsig STREQUAL "/* getParam<std::string>(params, \"d\") 这一整条在真块注释里 不算实读 */")
    message(FATAL_ERROR
        "形状探针红：唯一的块注释命中内容与字面期望不符：[${_pbsig}]。\n"
        "⇒ 形状 d 被挪走/改写了，探针就不再为\"块注释里的读参不算实读\"这一条说话。")
  endif()
  if(NOT preadtotal EQUAL 4)
    message(FATAL_ERROR
        "形状探针红：实读点总数 ${preadtotal}，字面期望 4（a 函数式 + b 点式 at + c 点式 find + c 点式 count）。\n"
        "键集合去重后看不出重复计入，这条按次数钉 ⇒ 同一个形状被匹配两遍（正则退化）也会红。")
  endif()
  message(STATUS "[21C] shape_probe keys=${pkeysc} read_sites=${preadtotal} block_hits=${pblocktotal} unknown=${punknownc}")
endfunction()

if(GIS_21C_SHAPE_PROBE)
  gis_21c_shape_probe("${GIS_21C_SHAPE_PROBE}")
endif()
if(GIS_21C_PROBE_ONLY)
  return()
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
    # 本仓只有 execute_process 的 TIMEOUT 真执法（目录属性 PROCESS_TIMEOUT 不是 CMake 属性，
    # 见 tests/CMakeLists.txt:161-163 的注释）。WHY 必须给：导出子步实测 0.134s，而本用例整体
    # 吃到的是用例级 TIMEOUT 300 —— 不设这道闸，导出侧一旦挂住要等满 300s 才红，
    # 红字还只会说"超时"，报不出是导出卡住还是扫描卡住。
    TIMEOUT 60
)
if(NOT EXPORT_RC EQUAL 0)
  message(FATAL_ERROR
      "导出用例 GuiContract.ExportActionVisibleKeys 运行失败 rc=${EXPORT_RC}（rc=<not-set> 或非数字多为 TIMEOUT 60 到点）\n${EXPORT_OUT}\n${EXPORT_ERR}")
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
set(NOCFG_PAIRS "")
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
      list(FIND NOCFG_PAIRS "${p}|${a}" _ncSeen)
      if(_ncSeen EQUAL -1)
        list(APPEND NOCFG_PAIRS "${p}|${a}")
      endif()
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
# 块注释命中**跨文件累积**（21-FixA C-1）。
# 复位原来写在每文件循环体内（set(BLOCK_COMMENT FALSE)），而判定在所有循环之后（if(BLOCK_COMMENT)）
# ⇒ 最终值只反映扫描序列里**最后一个文件**，前面 21 个插件目录、54 个文件里的一切块注释
#   都被后续文件的复位抹掉：今天这条哨兵确实在 vector_plugin_info_filter.inc:41 触发过一次 TRUE，
#   然后被后面的文件复位成 FALSE 而放过 ⇒ 守卫绿恰恰证明它在空转，而不是证明前提成立。
# 改成累积清单 + 第 8 节与 GIS_21C_BLOCK_COMMENT_ALLOWLIST 双向比对（三向：成员、销账、条数）。
set(BLOCK_HITS "")

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
        # 命中一律**跨文件累积**进 BLOCK_HITS，由第 8 节与 GIS_21C_BLOCK_COMMENT_ALLOWLIST 双向比对；
        # 这里的 set(BLOCK_COMMENT) 已经删掉了——旧写法是"每文件复位 + 末尾读一次"，
        # 于是最终值只反映最后一个被扫文件，前面所有文件的块注释都被复位抹掉 ⇒ 哨兵空转（C-1）。
        # 同时第 4 节把命中行的读参匹配面摘掉（见 _readline）⇒ 不许静默把注释里的文字当代码读。
        if(clean MATCHES "${GIS_21C_SHAPE_PROBE_RE}" OR clean MATCHES "${GIS_21C_SHAPE_PROBE_CLOSERE}")
          string(REGEX REPLACE "[ \t]+" " " _bsig "${clean}")
          string(REGEX REPLACE "^ | $" "" _bsig "${_bsig}")
          # 分号折成 <SEMI>：见 GIS_21C_BLOCK_COMMENT_ALLOWLIST 上方那条 CMake 列表语义说明。
          string(REPLACE ";" "<SEMI>" _bsig "${_bsig}")
          list(APPEND BLOCK_HITS "${frel}|${_bsig}")
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

    # ---- 块注释口径与第 3 节、第 3.5 节探针**共用同一条判据**：含 /* 或 */ 的行不当代码读。
    # WHY 不只是"报红"：光靠第 8 节的红，白名单−实读的比对在这一轮里其实已经把这些文字当代码算过了
    #   （只是没人看报表）。这里先把它摘掉，第 8 节再决定"这种行到底该不该存在"⇒
    #   判据与报警各管一半，任何一侧退化都会被抓到（探针第 3.5 节钉的就是这条）。
    set(_readline "${_l}")
    if(_l MATCHES "${GIS_21C_SHAPE_PROBE_RE}" OR _l MATCHES "${GIS_21C_SHAPE_PROBE_CLOSERE}")
      set(_readline "")
    endif()

    # ---- 读键（字面量第二实参）
    string(REGEX MATCHALL "${GIS_21C_FUNC_READ_RE}" _mm "${_readline}")
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

    # ---- 读键（点式形态：params.at("k") / params.find("k")->second）
    # 与上面的函数式形态**同口径**计入实读：同一个键无论是 getParam 还是 params.at 读的，
    # 都是"这个 action 真的读了它"。以前只认函数式 ⇒ 有人把 getParam 重写成 params.at，
    # 守卫会报一条语义完全相反的白（"这条白名单没人读"）而真因（扫描器看不见这种读法）不在红字里。
    string(REGEX MATCHALL "${GIS_21C_DOT_READ_RE}" _dm "${_readline}")
    foreach(_m IN LISTS _dm)
      set(_dmfn "")
      if(_m MATCHES "[A-Za-z_]*[Pp]arams[ \t]*[.][ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*[(]")
        set(_dmfn "${CMAKE_MATCH_1}")
        if(NOT ";${GIS_21C_DOT_READERS};" MATCHES ";${_dmfn};")
          list(APPEND UNKNOWN_READER_HITS "params.${_dmfn}@${_plug}")
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
    # 同样走 ${_readline}：块注释行里的 getParam(…, 变量, …) 不许进豁免表的账（第 3.5 节探针钉这条）。
    if(_readline MATCHES "getParam(<[^()]*>)?[(][ \t]*[A-Za-z_]*[Pp]arams[ \t]*,[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]*[,)]")
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
# 口径来自 src/framework/include/gis/framework/param_spec.h 的 validate 的 ParamType::Int 分支
# （按符号取，别写裸行号 —— K-26 那一族）：
#   const auto* minValue = std::get_if<int>(&spec.minValue);
#   const auto* maxValue = std::get_if<int>(&spec.maxValue);
#   if (minValue && maxValue && *maxValue > *minValue && (v < *min || v > *max)) { 拦 }
# ⇒ 三条都得判，缺一条都不算"区间生效"：
#   ① min/max 都必须是 **int 重载**（写成 double{} 的话 std::get_if<int> 返回空 ⇒ 运行时不拦）；
#   ② **\*max > \*min**（min==max 或反过来时运行时整个条件短路 ⇒ 完全不校验）；
#   ③ 三个值都得在（只给默认值 ⇒ minValue/maxValue 用结构体默认的 int{0}/int{0} ⇒ max<=min ⇒ 不拦）。
# 本列**只报告、不修改**：补声明区间是行为变更，批次 A 参数面已停止漂移 ⇒ 交批次 B 立项。
#
# 折算方式（两处坑都要留着注释）：
# * 词边界：折 `int{…}` 时**必须**要求关键字前面不是标识符字符，否则
#   `points.push_back(ProfilePoint{std::stod(xText), std::stod(yText)})` 的尾巴 `int{`
#   会被当成一次 `int{…}` 折走（实测 src/plugins/terrain/terrain_plugin.cpp 两处，
#   用 `grep -rnE "[A-Za-z_0-9](int|double|float|bool|string)\{" src/plugins/` 复核）。
#   今天这两处不在 ParamSpec 体内所以不影响 total=39，但一旦有人给 Int 规格写一个
#   以 …int / …Point 结尾的 brace-init 默认值，@BV@ 计数就会虚高，把"没声明区间"报成"声明了"。
# * 折出来的记号要**带上类型和值**（@BV:<类型>:<值>@），否则第 ②③ 条无从比较 ——
#   旧口径只数"三个尖括号值都在"，从不比 max 与 min（复审 §5.3）。
set(INT_DECL_TOTAL 0)
set(INT_DECL_EFFECTIVE 0)
set(INT_DECL_DEGENERATE 0)
set(INT_DECL_UNDECLARED 0)
set(INT_DEGENERATE_KEYS "")
foreach(P IN LISTS PLUGIN_NAMES)
  file(GLOB pspecfiles "${PLUGIN_DIR}/${P}/*.cpp")
  foreach(f IN LISTS pspecfiles)
    file(READ "${f}" TXT ENCODING UTF-8)
    string(REGEX REPLACE "(^|[^A-Za-z_0-9])(int|double|float|std::string|bool)[ \t]*[{]([^}]*)[}]" "\\1@BV:\\2:\\3@" TXT "${TXT}")
    string(REGEX MATCHALL "ParamSpec[{][^}]*[}]" SPECS "${TXT}")
    foreach(s IN LISTS SPECS)
      if(NOT s MATCHES "ParamType::Int")
        continue()
      endif()
      if(NOT s MATCHES "\"([A-Za-z_][A-Za-z0-9_]*)\"")
        continue()
      endif()
      set(_key "${CMAKE_MATCH_1}")
      math(EXPR INT_DECL_TOTAL "${INT_DECL_TOTAL} + 1")
      # 数记号只能用 MATCHALL：string(REPLACE) 是**全局**替换，
      # "逐个替换再计数"的循环会把剩下的标记一次清光而永远数出 1；
      # 用长度差除以标记长度又会把转义/编码算进去（实测差 3 个字符），两者都不可信。
      # 这里只数 int 重载的记号 —— 与 std::get_if<int> 同口径：double{} 声明的 min/max
      # 在运行时拿不到，因此**不能**算"声明了区间"。
      string(REGEX MATCHALL "@BV:int:[^@]*@" _im "${s}")
      list(LENGTH _im _nint)
      if(_nint LESS 3)
        math(EXPR INT_DECL_UNDECLARED "${INT_DECL_UNDECLARED} + 1")
        set(INTSPEC_${P}_${_key} "undeclared")
        continue()
      endif()
      # 位置：ParamSpec{key, name, desc, type, required, defaultValue, minValue, maxValue, …}
      # ⇒ 三个 int 记号依序是 默认/min/max。多于三个时取最后两个当 min/max。
      math(EXPR _imax "${_nint} - 1")
      math(EXPR _imin "${_nint} - 2")
      list(GET _im ${_imin} _tokmin)
      list(GET _im ${_imax} _tokmax)
      set(_minval "")
      set(_maxval "")
      if(_tokmin MATCHES "@BV:int:[ \t]*(-?[0-9]+)[ \t]*@")
        set(_minval "${CMAKE_MATCH_1}")
      endif()
      if(_tokmax MATCHES "@BV:int:[ \t]*(-?[0-9]+)[ \t]*@")
        set(_maxval "${CMAKE_MATCH_1}")
      endif()
      if(_minval STREQUAL "" OR _maxval STREQUAL "")
        # 值不是纯整数字面量（例如有人写了 int{kFoo}）⇒ 本列算不出来，不许当成"生效"。
        math(EXPR INT_DECL_DEGENERATE "${INT_DECL_DEGENERATE} + 1")
        set(INTSPEC_${P}_${_key} "unparsed")
        list(APPEND INT_DEGENERATE_KEYS "${P}.${_key}(值无法解析)")
        continue()
      endif()
      if(_maxval GREATER _minval)
        math(EXPR INT_DECL_EFFECTIVE "${INT_DECL_EFFECTIVE} + 1")
        set(INTSPEC_${P}_${_key} "effective")
      else()
        # 三个值都在但 max <= min：运行时 *maxValue > *minValue 短路 ⇒ 声明了却完全不拦。
        math(EXPR INT_DECL_DEGENERATE "${INT_DECL_DEGENERATE} + 1")
        set(INTSPEC_${P}_${_key} "degenerate")
        list(APPEND INT_DEGENERATE_KEYS "${P}.${_key}(min=${_minval},max=${_maxval})")
      endif()
    endforeach()
  endforeach()
endforeach()

set(INT_REPORT "")
foreach(pair IN LISTS WL_PAIRS)
  string(REPLACE "." ";" ps "${pair}")
  list(GET ps 0 P)
  list(GET ps 1 A)
  set(_eff 0)
  set(_degen 0)
  set(_undecl 0)
  set(_undecl_keys "")
  foreach(_k IN LISTS READKEYS_${pair})
    if(DEFINED INTSPEC_${P}_${_k})
      if(INTSPEC_${P}_${_k} STREQUAL "effective")
        math(EXPR _eff "${_eff} + 1")
      elseif(INTSPEC_${P}_${_k} STREQUAL "degenerate" OR INTSPEC_${P}_${_k} STREQUAL "unparsed")
        math(EXPR _degen "${_degen} + 1")
      else()
        math(EXPR _undecl "${_undecl} + 1")
        list(APPEND _undecl_keys "${_k}")
      endif()
    endif()
  endforeach()
  string(REPLACE ";" "," _ukj "${_undecl_keys}")
  string(APPEND INT_REPORT "${pair}|int_effective=${_eff}|int_degenerate=${_degen}|int_undeclared=${_undecl}|${_ukj}\n")
endforeach()

# ------------------------------------------------------------------ 7. 汇总输出
list(LENGTH VIOLATIONS NVIOL)
list(LENGTH NO_SEED_CONFIGURED NNOSEEDC)
list(LENGTH NO_SEED_UNCONFIGURED NNOSEEDU)
list(LENGTH DYNAMIC_READ_HITS Ndyn)
list(LENGTH GIS_21C_EXEMPTIONS NEX)
list(LENGTH GIS_21C_NO_UI_CONFIG_ALLOWLIST NNOCFGREG)
list(LENGTH NOCFG_PAIRS NNOCFGPAIRS)
list(LENGTH INT_DEGENERATE_KEYS NDEGEN)
list(LENGTH BLOCK_HITS NBLOCK)
list(LENGTH GIS_21C_BLOCK_COMMENT_ALLOWLIST NBLOCKREG)

file(WRITE "${REPORT_FILE}"
    "# 21-C 反向参数守卫报告\n"
    "# 逐 action：plugin.action|visible=白名单条数|read=闭包内实读键数|extra=白名单−实读\n"
    "${REPORT}"
    "# Int 区间列（只报告不修改）：plugin.action|int_effective=三值齐全且max>min|int_degenerate=声明了但max<=min或值无法解析|int_undeclared=没声明区间|键清单\n"
    "${INT_REPORT}"
    "# 汇总\n"
    "# actions=${SCANNED_ACTIONS} visible_rows=${WL_CONFIGURED} no_ui_config=${WL_NOCONFIG}\n"
    "# no_ui_config_allowlist_registered=${NNOCFGREG} actual_no_ui_config_pairs=${NNOCFGPAIRS}\n"
    "# literal_read_sites=${KEY_READ_TOTAL} files=${FILE_COUNT} lines=${LINE_TOTAL} segments=${SEG_COUNT}\n"
    "# int_specs total=${INT_DECL_TOTAL} effective=${INT_DECL_EFFECTIVE} degenerate=${INT_DECL_DEGENERATE} undeclared=${INT_DECL_UNDECLARED}\n"
    "# dynamic_key_reads=${Ndyn} exemptions=${NEX}\n"
    "# block_comment_hits=${NBLOCK} block_comment_allowlist=${NBLOCKREG}\n"
)

message(STATUS "[21C] actions=${SCANNED_ACTIONS} visible_rows=${WL_CONFIGURED} no_ui_config=${WL_NOCONFIG}")
message(STATUS "[21C] no_ui_config_allowlist=${NNOCFGREG} actual=${NNOCFGPAIRS}")
message(STATUS "[21C] files=${FILE_COUNT} lines=${LINE_TOTAL} segments=${SEG_COUNT}")
message(STATUS "[21C] int_specs total=${INT_DECL_TOTAL} effective=${INT_DECL_EFFECTIVE} degenerate=${INT_DECL_DEGENERATE} undeclared=${INT_DECL_UNDECLARED}")
message(STATUS "[21C] no_seed configured=${NNOSEEDC} unconfigured=${NNOSEEDU}")
message(STATUS "[21C] violations=${NVIOL} list=${VIOLATIONS}")
message(STATUS "[21C] dynamic_read_hits=${DYNAMIC_READ_HITS}")
message(STATUS "[21C] block_comment_hits=${NBLOCK} allowlist=${NBLOCKREG}")
message(STATUS "[21C] no_seed_configured_list=${NO_SEED_CONFIGURED}")

# ------------------------------------------------------------------ 8. 自检与红判
# ---- 块注释前提哨兵（21-FixA C-1：三条断言，与 GIS_21C_BLOCK_COMMENT_ALLOWLIST 双向比对）
# ① 每一处命中都必须在表里 ⇒ 新增一处真块注释（或任何新的含 /* 形状）＝红；
# ② 表里每一条都必须仍有命中 ⇒ 已登记的那两处字面量被改写/删除后，表不许继续挂着＝红；
# ③ 命中条数必须等于表长 ⇒ 同一条字面量被复制第二遍（内容相同、位置不同也一样）＝红，
#    堵住"①② 各自都过但总数变多"的空子（去重比对做不到这一点）。
foreach(_b IN LISTS BLOCK_HITS)
  if(NOT "${_b}" IN_LIST GIS_21C_BLOCK_COMMENT_ALLOWLIST)
    string(REPLACE ";" "\n  " _btext "${BLOCK_HITS}")
    message(FATAL_ERROR
        "插件源码里出现了块注释/剥完 // 仍含 /* 或 */ 的行，且不在 GIS_21C_BLOCK_COMMENT_ALLOWLIST 里：\n"
        "  ${_b}\n"
        "命中清单（全部 ${NBLOCK} 处，豁免表 ${NBLOCKREG} 条）：\n  ${_btext}\n"
        "本扫描器的注释口径是\"只剥 // 到行尾\"，依据是豁免表逐条钉住的这几处字符串字面量。"
        "前提变了必须先扩口径（做真正的 /* */ 状态机并同步第 3.5 节探针的形状 d）再放宽这条哨兵，"
        "不许让它静默把注释当代码读或把文件当注释。")
  endif()
endforeach()
foreach(_b IN LISTS GIS_21C_BLOCK_COMMENT_ALLOWLIST)
  if(NOT "${_b}" IN_LIST BLOCK_HITS)
    message(FATAL_ERROR
        "块注释豁免表条目 ${_b} 已经没有对应命中的行（账清了没销账）：该处的 /* 字面量被改写或删除了。"
        "请删除这条豁免，不许留着当装饰。")
  endif()
endforeach()
if(NOT NBLOCK EQUAL NBLOCKREG)
  message(FATAL_ERROR
      "块注释命中 ${NBLOCK} 处，豁免表登记 ${NBLOCKREG} 条 ⇒ 有条目被命中了两次以上"
      "（同一条含 /* 的字面量被复制），命中清单：${BLOCK_HITS}")
endif()

if(UNKNOWN_READER_HITS)
  message(FATAL_ERROR
      "发现 GIS_21C_READER_FUNCS / GIS_21C_DOT_READERS 之外的读参形态"
      "（函数式与点式都必须先登记再用，否则字面量实读会被静默漏掉）：\n  ${UNKNOWN_READER_HITS}")
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

# ---- 免检桶冻结（复审 §2.1 的 Critical，新挂账 N91；名单见文件头 GIS_21C_NO_UI_CONFIG_ALLOWLIST）
# 三条断言与 GIS_21C_EXEMPTIONS 同一套自守思路：**名单本身要被断言**，不许默默长大也不许默默变小。
if(NOT NNOCFGREG EQUAL WL_NOCONFIG)
  message(FATAL_ERROR
      "免检桶（没有 actionUiConfig、本守卫一条都不判的 action）实测 ${WL_NOCONFIG} 行，"
      "冻结名单登记了 ${NNOCFGREG} 条 ⇒ 桶长大了就等于给自己开了免检通道（N91）。")
endif()
foreach(_np IN LISTS GIS_21C_NO_UI_CONFIG_ALLOWLIST)
  list(FIND NOCFG_PAIRS "${_np}" _npHit)
  if(_npHit EQUAL -1)
    message(FATAL_ERROR
        "免检名单登记了 ${_np}，但产物里它已经**不是**免检项了：要么它的 @NO_ACTION_UI_CONFIG 行"
        "换了写法，要么这个 action 已经补齐了 actionUiConfig ⇒ 请把这条从名单里销掉。")
  endif()
endforeach()
foreach(_np IN LISTS NOCFG_PAIRS)
  list(FIND GIS_21C_NO_UI_CONFIG_ALLOWLIST "${_np}" _npHit)
  if(_npHit EQUAL -1)
    message(FATAL_ERROR
        "免检桶里出现没登记的新条目 ${_np}：这个 action 在 GUI 侧没有 actionUiConfig 条目，"
        "本守卫对它是**恒空差集、一条都不判**，而动作登记册只管 GOLD/PENDING、"
        "PlanAConfigKeys 的 kMustBeConfigured 只含 7 个插件 ⇒ 三条既有守卫全绿，无人把守。"
        "三选一处置：①在 src/gui/gui_data_support.cpp 给它补 actionUiConfig（首选，直接进白名单比对）；"
        "②把 \"${_np}\" 显式登记进 GIS_21C_NO_UI_CONFIG_ALLOWLIST 并写明理由；"
        "③按 K-14/N91 挂批次 B 的账并在这里留一条可追踪的说明。")
  endif()
endforeach()

# ---- 第 6 节的解析空转哨兵（复审 §5.4：报告原先声称有这条断言，盘上没有）
# 注意这不是"为 Int 列设红"（工单禁止的是把 int_undeclared>0 当红判据），
# 而是"解析器不许静默归零"：@BV@ 折算或 ParamSpec{} 切片哪天失效，total 会静默变小甚至 0，
# 守卫照样绿，批次 B 拿到的就是张空表。
if(INT_DECL_TOTAL EQUAL 0)
  message(FATAL_ERROR
      "第 6 节一条 ParamType::Int 规格都没解析出来（INT_DECL_TOTAL=0）。"
      "插件源码里明明有 grep -c 命中的 ParamType::Int ⇒ 是折算/切片失效（注释口径、词边界、"
      "brace-init 写法变了），不是\"没有 Int 规格\"。报表的 Int 列不可信，禁止当空表放行。")
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
      "反向参数守卫变红：以下 visibleKeys 在该 action 的调用闭包里没有任何实读点（白名单−实读 非空即红）：\n  ${_vtext}\n"
      "先分清是哪种情况再动手，别直接删白名单：\n"
      "  (a) 白名单确实冗余 ⇒ 从 src/gui/gui_data_support.cpp 的 visibleKeys 里移出该键；\n"
      "  (b) 该键**其实被读了，只是用了扫描器登记的读法之外的形态**（GIS_21C_READER_FUNCS /"
      " GIS_21C_DOT_READERS 之外的第四个函数式或点式读法）。这种红是**误报**，语义与红字相反。\n"
      "      排查方向：对报出来的键先 `grep -rn '\"<键名>\"' src/plugins/<插件目录>` 看命中在哪，\n"
      "      再 `grep -rnE '[A-Za-z_]*[Pp]arams[ \\t]*[.][ \\t]*[A-Za-z_][A-Za-z0-9_]*[ \\t]*[(]' src/plugins/<插件目录>`"
      " 查点式读法；确认形态后**登记进脚本头部对应的表**，不许放宽比较式。\n"
      "详见 ${REPORT_FILE}")
endif()

message(STATUS "[21C] 白名单 − 实读 = 空，守卫通过")
