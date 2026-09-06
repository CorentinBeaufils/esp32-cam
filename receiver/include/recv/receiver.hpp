#pragma once

#include "cam/reassembler.hpp"
#include "recv/metrics.hpp"

#include <asio.hpp>

#include <array>
#include <cstdint>
#include <functional>

// ---------------------------------------------------------------------------
// Receiver: receives UDP datagrams in a loop, reassembles them into complete
// frames, and maintains real-time telemetry.
//
// New in this phase: ASYNCHRONOUS UDP reception. The sender transmitted
// synchronously (send_to); here we receive in a coroutine (async_receive_from),
// because a receiver must stay responsive and never block -- that is the whole
// point of asio. The asynchronous code reads like sequential code.
//
// The Receiver ties together everything built so far:
//   - the Reassembler (Phase 0) to rebuild the frames and count
//     loss/corruption;
//   - the MetricsWindow for fps / latency / jitter.
// ---------------------------------------------------------------------------
namespace rx {

class Receiver {
public:
    // Opens a UDP socket bound to 0.0.0.0:port (port 0 = ephemeral).
    Receiver(asio::io_context& io, unsigned short port);

    // Called for each complete frame received (after the metrics are updated).
    std::function<void(const cam::Frame&)> on_frame;

    void start();   // starts the reception loop (detached coroutine)
    void stop();    // shuts down cleanly (closes the socket -> operation_aborted)

    // Benchmark support: forces the kernel receive buffer size (SO_RCVBUF).
    // The kernel may clamp it to net.core.rmem_max; the value actually applied
    // is returned. Call this BEFORE start().
    int set_recv_buffer_bytes(int bytes);

    unsigned short port() const;                       // port actually listened on
    const cam::Telemetry& telemetry() const { return reassembler_.telemetry(); }
    const MetricsWindow& metrics() const { return metrics_; }

private:
    asio::awaitable<void> loop();

    asio::ip::udp::socket socket_;
    cam::Reassembler reassembler_;
    MetricsWindow metrics_;
    std::array<std::uint8_t, 2048> buffer_;   // a datagram fits easily in here
    bool running_ = false;
};

} // namespace rx
