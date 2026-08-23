#include "disp/latest_frame.hpp"
#include "cam/reassembler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace {

std::shared_ptr<const cam::Frame> make_frame(std::uint32_t id) {
    cam::Frame f;
    f.frame_id = id;
    return std::make_shared<const cam::Frame>(std::move(f));
}

} // namespace

// ---------------------------------------------------------------------------
// Comportement de base (un seul thread)
// ---------------------------------------------------------------------------
TEST_CASE("latest: store puis take rend la trame, take suivant rend null", "[display]") {
    disp::LatestFrame lf;
    lf.store(make_frame(7));

    auto f = lf.take();
    REQUIRE(f != nullptr);
    CHECK(f->frame_id == 7);

    CHECK(lf.take() == nullptr);      // plus rien de nouveau
    CHECK(lf.dropped() == 0);
}

TEST_CASE("latest: un store non consomme est ecrase et compte", "[display]") {
    disp::LatestFrame lf;
    lf.store(make_frame(1));
    lf.store(make_frame(2));          // ecrase la trame 1, jamais consommee

    CHECK(lf.dropped() == 1);
    auto f = lf.take();
    REQUIRE(f != nullptr);
    CHECK(f->frame_id == 2);          // le plus recent gagne
}

// ---------------------------------------------------------------------------
// Concurrence : producteur (reseau) vs consommateur (affichage)
//   Invariant : chaque trame stockee est soit CONSOMMEE, soit ECRASEE.
//   À lancer aussi sous ThreadSanitizer -> doit etre propre.
// ---------------------------------------------------------------------------
TEST_CASE("latest: producteur/consommateur, aucune trame perdue en compte", "[display][thread]") {
    disp::LatestFrame lf;
    const std::uint32_t N = 100'000;
    std::atomic<bool> done{false};

    std::thread producteur([&] {
        for (std::uint32_t i = 0; i < N; ++i) {
            lf.store(make_frame(i));
        }
        done.store(true);
    });

    std::uint64_t consommees = 0;
    while (!done.load()) {
        if (lf.take()) {
            ++consommees;
        }
    }
    producteur.join();
    while (lf.take()) {               // on vide ce qui reste
        ++consommees;
    }

    // Toute trame stockee a fini consommee ou ecrasee : rien ne s'evapore.
    CHECK(consommees + lf.dropped() == N);
}
