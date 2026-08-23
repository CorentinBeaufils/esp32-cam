#include "recv/receiver.hpp"

#include <chrono>
#include <system_error>

// ---------------------------------------------------------------------------
// TP-P1b — corrigé commenté (Receiver).
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
    // Le Reassembler émet une trame COMPLÈTE : c'est le seul moment où l'on
    // mesure la latence (on connaît l'instant d'émission via timestamp_us) et
    // où l'on prévient l'utilisateur.
    reassembler_.on_frame = [this](const cam::Frame& frame) {
        const std::uint64_t recv = now_us();
        metrics_.add(frame.timestamp_us, recv);
        if (on_frame) {
            on_frame(frame);
        }
    };
}

void Receiver::start() {
    running_ = true;
    // co_spawn lance la coroutine sur l'exécuteur de la socket. detached : on ne
    // récupère pas de résultat, la boucle vit tant que la socket vit.
    asio::co_spawn(socket_.get_executor(), loop(), asio::detached);
}

void Receiver::stop() {
    running_ = false;
    // Fermer la socket fait échouer le async_receive_from en attente avec
    // operation_aborted (souvenir du TP2/TP9), ce qui casse la boucle proprement
    // sans blocage.
    std::error_code ignore;
    socket_.close(ignore);
}

asio::awaitable<void> Receiver::loop() {
    std::error_code ec;
    while (running_) {
        asio::ip::udp::endpoint from;

        // async_receive_from remplit buffer_ avec UN datagramme et donne sa
        // taille. buffer_ est un membre : il survit au co_await (comme les
        // locales d'une coroutine), et comme une seule boucle tourne à la fois,
        // pas de conflit d'accès.
        const std::size_t n = co_await socket_.async_receive_from(
            asio::buffer(buffer_), from,
            asio::redirect_error(asio::use_awaitable, ec));

        if (ec) {
            if (ec == asio::error::operation_aborted) {
                break;      // stop() a fermé la socket : fin propre
            }
            continue;       // autre erreur (datagramme malformé...) : on continue
        }

        // Chaque datagramme part au Reassembler (Phase 0). S'il complète une
        // trame, le callback branché dans le constructeur se déclenche.
        reassembler_.feed(buffer_.data(), n);
    }
    co_return;
}

unsigned short Receiver::port() const {
    return socket_.local_endpoint().port();
}

} // namespace rx
