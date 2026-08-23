#include "cam/protocol.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// replayer — GENERATEUR DE CHARGE du banc.
//
// Émet un flux UDP SYNTHÉTIQUE et REPRODUCTIBLE (graine fixe) sur loopback, au
// débit / taille de ton choix, et peut injecter pertes et corruption. C'est le
// "même flux pour tout le monde" : on le rejoue à l'identique contre chaque
// récepteur, sans jamais toucher le vrai réseau (127.0.0.1 ne quitte pas la
// machine -> zéro risque de DoS, et aucune perte parasite : c'est TOI qui
// décides des pertes).
//
// Il réutilise le VRAI protocole (cam::fragment) : vrais en-têtes 30 octets,
// vrais frame_id, vrai CRC32 -> ce que mesure le récepteur est exact.
//
//   replayer <host> <port> <fps> <frame_bytes> <seconds>
//            [loss_pct=0] [corrupt_pct=0] [seed=1]
//
// Pas d'asio : un simple socket UDP bloquant + une cadence steady_clock.
//
// Note v1 : UN flux. Pour monter la charge, joue sur <fps> et <frame_bytes>
// (plus de paquets/s), et épingle le récepteur sur un cœur faible (taskset +
// hog). Le fan-out N flux est la molette suivante.
// ---------------------------------------------------------------------------

namespace {

std::uint64_t now_us() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
            "usage: %s <host> <base_port> <fps> <frame_bytes> <seconds> "
            "[loss_pct=0] [corrupt_pct=0] [seed=1] [streams=1]\n"
            "  streams>1 : fan-out sur base_port..base_port+streams-1 (fps PAR flux)\n",
            argv[0]);
        return 2;
    }
    const std::string host = argv[1];
    const int         base_port = std::atoi(argv[2]);
    const double      fps  = std::atof(argv[3]);
    const std::size_t frame_bytes = static_cast<std::size_t>(std::atoll(argv[4]));
    const double      seconds     = std::atof(argv[5]);
    const double      loss_pct    = (argc > 6) ? std::atof(argv[6]) : 0.0;
    const double      corrupt_pct = (argc > 7) ? std::atof(argv[7]) : 0.0;
    const unsigned    seed        = (argc > 8) ? static_cast<unsigned>(std::atoll(argv[8])) : 1u;
    const int         streams     = (argc > 9) ? std::atoi(argv[9]) : 1;

    if (fps <= 0.0 || seconds <= 0.0 || streams < 1) {
        std::fprintf(stderr, "fps, seconds > 0 et streams >= 1\n");
        return 2;
    }

    // Un socket source, N destinations (base_port + s). Tous les flux partagent
    // la meme sequence de frame_id -> les datagrammes d'une frame sont
    // identiques d'un flux a l'autre : on fragmente UNE fois, on envoie a chacun.
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { std::perror("socket"); return 1; }
    in_addr host_addr{};
    if (::inet_pton(AF_INET, host.c_str(), &host_addr) != 1) {
        std::fprintf(stderr, "host invalide: %s\n", host.c_str());
        ::close(fd);
        return 2;
    }
    std::vector<sockaddr_in> dsts(static_cast<std::size_t>(streams));
    for (int s = 0; s < streams; ++s) {
        dsts[s].sin_family = AF_INET;
        dsts[s].sin_addr   = host_addr;
        dsts[s].sin_port   = htons(static_cast<std::uint16_t>(base_port + s));
    }

    std::mt19937 gen(seed);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_int_distribution<int>     byte(0, 255);

    const std::uint64_t total_frames =
        static_cast<std::uint64_t>(fps * seconds);
    const auto t0 = std::chrono::steady_clock::now();
    const double period_ms = 1000.0 / fps;

    std::uint64_t sent_pkts = 0, dropped_pkts = 0, corrupted_pkts = 0;

    // Charge utile générée UNE SEULE FOIS (le récepteur ne décode pas le
    // contenu : il mesure la SÉQUENCE et le CRC). Régénérer 20 000 octets
    // aléatoires par frame ferait du générateur le goulot d'étranglement bien
    // avant le récepteur. On paie le RNG une fois, puis on ne fait plus que
    // fragmenter + envoyer.
    std::vector<std::uint8_t> jpeg(frame_bytes);
    for (auto& b : jpeg) b = static_cast<std::uint8_t>(byte(gen));

    for (std::uint64_t f = 1; f <= total_frames; ++f) {
        auto datagrams = cam::fragment(static_cast<std::uint32_t>(f), now_us(),
                                       jpeg.data(), jpeg.size());
        for (auto& dg : datagrams) {
            for (int s = 0; s < streams; ++s) {   // meme frame vers chaque flux
                if (loss_pct > 0.0 && unit(gen) * 100.0 < loss_pct) {
                    ++dropped_pkts;                       // perte simulée : on n'envoie pas
                    continue;
                }
                if (corrupt_pct > 0.0 && dg.size() > cam::HEADER_SIZE
                    && unit(gen) * 100.0 < corrupt_pct) {
                    // Copie locale pour ne pas abimer le datagramme des autres flux.
                    std::vector<std::uint8_t> bad(dg);
                    bad[cam::HEADER_SIZE] ^= 0xFF;         // CRC KO
                    ++corrupted_pkts;
                    ::sendto(fd, bad.data(), bad.size(), 0,
                             reinterpret_cast<sockaddr*>(&dsts[s]), sizeof(dsts[s]));
                } else {
                    ::sendto(fd, dg.data(), dg.size(), 0,
                             reinterpret_cast<sockaddr*>(&dsts[s]), sizeof(dsts[s]));
                }
                ++sent_pkts;
            }
        }

        // Cadence : viser t0 + f * période.
        const auto target = t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double, std::milli>(static_cast<double>(f) * period_ms));
        std::this_thread::sleep_until(target);
    }

    ::close(fd);
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();
    const double achieved_fps = (elapsed > 0.0)
        ? static_cast<double>(total_frames) / elapsed : 0.0;
    std::fprintf(stderr,
        "[replayer] offert: streams=%d frames/flux=%llu fps_cible/flux=%.1f "
        "frame_bytes=%zu duree=%.1fs  (fps agrege=%.1f)\n"
        "[replayer] REEL  : elapsed=%.2fs fps_atteint/flux=%.1f%s\n"
        "[replayer] paquets: envoyes=%llu perdus(sim)=%llu corrompus(sim)=%llu "
        "loss=%.1f%% corrupt=%.1f%% seed=%u\n",
        streams, (unsigned long long)total_frames, fps, frame_bytes, seconds,
        fps * streams,
        elapsed, achieved_fps,
        (achieved_fps < fps * 0.95 ? "  <-- generateur sature (n'atteint pas la cible/flux)" : ""),
        (unsigned long long)sent_pkts, (unsigned long long)dropped_pkts,
        (unsigned long long)corrupted_pkts, loss_pct, corrupt_pct, seed);
    return 0;
}
