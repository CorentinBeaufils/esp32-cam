#include "sim/pacer.hpp"
#include "sim/emitter.hpp"
#include "cam/reassembler.hpp"

#include <asio.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

using namespace std::chrono_literals;

namespace {

std::vector<std::uint8_t> fake_jpeg(std::size_t n, std::uint8_t seed) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<std::uint8_t>((i * 31 + seed) & 0xFF);
    }
    return v;
}

} // namespace

// ---------------------------------------------------------------------------
// Pacer : logique de cadence, testée avec des instants synthétiques
// ---------------------------------------------------------------------------
TEST_CASE("pacer: la periode derive du fps", "[sim][pacer]") {
    sim::Pacer p(25.0);
    CHECK(p.period() == 40ms);      // 1000 / 25
    sim::Pacer p2(50.0);
    CHECK(p2.period() == 20ms);
}

TEST_CASE("pacer: a l'heure, on attend une periode", "[sim][pacer]") {
    sim::Pacer p(25.0);
    const sim::Pacer::clock::time_point t0{};
    p.start(t0);

    // Emission instantanee : on doit attendre 40 ms jusqu'au 1er creneau.
    CHECK(p.next_wait(t0) == 40ms);
    // Creneau suivant respecte, emission instantanee : encore 40 ms.
    CHECK(p.next_wait(t0 + 40ms) == 40ms);
    CHECK(p.skipped_beats() == 0);
}

TEST_CASE("pacer: en retard -> attente nulle et battements comptes", "[sim][pacer]") {
    sim::Pacer p(25.0);
    const sim::Pacer::clock::time_point t0{};
    p.start(t0);

    p.next_wait(t0);          // deadline -> t0+40
    p.next_wait(t0 + 40ms);   // deadline -> t0+80

    // Emission tres lente : on arrive a t0+200, alors que le creneau vise etait
    // t0+120. Retard 80 ms = 2 periodes -> on n'attend pas, on compte 2 sautes.
    CHECK(p.next_wait(t0 + 200ms) == 0ns);
    CHECK(p.skipped_beats() == 2);
}

TEST_CASE("pacer: pas de derive sur le long terme", "[sim][pacer]") {
    sim::Pacer p(100.0);      // periode 10 ms
    const sim::Pacer::clock::time_point t0{};
    p.start(t0);

    // Chaque image sort pile a son creneau : l'attente reste 10 ms, sans derive.
    auto instant = t0;
    for (int i = 0; i < 100; ++i) {
        const auto attente = p.next_wait(instant);
        CHECK(attente == 10ms);
        instant += attente;   // on "dort" exactement l'attente demandee
    }
    CHECK(p.skipped_beats() == 0);
}

// ---------------------------------------------------------------------------
// Emitter : envoi UDP réel en loopback, réassemblé a l'arrivée
// ---------------------------------------------------------------------------
TEST_CASE("emitter: une trame traverse UDP et se reassemble", "[sim][emitter]") {
    asio::io_context io;

    // Recepteur : socket UDP sur un port ephemere de 127.0.0.1.
    asio::ip::udp::socket receiver(
        io, asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    const unsigned short port = receiver.local_endpoint().port();

    sim::Emitter emitter(io, "127.0.0.1", port);

    const auto img = fake_jpeg(3000, 5);   // 3 datagrammes
    emitter.send_frame(7, 123456, img.data(), img.size());
    CHECK(emitter.datagrams_sent() == 3);
    CHECK(emitter.bytes_sent() > img.size());   // en-tetes en plus

    // On recoit les 3 datagrammes et on les passe au reassembleur.
    cam::Reassembler r;
    cam::Frame recue;
    bool recu = false;
    r.on_frame = [&](const cam::Frame& f) { recue = f; recu = true; };

    std::vector<std::uint8_t> buf(2048);
    for (int i = 0; i < 3; ++i) {
        asio::ip::udp::endpoint from;
        const std::size_t n = receiver.receive_from(asio::buffer(buf), from);
        r.feed(buf.data(), n);
    }

    REQUIRE(recu);
    CHECK(recue.frame_id == 7);
    CHECK(recue.timestamp_us == 123456);
    CHECK(recue.jpeg == img);
}

TEST_CASE("emitter: une trame vide part quand meme (1 datagramme)", "[sim][emitter]") {
    asio::io_context io;
    asio::ip::udp::socket receiver(
        io, asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    const unsigned short port = receiver.local_endpoint().port();

    sim::Emitter emitter(io, "127.0.0.1", port);
    emitter.send_frame(1, 0, nullptr, 0);
    CHECK(emitter.datagrams_sent() == 1);

    std::vector<std::uint8_t> buf(2048);
    asio::ip::udp::endpoint from;
    const std::size_t n = receiver.receive_from(asio::buffer(buf), from);
    cam::Reassembler r;
    bool recu = false;
    r.on_frame = [&](const cam::Frame&) { recu = true; };
    r.feed(buf.data(), n);
    CHECK(recu);
}
