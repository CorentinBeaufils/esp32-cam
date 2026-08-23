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
// recv_asio_pool — asio a l'echelle, VERSION PROD : N sockets multiplexes sur
// UN io_context, mais tourne par un POOL de M threads (le design que tu voulais
// des le depart). On garde le multiplexage d'asio ET on prend plusieurs coeurs.
//
// M s'auto-regle sur le nombre de coeurs autorises par l'affinite du processus
// (sched_getaffinity) : pinne-le sur 2 coeurs -> 2 threads run(). Ainsi il
// "prend son budget" tout seul, meme CLI que les autres :
//
//   taskset -c 3,4 recv_asio_pool <base_port> <streams> [idle_ms=1000]
//
// A comparer a recv_baseline_mt SUR LE MEME budget de coeurs (taskset -c 3,4) :
// c'est le vrai match, pool async vs thread-par-socket, a silicium egal.
//
// Surete : chaque socket n'a qu'UNE operation async en vol a la fois (la
// coroutine loop() re-arme sequentiellement), donc les handlers d'une meme
// socket ne s'executent JAMAIS en parallele, meme sur un pool -> l'etat par
// socket (son Reassembler, son RunReport) est touche par un seul thread a la
// fois. Pas besoin de strand ici. Seuls les compteurs GLOBAUX de la fenetre
// active sont partages entre threads -> atomics.
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
double g_cpu0 = 0.0, g_wall0 = 0.0;   // ecrits une seule fois (par le gagnant du CAS)

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

    const int pool = affinity_cpu_count();   // 1 thread run() par coeur autorise

    asio::io_context io;
    std::vector<bench::RunReport> reports(static_cast<std::size_t>(streams));
    std::vector<std::unique_ptr<rx::Receiver>> rcvs;
    rcvs.reserve(static_cast<std::size_t>(streams));

    const char* rbenv = std::getenv("RCVBUF");
    const int rb = rbenv ? std::atoi(rbenv) : 0;   // SO_RCVBUF optionnel (octets)
    for (int s = 0; s < streams; ++s) {
        auto rcv = std::make_unique<rx::Receiver>(io,
            static_cast<unsigned short>(base_port + s));
        if (rb > 0) {
            const int act = rcv->set_recv_buffer_bytes(rb);
            if (s == 0) std::fprintf(stderr, "[recv_asio_pool] SO_RCVBUF demande=%d effectif=%d\n", rb, act);
        }
        bench::RunReport* rep = &reports[static_cast<std::size_t>(s)];
        rcv->on_frame = [rep](const cam::Frame& f) {
            bool expected = false;
            if (g_any.compare_exchange_strong(expected, true)) {
                g_cpu0 = cpu_ms_self();   // une seule fois, par le 1er handler
                g_wall0 = now_ms();
            }
            const double t = now_ms();
            g_last.store(t);
            rep->on_frame(f.frame_id, t, true);   // etat par-socket : mono-thread
        };
        rcv->start();
        rcvs.push_back(std::move(rcv));
    }

    asio::co_spawn(io, watchdog(rcvs, io, idle_ms), asio::detached);

    // Le POOL : M threads tournent le MEME io_context. Les completions des N
    // sockets sont distribuees sur les threads libres -> multicoeur.
    std::vector<std::thread> workers;
    for (int i = 1; i < pool; ++i) workers.emplace_back([&io]{ io.run(); });
    io.run();                                  // ce thread compte aussi
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

    std::fprintf(stderr, "[recv_asio_pool] pool=%d threads (coeurs autorises)\n", pool);
    std::printf("impl,streams,cpu_ms,cpu_pct,delivered,lost,corrupt,fps,loss_pct,jitter_ms\n");
    std::printf("asio_pool,%d,%.1f,%.1f,%llu,%llu,%llu,%.1f,%.4f,%.4f\n",
                streams, cpu, cpu_pct,
                (unsigned long long)delivered, (unsigned long long)lost,
                (unsigned long long)corrupt, fps_sum, loss_pct, jitter_avg);
    return 0;
}
