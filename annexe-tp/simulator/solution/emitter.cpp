#include "sim/emitter.hpp"

#include "cam/protocol.hpp"

// ---------------------------------------------------------------------------
// TP-P1a — corrigé commenté (Emitter).
// ---------------------------------------------------------------------------
namespace sim {

Emitter::Emitter(asio::io_context& io, const std::string& host, unsigned short port)
    : socket_(io) {
    // UDP n'a pas de connexion : on ouvre juste la socket (choix du protocole
    // v4) et on retient l'adresse de destination. Aucun handshake, contrairement
    // au connect() de TCP.
    socket_.open(asio::ip::udp::v4());
    dest_ = asio::ip::udp::endpoint(asio::ip::make_address(host), port);
}

void Emitter::send_frame(std::uint32_t frame_id, std::uint64_t timestamp_us,
                         const std::uint8_t* jpeg, std::size_t size) {
    // Réutilise la Phase 0 : une trame -> N datagrammes prêts à partir.
    const auto datagrams = cam::fragment(frame_id, timestamp_us, jpeg, size);

    for (const auto& dg : datagrams) {
        // send_to envoie UN datagramme vers dest_. Version synchrone : pour un
        // émetteur, l'envoi UDP ne bloque pas (il remet au noyau et rend la
        // main). asio::buffer(dg) pointe vers le vecteur -- valide le temps de
        // l'appel, qui est complet avant de rendre la main : pas de souci de
        // durée de vie ici (contrairement à l'asynchrone).
        socket_.send_to(asio::buffer(dg), dest_);
        ++datagrams_sent_;
        bytes_sent_ += dg.size();
    }
}

} // namespace sim
