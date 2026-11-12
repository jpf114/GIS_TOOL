#!/usr/bin/env python3
"""Build independent real-data fixtures and oracles for the true-data regression legs.

This script NEVER calls any algorithm in this repository. It only uses the GDAL command-line
tools shipped with vcpkg (gdal_translate / gdalwarp / ogr2ogr / gdalinfo) and numpy. Every
expected value is derived from the source raster itself (independent of the product under test).

Outputs (all gitignored under data/):
  data/true_matching/   reference.tif, input.tif, change_input.tif, mismatch_small.tif
  data/true_raster/     pansharpen_ms.tif, pansharpen_pan.tif, classification_raster.tif,
                        classification_vector.gpkg, classification_class_map.json,
                        pattern_checker.tif
  data/provenance.json  source paths, exact commands, parameters, seed, derivation notes
  data/true_expected.json  independent oracles (metadata + numpy-derived value expectations)
"""
import argparse
import datetime
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np

WIN = 256
SEED = 20261111
SRC_2024_A = r"D:\Code\Data\raster\2024\H48E018014C.tif"
SRC_2024_B = r"D:\Code\Data\raster\2024\H48E018015C.tif"
SRC_2025 = r"D:\Code\Data\raster\2025\GF2_PMS1_E105.6_N29.0_RGB_20250324C.tif"
SRC_GLC_RASTER = r"D:\Code\Data\GlobeLand30_2000\N20_50_2000LC030\n20_50_2000lc030.tif"
SRC_GLC_VECTOR = r"D:\Code\Data\GlobeLand30_2000\N20_50_2000LC030\N20_50_2000.shp"
SRC_DEM = r"D:\Code\Data\tiff\dem\refas.tif"
PLANE_A = 0.1   # elevation per pixel column (independent analytic fixture)
PLANE_B = 0.05  # elevation per pixel row
PLANE_C = 500.0


def tools(gdal_bin):
    return {
        "translate": str(Path(gdal_bin) / "gdal_translate.exe"),
        "warp": str(Path(gdal_bin) / "gdalwarp.exe"),
        "info": str(Path(gdal_bin) / "gdalinfo.exe"),
        "ogr2ogr": str(Path(gdal_bin) / "ogr2ogr.exe"),
        "buildvrt": str(Path(gdal_bin) / "gdalbuildvrt.exe"),
    }


def run(cmd, log):
    proc = subprocess.run(cmd, capture_output=True, text=True)
    log.append(" ".join(str(c) for c in cmd))
    if proc.returncode != 0:
        raise RuntimeError(
            "command failed (rc={}): {}\nstderr: {}".format(proc.returncode, " ".join(cmd), proc.stderr[:500])
        )
    return proc.stdout


def info(t, path, log):
    out = run([t["info"], "-json", "-stats", str(path)], log)
    return json.loads(out)


def xyz(t, path, dest, log):
    run([t["translate"], "-q", "-of", "XYZ", str(path), str(dest)], log)
    return np.loadtxt(dest)


def xyz_band(t, path, dest, band, log):
    """XYZ driver writes only the first band unless -b is given explicitly."""
    run([t["translate"], "-q", "-of", "XYZ", "-b", str(band), str(path), str(dest)], log)
    return np.loadtxt(dest)


def stats_of(meta, band_index=0):
    band = meta["bands"][band_index]
    md = band.get("metadata", {}).get("", {})
    return {
        "min": float(md["STATISTICS_MINIMUM"]),
        "max": float(md["STATISTICS_MAXIMUM"]),
        "mean": float(md["STATISTICS_MEAN"]),
        "stddev": float(md["STATISTICS_STDDEV"]),
    }


def base_meta(meta):
    return {
        "size": meta["size"],
        "bands": len(meta["bands"]),
        "geoTransform": meta.get("geoTransform"),
        "wkt": (meta.get("coordinateSystem", {}) or {}).get("wkt"),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", required=True)
    ap.add_argument("--src-root", default=r"D:\Code\Data")
    ap.add_argument(
        "--gdal-bin",
        default=r"D:\Code\GitHubCode\vcpkg\installed\x64-windows\tools\gdal",
    )
    ap.add_argument(
        "--proj-data",
        default=r"D:\Code\GitHubCode\vcpkg\installed\x64-windows\share\proj",
    )
    args = ap.parse_args()

    os.environ["PROJ_DATA"] = args.proj_data
    os.environ["PROJ_LIB"] = args.proj_data
    t = tools(args.gdal_bin)
    repo = Path(args.repo)
    out_r = repo / "data" / "true_raster"
    out_m = repo / "data" / "true_matching"
    tmp = repo / "build" / "tmp" / "true_fixtures"
    for d in (out_r, out_m, tmp):
        d.mkdir(parents=True, exist_ok=True)

    log = []
    prov = {
        "generated_at": datetime.datetime.now().isoformat(timespec="seconds"),
        "seed": SEED,
        "window": WIN,
        "toolchain": "vcpkg GDAL command-line tools only; no GIS_TOOL algorithm was invoked",
        "sources": {
            "s2024_a": SRC_2024_A,
            "s2024_b": SRC_2024_B,
            "s2025": SRC_2025,
            "glc_raster": SRC_GLC_RASTER,
            "glc_vector": SRC_GLC_VECTOR,
            "dem": SRC_DEM,
        },
        "notes": [
            "reference/input are two windows of the SAME 2024 scene, offset by 64 px horizontally; "
            "their overlap is pixel-identical by construction (independent geometric oracle).",
            "change_input is the 2025 scene resampled onto the 2024 window grid with bilinear; "
            "this is a genuine multi-temporal pair, not a synthetic copy.",
            "pansharpen_pan is band 1 of the same 2024 window: the sources have RGB+Alpha only "
            "(band 4 is constant 255), so there is NO real panchromatic band; this is an "
            "independent band extraction, NOT a product-algorithm output and NOT a real PAN.",
            "pattern_checker is an independently generated 64x64 board (fixed seed) used as a "
            "discriminating fixture; its values are written from numpy, not from any product.",
            "spindex_stack.tif is a 6-band 64x64 Float32 discriminating fixture: band values are "
            "fixed linear ramps plus a seeded checker perturbation, NOT derived from any index "
            "formula. Index expectations are computed independently with numpy from the public "
            "formulas documented in docs/算法说明/光谱指数/, never from a product run.",
            "classification fixtures are two geometrically separable clusters (left/right half) "
            "with seeded noise; the reference labels, training CSV and the deliberately degraded "
            "classification map are all defined by geometry here, never by a classifier run. "
            "Expected accuracies are 1.0 (perfect) and 0.75 (left quarter flipped), computed "
            "independently from the pixel counts.",
            "change_before/after are analytic ramps differing only in a known rectangle "
            "(y 16..32, x 16..48, +100); mosaic_left/right are two adjacent plates with "
            "different constant levels. The expected change map is |after-before| computed "
            "here, and the mosaic geometry is defined by the assigned bounds.",
            "georef fixtures are analytic ramps with independently defined slope/aspect, "
            "GAIN/OFFSET metadata, 4 GCP points and a 16-key RPC metadata domain inside a VRT. "
            "Expected DOS / radiometric / cosine / Minnaert / C / percentile outputs are "
            "computed independently with numpy from the published correction formulas.",
        ],
        "commands": log,
    }

    def translate(src, dst, extra):
        run([t["translate"], "-q", *extra, str(src), str(dst)], log)
        return info(t, dst, log)

    # ---- matching: real 2024 scene, x window inside the 2025 overlap area -----------------
    # H48E018015C origin (105.5, 29.1668), pixel 1.8e-05; window at x~105.55, y~29.10.
    ref = out_m / "reference.tif"
    inp = out_m / "input.tif"
    m_ref = translate(SRC_2024_B, ref, ["-srcwin", "2778", "3711", str(WIN), str(WIN), "-b", "1"])
    m_inp = translate(SRC_2024_B, inp, ["-srcwin", "2842", "3711", str(WIN), str(WIN), "-b", "1"])
    ul = m_ref["cornerCoordinates"]["upperLeft"]
    lr = m_ref["cornerCoordinates"]["lowerRight"]
    change = out_m / "change_input.tif"
    warp_tmp = tmp / "change_warp.tif"
    run(
        [t["warp"], "-q", "-overwrite", "-t_srs", "EPSG:4490", "-te", str(ul[0]), str(lr[1]), str(lr[0]), str(ul[1]),
         "-ts", str(WIN), str(WIN), "-r", "bilinear", "-b", "1", SRC_2025, str(warp_tmp)],
        log,
    )
    # gdalwarp keeps the source Alpha band; keep only band 1 so matching sees a single-band image.
    run([t["translate"], "-q", "-b", "1", str(warp_tmp), str(change)], log)
    m_chg = info(t, change, log)
    mismatch = out_m / "mismatch_small.tif"
    translate(ref, mismatch, ["-srcwin", "0", "0", "128", "128"])

    # independent geometric oracle: overlap of reference[64:, :] and input[:-64, :]
    a = xyz(t, ref, tmp / "ref.xyz", log)
    b = xyz(t, inp, tmp / "inp.xyz", log)
    va = a[:, 2].reshape(WIN, WIN)
    vb = b[:, 2].reshape(WIN, WIN)
    overlap_equal = bool(np.array_equal(va[:, 64:], vb[:, :-64]))

    # ---- raster: real multispectral, real land-cover, independent class map ---------------
    ms = out_r / "pansharpen_ms.tif"
    m_ms = translate(SRC_2024_A, ms, ["-srcwin", "3000", "3000", str(WIN), str(WIN),
                                      "-b", "1", "-b", "2", "-b", "3"])
    pan = out_r / "pansharpen_pan.tif"
    m_pan = translate(SRC_2024_A, pan, ["-srcwin", "3000", "3000", str(WIN), str(WIN), "-b", "1"])

    # XYZ emits one band at a time (x y value); read B1 and B2 independently.
    b1 = xyz_band(t, ms, tmp / "ms_b1.xyz", 1, log)[:, 2].reshape(WIN, WIN)
    b2 = xyz_band(t, ms, tmp / "ms_b2.xyz", 2, log)[:, 2].reshape(WIN, WIN)
    bsum = b1 + b2
    sum_oracle = {"min": float(bsum.min()), "max": float(bsum.max()), "mean": float(bsum.mean())}

    cls = out_r / "classification_raster.tif"
    m_cls = translate(SRC_GLC_RASTER, cls, ["-srcwin", "3000", "3000", str(WIN), str(WIN)])
    cls_xyz = xyz(t, cls, tmp / "cls.xyz", log)
    codes = sorted({int(v) for v in cls_xyz[:, 2]})
    class_map = {str(c): "class_{}".format(c) for c in codes}
    (out_r / "classification_class_map.json").write_text(
        json.dumps(class_map, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    # GLC shapefile is EPSG:4326 while the land-cover raster is UTM 32620. Project first,
    # then clip to the raster window so vector and raster actually overlap.
    cls_ul = m_cls["cornerCoordinates"]["upperLeft"]
    cls_lr = m_cls["cornerCoordinates"]["lowerRight"]
    proj_gpkg = tmp / "glc_32620.gpkg"
    gpkg = out_r / "classification_vector.gpkg"
    for p in (proj_gpkg, gpkg):
        if p.exists():
            p.unlink()
    run([t["ogr2ogr"], "-q", "-f", "GPKG", "-t_srs", "EPSG:32620",
         str(proj_gpkg), SRC_GLC_VECTOR], log)
    run([t["ogr2ogr"], "-q", "-f", "GPKG", "-spat",
         str(cls_ul[0]), str(cls_lr[1]), str(cls_lr[0]), str(cls_ul[1]),
         str(gpkg), str(proj_gpkg)], log)

    # ---- discriminating fixture: independent 64x64 checker board ------------------------
    rng = np.random.RandomState(SEED)
    board = ((np.indices((64, 64)).sum(axis=0) % 2) * 255).astype(np.uint8)
    # a small deterministic perturbation keeps it from being a trivial constant
    board[10:20, 10:20] = int(rng.randint(1, 254))
    board_path = tmp / "checker.xyz"
    with open(board_path, "w", encoding="ascii") as f:
        for y in range(64):
            for x in range(64):
                f.write("{} {} {}\n".format(x, y, board[y, x]))
    checker = out_r / "pattern_checker.tif"
    run([t["translate"], "-q", "-of", "GTiff", "-a_srs", "EPSG:4326",
         "-a_ullr", "116.0", "40.0", "116.064", "39.936",
         str(board_path), str(checker)], log)
    m_chk = info(t, checker, log)

    # ---- terrain: real DEM windows + an independent analytic plane -------------------
    dem_real = out_r / "dem_real.tif"
    m_dem = translate(SRC_DEM, dem_real, ["-srcwin", "1000", "300", str(WIN), str(WIN)])
    dem_gt = m_dem["geoTransform"]
    # cut_fill requires an identical geotransform, so take a DIFFERENT real DEM window and
    # re-assign dem_real's bounds: same grid, genuinely different real elevations.
    dem_ref = out_r / "dem_reference.tif"
    dem_ref_raw = tmp / "dem_ref_raw.tif"
    translate(SRC_DEM, dem_ref_raw, ["-srcwin", "1100", "300", str(WIN), str(WIN)])
    ulx, uly = dem_gt[0], dem_gt[3]
    lrx, lry = ulx + WIN * dem_gt[1], uly + WIN * dem_gt[5]
    run([t["translate"], "-q", "-a_ullr", str(ulx), str(uly), str(lrx), str(lry),
         str(dem_ref_raw), str(dem_ref)], log)
    m_dem_ref = info(t, dem_ref, log)
    dem_center = [dem_gt[0] + WIN * dem_gt[1] / 2.0, dem_gt[3] + WIN * dem_gt[5] / 2.0]
    dem_stats = stats_of(m_dem)
    dem_water = (dem_stats["min"] + dem_stats["max"]) / 2.0

    plane_path = tmp / "plane.xyz"
    with open(plane_path, "w", encoding="ascii") as f:
        for y in range(64):
            for x in range(64):
                f.write("{} {} {:.6f}\n".format(x, y, PLANE_C + PLANE_A * x + PLANE_B * y))
    dem_plane = out_r / "dem_plane.tif"
    run([t["translate"], "-q", "-of", "GTiff", "-a_srs", "EPSG:3857",
         "-a_ullr", "500000", "4000000", "500064", "3999936",
         str(plane_path), str(dem_plane)], log)
    m_plane = info(t, dem_plane, log)
    # Horn 3x3 on a perfect plane gives dz/dx=A, dz/dy=B (1 m pixels, scale=1);
    # the expected slope is pure analytic geometry, not a product output.
    plane_slope_deg = float(np.degrees(np.arctan(np.hypot(PLANE_A, PLANE_B))))

    # ---- spindex: independent 6-band discriminating stack + formula oracles ----------
    # The band values are generated from fixed linear ramps + a seeded checker perturbation.
    # They are NOT derived from any index formula, and no GIS_TOOL algorithm is involved.
    # Expected index values are then computed independently with numpy using the PUBLIC
    # index formulas (same ones documented in docs/算法说明/光谱指数/), never from a product run.
    n = 64
    gx, gy = np.meshgrid(np.arange(n), np.arange(n))
    rng2 = np.random.RandomState(SEED + 1)
    perturb = ((rng2.rand(n, n) > 0.5) * 2.0 - 1.0) * 0.01
    band_values = [
        0.05 + 0.006 * gx + 0.002 * gy + perturb,
        0.10 + 0.004 * gx + 0.003 * gy,
        0.06 + 0.002 * gx + 0.001 * gy,
        0.30 + 0.008 * gx + 0.002 * gy,
        0.20 + 0.005 * gx + 0.004 * gy,
        0.15 + 0.004 * gx + 0.006 * gy,
    ]
    B = np.stack([np.asarray(v, dtype=np.float32) for v in band_values], axis=0)
    single_paths = []
    for i in range(6):
        xyz_path = tmp / ("spindex_b{}.xyz".format(i + 1))
        with open(xyz_path, "w", encoding="ascii") as f:
            for y in range(n):
                for x in range(n):
                    f.write("{} {} {:.8f}\n".format(x, y, float(B[i, y, x])))
        tif = tmp / ("spindex_b{}.tif".format(i + 1))
        run([t["translate"], "-q", "-of", "GTiff", "-a_srs", "EPSG:4326",
             "-a_ullr", "116.0", "40.0", "116.064", "39.936",
             str(xyz_path), str(tif)], log)
        single_paths.append(str(tif))
    vrt = tmp / "spindex_stack.vrt"
    run([t["buildvrt"], "-q", "-separate", str(vrt), *single_paths], log)
    stack = out_r / "spindex_stack.tif"
    run([t["translate"], "-q", str(vrt), str(stack)], log)
    m_stack = info(t, stack, log)

    # (CLI args, independent numpy formula). B[k] is physical band k+1 of the stack,
    # so the formula and the band flags come from the same mapping and cannot drift.
    specs = {
        "ndvi": (["--red_band=1", "--nir_band=4"],
                 lambda a: (a[3] - a[0]) / (a[3] + a[0])),
        "ndmi": (["--nir_band=4", "--swir1_band=5"],
                 lambda a: (a[3] - a[4]) / (a[3] + a[4])),
        "evi": (["--blue_band=3", "--red_band=1", "--nir_band=4"],
                lambda a: 2.5 * (a[3] - a[0]) / (a[3] + 6.0 * a[0] - 7.5 * a[2] + 1.0)),
        "evi2": (["--red_band=1", "--nir_band=4"],
                 lambda a: 2.5 * (a[3] - a[0]) / (a[3] + 2.4 * a[0] + 1.0)),
        "savi": (["--red_band=1", "--nir_band=4"],
                 lambda a: ((a[3] - a[0]) * 1.5) / (a[3] + a[0] + 0.5)),
        "osavi": (["--red_band=1", "--nir_band=4"],
                  lambda a: 1.16 * (a[3] - a[0]) / (a[3] + a[0] + 0.16)),
        "gndvi": (["--green_band=2", "--nir_band=4"],
                  lambda a: (a[3] - a[1]) / (a[3] + a[1])),
        "ndwi": (["--green_band=2", "--nir_band=4"],
                 lambda a: (a[1] - a[3]) / (a[1] + a[3])),
        "mndwi": (["--green_band=2", "--swir1_band=4"],
                  lambda a: (a[1] - a[3]) / (a[1] + a[3])),
        "ndbi": (["--swir1_band=4", "--nir_band=2"],
                 lambda a: (a[3] - a[1]) / (a[3] + a[1])),
        "bsi": (["--blue_band=3", "--red_band=1", "--nir_band=4", "--swir1_band=5"],
                lambda a: ((a[4] + a[0]) - (a[3] + a[2])) / ((a[4] + a[0]) + (a[3] + a[2]))),
        "arvi": (["--blue_band=1", "--red_band=2", "--nir_band=4"],
                 lambda a: (a[3] - (2.0 * a[1] - a[0])) / (a[3] + (2.0 * a[1] - a[0]))),
        "nbr": (["--nir_band=4", "--swir2_band=6"],
                lambda a: (a[3] - a[5]) / (a[3] + a[5])),
        "awei": (["--green_band=2", "--nir_band=4", "--swir1_band=5", "--swir2_band=6"],
                 lambda a: 4.0 * (a[1] - a[4]) - (0.25 * a[3] + 2.75 * a[5])),
        "ui": (["--nir_band=4", "--swir2_band=6"],
               lambda a: (a[5] - a[3]) / (a[5] + a[3])),
        "bi": (["--red_band=3", "--nir_band=4"],
               lambda a: 1.0 / ((0.1 - a[2]) ** 2 + (0.06 - a[3]) ** 2 + 1e-10)),
        "custom_index": (["--preset=ndvi_alias", "--red_band=1", "--nir_band=4"],
                         lambda a: (a[3] - a[0]) / (a[3] + a[0])),
    }

    sample_xy = ((0, 0), (16, 16), (32, 48), (63, 63))

    def oracle(arr):
        return {
            "min": float(arr.min()), "max": float(arr.max()),
            "mean": float(arr.mean()), "std": float(arr.std()),
            "samples": [float(arr[y, x]) for x, y in sample_xy],
        }

    spindex_oracle = {}
    spindex_params = {}
    for name, (args, formula) in specs.items():
        arr = np.asarray(formula(B), dtype=np.float64)
        if not np.isfinite(arr).all():
            raise RuntimeError("non-finite independent oracle for spindex." + name)
        spindex_params[name] = args
        spindex_oracle[name] = oracle(arr)

    # ---- classification: independently separable 2-class fixture ------------------
    # Two well-separated clusters (left: ~10/12, right: ~200/210) with a small seeded noise.
    # The class boundaries, reference labels and the degraded map are defined here by geometry
    # alone; no classifier output is ever used as a fixture or as an expectation.
    cn = 64
    ccx, ccy = np.meshgrid(np.arange(cn), np.arange(cn))
    crng = np.random.RandomState(SEED + 2)
    cnoise = crng.uniform(-1.0, 1.0, (cn, cn)).astype(np.float32)
    cleft = ccx < (cn // 2)
    cls_b1 = np.where(cleft, 10.0, 200.0).astype(np.float32) + cnoise
    cls_b2 = np.where(cleft, 12.0, 210.0).astype(np.float32) + cnoise * 0.5
    cls_single = []
    for i, arr in enumerate((cls_b1, cls_b2), 1):
        p = tmp / ("cls_input_b{}.xyz".format(i))
        with open(p, "w", encoding="ascii") as f:
            for y in range(cn):
                for x in range(cn):
                    f.write("{} {} {:.6f}\n".format(x, y, float(arr[y, x])))
        tif = tmp / ("cls_input_b{}.tif".format(i))
        run([t["translate"], "-q", "-of", "GTiff", "-ot", "Float32",
             "-a_srs", "EPSG:4326", "-a_ullr", "116.0", "40.0", "116.064", "39.936",
             str(p), str(tif)], log)
        cls_single.append(str(tif))
    cls_vrt = tmp / "cls_input.vrt"
    run([t["buildvrt"], "-q", "-separate", str(cls_vrt), *cls_single], log)
    cls_input = out_r / "classification_input.tif"
    run([t["translate"], "-q", str(cls_vrt), str(cls_input)], log)
    m_cls_input = info(t, cls_input, log)

    cls_ref = np.where(cleft, 1, 2).astype(np.int32)
    cls_degraded = cls_ref.copy()
    cls_degraded[:, : (cn // 4)] = 2  # flip the left quarter: accuracy must be exactly 0.75
    cls_ref_path = out_r / "classification_reference.tif"
    cls_deg_path = out_r / "classification_degraded.tif"
    cls_perfect_path = out_r / "classification_perfect.tif"
    for arr, path in ((cls_ref, cls_ref_path), (cls_degraded, cls_deg_path),
                      (cls_ref, cls_perfect_path)):
        p = tmp / (path.stem + ".xyz")
        with open(p, "w", encoding="ascii") as f:
            for y in range(cn):
                for x in range(cn):
                    f.write("{} {} {}\n".format(x, y, int(arr[y, x])))
        run([t["translate"], "-q", "-of", "GTiff", "-ot", "Int32",
             "-a_srs", "EPSG:4326", "-a_ullr", "116.0", "40.0", "116.064", "39.936",
             str(p), str(path)], log)

    csv_path = out_r / "classification_training.csv"
    with open(csv_path, "w", encoding="ascii", newline="") as f:
        f.write("b1,b2,label\n")
        for _ in range(4):
            f.write("10,12,1\n")
        for _ in range(4):
            f.write("200,210,2\n")

    # ---- single-action fixtures (C4/C5/C7/C14): nodata / kmeans / change / mosaic ----
    # All values are fixed analytic ramps; the changed rectangle and the two mosaic plates
    # are defined here by geometry, so every expectation is independent of product output.
    sn = 64
    sx, sy = np.meshgrid(np.arange(sn), np.arange(sn))
    sramp = (0.01 * sx + 0.02 * sy).astype(np.float32)
    change_before = 50.0 + sramp
    change_after = change_before.copy()
    change_after[16:32, 16:48] += 100.0
    mosaic_left = 30.0 + sramp
    mosaic_right = 90.0 + sramp

    def write_single(name, arr, ulx, dtype="Float32"):
        p = tmp / (name + ".xyz")
        with open(p, "w", encoding="ascii") as f:
            for y in range(sn):
                for x in range(sn):
                    f.write("{} {} {:.6f}\n".format(x, y, float(arr[y, x])))
        out = out_r / (name + ".tif")
        run([t["translate"], "-q", "-of", "GTiff", "-ot", dtype,
             "-a_srs", "EPSG:4326",
             "-a_ullr", str(ulx), "40.0", str(ulx + 0.064), "39.936",
             str(p), str(out)], log)
        return out

    ch_before_path = write_single("change_before", change_before, 116.0)
    ch_after_path = write_single("change_after", change_after, 116.0)
    mos_left_path = write_single("mosaic_left", mosaic_left, 116.0)
    mos_right_path = write_single("mosaic_right", mosaic_right, 116.064)

    change_oracle = np.abs(change_after - change_before)

    def simple_oracle(arr):
        return {
            "min": float(arr.min()), "max": float(arr.max()), "mean": float(arr.mean()),
            "samples": [float(arr[y, x]) for x, y in sample_xy],
        }

    # ---- georef batch (C13): analytic ramps + independently authored RPC/GCP metadata ----
    # Ramp values, slope/aspect, gain/offset, GCP coordinates and RPC coefficients are all
    # defined here; none of them come from a product run. The expected correction rasters are
    # computed independently with numpy from the published correction formulas.
    gn = 64
    gxx, gyy = np.meshgrid(np.arange(gn), np.arange(gn))
    gbase = (20.0 + 1.2 * gxx + 0.8 * gyy).astype(np.float32)
    gslope = (15.0 + 0.05 * gxx).astype(np.float32)
    gaspect = (90.0 + 0.1 * gyy).astype(np.float32)
    georef_input = write_single("georef_input", gbase, 116.0)
    georef_slope = write_single("georef_slope", gslope, 116.0)
    georef_aspect = write_single("georef_aspect", gaspect, 116.0)

    (out_r / "georef_metadata.txt").write_text("GAIN=2.0\nOFFSET=5.0\n", encoding="ascii")

    gcp_rows = [
        "pixel_x,pixel_y,map_x,map_y",
        "0,0,116.0,40.0",
        "63,0,116.063,40.0",
        "0,63,116.0,39.937",
        "63,63,116.063,39.937",
    ]
    (out_r / "georef_gcp.csv").write_text("\n".join(gcp_rows) + "\n", encoding="ascii")

    # VRT carrying an independent identity-like RPC model (16 keys in the RPC metadata domain).
    den_coeff = [1] + [0] * 19
    line_num = [0, 1] + [0] * 18
    samp_num = [0, 0, 1] + [0] * 17
    rpc_items = {
        "ERR_BIAS": "0", "ERR_RAND": "0",
        "LINE_OFF": "32", "SAMP_OFF": "32",
        "LAT_OFF": "40", "LONG_OFF": "116", "HEIGHT_OFF": "0",
        "LINE_SCALE": "32", "SAMP_SCALE": "32",
        "LAT_SCALE": "0.064", "LONG_SCALE": "0.064", "HEIGHT_SCALE": "100",
        "LINE_NUM_COEFF": " ".join(str(v) for v in line_num),
        "LINE_DEN_COEFF": " ".join(str(v) for v in den_coeff),
        "SAMP_NUM_COEFF": " ".join(str(v) for v in samp_num),
        "SAMP_DEN_COEFF": " ".join(str(v) for v in den_coeff),
    }
    rpc_md = "".join('<MDI key="{}">{}</MDI>'.format(k, v) for k, v in rpc_items.items())
    rpc_vrt = out_r / "georef_rpc.vrt"
    rpc_vrt.write_text(
        '<VRTDataset rasterXSize="64" rasterYSize="64">'
        "<SRS>EPSG:4326</SRS>"
        "<GeoTransform>116.0, 0.001, 0.0, 40.0, 0.0, -0.001</GeoTransform>"
        '<Metadata domain="RPC">' + rpc_md + "</Metadata>"
        '<VRTRasterBand dataType="Float32" band="1"><SimpleSource>'
        "<SourceFilename relativeToVRT=\"0\">" +
        str(georef_input).replace("\\", "/") + "</SourceFilename>"
        "<SourceBand>1</SourceBand></SimpleSource></VRTRasterBand></VRTDataset>",
        encoding="ascii",
    )

    deg = np.pi / 180.0
    sun_z, sun_a = 30.0 * deg, 180.0 * deg
    cos_z = np.cos(sun_z)
    cos_i = (np.cos(gslope * deg) * cos_z +
             np.sin(gslope * deg) * np.sin(sun_z) * np.cos(sun_a - gaspect * deg))
    safe_i = np.maximum(cos_i, 1e-9)
    gcosine = np.where(cos_i > 1e-6, gbase * (cos_z / safe_i), 0.0)
    gminnaert = np.where((cos_i > 1e-6) & (cos_z > 1e-6),
                         gbase * (cos_z / safe_i) ** 0.5, 0.0)
    gccorrection = np.where(cos_i + 0.1 > 1e-6,
                            gbase * ((cos_z + 0.1) / (cos_i + 0.1)), 0.0)
    gdark = float(np.sort(gbase.flatten())[int(np.floor(0.01 * (gbase.size - 1) + 0.5))])
    gbright = float(np.sort(gbase.flatten())[int(np.floor(0.99 * (gbase.size - 1) + 0.5))])
    gstretch = np.minimum(np.maximum(gbase - gdark, 0.0) / (gbright - gdark), 1.0)
    gdos = np.maximum(gbase - gbase.min(), 0.0) / (gbase.max() - gbase.min())
    grad = gbase * 2.0 + 5.0

    georef_oracle = {
        "dos_correction": simple_oracle(gdos),
        "radiometric_calibration": simple_oracle(grad),
        "cosine_correction": simple_oracle(gcosine),
        "minnaert_correction": simple_oracle(gminnaert),
        "c_correction": simple_oracle(gccorrection),
        "percentile_stretch": simple_oracle(gstretch),
    }
    for k, v in georef_oracle.items():
        if not np.isfinite([v["min"], v["max"], v["mean"], *v["samples"]]).all():
            raise RuntimeError("non-finite independent oracle for georef." + k)

    expected = {
        "oracle_independence": "all values derived from gdalinfo / numpy on the SOURCE or fixture; "
                               "no GIS_TOOL algorithm or CLI output was used as an expectation",
        "reference": {**base_meta(m_ref), "stats": stats_of(m_ref)},
        "input": {**base_meta(m_inp), "stats": stats_of(m_inp)},
        "change_input": {**base_meta(m_chg), "stats": stats_of(m_chg)},
        "mismatch_small": base_meta(info(t, mismatch, log)),
        "pansharpen_ms": {**base_meta(m_ms), "bands_stats": [stats_of(m_ms, i) for i in range(3)]},
        "pansharpen_pan": {**base_meta(m_pan), "stats": stats_of(m_pan)},
        "classification_raster": {**base_meta(m_cls), "stats": stats_of(m_cls)},
        "classification_class_map_codes": codes,
        "classification_vector": {
            "layer": "N20_50_2000",
            "srs": "EPSG:32620",
            "id_field": "Filename",
            "name_field": "Row",
            "note": "field names read independently from ogrinfo -so; vector is projected and "
                     "clipped to the raster window, so the pair actually overlaps",
        },
        "pattern_checker": {**base_meta(m_chk), "stats": stats_of(m_chk)},
        "dem_real": {**base_meta(m_dem), "stats": dem_stats},
        "dem_reference": base_meta(m_dem_ref),
        "dem_plane": {**base_meta(m_plane), "slope_degrees": plane_slope_deg,
                      "plane_a": PLANE_A, "plane_b": PLANE_B, "plane_c": PLANE_C},
        "terrain_center": dem_center,
        "terrain_water_level": dem_water,
        "spindex_stack": {**base_meta(m_stack), "band_count": len(m_stack["bands"])},
        "spindex_params": spindex_params,
        "spindex_oracle": spindex_oracle,
        "spindex_sample_xy": [list(p) for p in sample_xy],
        "classification_input": base_meta(m_cls_input),
        "classification_size": [cn, cn],
        "classification_half": cn // 2,
        "classification_left_label": 1,
        "classification_right_label": 2,
        "classification_perfect_accuracy": 1.0,
        "classification_degraded_accuracy": 0.75,
        "change_oracle": simple_oracle(change_oracle),
        "change_rect": [16, 32, 16, 48],
        "change_delta": 100.0,
        "mosaic_expected_size": [128, 64],
        "mosaic_left_mean": float(mosaic_left.mean()),
        "mosaic_right_mean": float(mosaic_right.mean()),
        "georef_oracle": georef_oracle,
        "georef_gcp_expected_gt": [116.0, 0.001, 0.0, 40.0, 0.0, -0.001],
        "georef_rpc_expected_keys": len(rpc_items),
        "georef_dark_value": gdark,
        "georef_bright_value": gbright,
        "band_math_B1_plus_B2": sum_oracle,
        "reference_input_shift_px": 64,
        "reference_input_overlap_identical": overlap_equal,
    }

    (repo / "data" / "provenance.json").write_text(
        json.dumps(prov, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    (repo / "data" / "true_expected.json").write_text(
        json.dumps(expected, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    if not overlap_equal:
        raise RuntimeError("independent geometric oracle failed: shifted windows do not overlap identically")
    for key in ("reference", "input", "change_input", "pansharpen_ms", "classification_raster", "dem_real"):
        if expected[key]["stats" if key != "pansharpen_ms" else "bands_stats"] is None:
            raise RuntimeError("missing stats for " + key)
        st = expected[key].get("stats") or expected[key]["bands_stats"][0]
        if st["stddev"] <= 0:
            raise RuntimeError("degenerate (constant) fixture: " + key)

    print("OK true-data fixtures: {} products, {} commands".format(len(log), len(log)))
    print("overlap_identical={} band_math_oracle={}".format(overlap_equal, sum_oracle))
    return 0


if __name__ == "__main__":
    sys.exit(main())
