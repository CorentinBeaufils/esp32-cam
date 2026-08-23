#include "recv/receiver.hpp"
#include "bench/run_report.hpp"

#include <asio.hpp>

#include <pthread.h>
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
// recv_asio_shard — asio SHARDÉ : le design qui scale vraiment.
//
// Au lieu d'UN io_context partagé par M threads (le pool, qui contentionne le
// reactor), on crée M io_context INDÉPENDANTS, un par thread, chacun épinglé
// sur son cœur. Les N sockets sont réparties (round-robin) sur les M contextes.
// Chaque reactor epoll est privé -> aucun verrou partagé -> passage à l'échelle
// ~linéaire avec les cœurs. C'est le modèle nginx / un-reactor-par-cœur.
//
//   taskset -c 3,4 recv_asio_shard <base_port> <streams> [idle_ms=1000]
//
// M s'auto-règle sur les cœurs autorisés (sched_getaffinity), et chaque thread
// worker est ré-épinglé sur UN cœur précis de cet ensemble.
//
// Sûreté : chaque socket vit sur exactement un io_context/thread -> son état
// (Reassembler, RunReport) n'est jamais touché en parallèle. Seuls les
// compteurs globaux de fenêtre sont partagés -> atomics. L'arrêt se fait par
// io_context::stop() (thread-safe), sans toucher aux sockets d'un autre thread.
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
std::vector<int> allowed_cpus() {
    cpu_set_t set; CPU_ZERO(&set);
    std::vector<int> v;
    if (sched_getaffinity(0, sizeof(set), &set) == 0)
        for (int c = 0; c < CPU_SETSIZE; ++c) if (CPU_ISSET(c, &set)) v.push_back(c);
    if (v.empty()) v.push_back(0);
    return v;
}
void pin_current_thread(int cpu) {
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s);
    pthread_setaffinity_np(pthread_self(), sizeof(s), &s);
}

std::atomic<bool>   g_any{false};
std::atomic<double> g_last{0.0};
double g_cpu0 = 0.0, g_wall0 = 0.0;

asio::awaitable<void> watchdog(std::vector<std::unique_ptr<asio::io_context>>& ctxs, int idle_ms) {
    auto ex = co_await asio::this_coro::executor;
    asio::steady_timer timer(ex);
    for (;;) {
        timer.expires_after(std::chrono::milliseconds(200));
        co_await timer.async_wait(asio::use_awaitable);
        if (g_any.load() && (now_ms() - g_last.load()) > static_cast<double>(idle_ms)) {
            for (auto& c : ctxs) c->stop();   // thread-safe, arrete tous les run()
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

    const std::vector<int> cpus = allowed_cpus();
    const int M = static_cast<int>(cpus.size());

    std::vector<std::unique_ptr<asio::io_context>> ctxs;
    for (int i = 0; i < M; ++i) ctxs.push_back(std::make_unique<asio::io_context>(1));

    std::vector<bench::RunReport> reports(static_cast<std::size_t>(streams));
    std::vector<std::unique_ptr<rx::Receiver>> rcvs;
    rcvs.reserve(static_cast<std::size_t>(streams));
    const char* rbenv = std::getenv("RCVBUF");
    const int rb = rbenv ? std::atoi(rbenv) : 0;   // SO_RCVBUF optionnel (octets)
    for (int s = 0; s < streams; ++s) {
        asio::io_context& io = *ctxs[static_cast<std::size_t>(s % M)];   // round-robin
        auto rcv = std::make_unique<rx::Receiver>(io,
            static_cast<unsigned short>(base_port + s));
        if (rb > 0) {
            const int act = rcv->set_recv_buffer_bytes(rb);
            if (s == 0) std::fprintf(stderr, "[recv_asio_shard] SO_RCVBUF demande=%d effectif=%d\n", rb, act);
        }
        bench::RunReport* rep = &reports[static_cast<std::size_t>(s)];
        rcv->on_frame = [rep](const cam::Frame& f) {
            bool expected = false;
            if (g_any.compare_exchange_strong(expected, true)) {
                g_cpu0 = cpu_ms_self(); g_wall0 = now_ms();
            }
            const double t = now_ms();
            g_last.store(t);
            rep->on_frame(f.frame_id, t, true);
        };
        rcv->start();
        rcvs.push_back(std::move(rcv));
    }

    asio::co_spawn(*ctxs[0], watchdog(ctxs, idle_ms), asio::detached);

    // Un thread par io_context, épinglé sur SON cœur. ctxs[0] tourne sur le
    // thread principal (épinglé lui aussi).
    std::vector<std::thread> workers;
    for (int i = 1; i < M; ++i) {
        asio::io_context* c = ctxs[static_cast<std::size_t>(i)].get();
        const int cpu = cpus[static_cast<std::size_t>(i)];
        workers.emplace_back([c, cpu]{ pin_current_thread(cpu); c->run(); });
    }
    pin_current_thread(cpus[0]);
    ctxs[0]->run();
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
        fps_sum   += s.fps; jitter_sum += s.jitter_ms;
    }
    const double loss_pct = expected ? (100.0 * static_cast<double>(lost) / static_cast<double>(expected)) : 0.0;
    const double jitter_avg = streams ? jitter_sum / streams : 0.0;

    std::fprintf(stderr, "[recv_asio_shard] %d io_context (1/coeur) sur coeurs autorises\n", M);
    std::printf("impl,streams,cpu_ms,cpu_pct,delivered,lost,corrupt,fps,loss_pct,jitter_ms\n");
    std::printf("asio_shard,%d,%.1f,%.1f,%llu,%llu,%llu,%.1f,%.4f,%.4f\n",
                streams, cpu, cpu_pct,
                (unsigned long long)delivered, (unsigned long long)lost,
                (unsigned long long)corrupt, fps_sum, loss_pct, jitter_avg);
    return 0;
}
