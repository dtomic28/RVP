#!/usr/bin/env python3
"""
Generate graphs from compression_results.csv into graphs/

Creates:
  graphs/ratio_grouped_bars.png
  graphs/compress_time_scatter_avg.png
  graphs/decompress_time_scatter_avg.png
"""

import os
import sys
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

CSV_PATH = "compression_results.csv"
OUT_DIR = "graphs"

# ---------- helpers ----------

def load_data(path: str) -> pd.DataFrame:
    if not os.path.exists(path):
        print(f"ERROR: '{path}' not found.", file=sys.stderr)
        sys.exit(1)
    df = pd.read_csv(path)
    df.columns = [c.strip() for c in df.columns]
    # ensure correct types
    df["N"] = df["N"].astype(int)
    df["M"] = df["M"].astype(int)
    for c in ["input_size","output_size","ratio","compress_time","decompress_time"]:
        df[c] = pd.to_numeric(df[c], errors="coerce")
    return df.sort_values(["N", "M"]).reset_index(drop=True)

def ensure_outdir():
    os.makedirs(OUT_DIR, exist_ok=True)

def set_style():
    plt.rcParams.update({
        "figure.figsize": (9, 5.5),
        "figure.dpi": 160,
        "axes.titlesize": 16,
        "axes.labelsize": 13,
        "xtick.labelsize": 11,
        "ytick.labelsize": 11,
        "legend.fontsize": 11,
        "axes.grid": True,
        "grid.alpha": 0.3,
        "grid.linestyle": "--",
        "axes.spines.top": False,
        "axes.spines.right": False,
        "font.family": "DejaVu Sans"
    })

def savefig(path: str):
    plt.tight_layout()
    plt.savefig(path, dpi=300, bbox_inches="tight")
    plt.close()

def _format_seconds(x, _pos):
    if x < 0.001:
        return f"{x*1e6:.0f} µs"
    if x < 1:
        return f"{x*1e3:.0f} ms"
    return f"{x:.2f} s"

# ---------- plots ----------

def plot_ratio_grouped_bars(df: pd.DataFrame, out_png: str):
    N_vals = sorted(df["N"].unique())
    M_vals = sorted(df["M"].unique())
    width = 0.18
    x = np.arange(len(N_vals))

    fig, ax = plt.subplots()
    for k, M in enumerate(M_vals):
        y = [df[(df["N"]==N) & (df["M"]==M)]["ratio"].values[0] for N in N_vals]
        ax.bar(x + (k - (len(M_vals)-1)/2)*width, y, width, label=f"M={M}")

    ax.set_title("Compression ratio by N (grouped by M)")
    ax.set_xlabel("N")
    ax.set_ylabel("Compression ratio")
    ax.set_xticks(x)
    ax.set_xticklabels([str(n) for n in N_vals])
    ax.legend(ncol=len(M_vals), frameon=False, loc="upper center", bbox_to_anchor=(0.5, 1.15))
    savefig(out_png)

def plot_time_scatter_avg(df: pd.DataFrame, metric: str, title: str, out_png: str):
    fig, ax = plt.subplots()

    # Scatter points per M
    colors = plt.cm.viridis(np.linspace(0.2, 0.9, len(df["M"].unique())))
    for color, (M, g) in zip(colors, df.groupby("M")):
        ax.scatter(g["N"], g[metric], color=color, s=60, label=f"M={M}", alpha=0.8, edgecolor="black", linewidth=0.4)

    # Average line across M
    avg_df = df.groupby("N")[metric].mean().reset_index()
    ax.plot(avg_df["N"], avg_df[metric], color="black", linewidth=2.5, marker="o", label="Average")

    ax.set_title(title)
    ax.set_xlabel("N")
    ax.set_ylabel(f"{'De' if 'de' in metric else 'Com'}compression time [s]")
    ax.yaxis.set_major_formatter(FuncFormatter(_format_seconds))
    ax.legend(title="M", frameon=False, loc="upper left")
    savefig(out_png)

# ---------- main ----------

def main():
    set_style()
    ensure_outdir()
    df = load_data(CSV_PATH)

    plot_ratio_grouped_bars(df, os.path.join(OUT_DIR, "ratio_grouped_bars.png"))
    plot_time_scatter_avg(df, "compress_time", "Compression time vs N (scatter + avg line)",
                          os.path.join(OUT_DIR, "compress_time_scatter_avg.png"))
    plot_time_scatter_avg(df, "decompress_time", "Decompression time vs N (scatter + avg line)",
                          os.path.join(OUT_DIR, "decompress_time_scatter_avg.png"))

    print("✅ Graphs saved in ./graphs")

if __name__ == "__main__":
    main()
