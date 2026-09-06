#include "bench/run_report.hpp"

#include <cmath>
#include <sstream>

// ---------------------------------------------------------------------------
// RunReport: common report of a receive run (throughput, losses, jitter...).
//
// You fill in on_frame() and snapshot(). The CSV serialization is already
// written at the bottom (it depends only on snapshot()). Re-read the header
// (run_report.hpp) for the exact meaning of each field; the ASSIGNMENT details
// the why (expected/lost, throughput fencepost, jitter).
// ---------------------------------------------------------------------------
namespace bench {

RunReport::RunReport(std::size_t jitter_window)
    : jitter_window_(jitter_window == 0 ? 1 : jitter_window) {}

void RunReport::on_frame(std::uint32_t frame_id, double arrival_ms, bool crc_ok) {
    // for EACH delivered frame:
    //   1) ++delivered_ ; if !crc_ok -> ++corrupt_.
    //   2) SEQUENCE:
    //      - first frame (!have_any_): it SETS base_id_ and max_id_ (we can't
    //        know anything about losses BEFORE it);
    //      - otherwise: if frame_id < prev_id_ -> ++reordered_ ;
    //                   if frame_id > max_id_  -> max_id_ = frame_id.
    //      - duplicate: if frame_id is ALREADY in seen_ -> ++duplicate_.
    //        (Trick: seen_.insert(id).second is false if the id already existed.)
    //      - set prev_id_ = frame_id at the end.
    //   3) TIMING:
    //      - first time (!have_time_): first_ms_ = arrival_ms ;
    //      - otherwise: push (arrival_ms - prev_ms_) into gaps_, bounded to
    //        jitter_window_ (pop_front while we exceed it).
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
    // computes the report from the counters:
    //   - delivered/unique(=seen_.size())/corrupt/duplicate/reordered: copied over.
    //   - LOSS: expected = (max_id_ - base_id_) + 1 ; lost = max(0, expected -
    //     unique) ; loss_pct = 100 * lost / expected (if have_any_).
    //   - seconds = (last_ms_ - first_ms_)/1000 (if have_time_).
    //   - fps = (delivered - 1) / seconds  [intervals, not points!] if
    //     seconds > 0 and delivered > 1.
    //   - jitter_ms = mean absolute deviation of gaps_ (mean, then mean of
    //     |g - mean|); 0 if gaps_ is empty.
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

// --- Provided: serialization of the common format (depends only on snapshot()). ---
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
