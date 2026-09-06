#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_bench.sh - throughput sweep for the TP-P4 benchmark.
#
# For each target fps: launches recv_baseline (in the background), launches
# replayer, collects the receiver's CSV row + the fps actually achieved by the
# generator, and aggregates ONE row per run into a CSV (header written only once).
#
# Usage:
#   ./run_bench.sh [fps1 fps2 ...]                 # default: 500..5000
#
# Settings via environment variables (with their default values):
#   BIN=./build-rel/bench   directory of the binaries (build in Release!)
#   PORT=9000  FRAME_BYTES=20000  SECONDS_RUN=5  IDLE_MS=800  SEED=1
#   LOSS=0  CORRUPT=0        injected loss / corruption (%)
#   OUT=bench.csv            output file
#   RECV_CPU=  GEN_CPU=      pinning cores (taskset); empty = no pinning
#   HOG=0                    HOG=1 + RECV_CPU=N -> launches a CPU hog on that core
#   REPEATS=1                number of passes per point ('pass' column; ~5 recommended)
#   RECV_BIN=$BIN/recv_baseline   receiver under test (point at recv_asio for your impl)
#
# "Constrained environment" example (receiver starved on core 3):
#   RECV_CPU=3 GEN_CPU=5 HOG=1 OUT=baseline.csv ./run_bench.sh 1000 2000 3000 4000 5000
# ---------------------------------------------------------------------------
set -u

BIN="${BIN:-./build-rel/bench}"
# Receiver under test: by default the baseline; point RECV_BIN at recv_asio to
# measure your implementation (the impl self-labels in the CSV).
RECV_BIN="${RECV_BIN:-$BIN/recv_baseline}"
REPLAYER_BIN="${REPLAYER_BIN:-$BIN/replayer}"
PORT="${PORT:-9000}"
FRAME_BYTES="${FRAME_BYTES:-20000}"
SECONDS_RUN="${SECONDS_RUN:-5}"
IDLE_MS="${IDLE_MS:-800}"
SEED="${SEED:-1}"
LOSS="${LOSS:-0}"
CORRUPT="${CORRUPT:-0}"
OUT="${OUT:-bench.csv}"
RECV_CPU="${RECV_CPU:-}"
GEN_CPU="${GEN_CPU:-}"
HOG="${HOG:-0}"
REPEATS="${REPEATS:-1}"    # number of passes per point ('pass' column; median to aggregate)

FPS_LIST=("$@")
if [ ${#FPS_LIST[@]} -eq 0 ]; then FPS_LIST=(500 1000 2000 3000 4000 5000); fi

if [ ! -x "$RECV_BIN" ] || [ ! -x "$REPLAYER_BIN" ]; then
  echo "ERROR: binaries not found (RECV_BIN='$RECV_BIN', REPLAYER_BIN='$REPLAYER_BIN')." >&2
  echo "  Build first:  cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel -j" >&2
  echo "  (or point BIN=... / RECV_BIN=... to the right folder)" >&2
  exit 1
fi

# taskset prefixes (arrays so as not to break on spaces).
RECV_PREFIX=(); [ -n "$RECV_CPU" ] && RECV_PREFIX=(taskset -c "$RECV_CPU")
GEN_PREFIX=();  [ -n "$GEN_CPU" ]  && GEN_PREFIX=(taskset -c "$GEN_CPU")

# Optional CPU hog on the receiver's core.
HOGPID=""
cleanup() { [ -n "$HOGPID" ] && kill "$HOGPID" 2>/dev/null; }
trap cleanup EXIT INT TERM
if [ "$HOG" = "1" ]; then
  if [ -z "$RECV_CPU" ]; then echo "HOG=1 requires RECV_CPU=<core>." >&2; exit 2; fi
  taskset -c "$RECV_CPU" yes > /dev/null &
  HOGPID=$!
  echo "[bench] CPU hog on core $RECV_CPU (pid $HOGPID)"
fi

echo "pass,target_fps,offered_fps_real,impl,cpu_ms,cpu_pct,delivered,unique,lost,corrupt,duplicate,reordered,seconds,fps,loss_pct,jitter_ms" > "$OUT"

# Passes in the OUTER loop (one pass = one full sweep), fps in the inner loop:
# this scatters slow drift (thermal, other processes) across all the points
# instead of concentrating it on a single one.
for P in $(seq 1 "$REPEATS"); do
  echo "[bench] === pass ${P}/${REPEATS} ==="
  for FPS in "${FPS_LIST[@]}"; do
    RECVOUT="$(mktemp)"
    "${RECV_PREFIX[@]}" "$RECV_BIN" "$PORT" "$IDLE_MS" > "$RECVOUT" 2>/dev/null &
    RPID=$!
    sleep 0.3
    REAL="$("${GEN_PREFIX[@]}" "$REPLAYER_BIN" 127.0.0.1 "$PORT" "$FPS" "$FRAME_BYTES" "$SECONDS_RUN" "$LOSS" "$CORRUPT" "$SEED" 2>&1 \
            | sed -n 's/.*fps_atteint=\([0-9.]*\).*/\1/p')"
    wait "$RPID"
    DATA="$(tail -n1 "$RECVOUT")"     # 2nd line = the data row of the report
    rm -f "$RECVOUT"
    echo "${P},${FPS},${REAL:-NA},${DATA}" >> "$OUT"
    echo "   [p${P}] ${FPS} -> ${DATA}"
  done
done

echo "[bench] written to $OUT :"
column -s, -t "$OUT" 2>/dev/null || cat "$OUT"
