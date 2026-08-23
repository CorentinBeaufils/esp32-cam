#include "sim/emitter.hpp"

#include "cam/protocol.hpp"

// ---------------------------------------------------------------------------
// TP-P1a — à toi de jouer (partie 2/2 : l'Emitter).
//   Énoncé : ENONCE.md   Bloqué : INDICES.md   Corrigé : solution/emitter.cpp
// ---------------------------------------------------------------------------
namespace sim {

Emitter::Emitter(asio::io_context& io, const std::string& host, unsigned short port)
    : socket_(io) {
    // TODO :
    //   - ouvrir la socket en UDP v4 : socket_.open(asio::ip::udp::v4());
    //   - construire l'endpoint destination :
    //       dest_ = asio::ip::udp::endpoint(asio::ip::make_address(host), port);
    socket_.open(asio::ip::udp::v4());
    dest_ = asio::ip::udp::endpoint(asio::ip::make_address(host), port);
}

void Emitter::send_frame(std::uint32_t frame_id, std::uint64_t timestamp_us,
                         const std::uint8_t* jpeg, std::size_t size) {
    // TODO :
    //   - fragmenter : auto dgs = cam::fragment(frame_id, timestamp_us, jpeg, size);
    //   - pour chaque datagramme : socket_.send_to(asio::buffer(dg), dest_);
    //   - mettre à jour datagrams_sent_ et bytes_sent_.
    // send_to est SYNCHRONE et non bloquant en pratique (UDP) : pas besoin de
    // coroutine ici, un envoi direct suffit pour un émetteur.
    auto dgs = cam::fragment(frame_id, timestamp_us, jpeg, size);
    for (const auto& dg : dgs) {
        socket_.send_to(asio::buffer(dg), dest_);
        datagrams_sent_++;
        bytes_sent_ += dg.size();
    }
}

} // namespace sim
