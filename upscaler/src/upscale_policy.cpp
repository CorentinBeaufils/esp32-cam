#include "up/upscale_policy.hpp"

// ---------------------------------------------------------------------------
// TP-P3 — à toi de jouer (partie 2/2 : UpscalePolicy).
//   Énoncé : ENONCE.md   Bloqué : INDICES.md   Corrigé : solution/upscale_policy.cpp
//
// Le cœur du TP : un contrôleur de qualité adaptatif. Relis l'en-tête
// (upscale_policy.hpp) pour la règle ; l'ENONCE détaille l'hystérésis.
// ---------------------------------------------------------------------------
namespace up {

const char* to_string(Interp interp) {
    switch (interp) {
        case Interp::Nearest: return "nearest";
        case Interp::Linear:  return "linear";
        case Interp::Cubic:   return "cubic";
        case Interp::Lanczos: return "lanczos";
    }
    return "?";
}

UpscalePolicy::UpscalePolicy(double budget_ms, Interp start)
    : budget_ms_(budget_ms), current_(start) {}

Interp UpscalePolicy::update(double measured_ms) {
    // TODO — trois cas :
    //   1) measured > budget          -> good_streak_ = 0 ; descendre d'un cran
    //                                     (sans passer sous Nearest) ; ++downgrades_.
    //   2) measured < budget * 0.6    -> ++good_streak_ ; si good_streak_ atteint
    //                                     le seuil (8) : monter d'un cran (sans
    //                                     dépasser Lanczos), ++upgrades_, et
    //                                     remettre good_streak_ à 0.
    //   3) sinon (dans le budget mais  -> good_streak_ = 0 ; rester.
    //      pas confortable)
    // Astuce : borne les crans avec l'ordre de l'enum (static_cast<int>).

    if (measured_ms > budget_ms_) {
        good_streak_ = 0;
        if (current_ != Interp::Nearest) {
            current_ = static_cast<Interp>(static_cast<int>(current_) - 1); // question sur les cas des enums : est ce que c'est safe de faire un static_cast<int> sur un enum class et de le decrementeer ? 
            ++downgrades_;
        }
    } else if (measured_ms < budget_ms_ * 0.6) {
        ++good_streak_;
        if (good_streak_ >= 8) {
            good_streak_ = 0;
            if (current_ != Interp::Lanczos) {
                current_ = static_cast<Interp>(static_cast<int>(current_) + 1);
                ++upgrades_;
            }
        }
    } else {
        good_streak_ = 0;
    }
    
    return current_;
}

Interp UpscalePolicy::current() const { return current_; }
std::uint64_t UpscalePolicy::downgrades() const { return downgrades_; }
std::uint64_t UpscalePolicy::upgrades() const { return upgrades_; }

} // namespace up
