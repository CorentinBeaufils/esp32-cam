#pragma once

#include "cam/reassembler.hpp"   // cam::Frame

#include <cstdint>
#include <memory>
#include <mutex>

// ---------------------------------------------------------------------------
// LatestFrame : point de passage thread-safe entre le thread RÉSEAU (qui
// produit des trames complètes) et le thread d'AFFICHAGE (qui les consomme à
// son propre rythme).
//
// Politique « le plus récent gagne » : si une nouvelle trame arrive avant que
// l'affichage ait consommé la précédente, on ÉCRASE l'ancienne. En temps réel,
// afficher une image périmée n'a aucun intérêt -- on veut toujours la plus
// fraîche. C'est ton buffer « drop the oldest » du tout début, appliqué à la
// frontière réseau/affichage.
//
// Deux threads y accèdent en même temps : store() et take() DOIVENT être
// protégés. C'est le seul vrai enjeu de ce fichier -- et il se teste sous
// ThreadSanitizer.
//
// On échange des std::shared_ptr<const cam::Frame> : la trame est immuable et
// partagée, donc ni copie coûteuse ni course sur son contenu.
// ---------------------------------------------------------------------------
namespace disp {

class LatestFrame {
public:
    // Appelé par le thread RÉSEAU. Remplace la trame en attente (le cas échéant).
    void store(std::shared_ptr<const cam::Frame> frame);

    // Appelé par le thread d'AFFICHAGE. Renvoie la dernière trame déposée et
    // vide le slot ; renvoie nullptr s'il n'y a rien de nouveau.
    std::shared_ptr<const cam::Frame> take();

    // Nombre de trames écrasées sans avoir été consommées (télémétrie :
    // « l'affichage ne suit pas le réseau »).
    std::uint64_t dropped() const;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const cam::Frame> slot_;
    std::uint64_t dropped_ = 0;
};

} // namespace disp
