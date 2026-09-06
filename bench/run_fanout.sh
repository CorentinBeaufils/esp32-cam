#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_fanout.sh - sweep by NUMBER OF STREAMS (multi-stream benchmark TP-P4).
#
# Fixes the PER-STREAM rate, varies N (number of streams), and measures the
# receiver (aggregated over the N streams). Goal: see who scales -- the
# thread-per-socket model (recv_baseline_mt) or asio multiplexing (recv_asio_mux).
#
# Idea of the experiment: pin the receiver to ONE SINGLE core. asio_mux stays 1
# thread; baseline_mt puts N threads on that core -> as N grows, it thrashes
# (context switches). That is where async should win.
#
# Usage:
#   RECV_BIN=./build-rel/bench/recv_baseline_mt OUT=mt.csv   ./run_fanout.sh 1 2 4 8 16 32 64
#   RECV_BIN=./build-rel/bench/recv_asio_mux    OUT=mux.csv  ./run_fanout.sh 1 2 4 8 16 32 64
#
# Variables (defaults):
#   BIN=./build-rel/bench   REPLAYER_BIN=$BIN/replayer   RECV_BIN=$BIN/recv_baseline_mt
#   BASE_PORT=9100  FPS=60 (per stream)  FRAME_BYTES=8000  SECONDS_RUN=5  IDLE_MS=800  SEED=1
#   RECV_CPU=3  GEN_CPU=5    (RECV_CPU empty = no pinning)
#   REPEATS=1  OUT=fanout.csv
# ---------------------------------------------------------------------------
set -u
BIN="${BIN:-./build-rel/bench}"
REPLAYER_BIN="${REPLAYER_BIN:-$BIN/replayer}"
RECV_BIN="${RECV_BIN:-$BIN/recv_baseline_mt}"
BASE_PORT="${BASE_PORT:-9100}"
FPS="${FPS:-60}"
FRAME_BYTES="${FRAME_BYTES:-8000}"
SECONDS_RUN="${SECONDS_RUN:-5}"
IDLE_MS="${IDLE_MS:-800}"
SEED="${SEED:-1}"
RECV_CPU="${RECV_CPU:-}"
GEN_CPU="${GEN_CPU:-}"
REPEATS="${REPEATS:-1}"
OUT="${OUT:-fanout.csv}"

N_LIST=("$@"); [ ${#N_LIST[@]} -eq 0 ] && N_LIST=(1 2 4 8 16 32 64)

if [ ! -x "$RECV_BIN" ] || [ ! -x "$REPLAYER_BIN" ]; then
  echo "ERROR: binaries not found (RECV_BIN='$RECV_BIN', REPLAYER_BIN='$REPLAYER_BIN')." >&2
  echo "  cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel -j" >&2
  exit 1
fi

RECV_PREFIX=(); [ -n "$RECV_CPU" ] && RECV_PREFIX=(taskset -c "$RECV_CPU")
GEN_PREFIX=();  [ -n "$GEN_CPU" ]  && GEN_PREFIX=(taskset -c "$GEN_CPU")

# The receiver's row already starts with 'impl,streams,...' -> we only prefix
# pass + offered_fps_agg, and the rest of the columns come from it.
echo "pass,offered_fps_agg,impl,streams,cpu_ms,cpu_pct,delivered,lost,corrupt,fps,loss_pct,jitter_ms" > "$OUT"

for P in $(seq 1 "$REPEATS"); do
  echo "[fanout] === pass ${P}/${REPEATS} ==="
  for N in "${N_LIST[@]}"; do
    RECVOUT="$(mktemp)"
    "${RECV_PREFIX[@]}" "$RECV_BIN" "$BASE_PORT" "$N" "$IDLE_MS" > "$RECVOUT" 2>/dev/null &
    RPID=$!
    sleep 0.3
    "${GEN_PREFIX[@]}" "$REPLAYER_BIN" 127.0.0.1 "$BASE_PORT" "$FPS" "$FRAME_BYTES" "$SECONDS_RUN" 0 0 "$SEED" "$N" >/dev/null 2>&1
    wait "$RPID"
    DATA="$(tail -n1 "$RECVOUT")"
    rm -f "$RECVOUT"
    OFF=$(awk "BEGIN{print $N*$FPS}")
    echo "${P},${OFF},${DATA}" >> "$OUT"
    echo "   [p${P}] N=${N} -> ${DATA}"
  done
done

echo "[fanout] written to $OUT :"
column -s, -t "$OUT" 2>/dev/null || cat "$OUT"
