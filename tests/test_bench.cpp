#include "bench/run_report.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

using namespace bench;

// Comparaison flottante locale (evite de dependre du composant matchers de
// Catch2, que les autres tests du projet n'utilisent pas).
static bool near_eq(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}

// ---------------------------------------------------------------------------
// RunReport : relevé commun d'un run, logique pure et déterministe.
// ---------------------------------------------------------------------------
TEST_CASE("bench: vide -> tout a zero", "[bench]") {
    RunReport r;
    const Report s = r.snapshot();
    CHECK(s.delivered == 0);
    CHECK(s.lost == 0);
    CHECK(s.corrupt == 0);
    CHECK(s.fps == 0.0);
    CHECK(s.loss_pct == 0.0);
    CHECK(s.jitter_ms == 0.0);
}

TEST_CASE("bench: flux parfait -> ni perte ni doublon ni desordre", "[bench]") {
    RunReport r;
    // 10 trames, ids 1..10, espacees de 10 ms, toutes saines.
    for (std::uint32_t i = 1; i <= 10; ++i) r.on_frame(i, i * 10.0, true);
    const Report s = r.snapshot();
    CHECK(s.delivered == 10);
    CHECK(s.unique == 10);
    CHECK(s.lost == 0);
    CHECK(s.corrupt == 0);
    CHECK(s.duplicate == 0);
    CHECK(s.reordered == 0);
    // 10 trames -> 9 intervalles sur (100-10)=90 ms = 0.09 s -> 100 fps.
    CHECK(near_eq(s.seconds, 0.09, 1e-9));
    CHECK(near_eq(s.fps, 100.0, 1e-6));
    CHECK(near_eq(s.jitter_ms, 0.0, 1e-9));   // espacement constant
}

TEST_CASE("bench: trous dans la sequence -> perte", "[bench]") {
    RunReport r;
    // ids 1,2,3,5,6 : le 4 manque. Intervalle [1,6] -> expected 6, unique 5.
    for (std::uint32_t id : {1u, 2u, 3u, 5u, 6u}) r.on_frame(id, id * 10.0, true);
    const Report s = r.snapshot();
    CHECK(s.unique == 5);
    CHECK(s.lost == 1);
    CHECK(near_eq(s.loss_pct, 100.0 / 6.0, 1e-9));
}

TEST_CASE("bench: crc KO -> corruption comptee", "[bench]") {
    RunReport r;
    r.on_frame(1, 10.0, true);
    r.on_frame(2, 20.0, false);   // arrivee mais payload corrompu
    r.on_frame(3, 30.0, true);
    const Report s = r.snapshot();
    CHECK(s.delivered == 3);
    CHECK(s.corrupt == 1);
    CHECK(s.lost == 0);           // corrompu != perdu : la trame est bien arrivee
}

TEST_CASE("bench: doublon compte, pas re-perdu", "[bench]") {
    RunReport r;
    for (std::uint32_t id : {1u, 2u, 2u, 3u}) r.on_frame(id, id * 10.0, true);
    const Report s = r.snapshot();
    CHECK(s.delivered == 4);
    CHECK(s.unique == 3);
    CHECK(s.duplicate == 1);
    CHECK(s.lost == 0);
}

TEST_CASE("bench: desordre compte, sans perte", "[bench]") {
    RunReport r;
    // 1 puis 3 puis 2 : le 2 arrive apres un id superieur -> desordre, pas perte.
    r.on_frame(1, 10.0, true);
    r.on_frame(3, 20.0, true);
    r.on_frame(2, 30.0, true);
    const Report s = r.snapshot();
    CHECK(s.reordered == 1);
    CHECK(s.unique == 3);
    CHECK(s.lost == 0);           // intervalle [1,3], 3 distincts -> rien perdu
}

TEST_CASE("bench: gigue non nulle si espacement irregulier", "[bench]") {
    RunReport r;
    // La 1re trame fixe l'origine (pas de gap). Ensuite inter-arrivees
    // 10,30,10,30 ms -> moyenne 20, ecart absolu moyen (MAD) = 10.
    std::uint32_t id = 1;
    r.on_frame(id++, 0.0, true);
    double t = 0.0;
    for (double gap : {10.0, 30.0, 10.0, 30.0}) { t += gap; r.on_frame(id++, t, true); }
    const Report s = r.snapshot();
    CHECK(near_eq(s.jitter_ms, 10.0, 1e-9));
}

TEST_CASE("bench: csv header et ligne ont le meme nombre de colonnes", "[bench]") {
    RunReport r;
    r.on_frame(1, 10.0, true);
    const auto count_cols = [](const std::string& s) {
        std::size_t n = 1;
        for (char c : s) if (c == ',') ++n;
        return n;
    };
    CHECK(count_cols(RunReport::csv_header()) == count_cols(r.to_csv()));
}
