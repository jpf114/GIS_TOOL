#!/usr/bin/env python
# -*- coding: utf-8 -*-
r"""geotransform 防回潮守卫（批次 D / Task 8 的后一半，本刀的机器兜底）。

判据：产品代码里每一个 `GDALDataset::GetGeoTransform()` 调用位点，必须【判了返回值】，
或者【在白名单里被点名并写明兜底理由】。白名单里点名不到活位点的条目（DEAD-ALLOWLIST）
同样判红 —— 否则"缺陷修好了、豁免还留着"会把下一处真回潮吃掉（口径同 docs_sync_allowlist）。

WHY 载体是 .py + cmake 包装（而不是纯 cmake）：本判据要按 C++ 词法把
  ①行注释、②块注释、③字符串字面量 里的同形态文本从 CODE 数里分出去，还要拼跨行语句；
  cmake 的字符串正则既不认 `\(`（本仓已证），也没法做逐字符状态机 —— 硬写就只能得到
  "朴素 grep 被 terrain_plugin.cpp 那行注释污染"的那种错数。
  先例：check_docs_match_cli.py（判据在 py，cmake 只管取解释器与判退出码）。

退出码（消费者只有 tests/check_geotransform_guard.cmake 那一个包装，逐类核见 task-8-report 节 0）：
  0 = 自证臂 + 两个域全部通过
  1 = 域判定红（回潮位点 / DEAD 白名单 / 现读数与期望数不符）
  2 = 自证臂红（解析器自己坏了 ⇒ 对仓库的结论一律不可信，先于域判定）
  3 = 用法/环境错误（缺参数、缺白名单文件、仓库根不存在）

复算（与 ctest 注册的那条完全同形）：
  python tests/check_geotransform_guard.py --repo-root . --allowlist tests/geotransform_guard_allowlist.json
"""

import argparse
import json
import os
import re
import sys

TOKEN = "GetGeoTransform("
CPP_EXTS = (".cpp", ".h", ".hpp", ".cc", ".inc")

EXIT_OK = 0
EXIT_REPO = 1
EXIT_SELFCHECK = 2
EXIT_USAGE = 3

# ───────────────────────────────────────────────────────────────────────────────
# 域定义与期望值
#
# ⚠ 期望值是"位点全集"的**独立锚点**（另一个口径数出来的：对整行文本做 TOKEN 子串计数），
#   解析器分类结果必须满足 any == code + comment_line + block_comment + string_only，
#   并且**钉住的**每个桶都等于下面写死的数（产品域钉全部字面量桶；测试域只钉 CODE 桶那一侧，
#   理由与逐桶分工见下面第二域的注释——两域各自都跑锚点恒等式，一个都没少）。
#   这是本仓"归零类验收必须先证覆盖"的落地：
#   少分类一行（漏数）与多分类一行（把注释当代码）都会在这里红，而不是让"0 个违规"看着像好消息。
#   独立复算（与解析器不同口径；下面两条现读 = 21 与 1，必须逐字等于上面的 any / comment_line）：
#     grep -rn "GetGeoTransform(" src/ --include=*.cpp --include=*.h --include=*.hpp --include=*.cc --include=*.inc | grep -o "GetGeoTransform(" | wc -l
#     grep -rn "GetGeoTransform(" src/ --include=*.cpp --include=*.h --include=*.hpp --include=*.cc --include=*.inc | grep -c "^[^:]*:[0-9]*:[[:space:]]*//"
#   tests/ 用同样两条，但要先 `| grep -v geotransform_guard_fixtures`（夹具归自证臂管）。
#   ⚠ 朴素 grep 只认得出**整行 //** 这一类注释，块注释/字符串里的同形态文本它数不到 ——
#     所以这两条只能对撞 any 与 comment_line；block_comment/string_only 的覆盖由自证臂夹具负责。
#   改产品代码里任何一个位点后必须同步这里，并把新读数的算式写在提交信息里。
# ───────────────────────────────────────────────────────────────────────────────
DOMAINS = [
    {
        "name": "product",
        "root": "src",
        "exclude_parts": (),
        "allowlist_key": "entries",
        "expect": {
            "any": 24, "code": 23, "comment_line": 1, "block_comment": 0, "string_only": 0,
            "checked": 21, "unchecked": 2, "allowlisted": 2, "violations": 0, "dead": 0,
        },
    },
    {
        "name": "test",
        "root": "tests",
        "exclude_parts": ("geotransform_guard_fixtures",),
        "allowlist_key": "test_entries",
        # ⚠ 第二域**刻意不钉** any / comment_line / block_comment / string_only 这四个字面量桶。
        # 原因是实测撞上的：测试侧的解释性注释会原样引用缺陷行 —— 位点 B/C 的两个用例头
        #   （`ProcessingThresholdKeepsProductWithoutGeotransform`、
        #   `RasterMathReclassifyKeepsProductWithoutGeotransform`）与位点 F 的用例头
        #   （`TerrainMultiViewshedFailsWithoutGeotransform`）各引了一次改前原文，
        #   `FillSinks…` 族里那条是既有的（`7241b49` 那一版就已经在引用）⇒ 测试域的注释桶天然随散文增长，钉死它就等于
        #   "以后每写一句注释都要来改守卫"，而那正是本仓记过账的坑：
        #   "为解释某事写的注释会污染数那件事的 grep"。
        # 不钉 ≠ 不判，三样兜底都在：①`any == code + comment_line + block_comment + string_only`
        #   的锚点恒等式照样逐域对撞（漏分类当场红）；②分桶**能力**由自证臂 bad/ok 两个夹具钉死
        #   （bad：any=7 code=3 comment_line=1 block_comment=1 string_only=2；
        #     ok ：any=8 code=6 comment_line=1 block_comment=0 string_only=1 —— 全是字面量期望）；
        #   ③本域真正承重的是 CODE 桶那一侧（**现读 10 位点 / 9 已判 / 1 点名豁免 / 0 违规 / 0 死白名单**），
        #     一个都没放松 ⇒ 注释误并进 CODE 会让 code 从 10 变 11 当场红，
        #     新增未判的代码位点会让 violations 红，两个方向都还咬得住；只有"多写一句注释"不再需要改守卫。
        #     位点从 3 涨到 10 的账（写死在这里省得下把再翻守卫日志）：本守卫落地时是 3 位点
        #     （raster_fixture.h 一处点名豁免 + test_plugins.cpp 两处已判）；批次 D D1-P（Task 16）给
        #     `assign_srs` 加了两处"判了返回值"的 GT 读取却**没回来改这个期望数**⇒ 守卫从那时起就红
        #     （5 ≠ 3，本席现读到的是这条**已存在的红**，不是本席造成的）；批次 D D1-C1（Task 18）给
        #     `cutting.clip` 的三档窗判据又加三处（同样都判返回值）；批次 D R3a 给
        #     `PluginTest.RasterManageCogExecution` 与 `PluginTest.RasterRenderHistogramMatchExecution`
        #     两块各加一处"地理参考必须由产物读回"的已判位点（两处都用 ASSERT_EQ 接 CE_None）。
        #     两处都按 ①的补救口径改成现读，
        #     `violations` 与 `dead` 仍是 0 ⇒ 没有任何一位点是"裸调用混进来的"。
        # 产品域（src/）保留全部字面量桶：那里的散文由本刀统一写成**不含 TOKEN 字面量**的形式
        #   （A/E 两条兜底理由注释同此），所以 comment_line 恒为 1 = terrain_plugin.cpp:1337
        #   （BASE 上是 :1318，节3 在 F 位点上方插了代码才下移）这一条既有的行，独立锚点才守得住。
        # ⚠ 本注释里出现的 any=6 / comment_line=3 一律是**写这句时的现读，不是判据**；
        #   复算命令（逐字照跑即可）：
        #   python tests/check_geotransform_guard.py --repo-root . --allowlist tests/geotransform_guard_allowlist.json
        #   读 `GTGUARD SCAN domain=test(...) any=… code=… comment_line=…`，别拿这里的数去改期望值。
        #   （本文件自己不在扫描范围内：只走 CPP_EXTS，所以这些 TOKEN 字面量不会污染读数。）
        "expect": {
            "code": 9, "checked": 8, "unchecked": 1, "allowlisted": 1, "violations": 0, "dead": 0,
        },
    },
]

# 白名单形状闸：产品域**恰是** A 与 E 两处（批次 D Task 8 用户拍板的射程，多一条少一条都红）。
PRODUCT_ALLOWLIST_IDS = ("A-getRasterInfo", "E-cuttingTileWarped")
ENTRY_REQUIRED_FIELDS = ("id", "file", "function", "signature", "fallback", "reason")

# 产品域里 14 个已判位点的现读全文（改前 14 CHECKED；本刀把 B/C/D/F 四处改成判返回值 ⇒ 18）。
# 这一份**不参与判定**，只在 --explain 模式打印，用于人工对账；判定用的期望数是上面 DOMAIN。


def mask_line(line, in_block):
    """把注释与字符串字面量的**内容**替换成空格，返回值与 line 等长、逐字符对齐。

    返回 (masked, flags, in_block)；flags[i] 取 'n'（代码）/ 'l'（行注释）/ 'b'（块注释）/
    's'（字符串内容）/ 引号字符本身（算代码，好让 `"a" + f(...)` 里的调用仍被数到）。
    """
    n = len(line)
    out = []
    flags = []
    block = in_block
    quote = None
    escaped = False
    i = 0
    while i < n:
        ch = line[i]
        if block:
            if ch == "*" and i + 1 < n and line[i + 1] == "/":
                block = False
                out.extend("  ")
                flags.extend("bb")
                i += 2
                continue
            out.append(" ")
            flags.append("b")
            i += 1
            continue
        if quote is not None:
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == quote:
                quote = None
            out.append(" ")
            flags.append("s")
            i += 1
            continue
        if ch in ('"', "'"):
            quote = ch
            out.append(ch)
            flags.append(ch)
            i += 1
            continue
        if ch == "/" and i + 1 < n:
            if line[i + 1] == "/":
                out.extend(" " * (n - i))
                flags.extend("l" * (n - i))
                i = n
                break
            if line[i + 1] == "*":
                block = True
                out.extend("  ")
                flags.extend("bb")
                i += 2
                continue
        out.append(ch)
        flags.append("n")
        i += 1
    if len(out) != n or len(flags) != n:
        # 分类全靠"flags 与原始行逐字符对齐"，长度一不对，注释/字符串就会漏进 CODE 数。
        raise AssertionError("mask_line lost char alignment: len(out)=%d len(flags)=%d n=%d" % (len(out), len(flags), n))
    return "".join(out), flags, block


def read_masked(text):
    """整个文件 → 逐行 (masked, flags)。"""
    masked_lines = []
    flag_lines = []
    in_block = False
    for raw in text.splitlines():
        masked, flags, in_block = mask_line(raw, in_block)
        masked_lines.append(masked)
        flag_lines.append(flags)
    return masked_lines, flag_lines


BUCKET_BY_FLAG = {"l": "comment_line", "b": "block_comment", "s": "string_only"}


def statement_window(masked_lines, idx, back=4, fwd=8):
    """把 idx 行所在的**语句**拼出来（含跨行续行），返回 (joined_text, [行号])。

    WHY 要拼：`if (ds->GetGeoTransform(gt)\\n    != CE_None)` 这种写法比较式掉在下一行，
    只看命中行会把已经判了返回值的位点误报成回潮（假红会逼后人去加豁免 ⇒ 豁免变垃圾场）。
    """
    def stripped(j):
        return masked_lines[j].strip()

    def ends_statement(txt):
        return txt.endswith((";", "{", "}", ":", ",", ")"))

    def depth_of(txt):
        return txt.count("(") - txt.count(")")

    chosen = [idx]
    acc = stripped(idx)
    k = idx
    while not (ends_statement(acc) and depth_of(acc) <= 0) and len(chosen) <= fwd:
        if k + 1 >= len(masked_lines):
            break
        k += 1
        nxt = stripped(k)
        if not nxt:
            continue
        acc = acc + " " + nxt
        chosen.append(k)
    # 往回吃语句头（`if (a &&` / `return` / 类型声明在上一行）
    head = []
    j = idx
    while j > 0 and len(head) < back:
        prev = stripped(j - 1)
        if not prev or prev.endswith((";", "{", "}", ":")):
            break
        j -= 1
        head.insert(0, j)
    indices = head + chosen
    joined = " ".join(stripped(i) for i in indices)
    return joined, indices


ASSIGN_RE = re.compile(r"(?<![=!<>+\-*/%&|^])([A-Za-z_][A-Za-z0-9_]*)\s*=\s*[^=;]*?" + re.escape(TOKEN))
CE_RE = re.compile(r"CE_None|CE_Failure")


def classify_code_site(masked_lines, idx):
    """CODE 位点 → (verdict, why)。verdict ∈ {'CHECKED','UNCHECKED'}。

    ⚠ why 只写 ASCII：本脚本的 stdout 由 cmake execute_process 捕获后转述进 ctest 日志，
    本机控制台代码页是 cp936 ⇒ 中文判据文案会在失败那一路显示成乱码（本仓 mojibake 守卫
    同一口径：判据 stdout 刻意只用 ASCII，中文解释写在源码注释里）。
    """
    joined, _ = statement_window(masked_lines, idx)
    hit = masked_lines[idx]
    if CE_RE.search(hit):
        return "CHECKED", "E1_CE_COMPARE_ON_HIT_LINE"
    if CE_RE.search(joined) and joined.strip() != hit.strip():
        return "CHECKED", "E1B_CE_COMPARE_IN_JOINED_STATEMENT"
    m = ASSIGN_RE.search(joined)
    if m:
        var = m.group(1)
        # 作用域近似闸：越过本函数右括号（第 0 列的 `}`）就不再认，免得被下一个函数的 CE_* 蒙混。
        for j in range(idx + 1, min(idx + 9, len(masked_lines))):
            s = masked_lines[j].strip()
            if s == "}":
                break
            if var in s and CE_RE.search(s):
                return "CHECKED", "E2_STORED_IN_%s_THEN_COMPARED_TO_CE" % var
        return "UNCHECKED", "RETURN_VALUE_STORED_IN_%s_BUT_NEVER_COMPARED_TO_CE" % var
    return "UNCHECKED", "RETURN_VALUE_DISCARDED"


def iter_cpp_files(root_abs, exclude_parts):
    for dirpath, dirnames, filenames in os.walk(root_abs):
        relparts = set(os.path.relpath(dirpath, root_abs).split(os.sep))
        if relparts & set(exclude_parts):
            dirnames[:] = []
            continue
        dirnames[:] = [d for d in dirnames if d not in set(exclude_parts)]
        for fn in sorted(filenames):
            if fn.endswith(CPP_EXTS):
                yield os.path.join(dirpath, fn)


def normalize_sig(text):
    return re.sub(r"\s+", " ", text).strip()


def scan_domain(rel_base, files):
    """返回 dict：桶计数 + CODE 位点明细 + 逐位点判定。

    rel_base = 位点路径的命名基准：真域用**仓库根**（白名单里的 file 就是仓库相对路径），
    自证臂用夹具目录（内联白名单点的是裸文件名）。两处基准不一致就是第一次跑守卫时
    FILE_GONE × 3 的根因 —— 现在把它做成显式参数，两个调用点各自传自己那一个。
    """
    counts = {"any": 0, "code": 0, "comment_line": 0, "block_comment": 0, "string_only": 0}
    sites = []
    for path in files:
        with open(path, "r", encoding="utf-8-sig", newline="") as fh:
            text = fh.read()
        raw_lines = text.splitlines()
        masked_lines, flag_lines = read_masked(text)
        rel = os.path.relpath(path, rel_base).replace(os.sep, "/")
        for i, raw in enumerate(raw_lines):
            pos = 0
            while True:
                pos = raw.find(TOKEN, pos)
                if pos < 0:
                    break
                counts["any"] += 1
                if pos >= len(flag_lines[i]):
                    # 对齐失败本身就是解析器坏了 ⇒ 当场炸，不许悄悄按"代码"归类。
                    raise AssertionError("%s:%d TOKEN offset %d beyond flags length %d"
                                         % (rel, i + 1, pos, len(flag_lines[i])))
                flag = flag_lines[i][pos]
                if flag in BUCKET_BY_FLAG:
                    counts[BUCKET_BY_FLAG[flag]] += 1
                else:
                    counts["code"] += 1
                    verdict, why = classify_code_site(masked_lines, i)
                    sites.append({
                        "file": rel,
                        "line": i + 1,
                        "signature": normalize_sig(masked_lines[i]),
                        "verdict": verdict,
                        "why": why,
                    })
                pos += len(TOKEN)
    counts["checked"] = sum(1 for s in sites if s["verdict"] == "CHECKED")
    counts["unchecked"] = sum(1 for s in sites if s["verdict"] == "UNCHECKED")
    return {"counts": counts, "sites": sites}


def load_allowlist(path):
    with open(path, "r", encoding="utf-8") as fh:
        data = json.load(fh)
    problems = []
    for key in ("entries", "test_entries"):
        if key not in data or not isinstance(data[key], list):
            problems.append("ALLOWLIST_SCHEMA missing array %s" % key)
            continue
        seen_ids = set()
        for e in data[key]:
            for f in ENTRY_REQUIRED_FIELDS:
                v = e.get(f)
                if not isinstance(v, str) or not v.strip():
                    problems.append("ALLOWLIST_SCHEMA entry %s field %s missing or empty" % (e.get("id"), f))
            if e.get("id") in seen_ids:
                problems.append("ALLOWLIST_SCHEMA duplicated id %s" % e.get("id"))
            seen_ids.add(e.get("id"))
    return data, problems


def apply_allowlist(result, entries, root_abs, domain_name):
    """把 UNCHECKED 位点与白名单对撞：没被点名的 = VIOLATION，点不到活位点的 = DEAD。"""
    unchecked = [s for s in result["sites"] if s["verdict"] == "UNCHECKED"]
    violations = []
    allowlisted = 0
    dead = []
    used_ids = set()
    for e in entries:
        matches = [s for s in unchecked
                   if s["file"] == e["file"] and s["signature"] == normalize_sig(e["signature"])]
        if len(matches) == 0:
            reason_dead = "SITE_NOT_FOUND"
            if not os.path.exists(os.path.join(root_abs, e["file"].replace("/", os.sep))):
                reason_dead = "FILE_GONE"
            elif not re.search(r"\b" + re.escape(e["function"].split("::")[-1]) + r"\b",
                               _file_code_text(root_abs, e["file"])):
                reason_dead = "FUNCTION_GONE"
            dead.append("%s %s %s::%s" % (domain_name, reason_dead, e["id"], e["file"]))
            continue
        if len(matches) > 1:
            dead.append("%s AMBIGUOUS_ALLOWLIST %s matches %d sites (exemptions must name one site each)"
                        % (domain_name, e["id"], len(matches)))
            continue
        used_ids.add(e["id"])
        allowlisted += 1
        matches[0]["allowlist_id"] = e["id"]
    for s in unchecked:
        if "allowlist_id" not in s:
            violations.append(s)
    result["violations"] = violations
    result["dead"] = dead
    result["allowlisted"] = allowlisted
    counts = result["counts"]
    counts["allowlisted"] = allowlisted
    counts["violations"] = len(violations)
    counts["dead"] = len(dead)
    return result


_CODE_TEXT_CACHE = {}


def _file_code_text(root_abs, rel):
    if rel in _CODE_TEXT_CACHE:
        return _CODE_TEXT_CACHE[rel]
    path = os.path.join(root_abs, rel.replace("/", os.sep))
    text = ""
    try:
        with open(path, "r", encoding="utf-8-sig", newline="") as fh:
            masked, _ = read_masked(fh.read())
        text = "\n".join(masked)
    except OSError:
        text = ""
    _CODE_TEXT_CACHE[rel] = text
    return text


def evaluate(label, result, expect, out):
    """把现读数与期望数逐桶对撞，返回 mismatch 列表（同时把两侧读数都打出来）。"""
    c = dict(result["counts"])
    mismatches = []
    if c["any"] != c["code"] + c["comment_line"] + c["block_comment"] + c["string_only"]:
        mismatches.append(("anchor", c["any"],
                           c["code"] + c["comment_line"] + c["block_comment"] + c["string_only"]))
    for key in ("any", "code", "comment_line", "block_comment", "string_only",
                "checked", "unchecked", "allowlisted", "violations", "dead"):
        if key in expect and c[key] != expect[key]:
            mismatches.append((key, expect[key], c[key]))
    out.append("GTGUARD SCAN domain=%s any=%d code=%d comment_line=%d block_comment=%d "
               "string_only=%d checked=%d unchecked=%d allowlisted=%d violations=%d dead=%d"
               % (label, c["any"], c["code"], c["comment_line"], c["block_comment"],
                  c["string_only"], c["checked"], c["unchecked"], c["allowlisted"],
                  c["violations"], c["dead"]))
    for s in result["sites"]:
        if s["verdict"] == "UNCHECKED":
            out.append("GTGUARD SITE %s:%d %s [%s] %s"
                       % (s["file"], s["line"], s["signature"],
                          s.get("allowlist_id", "NOT-ALLOWLISTED"), s["why"]))
    for v in result.get("violations", []):
        out.append("GTGUARD VIOLATION %s:%d %s %s" % (v["file"], v["line"], v["signature"], v["why"]))
    for d in result.get("dead", []):
        out.append("GTGUARD DEAD_ALLOWLIST %s" % d)
    for name, exp, act in mismatches:
        out.append("GTGUARD EXPECT_MISMATCH domain=%s field=%s expected=%s actual=%s"
                   % (label, name, exp, act))
    return mismatches


# ───────────────────────────────────────────────────────────────────────────────
# 自证臂（Step 1：先于域判定跑；任何一臂不对就 exit 2，仓库结论不予发布）
#
# 设计约束（批次 C 的教训：docs_match_cli 第一版夹具只埋 1 行、四类缺陷只报得出 2 类）：
# 每一臂断言的是**具体数字**，不是"非 0"；红臂与绿臂成对出现，免得判据本身是恒真的。
# ───────────────────────────────────────────────────────────────────────────────
FIXTURE_DIR = os.path.join("tests", "geotransform_guard_fixtures")

SELF_ARMS = [
    # arm id, 目录/文件, 白名单（内联，不读仓库那份）, 期望, 是否要求"必须红"
    {
        "id": "bad_bare_sites_are_caught",
        "files": ("bad_unchecked.cpp",),
        "allowlist": [],
        "expect": {"any": 7, "code": 3, "comment_line": 1, "block_comment": 1, "string_only": 2,
                   "checked": 0, "unchecked": 3, "allowlisted": 0, "violations": 3, "dead": 0},
        "expect_red": False,
    },
    {
        # 白名单不是装饰品：点名到 bad 那一处时，违规数必须**恰好少 1**。
        "id": "allowlist_actually_binds",
        "files": ("bad_unchecked.cpp",),
        "allowlist": [{"id": "FAKE-A", "file": "bad_unchecked.cpp", "function": "badSiteBareCall",
                       "signature": "srcDS->GetGeoTransform(adfGT);", "fallback": "x", "reason": "x"}],
        "expect": {"any": 7, "code": 3, "comment_line": 1, "block_comment": 1, "string_only": 2,
                   "checked": 0, "unchecked": 3, "allowlisted": 1, "violations": 2, "dead": 0},
        "expect_red": False,
    },
    {
        # DEAD 臂：白名单点到不存在的位点必须红（"豁免用不上 = 该删"）。
        # 期望值刻意写"应当 0 个 DEAD、3 个违规"⇒ 只有 dead 字段会不符，red_field 据此钉住红的来源，
        # 免得这条臂靠别的字段凑红（那就等于没在判 DEAD）。
        "id": "dead_allowlist_is_red",
        "files": ("bad_unchecked.cpp",),
        "allowlist": [{"id": "FAKE-DEAD", "file": "bad_unchecked.cpp", "function": "goneFunction",
                       "signature": "nothing->GetGeoTransform(zzz);", "fallback": "x", "reason": "x"}],
        "expect": {"any": 7, "code": 3, "comment_line": 1, "block_comment": 1, "string_only": 2,
                   "checked": 0, "unchecked": 3, "allowlisted": 0, "violations": 3, "dead": 0},
        "expect_red": True,
        "red_field": "dead",
    },
    {
        # 反向：把仓库里 6 种"已判"写法各埋一次，必须全认成 CHECKED（0 违规）。
        # 少了这一臂，"把所有位点都判成 UNCHECKED"的坏解析器能过前面三臂。
        "id": "checked_shapes_are_not_flagged",
        "files": ("ok_checked.cpp",),
        "allowlist": [],
        "expect": {"any": 8, "code": 6, "comment_line": 1, "block_comment": 0, "string_only": 1,
                   "checked": 6, "unchecked": 0, "allowlisted": 0, "violations": 0, "dead": 0},
        "expect_red": False,
    },
    {
        # 归零闸的正反两面：期望值判据必须真的拦得住"位点数变少"。
        "id": "zero_code_sites_vs_expectation_is_red",
        "files": ("empty_domain/placeholder.cpp",),
        "allowlist": [],
        "expect": dict(DOMAINS[0]["expect"]),
        "expect_red": True,
        "red_field": "code",
    },
    {
        # ……而同一个目录用 code=0 的期望扫必须绿 ⇒ 上一条的红来自期望值，不是恒真断言。
        "id": "zero_code_sites_with_zero_expectation_is_green",
        "files": ("empty_domain/placeholder.cpp",),
        "allowlist": [],
        "expect": {"any": 0, "code": 0, "comment_line": 0, "block_comment": 0, "string_only": 0,
                   "checked": 0, "unchecked": 0, "allowlisted": 0, "violations": 0, "dead": 0},
        "expect_red": False,
    },
]


def run_self_check(root_abs, out):
    fixture_abs = os.path.join(root_abs, FIXTURE_DIR.replace("/", os.sep))
    if not os.path.isdir(fixture_abs):
        out.append("GTGUARD SELFCHECK_FAIL arm=fixtures_dir_present expected=exists actual=missing")
        return False
    ok = True
    for arm in SELF_ARMS:
        files = [os.path.join(fixture_abs, f.replace("/", os.sep)) for f in arm["files"]]
        missing = [f for f in files if not os.path.isfile(f)]
        if missing:
            out.append("GTGUARD SELFCHECK_FAIL arm=%s expected=fixture_present actual=missing:%s"
                       % (arm["id"], ",".join(os.path.basename(m) for m in missing)))
            ok = False
            continue
        # 夹具目录当作独立根：位点里的 file 就是文件名本身，白名单内联项才点得到。
        result = scan_domain(fixture_abs, files)
        result = apply_allowlist(result, arm["allowlist"], fixture_abs, "fixture:" + arm["id"])
        mismatches = evaluate("arm=" + arm["id"], result, arm["expect"] or {}, out)
        is_red = bool(mismatches)
        if arm["expect_red"]:
            if not is_red:
                out.append("GTGUARD SELFCHECK_FAIL arm=%s expected=RED actual=GREEN "
                           "(this arm blocks nothing: its assertion is vacuous)" % arm["id"])
                ok = False
            elif arm.get("red_field") and not any(m[0] == arm["red_field"] for m in mismatches):
                out.append("GTGUARD SELFCHECK_FAIL arm=%s expected=RED_ON_field=%s actual=red_on=%s"
                           % (arm["id"], arm["red_field"], ",".join(m[0] for m in mismatches)))
                ok = False
            else:
                out.append("GTGUARD SELFCHECK_PASS arm=%s red_on_expected_field" % arm["id"])
        else:
            if is_red:
                out.append("GTGUARD SELFCHECK_FAIL arm=%s expected=GREEN actual=RED "
                           "(parser disagrees with the defect families declared in the fixture)" % arm["id"])
                ok = False
            else:
                out.append("GTGUARD SELFCHECK_PASS arm=%s" % arm["id"])
    return ok


def check_fixture_exclusion(root_abs, out):
    """自证臂之外还有一件事必须钉住：夹具里那些故意坏的位点不许被真域扫到（否则守卫永远红）。"""
    bad = []
    for dom in DOMAINS:
        root_abs_path = os.path.join(root_abs, dom["root"])
        for path in iter_cpp_files(root_abs_path, dom["exclude_parts"]):
            rel = os.path.relpath(path, root_abs).replace(os.sep, "/")
            if FIXTURE_DIR + "/" in rel + "/":
                bad.append("%s:%s" % (dom["name"], rel))
    if bad:
        out.append("GTGUARD SELFCHECK_FAIL arm=fixtures_excluded_from_real_domains actual=%s"
                   % ",".join(bad))
        return False
    out.append("GTGUARD SELFCHECK_PASS arm=fixtures_excluded_from_real_domains")
    return True


def main():
    ap = argparse.ArgumentParser(description="GetGeoTransform anti-regression guard")
    ap.add_argument("--repo-root", required=True)
    ap.add_argument("--allowlist", required=True)
    args = ap.parse_args()

    root_abs = os.path.abspath(args.repo_root)
    out = []
    if not os.path.isdir(root_abs):
        print("GTGUARD USAGE_ERROR repo-root not a directory: %s" % root_abs)
        return EXIT_USAGE
    if not os.path.isfile(args.allowlist):
        print("GTGUARD USAGE_ERROR allowlist file missing: %s (refuse to pass: no named exemption means no check)" % args.allowlist)
        return EXIT_USAGE

    data, schema_problems = load_allowlist(os.path.abspath(args.allowlist))
    if schema_problems:
        for p in schema_problems:
            print("GTGUARD USAGE_ERROR %s" % p)
        return EXIT_USAGE
    if len(data["entries"]) != len(PRODUCT_ALLOWLIST_IDS) or \
       tuple(e["id"] for e in data["entries"]) != PRODUCT_ALLOWLIST_IDS:
        print("GTGUARD EXPECT_MISMATCH domain=product field=allowlist_ids expected=%s actual=%s"
              % (",".join(PRODUCT_ALLOWLIST_IDS), ",".join(e["id"] for e in data["entries"])))
        return EXIT_REPO

    self_ok = True
    self_ok = run_self_check(root_abs, out)
    self_ok = check_fixture_exclusion(root_abs, out) and self_ok

    repo_lines = []
    repo_problems = False
    for dom in DOMAINS:
        root_abs_path = os.path.join(root_abs, dom["root"])
        if not os.path.isdir(root_abs_path):
            repo_lines.append("GTGUARD EXPECT_MISMATCH domain=%s field=root expected=%s actual=missing"
                              % (dom["name"], dom["root"]))
            repo_problems = True
            continue
        files = list(iter_cpp_files(root_abs_path, dom["exclude_parts"]))
        if not files:
            repo_lines.append("GTGUARD PARSER_DEAD domain=%s scanned 0 C/C++ files - wrong domain root, "
                              "zero sites is never good news" % dom["name"])
            repo_problems = True
            continue
        result = scan_domain(root_abs, files)
        result = apply_allowlist(result, data[dom["allowlist_key"]], root_abs, dom["name"])
        mismatches = evaluate("%s(%d files)" % (dom["name"], len(files)), result, dom["expect"], repo_lines)
        if mismatches or result["violations"]:
            repo_problems = True

    sys.stdout.write("\n".join(out) + "\n")
    if not self_ok:
        sys.stdout.write("GTGUARD RESULT FAIL (SELFCHECK)\n")
        sys.stdout.write("\n".join(repo_lines) + "\n")
        return EXIT_SELFCHECK
    sys.stdout.write("\n".join(repo_lines) + "\n")
    if repo_problems:
        sys.stdout.write("GTGUARD RESULT FAIL (REPO)\n")
        return EXIT_REPO
    sys.stdout.write("GTGUARD RESULT PASS\n")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
