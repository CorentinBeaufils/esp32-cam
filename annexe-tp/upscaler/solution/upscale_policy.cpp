#include "up/upscale_policy.hpp"

// ---------------------------------------------------------------------------
// TP-P3 — corrigé commenté (UpscalePolicy).
// ---------------------------------------------------------------------------
namespace up {

namespace {
// Réglages de l'hystérésis. On ne remonte en qualité que si on est bien SOUS
// le budget (marge de sécurité) et de façon STABLE (plusieurs trames), pour ne
// pas osciller. On redescend, lui, IMMÉDIATEMENT dès qu'on dépasse : rater le
// temps réel coûte une trame sautée, on ne temporise pas.
constexpr double kUpgradeRatio = 0.6;   // "confortable" = < 60 % du budget
constexpr int    kStableNeeded = 8;     // trames consécutives avant de remonter

Interp lower(Interp i) {
    if (i == Interp::Nearest) return Interp::Nearest;
    return static_cast<Interp>(static_cast<int>(i) - 1);
}
Interp higher(Interp i) {
    if (i == Interp::Lanczos) return Interp::Lanczos;
    return static_cast<Interp>(static_cast<int>(i) + 1);
}
} // namespace

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
    if (measured_ms > budget_ms_) {
        // Dépassement : on casse la série et on redescend d'un cran (si possible).
        good_streak_ = 0;
        const Interp cible = lower(current_);
        if (cible != current_) {
            current_ = cible;
            ++downgrades_;
        }
    } else if (measured_ms < budget_ms_ * kUpgradeRatio) {
        // Confortablement sous le budget : on accumule de la confiance.
        ++good_streak_;
        if (good_streak_ >= kStableNeeded) {
            const Interp cible = higher(current_);
            if (cible != current_) {
                current_ = cible;
                ++upgrades_;
            }
            good_streak_ = 0;          // on repart pour une nouvelle série
        }
    } else {
        // Dans le budget mais pas "confortable" : on reste, on ne monte pas.
        good_streak_ = 0;
    }
    return current_;
}

Interp UpscalePolicy::current() const { return current_; }
std::uint64_t UpscalePolicy::downgrades() const { return downgrades_; }
std::uint64_t UpscalePolicy::upgrades() const { return upgrades_; }

} // namespace up
