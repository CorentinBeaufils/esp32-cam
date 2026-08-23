#include "up/scale_stats.hpp"
#include "up/upscale_policy.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace up;

// ---------------------------------------------------------------------------
// ScaleStats : fenêtre glissante de coûts, logique pure.
// ---------------------------------------------------------------------------
TEST_CASE("stats: vide -> tout a zero", "[up][stats]") {
    ScaleStats s(4);
    CHECK(s.count() == 0);
    CHECK(s.avg_ms() == 0.0);
    CHECK(s.max_ms() == 0.0);
    CHECK(s.p95_ms() == 0.0);
    CHECK(s.over_budget(10.0) == 0);
}

TEST_CASE("stats: moyenne, max et fenetre bornee", "[up][stats]") {
    ScaleStats s(4);
    s.record(10); s.record(20); s.record(30); s.record(40);
    CHECK(s.count() == 4);
    CHECK(s.avg_ms() == 25.0);
    CHECK(s.max_ms() == 40.0);

    // Fenetre pleine : le plus ancien (10) est evince.
    s.record(50);
    CHECK(s.count() == 4);
    CHECK(s.max_ms() == 50.0);
    CHECK(s.avg_ms() == (20 + 30 + 40 + 50) / 4.0);
}

TEST_CASE("stats: over_budget compte les depassements recents", "[up][stats]") {
    ScaleStats s(4);
    s.record(20); s.record(30); s.record(40); s.record(50);
    CHECK(s.over_budget(35.0) == 2);   // 40 et 50
    CHECK(s.over_budget(100.0) == 0);
    CHECK(s.over_budget(0.0) == 4);
}

TEST_CASE("stats: p95 par nearest-rank", "[up][stats]") {
    ScaleStats s(1000);
    for (int i = 1; i <= 100; ++i) s.record(i);
    // ceil(0.95 * 100) = 95 -> la 95e valeur triee = 95.
    CHECK(s.p95_ms() == 95.0);

    ScaleStats one;
    one.record(7.5);
    CHECK(one.p95_ms() == 7.5);   // un seul echantillon
}

// ---------------------------------------------------------------------------
// UpscalePolicy : contrôleur adaptatif, déterministe.
// ---------------------------------------------------------------------------
TEST_CASE("policy: depassement -> on redescend d'un cran", "[up][policy]") {
    UpscalePolicy p(10.0, Interp::Cubic);
    CHECK(p.current() == Interp::Cubic);

    CHECK(p.update(12.0) == Interp::Linear);
    CHECK(p.downgrades() == 1);

    p.update(20.0);
    CHECK(p.current() == Interp::Nearest);
    CHECK(p.downgrades() == 2);
}

TEST_CASE("policy: on ne descend jamais sous Nearest", "[up][policy]") {
    UpscalePolicy p(10.0, Interp::Nearest);
    p.update(99.0);
    CHECK(p.current() == Interp::Nearest);
    CHECK(p.downgrades() == 0);
}

TEST_CASE("policy: remontee seulement apres une serie stable et confortable", "[up][policy]") {
    UpscalePolicy p(10.0, Interp::Nearest);
    // 7 trames confortables (< 6 ms) : pas encore assez.
    for (int i = 0; i < 7; ++i) {
        p.update(3.0);
        CHECK(p.current() == Interp::Nearest);
    }
    // 8e : on remonte.
    p.update(3.0);
    CHECK(p.current() == Interp::Linear);
    CHECK(p.upgrades() == 1);
}

TEST_CASE("policy: dans le budget mais pas confortable -> on casse la serie", "[up][policy]") {
    UpscalePolicy p(10.0, Interp::Nearest);
    for (int i = 0; i < 5; ++i) p.update(3.0);   // 5 bonnes
    p.update(8.0);                                // ok mais pas confortable -> reset
    for (int i = 0; i < 7; ++i) p.update(3.0);    // 7 bonnes -> pas encore 8
    CHECK(p.current() == Interp::Nearest);
    p.update(3.0);                                // 8e -> remonte
    CHECK(p.current() == Interp::Linear);
}

TEST_CASE("policy: to_string", "[up][policy]") {
    CHECK(std::string(to_string(Interp::Nearest)) == "nearest");
    CHECK(std::string(to_string(Interp::Lanczos)) == "lanczos");
}
