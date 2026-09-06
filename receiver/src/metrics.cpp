#include "recv/metrics.hpp"
#include <cmath>

// ---------------------------------------------------------------------------
// MetricsWindow: real-time metrics over a sliding window of frames.
// ---------------------------------------------------------------------------
namespace rx {

MetricsWindow::MetricsWindow(std::uint64_t window_us) : window_us_(window_us) {}

void MetricsWindow::add(std::uint64_t emit_us, std::uint64_t recv_us) {
    //   - latency in ms = (recv_us - emit_us) / 1000. Note: recv and emit are
    //     uint64 -> compute the difference as SIGNED to avoid overflow if
    //     recv < emit (clock drift), and clamp to >= 0.
    //   - push {recv_us, latency}
    //   - evict from the front while the oldest is older than the window:
    //     front.recv_us + window_us_ < recv_us
    double latency_ms = (static_cast<std::int64_t>(recv_us) - static_cast<std::int64_t>(emit_us)) / 1000.0;
    // consider casting to a larger type for the overflow?
    if (latency_ms < 0) {
        latency_ms = 0;
    }

    samples_.push_back({recv_us, latency_ms});

    while (!samples_.empty() && samples_.front().recv_us + window_us_ < recv_us) {
        samples_.pop_front();
    }
}

double MetricsWindow::fps() const {
    // number of samples / (window_us_ in seconds).
    if (window_us_ <= 0) {
        return 0.0; 
    }
    return static_cast<double>(samples_.size()) / (static_cast<double>(window_us_) / 1'000'000.0);
}

double MetricsWindow::avg_latency_ms() const {
    // average of the latency_ms values (0 if empty).
    if (samples_.empty()) {
        return 0.0;
    }
    double sum_latency = 0.0;
    for (const auto& sample : samples_) {
        sum_latency += sample.latency_ms;
    }
    return sum_latency / static_cast<double>(samples_.size());
}

double MetricsWindow::jitter_ms() const {
    // mean absolute deviation of the latencies around their mean
    //   ( mean of |latency_i - mean| ), 0 if empty.
    if (samples_.empty()) {
        return 0.0;
    }

    double avg_latency = avg_latency_ms();
    double sum_abs_diff = 0.0;
    
    for (const auto& sample : samples_) {
        sum_abs_diff += std::fabs(sample.latency_ms - avg_latency);
    }
    return sum_abs_diff / static_cast<double>(samples_.size());
}

} // namespace rx
