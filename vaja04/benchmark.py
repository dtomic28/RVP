import subprocess
import time
import random
import csv
import os
from pathlib import Path
from PIL import Image
import numpy as np
import math

BINARY = "/Users/dtomic/CLionProjects/RVP/cmake-build-debug/vaja03/vaja03"

BASE = Path(__file__).parent
INPUT_DIR = BASE / "input"
OUTPUT_DIR = BASE / "output"
OUTPUT_DIR.mkdir(exist_ok=True)

CSV_FILE = BASE / "results.csv"

def psnr(img1, img2):
    a = np.array(img1, dtype=np.float32)
    b = np.array(img2, dtype=np.float32)
    mse = np.mean((a - b) ** 2)
    if mse == 0:
        return 99.0
    return 20 * math.log10(255.0 / math.sqrt(mse))

files = list(INPUT_DIR.glob("*.bmp"))
if len(files) < 10:
    raise RuntimeError("Need at least 10 BMP images in /input")

files = random.sample(files, 10)

rows = []

for i, bmp in enumerate(files, start=1):
    print(f"Processing {bmp.name}")

    # where your executable writes compressed when called with full bmp path:
    produced_comp = bmp.with_suffix(".flocic")      # inside input/
    comp_at_base = BASE / produced_comp.name        # we keep it next to script

    # decompressed output we want
    decompressed = OUTPUT_DIR / bmp.name

    # --- Compression ---
    t0 = time.perf_counter()
    subprocess.run([BINARY, "c", str(bmp)], check=True)
    t1 = time.perf_counter()

    if not produced_comp.exists():
        raise FileNotFoundError(f"Compressed file not produced: {produced_comp}")

    os.replace(produced_comp, comp_at_base)

    # --- Decompression ---
    t2 = time.perf_counter()
    subprocess.run([BINARY, "d", str(comp_at_base)], check=True)
    t3 = time.perf_counter()

    # your current C++ produces: <stem>._dec.bmp (note the dot)
    produced_dec_1 = comp_at_base.with_name(comp_at_base.stem + "._dec.bmp")
    # in case you later fix it to <stem>_dec.bmp, support that too:
    produced_dec_2 = comp_at_base.with_name(comp_at_base.stem + "_dec.bmp")

    if produced_dec_1.exists():
        produced_dec = produced_dec_1
    elif produced_dec_2.exists():
        produced_dec = produced_dec_2
    else:
        raise FileNotFoundError(
            f"Decompressed BMP not found. Tried:\n"
            f"  {produced_dec_1}\n"
            f"  {produced_dec_2}"
        )

    os.replace(produced_dec, decompressed)

    # --- Metrics ---
    orig_size = bmp.stat().st_size
    comp_size = comp_at_base.stat().st_size
    ratio = orig_size / comp_size if comp_size else 0.0

    img1 = Image.open(bmp).convert("L")
    img2 = Image.open(decompressed).convert("L")
    quality = psnr(img1, img2)

    rows.append([
        i,
        bmp.name,
        orig_size,
        comp_size,
        f"{ratio:.3f}",
        f"{(t1 - t0)*1000:.2f}",
        f"{(t3 - t2)*1000:.2f}",
        f"{quality:.2f}",
    ])

with open(CSV_FILE, "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow([
        "#",
        "Datoteka",
        "Velikost original (B)",
        "Velikost stisnjena (B)",
        "Razmerje (orig./stisn.)",
        "Čas kompresije (ms)",
        "Čas dekompresije (ms)",
        "PSNR (dB)"
    ])
    writer.writerows(rows)

print("\nDone.")
print("Results written to:", CSV_FILE)
