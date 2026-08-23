#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

// ---------------------------------------------------------------------------
// ScaleStats : statistiques glissantes sur le TEMPS DE TRAITEMENT d'une trame
// (ici : le coût de l'upscale, en millisecondes).
//
// C'est la cousine de MetricsWindow, mais elle ne mesure pas le
// réseau : elle mesure le COÛT CPU de ton pipeline d'affichage. On veut savoir
// « est-ce que j'agrandis chaque image assez vite pour tenir le temps réel ? ».
//
// Fenêtre par NOMBRE d'échantillons (les N dernières trames), pas par durée :
// on raisonne « sur les 120 dernières images », pas « sur la dernière seconde ».
//
// Trois vues utiles :
//   - avg_ms()  : le coût typique ;
//   - p95_ms()  : le coût dans le pire des cas courant (95e centile) -- c'est
//                 LUI qui fait sauter des trames, pas la moyenne ;
//   - over_budget(budget) : combien de trames récentes ont dépassé le budget.
//
// Logique PURE, aucune dépendance (ni OpenCV ni asio) -> testable exactement.
// ---------------------------------------------------------------------------
namespace up {

class ScaleStats {
public:
    // window : nombre d'échantillons conservés (défaut 120 ~ 4-5 s à 25 fps).
    explicit ScaleStats(std::size_t window = 120);

    // Enregistre le coût d'une trame (ms). Évince le plus ancien si plein.
    void record(double ms);

    std::size_t count() const;      // échantillons actuellement dans la fenêtre
    double avg_ms() const;          // moyenne (0 si vide)
    double max_ms() const;          // maximum (0 si vide)
    double p95_ms() const;          // 95e centile (0 si vide)

    // Nombre d'échantillons de la fenêtre strictement au-dessus du budget.
    std::size_t over_budget(double budget_ms) const;

private:
    std::size_t window_;
    std::deque<double> samples_;
};

} // namespace up
