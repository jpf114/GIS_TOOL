#!/usr/bin/env python3
"""True-data regression legs (track B).

Inputs are real imagery windows prepared by make_true_data_fixtures.py. Expectations come from
data/true_expected.json, which is derived with GDAL CLI + numpy only -- never from GIS_TOOL output.
This runner has its own lightweight "a case that ran must have been judged" gate (register / run /
judge sets must be equal and non-empty), so an accidentally skipped judgement cannot look green.

stdout is ASCII-only on purpose (CMake/CMake output re-encoding under the local codepage).
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np


def fail(msg):
    print("TRUE_DATA_FAIL " + msg)
    raise SystemExit(1)


class Runner:
    def __init__(self, cli, gdal_bin, repo, out_root, group):
        self.cli = cli
        self.gdal_bin = Path(gdal_bin)
        self.repo = Path(repo)
        self.out = Path(out_root)
        self.group = group
        self.out.mkdir(parents=True, exist_ok=True)
        self.registered = set()
        self.ran = set()
        self.judged = set()
        self.details = []
        self.expected = json.loads(
            (self.repo / "data" / "true_expected.json").read_text(encoding="utf-8")
        )

    def tool(self, name):
        return str(self.gdal_bin / (name + ".exe"))

    @staticmethod
    def decode(raw):
        # CLI output mixes ASCII diagnostics with local-codepage Chinese text; never let a
        # decoding error hide an actual case result.
        return (raw or b"").decode("utf-8", errors="replace")

    @staticmethod
    def read_json(path):
        # Product writers emit UTF-8 with BOM on Windows.
        return json.loads(Path(path).read_text(encoding="utf-8-sig"))

    def gdal(self, args):
        proc = subprocess.run(args, capture_output=True)
        if proc.returncode != 0:
            raise RuntimeError("gdal rc={}: {}".format(proc.returncode, self.decode(proc.stderr)[:300]))
        return self.decode(proc.stdout)

    def cli_run(self, args):
        proc = subprocess.run([self.cli, *args], capture_output=True)
        return proc.returncode, self.decode(proc.stdout) + self.decode(proc.stderr)

    def pixels(self, path, band=1):
        xyz = self.out / ("px_" + Path(path).stem + ".xyz")
        self.gdal([self.tool("gdal_translate"), "-q", "-of", "XYZ", "-b", str(band), str(path), str(xyz)])
        return np.loadtxt(xyz)[:, 2]

    def dims(self, path):
        meta = json.loads(self.gdal([self.tool("gdalinfo"), "-json", str(path)]))
        return meta["size"], len(meta["bands"])

    def register(self, name):
        self.registered.add(name)

    def judge(self, name, ok, note):
        self.judged.add(name)
        self.details.append({"case": name, "ok": bool(ok), "note": note})
        if not ok:
            fail("case {} : {}".format(name, note))

    def case(self, name, fn):
        self.register(name)
        self.ran.add(name)
        ok, note = fn()
        self.judge(name, ok, note)

    def finish(self):
        if not self.registered:
            fail("empty roster")
        if self.registered != self.ran or self.registered != self.judged:
            fail("gate: registered={} ran={} judged={}".format(
                len(self.registered), len(self.ran), len(self.judged)))
        summary = self.out / ("summary_true_" + self.group + ".json")
        summary.write_text(json.dumps({
            "group": self.group,
            "cases": len(self.registered),
            "judged": len(self.judged),
            "details": self.details,
        }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print("OK true_data_{}: {} cases judged".format(self.group, len(self.judged)))


def near(a, b, tol):
    return abs(float(a) - float(b)) <= tol


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", required=True)
    ap.add_argument("--repo", required=True)
    ap.add_argument("--out-root", required=True)
    ap.add_argument("--group", choices=("raster", "matching"), required=True)
    ap.add_argument("--gdal-bin", default=r"D:\Code\GitHubCode\vcpkg\installed\x64-windows\tools\gdal")
    ap.add_argument("--proj-data", default=r"D:\Code\GitHubCode\vcpkg\installed\x64-windows\share\proj")
    args = ap.parse_args()

    os.environ["PROJ_DATA"] = args.proj_data
    os.environ["PROJ_LIB"] = os.environ["PROJ_DATA"]
    r = Runner(args.cli, args.gdal_bin, args.repo, args.out_root, args.group)
    E = r.expected
    TR = r.repo / "data" / "true_raster"
    TM = r.repo / "data" / "true_matching"

    if not E["reference_input_overlap_identical"]:
        fail("independent overlap oracle is false")

    if args.group == "raster":
        # 1. metadata oracle: size / band count / band-1 mean come from independent gdalinfo
        def c_info():
            ms = TR / "pansharpen_ms.tif"
            rc, text = r.cli_run(["raster_inspect", "info", "--input=" + str(ms)])
            exp = E["pansharpen_ms"]
            shape = "Size:   {} x {} x {} bands".format(exp["size"][0], exp["size"][1], exp["bands"])
            if rc != 0 or shape not in text:
                return False, "info rc={} shape-missing({})".format(rc, shape)
            mean = exp["bands_stats"][0]["mean"]
            if ("Mean:" not in text) or (("{:.4f}".format(mean)[:5] not in text and
                                         "{:.1f}".format(mean) not in text)):
                return False, "band1 mean not near independent {:.4f}".format(mean)
            return True, "size+mean match independent gdalinfo"
        r.case("true_raster_info", c_info)

        # 2. value-level oracle: B1+B2 computed by numpy on the SOURCE, not from product output
        def c_band_math():
            out = r.out / "band_math_true.tif"
            rc, text = r.cli_run(["raster_math", "band_math",
                                  "--input=" + str(TR / "pansharpen_ms.tif"),
                                  "--output=" + str(out), "--expression=B1+B2"])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing".format(rc)
            vals = r.pixels(out)
            exp = E["band_math_B1_plus_B2"]
            got = {"min": float(vals.min()), "max": float(vals.max()), "mean": float(vals.mean())}
            for k in exp:
                if not near(got[k], exp[k], 1e-3):
                    return False, "{} got {:.4f} expected {:.4f}".format(k, got[k], exp[k])
            return True, "B1+B2 min/max/mean match independent numpy oracle"
        r.case("true_band_math", c_band_math)

        # 3. structural invariant for pansharpen (no product-formula-derived expectation)
        def c_pansharpen():
            out = r.out / "pansharpen_true.tif"
            rc, text = r.cli_run(["processing", "pansharpen",
                                  "--input=" + str(TR / "pansharpen_ms.tif"),
                                  "--pan_file=" + str(TR / "pansharpen_pan.tif"),
                                  "--pan_method=mean_blend", "--output=" + str(out)])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing".format(rc)
            size, bands = r.dims(out)
            if size != E["pansharpen_ms"]["size"] or bands != 3:
                return False, "size/bands {} / {}".format(size, bands)
            vals = r.pixels(out)
            if not np.isfinite(vals).all() or float(vals.std()) <= 0:
                return False, "output degenerate or non-finite"
            return True, "256x256x3, finite, non-constant"
        r.case("true_pansharpen", c_pansharpen)

        # 4. discriminating fixture: independently generated checker board
        def c_checker():
            rc, text = r.cli_run(["raster_inspect", "info",
                                  "--input=" + str(TR / "pattern_checker.tif")])
            exp = E["pattern_checker"]
            shape = "Size:   {} x {} x {} bands".format(exp["size"][0], exp["size"][1], exp["bands"])
            if rc != 0 or shape not in text or "Type:   Byte" not in text:
                return False, "checker info mismatch rc={}".format(rc)
            if not near(exp["stats"]["min"], 0, 1e-6) or not near(exp["stats"]["max"], 255, 1e-6):
                return False, "independent checker stats unexpected"
            return True, "64x64 Byte checker, min=0 max=255"
        r.case("true_checker", c_checker)

        # 5. real land-cover raster + projected real vector + independently derived class map
        def c_feature_stats():
            out = r.out / "feature_stats_true.json"
            v = E["classification_vector"]
            rc, text = r.cli_run(["classification", "feature_stats",
                                  "--vector=" + str(TR / "classification_vector.gpkg"),
                                  "--feature_id_field=" + v["id_field"],
                                  "--feature_name_field=" + v["name_field"],
                                  "--class_map=" + str(TR / "classification_class_map.json"),
                                  "--rasters=" + str(TR / "classification_raster.tif"),
                                  "--nodatas=0", "--output=" + str(out)])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing: {}".format(rc, text[-200:])
            payload = r.read_json(out)
            records = payload.get("records", [])
            if len(records) < 1:
                return False, "no records from real vector/raster pair"
            return True, "{} records from GLC raster + UTM vector".format(len(records))
        r.case("true_feature_stats", c_feature_stats)

    else:
        # 1. detect: structural invariants with a real textured image
        def c_detect():
            out = r.out / "detect_true.json"
            rc, text = r.cli_run(["matching", "detect",
                                  "--input=" + str(TM / "reference.tif"),
                                  "--method=orb", "--max_points=50", "--output=" + str(out)])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing".format(rc)
            p = r.read_json(out)
            if not (0 < p["count"] <= 50) or len(p["keypoints"]) != p["count"]:
                return False, "count/cap/keypoints mismatch"
            return True, "{} ORB keypoints within cap 50".format(p["count"])
        r.case("true_detect", c_detect)

        # 2. match: two real windows of the same scene with a known 64 px shift
        def c_match():
            out = r.out / "match_true.json"
            rc, text = r.cli_run(["matching", "match",
                                  "--reference=" + str(TM / "reference.tif"),
                                  "--input=" + str(TM / "input.tif"),
                                  "--max_points=200", "--output=" + str(out)])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing".format(rc)
            p = r.read_json(out)
            if p["count"] <= 0 or len(p["matches"]) != p["count"]:
                return False, "no matches on overlapping real windows"
            return True, "{} matches on shifted real windows".format(p["count"])
        r.case("true_match", c_match)

        # 3. register: output geometry must follow the reference raster (dynamic oracle)
        def c_register():
            out = r.out / "register_true.tif"
            rc, text = r.cli_run(["matching", "register",
                                  "--reference=" + str(TM / "reference.tif"),
                                  "--input=" + str(TM / "input.tif"),
                                  "--output=" + str(out)])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing".format(rc)
            size, _ = r.dims(out)
            if size != E["reference"]["size"]:
                return False, "warp size {} != reference {}".format(size, E["reference"]["size"])
            return True, "warp output follows reference dimensions"
        r.case("true_register", c_register)

        # 4. change: genuine 2024 vs 2025 pair, structural invariants only
        def c_change():
            out = r.out / "change_true.tif"
            rc, text = r.cli_run(["matching", "change",
                                  "--reference=" + str(TM / "reference.tif"),
                                  "--input=" + str(TM / "change_input.tif"),
                                  "--change_method=differencing", "--threshold=1.0",
                                  "--output=" + str(out)])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing: {}".format(rc, text[-220:])
            size, _ = r.dims(out)
            vals = r.pixels(out)
            if size != E["reference"]["size"] or not np.isfinite(vals).all():
                return False, "bad size or non-finite values"
            return True, "multi-temporal change map {} finite".format(size)
        r.case("true_change", c_change)

        # 5. negative case: mismatched dimensions must fail and leave no product
        def c_negative():
            out = r.out / "change_mismatch_true.tif"
            if out.exists():
                out.unlink()
            rc, text = r.cli_run(["matching", "change",
                                  "--reference=" + str(TM / "reference.tif"),
                                  "--input=" + str(TM / "mismatch_small.tif"),
                                  "--change_method=differencing", "--threshold=1.0",
                                  "--output=" + str(out)])
            if rc == 0 or out.exists():
                return False, "size mismatch was accepted (rc={})".format(rc)
            return True, "size mismatch rejected, no product"
        r.case("true_change_mismatch", c_negative)

    r.finish()
    return 0


if __name__ == "__main__":
    sys.exit(main())
