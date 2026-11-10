#!/usr/bin/env python3
"""docs_prose_claims: minimal guard for stale facts in outward-facing docs.

Scope: 7 docs, stale tokens, hardcoded registration counts, RELEASE current-status
section, docs/README.md links. Parameter tables are covered by docs_match_cli.
stdout is ASCII-only so CMake cannot re-encode Chinese under the local codepage.
"""
import argparse
import re
import sys
from pathlib import Path

SCANNED = (
    "README.md", "CHANGELOG.md", "docs/交付自验.md", "docs/用户手册.md",
    "docs/README.md", "docs/v1.0.0发布说明.md", "docs/v1.0.0_GITHUB_RELEASE.md",
)
STALE_TOKENS = (
    "475 项", "697/695", "749/747", "754/752", "GOLD 77", "PENDING 49",
    "share/plugins/", "NSIS 安装脚本", "现已自动产出", "500MB", "8 位小数",
)
REGISTERED_COUNT = re.compile(r"\b7\d\d\s*/\s*7\d\d\b")
LINK = re.compile(r"\[[^\]]*\]\((\.\.?/[^)#]+)(?:#[^)]*)?\)")
STATUS_MARKER = "## 当前状态（唯一发布事实源）"


def _ascii(text):
    return text.encode("unicode_escape").decode("ascii")


def scan_text(rel, text):
    out = []
    for lineno, line in enumerate(text.splitlines(), 1):
        for token in STALE_TOKENS:
            if token in line:
                out.append("DRIFT {}:{} stale token {}".format(_ascii(rel), lineno, _ascii(token)))
        if REGISTERED_COUNT.search(line):
            out.append("DRIFT {}:{} hardcoded registration count".format(_ascii(rel), lineno))
    return out


def check_release_text(text):
    if STATUS_MARKER not in text:
        return ["DRIFT docs/RELEASE.md missing current-status section"]
    section = text.split(STATUS_MARKER, 1)[1].split("\n## ", 1)[0]
    if "ctest" not in section or "-N" not in section:
        return ["DRIFT docs/RELEASE.md current-status section lacks ctest -N readout"]
    return []


def check_links_text(text, base_dir):
    out = []
    for match in LINK.finditer(text):
        target = match.group(1).split("#", 1)[0]
        if not (base_dir / target).exists():
            out.append("DRIFT docs/README.md broken link {}".format(_ascii(target)))
    return out


def check(root):
    out = []
    for rel in SCANNED:
        path = root / rel
        if not path.is_file():
            out.append("MISSING {}".format(_ascii(rel)))
            continue
        out.extend(scan_text(rel, path.read_text(encoding="utf-8")))
    release = root / "docs/RELEASE.md"
    if not release.is_file():
        out.append("MISSING docs/RELEASE.md")
    else:
        out.extend(check_release_text(release.read_text(encoding="utf-8")))
    readme = root / "docs/README.md"
    if readme.is_file():
        out.extend(check_links_text(readme.read_text(encoding="utf-8"), readme.parent))
    return out


def self_test():
    base = Path(__file__).resolve().parent
    cases = (
        ("stale token", scan_text("README.md", "ctest 697/695 passed"), True),
        ("stale token 2", scan_text("CHANGELOG.md", "475 项 CTest"), True),
        ("stale path", scan_text("docs/用户手册.md", "share/plugins/"), True),
        ("registration count", scan_text("README.md", "Debug 749 / 747 baseline"), True),
        ("clean text", scan_text("README.md", "see RELEASE current status"), False),
        ("missing status", check_release_text("## 旧章节\n无现读命令"), True),
        ("status without command", check_release_text(STATUS_MARKER + "\n只是文字"), True),
        ("status with command", check_release_text(STATUS_MARKER + "\nctest -C Debug -N\n\n## 下一节"), False),
        ("broken link", check_links_text("[x](./not-exist.md)", base), True),
        ("valid link", check_links_text("[x](./check_docs_prose_claims.py)", base), False),
    )
    failed = ["SELFTEST {} expected_red={} got={}".format(name, want, bool(got))
              for name, got, want in cases if bool(got) != want]
    for line in failed:
        print(line)
    if failed:
        return 1
    print("OK docs_prose_claims self-test: 10 arms")
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
    for line in findings:
        print(line)
    if findings:
        return 1
    print("OK docs_prose_claims: 0 drift")
    return 0


if __name__ == "__main__":
    sys.exit(main())
