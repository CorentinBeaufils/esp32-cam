#include "up/scale_stats.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// TP-P3 — corrigé commenté (ScaleStats).
// ---------------------------------------------------------------------------
namespace up {

ScaleStats::ScaleStats(std::size_t window)
    : window_(window == 0 ? 1 : window) {}

void ScaleStats::record(double ms) {
    samples_.push_back(ms);
    // Fenêtre bornée : on jette par l'avant tant qu'on dépasse la taille.
    while (samples_.size() > window_) {
        samples_.pop_front();
    }
}

std::size_t ScaleStats::count() const {
    return samples_.size();
}

double ScaleStats::avg_ms() const {
    if (samples_.empty()) return 0.0;
    double somme = 0.0;
    for (double v : samples_) somme += v;
    return somme / static_cast<double>(samples_.size());
}

double ScaleStats::max_ms() const {
    if (samples_.empty()) return 0.0;
    return *std::max_element(samples_.begin(), samples_.end());
}

double ScaleStats::p95_ms() const {
    if (samples_.empty()) return 0.0;
    // Centile par "nearest-rank" : on trie une COPIE (ne jamais réordonner la
    // fenêtre elle-même, l'ordre chronologique nous sert ailleurs), puis on
    // prend le rang ceil(0.95 * n).
    std::vector<double> trie(samples_.begin(), samples_.end());
    std::sort(trie.begin(), trie.end());
    const std::size_t n = trie.size();
    std::size_t rang = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(n)));
    if (rang == 0) rang = 1;            // garde-fou
    if (rang > n) rang = n;
    return trie[rang - 1];             // rang 1-based -> index 0-based
}

std::size_t ScaleStats::over_budget(double budget_ms) const {
    std::size_t n = 0;
    for (double v : samples_) {
        if (v > budget_ms) ++n;
    }
    return n;
}

} // namespace up
