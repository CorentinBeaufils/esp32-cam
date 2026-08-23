#pragma once

#include <chrono>
#include <cstdint>

// ---------------------------------------------------------------------------
// Pacer : régulateur de cadence pour une boucle d'émission à débit fixe.
//
// Le problème (ta remarque du départ) : une ESP32 met un temps VARIABLE à
// capturer et envoyer une image. Si on émettait "aussi vite que possible", le
// débit serait irrégulier ; si on émettait toujours "après un délai fixe", le
// moindre retard s'accumulerait et on dériverait.
//
// Le Pacer implémente un pas de temps fixe avec protection anti-rafale :
//   - il vise une échéance régulière (période = 1 / fps) ;
//   - si on est en avance, il dit combien de temps attendre ;
//   - si on est EN RETARD (l'émission a pris trop longtemps), il n'essaie PAS
//     de rattraper en envoyant en rafale : il se resynchronise et COMPTE les
//     "battements" manqués -- exactement les images sautées d'un ESP32 surchargé.
//
// Logique pure, sans asio : testable en injectant des instants synthétiques.
// ---------------------------------------------------------------------------
namespace sim {

class Pacer {
public:
    using clock = std::chrono::steady_clock;

    explicit Pacer(double target_fps);

    // À appeler UNE fois avant la boucle, pour armer la première échéance.
    void start(clock::time_point now);

    // À appeler après avoir émis une image. Avance à l'échéance suivante et
    // renvoie le temps à attendre avant la prochaine émission (>= 0). Si on a
    // plus d'une période de retard, se resynchronise sur `now` et compte le(s)
    // battement(s) manqué(s).
    std::chrono::nanoseconds next_wait(clock::time_point now);

    std::chrono::nanoseconds period() const { return period_; }
    std::uint64_t skipped_beats() const { return skipped_; }

private:
    std::chrono::nanoseconds period_;
    clock::time_point deadline_;
    std::uint64_t skipped_ = 0;
};

} // namespace sim
