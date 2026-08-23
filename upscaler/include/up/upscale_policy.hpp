#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// UpscalePolicy : choisit, image après image, la MÉTHODE d'interpolation à
// utiliser pour rester dans le budget temps réel.
//
// Le problème concret : à ~28 fps tu as ~35 ms par image pour TOUT faire
// (décoder + agrandir + afficher). Les algos d'agrandissement ne coûtent pas
// pareil -- du moins cher au plus cher / du plus grossier au plus fin :
//
//     Nearest  <  Linear  <  Cubic  <  Lanczos
//
// Lanczos est le plus beau mais peut ne pas tenir dans le budget selon la
// machine et la résolution. Plutôt que de choisir une fois pour toutes, on
// s'ADAPTE : on mesure le temps réel de l'upscale, et
//   - si on dépasse le budget -> on redescend d'un cran (moins cher) ;
//   - si on est confortablement SOUS le budget depuis un moment -> on retente
//     un cran plus beau.
//
// C'est exactement le principe du contrôle de débit d'un encodeur vidéo, en
// miniature. L'HYSTÉRÉSIS (le « depuis un moment ») évite l'oscillation :
// sans elle, on basculerait entre deux niveaux à chaque image.
//
// Logique PURE et DÉTERMINISTE -> testable au cas près.
// ---------------------------------------------------------------------------
namespace up {

// Ordonnées du moins cher/fin (0) au plus cher/fin (3). L'ordre EST utilisé
// (on monte/descend par ++/--), ne le change pas sans réfléchir.
enum class Interp {
    Nearest = 0,
    Linear  = 1,
    Cubic   = 2,
    Lanczos = 3,
};

const char* to_string(Interp interp);

class UpscalePolicy {
public:
    // budget_ms : temps max autorisé par trame pour l'upscale.
    // start     : méthode de départ (Cubic : bon compromis).
    explicit UpscalePolicy(double budget_ms, Interp start = Interp::Cubic);

    // À appeler APRÈS chaque upscale avec le temps mesuré (ms). Met à jour le
    // niveau et renvoie la méthode à utiliser pour la PROCHAINE trame.
    Interp update(double measured_ms);

    Interp current() const;
    std::uint64_t downgrades() const;   // nombre de descentes (télémétrie)
    std::uint64_t upgrades() const;     // nombre de montées

private:
    double budget_ms_;
    Interp current_;
    int good_streak_ = 0;               // trames consécutives bien sous le budget
    std::uint64_t downgrades_ = 0;
    std::uint64_t upgrades_ = 0;
};

} // namespace up
