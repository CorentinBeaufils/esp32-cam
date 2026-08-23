#include "bench/run_report.hpp"

#include <cmath>
#include <sstream>

// ---------------------------------------------------------------------------
// RunReport : releve commun d'un run de reception (debit, pertes, gigue...).
//
// Tu remplis on_frame() et snapshot(). La sérialisation CSV est déjà écrite en
// bas (elle ne dépend que de snapshot()). Relis l'en-tête (run_report.hpp) pour
// le sens exact de chaque champ ; l'ENONCE détaille le pourquoi (expected/lost,
// fencepost du débit, gigue).
// ---------------------------------------------------------------------------
namespace bench {

RunReport::RunReport(std::size_t jitter_window)
    : jitter_window_(jitter_window == 0 ? 1 : jitter_window) {}

void RunReport::on_frame(std::uint32_t frame_id, double arrival_ms, bool crc_ok) {
    // pour CHAQUE trame livrée :
    //   1) ++delivered_ ; si !crc_ok -> ++corrupt_.
    //   2) SÉQUENCE :
    //      - 1re trame (!have_any_) : elle FIXE base_id_ et max_id_ (on ne peut
    //        rien savoir des pertes AVANT elle) ;
    //      - sinon : si frame_id < prev_id_ -> ++reordered_ ;
    //                si frame_id > max_id_  -> max_id_ = frame_id.
    //      - doublon : si frame_id est DÉJÀ dans seen_ -> ++duplicate_.
    //        (Astuce : seen_.insert(id).second vaut false si l'id existait déjà.)
    //      - mets prev_id_ = frame_id à la fin.
    //   3) TIMING :
    //      - 1re fois (!have_time_) : first_ms_ = arrival_ms ;
    //      - sinon : pousse (arrival_ms - prev_ms_) dans gaps_, borné à
    //        jitter_window_ (pop_front tant qu'on dépasse).
    //      - last_ms_ = arrival_ms ; prev_ms_ = arrival_ms.
    if (!have_any_) {
        base_id_ = frame_id;
        max_id_ = frame_id;
        have_any_ = true;
    } else {
        if (frame_id < prev_id_) ++reordered_;
        if (frame_id > max_id_) max_id_ = frame_id;
    }
    ++delivered_;

    if (!seen_.insert(frame_id).second) ++duplicate_;
    prev_id_ = frame_id;

    if (!have_time_) {
        first_ms_ = arrival_ms;
        have_time_ = true;
    } else {
        gaps_.push_back(arrival_ms - prev_ms_);
        while (gaps_.size() > jitter_window_) {
            gaps_.pop_front();
        } 
    }

    last_ms_ = arrival_ms;
    prev_ms_ = arrival_ms;

    if (!crc_ok) ++corrupt_;
}

Report RunReport::snapshot() const {
    // calcule le relevé à partir des compteurs :
    //   - delivered/unique(=seen_.size())/corrupt/duplicate/reordered : recopie.
    //   - PERTE : expected = (max_id_ - base_id_) + 1 ; lost = max(0, expected -
    //     unique) ; loss_pct = 100 * lost / expected (si have_any_).
    //   - seconds = (last_ms_ - first_ms_)/1000 (si have_time_).
    //   - fps = (delivered - 1) / seconds  [intervalles, pas points !] si
    //     seconds > 0 et delivered > 1.
    //   - jitter_ms = écart absolu moyen de gaps_ (moyenne, puis moyenne des
    //     |g - moyenne|) ; 0 si gaps_ vide.
    Report r = {delivered_, seen_.size(), 0, corrupt_, duplicate_, reordered_, 0.0, 0.0, 0.0, 0.0};
    std::uint32_t expected = (max_id_ - base_id_) + 1;
    r.lost = (have_any_ && expected > r.unique) ? (expected - r.unique) : 0;
    r.loss_pct = have_any_ ? (100.0 * static_cast<double>(r.lost) / static_cast<double>(expected)) : 0.0;
    r.seconds = have_time_ ? (last_ms_ - first_ms_) / 1000.0 : 0.0;
    r.fps = (r.seconds > 0.0 && delivered_ > 1) ? (static_cast<double>(delivered_ - 1) / r.seconds) : 0.0;

    if (!gaps_.empty()) {
        double mean = 0.0;
        for (double g : gaps_) mean += g;
        mean /= static_cast<double>(gaps_.size());

        double mean_abs_dev = 0.0;
        for (double g : gaps_) mean_abs_dev += std::abs(g - mean);
        mean_abs_dev /= static_cast<double>(gaps_.size());


        r.jitter_ms = mean_abs_dev;
    } else {
        r.jitter_ms = 0.0;
    }
    return r;
}

// --- Fourni : sérialisation du format commun (ne dépend que de snapshot()). ---
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
