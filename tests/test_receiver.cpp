#include "recv/metrics.hpp"
#include "recv/receiver.hpp"
#include "sim/emitter.hpp"

#include <asio.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

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
// MetricsWindow : logique pure, instants synthétiques
// ---------------------------------------------------------------------------
TEST_CASE("metrics: fps sur une fenetre pleine", "[recv][metrics]") {
    rx::MetricsWindow m(1'000'000);   // fenetre 1 s
    // 25 trames espacees de 40 ms, latence fixe 5 ms.
    for (int k = 0; k < 25; ++k) {
        const std::uint64_t recv = static_cast<std::uint64_t>(k) * 40'000;
        m.add(recv - 5'000, recv);
    }
    CHECK(m.count() == 25);
    CHECK(m.fps() == 25.0);              // 25 trames sur 1 s
    CHECK(m.avg_latency_ms() == 5.0);
    CHECK(m.jitter_ms() == 0.0);        // latence constante -> gigue nulle
}

TEST_CASE("metrics: la fenetre glissante evince les vieux echantillons", "[recv][metrics]") {
    rx::MetricsWindow m(100'000);     // fenetre 100 ms
    m.add(0, 0);
    m.add(10'000, 20'000);
    // Une trame bien plus tard : tout ce qui precede la fenetre est evince.
    m.add(495'000, 500'000);
    CHECK(m.count() == 1);              // seule la derniere reste
}

TEST_CASE("metrics: gigue = variation de latence", "[recv][metrics]") {
    rx::MetricsWindow m(10'000'000);
    // Latences 10, 20, 30 ms (recv - emit). Moyenne 20, ecart abs moyen 6.67.
    m.add(0, 10'000);
    m.add(100'000, 120'000);
    m.add(200'000, 230'000);
    CHECK(m.avg_latency_ms() == 20.0);
    CHECK(m.jitter_ms() > 6.0);
    CHECK(m.jitter_ms() < 7.0);
}

TEST_CASE("metrics: latence negative (derive d'horloge) bornee a 0", "[recv][metrics]") {
    rx::MetricsWindow m;
    m.add(1'000'000, 500'000);          // recv < emit
    CHECK(m.avg_latency_ms() == 0.0);   // pas de latence negative absurde
}

// ---------------------------------------------------------------------------
// Receiver : reception UDP reelle en loopback, alimentee par l'Emitter
// ---------------------------------------------------------------------------
TEST_CASE("receiver: une trame emise est recue et reassemblee", "[recv][receiver]") {
    asio::io_context io;

    rx::Receiver receiver(io, 0);     // port ephemere
    const unsigned short port = receiver.port();

    int recu = 0;
    cam::Frame derniere;
    receiver.on_frame = [&](const cam::Frame& f) { ++recu; derniere = f; };
    receiver.start();

    sim::Emitter emitter(io, "127.0.0.1", port);
    const auto img = fake_jpeg(3000, 5);   // 3 datagrammes
    emitter.send_frame(7, 111111, img.data(), img.size());

    // On laisse la boucle asynchrone traiter les datagrammes.
    io.run_for(std::chrono::milliseconds(200));

    CHECK(recu == 1);
    CHECK(derniere.frame_id == 7);
    CHECK(derniere.jpeg == img);
    CHECK(receiver.telemetry().frames_completed == 1);
    CHECK(receiver.metrics().count() == 1);
}

TEST_CASE("receiver: plusieurs trames -> fps et telemetrie", "[recv][receiver]") {
    asio::io_context io;
    rx::Receiver receiver(io, 0);
    const unsigned short port = receiver.port();

    int recu = 0;
    receiver.on_frame = [&](const cam::Frame&) { ++recu; };
    receiver.start();

    sim::Emitter emitter(io, "127.0.0.1", port);
    const auto img = fake_jpeg(2000, 3);
    for (std::uint32_t id = 0; id < 10; ++id) {
        emitter.send_frame(id, id * 1000ull, img.data(), img.size());
    }
    io.run_for(std::chrono::milliseconds(200));

    CHECK(recu == 10);
    CHECK(receiver.telemetry().frames_completed == 10);
    CHECK(receiver.telemetry().fragments_rejected == 0);
}

TEST_CASE("receiver: stop() arrete proprement la boucle", "[recv][receiver]") {
    asio::io_context io;
    rx::Receiver receiver(io, 0);
    receiver.start();
    receiver.stop();
    // Sans travail restant, run() rend la main : le test se termine (pas de blocage).
    io.run_for(std::chrono::milliseconds(100));
    SUCCEED();
}
