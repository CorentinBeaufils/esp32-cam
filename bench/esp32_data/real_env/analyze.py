#!/usr/bin/env python3
# Analysis of the ESP32-CAM "real conditions" measurements.
import csv, statistics as st
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from pathlib import Path

# Paths relative to this script (it lives in bench/esp32_data/real_env/).
SRC = Path(__file__).resolve().parent
OUT = SRC / "charts"; OUT.mkdir(parents=True, exist_ok=True)

# Okabe-Ito (colorblind-safe palette, fixed order)
BLUE, ORANGE, GREEN, VERM = "#0072B2", "#E69F00", "#009E73", "#D55E00"
INK, MUTED, GRID = "#1a1a1a", "#666666", "#dddddd"

# NB: the 02/03 filenames were swapped at capture time.
# We restore the correct PHYSICAL label here (the data itself is fine):
#   03_2_thinwall_7_meters.csv  == actually  1 thin wall, 4 m   (8.7%)
#   02_thinwall_4_meters.csv    == actually  2 thin walls, 7 m (38.5%)
FILES = [
    ("01_proche.csv",                  "Close, line of sight",      BLUE),
    ("03_2_thinwall_7_meters.csv",     "1 thin wall - 4 m",   GREEN),
    ("02_thinwall_4_meters.csv",       "2 thin walls - 7 m", ORANGE),
    ("04_1_concrete_wall_4_meters.csv","Concrete wall - 4 m",          VERM),
]

def load(fn):
    t, fps, jit, comp, lost = [], [], [], [], []
    with open(SRC / fn) as f:
        for r in csv.DictReader(f):
            t.append(int(r["t_s"])); fps.append(float(r["fps"]))
            jit.append(float(r["jitter_ms"])); comp.append(int(r["completed"]))
            lost.append(int(r["lost"]))
    return dict(t=t, fps=fps, jit=jit, comp=comp, lost=lost)

S = []
for fn, label, col in FILES:
    d = load(fn); d["label"] = label; d["col"] = col
    tot_c, tot_l = d["comp"][-1], d["lost"][-1]
    d["loss_pct"] = 100.0 * tot_l / (tot_c + tot_l) if (tot_c + tot_l) else 0.0
    d["mean_fps"] = st.mean(d["fps"]); d["mean_jit"] = st.mean(d["jit"])
    d["med_jit"] = st.median(d["jit"]); d["tot_c"], d["tot_l"] = tot_c, tot_l
    S.append(d)

print(f"{'scenario':28} {'fps':>6} {'loss%':>7} {'jit_moy':>8} {'jit_med':>8} {'comp':>6} {'lost':>6}")
for d in S:
    print(f"{d['label']:28} {d['mean_fps']:6.1f} {d['loss_pct']:7.1f} "
          f"{d['mean_jit']:8.1f} {d['med_jit']:8.1f} {d['tot_c']:6d} {d['tot_l']:6d}")

plt.rcParams.update({"font.size": 11, "axes.edgecolor": MUTED, "text.color": INK,
                     "axes.labelcolor": INK, "xtick.color": MUTED, "ytick.color": MUTED,
                     "axes.titlecolor": INK, "figure.facecolor": "white",
                     "axes.facecolor": "white"})

def style(ax):
    ax.spines[["top", "right"]].set_visible(False)
    ax.grid(axis="y", color=GRID, lw=0.8); ax.set_axisbelow(True)

# ---- Figure 1: summary (bars, one metric per panel) ----------------------
order = sorted(S, key=lambda d: d["loss_pct"], reverse=True)
labels = [d["label"] for d in order]; cols = [d["col"] for d in order]
fig, ax = plt.subplots(1, 3, figsize=(15, 4.6))
for a in ax: style(a)

vals = [d["loss_pct"] for d in order]
b = ax[0].bar(range(len(order)), vals, color=cols, width=0.62)
ax[0].set_title("Frame loss (cumulative over 60 s)", fontweight="bold", loc="left")
ax[0].set_ylabel("% lost"); ax[0].set_ylim(0, max(vals) * 1.18 + 1)
for i, v in enumerate(vals): ax[0].text(i, v + max(vals)*0.02, f"{v:.1f}%", ha="center", fontweight="bold")

vals = [d["mean_fps"] for d in order]
ax[1].bar(range(len(order)), vals, color=cols, width=0.62)
ax[1].set_title("Mean throughput (fps rendered)", fontweight="bold", loc="left")
ax[1].set_ylabel("fps"); ax[1].set_ylim(0, 30)
ax[1].axhline(25, color=MUTED, ls="--", lw=1); ax[1].text(-0.35, 25.6, "target 25", color=MUTED, ha="left", fontsize=9)
for i, v in enumerate(vals): ax[1].text(i, v + 0.4, f"{v:.1f}", ha="center", fontweight="bold")

vals = [d["med_jit"] for d in order]
ax[2].bar(range(len(order)), vals, color=cols, width=0.62)
ax[2].set_title("Jitter (median, ms)", fontweight="bold", loc="left")
ax[2].set_ylabel("ms"); ax[2].set_ylim(0, max(vals) * 1.2 + 2)
for i, v in enumerate(vals): ax[2].text(i, v + max(vals)*0.02, f"{v:.0f}", ha="center", fontweight="bold")

SHORT = {"Close, line of sight": "Close\n(line of sight)",
         "1 thin wall - 4 m": "1 wall\n4 m",
         "2 thin walls - 7 m": "2 walls\n7 m",
         "Concrete wall - 4 m": "Concrete\n4 m"}
for a in ax:
    a.set_xticks(range(len(order)))
    a.set_xticklabels([SHORT[l] for l in labels], fontsize=9.5)
fig.suptitle("ESP32-CAM under real conditions - per-scenario summary",
             fontweight="bold", x=0.012, ha="left", fontsize=14)
fig.tight_layout(rect=[0, 0, 1, 0.95])
fig.savefig(OUT / "reel_synthese.png", dpi=140, bbox_inches="tight")
print("wrote", OUT / "reel_synthese.png")

# ---- Figure 2: time series (cumulative loss + fps) -----------------------
fig, ax = plt.subplots(1, 2, figsize=(15, 4.8))
for a in ax: style(a)
for d in S:
    ax[0].plot(d["t"], d["lost"], color=d["col"], lw=2, label=d["label"])
    ax[1].plot(d["t"], d["fps"], color=d["col"], lw=1.6, label=d["label"])
ax[0].set_title("Cumulative lost frames", fontweight="bold", loc="left")
ax[0].set_xlabel("time (s)"); ax[0].set_ylabel("lost frames (cumulative)")
ax[1].set_title("Instantaneous throughput (1 s window)", fontweight="bold", loc="left")
ax[1].set_xlabel("time (s)"); ax[1].set_ylabel("fps"); ax[1].set_ylim(0, 30)
ax[0].legend(frameon=False, fontsize=9, loc="upper left")
fig.suptitle("ESP32-CAM under real conditions - over time",
             fontweight="bold", x=0.012, ha="left", fontsize=14)
fig.tight_layout(rect=[0, 0, 1, 0.95])
fig.savefig(OUT / "reel_temporel.png", dpi=140, bbox_inches="tight")
print("wrote", OUT / "reel_temporel.png")
