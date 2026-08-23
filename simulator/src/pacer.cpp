#include "sim/pacer.hpp"

// ---------------------------------------------------------------------------
// Pacer : cadence l'emission a un fps cible (echeances regulieres).
// ---------------------------------------------------------------------------
namespace sim {

Pacer::Pacer(double target_fps) {
    // convertir target_fps en une période (nanosecondes). 25 fps -> 40 ms.
    // Protège-toi d'un fps <= 0 (mets une valeur par défaut raisonnable).
    if (target_fps <= 0) {
        target_fps = 25; // 25 fps default
    }
    period_ = std::chrono::nanoseconds(static_cast<long long>(1e9 / target_fps));
}

void Pacer::start(clock::time_point now) {
    // armer la première échéance sur `now`.
    deadline_ = now + period_;
}

std::chrono::nanoseconds Pacer::next_wait(clock::time_point now) {
    //   - avancer l'échéance d'une période : deadline_ += period_
    //   - si now <= deadline_  : on est à l'heure -> renvoyer (deadline_ - now)
    //   - sinon (en retard)    : compter les battements manqués
    //         beats = (now - deadline_) / period_
    //         skipped_ += beats
    //     puis se resynchroniser (deadline_ = now) et renvoyer 0 (émettre tout
    //     de suite, sans rafale de rattrapage).
    if (now <= deadline_) {
        auto wait_time = deadline_ - now;
        deadline_ += period_;
        return wait_time;
    } else {
        std::uint64_t beats = (now - deadline_) / period_; // arrondit vers le bas lors de l'opération, devrions-nous 
                                                           //   sauter la trame deja entamer ou non pour etre " dans les temps ?" 
                                                           // de plus on mets le chrono a 0 ce qui est pas forcément le cas
        skipped_ += beats;
        deadline_ = now + period_;
        return std::chrono::nanoseconds(0);
    }
}

} // namespace sim
