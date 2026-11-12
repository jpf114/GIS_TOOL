#!/usr/bin/env python3
"""Discriminating mutation arms for the track-B true-data legs.

For each arm: patch ONE product line, rebuild, assert the corresponding B-leg judgement goes RED,
restore the exact original bytes (sha256 comparison), rebuild, assert the whole B suite is GREEN.
This is the evidence that the fixtures/expectations were built to test the product, not to pass it.

The script restores product files in a finally block, so an interrupted run never leaves a
deliberately broken source tree behind.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run(cmd, cwd):
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True)
    out = (proc.stdout or b"").decode("utf-8", errors="replace") + (proc.stderr or b"").decode("utf-8", errors="replace")
    return proc.returncode, out


ARMS = [
    {
        "name": "numeric_band_math",
        "file": "src/core/src/expression_engine.cpp",
        "find": "c == '+' ? ExpressionNode::Op::Add : ExpressionNode::Op::Subtract",
        "replace": "c == '+' ? ExpressionNode::Op::Subtract : ExpressionNode::Op::Subtract",
        "case": "real_data_true_raster",
        "why": "B1+B2 becomes B1-B2; independent numpy oracle must go red",
    },
    {
        "name": "detect_cap",
        "file": "src/plugins/matching/matching_plugin.cpp",
        "find": "if (method == \"orb\")   return cv::ORB::create(maxPoints);",
        "replace": "if (method == \"orb\")   return cv::ORB::create(5000);",
        "case": "real_data_true_matching",
        "why": "max_points cap is ignored; ORB on real texture must exceed the cap of 50",
    },
    {
        "name": "change_size_guard",
        "file": "src/plugins/matching/matching_plugin.cpp",
        "regex": r"(?s)    if \(before\.size\(\) != after\.size\(\)\) \{.*?\n    \}",
        "replace": "    if (before.size() != after.size()) {\r\n        cv::resize(after, after, before.size());\r\n    }",
        "case": "real_data_true_matching",
        "why": "size mismatch is silently repaired; the negative case must go red",
    },
]


def patch(text, arm):
    if "regex" in arm:
        new, n = re.subn(arm["regex"], lambda m: arm["replace"], text, count=1)
    else:
        n = text.count(arm["find"])
        new = text.replace(arm["find"], arm["replace"], 1)
    if n != 1:
        raise RuntimeError("arm {} patch target matched {} times (expected 1)".format(arm["name"], n))
    return new


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", required=True)
    ap.add_argument("--build", default="build/release")
    ap.add_argument("--config", default="Release")
    args = ap.parse_args()
    repo = Path(args.repo)
    results = []

    for arm in ARMS:
        path = repo / arm["file"]
        original = path.read_bytes()
        text = original.decode("utf-8")
        entry = {"arm": arm["name"], "why": arm["why"], "case": arm["case"]}
        try:
            path.write_bytes(patch(text, arm).encode("utf-8"))
            rc, out = run(["cmake", "--build", args.build, "--config", args.config], repo)
            entry["build_rc"] = rc
            if rc != 0:
                raise RuntimeError("mutation build failed: " + out[-400:])
            rc, out = run(["ctest", "--test-dir", args.build, "-C", args.config,
                           "-R", "^" + arm["case"] + "$", "--output-on-failure", "-j1"], repo)
            entry["red_rc"] = rc
            entry["red"] = rc != 0
            entry["red_tail"] = out[-500:]
            if rc == 0:
                raise RuntimeError("arm {} did NOT make the judgement red".format(arm["name"]))
        except Exception as exc:
            entry["error"] = str(exc)
        finally:
            path.write_bytes(original)
            if sha(path.read_bytes()) != sha(original):
                raise RuntimeError("FATAL: restore mismatch for " + arm["file"])

        rc, out = run(["cmake", "--build", args.build, "--config", args.config], repo)
        entry["restore_build_rc"] = rc
        if rc != 0:
            entry["error"] = "restore build failed: " + out[-300:]
        rc, out = run(["ctest", "--test-dir", args.build, "-C", args.config,
                       "-R", "^real_data_true_", "--output-on-failure", "-j1"], repo)
        entry["green_rc"] = rc
        entry["green"] = rc == 0
        entry["green_tail"] = out[-300:]
        results.append(entry)
        print("ARM {} red={} green={}".format(arm["name"], entry.get("red"), entry.get("green")))

    report = repo / "build" / "tmp" / "true_data_mutation_arms.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    bad = [r["arm"] for r in results if not r.get("red") or not r.get("green") or r.get("error")]
    if bad:
        print("MUTATION_ARMS_FAIL " + ",".join(bad))
        return 1
    print("OK mutation arms: {} arms red->green".format(len(results)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
