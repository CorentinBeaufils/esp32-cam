#include "sim/emitter.hpp"

#include "cam/protocol.hpp"

// ---------------------------------------------------------------------------
// Emitter: splits a JPEG frame into datagrams and emits them over UDP.
// ---------------------------------------------------------------------------
namespace sim {

Emitter::Emitter(asio::io_context& io, const std::string& host, unsigned short port)
    : socket_(io) {
    //   - open the socket in UDP v4: socket_.open(asio::ip::udp::v4());
    //   - build the destination endpoint:
    //       dest_ = asio::ip::udp::endpoint(asio::ip::make_address(host), port);
    socket_.open(asio::ip::udp::v4());
    dest_ = asio::ip::udp::endpoint(asio::ip::make_address(host), port);
}

void Emitter::send_frame(std::uint32_t frame_id, std::uint64_t timestamp_us,
                         const std::uint8_t* jpeg, std::size_t size) {
    //   - fragment: auto dgs = cam::fragment(frame_id, timestamp_us, jpeg, size);
    //   - for each datagram: socket_.send_to(asio::buffer(dg), dest_);
    //   - update datagrams_sent_ and bytes_sent_.
    // send_to is SYNCHRONOUS and non-blocking in practice (UDP): no need for a
    // coroutine here, a direct send is enough for an emitter.
    auto dgs = cam::fragment(frame_id, timestamp_us, jpeg, size);
    for (const auto& dg : dgs) {
        socket_.send_to(asio::buffer(dg), dest_);
        datagrams_sent_++;
        bytes_sent_ += dg.size();
    }
}

} // namespace sim
