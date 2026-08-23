#include "up/scale_stats.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// TP-P3 — à toi de jouer (partie 1/2 : ScaleStats).
//   Énoncé : ENONCE.md   Bloqué : INDICES.md   Corrigé : solution/scale_stats.cpp
//
// Une fenêtre glissante par NOMBRE d'échantillons. Tu as déjà fait le cousin
// temporel au TP-P1b ; ici c'est plus simple (pas d'éviction par le temps).
// ---------------------------------------------------------------------------
namespace up {

ScaleStats::ScaleStats(std::size_t window)
    : window_(window == 0 ? 1 : window) {}

void ScaleStats::record(double ms) {
    // TODO : empiler l'échantillon, puis évincer par l'avant tant que la taille
    // dépasse window_.
    samples_.push_back(ms);
    while (samples_.size() > window_) {
        samples_.erase(samples_.begin());
    }
}

std::size_t ScaleStats::count() const {
    return samples_.size();
}

double ScaleStats::avg_ms() const {
    // TODO : moyenne des échantillons (0 si vide).
    double sum = 0.0;
    for (double sample : samples_) {
        sum += sample;
    }
    return samples_.empty() ? 0.0 : sum / samples_.size();
}

double ScaleStats::max_ms() const {
    // TODO : maximum (0 si vide). std::max_element aide.
    return samples_.empty() ? 0.0 : *std::max_element(samples_.begin(), samples_.end()); // est ce qu'il est autorisé de derefenrcer un iterateur retourné par std::max_element ?
}

double ScaleStats::p95_ms() const {
    // TODO : 95e centile "nearest-rank".
    //   - trie une COPIE (ne réordonne pas samples_ : l'ordre chrono sert au
    //     viewer) ;
    //   - rang = ceil(0.95 * n), borné dans [1, n] ;
    //   - renvoie la valeur au rang (attention : rang 1-based -> index 0-based).
    if (samples_.empty()) {
        return 0.0;
    }
    std::deque<double> copy = samples_;
    std::sort(copy.begin(), copy.end());
    std::size_t n = copy.size();
    std::size_t rank = static_cast<std::size_t>(std::ceil(0.95 * n)); // devrait toujours etre <= n 

    if (rank < 1) {
        rank = 1;
    } else if (rank > n) {
        rank = n;
    }
    return copy[rank - 1]; 
}

std::size_t ScaleStats::over_budget(double budget_ms) const {
    // TODO : compte les échantillons strictement au-dessus du budget.
    std::size_t count = 0;
    for (double sample : samples_) {
        if (sample > budget_ms) {
            ++count;
        }
    }
    return count;
}

} // namespace up
