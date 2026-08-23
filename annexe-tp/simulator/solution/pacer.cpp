#include "sim/pacer.hpp"

// ---------------------------------------------------------------------------
// TP-P1a — corrigé commenté (Pacer).
// ---------------------------------------------------------------------------
namespace sim {

Pacer::Pacer(double target_fps) {
    // fps <= 0 n'a pas de sens : on retombe sur 1 fps pour ne pas diviser par 0.
    const double fps = (target_fps > 0.0) ? target_fps : 1.0;
    // Période en nanosecondes = 1 seconde / fps. On calcule en double puis on
    // convertit, pour ne pas perdre de précision sur des fps non entiers.
    const double periode_ns = 1e9 / fps;
    period_ = std::chrono::nanoseconds(static_cast<std::int64_t>(periode_ns));
}

void Pacer::start(clock::time_point now) {
    // La première image doit sortir "maintenant" : l'échéance de référence est
    // l'instant de départ. Le premier next_wait ajoutera une période.
    deadline_ = now;
}

std::chrono::nanoseconds Pacer::next_wait(clock::time_point now) {
    // L'échéance de la PROCHAINE image = échéance précédente + une période.
    // On raisonne sur des échéances absolues (et non "attendre period_ depuis
    // maintenant") : c'est ce qui empêche la dérive. Même si une image sort un
    // peu en retard, la suivante vise toujours son créneau théorique.
    deadline_ += period_;

    if (now <= deadline_) {
        // À l'heure (ou en avance) : on attend jusqu'au créneau.
        return deadline_ - now;
    }

    // En retard : l'émission a dépassé son créneau. On NE rattrape PAS en
    // envoyant plus vite (ce serait une rafale qui saturerait le réseau) : on
    // compte les créneaux manqués et on se resynchronise sur l'instant présent.
    const auto retard = now - deadline_;
    skipped_ += static_cast<std::uint64_t>(retard / period_);
    deadline_ = now;   // repart d'ici ; le prochain créneau sera now + période
    return std::chrono::nanoseconds(0);
}

} // namespace sim
