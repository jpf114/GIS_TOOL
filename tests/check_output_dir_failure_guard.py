#!/usr/bin/env python
"""N162/#156 三载体 CLI 判据：output 父目录指向已存在文件时，用户可见输出必须——
  (1) rc == 1；
  (2) 不含 std/GDAL 原文（`create_directories:` / `equivalent:` / `ERROR 4:` / `ERROR 1:` /
      `Unable to create dataset` / `filesystem error` / `Cannot create a file when that file
      already exists` —— 改前三臂实测原样吐过的那些串，禁词表只增不减）；
  (3) 含本仓中文文案（`无法创建输出目录` + `是已存在的文件而非目录`）；
  (4) 含动作名（`（动作：<plugin> <action>）`）。
三件事缺一不判；(2)+(3)+(4) 的同一行判据来自 framework 前置闸（action_validation.cpp 末尾的
父目录形态闸，CLI/GUI/工作流共用）。判定本体 judge_carrier() 与运行解耦，--self-test 用合成
输出喂同一套判据：绿臂 1 + 红臂 4（std 原文混入 / 缺动作名 / 缺中文文案 / rc=0），四类缺陷
必须全被点名——判据退化先在自证臂红，不许拿"仓库当前正好没缺陷"当判据有效的证据。

复算：
  python tests/check_output_dir_failure_guard.py --cli <build/.../gis-cli.exe> --root <仓库根>
  python tests/check_output_dir_failure_guard.py --self-test
退出码：0 = 干净；1 = 有载体判据缺陷；2 = 用法或环境错误。

载体（与任务书既有取证三臂同形，output 父路径各指向一个已存在的**文件**）：
  vector convert / raster_inspect histogram / terrain slope。
栅格输入取 <root>/data/dem/refas.tif（仓库自带 DEM，只读）；缺文件按缺陷红，不静默跳过。

编码口径（与 docs_match_cli 同）：stdout 只打 ASCII（中文原文以 UTF-8 字节进 REPORT 文件）；
子进程输出按 utf-8 解、errors=replace，判据在解码后的文本上跑。
"""
import argparse
import pathlib
import subprocess
import sys
import tempfile

FORBIDDEN_SUBSTRINGS = [
    "create_directories:",
    "equivalent:",
    "ERROR 4:",
    "ERROR 1:",
    "Unable to create dataset",
    "filesystem error",
    "Cannot create a file when that file already exists",
]

REQUIRED_SUBSTRINGS = [
    "无法创建输出目录",
    "是已存在的文件而非目录",
]

# 三载体与各自的动作名（必须逐字出现在输出里：`（动作：<plugin> <action>）`）。
CARRIERS = [
    ("vector", "convert"),
    ("raster_inspect", "histogram"),
    ("terrain", "slope"),
]

RASTER_INPUT_REL = "data/dem/refas.tif"

MINI_GEOJSON = (
    '{"type":"FeatureCollection","features":['
    '{"type":"Feature","properties":{"name":"a"},'
    '"geometry":{"type":"Point","coordinates":[116.0,39.0]}}]}'
)


def action_label(plugin, action):
    return "（动作：%s %s）" % (plugin, action)


def judge_carrier(returncode, combined, plugin, action):
    """判定本体：返回缺陷清单（空 = 干净）。--self-test 与真跑共用这一份。"""
    findings = []
    if returncode != 1:
        findings.append("rc!=1 (rc=%d)" % returncode)
    for needle in FORBIDDEN_SUBSTRINGS:
        if needle in combined:
            findings.append("std/GDAL raw leaked: %r" % needle)
    for needle in REQUIRED_SUBSTRINGS:
        if needle not in combined:
            findings.append("repo Chinese text missing: %r" % needle)
    if action_label(plugin, action) not in combined:
        findings.append("action label missing: %r" % action_label(plugin, action))
    return findings


def build_args(workdir, root, plugin, action):
    blocker = workdir / "blocker.txt"
    blocker.write_bytes(b"blocker\n")  # 已存在的**文件**，output 的父路径指向它
    out = blocker / {"vector": "out.gpkg", "raster_inspect": "h.png", "terrain": "o.tif"}[plugin]
    if plugin == "vector":
        pts = workdir / "pts.geojson"
        pts.write_bytes(MINI_GEOJSON.encode("utf-8"))
        return ["vector", "convert", "--input=%s" % pts, "--output=%s" % out]
    raster = root / RASTER_INPUT_REL
    if not raster.is_file():
        return None  # 环境缺陷：判据自己红，不静默跳过
    return [plugin, action, "--input=%s" % raster, "--output=%s" % out]


def run_real(cli, root, report):
    workdir = pathlib.Path(tempfile.mkdtemp(prefix="n162_output_guard_"))
    failed = 0
    for plugin, action in CARRIERS:
        args = build_args(workdir, root, plugin, action)
        if args is None:
            report.append("carrier %s %s: raster fixture missing: %s" %
                          (plugin, action, root / RASTER_INPUT_REL))
            print("carrier %s %s: FAIL raster fixture missing" % (plugin, action))
            failed += 1
            continue
        proc = subprocess.run([cli] + args, capture_output=True)
        combined = (proc.stdout + proc.stderr).decode("utf-8", errors="replace")
        findings = judge_carrier(proc.returncode, combined, plugin, action)
        report.append("carrier %s %s rc=%d findings=%d" % (plugin, action, proc.returncode, len(findings)))
        report.append("  argv: " + " ".join(args))
        report.append("  output(head): " + combined.replace("\r", " ")[:600])
        if findings:
            failed += 1
            print("carrier %s %s: FAIL %s" % (plugin, action, "; ".join(findings)))
        else:
            print("carrier %s %s: ok" % (plugin, action))
    return failed


def run_self_test():
    good = "Error: 输出路径的父目录“C:/x/blocker.txt”是已存在的文件而非目录，无法创建输出目录（动作：vector convert）"
    cases = [
        ("green", 1, good, "vector", "convert", False),
        ("std raw leaked", 1, "Error: create_directories: Cannot create a file when that file "
         "already exists.: \"b.txt\"", "vector", "convert", True),
        ("action label missing", 1, "Error: 输出路径的父目录“b”是已存在的文件而非目录，无法创建输出目录",
         "raster_inspect", "histogram", True),
        ("chinese missing", 1, "Error: parent is not a directory（动作：terrain slope）",
         "terrain", "slope", True),
        ("rc=0", 0, good.replace("vector convert", "terrain slope"), "terrain", "slope", True),
    ]
    failed = 0
    for name, rc, combined, plugin, action, expect_findings in cases:
        findings = judge_carrier(rc, combined, plugin, action)
        if expect_findings and not findings:
            print("self-test %s: FAIL judge saw nothing" % name)
            failed += 1
        elif not expect_findings and findings:
            print("self-test %s: FAIL false positive %s" % (name, findings))
            failed += 1
        else:
            print("self-test %s: ok (%d findings)" % (name, len(findings)))
    return failed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli")
    parser.add_argument("--root")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--report")
    args = parser.parse_args()
    if args.self_test:
        sys.exit(1 if run_self_test() else 0)
    if not args.cli or not args.root:
        print("usage: --self-test, or --cli <gis-cli> --root <repo root>")
        sys.exit(2)
    report = []
    failed = run_real(args.cli, pathlib.Path(args.root), report)
    if args.report:
        pathlib.Path(args.report).write_text("\n".join(report) + "\n", encoding="utf-8")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
