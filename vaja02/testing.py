#!/usr/bin/env python3
"""
test_dct_rle.py

Automates testing:
- Find 5 color images (>=500x500) with at least one .bmp, one .jpg/.jpeg, one .png.
- Compress/decompress with 3 factors depending on mode:
    mode 1 (triangular, "prosojnica 9"): 0, 8, 14
    mode 2 (zig-zag count, "prosojnica 11"): 0, 30, 61
- Save decompressed images and contact sheets ("screenshots").
- Record compression/decompression times and throughput.

Usage:
  python test_dct_rle.py --input ./images --output ./results --mode 1 --exe ./dct_rle
  python test_dct_rle.py --input ./images --output ./results --mode 2 --exe ./dct_rle

Notes:
- If some images are smaller than 500x500, you can allow upscaling with --resize-if-small.
- The script does not download images; put your test images into --input.
"""

import argparse
import csv
import os
import sys
import time
import subprocess
from pathlib import Path
from typing import List, Tuple, Dict

import cv2
import numpy as np


REQUIRED_EXT_GROUPS = {
    "bmp": {".bmp"},
    "jpg": {".jpg", ".jpeg"},
    "png": {".png"},
}

MODE_FACTORS = {
    1: [0, 8, 14],   # triangular mask factors (your current C++ implementation)
    2: [0, 30, 61],  # zig-zag "drop last K" factors (requires matching encoder/decoder)
}


def is_color(img: np.ndarray) -> bool:
    return img is not None and len(img.shape) == 3 and img.shape[2] >= 3


def load_image_info(path: Path) -> Tuple[bool, Tuple[int, int]]:
    img = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if img is None:
        return False, (0, 0)
    h, w = img.shape[:2]
    return is_color(img), (w, h)


def ensure_constraints(candidates: List[Path]) -> List[Path]:
    """
    Pick 5 images with at least one .bmp, one .jpg/.jpeg, one .png, all >= 500x500 and color.
    """
    ok_items = []
    by_ext_bucket: Dict[str, List[Path]] = {"bmp": [], "jpg": [], "png": []}

    for p in candidates:
        ok, (w, h) = load_image_info(p)
        if not ok or w < 500 or h < 500:
            continue
        ext = p.suffix.lower()
        # Put in buckets for constraints
        if ext in REQUIRED_EXT_GROUPS["bmp"]:
            by_ext_bucket["bmp"].append(p)
        if ext in REQUIRED_EXT_GROUPS["jpg"]:
            by_ext_bucket["jpg"].append(p)
        if ext in REQUIRED_EXT_GROUPS["png"]:
            by_ext_bucket["png"].append(p)
        ok_items.append(p)

    # Must have at least one of each required type
    if not by_ext_bucket["bmp"] or not by_ext_bucket["jpg"] or not by_ext_bucket["png"]:
        raise RuntimeError("Need at least one .bmp, one .jpg/.jpeg, and one .png (all >= 500x500, color).")

    # Strategy: pick one from each bucket, then fill remaining (up to 5) from remaining ok_items
    selected = []
    # one per required group
    selected.append(by_ext_bucket["bmp"][0])
    selected.append(by_ext_bucket["jpg"][0])
    selected.append(by_ext_bucket["png"][0])

    # fill to 5 unique items, keeping order of candidates
    seen = {s.resolve() for s in selected}
    for p in ok_items:
        if len(selected) >= 5:
            break
        if p.resolve() not in seen:
            selected.append(p)
            seen.add(p.resolve())

    if len(selected) < 5:
        raise RuntimeError("Could not assemble 5 qualifying images; add more to --input.")

    return selected[:5]


def maybe_resize_to_min(img_path: Path, min_wh: int) -> Path:
    """
    If image < min_wh in either dimension, resize with OpenCV to min 500 (keeping aspect).
    Returns path to possibly-resized temp image (same folder, suffix _resized.ext).
    """
    img = cv2.imread(str(img_path), cv2.IMREAD_COLOR)
    if img is None:
        return img_path
    h, w = img.shape[:2]
    if w >= min_wh and h >= min_wh:
        return img_path

    scale = max(min_wh / float(w), min_wh / float(h))
    new_w = int(round(w * scale))
    new_h = int(round(h * scale))
    resized = cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_CUBIC)
    out = img_path.with_name(img_path.stem + "_resized" + img_path.suffix)
    cv2.imwrite(str(out), resized)
    return out


def run_cmd_timed(cmd: List[str]) -> float:
    """
    Run a command and return elapsed time in seconds.
    Raises on non-zero exit.
    """
    t0 = time.perf_counter()
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as e:
        raise RuntimeError(f"Command failed ({e.returncode}): {' '.join(cmd)}") from e
    return time.perf_counter() - t0


def make_contact_sheet(original: Path, outputs: List[Tuple[str, Path]], out_path: Path) -> None:
    """
    Build a simple contact sheet: [Original | Dec F0 | Dec F1 | Dec F2]
    """
    imgs = []
    # read original
    img0 = cv2.imread(str(original), cv2.IMREAD_COLOR)
    if img0 is None:
        return
    imgs.append(img0)

    # read each decompressed
    for label, p in outputs:
        im = cv2.imread(str(p), cv2.IMREAD_COLOR)
        if im is None:
            continue
        imgs.append(im)

    # unify heights
    Hmin = min(img.shape[0] for img in imgs)
    resized = []
    for im in imgs:
        h, w = im.shape[:2]
        scale = Hmin / float(h)
        new_w = max(1, int(round(w * scale)))
        imr = cv2.resize(im, (new_w, Hmin), interpolation=cv2.INTER_AREA)
        resized.append(imr)

    canvas = cv2.hconcat(resized)
    cv2.imwrite(str(out_path), canvas)


def main():
    ap = argparse.ArgumentParser(description="Automated test runner for DCT+RLE codec.")
    ap.add_argument("--input", required=True, type=Path, help="Folder with candidate images.")
    ap.add_argument("--output", required=True, type=Path, help="Output folder.")
    ap.add_argument("--mode", required=True, type=int, choices=[1, 2],
                    help="1 = triangular (factors 0,8,14), 2 = zig-zag count (0,30,61).")
    ap.add_argument("--exe", default="C:\\Users\\danijel\\CLionProjects\\RVP\\cmake-build-debug\\vaja02\\vaja02.exe",
                    help="Path to encoder/decoder executable.")
    ap.add_argument("--resize-if-small", action="store_true",
                    help="If an image <500x500, upscale it to meet the constraint.")
    args = ap.parse_args()

    exe = Path(args.exe)
    if not exe.exists():
        print(f"ERROR: executable not found: {exe}", file=sys.stderr)
        sys.exit(2)

    out_root = args.output
    out_root.mkdir(parents=True, exist_ok=True)

    # gather candidates
    exts = (".bmp", ".png", ".jpg", ".jpeg")
    candidates = sorted([p for p in args.input.rglob("*") if p.suffix.lower() in exts])

    if not candidates:
        print("No candidate images found in --input.", file=sys.stderr)
        sys.exit(1)

    # optionally resize if small
    if args.resize_if_small:
        resized_origs = []
        for p in candidates:
            resized_origs.append(maybe_resize_to_min(p, 500))
        candidates = sorted(set(resized_origs), key=lambda x: str(x))

    selected = ensure_constraints(candidates)

    factors = MODE_FACTORS[args.mode]
    print(f"Selected files ({len(selected)}):")
    for p in selected:
        print("  -", p)

    print(f"Using mode {args.mode} with factors: {factors}")

    # Summary CSV (now includes timing + throughput)
    csv_path = out_root / "summary.csv"
    with csv_path.open("w", newline="") as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow([
            "original", "ext", "width", "height", "factor",
            "compressed_bin", "decompressed_png",
            "comp_time_s", "decomp_time_s", "comp_mpix_s", "decomp_mpix_s"
        ])

        for img_path in selected:
            img = cv2.imread(str(img_path), cv2.IMREAD_COLOR)
            if img is None:
                print(f"Skipping unreadable image: {img_path}", file=sys.stderr)
                continue
            h, w = img.shape[:2]
            mpix = (w * h) / 1_000_000.0

            base_dir = out_root / img_path.stem
            base_dir.mkdir(parents=True, exist_ok=True)

            dec_outputs_for_sheet: List[Tuple[str, Path]] = []

            for f in factors:
                bin_path = base_dir / f"{img_path.stem}_f{f}.dct"
                dec_path = base_dir / f"{img_path.stem}_f{f}_decoded.png"

                # compress (timed)
                cmd_c = [str(exe), "c", str(img_path), str(bin_path), str(f)]
                print(">", " ".join(cmd_c))
                comp_time = run_cmd_timed(cmd_c)

                # decompress (timed)
                cmd_d = [str(exe), "d", str(bin_path), str(dec_path)]
                print(">", " ".join(cmd_d))
                decomp_time = run_cmd_timed(cmd_d)

                # throughput (MPix/s)
                comp_mpix_s = (mpix / comp_time) if comp_time > 0 else float('inf')
                decomp_mpix_s = (mpix / decomp_time) if decomp_time > 0 else float('inf')

                # log line
                print(f"  [{img_path.name}] f={f} | comp={comp_time:.3f}s ({comp_mpix_s:.2f} MPix/s) "
                      f"| decomp={decomp_time:.3f}s ({decomp_mpix_s:.2f} MPix/s)")

                writer.writerow([
                    str(img_path), img_path.suffix.lower(), w, h, f,
                    str(bin_path), str(dec_path),
                    f"{comp_time:.6f}", f"{decomp_time:.6f}",
                    f"{comp_mpix_s:.3f}", f"{decomp_mpix_s:.3f}"
                ])
                dec_outputs_for_sheet.append((f"f{f}", dec_path))

            # contact sheet / "screenshot"
            sheet_path = base_dir / f"{img_path.stem}_contact_sheet_mode{args.mode}.png"
            try:
                make_contact_sheet(img_path, dec_outputs_for_sheet, sheet_path)
                print("Contact sheet:", sheet_path)
            except Exception as e:
                print(f"Contact sheet failed for {img_path.name}: {e}", file=sys.stderr)

    print("\nDone.")
    print(f"- Results folder: {out_root}")
    print(f"- Summary CSV:   {csv_path}")
    print("- For the report: include the decompressed images and the contact sheets as 'zaslonski posnetki'.")
    if args.mode == 2:
        print("\nNOTE: Mode 2 expects the encoder/decoder to implement the zig-zag 'drop last K' scheme.")
        print("      If your current C++ uses the triangular mask only, stick to --mode 1.")


if __name__ == "__main__":
    main()
