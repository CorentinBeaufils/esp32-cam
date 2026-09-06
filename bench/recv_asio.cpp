#include "recv/receiver.hpp"
#include "bench/run_report.hpp"

#include <asio.hpp>

#include <sys/resource.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// recv_asio - asio receiver instrumented for the benchmark.
//
// Same CLI and same CSV output as recv_baseline, so that run_bench.sh drives
// both identically and we can line the rows up. The ONLY difference from the
// real `receiver` is that here we wire bench::RunReport onto the on_frame
// callback (already exposed by rx::Receiver) and stop on idle.
//
//   recv_asio <port> [idle_ms=1000]
//
// Note (a difference in POLICY, not in measurement): your Reassembler REJECTS
// fragments with a bad CRC and keeps only 2 frames in flight (most recent
// wins). So a corrupted frame is never emitted -> it counts as LOST here,
// whereas the baseline delivers it and marks it `corrupt`. To compare
// THROUGHPUT/CPU cleanly, run the sweep without corruption (CORRUPT=0): the only
// loss is then the one induced by load (kernel buffer overflow), which both
// count the same way (gaps in frame_id).
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
    const double u = ru.ru_utime.tv_sec * 1000.0 + ru.ru_utime.tv_usec / 1000.0;
    const double s = ru.ru_stime.tv_sec * 1000.0 + ru.ru_stime.tv_usec / 1000.0;
    return u + s;
}

// Watchdog: shuts down the io_context after `idle_ms` with no new frame (once
// at least one has arrived). A FREE function taking references (not a
// coroutine-lambda: that avoids the destroyed-capture pitfall).
asio::awaitable<void> watchdog(rx::Receiver& rcv, asio::io_context& io,
                               const bool& got_any, const double& last_ms,
                               int idle_ms) {
    auto ex = co_await asio::this_coro::executor;
    asio::steady_timer timer(ex);
    for (;;) {
        timer.expires_after(std::chrono::milliseconds(200));
        co_await timer.async_wait(asio::use_awaitable);
        if (got_any && (now_ms() - last_ms) > static_cast<double>(idle_ms)) {
            rcv.stop();
            io.stop();
            co_return;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <port> [idle_ms=1000]\n", argv[0]);
        return 2;
    }
    const unsigned short port =
        static_cast<unsigned short>(std::atoi(argv[1]));
    const int idle_ms = (argc > 2) ? std::atoi(argv[2]) : 1000;

    asio::io_context io;
    bench::RunReport report;

    // Active window (first -> last packet) for an honest CPU%, like the baseline.
    bool   got_any  = false;
    double cpu0     = 0.0;
    double wall0    = 0.0;
    double wall_last = 0.0;

    rx::Receiver receiver(io, port);
    receiver.on_frame = [&](const cam::Frame& f) {
        if (!got_any) { got_any = true; cpu0 = cpu_ms_self(); wall0 = now_ms(); }
        wall_last = now_ms();
        // The emitted frames are complete AND valid (CRC OK by construction of
        // the Reassembler) -> crc_ok = true.
        report.on_frame(f.frame_id, wall_last, true);
    };

    receiver.start();
    asio::co_spawn(io, watchdog(receiver, io, got_any, wall_last, idle_ms),
                   asio::detached);
    io.run();   // a single thread -> comparable to the single-threaded baseline

    const double cpu  = got_any ? (cpu_ms_self() - cpu0) : 0.0;
    const double wall = (wall_last > wall0) ? (wall_last - wall0) : 0.0;
    const double cpu_pct = (wall > 0.0) ? (100.0 * cpu / wall) : 0.0;

    std::printf("impl,cpu_ms,cpu_pct,%s\n", bench::RunReport::csv_header().c_str());
    std::printf("asio,%.1f,%.1f,%s\n", cpu, cpu_pct, report.to_csv().c_str());
    return 0;
}
