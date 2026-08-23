#include "recv/metrics.hpp"
#include <cmath>

// ---------------------------------------------------------------------------
// MetricsWindow : metriques temps reel sur une fenetre glissante de trames.
// ---------------------------------------------------------------------------
namespace rx {

MetricsWindow::MetricsWindow(std::uint64_t window_us) : window_us_(window_us) {}

void MetricsWindow::add(std::uint64_t emit_us, std::uint64_t recv_us) {
    //   - latence en ms = (recv_us - emit_us) / 1000. Attention : recv et emit
    //     sont des uint64 -> calcule la différence en SIGNÉ pour éviter un
    //     débordement si recv < emit (dérive d'horloge), et borne à >= 0.
    //   - empiler {recv_us, latence}
    //   - évincer par l'avant tant que le plus ancien est plus vieux que la
    //     fenêtre : front.recv_us + window_us_ < recv_us
    double latency_ms = (static_cast<std::int64_t>(recv_us) - static_cast<std::int64_t>(emit_us)) / 1000.0;
    //voir pour cast en un type plus grand pour le debordement ?
    if (latency_ms < 0) {
        latency_ms = 0;
    }

    samples_.push_back({recv_us, latency_ms});

    while (!samples_.empty() && samples_.front().recv_us + window_us_ < recv_us) {
        samples_.pop_front();
    }
}

double MetricsWindow::fps() const {
    // nombre d'échantillons / (window_us_ en secondes).
    if (window_us_ <= 0) {
        return 0.0; 
    }
    return static_cast<double>(samples_.size()) / (static_cast<double>(window_us_) / 1'000'000.0);
}

double MetricsWindow::avg_latency_ms() const {
    // moyenne des latency_ms (0 si vide).
    if (samples_.empty()) {
        return 0.0;
    }
    double sum_latency = 0.0;
    for (const auto& sample : samples_) {
        sum_latency += sample.latency_ms;
    }
    return sum_latency / static_cast<double>(samples_.size());
}

double MetricsWindow::jitter_ms() const {
    // écart absolu moyen des latences autour de leur moyenne
    //   ( moyenne de |latence_i - moyenne| ), 0 si vide.
    if (samples_.empty()) {
        return 0.0;
    }

    double avg_latency = avg_latency_ms();
    double sum_abs_diff = 0.0;
    
    for (const auto& sample : samples_) {
        sum_abs_diff += std::fabs(sample.latency_ms - avg_latency);
    }
    return sum_abs_diff / static_cast<double>(samples_.size());
}

} // namespace rx
