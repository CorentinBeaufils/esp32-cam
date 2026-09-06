#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>

// ---------------------------------------------------------------------------
// RunReport: the COMMON REPORT of a receive run, identical whatever receiver is
// behind it (your C++/asio one, a blocking-recvfrom baseline, some day a Node
// one...). THIS is the format we compare: two implementations, the same stream
// replayed, and we read the two CSV rows side by side.
//
// Why a separate class when rx::MetricsWindow already exists? Because
// MetricsWindow answers "how are things RIGHT NOW" (sliding window, internal to
// a receiver). Here we want "how did THIS run go, as one comparable record" --
// the benchmark's unit of comparison.
//
// The new part is the SEQUENCE accounting from the frame_ids:
//   - LOSS       : some frame_ids are missing in the observed interval (gaps);
//   - CORRUPTION : the frame arrived but its payload_crc did not match;
//   - DUPLICATE  : the same frame_id delivered more than once;
//   - REORDER    : a frame arrived after a higher id had already been seen.
// UDP guarantees neither delivery, nor order, nor integrity: these four numbers
// are exactly what the protocol (frame_id, payload_crc) lets us reconstruct on
// the PC side.
//
// PURE and DETERMINISTIC logic (no asio, no internal clock: the timestamps are
// supplied to it) -> testable to the case, like ScaleStats/MetricsWindow.
// ---------------------------------------------------------------------------
namespace bench {

// The comparable record. A flat struct, serializable as a single CSV row.
struct Report {
    std::uint64_t delivered = 0;   // complete frames delivered (calls to on_frame)
    std::uint64_t unique    = 0;   // distinct frame_ids delivered
    std::uint64_t lost      = 0;   // frame_ids never delivered (gaps in the interval)
    std::uint64_t corrupt   = 0;   // frames delivered with a bad CRC
    std::uint64_t duplicate = 0;   // deliveries of an already-seen frame_id
    std::uint64_t reordered = 0;   // frames arrived after a higher id already seen
    double        seconds   = 0.0; // observed duration (first -> last arrival)
    double        fps       = 0.0; // throughput = intervals / duration
    double        loss_pct  = 0.0; // 100 * lost / expected
    double        jitter_ms = 0.0; // mean absolute deviation of inter-arrivals
};

class RunReport {
public:
    // jitter_window: number of inter-arrivals kept for the mean absolute deviation.
    explicit RunReport(std::size_t jitter_window = 300);

    // To be called for EACH complete frame a receiver produces.
    //   frame_id   : cam::Header.frame_id -> used to detect gaps/duplicates/reorder
    //   arrival_ms : receive instant, the PC's monotonic clock (ms)
    //   crc_ok     : payload_crc validated?
    void on_frame(std::uint32_t frame_id, double arrival_ms, bool crc_ok);

    Report snapshot() const;   // computes the common report at time t

    // Serialization of the common format: one CSV row + the matching header.
    std::string        to_csv() const;
    static std::string csv_header();

private:
    std::size_t jitter_window_;

    // Sequence counting.
    bool          have_any_ = false;
    std::uint32_t base_id_  = 0;   // first frame_id seen
    std::uint32_t max_id_   = 0;   // largest frame_id seen
    std::uint32_t prev_id_  = 0;   // frame_id of the previous call (for reorder)
    std::unordered_set<std::uint32_t> seen_;   // distinct frame_ids already delivered

    std::uint64_t delivered_ = 0;
    std::uint64_t corrupt_   = 0;
    std::uint64_t duplicate_ = 0;
    std::uint64_t reordered_ = 0;

    // Timing.
    bool             have_time_ = false;
    double           first_ms_  = 0.0;
    double           last_ms_    = 0.0;
    double           prev_ms_   = 0.0;
    std::deque<double> gaps_;      // recent inter-arrivals (ms), bounded
};

} // namespace bench
