#!/usr/bin/env python
"""文档参数表 ↔ 二进制参数契约 的守卫（批次 C 计划里的 Task C21/C24）。

期望侧（二进制，三个来源都是编译进产物的表）：
  1. `src/gui/gui_data_support.cpp` 的 `actionUiConfigStorage()`
     —— 每个动作在 GUI 面板里真正渲染的键（visibleKeys）与真正标必填的键（requiredKeys）；
  2. 同文件的 `commonParamTextStorage()` / `actionSpecificParamTextStorage()` / `genericActionDisplayName()`
     —— 面板显示的动作名与键标签；
  3. `<cli> <plugin> --help` —— CLI 实际契约：`[required|optional]`、`Default:`、`Options:`。

文档侧：`docs/算法说明/<主功能>/<动作名>.md` 的 `## 参数` 表（列：参数/类型/必填/默认值/说明）。
比对维度：**行数、参数名、必填、默认值**；`类型` 与 `说明` 两列不在本守卫射程内（见白名单旁的说明）。

不静音：任何"这条其实不是漂移"的判定都必须写进 `tests/docs_sync_allowlist.json` 的 `verified`
里并带证据（源码 文件:行）；反过来，**白名单里的死条目也算失败** —— 漂移被修好后忘了删条目，
下一次同处真漂移就会被这条陈旧豁免吞掉。

自证：`--self-test` 用一份合成输入（2 键 1 行的假文档，故意埋 4 类缺陷）跑同一套判据，
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
                    rows.append({"参数": cells[0], "必填": cells[2], "默认值": cells[3]})
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


def compare(ui, common, spec, help_spec, docs_root, overrides, verified):
    findings, hits, used_ov = [], set(), set()
    stats = {"docs": 0, "rows": 0, "matched": 0, "missing_doc": []}
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
    findings, allowed, dead, stats, unused = compare(
        ui, common, spec, help_spec, pathlib.Path(root, "docs/算法说明"), overrides, verified)
    total_actions = sum(len(v) for v in ui.values())
    say("actions=%d docs_checked=%d missing_doc=%d rows=%d matched=%d drift=%d allowed=%d "
          "dead_allowlist=%d dead_override=%d overrides=%d" % (
              total_actions, stats["docs"], len(stats["missing_doc"]), stats["rows"], stats["matched"],
              len(findings) - len(allowed), len(allowed), len(dead), len(unused), len(overrides)))
    if stats["missing_doc"]:
        say("MISSING-DOC " + ", ".join(stats["missing_doc"]))
    if stats["matched"] != stats["rows"]:
        say("UNMATCHED-ROWS %d 行没配上任何二进制键 ⇒ 上面这些行的 必填/默认值 未被检查" % (
            stats["rows"] - stats["matched"]))
    return (len(findings) - len(allowed)) + len(dead) + len(unused) + len(stats["missing_doc"])


def self_test():
    """合成一份"2 键 1 行、四类缺陷齐全"的假仓库，判据必须四类全点名，死豁免必须报错。"""
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="docs_selftest_"))
    (tmp / "docs/算法说明/假类").mkdir(parents=True)
    good = tmp / "docs/算法说明/假类/假动作.md"
    bad = tmp / "docs/算法说明/假类/假动作坏.md"
    bad.write_text("## 参数\n\n| 参数 | 类型 | 必填 | 默认值 | 说明 |\n| --- | --- | --- | --- | --- |\n"
                   "| 输入文件 | 文件路径 | 否 | 无 | 必填标错 |\n"
                   "| Foo | 整数 | 否 | `3` | 默认值标错 |\n"
                   "| 名字不对 | 字符串 | 否 | 无 | 多余行 |\n", encoding="utf-8")
    good.write_text("## 参数\n\n| 参数 | 类型 | 必填 | 默认值 | 说明 |\n | --- | --- | --- | --- | --- |\n"
                    "| 输入文件 | 文件路径 | 是 | 无 | x |\n| Foo | 整数 | 否 | `7` | x |\n", encoding="utf-8")
    ui = {"fake": {"act_bad": {"displayName": "假动作", "visible": ["input", "foo"],
                               "required": ["input"], "nameSource": "ActionUiConfig"},
                   "act_good": {"displayName": "假动作", "visible": ["input", "foo"],
                                "required": ["input"], "nameSource": "ActionUiConfig"}}}
    hs = {"fake": {"input": {"required": True, "default": None, "label": "输入文件"},
                   "foo": {"required": False, "default": "7", "label": "Foo"}}}
    outs = []
    # 臂 1：好文档必须 0 漂移（否则判据根本不可信）
    findings, allowed, dead, stats, _ = compare(
        {"fake": {"act_good": ui["fake"]["act_good"]}}, {}, {}, hs, tmp / "docs/算法说明",
        {"fake.act_good": ("假类/假动作", True)}, {})
    outs.append(("绿臂：好文档 0 漂移", len(findings) == 0 and stats["docs"] == 1))
    # 臂 2：坏文档必须同时点名 count / label / required / default 四类。
    # 埋 3 行而不是 1 行是这里的关键：required/default 只在"行配上键"之后才检查，
    # 一行全错的表只能报出 count+label（第一版夹具就是这么漏的 ⇒ 判据看着能红，实际只红两类）。
    findings, allowed, dead, stats, _ = compare(
        {"fake": {"act_bad": ui["fake"]["act_bad"]}}, {}, {}, hs, tmp / "docs/算法说明",
        {"fake.act_bad": ("假类/假动作坏", True)}, {})
    kinds = {f[2] for f in findings}
    outs.append(("红臂：四类缺陷全点名（本轮 kinds=%s，配上 %d/%d 行）" % (
        sorted(kinds), stats["matched"], stats["rows"]),
        {"count", "label", "required", "default"} <= kinds and stats["matched"] == 2))
    # 臂 3：豁免用不上 ⇒ 必须报死条目
    verified = {("fake.act_bad", "table"): {"kinds": ["count"], "evidence": "x"}}
    findings, allowed, dead, stats, _ = compare(
        {"fake": {"act_bad": ui["fake"]["act_bad"]}}, {}, {}, hs, tmp / "docs/算法说明",
        {"fake.act_bad": ("假类/假动作坏", True)}, verified)
    outs.append(("红臂：只豁免 1 类时其余 3 类仍算漂移、豁免不算死", len(allowed) >= 1 and not dead))
    # 臂 4：与本轮漂移无关的豁免必须报 DEAD
    verified2 = {("fake.other", "table"): {"kinds": ["count"], "evidence": "x"}}
    findings, allowed, dead, stats, _ = compare(
        {"fake": {"act_good": ui["fake"]["act_good"]}}, {}, {}, hs, tmp / "docs/算法说明",
        {"fake.act_good": ("假类/假动作", True)}, verified2)
    outs.append(("红臂：与本轮漂移无关的豁免必须报 DEAD", ("fake.other", "table") in dead))
    # 臂 5：总览页双向对撞 —— 挂了的文件不存在 ⇒ dangling；盘上有、链接没有 ⇒ unlinked。
    # （前面几条不建 总览.md，判据那一步是 skip，不会污染"好文档 0 漂移"这条绿臂。）
    (tmp / "docs/算法说明/总览.md").write_text(
        "- [假动作](./假类/假动作.md)\n- [不存在](./假类/根本没有.md)\n", encoding="utf-8")
    findings, allowed, dead, stats, _ = compare(
        {"fake": {"act_good": ui["fake"]["act_good"]}}, {}, {}, hs, tmp / "docs/算法说明",
        {"fake.act_good": ("假类/假动作", True)}, {})
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
