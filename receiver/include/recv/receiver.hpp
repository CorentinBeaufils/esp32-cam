#pragma once

#include "cam/reassembler.hpp"
#include "recv/metrics.hpp"

#include <asio.hpp>

#include <array>
#include <cstdint>
#include <functional>

// ---------------------------------------------------------------------------
// Receiver : reçoit les datagrammes UDP en boucle, les réassemble en trames
// complètes, et tient la télémétrie temps réel.
//
// Nouveauté de cette phase : la réception UDP ASYNCHRONE. L'émetteur envoyait
// en synchrone (send_to) ; ici on reçoit en coroutine (async_receive_from), car
// un récepteur doit rester réactif et ne jamais bloquer -- c'est tout l'intérêt
// d'asio. Le code asynchrone se lit comme du sequentiel.
//
// Le Receiver assemble tout ce que tu as construit :
//   - le Reassembler (Phase 0) pour reconstituer les trames et compter
//     pertes/corruption ;
//   - la MetricsWindow pour le fps / la latence / la gigue.
// ---------------------------------------------------------------------------
namespace rx {

class Receiver {
public:
    // Ouvre une socket UDP liée à 0.0.0.0:port (port 0 = éphémère).
    Receiver(asio::io_context& io, unsigned short port);

    // Appelé pour chaque trame complète reçue (après mise à jour des métriques).
    std::function<void(const cam::Frame&)> on_frame;

    void start();   // lance la boucle de réception (coroutine détachée)
    void stop();    // arrête proprement (ferme la socket -> operation_aborted)

    // Support du banc : force la taille du tampon de reception noyau
    // (SO_RCVBUF). Le noyau peut clamper à net.core.rmem_max ; la valeur
    // effectivement appliquee est renvoyee. A appeler AVANT start().
    int set_recv_buffer_bytes(int bytes);

    unsigned short port() const;                       // port réellement écouté
    const cam::Telemetry& telemetry() const { return reassembler_.telemetry(); }
    const MetricsWindow& metrics() const { return metrics_; }

private:
    asio::awaitable<void> loop();

    asio::ip::udp::socket socket_;
    cam::Reassembler reassembler_;
    MetricsWindow metrics_;
    std::array<std::uint8_t, 2048> buffer_;   // un datagramme tient largement dedans
    bool running_ = false;
};

} // namespace rx
