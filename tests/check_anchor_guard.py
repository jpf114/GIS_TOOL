#!/usr/bin/env python
# -*- coding: utf-8 -*-
r"""裸行号锚点守卫 anchor_guard（批次 D / N178 的机器兜底）。

R-D34 早就定了"已提交文本不许写裸行号锚"，但一直没有判据，所以活注释里仍大量存在
`<path>/<file>.<ext>:<NNN>`，而且**已经漂了**（本刀现读 3 处指向被删掉的行）。本守卫把这条规矩
变成机器话，并顺手把"漂移"本身抓出来：锚必须能解析到本仓真实文件的真实行号范围内。

判据分四类（kind），逐类都有自证臂：
  range      —— 锚解析到本仓文件，但行号（或区间的上端）超过该文件行数 ⇒ 行被删过/文件缩了。
  unresolved —— 锚解析不到本仓文件，且文件名不在上游名单里 ⇒ 典型形态是把
                 `vector_plugin_common.inc` 简写成 `common.inc`（读者与机器都解不开）。
  ambiguous  —— 不带目录的裸文件名在本仓命中 >1 个文件 ⇒ 读者必须自己猜是哪一个。
  ceiling / dead-registry —— 见下面"棘轮"与"名单腐化"。

**棘轮（ceiling）**：登记表里的 `anchor_ceiling` 必须**逐字等于**现读的锚次数。
  多一条 ⇒ 红（有人新写了裸锚，违反 R-D34）；少一条 ⇒ 红（有人清了锚却没把界降下来，
  于是下一刀可以悄悄再加回去）。这一条同时充当"扫描真的跑了"的地板：解析器整体失效 ⇒
  现读掉到 0 ⇒ 与界不等 ⇒ 红，不需要另设地板。

**上游名单（upstream_files）**：`gcore/gdaldataset.cpp:1509`、`qsettings.h:184` 这类锚指向的是
GDAL/Qt/OpenCV/GEOS/MSVC-STL 自己的源文件，本仓没有 ⇒ 无法在本地核行号，但**必须点名登记**：
  - 只有锚的**最后一段文件名**命中名单才算合法（目录前缀随便写，命中不到名单就判 unresolved）；
  - 名单里每条都要带 `lib`（哪个上游 + 版本口径）与 `evidence`（为什么这句值得钉）；
  - 名单条目本轮没被任何锚用到 ⇒ 判红（`dead-registry`，口径同 docs_match_cli 的 DEAD-ALLOWLIST），
    否则上游文件改名后名单会和那批锚一起烂掉。

退出码（唯一消费者是 tests/check_anchor_guard.cmake）：
  0 = 自证臂 + 真仓判定都过
  1 = 真仓判定红（range / unresolved / ambiguous / ceiling / dead-registry）
  2 = 自证臂红（判据自己不可信 ⇒ 对仓库的结论一律作废，先于真仓）
  3 = 用法/环境错误（缺 --repo-root、缺登记表、登记表形态不对）

三条已量的射程限制（不是缺陷待办，是这条判据的形状，别把它当成"锚都核对过了"）：
  ① 上游锚只核"文件名登记过"，**不核行号**：本机 `vcpkg_installed/` 只有 vcpkg/info 与 vcpkg/updates，
     没有 port 源文件 ⇒ 上游行号属于"写那次注释时的现读"，机器无法在此刻否证。
  ② 本仓锚只核"文件在、行号不超过文件行数"，**不核内容**：现读里 `overlay.inc` 那批简写锚补全路径后
     行号仍在范围内但指的却是别的代码 ⇒ 窄规则的 0 是假绿，所以内容层面的清理走"逐刀迁移成符号名"，
     由 ceiling 的"必须等于现读"托住不许增长。
  ③ 只认 `name.ext:N` 形态：写成 `:1506-:1512` 这种"续行裸号"的锚看不见（正则要吃文件名，
     否则 `value: 5` 一片误红）。本把顺手把见到的一处续行裸号改成了符号写法。

复算（与 ctest 注册的那条完全同形）：
  python tests/check_anchor_guard.py --repo-root . --config tests/anchor_guard_config.json
"""

import argparse
import json
import pathlib
import re
import sys

EXIT_OK = 0
EXIT_REPO = 1
EXIT_SELFCHECK = 2
EXIT_USAGE = 3

# 锚的形态：可选目录前缀 + 文件名 + .ext + :N 或 :N-M。
# 刻意**不**把空格与 `+` 放进字符类：第一版探针放了，于是 ":1506-1512 + gcore/gdal_geotransform.h"
# 里的空白与加号被吞进文件名，8 条本来能解析的锚被误判成 unresolved ⇒ 假缺陷数从 35 涨到 49。
EXTS = r"cpp|h|hpp|cc|inc|ps1|py|cmake|json|txt"
ANCHOR_RE = re.compile(r"((?:[A-Za-z0-9_\-.]+/)*[A-Za-z0-9_\-.]+\.(?:" + EXTS + r")):(\d+)(?:-(\d+))?")


def say(msg):
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()


# 逐行原文（UTF-8）由 --report 落盘；stdout 只留 ASCII 形态，口径同 check_docs_match_cli.py。
REPORT = []


def load_config(path):
    cfg = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    for key in ("carriers", "index_roots", "anchor_ceiling", "upstream_files"):
        if key not in cfg:
            raise SystemExit("%d 登记表缺键 %s（形态不对，不许当空表用）" % (EXIT_USAGE, key))
    for u in cfg["upstream_files"]:
        if not u.get("name") or not u.get("lib") or not u.get("evidence"):
            raise SystemExit("%d 上游名单条目缺 name/lib/evidence：%s" % (EXIT_USAGE, u))
    return cfg


# ───────────────────────────────────────────────────────────────────────────────
# 判据本体（RED 阶段先留桩：返回空 findings，自证臂必须因此全红）
# ───────────────────────────────────────────────────────────────────────────────
def build_index(root, index_roots):
    """本仓可解析文件的两张表：全路径集合 + 文件名 -> 全路径列表。"""
    rels = set()
    by_last = {}
    for r in index_roots:
        d = root / r
        if not d.is_dir():
            continue
        for p in d.rglob("*"):
            if p.is_file():
                rel = p.relative_to(root).as_posix()
                rels.add(rel)
                by_last.setdefault(p.name, []).append(rel)
    return rels, {k: sorted(v) for k, v in by_last.items()}


def resolve_target(anchor_path, rels, by_last):
    """锚写的路径 → 本仓候选全路径列表（空列表 = 解不开）。

    带目录的锚按**整段后缀**比：`cli/main.cpp` 能对上 `src/cli/main.cpp`，
    而 `common.inc` 对不上 `vector_plugin_common.inc`（段不等）⇒ 判 unresolved，
    这正是"把文件名简写"那批锚要的处置。不带目录的按文件名命中，>1 个由调用方判 ambiguous。
    """
    parts = anchor_path.split("/")
    if len(parts) == 1:
        return list(by_last.get(parts[0], []))
    n = len(parts)
    return sorted(rel for rel in rels
                  if len(rel.split("/")) >= n and rel.split("/")[-n:] == parts)


def judge(root, cfg):
    """扫 root 里的载体文件，返回 (findings, stats)。

    findings 元素 = (kind, carrier, anchor, message)，message 面向人读。
    """
    findings = []
    rels, by_last = build_index(root, cfg["index_roots"])
    upstream = {u["name"]: u for u in cfg["upstream_files"]}
    used_upstream = set()
    line_cache = {}
    stats = {"carrier_files": 0, "anchor_lines": 0, "anchors": 0, "local_ok": 0,
             "upstream": 0, "range": 0, "unresolved": 0, "ambiguous": 0,
             "dead_registry": 0}

    def n_lines(rel):
        if rel not in line_cache:
            line_cache[rel] = len((root / rel).read_bytes().split(b"\n"))
        return line_cache[rel]

    exts = tuple("." + e for e in cfg["carriers"]["exts"])
    carriers = []
    for r in cfg["carriers"]["roots"]:
        d = root / r
        if not d.is_dir():
            continue
        for p in sorted(d.rglob("*")):
            if p.is_file() and p.suffix in exts:
                carriers.append(p)
    for p in carriers:
        rel_carrier = p.relative_to(root).as_posix()
        try:
            text = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        hit_in_file = 0
        for lineno, line in enumerate(text.splitlines(), 1):
            ms = list(ANCHOR_RE.finditer(line))
            if not ms:
                continue
            hit_in_file += len(ms)
            stats["anchor_lines"] += 1
            for m in ms:
                stats["anchors"] += 1
                # 前导点要脱掉：`...deriv.cpp:66` 是**原样引用的 OpenCV 异常串**（test_plugins.cpp:3142），
                # 字符类里的 `.` 会把省略号吞进文件名 ⇒ 段比对必败，把一条合法的上游锚误判 unresolved。
                # 脱掉之后它按 basename `deriv.cpp` 走上游名单；引文一个字不动。
                ap_ = m.group(1).lstrip(".")
                a, b = int(m.group(2)), m.group(3)
                top = int(b) if b else a
                where = "%s:%d" % (rel_carrier, lineno)
                cands = resolve_target(ap_, rels, by_last)
                if len(cands) > 1:
                    stats["ambiguous"] += 1
                    findings.append(("ambiguous", rel_carrier, m.group(0),
                                     "%s 锚 %s 在本仓命中 %d 个文件（%s）⇒ 裸名要写成带目录的路径"
                                     % (where, ap_, len(cands), ", ".join(cands))))
                    continue
                if cands:
                    rel = cands[0]
                    hi = n_lines(rel)
                    if top > hi:
                        stats["range"] += 1
                        findings.append(("range", rel_carrier, m.group(0),
                                         "%s 锚 %s 指向 %s，但该文件只有 %d 行 ⇒ 目标行已被删/挪"
                                         % (where, m.group(0), rel, hi)))
                        continue
                    stats["local_ok"] += 1
                    continue
                last = ap_.split("/")[-1]
                if last in upstream:
                    stats["upstream"] += 1
                    used_upstream.add(last)
                    continue
                stats["unresolved"] += 1
                findings.append(("unresolved", rel_carrier, m.group(0),
                                 "%s 锚 %s 既不在本仓、也没登记为上游 ⇒ 要么补全路径，要么进 upstream_files"
                                 % (where, m.group(0))))
        if hit_in_file:
            stats["carrier_files"] += 1

    for name, ent in sorted(upstream.items()):
        if name not in used_upstream:
            stats["dead_registry"] += 1
            findings.append(("dead-registry", "config", name,
                             "上游名单条目 %s（%s）本轮没有任何锚用到 ⇒ 删掉它" % (name, ent["lib"])))

    if stats["anchors"] != cfg["anchor_ceiling"]:
        direction = ("多了 %d 条 ⇒ 新写了裸行号锚（违反 R-D34）"
                     % (stats["anchors"] - cfg["anchor_ceiling"])
                     if stats["anchors"] > cfg["anchor_ceiling"] else
                     "少了 %d 条 ⇒ 把登记表里的 anchor_ceiling 降到现读 %d"
                     % (cfg["anchor_ceiling"] - stats["anchors"], stats["anchors"]))
        findings.append(("ceiling", "config", "anchor_ceiling",
                         "锚次数现读 %d != 登记表 %d：%s"
                         % (stats["anchors"], cfg["anchor_ceiling"], direction)))
    return findings, stats


def self_test():
    """合成一个小仓库，逐类注入缺陷，判据必须每类都点名；干净夹具必须 0 漂移。"""
    import tempfile
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="anchor_selftest_"))
    (tmp / "src").mkdir()
    (tmp / "tests").mkdir()
    # 被引对象：只有 3 行 ⇒ :9 一定越界
    (tmp / "src/a.cpp").write_text("line1\nline2\nline3\n", encoding="utf-8")
    # 同名歧义夹具：两个目录里各有一个 dup.cpp
    (tmp / "src/dup.cpp").write_text("x\ny\nz\n", encoding="utf-8")
    (tmp / "tests/dup.cpp").write_text("x\ny\nz\n", encoding="utf-8")
    # 简称夹具：真文件叫 vector_plugin_common.inc，注释里写成 common.inc
    (tmp / "src/vector_plugin_common.inc").write_text("a\nb\nc\nd\ne\n", encoding="utf-8")
    # 夹具文本里的锚一律用 A() 现场拼：直接写成字面量就会让本文件自己变成载体
    # ⇒ 守卫会把夹具里的例证算进现读锚数（棘轮界与真仓数一起被污染）。
    def A(name, a, b=None):
        return "%s:%s" % (name, a if b is None else "%d-%d" % (a, b))

    good = tmp / "tests/ok.txt"
    bad = tmp / "tests/bad.txt"
    good.write_text("本仓锚 %s 合法；上游锚 %s 已登记。\n"
                    % (A("src/a.cpp", 2), A("gcore/gdaldataset.cpp", 1509)), encoding="utf-8")
    bad.write_text(
        "越界 %s；\n"
        "简称 %s；\n"
        "裸名歧义 %s；\n"
        "没登记的上游 %s。\n" % (A("src/a.cpp", 9), A("src/common.inc", 3),
                                A("dup.cpp", 2), A("some_random.cpp", 1)),
        encoding="utf-8")
    cfg = {
        "carriers": {"roots": ["src", "tests"], "exts": ["txt"]},
        "index_roots": ["src", "tests"],
        "anchor_ceiling": 2,
        "upstream_files": [{"name": "gdaldataset.cpp", "lib": "GDAL 3.12.4", "evidence": "x"}],
    }
    outs = []

    findings, stats = judge(tmp, cfg)
    kinds = {f[0] for f in findings}
    outs.append(("绿臂：好文档那条夹具不该报 anything（现报 %d 条）" % len(findings),
                 all(f[1] != "tests/ok.txt" for f in findings)))
    want = {"range", "unresolved", "ambiguous"}
    outs.append(("红臂：三类缺陷各点名一次（本轮 kinds=%s，bad.txt 报 %d 条）" % (
        sorted(kinds), len([f for f in findings if f[1] == "tests/bad.txt"])),
        {"range", "unresolved", "ambiguous"} <= kinds))
    outs.append(("红臂：未登记的上游单独点名（some_random.cpp）",
                 any("some_random.cpp" in f[3] for f in findings)))
    outs.append(("红臂：棘轮现读 != 界 ⇒ 点名（现读 %d 界 %d）" % (
        stats.get("anchors", -1), cfg["anchor_ceiling"]),
        "ceiling" in kinds))
    cfg_lo = dict(cfg)
    cfg_lo["anchor_ceiling"] = 99
    f_lo, stats_lo = judge(tmp, cfg_lo)
    # 这条第一版写成 `stats_lo["anchors"] != 99`：桩函数回 0 也满足 ⇒ 恒真是条假臂。
    # 判"清理后没把界降下来"必须看 findings 里有没有 ceiling，而不是看数不等。
    outs.append(("红臂：界高于现读也判红（不许只拦增长不拦清理；本轮 ceiling=%d 现读=%d）" % (
        99, stats_lo["anchors"]),
        any(f[0] == "ceiling" for f in f_lo)))
    cfg_dead = dict(cfg)
    cfg_dead["anchor_ceiling"] = stats["anchors"]
    cfg_dead["upstream_files"] = [{"name": "never_used.cpp", "lib": "x", "evidence": "y"}]
    f_dead, _ = judge(tmp, cfg_dead)
    outs.append(("红臂：用不到的上游条目判 dead-registry（本轮 kinds=%s）" % sorted({f[0] for f in f_dead}),
                 "dead-registry" in {f[0] for f in f_dead}))
    for name, passed in outs:
        say("%s  %s" % ("OK  " if passed else "FAIL", name))
    return EXIT_OK if all(p for _, p in outs) else EXIT_SELFCHECK


def main():
    # stdout 一律 ASCII（cmake -P 的 message() 会把子进程 UTF-8 按本地码页重编一次 ⇒ 中文变乱码，
    # 与本仓 docs_match_cli / geotransform_guard 同一条本机限制）；原文另写 --report 指向的 UTF-8 文件。
    try:
        sys.stdout.reconfigure(encoding="ascii", errors="backslashreplace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="裸行号锚点守卫（anchor_guard）")
    ap.add_argument("--repo-root", default=".")
    ap.add_argument("--config", default="")
    ap.add_argument("--report", default="", help="把逐行原文（UTF-8）写进这个文件")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    rc = EXIT_OK
    try:
        if a.self_test:
            return self_test()
        if not a.config:
            say("用法错误：真跑必须给 --config")
            return EXIT_USAGE
        root = pathlib.Path(a.repo_root)
        if not root.is_dir():
            say("用法错误：--repo-root 不存在：%s" % root)
            return EXIT_USAGE
        cfg = load_config(a.config)
        rc_self = self_test()
        if rc_self != EXIT_OK:
            say("SELF-TEST 红 ⇒ 判据自己不可信，真仓结论不作数")
            return rc_self
        findings, stats = judge(root, cfg)
        summary = ("carriers=%d anchor_lines=%d anchors=%d local_ok=%d upstream=%d "
                   "range=%d unresolved=%d ambiguous=%d ceiling=%d dead_registry=%d" % (
                       stats["carrier_files"], stats["anchor_lines"], stats["anchors"],
                       stats["local_ok"], stats["upstream"], stats["range"], stats["unresolved"],
                       stats["ambiguous"], cfg["anchor_ceiling"], stats["dead_registry"]))
        REPORT.append(summary)
        say(summary)
        for f in findings:
            REPORT.append(f[3])
            say("DRIFT " + f[3])
        rc = EXIT_REPO if findings else EXIT_OK
        return rc
    finally:
        if a.report:
            pathlib.Path(a.report).write_text("\n".join(REPORT) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
