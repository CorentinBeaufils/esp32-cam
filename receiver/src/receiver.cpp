#include "recv/receiver.hpp"

#include <chrono>

// ---------------------------------------------------------------------------
// Receiver: asynchronous UDP reception (coroutine) + reassembly + metrics.
// ---------------------------------------------------------------------------
namespace rx {

namespace {
std::uint64_t now_us() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}
} // namespace

Receiver::Receiver(asio::io_context& io, unsigned short port)
    : socket_(io, asio::ip::udp::endpoint(asio::ip::udp::v4(), port)) {
    // wire up reassembler_.on_frame so that, for each complete frame:
    //   - compute the latency: recv = now_us(), and feed the window:
    //       metrics_.add(frame.timestamp_us, recv);
    //   - forward to the user callback: if (on_frame) on_frame(frame);
    reassembler_.on_frame = [this](const cam::Frame& frame) {
        std::uint64_t recv = now_us();
        metrics_.add(frame.timestamp_us, recv);
        if (on_frame) {
            on_frame(frame);
        }
    };
}

void Receiver::start() {
    // running_ = true, then launch the loop() coroutine:
    //   asio::co_spawn(socket_.get_executor(), loop(), asio::detached);
    running_ = true;
    asio::co_spawn(socket_.get_executor(), loop(), asio::detached);
}

void Receiver::stop() {
    // running_ = false, then close the socket to unblock the in-progress
    //   async_receive_from (it will resume with operation_aborted):
    //     asio::error_code ignore; socket_.close(ignore);
    running_ = false;
    asio::error_code ignore;
    socket_.close(ignore);
}

asio::awaitable<void> Receiver::loop() {
    // while running_:
    //   - asio::ip::udp::endpoint from;
    //   - error_code ec;
    //   - n = co_await socket_.async_receive_from(asio::buffer(buffer_), from,
    //           asio::redirect_error(asio::use_awaitable, ec));
    //   - if ec == operation_aborted: break; if any other ec: continue;
    //   - otherwise: reassembler_.feed(buffer_.data(), n);
    while (running_) {
        asio::ip::udp::endpoint from;
        asio::error_code ec;
        std::size_t n = co_await socket_.async_receive_from(
            asio::buffer(buffer_), from, asio::redirect_error(asio::use_awaitable, ec));
        if (ec == asio::error::operation_aborted) {
            break;
        }
        if (ec) {
            continue;
        }
        reassembler_.feed(buffer_.data(), n);
    }
    co_return;
}

unsigned short Receiver::port() const {
    return socket_.local_endpoint().port();
}

int Receiver::set_recv_buffer_bytes(int bytes) {
    asio::error_code ec;
    socket_.set_option(asio::socket_base::receive_buffer_size(bytes), ec);
    asio::socket_base::receive_buffer_size opt;
    socket_.get_option(opt, ec);
    return ec ? -1 : opt.value();
}

} // namespace rx
