#include "bench/run_report.hpp"
#include "cam/protocol.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// recv_baseline_mt — l'etalon "montee en charge naive" (banc multi-flux P4).
//
// Le modele bloquant qui passe a l'echelle de la seule facon qu'il connait :
// UN THREAD PAR SOCKET. N flux -> N threads, chacun bloque dans son recvfrom.
// Simple, mais chaque thread est un cout (pile, ordonnancement) : quand N
// depasse le nombre de coeurs, ca thrashe (changements de contexte).
//
// A comparer a recv_asio_mux, qui multiplexe les N sockets sur UN SEUL thread.
// C'est LA question ou l'async est cense payer : la concurrence.
//
//   recv_baseline_mt <base_port> <streams> [idle_ms=1000]
//
// Sortie : une ligne CSV AGREGEE (somme sur les N flux). Meme colonnes que
// recv_asio_mux -> comparables directement.
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

struct Partial {
    std::uint16_t count = 0, have = 0;
    bool bad = false;
    std::vector<bool> got;
};

// Horodatage global de la fenetre active, partage entre threads.
std::atomic<double> g_first{-1.0};
std::atomic<double> g_last{0.0};

void receive_one(int port, int idle_ms, bench::RunReport* report, bool verbose) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    // SO_RCVBUF optionnel (env RCVBUF, en octets) -- test du levier "tampon".
    if (const char* e = ::getenv("RCVBUF")) {
        int rb = std::atoi(e);
        if (rb > 0) {
            ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rb, sizeof(rb));
            if (verbose) {
                int act = 0; socklen_t l = sizeof(act);
                ::getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &act, &l);
                std::fprintf(stderr, "[recv_baseline_mt] SO_RCVBUF demande=%d effectif=%d (x2 noyau)\n", rb, act);
            }
        }
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { ::close(fd); return; }
    timeval tv{ idle_ms / 1000, (idle_ms % 1000) * 1000 };
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::unordered_map<std::uint32_t, Partial> partials;
    std::vector<std::uint8_t> buf(2048);
    bool got_any = false;

    for (;;) {
        const ssize_t n = ::recvfrom(fd, buf.data(), buf.size(), 0, nullptr, nullptr);
        if (n < 0) { if (got_any) break; else continue; }
        if (static_cast<std::size_t>(n) < cam::HEADER_SIZE) continue;
        const cam::Header h = cam::read_header(buf.data());
        if (h.magic != cam::MAGIC) continue;
        if (static_cast<std::size_t>(n) < cam::HEADER_SIZE + h.payload_size) continue;
        if (h.fragment_count == 0 || h.fragment_index >= h.fragment_count) continue;

        got_any = true;
        const double t = now_ms();
        double exp = g_first.load();
        if (exp < 0) g_first.compare_exchange_strong(exp, t);
        g_last.store(t);

        const bool ok = cam::crc32(buf.data() + cam::HEADER_SIZE, h.payload_size) == h.payload_crc;
        Partial& p = partials[h.frame_id];
        if (p.count == 0) { p.count = h.fragment_count; p.got.assign(h.fragment_count, false); }
        if (!p.got[h.fragment_index]) { p.got[h.fragment_index] = true; ++p.have; }
        if (!ok) p.bad = true;
        if (p.have == p.count) {
            report->on_frame(h.frame_id, t, !p.bad);
            partials.erase(h.frame_id);
        }
    }
    ::close(fd);
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

    std::vector<bench::RunReport> reports(static_cast<std::size_t>(streams));
    std::vector<std::thread> threads;
    const double cpu0 = cpu_ms_self();
    for (int s = 0; s < streams; ++s)
        threads.emplace_back(receive_one, base_port + s, idle_ms,
                             &reports[static_cast<std::size_t>(s)], s == 0);
    for (auto& th : threads) th.join();
    const double cpu = cpu_ms_self() - cpu0;

    // Agregation sur les N flux.
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
    const double wall = g_last.load() - (g_first.load() < 0 ? g_last.load() : g_first.load());
    const double cpu_pct = (wall > 0.0) ? (100.0 * cpu / wall) : 0.0;
    const double jitter_avg = streams ? jitter_sum / streams : 0.0;

    std::printf("impl,streams,cpu_ms,cpu_pct,delivered,lost,corrupt,fps,loss_pct,jitter_ms\n");
    std::printf("baseline_mt,%d,%.1f,%.1f,%llu,%llu,%llu,%.1f,%.4f,%.4f\n",
                streams, cpu, cpu_pct,
                (unsigned long long)delivered, (unsigned long long)lost,
                (unsigned long long)corrupt, fps_sum, loss_pct, jitter_avg);
    return 0;
}
