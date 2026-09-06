#include "recv/receiver.hpp"
#include "bench/run_report.hpp"

#include <asio.hpp>

#include <sched.h>
#include <sys/resource.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// recv_asio_pool - asio at scale, PROD VERSION: N sockets multiplexed onto ONE
// io_context, but driven by a POOL of M threads (the design you wanted from the
// start). We keep asio's multiplexing AND we take several cores.
//
// M auto-tunes to the number of cores allowed by the process affinity
// (sched_getaffinity): pin it to 2 cores -> 2 run() threads. This way it
// "takes its budget" on its own, same CLI as the others:
//
//   taskset -c 3,4 recv_asio_pool <base_port> <streams> [idle_ms=1000]
//
// To be compared with recv_baseline_mt ON THE SAME core budget (taskset -c 3,4):
// this is the real match, async pool vs thread-per-socket, at equal silicon.
//
// Safety: each socket has only ONE async operation in flight at a time (the
// loop() coroutine re-arms sequentially), so the handlers of the same socket
// NEVER run in parallel, even on a pool -> the per-socket state (its
// Reassembler, its RunReport) is touched by a single thread at a time. No strand
// needed here. Only the GLOBAL counters of the active window are shared between
// threads -> atomics.
// ---------------------------------------------------------------------------

namespace {

double now_ms() {
    using namespace std::chrono;
    return duration_cast<duration<double, std::milli>>(
        steady_clock::now().time_since_epoch()).count();
}
double cpu_ms_self() {
    rusage ru{};
    ::getrusage(RUSAGE_SELF, &ru);
    return (ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000.0
         + (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1000.0;
}
int affinity_cpu_count() {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        const int n = CPU_COUNT(&set);
        if (n > 0) return n;
    }
    const unsigned hc = std::thread::hardware_concurrency();
    return hc ? static_cast<int>(hc) : 1;
}

std::atomic<bool>   g_any{false};
std::atomic<double> g_last{0.0};
double g_cpu0 = 0.0, g_wall0 = 0.0;   // written only once (by the CAS winner)

asio::awaitable<void> watchdog(std::vector<std::unique_ptr<rx::Receiver>>& rcvs,
                               asio::io_context& io, int idle_ms) {
    auto ex = co_await asio::this_coro::executor;
    asio::steady_timer timer(ex);
    for (;;) {
        timer.expires_after(std::chrono::milliseconds(200));
        co_await timer.async_wait(asio::use_awaitable);
        if (g_any.load() && (now_ms() - g_last.load()) > static_cast<double>(idle_ms)) {
            for (auto& r : rcvs) r->stop();
            io.stop();
            co_return;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <base_port> <streams> [idle_ms=1000]\n", argv[0]);
        return 2;
    }
    const int base_port = std::atoi(argv[1]);
    const int streams   = std::atoi(argv[2]);
    const int idle_ms   = (argc > 3) ? std::atoi(argv[3]) : 1000;
    if (streams < 1) { std::fprintf(stderr, "streams >= 1\n"); return 2; }

    const int pool = affinity_cpu_count();   // 1 run() thread per allowed core

    asio::io_context io;
    std::vector<bench::RunReport> reports(static_cast<std::size_t>(streams));
    std::vector<std::unique_ptr<rx::Receiver>> rcvs;
    rcvs.reserve(static_cast<std::size_t>(streams));

    const char* rbenv = std::getenv("RCVBUF");
    const int rb = rbenv ? std::atoi(rbenv) : 0;   // optional SO_RCVBUF (bytes)
    for (int s = 0; s < streams; ++s) {
        auto rcv = std::make_unique<rx::Receiver>(io,
            static_cast<unsigned short>(base_port + s));
        if (rb > 0) {
            const int act = rcv->set_recv_buffer_bytes(rb);
            if (s == 0) std::fprintf(stderr, "[recv_asio_pool] SO_RCVBUF requested=%d effective=%d\n", rb, act);
        }
        bench::RunReport* rep = &reports[static_cast<std::size_t>(s)];
        rcv->on_frame = [rep](const cam::Frame& f) {
            bool expected = false;
            if (g_any.compare_exchange_strong(expected, true)) {
                g_cpu0 = cpu_ms_self();   // only once, by the first handler
                g_wall0 = now_ms();
            }
            const double t = now_ms();
            g_last.store(t);
            rep->on_frame(f.frame_id, t, true);   // per-socket state: single-threaded
        };
        rcv->start();
        rcvs.push_back(std::move(rcv));
    }

    asio::co_spawn(io, watchdog(rcvs, io, idle_ms), asio::detached);

    // The POOL: M threads run the SAME io_context. The completions of the N
    // sockets are distributed across the free threads -> multicore.
    std::vector<std::thread> workers;
    for (int i = 1; i < pool; ++i) workers.emplace_back([&io]{ io.run(); });
    io.run();                                  // this thread counts too
    for (auto& w : workers) w.join();

    const double cpu  = g_any.load() ? (cpu_ms_self() - g_cpu0) : 0.0;
    const double wall = (g_last.load() > g_wall0) ? (g_last.load() - g_wall0) : 0.0;
    const double cpu_pct = (wall > 0.0) ? (100.0 * cpu / wall) : 0.0;

    std::uint64_t delivered = 0, lost = 0, corrupt = 0, expected = 0;
    double fps_sum = 0.0, jitter_sum = 0.0;
    for (auto& r : reports) {
        const bench::Report s = r.snapshot();
        delivered += s.delivered; lost += s.lost; corrupt += s.corrupt;
        expected  += s.unique + s.lost;
        fps_sum   += s.fps;
        jitter_sum += s.jitter_ms;
    }
    const double loss_pct = expected ? (100.0 * static_cast<double>(lost) / static_cast<double>(expected)) : 0.0;
    const double jitter_avg = streams ? jitter_sum / streams : 0.0;

    std::fprintf(stderr, "[recv_asio_pool] pool=%d threads (allowed cores)\n", pool);
    std::printf("impl,streams,cpu_ms,cpu_pct,delivered,lost,corrupt,fps,loss_pct,jitter_ms\n");
    std::printf("asio_pool,%d,%.1f,%.1f,%llu,%llu,%llu,%.1f,%.4f,%.4f\n",
                streams, cpu, cpu_pct,
                (unsigned long long)delivered, (unsigned long long)lost,
                (unsigned long long)corrupt, fps_sum, loss_pct, jitter_avg);
    return 0;
}
