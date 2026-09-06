**English** · [Français](benchmark.fr.md)

# UDP receiver comparison bench — report

A measured study: **is an asio receiver (coroutines) worth it compared to a naive
blocking `recvfrom` receiver**, for the ESP32-CAM video stream? A numbers-backed answer,
obtained across five rounds on a reproducible bench.

## TL;DR

- With **few active streams** (this project's case), blocking `recvfrom` per socket is
  the simplest and best-performing choice. asio brings no advantage.
- The apparent superiority of thread-per-socket in multi-stream was caused by a **receive
  buffer (`SO_RCVBUF`) that was too small**, not by the architecture: sized correctly,
  the sharded asio **matches** thread-per-socket (0% loss, comparable CPU).
- asio's real value lies in the **c10k** regime (thousands of mostly *idle* connections)
  — the exact opposite of this stream. Here, we never get there.

## Method

- **Machine**: Intel i7-14650HX (8 P-cores + 8 E-cores), WSL2 (Ubuntu).
- **Load**: a reproducible synthetic generator (`replayer`) emitting the **real protocol**
  (30-byte headers, `frame_id`, CRC32) over **loopback** (`127.0.0.1`) — no real network
  traffic, with loss/corruption injectable on demand.
- **Common metric**: `bench::RunReport` (a pure, tested library) — throughput, loss
  (`frame_id` gaps), corruption, duplicates, reordering, jitter — serialized to identical
  CSV for every receiver, so they are directly comparable.
- **Rigor**: pinned receiver (`taskset`), **10 passes** per point, median + min–max band,
  verified affinity (`taskset -cp`). Raw data in `bench/data/`.

## Receivers compared

| Name | Model |
|---|---|
| `recv_baseline` | blocking `recvfrom`, one stream |
| `recv_baseline_mt` | **one blocking thread per socket** (N threads) |
| `recv_asio_mux` | N sockets on **1 `io_context`, 1 thread** |
| `recv_asio_pool` | N sockets on **1 `io_context`, M threads** (pool) |
| `recv_asio_shard` | **M independent `io_context`, 1 per core** (sharding) |

## The five rounds

### 1. Single stream, a starved half-core (throughput sweep)

Throughput ramped from 750 to 1450 fps, receiver on a core shared with a hog.

| | baseline | asio (1 stream) |
|---|---|---|
| Loss @1450 fps | ~1.8% | ~3.2% |
| CPU | reference | ~+10% / frame |

→ The naive baseline wins: asio pays a per-packet overhead (CRC, "latest wins"
reassembly, telemetry, coroutine machinery) and saturates a little earlier.

### 2. Multi-stream, thread-per-socket vs asio 1-thread (1 core)

60 fps/stream, N from 1 to 128.

| N | `baseline_mt` loss | `asio_mux` loss |
|---|---|---|
| ≤32 | 0% | 0% |
| 64 | 0% | 3.6% |
| 128 | 0% | 15.5% |

→ The single-thread async **hits a ceiling**: 1 thread = 1 core. Thread-per-socket
spreads the load and stays loss-free.

### 3. Multi-stream, asio pool (2 threads, 1 `io_context`, 2 cores)

| N | `baseline_mt` | `asio_pool` |
|---|---|---|
| 64 | 0% | 3.3% |
| 128 | 0% | 15.5% |

→ The pool **does not help**: M threads on *the same* `io_context` contend for the same
epoll reactor (a shared lock). The same ceiling as single-thread, for ~2× the CPU at low
load.

### 4. Multi-stream, sharding (M independent `io_context`, 2 cores)

| N | `baseline_mt` | `asio_shard` | shard `cpu_pct` |
|---|---|---|---|
| 64 | 0% | 3.2% | 44% |
| 128 | 0% | 14.9% | **90%** |

→ Sharding **does not help either** — but the **tell** appears: at N=128, every receiver
plateaus **below 100% of a single core**. **CPU was never the bottleneck.** We were
throwing cores at a problem that wasn't one.

### 5. Multi-stream, sharding + `SO_RCVBUF` = 8 MB (2 cores)

| N | `asio_shard` default | `asio_shard` 8 MB | `baseline_mt` 8 MB |
|---|---|---|---|
| 64 | 3.2% | **0%** | 0% |
| 128 | 14.9% | **0%** | 0% |

→ **Root cause found.** With an 8 MB buffer, the async loss **disappears**, exactly on par
with thread-per-socket, for comparable CPU. The residual delivery shortfall (~12% at
N=128) is identical for both: it's the **generator** that throttles, not the receiver.

## Explanation

The async loss was **neither a CPU problem, nor a core problem, nor an architecture
problem**: it was **service latency under bursts**. Thread-per-socket drains each socket
*the instant* the packet arrives (the kernel wakes the thread parked on that `fd`), so the
buffer never fills. The async path adds an `epoll → dispatch → coroutine` round trip; when
every socket receives at once (a synchronized burst from the generator), the reactor serves
them in a queue and the last buffers overflow — **unless** `SO_RCVBUF` is large enough to
absorb the burst.

Note: the perfectly synchronized burst is a bench artifact (real cameras aren't
*frame-locked*). Under real conditions, the async/threads gap would be even smaller.

## Conclusion — when to use what

- **Few active streams (this project)** → **blocking threads**: simpler, lower latency,
  zero tuning. asio has no ground on which to win.
- **Correctly tuned asio** (`SO_RCVBUF`, per-core sharding) → **matches** threads on active
  streams, without surpassing them.
- **c10k** (thousands of mostly idle connections) → **asio**: where a thread per socket
  wrecks memory and the scheduler, the event loop shines. Not relevant here.

Key takeaway on tuning: on an async receiver, **sizing `SO_RCVBUF`** (and `net.core.rmem_max`
on the OS side) matters more than the number of threads.

## Reproduce

```bash
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel -j

# balayage débit (manche 1)
bash bench/run_bench.sh 750 800 850 900 950 1000 1050 1100 1150 1200 1250 1300 1350 1400 1450

# balayage multi-flux (manches 2-5), ex. sharding avec gros tampon :
sudo sysctl -w net.core.rmem_max=33554432
RCVBUF=8388608 RECV_CPU=3,4 GEN_CPU=6 REPEATS=5 FPS=60 \
  RECV_BIN=./build-rel/bench/recv_asio_shard OUT=shard_big.csv \
  bash bench/run_fanout.sh 1 2 4 8 16 32 64 128
```

Charts: `bench/comparison*.html`. Raw data: `bench/data/*.csv`.
