#include "recv/receiver.hpp"
#include "bench/run_report.hpp"

#include <asio.hpp>

#include <sys/resource.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// recv_asio_mux — TON asio, mais a l'echelle : N flux MULTIPLEXES sur UN SEUL
// thread (banc multi-flux P4).
//
// On instancie N fois ta classe rx::Receiver, TOUTES sur le meme io_context,
// et on lance io.run() sur un unique thread : asio jongle avec les N sockets
// tout seul (async_receive_from). C'est exactement l'inverse du modele
// thread-par-socket (recv_baseline_mt) -- et c'est le terrain ou l'async est
// cense gagner quand N grimpe.
//
//   recv_asio_mux <base_port> <streams> [idle_ms=1000]
//
// Meme ligne CSV agregee que recv_baseline_mt -> comparables directement.
// Un seul thread => pas d'atomics, tout se passe sur le thread de l'io_context.
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

asio::awaitable<void> watchdog(std::vector<std::unique_ptr<rx::Receiver>>& rcvs,
                               asio::io_context& io,
                               const bool& got_any, const double& last_ms, int idle_ms) {
    auto ex = co_await asio::this_coro::executor;
    asio::steady_timer timer(ex);
    for (;;) {
        timer.expires_after(std::chrono::milliseconds(200));
        co_await timer.async_wait(asio::use_awaitable);
        if (got_any && (now_ms() - last_ms) > static_cast<double>(idle_ms)) {
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

    asio::io_context io;
    std::vector<bench::RunReport> reports(static_cast<std::size_t>(streams));
    std::vector<std::unique_ptr<rx::Receiver>> rcvs;
    rcvs.reserve(static_cast<std::size_t>(streams));

    bool   got_any  = false;
    double cpu0     = 0.0;
    double wall0    = 0.0;
    double last_ms  = 0.0;

    const char* rbenv = std::getenv("RCVBUF");
    const int rb = rbenv ? std::atoi(rbenv) : 0;   // SO_RCVBUF optionnel (octets)
    for (int s = 0; s < streams; ++s) {
        auto rcv = std::make_unique<rx::Receiver>(io,
            static_cast<unsigned short>(base_port + s));
        if (rb > 0) {
            const int act = rcv->set_recv_buffer_bytes(rb);
            if (s == 0) std::fprintf(stderr, "[recv_asio_mux] SO_RCVBUF demande=%d effectif=%d\n", rb, act);
        }
        bench::RunReport* rep = &reports[static_cast<std::size_t>(s)];
        rcv->on_frame = [&, rep](const cam::Frame& f) {
            if (!got_any) { got_any = true; cpu0 = cpu_ms_self(); wall0 = now_ms(); }
            last_ms = now_ms();
            rep->on_frame(f.frame_id, last_ms, true);
        };
        rcv->start();
        rcvs.push_back(std::move(rcv));
    }

    asio::co_spawn(io, watchdog(rcvs, io, got_any, last_ms, idle_ms), asio::detached);
    io.run();   // UN SEUL thread pour les N flux

    const double cpu  = got_any ? (cpu_ms_self() - cpu0) : 0.0;
    const double wall = (last_ms > wall0) ? (last_ms - wall0) : 0.0;
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

    std::printf("impl,streams,cpu_ms,cpu_pct,delivered,lost,corrupt,fps,loss_pct,jitter_ms\n");
    std::printf("asio_mux,%d,%.1f,%.1f,%llu,%llu,%llu,%.1f,%.4f,%.4f\n",
                streams, cpu, cpu_pct,
                (unsigned long long)delivered, (unsigned long long)lost,
                (unsigned long long)corrupt, fps_sum, loss_pct, jitter_avg);
    return 0;
}
