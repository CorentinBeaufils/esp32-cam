#include "recv/metrics.hpp"

#include <cmath>

// ---------------------------------------------------------------------------
// TP-P1b — corrigé commenté (MetricsWindow).
// ---------------------------------------------------------------------------
namespace rx {

MetricsWindow::MetricsWindow(std::uint64_t window_us) : window_us_(window_us) {}

void MetricsWindow::add(std::uint64_t emit_us, std::uint64_t recv_us) {
    // Différence en SIGNÉ : recv_us et emit_us sont des uint64. Si les horloges
    // de l'émetteur et du récepteur diffèrent un peu (cas réel dès que ce sont
    // deux machines), recv peut être < emit et une soustraction non signée
    // donnerait un nombre gigantesque. On calcule en int64 et on borne à 0.
    const std::int64_t delta_us =
        static_cast<std::int64_t>(recv_us) - static_cast<std::int64_t>(emit_us);
    const double latency_ms = (delta_us > 0) ? (static_cast<double>(delta_us) / 1000.0) : 0.0;

    samples_.push_back(Sample{recv_us, latency_ms});

    // Éviction : on retire par l'avant tout échantillon dont la réception est
    // plus vieille que la fenêtre par rapport à la trame qu'on vient d'ajouter.
    while (!samples_.empty() && samples_.front().recv_us + window_us_ < recv_us) {
        samples_.pop_front();
    }
}

double MetricsWindow::fps() const {
    // Débit = nombre de trames observées sur la largeur de fenêtre. Une fois la
    // fenêtre "chaude", count ≈ fps * fenêtre_en_secondes.
    const double window_s = static_cast<double>(window_us_) / 1e6;
    if (window_s <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(samples_.size()) / window_s;
}

double MetricsWindow::avg_latency_ms() const {
    if (samples_.empty()) {
        return 0.0;
    }
    double somme = 0.0;
    for (const auto& s : samples_) {
        somme += s.latency_ms;
    }
    return somme / static_cast<double>(samples_.size());
}

double MetricsWindow::jitter_ms() const {
    if (samples_.empty()) {
        return 0.0;
    }
    // Écart absolu moyen (mean absolute deviation) autour de la moyenne : plus
    // robuste et plus lisible qu'un écart-type pour de la gigue réseau, et sans
    // racine carrée. C'est "de combien la latence bouge, en moyenne".
    const double moyenne = avg_latency_ms();
    double somme = 0.0;
    for (const auto& s : samples_) {
        somme += std::fabs(s.latency_ms - moyenne);
    }
    return somme / static_cast<double>(samples_.size());
}

} // namespace rx
