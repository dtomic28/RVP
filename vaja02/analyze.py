# analyze.py
import os
import csv
import math
import cv2
import pandas as pd

CSV_IN  = r"output\summary.csv"   # path to your summary
CSV_OUT = r"output\analysis.csv"  # enriched report

def safe_read(path):
    img = cv2.imread(path, cv2.IMREAD_COLOR)
    return img

def psnr_with_crop(orig, dec):
    ho, wo = orig.shape[:2]
    hd, wd = dec.shape[:2]
    # crop decoded to original if padded
    if (hd, wd) != (ho, wo):
        dec = dec[:ho, :wo]
        if dec.shape[:2] != (ho, wo):
            dec = cv2.resize(dec, (wo, ho), interpolation=cv2.INTER_AREA)
    return cv2.PSNR(orig, dec)

def kb(nbytes):
    return nbytes / 1024.0

def main():
    if not os.path.exists(CSV_IN):
        print(f"Missing: {CSV_IN}")
        return

    df = pd.read_csv(CSV_IN)
    rows_out = []
    last_orig = None

    for _, r in df.iterrows():
        orig_path = r["original"]
        bin_path  = r["compressed_bin"]
        dec_path  = r["decompressed_png"]
        w, h      = int(r["width"]), int(r["height"])
        fac       = int(r["factor"])

        # file sizes
        try:
            orig_bytes = os.path.getsize(orig_path)
        except OSError:
            print(f"Skip (missing original): {orig_path}")
            continue
        try:
            comp_bytes = os.path.getsize(bin_path)
        except OSError:
            print(f"Skip (missing compressed): {bin_path}")
            continue

        # compression ratio and savings
        ratio = (orig_bytes / comp_bytes) if comp_bytes > 0 else math.inf
        saving_pct = 100.0 * (1.0 - (comp_bytes / orig_bytes))

        # bits-per-pixel for compressed stream (over RGB image area)
        total_pixels = w * h
        bpp = (8.0 * comp_bytes) / total_pixels if total_pixels > 0 else float('nan')

        # PSNR
        orig = safe_read(orig_path)
        dec  = safe_read(dec_path)
        if orig is None or dec is None:
            psnr = float('nan')
        else:
            psnr = psnr_with_crop(orig, dec)

        # print compact line-grouped by original
        if last_orig != orig_path:
            print()  # blank line between images
            print(orig_path, f"({w}x{h})")
            last_orig = orig_path
        print(f"  f={fac:>2} | PSNR={psnr:6.2f} dB | orig={kb(orig_bytes):8.1f} KB | "
              f"comp={kb(comp_bytes):8.1f} KB | ratio={ratio:6.2f}x | save={saving_pct:6.1f}% | bpp={bpp:5.2f}")

        rows_out.append({
            "original": orig_path,
            "width": w,
            "height": h,
            "factor": fac,
            "psnr_db": f"{psnr:.4f}",
            "orig_bytes": orig_bytes,
            "comp_bytes": comp_bytes,
            "orig_kb": f"{kb(orig_bytes):.2f}",
            "comp_kb": f"{kb(comp_bytes):.2f}",
            "compression_ratio": f"{ratio:.4f}",
            "size_saving_percent": f"{saving_pct:.2f}",
            "compressed_bpp": f"{bpp:.4f}",
            "compressed_bin": bin_path,
            "decompressed_png": dec_path,
        })

    # write enriched CSV
    os.makedirs(os.path.dirname(CSV_OUT), exist_ok=True)
    with open(CSV_OUT, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows_out[0].keys()))
        writer.writeheader()
        writer.writerows(rows_out)

    print(f"\nSaved detailed report -> {CSV_OUT}")

if __name__ == "__main__":
    main()
