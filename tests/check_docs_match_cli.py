#!/usr/bin/env python
"""文档参数表 ↔ 二进制参数契约 的守卫（批次 C 计划里的 Task C21/C24）。

期望侧（二进制，三个来源都是编译进产物的表）：
  1. `src/gui/gui_data_support.cpp` 的 `actionUiConfigStorage()`
     —— 每个动作在 GUI 面板里真正渲染的键（visibleKeys）与真正标必填的键（requiredKeys）；
  2. 同文件的 `commonParamTextStorage()` / `actionSpecificParamTextStorage()` / `genericActionDisplayName()`
     —— 面板显示的动作名与键标签；
  3. `<cli> <plugin> --help` —— CLI 实际契约：`[required|optional]`、`Default:`、`Options:`。

文档侧：`docs/算法说明/<主功能>/<动作名>.md` 的 `## 参数` 表（列：参数/类型/必填/默认值/说明）。
比对维度：**行数、参数名、必填、默认值、类型、说明非空、枚举候选值集合**。
`类型` 的真值来自第 4 个载体：插件源码里 `paramSpecs()` 的 `ParamSpec{key, …, ParamType::X, …}`
（逐条按大括号配对取第一个字符串当键、取 `ParamType::` 当类型，**先剥注释**——本轮未剥注释的版本
多抓到一句注释里的 `ParamSpec{"k", …}`，而全仓并没有声明过键 "k"）。
`enumValues`/`Options:` 的集合对撞：文档说明里用反引号列了候选值的行，集合必须等于 `--help` 的 `Options:`；
只取**纯 ASCII** 的反引号串（中文文档也用反引号引用**别的参数名**，现读唯一一处是 raster_math.reclassify 的
`interval` 按 `间隔步长` 等距切分；Options 恒为 ASCII ⇒ 非 ASCII 引用不参与对撞）。
**本判据是条件式的**（只在文档列了候选值时才比较）。现读 34 行有 Options，其中 **7 行一个候选值都没列**
（如 georef 两篇的 resample 只写"重采样方式"）⇒ 这 7 行是**已知未覆盖面**，要收就得逐条补文档，另立刀。
绿臂里放一个中文参数名引用当诱饵，就是为了证明"按 ASCII 收窄"没有把该比的行一起免掉。

不静音：任何"这条其实不是漂移"的判定都必须写进 `tests/docs_sync_allowlist.json` 的 `verified`
里并带证据（源码 文件:行）；反过来，**白名单里的死条目也算失败** —— 漂移被修好后忘了删条目，
下一次同处真漂移就会被这条陈旧豁免吞掉。

自证：`--self-test` 用一份合成输入（2 键 3 行的假文档，故意埋 6 类缺陷）跑同一套判据，
四类缺陷必须全被点名，且一条用不上的豁免必须报错。判据退化会先在这里红。

复算：
  python tests/check_docs_match_cli.py --cli <build/.../gis-cli.exe>
  python tests/check_docs_match_cli.py --self-test
退出码：0 = 干净；1 = 有未豁免的漂移 / 白名单死条目 / 表结构异常；2 = 用法或环境错误。
"""
import argparse, json, pathlib, re, subprocess, sys, tempfile

DOC_DIR = {
    "spindex": ["光谱指数"], "georef": ["几何校正与辐射处理"], "terrain": ["地形分析"],
    "processing": ["影像处理与分析"], "cutting": ["影像裁切与镶嵌"], "projection": ["投影转换"],
    "matching": ["特征匹配与配准"], "vector": ["矢量数据处理"], "classification": ["分类统计"],
    "raster_inspect": ["栅格工具"], "raster_manage": ["栅格工具"],
    "raster_math": ["栅格工具"], "raster_render": ["栅格工具"],
}
NO_DEF = ("无", "-", "—", "", "空", "（空）", "(空)", "不适用", "--")
STR = re.compile(r'QStringLiteral\("((?:[^"\\]|\\.)*)"\s*\)|"((?:[^"\\]|\\.)*)"')

REPORT = []

# 总览链接的提取式。`[^)]+` 而不是 `[^)\s]+`：仓库里有 12 个文件名带空格
# （`C 校正.md`/`SVM 分类.md`/`NoData 设置.md`…），排除空格的写法会把它们截成
# "几何校正与辐射处理/C" 这种假路径 ⇒ 12 条 unlinked 假红（本轮真撞到过，见 --self-test 臂 6）。
OVERVIEW_LINK_RE = re.compile(r"\]\(\.?/?([^)]+\.md)\)")


def say(line):
    """stdout 上是 ASCII 转义形态（见 main 里的编码说明），同一行的原文进 REPORT 落盘。"""
    REPORT.append(line)
    print(line)


def strip_comments(s):
    """剥 // 与 /* */ 注释，字符串/字符字面量内部不剥。
    WHY：不剥注释时一条 `//` 会被后面的解析当成一个字段，把 visibleKeys/requiredKeys
    整体右移一位（terrain.viewshed 就被这么误判成"白名单为空"过）。"""
    out, i, n = [], 0, len(s)
    while i < n:
        ch = s[i]
        if ch == "/" and i + 1 < n and s[i + 1] == "/":
            j = s.find("\n", i)
            i = n if j < 0 else j
            continue
        if ch == "/" and i + 1 < n and s[i + 1] == "*":
            j = s.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" if "\n" in s[i:j] else " ")
            i = j
            continue
        if ch in "\"'":
            quote, j = ch, i + 1
            while j < n:
                if s[j] == "\\":
                    j += 2
                    continue
                if s[j] == quote:
                    j += 1
                    break
                j += 1
            out.append(s[i:j])
            i = j
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def grab_initializer(s, fn_name, var_name):
    i = s.index(fn_name)
    j = s.index(var_name + " = {", i)
    k = s.index("{", j)
    depth, in_str, p = 0, None, k
    while p < len(s):
        c = s[p]
        if in_str:
            if c == "\\":
                p += 2
                continue
            if c == in_str:
                in_str = None
        elif c in "\"'":
            in_str = c
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return s[k:p + 1]
        p += 1
    raise SystemExit("2 未配平的初始化列表：" + fn_name)


def tokenize(chunk):
    pos = 0
    while pos < len(chunk):
        ch = chunk[pos]
        if ch in " \t\r\n":
            pos += 1
            continue
        if ch in "{}":
            yield ("punct", ch); pos += 1; continue
        if ch == ",":
            yield ("punct", ","); pos += 1; continue
        m = STR.match(chunk, pos)
        if m:
            yield ("str", m.group(1) if m.group(1) is not None else m.group(2))
            pos = m.end(); continue
        nxt = min((p for p in [chunk.find("{", pos), chunk.find("}", pos), chunk.find(",", pos)]
                   if p >= 0), default=len(chunk))
        if chunk[pos:nxt].strip():
            yield ("other", chunk[pos:nxt].strip())
        pos = nxt


def parse(chunk):
    toks, idx = list(tokenize(chunk)), [0]

    def peek():
        return toks[idx[0]] if idx[0] < len(toks) else (None, None)

    def value():
        kind, val = peek()
        if kind == "punct" and val == "{":
            idx[0] += 1
            items = []
            while True:
                k, v = peek()
                if k is None:
                    break
                if k == "punct" and v == "}":
                    idx[0] += 1
                    break
                if k == "punct" and v == ",":
                    idx[0] += 1
                    continue
                items.append(value())
            return items
        idx[0] += 1
        return (("s", val) if kind == "str" else ("o", val))

    return value()


def as_pairs(node):
    return {it[0][1]: it[1] for it in node
            if isinstance(it, list) and len(it) >= 2 and it[0][0] in ("s", "o")}


def keyset(node):
    return sorted(x[1] for x in node if isinstance(x, tuple) and x[0] == "s") if isinstance(node, list) else []


def load_gui_tables(root):
    text = strip_comments(pathlib.Path(root, "src/gui/gui_data_support.cpp").read_text(encoding="utf-8"))
    text = text.replace("QString{}", 'QStringLiteral("")')
    ui = {}
    shape_bad = []
    for plugin, body in as_pairs(parse(grab_initializer(text, "actionUiConfigStorage()", "kConfigs"))).items():
        acts = {}
        for action, cfg in as_pairs(body).items():
            f = cfg if isinstance(cfg, list) else []
            if len(f) != 4 or not isinstance(f[2], list) or not isinstance(f[3], list):
                shape_bad.append("%s.%s fields=%d" % (plugin, action, len(f)))
            name = f[0][1] if len(f) > 0 and f[0][0] == "s" else None
            acts[action] = {"displayName": name, "visible": keyset(f[2]) if len(f) > 2 else [],
                            "required": keyset(f[3]) if len(f) > 3 else []}
        ui[plugin] = acts
    common = {k: (v[0][1] if v and v[0][0] == "s" else None)
              for k, v in as_pairs(parse(grab_initializer(text, "commonParamTextStorage()", "kTexts"))).items()}
    spec = {}
    for plugin, body in as_pairs(parse(grab_initializer(text, "actionSpecificParamTextStorage()", "kTexts"))).items():
        for action, ab in as_pairs(body).items():
            spec.setdefault(plugin, {})[action] = {
                k: (v[0][1] if v and v[0][0] == "s" else None) for k, v in as_pairs(ab).items()}
    labels = {it[0][1]: (it[1][1] if it[1][0] == "s" else None)
              for it in parse(grab_initializer(text, "genericActionDisplayName", "kLabels"))
              if isinstance(it, list) and len(it) >= 2 and it[0][0] == "s"}
    for plugin, acts in ui.items():
        for action, cfg in acts.items():
            cfg["nameSource"] = "ActionUiConfig" if cfg["displayName"] else "genericActionDisplayName"
            if not cfg["displayName"]:
                cfg["displayName"] = labels.get(action)
    return ui, common, spec, labels, shape_bad


def load_cli_help(cli, plugins):
    help_spec = {}
    for plugin in plugins:
        r = subprocess.run([str(cli), plugin, "--help"], capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        if r.returncode != 0:
            raise SystemExit("2 gis-cli %s --help 退出码 %d ⇒ 二进制或插件路径不对" % (plugin, r.returncode))
        keys, cur = {}, None
        for line in r.stdout.splitlines():
            m = re.match(r"^\s+--([A-Za-z_0-9]+)\s+\[(required|optional)\]\s*$", line)
            if m:
                cur = m.group(1)
                keys[cur] = {"required": m.group(2) == "required", "default": None,
                             "label": None, "options": None}
                continue
            if cur is None:
                continue
            m = re.match(r"^\s{4}([^:：]+)[:：]\s*(.*)$", line)
            if m and keys[cur]["label"] is None:
                keys[cur]["label"] = m.group(1).strip()
                continue
            m = re.match(r"^\s+Default:\s*(.*)$", line)
            if m:
                keys[cur]["default"] = m.group(1).strip()
                continue
            m = re.match(r"^\s+Options:\s*(.*)$", line)
            if m:
                keys[cur]["options"] = m.group(1).strip()
        help_spec[plugin] = keys
    return help_spec


# ParamType -> 文档第 2 列的用词。本仓文档侧类型词表实测封闭为这 9 个词（594 行、无第 10 种），
# 与 enum class ParamType 的 9 个成员一一对应，所以这张表是双射而不是猜测。
DOC_TYPE_OF_PARAM_TYPE = {
    "String": "字符串",
    "Int": "整数",
    "Double": "浮点数",
    "Bool": "布尔值",
    "FilePath": "文件路径",
    "DirPath": "目录路径",
    "Enum": "枚举",
    "Extent": "范围",
    "CRS": "坐标系",
}


def load_param_types(root):
    """从插件源码取 (plugin, key) -> ParamType 成员名。

    WHY 必须先剥注释：本轮取证时未剥注释的版本多抓到 1 个"条目"，它是 processing_plugin.cpp
    里一句注释写的 `ParamSpec{"k", ...}`（全仓并没有声明过键 "k"）——与 CoverageMeta 判据
    要求剥注释是同一个坑。WHY 要报计数：类型判据只在"行配上二进制键"之后才跑，
    解析数若悄悄掉到 0，判据会绿着什么都不检查。
    """
    types = {}
    blocks = 0
    for plugin_dir in sorted(pathlib.Path(root, "src/plugins").iterdir()):
        if not plugin_dir.is_dir():
            continue
        plugin = plugin_dir.name
        for cpp in sorted(plugin_dir.glob("*.cpp")):
            body = strip_comments(cpp.read_text(encoding="utf-8"))
            for m in re.finditer(r"ParamSpec\s*\{", body):
                depth, i = 1, m.end()
                while i < len(body) and depth:
                    if body[i] == "{":
                        depth += 1
                    elif body[i] == "}":
                        depth -= 1
                    i += 1
                blocks += 1
                entry = body[m.end():i - 1]
                km = re.search(r'"([A-Za-z_0-9]+)"', entry)
                tm = re.search(r"ParamType::(\w+)", entry)
                if not km or not tm:
                    raise SystemExit(
                        "2 ParamSpec 条目缺 key 或 ParamType（解析器不能吞掉条目）：%s 第 %d 条" % (
                            cpp.name, blocks))
                types.setdefault(plugin, {})[km.group(1)] = tm.group(1)
    return types, blocks


def load_param_bounds(root):
    """从插件源码取 (plugin, key) -> 数值界声明（min/max 原文 + 开闭位 + 奇偶位）。

    与 load_param_types 分开的两个理由：
      ①本函数额外读 *.inc（vector 的 spec 表整个活在 .inc 里，类型判据当年没扩这一维，
        这里若跟着改会把"类型列"的论域一起改动，那是另一把刀的射程）；
      ②区间判据要的是**声明的括号形状**，与类型无关，混在一个解析器里会让两边的坑互相污染。
    三个 int{}/double{} 记号依序当 默认/min/max（与 ParamSpec 的位置初始化同序），
    尾随的 .setExclusive(a,b) / .setRequireOdd() 当开闭位与奇偶位。
    """
    bounds = {}
    for plugin_dir in sorted(pathlib.Path(root, "src/plugins").iterdir()):
        if not plugin_dir.is_dir():
            continue
        plugin = plugin_dir.name
        for src in sorted(list(plugin_dir.glob("*.cpp")) + list(plugin_dir.glob("*.inc"))):
            body = strip_comments(src.read_text(encoding="utf-8"))
            for m in re.finditer(r"ParamSpec\s*\{", body):
                depth, i = 1, m.end()
                while i < len(body) and depth:
                    if body[i] == "{":
                        depth += 1
                    elif body[i] == "}":
                        depth -= 1
                    i += 1
                entry = body[m.end():i - 1]
                tail = body[i:i + 60]
                km = re.search(r'"([A-Za-z_0-9]+)"', entry)
                # 字符类含 Ee 与正负号：源码里确有 double{1e-6} / double{1e9} 这类形态（matching.ecc_epsilon、
                # terrain.accum_threshold），只写 [0-9.e+] 会把带负指数的**上界**漏掉 ⇒ len(lits)<3 ⇒ 整个键不进界表。
                lits = re.findall(r"(?:int|double)\{(-?[0-9.eE+-]+)\}", entry)
                if not km or len(lits) < 3:
                    continue
                ex = re.search(r"setExclusive\(\s*(true|false)\s*,\s*(true|false)\s*\)", tail)
                bounds.setdefault(plugin, {})[km.group(1)] = {
                    "min": lits[1], "max": lits[2],
                    "exMin": bool(ex and ex.group(1) == "true"),
                    "exMax": bool(ex and ex.group(2) == "true"),
                    "odd": "setRequireOdd" in tail,
                }
    return bounds


# 文档说明列里的区间写法：[a, b] / (a, b] / [a, b) / (a, b)，两端允许小数与科学计数。
DOC_RANGE_RE = re.compile(r"([\[\(])\s*(-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)\s*,\s*"
                          r"(-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)\s*([\]\)])")


def doc_range_claims(cell):
    """取出说明列里所有区间形状。

    按**括号对**定位，与外面有没有反引号无关：现读 11 处里 9 处写在代码段内、2 处写在正文里
    ⇒ 沿用"枚举候选值"那条只取反引号内 token 的取法会漏掉 2 处，反向只取体外会漏掉 9 处，
    两种收窄都不做，整格扫括号对。论域（9 行 11 处）逐处核过与参数表一致 ⇒ 误伤面量过。
    """
    return list(DOC_RANGE_RE.finditer(cell))


def spec_interval_text(b):
    """把一条界声明还原成文档该写的区间串：括号形状取开闭位，两端取源码字面量原文。"""
    return ("%s%s, %s%s" % ("(" if b["exMin"] else "[", b["min"], b["max"],
                            ")" if b["exMax"] else "]"))


def canon(s):
    """去空白 + 小写：文档排版会在中英文间加空格，二进制标签有 Sigma/NoData 这类大小写形态。"""
    return "".join(ch for ch in s if not ch.isspace()).lower()


def has_cjk(s):
    return any("一" <= ch <= "鿿" for ch in s)


def cell_matches(label, key, cell):
    """D6 口径：参数列 = 二进制 displayName；标签是纯 ASCII 时允许直接写 CLI 键名。"""
    c, l, k = canon(cell), canon(label), canon(key)
    if c == l:
        return True
    if has_cjk(label):
        return False
    return c == k or c.replace("_", " ") == l


def parse_param_table(path):
    rows, lines = [], path.read_text(encoding="utf-8").splitlines()
    i = 0
    while i < len(lines):
        if lines[i].strip().startswith("## 参数"):
            j = i + 1
            while j < len(lines) and not lines[j].strip().startswith("|"):
                if lines[j].startswith("## "):
                    return rows, False
                j += 1
            if j >= len(lines):
                return rows, False
            j += 2
            while j < len(lines) and lines[j].strip().startswith("|"):
                cells = [c.strip() for c in lines[j].strip().strip("|").split("|")]
                if len(cells) != 5:
                    return rows, False
                if cells[0] != "参数":
                    rows.append({"参数": cells[0], "类型": cells[1], "必填": cells[2],
                                 "默认值": cells[3], "说明": cells[4]})
                j += 1
            return rows, True
        i += 1
    return rows, False


def norm(value):
    return "".join(ch for ch in value if not ch.isspace())


def find_doc(docs_root, plugin, action, display_name, overrides):
    """动作 → 文档：人工映射优先；否则按"文件名 == 动作名 > 文件名是动作名前缀 >
    动作名是文件名前缀 > 互为子串"取最长候选（与阶段三建映射时同一套规则）。"""
    fid = plugin + "." + action
    if fid in overrides:
        return docs_root / (overrides[fid][0] + ".md"), True
    dn = norm(display_name or "")
    pools = []
    for d in DOC_DIR.get(plugin, []):
        dd = docs_root / d
        if dd.is_dir():
            pools.extend([(d, f.stem) for f in sorted(dd.glob("*.md")) if f.stem != "总览"])
    exact = [d + "/" + s for d, s in pools if norm(s) == dn]
    suf = [d + "/" + s for d, s in pools if not exact and dn.startswith(norm(s))]
    pre = [d + "/" + s for d, s in pools if not exact and not suf and norm(s).startswith(dn)]
    sub = [d + "/" + s for d, s in pools
           if not any([exact, suf, pre]) and (norm(s) in dn or dn in norm(s))]
    for cands in (exact, suf, pre, sub):
        if cands:
            return docs_root / (max(cands, key=len) + ".md"), False
    return None, False


def same_number(a, b):
    if a == b:
        return True
    try:
        return abs(float(a) - float(b)) < 1e-12
    except ValueError:
        return False


def compare(ui, common, spec, help_spec, docs_root, overrides, verified, types=None, bounds=None):
    types = types or {}
    findings, hits, used_ov = [], set(), set()
    stats = {"docs": 0, "rows": 0, "matched": 0, "missing_doc": [],
             "type_rows": 0, "desc_rows": 0, "option_rows": 0, "range_rows": 0, "range_claims": 0}
    resolved = {}
    for plugin in sorted(ui):
        for action in sorted(ui[plugin]):
            fid = plugin + "." + action
            cfg = ui[plugin][action]
            doc, via_override = find_doc(docs_root, plugin, action, cfg["displayName"], overrides)
            if via_override:
                used_ov.add(fid)
            if doc is None or not doc.exists():
                stats["missing_doc"].append(fid)
                continue
            resolved[str(doc.relative_to(docs_root)).replace("\\", "/")] = fid
            rows, ok = parse_param_table(doc)
            if not ok:
                findings.append((fid, "table", "badtable",
                                 "%s [%s] 没有 ## 参数 表，或有行不是 5 列" % (fid, doc.name)))
                continue
            stats["docs"] += 1
            stats["rows"] += len(rows)
            exp = {}
            for key in cfg["visible"]:
                lab = spec.get(plugin, {}).get(action, {}).get(key) or common.get(key)
                if not lab:
                    lab = (help_spec.get(plugin, {}).get(key) or {}).get("label")
                exp[key] = lab
            matched, used = {}, set()
            for i, r in enumerate(rows):
                for key, lab in exp.items():
                    if lab and key not in used and cell_matches(lab, key, r["参数"]):
                        matched[i] = key
                        used.add(key)
                        break
            stats["matched"] += len(matched)
            if len(rows) != len(cfg["visible"]):
                findings.append((fid, "table", "count",
                                 "%s [%s] 行数：文档 %d vs visibleKeys %d；文档列=%s 键=%s" % (
                                     fid, doc.name, len(rows), len(cfg["visible"]),
                                     [r["参数"] for r in rows], cfg["visible"])))
            miss = sorted({lab for k, lab in exp.items() if lab and k not in used})
            extra = sorted({rows[i]["参数"] for i in range(len(rows)) if i not in matched})
            if miss or extra:
                findings.append((fid, "table", "label",
                                 "%s [%s] 参数名：文档缺=%s 文档多=%s" % (fid, doc.name, miss, extra)))
            for i, r in enumerate(rows):
                key = matched.get(i)
                if key is None:
                    continue
                want = "是" if (key in cfg["required"]
                                or (help_spec.get(plugin, {}).get(key) or {}).get("required")) else "否"
                if r["必填"] != want:
                    findings.append((fid, key, "required", "%s 键 %s 必填：文档=%s 二进制=%s" % (
                        fid, key, r["必填"], want)))
                bdef = (help_spec.get(plugin, {}).get(key) or {}).get("default")
                dm = re.match(r"^`?([^`]+)`?\s*(?:[（(].*$)?", r["默认值"].strip())
                nd = (dm.group(1) if dm else r["默认值"].strip()).strip().strip('"')
                if bdef is not None:
                    if not same_number(bdef.strip().strip('"'), nd) and nd not in NO_DEF:
                        findings.append((fid, key, "default", "%s 键 %s 默认值：文档=%s 二进制=%s" % (
                            fid, key, nd, bdef.strip())))
                elif nd not in NO_DEF:
                    findings.append((fid, key, "nodefaultspec",
                                     "%s 键 %s 文档写了默认值 %s，二进制 spec 不打 Default" % (fid, key, nd)))
                # 类型列：文档第 2 列必须等于该键 ParamSpec 的 ParamType（词表双射见 DOC_TYPE_OF_PARAM_TYPE）。
                want_type = DOC_TYPE_OF_PARAM_TYPE.get(types.get(plugin, {}).get(key))
                if want_type is not None:
                    stats["type_rows"] += 1
                    if r["类型"] != want_type:
                        findings.append((fid, key, "type", "%s 键 %s 类型：文档=%s 二进制=%s(%s)" % (
                            fid, key, r["类型"] or "空", types[plugin][key], want_type)))
                # 说明列：配上键的行必须有内容（空说明=文档把这行当占位，读者拿不到任何信息）。
                stats["desc_rows"] += 1
                if not r["说明"].strip():
                    findings.append((fid, key, "desc",
                                     "%s 键 %s 的说明列是空的" % (fid, key)))
                # 枚举候选值：CLI 打了 Options 且文档说明里用反引号列了候选值 ⇒ 两个集合必须相等。
                # 只取纯 ASCII 的反引号串：中文文档里反引号也被用来引用**别的参数名**
                # （现读唯一一处：raster_math.reclassify 的 `interval` 按 `间隔步长` 等距切分），
                # 而 Options 恒为 ASCII ⇒ 非 ASCII 的引用不参与对撞。
                # 本轮刻意**不要求**文档必须列全（现读 34 行有 Options、其中 7 行一个 token 都没列），
                # 那是另一条判据的射程，需要逐条补文档，不能靠收窄集合比较来冒充。
                bopts = (help_spec.get(plugin, {}).get(key) or {}).get("options")
                if bopts:
                    stats["option_rows"] += 1
                    doc_set = {t.strip().lower() for t in re.findall(r"`([^`]+)`", r["说明"])
                               if t.strip().isascii()}
                    if doc_set:
                        bin_set = {t.strip().lower() for t in bopts.split(",") if t.strip()}
                        if doc_set != bin_set:
                            findings.append((fid, key, "options", "%s 键 %s 枚举候选值：文档=%s 二进制=%s（文档缺=%s 文档多=%s）" % (
                                fid, key, sorted(doc_set), sorted(bin_set),
                                sorted(bin_set - doc_set), sorted(doc_set - bin_set))))
                # 区间描述串：说明列里每一处 `[a, b]` 形状都必须等于该键 ParamSpec 声明的数值界
                # （含开闭位）。这条判据在 C1 之前**不可能绿**——同一个界当时有四个写者（spec / 派发层手写闸 /
                # GUI 魔数 / 插件内静默改写），文档只能对其中一个；C2~C4 把四处收成 ParamSpec 一处之后，
                # "文档 == spec" 才有单一比较对象，也才有资格进机器判据。
                # 逐处都查（不是只查第一处）：现读同一格多处的是"核大小"那格，三处全是同一个 `[3, 101]`
                # ⇒ 重复声明不误伤；反之若某格第二处讲的是**产物值域**且数值不同，它会报红，
                # 那时该改的是文档措辞（把值域写成区间以外的形态），而不是把比较集合收窄成"只看第一处"。
                # 奇偶位（requireOdd）**不在这条判据的射程**：文档用"必须为奇数"这类中文措辞，形状对不上
                # 区间正则；它由 ParamSpec 自己 + roster 的奇偶行承重，别在这里假称判过。
                claims = doc_range_claims(r["说明"])
                if claims:
                    stats["range_rows"] += 1
                    bnd = (bounds or {}).get(plugin, {}).get(key)
                    for m in claims:
                        stats["range_claims"] += 1
                        got = m.group(0)
                        if bnd is None:
                            findings.append((fid, key, "range", "%s 键 %s 文档写了区间 %s，"
                                             "但该键的 ParamSpec 没有声明数值界（三个 int{}/double{} 不齐全）" % (
                                                 fid, key, got)))
                            continue
                        want = spec_interval_text(bnd)
                        # 数字按 same_number 比、括号按形状比，不比整串字面量：
                        # 源码界写成 double{0.0}/double{360.0}，文档按中文排版习惯写 [0, 360] ——
                        # 那是同一个区间的两种拼法，不是漂移（现读此类 6 处，逐处两边都是同值）。
                        if (got[0] != want[0] or got[-1] != want[-1]
                                or not same_number(m.group(2), bnd["min"])
                                or not same_number(m.group(3), bnd["max"])):
                            findings.append((fid, key, "range", "%s 键 %s 区间：文档=%s 二进制=%s" % (
                                fid, key, got, want)))
    # 总览页与文档集合的双向一致：新增一篇却忘了挂链接、或链接指向已改名的文件，
    # 都是"文档树自己内部对不上"，与二进制无关但同样会误导读者。
    overview = docs_root / "总览.md"
    if overview.exists():
        linked = set(OVERVIEW_LINK_RE.findall(overview.read_text(encoding="utf-8")))
        skip = {"总览.md", "文档漂移清单.md"}
        on_disk = {str(p.relative_to(docs_root)).replace("\\", "/")
                   for p in docs_root.rglob("*.md")} - skip
        unlinked = sorted(on_disk - linked)
        dangling = sorted(linked - on_disk)
        for rel in unlinked:
            findings.append(("overview", "table", "unlinked",
                             "总览.md 没挂链接的文档：%s" % rel))
        for rel in dangling:
            findings.append(("overview", "table", "dangling",
                             "总览.md 链接指向不存在的文档：%s" % rel))
    allowed, hits = [], set()
    for f in findings:
        ent = verified.get((f[0], f[1])) or verified.get((f[0], "table"))
        if ent and f[2] in ent.get("kinds", [f[2]]):
            allowed.append(f)
            hits.add((f[0], f[1]))
        else:
            say("DRIFT " + f[3])
    dead = [k for k in verified if k not in hits]
    for k in dead:
        say("DEAD-ALLOWLIST %s 键=%s（本轮没有对应漂移 ⇒ 必须删掉这条豁免）" % (k[0], k[1]))
    unused_ov = sorted(set(overrides) - used_ov)
    for a in unused_ov:
        say("DEAD-OVERRIDE %s（文档标题已能被机器直接匹配上 ⇒ 这条人工映射可以删）" % a)
    return findings, allowed, dead, stats, unused_ov


def run(root, cli, allowlist_path):
    data = json.loads(pathlib.Path(allowlist_path).read_text(encoding="utf-8"))
    overrides = {o["action"]: (o["doc"], True) for o in data["doc_overrides"]}
    verified = {(v["action"], v["key"]): v for v in data["verified"]}
    for v in data["verified"]:
        if not v.get("evidence"):
            raise SystemExit("2 白名单条目缺证据（evidence 必须给 文件:行 或 文档说明）：%s" % v)
    ui, common, spec, labels, shape_bad = load_gui_tables(root)
    if shape_bad:
        raise SystemExit("2 ActionUiConfig 表形态异常（解析器右移的老坑）：%s" % shape_bad)
    help_spec = load_cli_help(cli, sorted(ui))
    types, blocks = load_param_types(root)
    if not types:
        raise SystemExit("2 一个 ParamSpec 类型都没解析到 ⇒ 类型判据等于没跑（插件目录或声明形态变了）")
    bounds = load_param_bounds(root)
    n_bounds = sum(len(v) for v in bounds.values())
    # 现读 104 个带界键 / 13 个插件。地板取 90：这条只拦"解析器整体失效"（声明形态或插件目录变了 ⇒
    # 界表空/半空，区间判据静默变成永不命中），不给单键的合法改写设卡。
    if n_bounds < 90:
        raise SystemExit("2 只解析到 %d 个带数值界的 ParamSpec（现读量级 104）⇒ 区间判据等于没跑"
                         "（声明形态或插件目录变了）" % n_bounds)
    findings, allowed, dead, stats, unused = compare(
        ui, common, spec, help_spec, pathlib.Path(root, "docs/算法说明"), overrides, verified,
        types, bounds)
    total_actions = sum(len(v) for v in ui.values())
    say("actions=%d docs_checked=%d missing_doc=%d rows=%d matched=%d type_rows=%d desc_rows=%d "
        "option_rows=%d range_rows=%d range_claims=%d "
        "param_spec_blocks=%d drift=%d allowed=%d "
        "dead_allowlist=%d dead_override=%d overrides=%d" % (
              total_actions, stats["docs"], len(stats["missing_doc"]), stats["rows"], stats["matched"],
              stats["type_rows"], stats["desc_rows"], stats["option_rows"],
              stats["range_rows"], stats["range_claims"], blocks,
              len(findings) - len(allowed), len(allowed), len(dead), len(unused), len(overrides)))
    if stats["missing_doc"]:
        say("MISSING-DOC " + ", ".join(stats["missing_doc"]))
    if stats["matched"] != stats["rows"]:
        say("UNMATCHED-ROWS %d 行没配上任何二进制键 ⇒ 上面这些行的 必填/默认值/类型/说明 未被检查" % (
            stats["rows"] - stats["matched"]))
    # 区间维度的地板：现读 11 处（本判据接入时逐处核过全对）⇒ 取 10 只拦"整条判据空跑"，
    # 不拦单处文档改写。没有这条地板，说明列被改写成"约 0 到 1 之间"这类非区间拼法就会静默失去承重。
    if stats["range_claims"] < 10:
        say("RANGE-COVERAGE 说明列里只有 %d 处区间描述串（本轮接入时现读 11）⇒ 区间判据的承重面塌了" % (
            stats["range_claims"]))
        return 1
    return (len(findings) - len(allowed)) + len(dead) + len(unused) + len(stats["missing_doc"])


def self_test():
    """合成一份"3 键 4 行、八类缺陷齐全"的假仓库，判据必须八类全点名，死豁免必须报错。"""
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="docs_selftest_"))
    (tmp / "docs/算法说明/假类").mkdir(parents=True)
    good = tmp / "docs/算法说明/假类/假动作.md"
    bad = tmp / "docs/算法说明/假类/假动作坏.md"
    # 坏文档四行各埋一类以上，且第四行是"配不上键的多余行"——
    # required/default/type/desc/options/range 只在"行配上键"之后才检查，
    # 只埋一行全错的表只能报出 count+label（第一版夹具就是这么漏的 ⇒ 判据看着能红，实际只红两类）。
    # 区间类埋**三处、三条分支各自承重**（同一 kind 只报一次不等于三条臂都证到）：
    #   Foo 行 "[0, 10]"  = 数字全对、右括号 "]" 对上声明的 ")" ⇒ 只有开闭位比较能抓到；
    #   Foo 行 "[0, 20)"  = 括号全对、上界 20≠10 ⇒ 只有数值比较能抓到；
    #   模式 行 "[0, 1]"   = 该键（Enum）根本没有数值界声明 ⇒ 另一条"文档凭空造界"分支。
    # 输入行留空说明，专供 desc ⇒ desc 与 range 的载体互不遮挡。
    bad.write_text("## 参数\n\n| 参数 | 类型 | 必填 | 默认值 | 说明 |\n| --- | --- | --- | --- | --- |\n"
                   "| 输入文件 | 字符串 | 否 | 无 | |\n"
                   "| Foo | 整数 | 否 | `3` | 取值 [0, 10] 与 [0, 20) |\n"
                   "| 模式 | 枚举 | 否 | `alpha` | 可选 `alpha` 或 `gamma`，另有 [0, 1] |\n"
                   "| 名字不对 | 字符串 | 否 | 无 | 多余行 |\n", encoding="utf-8")
    # 好文档的"模式"行是 options 判据的**在体负控**：`阈值` 是引用别的参数名（非 ASCII），
    # 必须被剔除后仍 0 漂移——否则这条判据会把中文引用当成候选值误伤。
    # "Foo"行的区间放在反引号里，是 range 判据的**绿臂载体**：现读文档最常见的写法就是代码段，
    # 本判据刻意不剥反引号 ⇒ 这里必须仍然配上，否则反引号形态会被整条判据漏掉。
    good.write_text("## 参数\n\n| 参数 | 类型 | 必填 | 默认值 | 说明 |\n | --- | --- | --- | --- | --- |\n"
                    "| 输入文件 | 文件路径 | 是 | 无 | x |\n| Foo | 整数 | 否 | `7` | 合法取值 `[0, 10)` |\n"
                    "| 模式 | 枚举 | 否 | `alpha` | 可选 `alpha` 或 `beta`，步长按 `阈值` 计 |\n", encoding="utf-8")
    # 合成二进制的 ParamSpec 类型：input=FilePath（该写 文件路径）、foo=Int（该写 整数）、mode=Enum。
    fake_types = {"fake": {"input": "FilePath", "foo": "Int", "mode": "Enum"}}
    # 合成二进制的数值界：只有 foo 声明了 [0, 10)（开闭位 exMax=True）；input/mode 声明里没凑齐三个
    # int{}/double{} ⇒ 不进界表，正是"文档写了区间但 spec 没声明界"那条分支的载体。
    fake_bounds = {"fake": {"foo": {"min": "0", "max": "10", "exMin": False, "exMax": True, "odd": False}}}
    ui = {"fake": {"act_bad": {"displayName": "假动作", "visible": ["input", "foo", "mode"],
                               "required": ["input"], "nameSource": "ActionUiConfig"},
                   "act_good": {"displayName": "假动作", "visible": ["input", "foo", "mode"],
                                "required": ["input"], "nameSource": "ActionUiConfig"}}}
    hs = {"fake": {"input": {"required": True, "default": None, "label": "输入文件"},
                   "foo": {"required": False, "default": "7", "label": "Foo"},
                   "mode": {"required": False, "default": "alpha", "label": "模式",
                            "options": "alpha, beta"}}}
    outs = []
    # 臂 1：好文档必须 0 漂移（否则判据根本不可信）；option_rows 必须 ==1，
    # 这条同时证明"中文参数名引用被剔除后不误伤"确实跑到了（好文档那行的 `阈值` 就是诱饵）；
    # range_claims 必须 ==1，同时证明"区间写在反引号里"这一形态真的被吃到而不是静默跳过。
    findings, allowed, dead, stats, _ = compare(
        {"fake": {"act_good": ui["fake"]["act_good"]}}, {}, {}, hs, tmp / "docs/算法说明",
        {"fake.act_good": ("假类/假动作", True)}, {}, fake_types, fake_bounds)
    outs.append(("绿臂：好文档 0 漂移（option_rows=%d type_rows=%d range_claims=%d）" % (
        stats["option_rows"], stats["type_rows"], stats["range_claims"]),
        len(findings) == 0 and stats["docs"] == 1 and stats["option_rows"] == 1
        and stats["range_claims"] == 1))
    # 臂 2：坏文档必须同时点名 count / label / required / default / type / desc / options / range 八类。
    # 埋 4 行而不是 1 行是这里的关键：required/default/type/desc/options/range 只在"行配上键"之后才检查，
    # 一行全错的表只能报出 count+label（第一版夹具就是这么漏的 ⇒ 判据看着能红，实际只红两类）。
    findings, allowed, dead, stats, _ = compare(
        {"fake": {"act_bad": ui["fake"]["act_bad"]}}, {}, {}, hs, tmp / "docs/算法说明",
        {"fake.act_bad": ("假类/假动作坏", True)}, {}, fake_types, fake_bounds)
    kinds = {f[2] for f in findings}
    ranges = [f for f in findings if f[2] == "range"]
    outs.append(("红臂：八类缺陷全点名（本轮 kinds=%s，配上 %d/%d 行，type_rows=%d desc_rows=%d "
                 "option_rows=%d range=%d）" % (
                     sorted(kinds), stats["matched"], stats["rows"], stats["type_rows"],
                     stats["desc_rows"], stats["option_rows"], len(ranges)),
                     {"count", "label", "required", "default", "type", "desc", "options", "range"} <= kinds
                     and stats["matched"] == 3))
    # 臂 2b：range 的三条分支各自单独承重（同一 kind 只报一次不算证到三条路）——
    # 开闭位单点错、数字单点错、该键根本没声明界，三条必须各出一条。
    outs.append(("红臂：range 三条分支各报一次（本轮 %d 条：无声明界 %d / 只括号错 %d / 只数字错 %d）" % (
        len(ranges), sum(1 for f in ranges if "声明" in f[3]),
        sum(1 for f in ranges if "文档=[0, 10] " in f[3]),
        sum(1 for f in ranges if "文档=[0, 20) " in f[3])),
        len(ranges) == 3
        and sum(1 for f in ranges if "声明" in f[3]) == 1
        and sum(1 for f in ranges if "文档=[0, 10] " in f[3]) == 1
        and sum(1 for f in ranges if "文档=[0, 20) " in f[3]) == 1))
    # 臂 3：只豁免其中一类（count）⇒ 其余各类仍算漂移，且这条豁免因为用上了，不许报 DEAD。
    # 与臂 2 唯一的差别就是那份 verified，所以这里必须把 fake_types / fake_bounds 也传进去——
    # 少传一个参数会让 type（或 range）那类根本不存在，臂 2 的"八类齐全"就白测了（本轮真撞到过一次）。
    verified = {("fake.act_bad", "table"): {"kinds": ["count"], "evidence": "x"}}
    findings, allowed, dead, stats, _ = compare(
        {"fake": {"act_bad": ui["fake"]["act_bad"]}}, {}, {}, hs, tmp / "docs/算法说明",
        {"fake.act_bad": ("假类/假动作坏", True)}, verified, fake_types, fake_bounds)
    outs.append(("红臂：只豁免 1 类时其余 %d 类仍算漂移、豁免不算死（allowed=%d 总数=%d）" % (
        len(findings) - len(allowed), len(allowed), len(findings)),
        len(allowed) == 1 and len(findings) - len(allowed) == 9 and not dead))
    # 臂 4：与本轮漂移无关的豁免必须报 DEAD
    verified2 = {("fake.other", "table"): {"kinds": ["count"], "evidence": "x"}}
    findings, allowed, dead, stats, _ = compare(
        {"fake": {"act_good": ui["fake"]["act_good"]}}, {}, {}, hs, tmp / "docs/算法说明",
        {"fake.act_good": ("假类/假动作", True)}, verified2, fake_types, fake_bounds)
    outs.append(("红臂：与本轮漂移无关的豁免必须报 DEAD", ("fake.other", "table") in dead))
    # 臂 5：总览页双向对撞 —— 挂了的文件不存在 ⇒ dangling；盘上有、链接没有 ⇒ unlinked。
    # （前面几条不建 总览.md，判据那一步是 skip，不会污染"好文档 0 漂移"这条绿臂。）
    (tmp / "docs/算法说明/总览.md").write_text(
        "- [假动作](./假类/假动作.md)\n- [不存在](./假类/根本没有.md)\n", encoding="utf-8")
    findings, allowed, dead, stats, _ = compare(
        {"fake": {"act_good": ui["fake"]["act_good"]}}, {}, {}, hs, tmp / "docs/算法说明",
        {"fake.act_good": ("假类/假动作", True)}, {}, fake_types, fake_bounds)
    kinds5 = {f[2] for f in findings}
    outs.append(("红臂：总览缺挂与死链都点名（本轮 kinds=%s）" % sorted(kinds5),
                 {"unlinked", "dangling"} <= kinds5))
    # 臂 6：链接提取式本身 —— 带空格的文件名必须整段取到（本轮真因 "[^)]+ vs [^)\s]+" 报过 12 条假红）
    got = OVERVIEW_LINK_RE.findall("- [C 校正](./几何校正与辐射处理/C 校正.md)" + chr(10))
    outs.append(("绿臂：带空格的文件名不被截断（取到 %s）" % got,
                 got == ["几何校正与辐射处理/C 校正.md"]))
    for name, passed in outs:
        say("%s  %s" % ("OK  " if passed else "FAIL", name))
    return 0 if all(p for _, p in outs) else 1


def main():
    # stdout 一律 ASCII：ctest 的日志链路（cmake -P 的子进程输出会被按本地码页转一次）会把中文
    # 变成"UTF-8→GBK→UTF-8"的双编码乱码（实测 `1 行没配上` 落地成 `1 琛屾病閰嶄笂`，
    # 与 B21 给 source_mojibake.ps1 定"stdout 只用 ASCII"是同一条本机限制）。
    # ⇒ stdout 用 ASCII + backslashreplace（永远可读、不会假乱码），原文写进 --report 指向的 UTF-8 文件。
    try:
        sys.stdout.reconfigure(encoding="ascii", errors="backslashreplace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="docs/算法说明 参数表 <-> 二进制参数契约 的守卫")
    ap.add_argument("--cli", help="gis-cli 可执行文件路径（构建产物或安装包里都行）")
    ap.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parents[1]))
    ap.add_argument("--allowlist", default=str(pathlib.Path(__file__).with_name("docs_sync_allowlist.json")))
    ap.add_argument("--report", default="", help="把逐行原文（UTF-8）写进这个文件；stdout 只留 ASCII 形态")
    ap.add_argument("--self-test", action="store_true", help="只跑合成输入的自证，不读仓库")
    a = ap.parse_args()
    try:
        if a.self_test:
            return self_test()
        if not a.cli:
            say("用法错误：真跑必须给 --cli <gis-cli 路径>")
            return 2
        cli = pathlib.Path(a.cli)
        if not cli.exists():
            say("用法错误：--cli 指向的文件不存在：%s" % cli)
            return 2
        return run(a.root, cli, a.allowlist)
    finally:
        if a.report:
            pathlib.Path(a.report).write_text("\n".join(REPORT) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
