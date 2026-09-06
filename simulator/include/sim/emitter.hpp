#pragma once

#include <asio.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Emitter: sends a JPEG frame to the PC over UDP.
//
// This is the new asio piece of this phase: udp::socket (everything was TCP
// until now). UDP is connectionless -- we open a socket and send datagrams to a
// destination endpoint, without a handshake.
//
// The Emitter reuses cam::fragment() (Phase 0): one frame -> N datagrams,
// then each datagram goes out via send_to.
// ---------------------------------------------------------------------------
namespace sim {

class Emitter {
public:
    // Opens a UDP socket and stores the destination (host:port).
    Emitter(asio::io_context& io, const std::string& host, unsigned short port);

    // Splits the JPEG frame and sends all its datagrams to the destination.
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
