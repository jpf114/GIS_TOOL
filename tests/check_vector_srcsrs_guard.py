#!/usr/bin/env python
# -*- coding: utf-8 -*-
r"""vector 插件 srcSRS 结构守卫 vector_srcsrs_guard（批次 D / N144，用户 2026-11-05 裁定"甲"）。

命题：src/plugins/vector/ 全目录里，Clone 出来的 OGRSpatialReference 只许用
std::unique_ptr<OGRSpatialReference, OGRSpatialReferenceReleaser> 托管（GDAL 官方 Release 语义
deleter，ogr_spatialref.h 内建类型）；裸 `OGRSpatialReference* srcSRS` 声明与 `delete srcSRS`
一律不许出现——它们是取消路径泄漏（throwIfCancelled 抛出时栈展开不释放裸指针）与
悬垂读 UB 的宿主形态。守卫是结构棘轮：改回裸指针红、登记/扫描脱钩红、
每文件 RAII 声明与 .get() 调用点计数漂移红（逼计数变化先过登记表）。

为什么射程只钉 srcSRS 这个名字：本仓 vector 插件里 OGR 家族的其他裸指针各有既定所有权口径
（OGRFeature 由 DestroyFeature/unique_ptr 双轨、OGRGeometry 普遍 unique_ptr 化但 Clone 归调用方），
一次刀同时收编所有 OGR 裸指针会把零行为变更刀变成大面积重写。本守卫按任务书只禁
srcSRS 的裸声明与裸 delete 两个精确字形；"要不要扩到全部 OGR 裸指针"另立裁定（登记表 _comment）。

判据四条（判定与登记表对撞，全部在本文件内机器化；finding 形态三种——b 的双向相等违例以 c 形态报告，b/c 是同一集合关系的两侧）：
  a. 扫描域（src/plugins/vector/ 全目录，.cpp/.h/.hpp/.cc/.inc）内裸声明字形
     `OGRSpatialReference * srcSRS`（\b 收尾，srcSRSRef 这类前缀不误捕）与 `delete srcSRS` 出现 = 红；
  b. 含 RAII 形态的文件集合 与 登记表 file 集合 双向相等
     （新文件用 RAII srcSRS 不登记 = 红；登记文件里 RAII 形态消失 = 红——脱钩两侧都红）；
  c. 登记表每条目 file 在扫描域内存在（登记指向域外/已删文件 = 红）；
  d. 每文件 RAII 声明数与 .get() 调用点数与登记值逐条相等（少声明/多声明/少调用点 = 红）。
  自证臂先于真仓判定：判据自己红 ⇒ 对仓库的结论一律不作数（与 param_effectiveness_guard 同一顺序）。

自证臂清单（合成夹具仓上跑，永不触碰真仓）：
  绿臂  干净夹具必须 0 findings——证明判据不是恒红。
  臂A   把一处声明改回裸 `OGRSpatialReference* srcSRS` ⇒ a 红——证明"改回裸指针必须红"（双向臂 a）。
  臂B   临时插回一行 `delete srcSRS;` ⇒ a 红——证明"裸 delete 必须红"。
  臂C   登记表条目的 file 改成扫描域里不存在 RAII 形态的文件 ⇒ c 红——证明"登记/扫描脱钩（stale）必须红"（双向臂 b）。
  臂D   扫描域里新造一个含 RAII srcSRS 的文件不登记 ⇒ c 红——证明"新持有点必须带登记进棘轮"（脱钩另一侧）。
  臂E   登记表把某文件 raii_declarations 改成错误计数 ⇒ d 红——证明"计数漂移必须红（逼改登记先过人）"。

扫描形状（陷阱规避）：
  - 裸声明字形钉死「OGRSpatialReference、星号、srcSRS、词边界」：`const OGRSpatialReference* srcSRSRef`
    这类前缀借用名不命中（N144 刀已把该局部改名为 layerSrsRef，验收 grep 全目录字面 0）；
    RAII 形态 `unique_ptr<OGRSpatialReference, OGRSpatialReferenceReleaser> srcSRS` 因星号不挨着
    srcSRS 而天然不命中。
  - 原文扫描不剥注释（口径同 param_effectiveness_guard 的引号 flag 扫描）：误捕只会产生假红
    （可见、当场改注释措辞），不会产生假绿——方向安全。现读（2026-11-09，BASE=67d6cc9）
    src/plugins/vector/ 注释零命中、登记表现读 20 文件/23 声明/23 调用点零漂移。
  - 已量化的射程限制：本守卫只钉「声明与 delete 的结构形态」，不证明运行期语义
    （等价性由 vector 族 175 条 ctest + 两条真实腿承重）；同名变量在扫描域之外（core/gui 等）
    的裸指针不在射程。

退出码（唯一消费者是 tests/check_vector_srcsrs_guard.cmake）：
  0 = 自证臂 + 真仓判定都过
  1 = 真仓判定红（a/c/d 有 finding）
  2 = 自证臂红（判据自己不可信 ⇒ 对仓库的结论一律作废，先于真仓）
  3 = 用法/环境错误（缺参、登记表缺失/形态不对）

复算（与 ctest 注册的那条完全同形）：
  python tests/check_vector_srcsrs_guard.py --repo-root . --registry tests/vector_srcsrs_guard_registry.json
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

# 扫描域 = vector 插件全目录（任务书口径逐字同形）；扩展名集合与 param_effectiveness_guard 一致。
SCAN_DIR = "src/plugins/vector"
SRC_EXTS = (".cpp", ".h", ".hpp", ".cc", ".inc")

# 裸声明字形：OGRSpatialReference、星号（允许空格）、srcSRS、词边界。
# \b 收尾 ⇒ srcSRSRef / srcSRSRef2 等前缀名不误捕；RAII 形态星号不挨 srcSRS，天然不命中。
BARE_DECL_RE = re.compile(r"OGRSpatialReference\s*\*\s*srcSRS\b")
# 裸 delete 字形：delete srcSRS（词边界，deleteSrcSRS 这类标识符不误捕）。
BARE_DELETE_RE = re.compile(r"\bdelete\s+srcSRS\b")
# RAII 形态（登记判据 b/d 的识别形状，与仓内统一写法逐字同构、允许空格伸缩）。
RAII_DECL_RE = re.compile(
    r"unique_ptr\s*<\s*OGRSpatialReference\s*,\s*OGRSpatialReferenceReleaser\s*>\s*srcSRS\b")
GET_CALL_RE = re.compile(r"\bsrcSRS\s*\.\s*get\s*\(\s*\)")

REQUIRED_ENTRY_FIELDS = ("file", "raii_declarations", "get_call_sites")


def say(msg):
    sys.stdout.write(msg + "\n")
    sys.stdout.flush()


# 逐行原文（UTF-8）由 --report 落盘；stdout 只留 ASCII 形态，口径同 check_param_effectiveness_guard.py。
REPORT = []


def load_registry(path):
    """登记表形态校验：缺文件/缺键/计数为负 = 用法错误，不许当空表用。"""
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
        for k in ("raii_declarations", "get_call_sites"):
            if not isinstance(e[k], int) or e[k] < 0:
                raise SystemExit("%d 登记表条目 %d 的 %s 必须是非负整数" % (EXIT_USAGE, i, k))
    return reg


def scan_files(root):
    """src/plugins/vector/ 树的 C++ 源文件集合（相对路径，正斜杠）。"""
    base = root / SCAN_DIR
    hits = set()
    if not base.is_dir():
        return hits
    for p in sorted(base.rglob("*")):
        if p.is_file() and p.suffix in SRC_EXTS:
            hits.add(p.relative_to(root).as_posix())
    return hits


def file_text(root, rel, cache):
    if rel not in cache:
        p = root / rel
        cache[rel] = p.read_text(encoding="utf-8", errors="replace") if p.is_file() else None
    return cache[rel]


def judge(root, reg):
    """真仓（或合成仓）判定。返回 (findings, stats)。

    findings 元素 = (kind, file, detail)，kind ∈ {a, b, c, d}。
    """
    entries = reg["entries"]
    findings = []
    cache = {}
    files = scan_files(root)
    stats = {"scan_files": len(files), "entries": len(entries),
             "raii_decl_total": 0, "get_call_total": 0}

    # ── a：扫描域内裸声明与裸 delete 禁止 ────────────────────────────────────
    for f in sorted(files):
        text = file_text(root, f, cache)
        for m in BARE_DECL_RE.finditer(text):
            findings.append(("a", f,
                             "a/bare-decl: %s 出现裸 OGRSpatialReference* srcSRS 声明 ⇒ 改回 "
                             "std::unique_ptr<OGRSpatialReference, OGRSpatialReferenceReleaser>（取消路径靠 RAII 兜底）" % f))
        for m in BARE_DELETE_RE.finditer(text):
            findings.append(("a", f,
                             "a/bare-delete: %s 出现手工 delete srcSRS ⇒ RAII 托管下不许手写释放" % f))

    # ── b/c：RAII 文件集合与登记表双向相等 + 登记条目必须在扫描域内 ───────────
    raii_files = set()
    for f in sorted(files):
        if RAII_DECL_RE.search(file_text(root, f, cache)):
            raii_files.add(f)
    reg_files = {e["file"] for e in entries}

    for f in sorted(raii_files - reg_files):
        findings.append(("c", f,
                         "c/new-raii-site: 扫描域文件 %s 含 RAII srcSRS 但不在登记表 ⇒ 新持有点必须带登记进棘轮" % f))
    for f in sorted(reg_files - raii_files):
        findings.append(("c", f,
                         "c/stale-site: 登记条目文件 %s 在扫描域里已扫不到 RAII srcSRS 形态 ⇒ 登记与扫描脱钩，"
                         "登记表要改（声明被拆/文件被删/写法漂移都走这里）" % f))
    for f in sorted(reg_files - files):
        findings.append(("c", f,
                         "c/out-of-scan: 登记条目文件 %s 不在扫描域 src/plugins/vector/ 内 ⇒ 登记指向域外，"
                         "登记表要改" % f))

    # ── d：每文件两计数与登记值逐条相等 ─────────────────────────────────────
    for e in entries:
        f = e["file"]
        text = file_text(root, f, cache)
        if text is None or f not in raii_files:
            continue  # 文件缺失/脱钩已在 c 点名，不重复刷 d
        decls = len(RAII_DECL_RE.findall(text))
        gets = len(GET_CALL_RE.findall(text))
        stats["raii_decl_total"] += decls
        stats["get_call_total"] += gets
        if decls != e["raii_declarations"]:
            findings.append(("d", f,
                             "d/decl-count: %s RAII 声明实数 %d != 登记 %d ⇒ 计数漂移必须改登记表先过人" %
                             (f, decls, e["raii_declarations"])))
        if gets != e["get_call_sites"]:
            findings.append(("d", f,
                             "d/get-count: %s srcSRS.get() 实数 %d != 登记 %d ⇒ 计数漂移必须改登记表先过人" %
                             (f, gets, e["get_call_sites"])))

    findings.sort(key=lambda x: (x[0], x[1]))
    return findings, stats


def self_test():
    """合成夹具仓上逐臂验证判据（1 绿 + 5 红；每臂证明的命题写在断言名里）。"""
    import atexit
    import shutil
    import tempfile
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="vector_srcsrs_guard_selftest_"))
    atexit.register(lambda: shutil.rmtree(tmp, True))
    vecdir = tmp / "src" / "plugins" / "vector"
    vecdir.mkdir(parents=True)

    RAII_SNIPPET = (
        "    std::unique_ptr<OGRSpatialReference, OGRSpatialReferenceReleaser> srcSRS(\n"
        "        srcLayer->GetSpatialRef() ? srcLayer->GetSpatialRef()->Clone() : nullptr);\n"
        "    OGRLayer* dstLayer = dstDS->CreateLayer(name, srcSRS.get(), wkbPoint, nullptr);\n")

    def write_site(name, body):
        (vecdir / name).write_text(body, encoding="utf-8")

    def entry(file, decls=1, gets=1):
        return {"file": "src/plugins/vector/" + file, "raii_declarations": decls, "get_call_sites": gets}

    write_site("clean.inc", "void f() {\n" + RAII_SNIPPET + "}\n")
    (tmp / "src" / "plugins").joinpath("vector").joinpath("plain.cpp").write_text(
        "int plain() { return 0; }\n", encoding="utf-8")

    base_reg = {"entries": [entry("clean.inc")]}
    outs = []

    def kinds_of(reg):
        findings, _ = judge(tmp, reg)
        return findings, {f[0] for f in findings}

    # 绿臂：干净夹具必须 0 findings（判据不是恒红）。
    f0, k0 = kinds_of(base_reg)
    outs.append(("绿臂：干净夹具 0 findings（本轮 kinds=%s findings=%d）" % (sorted(k0), len(f0)),
                 not f0))

    # 臂A：声明改回裸 OGRSpatialReference* srcSRS ⇒ a 红（双向臂 a：改回裸指针必须红）。
    write_site("clean.inc",
               "void f() {\n"
               "    OGRSpatialReference* srcSRS = srcLayer->GetSpatialRef()->Clone();\n"
               "    OGRLayer* dstLayer = dstDS->CreateLayer(name, srcSRS, wkbPoint, nullptr);\n"
               "}\n")
    fa, ka = kinds_of(base_reg)
    outs.append(("臂A：改回裸 srcSRS 声明 ⇒ a 红（本轮 kinds=%s）" % sorted(ka), "a" in ka))

    # 臂B：插回一行 delete srcSRS ⇒ a 红（命题：裸 delete 必须红）。
    write_site("clean.inc", "void f() {\n" + RAII_SNIPPET + "    delete srcSRS;\n}\n")
    fb, kb = kinds_of(base_reg)
    outs.append(("臂B：插回 delete srcSRS ⇒ a 红（本轮 kinds=%s）" % sorted(kb), "a" in kb))

    # 臂C：登记条目 file 指向扫描域内没有 RAII 形态的文件 ⇒ c 红（双向臂 b：登记/扫描脱钩必须红）。
    write_site("clean.inc", "void f() {\n" + RAII_SNIPPET + "}\n")
    write_site("other.inc", "void g() { }\n")
    reg_c = {"entries": [entry("other.inc")]}
    fc, kc = kinds_of(reg_c)
    outs.append(("臂C：登记指向无 RAII 形态的 other.inc ⇒ c 红（本轮 kinds=%s）" % sorted(kc), "c" in kc))

    # 臂D：扫描域新造含 RAII srcSRS 的文件不登记 ⇒ c 红（脱钩另一侧：新持有点必须带登记）。
    write_site("unregistered.inc", "void h() {\n" + RAII_SNIPPET + "}\n")
    fd, kd = kinds_of(base_reg)
    outs.append(("臂D：新 RAII 文件不登记 ⇒ c 红（本轮 kinds=%s）" % sorted(kd), "c" in kd))
    (vecdir / "unregistered.inc").unlink()

    # 臂E：登记计数与实数不符 ⇒ d 红（命题：计数漂移必须红，逼改登记先过人）。
    reg_e = {"entries": [entry("clean.inc", decls=2, gets=1)]}
    fe, ke = kinds_of(reg_e)
    outs.append(("臂E：登记 raii_declarations=2 实数 1 ⇒ d 红（本轮 kinds=%s）" % sorted(ke), "d" in ke))

    for name, passed in outs:
        say("%s  %s" % ("OK  " if passed else "FAIL", name))
    return EXIT_OK if all(p for _, p in outs) else EXIT_SELFCHECK


def main():
    # stdout 一律 ASCII（cmake -P 的 message() 会按本地码页重编子进程 UTF-8 ⇒ 中文变乱码，
    # 与 param_effectiveness_guard / anchor_guard 同一条本机限制）；原文另写 --report 指向的 UTF-8 文件。
    try:
        sys.stdout.reconfigure(encoding="ascii", errors="backslashreplace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser(description="vector srcSRS 结构守卫（vector_srcsrs_guard）")
    ap.add_argument("--repo-root", default=".")
    ap.add_argument("--registry", default="tests/vector_srcsrs_guard_registry.json")
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
        summary = ("scan_files=%d entries=%d raii_decl_total=%d get_call_total=%d findings=%d" % (
            stats["scan_files"], stats["entries"], stats["raii_decl_total"],
            stats["get_call_total"], len(findings)))
        REPORT.append(summary)
        say(summary)
        for f in findings:
            REPORT.append(f[2])
            say("VIOLATION " + f[2])
        rc = EXIT_REPO if findings else EXIT_OK
        return rc
    finally:
        if a.report:
            pathlib.Path(a.report).write_text("\n".join(REPORT) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
