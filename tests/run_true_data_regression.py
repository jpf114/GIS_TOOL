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

        # ---- terrain batch (C11): the 9 matrix rows still marked not-consumed -----
        # Real DEM windows, not the A-leg synthetic DEM. Expectations are independent:
        # metadata from gdalinfo, fill_sinks monotonicity from the SOURCE pixels,
        # analytic slope from the plane's geometry (numpy), never from product output.
        def terrain_case(action, extra, kind):
            out = r.out / ("terrain_" + action + "_true.tif")
            args = ["terrain", action,
                    "--input=" + str(TR / "dem_real.tif"),
                    "--output=" + str(out), "--band=1", *extra]
            rc, text = r.cli_run(args)
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing: {}".format(rc, text[-180:])
            size, _ = r.dims(out)
            if size != E["dem_real"]["size"]:
                return False, "size {} != real DEM {}".format(size, E["dem_real"]["size"])
            vals = r.pixels(out)
            if not np.isfinite(vals).all() or float(vals.std()) <= 0:
                return False, "output non-finite or constant on real DEM"
            if kind == "fill":
                src = r.pixels(TR / "dem_real.tif")
                if not bool((vals >= src).all()):
                    return False, "filled DEM is below the source somewhere"
            if kind == "cutfill":
                if not (bool((vals > 0).any()) and bool((vals < 0).any())):
                    return False, "cut_fill on two distinct real windows has no cut and no fill"
            return True, "real DEM -> finite non-constant {} output".format(action)

        cx, cy = E["terrain_center"]
        water = E["terrain_water_level"]
        terrain_jobs = [
            ("true_terrain_fill_sinks", "fill_sinks", ["--z_factor=1"], "fill"),
            ("true_terrain_flow_direction", "flow_direction", ["--z_factor=1"], ""),
            ("true_terrain_flow_accumulation", "flow_accumulation", ["--z_factor=1"], ""),
            ("true_terrain_stream_extract", "stream_extract",
             ["--z_factor=1", "--accum_threshold=10"], ""),
            ("true_terrain_watershed", "watershed", ["--z_factor=1"], ""),
            ("true_terrain_viewshed", "viewshed",
             ["--observer_x={:.6f}".format(cx), "--observer_y={:.6f}".format(cy),
              "--observer_height=2.0", "--target_height=0.0", "--max_distance=0.0"], ""),
            ("true_terrain_viewshed_multi", "viewshed_multi",
             ["--observer_points={:.6f},{:.6f};{:.6f},{:.6f}".format(
                 cx, cy, cx + 200.0, cy + 200.0),
              "--observer_height=2.0", "--target_height=0.0", "--max_distance=0.0"], ""),
            ("true_terrain_cut_fill", "cut_fill",
             ["--reference=" + str(TR / "dem_reference.tif")], "cutfill"),
            ("true_terrain_reservoir_volume", "reservoir_volume",
             ["--water_level={:.4f}".format(water)], ""),
        ]
        for cname, action, extra, kind in terrain_jobs:
            r.case(cname, (lambda a=action, e=extra, k=kind: terrain_case(a, e, k)))

        # strong value-level oracle: independent analytic slope on a generated plane
        def c_plane_slope():
            out = r.out / "terrain_slope_plane_true.tif"
            rc, text = r.cli_run(["terrain", "slope",
                                  "--input=" + str(TR / "dem_plane.tif"),
                                  "--output=" + str(out), "--band=1", "--z_factor=1"])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing".format(rc)
            vals = r.pixels(out).reshape(64, 64)
            inner = float(vals[2:-2, 2:-2].mean())
            exp = E["dem_plane"]["slope_degrees"]
            if not near(inner, exp, 0.5):
                return False, "slope got {:.4f} deg vs analytic {:.4f} deg".format(inner, exp)
            return True, "analytic plane slope {:.4f} deg matches independent geometry".format(exp)
        r.case("true_terrain_slope_analytic", c_plane_slope)

        # ---- spindex batch (C8): 17 actions on an INDEPENDENT 6-band stack ---------
        # Real imagery has no NIR/SWIR (band 4 is constant Alpha=255), so this batch uses a
        # deliberately generated 6-band discriminating fixture: fixed ramps + seeded checker,
        # NOT derived from any index formula. Every expectation is an independent numpy
        # evaluation of the public index formula, so changing a product formula must go red.
        def index_case(action):
            name = "spindex_" + action
            out = r.out / (name + "_true.tif")
            rc, text = r.cli_run(["spindex", action,
                                  "--input=" + str(TR / "spindex_stack.tif"),
                                  "--output=" + str(out),
                                  *E["spindex_params"][action]])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing: {}".format(rc, text[-180:])
            vals = r.pixels(out)
            if vals.size != 64 * 64 or not np.isfinite(vals).all():
                return False, "bad size or non-finite values"
            arr = vals.reshape(64, 64)
            exp = E["spindex_oracle"][action]
            got = {"min": float(arr.min()), "max": float(arr.max()),
                   "mean": float(arr.mean()), "std": float(arr.std())}
            for k in ("min", "max", "mean", "std"):
                if not near(got[k], exp[k], 2e-4):
                    return False, "{} got {:.6f} expected {:.6f}".format(k, got[k], exp[k])
            for i, xy in enumerate(E["spindex_sample_xy"]):
                x, y = int(xy[0]), int(xy[1])
                if not near(float(arr[y, x]), exp["samples"][i], 2e-4):
                    return False, "sample {} got {:.6f} expected {:.6f}".format(
                        i, float(arr[y, x]), exp["samples"][i])
            return True, "{} matches independent formula oracle".format(action)

        for action_name in E["spindex_params"]:
            r.case("true_spindex_" + action_name,
                   (lambda a=action_name: index_case(a)))

        # negative case: custom_index without expression/preset must be rejected
        def c_index_negative():
            out = r.out / "spindex_custom_index_none_true.tif"
            if out.exists():
                out.unlink()
            rc, text = r.cli_run(["spindex", "custom_index",
                                  "--input=" + str(TR / "spindex_stack.tif"),
                                  "--output=" + str(out), "--preset=none"])
            if rc == 0 or out.exists():
                return False, "missing expression/preset was accepted (rc={})".format(rc)
            return True, "custom_index with no expression rejected"
        r.case("true_spindex_custom_index_negative", c_index_negative)

        # ---- classification batch (C9): separable 2-class fixture ------------------
        # Fixtures are defined by geometry (left/right half, labels 1/2), not by any
        # classifier output. Three algorithms must all separate the halves; accuracy is
        # checked against independently computed 1.0 / 0.75, so the test is not a constant pass.
        def classify_case(action):
            out = r.out / ("classification_" + action + "_true.tif")
            rc, text = r.cli_run([
                "classification", action,
                "--input=" + str(TR / "classification_input.tif"),
                "--training_csv=" + str(TR / "classification_training.csv"),
                "--output=" + str(out), "--bands=1,2"])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing: {}".format(rc, text[-180:])
            vals = r.pixels(out).reshape(cn, cn)
            if len(np.unique(vals)) != 2:
                return False, "expected exactly 2 classes, got {}".format(len(np.unique(vals)))
            half = E["classification_half"]
            left = set(np.unique(vals[:, :half]))
            right = set(np.unique(vals[:, half:]))
            if len(left) != 1 or len(right) != 1 or left == right:
                return False, "left/right halves not separated ({} vs {})".format(left, right)
            return True, "{} separates the two independent classes".format(action)

        cn = E["classification_size"][0]
        for cls_action in ("svm_classify", "random_forest_classify", "max_likelihood_classify"):
            r.case("true_classification_" + cls_action,
                   (lambda a=cls_action: classify_case(a)))

        def accuracy_case(name, classified_file, expected_acc):
            out = r.out / ("classification_" + name + "_true.json")
            rc, text = r.cli_run([
                "classification", "accuracy_assessment",
                "--classified_raster=" + str(TR / classified_file),
                "--reference_raster=" + str(TR / "classification_reference.tif"),
                "--output=" + str(out)])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing: {}".format(rc, text[-180:])
            payload = r.read_json(out)
            got = float(payload["overall_accuracy"])
            if not near(got, expected_acc, 1e-6):
                return False, "overall_accuracy got {} expected {}".format(got, expected_acc)
            return True, "overall_accuracy {} matches independent pixel count".format(got)

        r.case("true_classification_accuracy_perfect",
               lambda: accuracy_case("accuracy_perfect", "classification_perfect.tif",
                                     E["classification_perfect_accuracy"]))
        r.case("true_classification_accuracy_degraded",
               lambda: accuracy_case("accuracy_degraded", "classification_degraded.tif",
                                     E["classification_degraded_accuracy"]))

        def c_cls_negative():
            out = r.out / "classification_missing_label_true.tif"
            if out.exists():
                out.unlink()
            rc, text = r.cli_run([
                "classification", "svm_classify",
                "--input=" + str(TR / "classification_input.tif"),
                "--training_csv=" + str(TR / "classification_training.csv"),
                "--output=" + str(out), "--bands=1,2", "--label_column=missing"])
            if rc == 0 or out.exists():
                return False, "missing label column was accepted (rc={})".format(rc)
            return True, "missing label column rejected, no product"
        r.case("true_classification_missing_label", c_cls_negative)

        # ---- single-action batch (C4/C5/C7/C14) --------------------------------
        sn = E["classification_size"][0]
        # Each judgement uses an independently derived expectation: GDAL metadata for NoData,
        # geometric separability for K-Means, |after-before| for change, and the two plate
        # ramps plus their assigned bounds for mosaic.
        def c_nodata():
            work = r.out / "nodata_work.tif"
            r.gdal([r.tool("gdal_translate"), "-q", "-of", "GTiff",
                    str(TR / "pattern_checker.tif"), str(work)])
            rc, text = r.cli_run(["raster_manage", "nodata",
                                  "--input=" + str(work), "--band=1",
                                  "--nodata_value=255"])
            if rc != 0:
                return False, "rc={}: {}".format(rc, text[-180:])
            meta = json.loads(r.gdal([r.tool("gdalinfo"), "-json", str(work)]))
            band = meta["bands"][0]
            got = band.get("noDataValue", band.get("nodata_value"))
            if got is None or not near(float(got), 255.0, 1e-6):
                return False, "independent gdalinfo NoData got {}".format(got)
            return True, "NoData 255 confirmed by independent gdalinfo"
        r.case("true_raster_manage_nodata", c_nodata)

        def c_kmeans():
            out = r.out / "kmeans_true.tif"
            rc, text = r.cli_run(["processing", "kmeans",
                                  "--input=" + str(TR / "classification_input.tif"),
                                  "--output=" + str(out), "--kmeans_k=2"])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing: {}".format(rc, text[-180:])
            arr = r.pixels(out).reshape(cn, cn)
            if len(np.unique(arr)) != 2:
                return False, "kmeans_k=2 did not produce exactly 2 labels"
            half = E["classification_half"]
            left, right = set(np.unique(arr[:, :half])), set(np.unique(arr[:, half:]))
            if len(left) != 1 or len(right) != 1 or left == right:
                return False, "clusters do not follow the independent two-plate geometry"
            return True, "2 clusters separate the independent left/right plates"
        r.case("true_processing_kmeans", c_kmeans)

        def c_change_numeric():
            out = r.out / "change_numeric_true.tif"
            rc, text = r.cli_run(["matching", "change",
                                  "--reference=" + str(TR / "change_before.tif"),
                                  "--input=" + str(TR / "change_after.tif"),
                                  "--change_method=differencing", "--threshold=10",
                                  "--output=" + str(out)])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing: {}".format(rc, text[-180:])
            arr = r.pixels(out).reshape(sn, sn)
            exp = E["change_oracle"]
            got = {"min": float(arr.min()), "max": float(arr.max()), "mean": float(arr.mean())}
            for k in ("min", "max", "mean"):
                if not near(got[k], exp[k], 1e-3):
                    return False, "{} got {:.6f} expected {:.6f}".format(k, got[k], exp[k])
            for i, xy in enumerate(E["spindex_sample_xy"]):
                x, y = int(xy[0]), int(xy[1])
                if not near(float(arr[y, x]), exp["samples"][i], 1e-3):
                    return False, "sample {} got {:.6f} expected {:.6f}".format(
                        i, float(arr[y, x]), exp["samples"][i])
            y0, y1, x0, x1 = E["change_rect"]
            if not near(float(arr[y0:y1, x0:x1].mean()), E["change_delta"], 1e-3):
                return False, "changed rectangle mean != independent delta"
            return True, "change map equals independent |after-before|"
        r.case("true_matching_change_numeric", c_change_numeric)

        def c_mosaic():
            out = r.out / "mosaic_true.tif"
            rc, text = r.cli_run(["cutting", "mosaic",
                                  "--input=" + str(TR / "mosaic_left.tif") + "," +
                                  str(TR / "mosaic_right.tif"),
                                  "--output=" + str(out)])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing: {}".format(rc, text[-180:])
            size, _ = r.dims(out)
            if size != E["mosaic_expected_size"]:
                return False, "mosaic size {} != {}".format(size, E["mosaic_expected_size"])
            arr = r.pixels(out).reshape(sn, 2 * sn)
            left = r.pixels(TR / "mosaic_left.tif").reshape(sn, sn)
            right = r.pixels(TR / "mosaic_right.tif").reshape(sn, sn)
            if not np.allclose(arr[:, :sn], left, atol=1e-3) or \
                    not np.allclose(arr[:, sn:], right, atol=1e-3):
                return False, "mosaic halves do not equal the independent plates"
            return True, "128x64 mosaic reproduces both independent plates pixel-wise"
        r.case("true_cutting_mosaic", c_mosaic)

        def c_nodata_negative():
            work = r.out / "nodata_bad_work.tif"
            r.gdal([r.tool("gdal_translate"), "-q", "-of", "GTiff",
                    str(TR / "pattern_checker.tif"), str(work)])
            rc, text = r.cli_run(["raster_manage", "nodata",
                                  "--input=" + str(work), "--band=1",
                                  "--nodata_value=1e20"])
            if rc == 0:
                return False, "out-of-range nodata_value was accepted"
            return True, "out-of-range nodata_value rejected by param guard"
        r.case("true_raster_manage_nodata_negative", c_nodata_negative)

        # ---- georef batch (C13): 6 numeric corrections + GCP/RPC metadata paths ----
        # All expectations come from numpy evaluation of the published correction formulas
        # and from independently authored GCP coordinates / RPC metadata, never from a run.
        def georef_case(action, extra, oracle_key):
            out = r.out / ("georef_" + action + "_true.tif")
            rc, text = r.cli_run(["georef", action,
                                  "--input=" + str(TR / "georef_input.tif"),
                                  "--output=" + str(out), *extra])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing: {}".format(rc, text[-180:])
            vals = r.pixels(out)
            if vals.size != sn * sn or not np.isfinite(vals).all():
                return False, "bad size or non-finite values"
            arr = vals.reshape(sn, sn)
            exp = E["georef_oracle"][oracle_key]
            got = {"min": float(arr.min()), "max": float(arr.max()), "mean": float(arr.mean())}
            for k in ("min", "max", "mean"):
                if not near(got[k], exp[k], 2e-2):
                    return False, "{} got {:.6f} expected {:.6f}".format(k, got[k], exp[k])
            for i, xy in enumerate(E["spindex_sample_xy"]):
                x, y = int(xy[0]), int(xy[1])
                if not near(float(arr[y, x]), exp["samples"][i], 2e-2):
                    return False, "sample {} got {:.6f} expected {:.6f}".format(
                        i, float(arr[y, x]), exp["samples"][i])
            return True, "{} matches independent correction oracle".format(action)

        topo = ["--slope_raster=" + str(TR / "georef_slope.tif"),
                "--aspect_raster=" + str(TR / "georef_aspect.tif"),
                "--sun_zenith_deg=30", "--sun_azimuth_deg=180", "--band=1"]
        georef_jobs = [
            ("dos_correction", ["--band=1"], "dos_correction"),
            ("radiometric_calibration",
             ["--metadata_file=" + str(TR / "georef_metadata.txt"), "--band=1"],
             "radiometric_calibration"),
            ("cosine_correction", topo, "cosine_correction"),
            ("minnaert_correction", topo + ["--minnaert_k=0.5"], "minnaert_correction"),
            ("c_correction", topo + ["--c_value=0.1"], "c_correction"),
            ("percentile_stretch",
             ["--dark_percentile=1", "--bright_percentile=99"], "percentile_stretch"),
        ]
        for action_name, extra, oracle_key in georef_jobs:
            r.case("true_georef_" + action_name,
                   (lambda a=action_name, e=extra, k=oracle_key: georef_case(a, e, k)))

        def c_gcp():
            vrt = TR / "georef_input.tif"
            out = r.out / "georef_gcp_true.tif"
            rc, text = r.cli_run([
                "georef", "gcp_register", "--input=" + str(vrt), "--output=" + str(out),
                "--gcp_file=" + str(TR / "georef_gcp.csv"),
                "--dst_srs=EPSG:4326", "--resample=nearest"])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing: {}".format(rc, text[-180:])
            meta = json.loads(r.gdal([r.tool("gdalinfo"), "-json", str(out)]))
            gt = meta["geoTransform"]
            exp = E["georef_gcp_expected_gt"]
            if len(gt) != len(exp) or any(abs(a - b) > 1e-2 for a, b in zip(gt, exp)):
                return False, "GCP geotransform {} != independent {}".format(gt, exp)
            return True, "GCP warp follows the independently authored control points"
        r.case("true_georef_gcp_register", c_gcp)

        def c_rpc():
            vrt = TR / "georef_rpc.vrt"
            meta_in = json.loads(r.gdal([r.tool("gdalinfo"), "-json", str(vrt)]))
            keys = len(meta_in.get("metadata", {}).get("RPC", {}))
            if keys != E["georef_rpc_expected_keys"]:
                return False, "RPC metadata keys {} != independent {}".format(
                    keys, E["georef_rpc_expected_keys"])
            out = r.out / "georef_rpc_true.tif"
            rc, text = r.cli_run([
                "georef", "rpc_orthorectify", "--input=" + str(vrt), "--output=" + str(out),
                "--dst_srs=EPSG:4326", "--rpc_height=0", "--resample=nearest"])
            if rc != 0 or not out.is_file():
                return False, "rc={} output-missing: {}".format(rc, text[-180:])
            size, _ = r.dims(out)
            vals = r.pixels(out)
            if size[0] <= 0 or size[1] <= 0 or not np.isfinite(vals).all():
                return False, "empty or non-finite RPC output"
            return True, "RPC orthorectification from independently authored RPC domain"
        r.case("true_georef_rpc_orthorectify", c_rpc)

        def c_georef_negatives():
            bad_gcp = r.out / "georef_bad_gcp.csv"
            bad_gcp.write_text("pixel_x,pixel_y,map_x,map_y\n0,0,116,40\n1,1,116.001,39.999\n",
                               encoding="ascii")
            out1 = r.out / "georef_bad_gcp_true.tif"
            rc1, _ = r.cli_run([
                "georef", "gcp_register", "--input=" + str(TR / "georef_input.tif"),
                "--output=" + str(out1), "--gcp_file=" + str(bad_gcp),
                "--dst_srs=EPSG:4326", "--resample=nearest"])
            out2 = r.out / "georef_no_rpc_true.tif"
            rc2, _ = r.cli_run([
                "georef", "rpc_orthorectify", "--input=" + str(TR / "georef_input.tif"),
                "--output=" + str(out2), "--dst_srs=EPSG:4326", "--rpc_height=0"])
            if rc1 == 0 or rc2 == 0 or out1.exists() or out2.exists():
                return False, "bad GCP / missing RPC were accepted (rc={}/{})".format(rc1, rc2)
            return True, "insufficient GCPs and missing RPC metadata both rejected"
        r.case("true_georef_metadata_negatives", c_georef_negatives)

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
