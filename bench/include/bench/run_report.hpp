#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>

// ---------------------------------------------------------------------------
// RunReport : le RELEVÉ COMMUN d'un run de réception, identique quel que soit
// le récepteur derrière (ton C++/asio, un baseline recvfrom bloquant, un jour
// du Node...). C'est CE format qu'on compare : deux implémentations, même flux
// rejoué, on lit les deux lignes CSV côte à côte.
//
// Pourquoi une classe à part alors que rx::MetricsWindow existe déjà ? Parce
// que MetricsWindow répond à « comment ça va MAINTENANT » (fenêtre glissante,
// interne à un récepteur). Ici on veut « comment s'est passé CE run, en un
// enregistrement comparable » -- l'unité de comparaison du banc.
//
// Le point neuf, c'est la comptabilité de SÉQUENCE à partir des frame_id :
//   - PERTE     : des frame_id manquent dans l'intervalle observé (trous) ;
//   - CORRUPTION: la trame est arrivée mais son payload_crc ne collait pas ;
//   - DOUBLON   : un même frame_id livré plus d'une fois ;
//   - DÉSORDRE  : une trame arrivée après un id supérieur déjà vu.
// UDP ne garantit ni livraison, ni ordre, ni intégrité : ces quatre chiffres
// sont exactement ce que le protocole (frame_id, payload_crc) permet de
// reconstituer côté PC.
//
// Logique PURE et DÉTERMINISTE (aucun asio, aucune horloge interne : on lui
// fournit les instants) -> testable au cas près, comme ScaleStats/MetricsWindow.
// ---------------------------------------------------------------------------
namespace bench {

// L'enregistrement comparable. Une struct plate, sérialisable en une ligne CSV.
struct Report {
    std::uint64_t delivered = 0;   // trames complètes livrées (appels à on_frame)
    std::uint64_t unique    = 0;   // frame_id distincts livrés
    std::uint64_t lost      = 0;   // frame_id jamais livrés (trous dans l'intervalle)
    std::uint64_t corrupt   = 0;   // trames livrées avec CRC KO
    std::uint64_t duplicate = 0;   // livraisons d'un frame_id déjà vu
    std::uint64_t reordered = 0;   // trames arrivées après un id supérieur déjà vu
    double        seconds   = 0.0; // durée observée (1re -> dernière arrivée)
    double        fps       = 0.0; // débit = intervalles / durée
    double        loss_pct  = 0.0; // 100 * lost / expected
    double        jitter_ms = 0.0; // écart absolu moyen des inter-arrivées
};

class RunReport {
public:
    // jitter_window : nb d'inter-arrivées gardées pour l'écart absolu moyen.
    explicit RunReport(std::size_t jitter_window = 300);

    // À appeler pour CHAQUE trame complète qu'un récepteur produit.
    //   frame_id   : cam::Header.frame_id -> sert à détecter trous/doublons/désordre
    //   arrival_ms : instant de réception, horloge monotone du PC (ms)
    //   crc_ok     : payload_crc validé ?
    void on_frame(std::uint32_t frame_id, double arrival_ms, bool crc_ok);

    Report snapshot() const;   // calcule le relevé commun à l'instant t

    // Sérialisation du format commun : une ligne CSV + l'en-tête assorti.
    std::string        to_csv() const;
    static std::string csv_header();

private:
    std::size_t jitter_window_;

    // Comptage de séquence.
    bool          have_any_ = false;
    std::uint32_t base_id_  = 0;   // premier frame_id vu
    std::uint32_t max_id_   = 0;   // plus grand frame_id vu
    std::uint32_t prev_id_  = 0;   // frame_id de l'appel précédent (pour le désordre)
    std::unordered_set<std::uint32_t> seen_;   // frame_id distincts déjà livrés

    std::uint64_t delivered_ = 0;
    std::uint64_t corrupt_   = 0;
    std::uint64_t duplicate_ = 0;
    std::uint64_t reordered_ = 0;

    // Timing.
    bool             have_time_ = false;
    double           first_ms_  = 0.0;
    double           last_ms_    = 0.0;
    double           prev_ms_   = 0.0;
    std::deque<double> gaps_;      // inter-arrivées récentes (ms), bornées
};

} // namespace bench
