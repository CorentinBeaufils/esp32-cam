#include "sim/emitter.hpp"
#include "sim/pacer.hpp"

#include <asio.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Simulateur "faux ESP32" : émet des trames synthétiques en UDP, à cadence
// fixe, avec le même protocole que la vraie carte. Fourni complet -- il te sert
// à VÉRIFIER ton Pacer et ton Emitter en conditions réelles.
//
//   ./simulator [host] [port] [fps] [taille_octets]
//   ex :  ./simulator 127.0.0.1 9000 25 8000
//
// La "trame JPEG" est ici synthétique (un motif qui change à chaque image) :
// le vrai encodage JPEG viendra avec OpenCV en Phase 1c. Le but ici est le
// réseau et la cadence.
// ---------------------------------------------------------------------------
namespace {

std::uint64_t now_us() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

// Fabrique une fausse trame : un motif qui bouge avec frame_id, pour qu'à la
// réception on "voie" quelque chose changer.
std::vector<std::uint8_t> synth_frame(std::uint32_t frame_id, std::size_t size) {
    std::vector<std::uint8_t> v(size);
    for (std::size_t i = 0; i < size; ++i) {
        v[i] = static_cast<std::uint8_t>((i + frame_id * 7u) & 0xFF);
    }
    return v;
}

} // namespace

int main(int argc, char** argv) {
    const std::string host = (argc > 1) ? argv[1] : "127.0.0.1";
    const unsigned short port =
        static_cast<unsigned short>((argc > 2) ? std::atoi(argv[2]) : 9000);
    const double fps = (argc > 3) ? std::atof(argv[3]) : 25.0;
    const std::size_t taille = (argc > 4) ? static_cast<std::size_t>(std::atoi(argv[4])) : 8000;

    std::printf("Simulateur -> %s:%u  |  %.1f fps  |  %zu octets/trame\n",
                host.c_str(), port, fps, taille);

    asio::io_context io;
    sim::Emitter emitter(io, host, port);
    sim::Pacer pacer(fps);

    const auto depart = sim::Pacer::clock::now();
    pacer.start(depart);

    std::uint32_t frame_id = 0;
    auto derniere_stat = std::chrono::steady_clock::now();

    while (true) {
        const auto image = synth_frame(frame_id, taille);
        emitter.send_frame(frame_id, now_us(), image.data(), image.size());
        ++frame_id;

        // Statistiques toutes les secondes.
        const auto maintenant = std::chrono::steady_clock::now();
        if (maintenant - derniere_stat >= std::chrono::seconds(1)) {
            std::printf("  trames=%u  datagrammes=%llu  octets=%llu  sautes=%llu\n",
                        frame_id,
                        static_cast<unsigned long long>(emitter.datagrams_sent()),
                        static_cast<unsigned long long>(emitter.bytes_sent()),
                        static_cast<unsigned long long>(pacer.skipped_beats()));
            derniere_stat = maintenant;
        }

        // Cadence : on attend jusqu'au prochain créneau.
        const auto attente = pacer.next_wait(sim::Pacer::clock::now());
        if (attente > std::chrono::nanoseconds(0)) {
            std::this_thread::sleep_for(attente);
        }
    }
    return 0;
}
