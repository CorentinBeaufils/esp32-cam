#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

// ---------------------------------------------------------------------------
// MetricsWindow: real-time measurements over a SLIDING WINDOW of the most
// recent frames received. Pure logic (no asio, no internal clock): you supply
// the timestamps, it does the computation -- so it is exactly testable.
//
// What it measures (the rest -- loss, corruption -- comes from cam::Telemetry,
// already filled in by the Reassembler):
//   - THROUGHPUT (fps): number of frames in the window / window duration;
//   - LATENCY: time between the send (the frame's timestamp_us) and reception
//     -- averaged over the window;
//   - JITTER: how much the latency varies (mean absolute deviation).
//
// The sliding window avoids two pitfalls: an average from the very beginning
// (which would smooth everything out and hide a recent degradation) and an
// instantaneous measurement (too noisy). We look at "the last second".
// ---------------------------------------------------------------------------
namespace rx {

class MetricsWindow {
public:
    // window_us: window width in microseconds (default 1 s).
    explicit MetricsWindow(std::uint64_t window_us = 1'000'000);

    // Records a complete frame: send time and receive time (us). Also evicts
    // any samples older than the window as it goes.
    void add(std::uint64_t emit_us, std::uint64_t recv_us);

    std::size_t count() const { return samples_.size(); }  // frames in the window
    double fps() const;                                    // frames / second
    double avg_latency_ms() const;                         // average latency (ms)
    double jitter_ms() const;                              // mean absolute deviation (ms)

private:
    struct Sample {
        std::uint64_t recv_us;
        double latency_ms;
    };
    std::uint64_t window_us_;
    std::deque<Sample> samples_;
};

} // namespace rx
