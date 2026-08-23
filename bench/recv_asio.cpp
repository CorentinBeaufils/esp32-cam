#include "recv/receiver.hpp"
#include "bench/run_report.hpp"

#include <asio.hpp>

#include <sys/resource.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// recv_asio — TON récepteur asio, instrumenté pour le banc (TP-P4).
//
// Même CLI et même sortie CSV que recv_baseline, pour que run_bench.sh pilote
// les deux à l'identique et qu'on aligne les lignes. La SEULE différence avec le
// vrai `receiver`, c'est qu'ici on branche bench::RunReport sur le callback
// on_frame (déjà exposé par rx::Receiver) et qu'on s'arrête sur idle.
//
//   recv_asio <port> [idle_ms=1000]
//
// À noter (différence de POLITIQUE, pas de mesure) : ton Reassembler REJETTE les
// fragments au CRC KO et ne garde que 2 trames en vol (le plus récent gagne).
// Donc une trame corrompue n'est jamais émise -> elle compte comme PERDUE ici,
// alors que le baseline la livre en la marquant `corrupt`. Pour comparer le
// DÉBIT/CPU proprement, lance le balayage sans corruption (CORRUPT=0) : la seule
// perte est alors celle induite par la charge (débordement du tampon noyau),
// que les deux comptent de la même façon (trous de frame_id).
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

// Chien de garde : coupe l'io_context apres `idle_ms` sans nouvelle trame (une
// fois qu'au moins une est arrivee). Fonction LIBRE prenant des references (pas
// une lambda-coroutine : ca eviterait le piege de capture detruite).
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

    // Fenetre active (1er -> dernier paquet) pour un CPU% honnete, comme baseline.
    bool   got_any  = false;
    double cpu0     = 0.0;
    double wall0    = 0.0;
    double wall_last = 0.0;

    rx::Receiver receiver(io, port);
    receiver.on_frame = [&](const cam::Frame& f) {
        if (!got_any) { got_any = true; cpu0 = cpu_ms_self(); wall0 = now_ms(); }
        wall_last = now_ms();
        // Les trames emises sont completes ET valides (CRC OK par construction du
        // Reassembler) -> crc_ok = true.
        report.on_frame(f.frame_id, wall_last, true);
    };

    receiver.start();
    asio::co_spawn(io, watchdog(receiver, io, got_any, wall_last, idle_ms),
                   asio::detached);
    io.run();   // un seul thread -> comparable au baseline mono-thread

    const double cpu  = got_any ? (cpu_ms_self() - cpu0) : 0.0;
    const double wall = (wall_last > wall0) ? (wall_last - wall0) : 0.0;
    const double cpu_pct = (wall > 0.0) ? (100.0 * cpu / wall) : 0.0;

    std::printf("impl,cpu_ms,cpu_pct,%s\n", bench::RunReport::csv_header().c_str());
    std::printf("asio,%.1f,%.1f,%s\n", cpu, cpu_pct, report.to_csv().c_str());
    return 0;
}
