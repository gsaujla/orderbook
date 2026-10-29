"""Plot a latency histogram CSV written by bench_synthetic --csv.

    python3 tools/plot_latency.py build/release/latency.csv [out.png]
"""
import csv
import sys

import matplotlib.pyplot as plt

path = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else path.rsplit(".", 1)[0] + ".png"

rows = list(csv.DictReader(open(path)))
upper = [int(r["upper_ns"]) for r in rows]
count = [int(r["count"]) for r in rows]
total = sum(count)

# Fraction of events slower than each bucket, on log scales so the tail is visible.
slower, seen = [], 0
for c in count:
    seen += c
    slower.append(max(total - seen, 0.5) / total)

fig, ax = plt.subplots(figsize=(7, 4))
ax.step([max(u, 1) for u in upper], slower, where="post")
ax.set_xscale("log")
ax.set_yscale("log")
ax.set_xlabel("latency (ns)")
ax.set_ylabel("fraction of events slower")
for p in (0.5, 0.99, 0.999):
    ax.axhline(1 - p, color="grey", lw=0.5, ls="--")
    ax.annotate(f"p{p * 100:g}", (ax.get_xlim()[0], 1 - p), fontsize=8, va="bottom")
ax.set_title(f"per-event latency, {total:,} events")
fig.tight_layout()
fig.savefig(out, dpi=150)
print(f"wrote {out}")
