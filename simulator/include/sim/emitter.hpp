#pragma once

#include <asio.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Emitter : envoie une trame JPEG vers le PC en UDP.
//
// C'est la nouveauté asio de cette phase : udp::socket (tout était TCP
// jusqu'ici). UDP n'a pas de connexion -- on ouvre une socket et on envoie des
// datagrammes vers un endpoint destination, sans handshake.
//
// L'Emitter réutilise cam::fragment() (Phase 0) : une trame -> N datagrammes,
// puis chaque datagramme part par send_to.
// ---------------------------------------------------------------------------
namespace sim {

class Emitter {
public:
    // Ouvre une socket UDP et mémorise la destination (host:port).
    Emitter(asio::io_context& io, const std::string& host, unsigned short port);

    // Découpe la trame JPEG et envoie tous ses datagrammes vers la destination.
    void send_frame(std::uint32_t frame_id, std::uint64_t timestamp_us,
                    const std::uint8_t* jpeg, std::size_t size);

    std::uint64_t datagrams_sent() const { return datagrams_sent_; }
    std::uint64_t bytes_sent() const { return bytes_sent_; }

private:
    asio::ip::udp::socket socket_;
    asio::ip::udp::endpoint dest_;
    std::uint64_t datagrams_sent_ = 0;
    std::uint64_t bytes_sent_ = 0;
};

} // namespace sim
