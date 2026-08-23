#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

// ---------------------------------------------------------------------------
// MetricsWindow : mesures temps réel sur une FENÊTRE GLISSANTE des dernières
// trames reçues. Logique pure (aucun asio, aucune horloge interne) : on lui
// fournit les instants, elle calcule -- donc testable exactement.
//
// Ce qu'elle mesure (le reste -- pertes, corruption -- vient de cam::Telemetry,
// déjà rempli par le Reassembler) :
//   - le DÉBIT (fps) : nombre de trames dans la fenêtre / durée de la fenêtre ;
//   - la LATENCE : temps entre l'émission (timestamp_us de la trame) et la
//     réception -- moyenne sur la fenêtre ;
//   - la GIGUE (jitter) : à quel point la latence varie (écart absolu moyen).
//
// La fenêtre glissante évite deux écueils : une moyenne depuis le début (qui
// lisserait tout et masquerait une dégradation récente) et une mesure
// instantanée (trop bruitée). On regarde "la dernière seconde".
// ---------------------------------------------------------------------------
namespace rx {

class MetricsWindow {
public:
    // window_us : largeur de la fenêtre en microsecondes (défaut 1 s).
    explicit MetricsWindow(std::uint64_t window_us = 1'000'000);

    // Enregistre une trame complète : instant d'émission et de réception (µs).
    // Évince au passage les échantillons plus vieux que la fenêtre.
    void add(std::uint64_t emit_us, std::uint64_t recv_us);

    std::size_t count() const { return samples_.size(); }  // trames dans la fenêtre
    double fps() const;                                    // trames / seconde
    double avg_latency_ms() const;                         // latence moyenne (ms)
    double jitter_ms() const;                              // écart absolu moyen (ms)

private:
    struct Sample {
        std::uint64_t recv_us;
        double latency_ms;
    };
    std::uint64_t window_us_;
    std::deque<Sample> samples_;
};

} // namespace rx
