#include "bench/run_report.hpp"

#include <cmath>
#include <sstream>

// ---------------------------------------------------------------------------
// TP-P4 — corrigé commenté (RunReport).
// ---------------------------------------------------------------------------
namespace bench {

RunReport::RunReport(std::size_t jitter_window)
    : jitter_window_(jitter_window == 0 ? 1 : jitter_window) {}

void RunReport::on_frame(std::uint32_t frame_id, double arrival_ms, bool crc_ok) {
    ++delivered_;
    if (!crc_ok) ++corrupt_;

    // --- Séquence : trous / doublons / désordre à partir des frame_id. --------
    if (!have_any_) {
        // Première trame : elle FIXE la base de l'intervalle observé. On ne peut
        // pas savoir ce qui a été perdu AVANT elle (on n'a pas de référence).
        have_any_ = true;
        base_id_ = frame_id;
        max_id_ = frame_id;
    } else {
        if (frame_id < prev_id_) ++reordered_;   // arrivée après un id plus grand
        if (frame_id > max_id_) max_id_ = frame_id;
    }
    // Doublon : ce frame_id a-t-il déjà été livré ? (insert renvoie false sinon.)
    if (!seen_.insert(frame_id).second) ++duplicate_;
    prev_id_ = frame_id;

    // --- Timing : durée observée + gigue (inter-arrivées). --------------------
    if (!have_time_) {
        have_time_ = true;
        first_ms_ = arrival_ms;
    } else {
        gaps_.push_back(arrival_ms - prev_ms_);
        while (gaps_.size() > jitter_window_) gaps_.pop_front();
    }
    last_ms_ = arrival_ms;
    prev_ms_ = arrival_ms;
}

Report RunReport::snapshot() const {
    Report r;
    r.delivered = delivered_;
    r.unique    = seen_.size();
    r.corrupt   = corrupt_;
    r.duplicate = duplicate_;
    r.reordered = reordered_;

    // Perte : sur l'intervalle [base, max] on ATTEND (max - base + 1) frame_id
    // distincts ; ce qui manque à l'appel, c'est perdu. (RTP fait pareil :
    // expected = highest - base + 1, lost = expected - received.)
    if (have_any_) {
        const std::uint64_t expected =
            static_cast<std::uint64_t>(max_id_ - base_id_) + 1;
        r.lost = (expected > r.unique) ? (expected - r.unique) : 0;
        r.loss_pct = 100.0 * static_cast<double>(r.lost) / static_cast<double>(expected);
    }

    // Durée observée + débit. n trames sur (n-1) INTERVALLES : le débit se calcule
    // sur les intervalles, pas les points (sinon on sur-compte d'un cran).
    r.seconds = have_time_ ? (last_ms_ - first_ms_) / 1000.0 : 0.0;
    if (r.seconds > 0.0 && r.delivered > 1) {
        r.fps = static_cast<double>(r.delivered - 1) / r.seconds;
    }

    // Gigue : écart absolu moyen des inter-arrivées autour de leur moyenne --
    // même choix que rx::MetricsWindow (lisible, pas de racine carrée).
    if (!gaps_.empty()) {
        double somme = 0.0;
        for (double g : gaps_) somme += g;
        const double moyenne = somme / static_cast<double>(gaps_.size());
        double mad = 0.0;
        for (double g : gaps_) mad += std::fabs(g - moyenne);
        r.jitter_ms = mad / static_cast<double>(gaps_.size());
    }
    return r;
}

std::string RunReport::csv_header() {
    return "delivered,unique,lost,corrupt,duplicate,reordered,seconds,fps,loss_pct,jitter_ms";
}

std::string RunReport::to_csv() const {
    const Report r = snapshot();
    std::ostringstream os;
    os << r.delivered << ',' << r.unique << ',' << r.lost << ',' << r.corrupt << ','
       << r.duplicate << ',' << r.reordered << ',' << r.seconds << ',' << r.fps << ','
       << r.loss_pct << ',' << r.jitter_ms;
    return os.str();
}

} // namespace bench
