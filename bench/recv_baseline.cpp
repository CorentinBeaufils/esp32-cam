#include "bench/run_report.hpp"
#include "cam/protocol.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// recv_baseline — le RÉCEPTEUR ÉTALON (TP-P4).
//
// Volontairement NAÏF : un socket UDP bloquant, une boucle recvfrom, un
// réassemblage minimal (une map frame_id -> fragments reçus). AUCUN asio,
// aucune coroutine, aucun réassemblage "le plus récent gagne". C'est le point
// de comparaison "bête" face à ton récepteur soigné : à charge égale, est-ce
// que le tien fait mieux, et de combien ?
//
// Il émet le RELEVÉ COMMUN (bench::RunReport -> CSV), plus deux colonnes que
// SEUL l'exécutable connaît : l'implémentation et le CPU consommé (getrusage).
// Ton récepteur asio émettra EXACTEMENT le même format -> on aligne les lignes.
//
//   recv_baseline <port> [idle_ms=1000]
//
// S'arrête après <idle_ms> sans paquet (via SO_RCVTIMEO), une fois qu'au moins
// une trame est arrivée -> le harness lance le replayer, puis le baseline
// finit tout seul et imprime sa ligne.
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

// Réassemblage minimal d'UNE trame en cours.
struct Partial {
    std::uint16_t count = 0;      // fragments attendus
    std::uint16_t have  = 0;      // fragments distincts reçus
    bool          bad   = false;  // au moins un fragment au CRC KO
    std::vector<bool> got;        // présence par fragment_index
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <port> [idle_ms=1000]\n", argv[0]);
        return 2;
    }
    const int port    = std::atoi(argv[1]);
    const int idle_ms = (argc > 2) ? std::atoi(argv[2]) : 1000;

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { std::perror("socket"); return 1; }
    // Reutilisation du port entre deux runs d'un balayage (evite "Address
    // already in use" si un socket precedent traine encore).
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(static_cast<std::uint16_t>(port));
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind"); ::close(fd); return 1;
    }
    // Timeout de réception : sert de condition d'arrêt (fin du flux).
    timeval tv{};
    tv.tv_sec  = idle_ms / 1000;
    tv.tv_usec = (idle_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    bench::RunReport report;
    std::unordered_map<std::uint32_t, Partial> partials;
    std::vector<std::uint8_t> buf(2048);
    bool got_any = false;
    // CPU/wall bracketes sur la FENÊTRE ACTIVE (1er -> dernier paquet), pour ne
    // pas diluer le CPU% avec l'attente de demarrage ni l'idle de fin.
    double cpu0 = 0.0, wall0 = 0.0, wall_last = 0.0;

    for (;;) {
        const ssize_t n = ::recvfrom(fd, buf.data(), buf.size(), 0, nullptr, nullptr);
        if (n < 0) {
            if (got_any) break;        // timeout apres reception -> flux fini
            continue;                  // pas encore commence : on attend
        }
        if (static_cast<std::size_t>(n) < cam::HEADER_SIZE) continue;

        const cam::Header h = cam::read_header(buf.data());
        if (h.magic != cam::MAGIC) continue;
        if (static_cast<std::size_t>(n) < cam::HEADER_SIZE + h.payload_size) continue;
        if (h.fragment_count == 0 || h.fragment_index >= h.fragment_count) continue;

        if (!got_any) { got_any = true; cpu0 = cpu_ms_self(); wall0 = now_ms(); }
        wall_last = now_ms();
        const bool frag_ok =
            cam::crc32(buf.data() + cam::HEADER_SIZE, h.payload_size) == h.payload_crc;

        Partial& p = partials[h.frame_id];
        if (p.count == 0) { p.count = h.fragment_count; p.got.assign(h.fragment_count, false); }
        if (!p.got[h.fragment_index]) { p.got[h.fragment_index] = true; ++p.have; }
        if (!frag_ok) p.bad = true;

        if (p.have == p.count) {                 // trame complete
            report.on_frame(h.frame_id, now_ms(), !p.bad);
            partials.erase(h.frame_id);
        }
    }

    ::close(fd);
    const double cpu = got_any ? (cpu_ms_self() - cpu0) : 0.0;
    const double wall = (wall_last > wall0) ? (wall_last - wall0) : 0.0;
    const double cpu_pct = (wall > 0.0) ? (100.0 * cpu / wall) : 0.0;

    // Ligne commune : impl + cpu (specifiques a l'executable) puis le releve.
    std::printf("impl,cpu_ms,cpu_pct,%s\n", bench::RunReport::csv_header().c_str());
    std::printf("baseline,%.1f,%.1f,%s\n", cpu, cpu_pct, report.to_csv().c_str());
    return 0;
}
