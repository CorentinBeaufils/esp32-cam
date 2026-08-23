#include "recv/receiver.hpp"

#include <chrono>

// ---------------------------------------------------------------------------
// Receiver : reception UDP asynchrone (coroutine) + reassemblage + metriques.
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
    // brancher reassembler_.on_frame pour, à chaque trame complète :
    //   - calculer la latence : recv = now_us(), et alimenter la fenêtre :
    //       metrics_.add(frame.timestamp_us, recv);
    //   - propager au callback utilisateur : if (on_frame) on_frame(frame);
    reassembler_.on_frame = [this](const cam::Frame& frame) {
        std::uint64_t recv = now_us();
        metrics_.add(frame.timestamp_us, recv);
        if (on_frame) {
            on_frame(frame);
        }
    };
}

void Receiver::start() {
    // running_ = true, puis lancer la coroutine loop() :
    //   asio::co_spawn(socket_.get_executor(), loop(), asio::detached);
    running_ = true;
    asio::co_spawn(socket_.get_executor(), loop(), asio::detached);
}

void Receiver::stop() {
    // running_ = false, puis fermer la socket pour débloquer le
    //   async_receive_from en cours (il repartira avec operation_aborted) :
    //     asio::error_code ignore; socket_.close(ignore);
    running_ = false;
    asio::error_code ignore;
    socket_.close(ignore);
}

asio::awaitable<void> Receiver::loop() {
    // tant que running_ :
    //   - asio::ip::udp::endpoint from;
    //   - error_code ec;
    //   - n = co_await socket_.async_receive_from(asio::buffer(buffer_), from,
    //           asio::redirect_error(asio::use_awaitable, ec));
    //   - si ec == operation_aborted : break ; si autre ec : continue ;
    //   - sinon : reassembler_.feed(buffer_.data(), n);
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
