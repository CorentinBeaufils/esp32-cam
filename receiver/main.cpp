#include "recv/receiver.hpp"

#include <asio.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Récepteur headless : reçoit le flux UDP, réassemble, et affiche la télémétrie
// chaque seconde. Fourni complet -- il te sert à VÉRIFIER ton Receiver contre le
// simulateur. L'affichage des images (OpenCV) viendra au TP-P1c.
//
//   ./receiver [port]          (défaut 9000)
//
// À lancer AVANT le simulateur :
//   Terminal 1 :  ./receiver 9000
//   Terminal 2 :  ./simulator 127.0.0.1 9000 25 8000
// ---------------------------------------------------------------------------
namespace {

asio::awaitable<void> print_stats_loop(const rx::Receiver& receiver) {
    auto ex = co_await asio::this_coro::executor;
    asio::steady_timer timer(ex);
    while (true) {
        timer.expires_after(std::chrono::seconds(1));
        co_await timer.async_wait(asio::use_awaitable);

        const auto& t = receiver.telemetry();
        const auto& m = receiver.metrics();
        const std::uint64_t total = t.frames_completed + t.frames_lost;
        const double loss_pct =
            (total > 0) ? (100.0 * static_cast<double>(t.frames_lost) / static_cast<double>(total))
                        : 0.0;

        std::printf(
            "fps=%5.1f  latence=%5.1f ms  gigue=%4.1f ms  |  "
            "completes=%llu  perdues=%llu (%.1f%%)  corrompus=%llu  tardifs=%llu\n",
            m.fps(), m.avg_latency_ms(), m.jitter_ms(),
            static_cast<unsigned long long>(t.frames_completed),
            static_cast<unsigned long long>(t.frames_lost), loss_pct,
            static_cast<unsigned long long>(t.fragments_rejected),
            static_cast<unsigned long long>(t.fragments_late));
    }
}

} // namespace

int main(int argc, char** argv) {
    const unsigned short port =
        static_cast<unsigned short>((argc > 1) ? std::atoi(argv[1]) : 9000);

    asio::io_context io;
    rx::Receiver receiver(io, port);
    receiver.start();

    asio::co_spawn(io, print_stats_loop(receiver), asio::detached);

    std::printf("Récepteur à l'écoute sur le port %u  (Ctrl-C pour quitter)\n",
                receiver.port());
    io.run();
    return 0;
}
