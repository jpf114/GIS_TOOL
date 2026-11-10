#!/usr/bin/env python3
"""docs_prose_claims: 对外文档关键事实的最小机器判据（不依赖第三方库）。

射程：只核 7 份对外文档里的过期事实 token、写死的注册数、RELEASE 当前状态节、
以及 docs/README.md 的相对链接。参数表由 check_docs_match_cli.py 负责，本判据不碰。
stdout 只用 ASCII，避免 CMake 捕获后按本地码页二次编码。
"""

import argparse
import re
import sys
from pathlib import Path

SCANNED = (
    "README.md",
    "CHANGELOG.md",
    "docs/交付自验.md",
    "docs/用户手册.md",
    "docs/README.md",
    "docs/v1.0.0发布说明.md",
    "docs/v1.0.0_GITHUB_RELEASE.md",
)

STALE_TOKENS = (
    "475 项",
    "697/695",
    "749/747",
    "754/752",
    "GOLD 77",
    "PENDING 49",
    "share/plugins/",
    "NSIS 安装脚本",
    "现已自动产出",
    "500MB",
    "8 位小数",
)

REGISTERED_COUNT = re.compile(r"\b[67]\d\d\s*/\s*[67]\d\d\b")
LINK = re.compile(r"\[[^\]]*\]\((\.\.?/[^)#]+)(?:#[^)]*)?\)")
STATUS_MARKER = "## 当前状态（唯一发布事实源）"


def scan_text(rel, text):
    """返回该文档中的过期事实行，ASCII 输出。"""
    findings = []
    for lineno, line in enumerate(text.splitlines(), 1):
        for token in STALE_TOKENS:
            if token in line:
                findings.append("DRIFT {}:{} stale token {}".format(rel, lineno, token.encode("unicode_escape").decode("ascii")))
        if REGISTERED_COUNT.search(line):
            findings.append("DRIFT {}:{} hardcoded registration count".format(rel, lineno))
    return findings


def check_release_text(text):
    """当前状态节必须存在且含现读命令；找不到就拒绝放行。"""
    if STATUS_MARKER not in text:
        return ["DRIFT docs/RELEASE.md missing current-status section"]
    section = text.split(STATUS_MARKER, 1)[1].split("\n## ", 1)[0]
    if "ctest" not in section or "-N" not in section:
        return ["DRIFT docs/RELEASE.md current-status section lacks ctest -N readout"]
    return []


def check_links_text(text, base_dir):
    findings = []
    for match in LINK.finditer(text):
        target = match.group(1).split("#", 1)[0]
        if not (base_dir / target).exists():
            findings.append("DRIFT docs/README.md broken link {}".format(target.encode("unicode_escape").decode("ascii")))
    return findings


def check(root):
    findings = []
    for rel in SCANNED:
        path = root / rel
        if not path.is_file():
            findings.append("MISSING {}".format(rel.encode("unicode_escape").decode("ascii")))
            continue
        findings.extend(scan_text(rel, path.read_text(encoding="utf-8")))

    release = root / "docs/RELEASE.md"
    if not release.is_file():
        findings.append("MISSING docs/RELEASE.md")
    else:
        findings.extend(check_release_text(release.read_text(encoding="utf-8")))

    readme = root / "docs/README.md"
    if readme.is_file():
        findings.extend(check_links_text(readme.read_text(encoding="utf-8"), readme.parent))
    return findings


def self_test():
    """判据必须能变红，否则不值得相信它是绿的。"""
    cases = (
        ("stale token", scan_text("README.md", "ctest 697/695 passed"), True),
        ("stale token 2", scan_text("CHANGELOG.md", "475 项 CTest"), True),
        ("stale path", scan_text("docs/用户手册.md", "share/plugins/"), True),
        ("registration count", scan_text("README.md", "Debug 749 / 747 baseline"), True),
        ("registration count compact", scan_text("README.md", "ctest 697/697 passed"), True),
        ("clean text", scan_text("README.md", "see RELEASE current status"), False),
        ("missing status", check_release_text("## 旧章节\n无现读命令"), True),
        ("status without command", check_release_text(STATUS_MARKER + "\n只是文字"), True),
        ("status with command", check_release_text(STATUS_MARKER + "\nctest -C Debug -N\n\n## 下一节"), False),
    )
    failed = []
    for name, result, should_be_red in cases:
        is_red = bool(result)
        if is_red != should_be_red:
            failed.append("SELFTEST {} expected_red={} got={}".format(name, should_be_red, is_red))
    if failed:
        for line in failed:
            print(line)
        return 1
    print("OK docs_prose_claims self-test: 9 arms")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.root:
        print("ERROR --root is required")
        return 2
    findings = check(Path(args.root))
    if findings:
        for line in findings:
            print(line)
        return 1
    print("OK docs_prose_claims: 0 drift")
    return 0


if __name__ == "__main__":
    sys.exit(main())
