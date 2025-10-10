import subprocess
import time
import os
import csv
import sys

# parameters
Ns = [5, 50, 500, 5000]
Ms = [5, 10, 15, 30]
exe = "C:\\users\\danijel\\CLionProjects\\RVP\\cmake-build-debug\\vaja01\\vaja01.exe"  # path to your compiled program

results = []
total = len(Ns) * len(Ms)
done = 0

for N in Ns:
    for M in Ms:
        done += 1
        outfile = f"out_N{N}_M{M}.bin"

        print(f"\n▶️  [{done}/{total}] Running N={N}, M={M}...", flush=True)

        # --- Compress ---
        start_c = time.perf_counter()
        subprocess.run([exe, "c", str(N), str(M), outfile], check=True)
        end_c = time.perf_counter()
        compress_time = end_c - start_c

        # Show progress heartbeat
        print(f"   🧩 Compressed -> {outfile} ({round(compress_time, 4)} s)", flush=True)

        # --- Get compression ratio ---
        input_size = N
        output_size = os.path.getsize(outfile)
        compression_ratio = input_size / output_size if output_size != 0 else 0

        # --- Decompress ---
        start_d = time.perf_counter()
        subprocess.run([exe, "d", outfile], check=True)
        end_d = time.perf_counter()
        decompress_time = end_d - start_d

        print(f"   🔄 Decompressed ({round(decompress_time, 4)} s)", flush=True)

        results.append({
            "N": N,
            "M": M,
            "input_size": input_size,
            "output_size": output_size,
            "ratio": compression_ratio,
            "compress_time": compress_time,
            "decompress_time": decompress_time
        })

        # Live heartbeat symbol
        print(f"   ✅ Done {done}/{total}\n", flush=True)
        sys.stdout.flush()

print("💾 Writing results to compression_results.csv ...", flush=True)
with open("compression_results.csv", "w", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=results[0].keys())
    writer.writeheader()
    writer.writerows(results)

print("\n🎉 All tests complete! Results saved to compression_results.csv")