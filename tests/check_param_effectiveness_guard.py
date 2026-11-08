#!/usr/bin/env python
# -*- coding: utf-8 -*-
r"""上游边界参数有效性棘轮守卫 param_effectiveness_guard（批次 D / #164，裁定 R-D71）。

命题：凡是"可见参数的值离开本仓、进上游 C API/argv"的接线点，每个 flag 字面量都必须登记在册，
且登记必须链到一个真实存在的 gtest 证据用例名；登记在册的 flag 从源码里消失、登记行被删、
或出现未登记的新边界 flag/新边界文件，守卫必须红。本守卫是完备性棘轮（新接线必须带证据注册；
既有接线被拆必须红），逐实例的"值级真生效"仍由各刀的 gtest 值判据承重。

为什么射程只到"完备性棘轮"：观察到的"声明了、读了、静默无效"全部发生在值跨仓界那一步
（z_factor 在 slope/aspect 被上游静默丢弃，N163 处置）；仓内消费的参数由 param_guard /
124/126 动作的 GOLD 值判据钉着；通用"全部可见键三元组逐一配行为变异档"的注册表另立 N196，
本守卫不做（尺寸不够，别假装做了）。

登记表 tests/param_effectiveness_registry.json：条目 = {file, context, flag, key, evidence_test, note}，
逐 site 现读填（46 grep 行 / 5 文件 / 7 逻辑调用点 + 1 零 flag 调用点，映射见登记表 _comment）。

判据五条（判定与登记表对撞，全部在本文件内机器化）：
  a. 登记表每个条目的 flag 字面量在该条目 site 文件的引号 flag 集里仍找得到
     （丢 = N163 型回归当场红：登记还在、源码不发了）；
  b. 登记表每个条目的非空 evidence_test 名在 tests/test_plugins.cpp 里存在
     （gtest 改名/删除 ⇒ 登记表被逼复审）；
  c. site 文件集合与"扫 src/ 边界调用符号得到的文件集合"双向相等
     （新边界文件不登记 = 红；登记表出现已不是边界的文件 = 红）；
  d. 每个 site 文件内引号 flag 字面量集合 ⊆ 登记表该文件的 flag 集合（新 flag 不登记 = 红）；
  e. 自证臂（本文件 self_test，≥4 且配对设计，每臂红时证明哪个命题写明），
     自证臂先于真仓判定：判据自己红 ⇒ 对仓库的结论一律不作数（与 anchor_guard 同一顺序）。

自证臂清单（合成夹具仓上跑，永不触碰真仓）：
  绿臂  干净夹具必须 0 findings——证明判据不是恒红。
  臂A   改一个 flag 假名（登记表 -s → -sX）⇒ a 红——证明"登记在册的 flag 从源码里消失（含名实不符）必须红"。
  臂B   删一行登记（-b）⇒ d 红——证明"登记行被删、源码 flag 成为未登记新旗"必须红（完备性另一侧）。
  臂C   evidence_test 改成不存在的名 ⇒ b 红——证明"登记必须链到真实存在的 gtest 证据用例名"。
  臂D   在既有 site 文件里临时造一个新 flag（push_back("-newflag")）⇒ d 红——证明"新边界 flag 不登记 = 红"。
  臂E   新造一个含边界调用符号的文件进 site 集合 ⇒ c 红（新边界文件不登记）——证明"新边界文件必须带登记进棘轮"。
  臂F   登记表把某条目的 file 改成已不是边界的文件 ⇒ c 红（反向）——证明棘轮双向：文件退出边界也必须改登记表。

flag 扫描形状（陷阱规避，本仓 N193/段模型与短横线误捕两案在先）：
  只认「双引号串整体 = 单个短横线 + 首字母为字母 + 后续字母/数字/下划线」：
  - 首字母锚定为字母 ⇒ `-9999` 哨兵与以短横线开头的文件名实参不进集合（任务书点名的误捕陷阱）；
  - 后续允许字母/数字/下划线 ⇒ `-t_srs`/`-srcwin`/`-crop_to_cutline` 这族真 flag 在棘轮内
    （若只认纯字母，这族会静默逃出 d 判据，新写 `-foo_bar` 同样逃逸——形状放宽是收紧不是放松）；
  - 扫描按登记表 site 文件白名单逐文件做，不全仓正则撒网；site 集合之外不扫（c 判据负责把
    "site 集合该不该扩"先钉住，d 判据只对已定界的文件清单负责）；
  - 双引号串按朴素扫描（不建模转义/字符字面量/原始串）：误捕只会产生假红（可见、当场修），
    不会产生假绿——方向安全。现读（2026-11-09，BASE=161ef07）零误捕零漏捕。

  已量化的射程限制（不是缺陷待办，是判据形状，别把它当"值级都验过了"）：
  ① a/d 只钉 flag 字面量在册与在场，不钉实参取值——值级真生效由各刀 gtest 值判据承重（命题第二句）；
  ② context 是给人读的调用点符号名，机器不解析函数边界（N193 前科：段模型栽在不配平括号上），
     同文件多调用点共用同一 flag 集合判据；
  ③ b 只核名字存在，不核该 gtest 是否走到登记的分支——note 里逐条写明值级欠账
    （cutline 成功档 / mosaic dst_srs 条件档 / reproject 栅格档与 -s_srs 条件档 / rpc dem_file 档）。

退出码（唯一消费者是 tests/check_param_effectiveness_guard.cmake）：
  0 = 自证臂 + 真仓判定都过
  1 = 真仓判定红（a/b/c/d 有 finding）
  2 = 自证臂红（判据自己不可信 ⇒ 对仓库的结论一律作废，先于真仓）
  3 = 用法/环境错误（缺参、登记表缺失/形态不对、tests/test_plugins.cpp 缺失）

复算（与 ctest 注册的那条完全同形）：
  python tests/check_param_effectiveness_guard.py --repo-root . --registry tests/param_effectiveness_registry.json
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

# 边界调用符号（与任务书的 grep 口径逐字同形）；扫描域 = 仓内 src/ 树的 C++ 源文件。
BOUNDARY_PATTERN = re.compile(r"OptionsNew|DEMProcessing|GDALWarp|apszArgs|GDALTranslate|GDALBuildVRT")
SRC_EXTS = (".cpp", ".h", ".hpp", ".cc", ".inc")

# 引号 flag 形状：双引号串整体 = - + 首字母字母 + [字母数字下划线]*。
# 陷阱规避与放宽理由见文件头"flag 扫描形状"一节。
FLAG_RE = re.compile(r'"(-[A-Za-z][A-Za-z0-9_]*)"')

# gtest 用例名：行首（允许缩进）TEST/TEST_F(套件, 名字)。行首锚定把"注释里提到 TEST 形态"
# 的误白名单压到最小；TEST_P 等其它宏不在列（登记表现读 35 条 evidence 全部是 TEST_F）。
GTEST_RE = re.compile(r"^\s*TEST(?:_F)?\s*\(\s*[A-Za-z_]\w*\s*,\s*([A-Za-z_]\w*)\s*[,)]", re.M)

REQUIRED_ENTRY_FIELDS = ("file", "context", "flag", "key", "evidence_test", "note")


def say(msg):
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()


# 逐行原文（UTF-8）由 --report 落盘；stdout 只留 ASCII 形态，口径同 check_anchor_guard.py。
REPORT = []


def load_registry(path):
    """登记表形态校验：缺文件/缺键/flag 形状不对 = 用法错误，不许当空表用。"""
    p = pathlib.Path(path)
    if not p.is_file():
        raise SystemExit("%d 登记表不存在：%s" % (EXIT_USAGE, p))
    try:
        reg = json.loads(p.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise SystemExit("%d 登记表不是合法 JSON：%s（%s）" % (EXIT_USAGE, p, exc))
    entries = reg.get("entries")
    if not isinstance(entries, list) or not entries:
        raise SystemExit("%d 登记表缺非空 entries 数组（形态不对，不许当空表用）" % EXIT_USAGE)
    for i, e in enumerate(entries):
        for k in REQUIRED_ENTRY_FIELDS:
            if k not in e:
                raise SystemExit("%d 登记表条目 %d 缺字段 %s：%s" % (EXIT_USAGE, i, k, e))
        if not FLAG_RE.fullmatch('"%s"' % e["flag"]):
            raise SystemExit("%d 登记表条目 %d 的 flag 形状不对（须 -[字母][字母数字_]*）：%s"
                             % (EXIT_USAGE, i, e["flag"]))
    return reg


def quoted_flags(text):
    """引号 flag 集合（形状见文件头）。"""
    return set(FLAG_RE.findall(text))


def test_names(test_plugins_path):
    p = pathlib.Path(test_plugins_path)
    if not p.is_file():
        raise SystemExit("%d 找不到 %s（b 判据的目标文件缺失）" % (EXIT_USAGE, p))
    return set(GTEST_RE.findall(p.read_text(encoding="utf-8", errors="replace")))


def boundary_files(root):
    """src/ 树里命中边界调用符号的文件集合（相对路径，正斜杠）。"""
    src = root / "src"
    hits = set()
    if not src.is_dir():
        return hits
    for p in sorted(src.rglob("*")):
        if p.is_file() and p.suffix in SRC_EXTS:
            try:
                text = p.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            if BOUNDARY_PATTERN.search(text):
                hits.add(p.relative_to(root).as_posix())
    return hits


def judge(root, reg):
    """真仓（或合成仓）判定。返回 (findings, stats)。

    findings 元素 = (kind, file, flag_or_name, message)，message 面向人读；kind ∈ {a,b,c,d}。
    """
    entries = reg["entries"]
    findings = []
    stats = {"boundary_files": 0, "site_files": 0, "entries": len(entries),
             "distinct_file_flags": 0, "quoted_flag_total": 0}

    site_files = sorted({e["file"] for e in entries})
    bset = boundary_files(root)
    stats["boundary_files"] = len(bset)
    stats["site_files"] = len(site_files)

    # ── c：site 文件集合 与 边界文件集合 双向相等 ─────────────────────────────
    for f in sorted(bset - set(site_files)):
        findings.append(("c", f, "*",
                         "c/new-site: src 下的边界调用文件 %s 不在登记表 site 集合里 ⇒ 新边界文件必须带登记进棘轮"
                         % f))
    for f in sorted(set(site_files) - bset):
        findings.append(("c", f, "*",
                         "c/stale-site: 登记表 site 文件 %s 在 src 下已扫不到边界调用符号 ⇒ 文件已退出边界，登记表要改"
                         % f))

    # 逐文件引号 flag 集合（site ∪ 边界；文件缺失按 a 红，不当环境错）
    text_cache = {}

    def file_text(rel):
        if rel not in text_cache:
            p = root / rel
            text_cache[rel] = p.read_text(encoding="utf-8", errors="replace") if p.is_file() else None
        return text_cache[rel]

    scanned_flags = {}
    for f in sorted(set(site_files) | bset):
        t = file_text(f)
        scanned_flags[f] = quoted_flags(t) if t is not None else None
        if scanned_flags[f] is not None:
            stats["quoted_flag_total"] += len(scanned_flags[f])

    reg_flags = {}
    for e in entries:
        reg_flags.setdefault(e["file"], set()).add(e["flag"])
    stats["distinct_file_flags"] = sum(len(v) for v in reg_flags.values())

    # ── a：登记的 flag 在 site 文件里仍找得到 ────────────────────────────────
    for e in entries:
        f = e["file"]
        if scanned_flags.get(f) is None:
            findings.append(("a", f, e["flag"],
                             "a/missing-file: 登记条目 site 文件 %s 不存在 ⇒ flag %s 无处可找（文件被删/改名）"
                             % (f, e["flag"])))
            continue
        if e["flag"] not in scanned_flags[f]:
            findings.append(("a", f, e["flag"],
                             "a/flag-gone: 登记条目的 flag %s 在 site 文件 %s 的引号 flag 集里消失 ⇒ "
                             "N163 型回归（登记还在、源码不发了）" % (e["flag"], f)))

    # ── d：site 文件引号 flag 集合 ⊆ 登记表该文件的 flag 集合 ─────────────────
    for f in site_files:
        scanned = scanned_flags.get(f)
        if scanned is None:
            continue  # 文件缺失已在 a 点名，不重复刷 c/d
        for fl in sorted(scanned - reg_flags.get(f, set())):
            findings.append(("d", f, fl,
                             "d/unregistered-flag: site 文件 %s 出现未登记的新 flag %s ⇒ 新接线必须带证据注册"
                             % (f, fl)))

    # ── b：evidence_test 名在 tests/test_plugins.cpp 里存在 ──────────────────
    names = test_names(root / "tests" / "test_plugins.cpp")
    for e in entries:
        ev = e["evidence_test"]
        if ev and ev not in names:
            findings.append(("b", e["file"], ev,
                             "b/evidence-missing: 登记条目（%s %s）的 evidence_test %s 在 tests/test_plugins.cpp "
                             "里不存在 ⇒ 证据用例被改名/删除，登记表要复审" % (e["file"], e["flag"], ev)))

    findings.sort(key=lambda x: (x[0], x[1], x[2]))
    return findings, stats


def self_test():
    """合成夹具仓上逐臂验证判据（≥4 配对臂；每臂证明的命题写在断言名里）。"""
    import tempfile
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="param_guard164_selftest_"))
    (tmp / "src").mkdir()
    (tmp / "tests").mkdir()

    def write_site(name, flags):
        # 合成 site 文件：一个边界调用点 + flag push 列表（引号形态与真仓一致）。
        pushes = "".join('    argv.push_back("%s");\n' % f for f in flags)
        (tmp / "src" / name).write_text(
            "void site() {\n"
            "    GDALDEMProcessingOptionsNew(nullptr, nullptr);\n"
            + pushes +
            "}\n", encoding="utf-8")

    def entry(file, flag, ev="TestEvidence"):
        return {"file": "src/" + file, "context": "site()", "flag": flag, "key": "internal",
                "evidence_test": ev, "note": "fixture"}

    write_site("terrain.cpp", ["-of", "-b", "-s"])
    (tmp / "src" / "plain.cpp").write_text("int plain() { return 0; }\n", encoding="utf-8")
    (tmp / "tests" / "test_plugins.cpp").write_text(
        "TEST_F(PluginTest, TestEvidence) {}\n"
        "TEST_F(PluginTest, TestOther) {}\n", encoding="utf-8")

    base_entries = [entry("terrain.cpp", "-of"), entry("terrain.cpp", "-b"),
                    entry("terrain.cpp", "-s", ev="TestEvidence")]
    base_reg = {"entries": [dict(e) for e in base_entries]}
    outs = []

    def kinds_of(reg):
        findings, _ = judge(tmp, reg)
        return findings, {f[0] for f in findings}

    # 绿臂：干净夹具必须 0 findings（判据不是恒红）。
    f0, k0 = kinds_of(base_reg)
    outs.append(("绿臂：干净夹具 0 findings（本轮 kinds=%s findings=%d）" % (sorted(k0), len(f0)),
                 not f0))

    # 臂A：登记 flag 改假名 ⇒ a 红（命题：登记在册的 flag 名实不符/从源码消失必须红）。
    reg_a = {"entries": [dict(e) for e in base_entries]}
    reg_a["entries"][2]["flag"] = "-sX"
    fa, ka = kinds_of(reg_a)
    outs.append(("臂A：登记 flag 改假名 -sX ⇒ a 红（本轮 kinds=%s）" % sorted(ka),
                 "a" in ka))

    # 臂B：删一行登记（-b）⇒ d 红（命题：登记行被删、源码 flag 沦为未登记新旗必须红）。
    reg_b = {"entries": [dict(e) for e in base_entries[:1]] + [dict(base_entries[2])]}
    fb, kb = kinds_of(reg_b)
    outs.append(("臂B：删 -b 那行登记 ⇒ d 红（本轮 kinds=%s）" % sorted(kb),
                 "d" in kb))

    # 臂C：evidence_test 改成不存在的名 ⇒ b 红（命题：登记必须链到真实存在的 gtest 证据用例名）。
    reg_c = {"entries": [dict(e) for e in base_entries]}
    reg_c["entries"][0]["evidence_test"] = "NoSuchTestName"
    fc, kc = kinds_of(reg_c)
    outs.append(("臂C：evidence_test 改成 NoSuchTestName ⇒ b 红（本轮 kinds=%s）" % sorted(kc),
                 "b" in kc))

    # 臂D：既有 site 文件里临时造新 flag ⇒ d 红（命题：新边界 flag 不登记 = 红）。
    (tmp / "src" / "terrain.cpp").write_text(
        "void site() {\n"
        "    GDALDEMProcessingOptionsNew(nullptr, nullptr);\n"
        '    argv.push_back("-of");\n'
        '    argv.push_back("-b");\n'
        '    argv.push_back("-s");\n'
        '    argv.push_back("-newflag");\n'
        "}\n", encoding="utf-8")
    fd, kd = kinds_of(base_reg)
    outs.append(("臂D：site 文件新 flag -newflag ⇒ d 红（本轮 kinds=%s）" % sorted(kd),
                 "d" in kd))
    (tmp / "src" / "terrain.cpp").write_text(
        "void site() {\n"
        "    GDALDEMProcessingOptionsNew(nullptr, nullptr);\n"
        '    argv.push_back("-of");\n'
        '    argv.push_back("-b");\n'
        '    argv.push_back("-s");\n'
        "}\n", encoding="utf-8")

    # 臂E：新造一个含边界调用符号的文件（带未登记 flag）⇒ c 红（命题：新边界文件必须带登记进棘轮）。
    (tmp / "src" / "newbound.cpp").write_text(
        "void nb() {\n"
        "    GDALWarpAppOptionsNew(nullptr, nullptr);\n"
        '    argv.push_back("-warpnew");\n'
        "}\n", encoding="utf-8")
    fe, ke = kinds_of(base_reg)
    outs.append(("臂E：新边界文件 newbound.cpp ⇒ c 红（本轮 kinds=%s）" % sorted(ke),
                 "c" in ke))
    (tmp / "src" / "newbound.cpp").unlink()

    # 臂F：登记表把条目 file 改成已不是边界的文件 ⇒ c 红（命题：棘轮双向，文件退出边界也必须改登记表）。
    reg_f = {"entries": [dict(e) for e in base_entries]}
    reg_f["entries"][0]["file"] = "src/plain.cpp"
    ff, kf = kinds_of(reg_f)
    outs.append(("臂F：登记 file 指向非边界文件 plain.cpp ⇒ c 红（本轮 kinds=%s）" % sorted(kf),
                 "c" in kf))

    for name, passed in outs:
        say("%s  %s" % ("OK  " if passed else "FAIL", name))
    return EXIT_OK if all(p for _, p in outs) else EXIT_SELFCHECK


def main():
    # stdout 一律 ASCII（cmake -P 的 message() 会按本地码页重编子进程 UTF-8 ⇒ 中文变乱码，
    # 与 anchor_guard / geotransform_guard 同一条本机限制）；原文另写 --report 指向的 UTF-8 文件。
    try:
        sys.stdout.reconfigure(encoding="ascii", errors="backslashreplace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="上游边界参数有效性棘轮守卫（param_effectiveness_guard）")
    ap.add_argument("--repo-root", default=".")
    ap.add_argument("--registry", default="tests/param_effectiveness_registry.json")
    ap.add_argument("--report", default="", help="把判定原文（UTF-8）写进这个文件")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    rc = EXIT_OK
    try:
        if a.self_test:
            return self_test()
        root = pathlib.Path(a.repo_root)
        if not root.is_dir():
            say("usage error: --repo-root not a directory: %s" % root)
            return EXIT_USAGE
        reg = load_registry(root / a.registry)
        rc_self = self_test()
        if rc_self != EXIT_OK:
            say("SELF-TEST red => judge itself untrusted, repo verdict void")
            return rc_self
        findings, stats = judge(root, reg)
        summary = ("boundary_files=%d site_files=%d entries=%d distinct_file_flags=%d "
                   "quoted_flag_total=%d findings=%d" % (
                       stats["boundary_files"], stats["site_files"], stats["entries"],
                       stats["distinct_file_flags"], stats["quoted_flag_total"], len(findings)))
        REPORT.append(summary)
        say(summary)
        for f in findings:
            REPORT.append(f[3])
            say("VIOLATION " + f[3])
        rc = EXIT_REPO if findings else EXIT_OK
        return rc
    finally:
        if a.report:
            pathlib.Path(a.report).write_text("\n".join(REPORT) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
