#include "up/scale_stats.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// ScaleStats: sliding statistics over the upscale cost (ms).
//
// A sliding window by NUMBER of samples. You have already done the temporal
// cousin (MetricsWindow); here it is simpler (no time-based eviction).
// ---------------------------------------------------------------------------
namespace up {

ScaleStats::ScaleStats(std::size_t window)
    : window_(window == 0 ? 1 : window) {}

void ScaleStats::record(double ms) {
    // push the sample, then evict from the front while the size
    // exceeds window_.
    samples_.push_back(ms);
    while (samples_.size() > window_) {
        samples_.erase(samples_.begin());
    }
}

std::size_t ScaleStats::count() const {
    return samples_.size();
}

double ScaleStats::avg_ms() const {
    // average of the samples (0 if empty).
    double sum = 0.0;
    for (double sample : samples_) {
        sum += sample;
    }
    return samples_.empty() ? 0.0 : sum / samples_.size();
}

double ScaleStats::max_ms() const {
    // maximum (0 if empty). std::max_element helps.
    return samples_.empty() ? 0.0 : *std::max_element(samples_.begin(), samples_.end()); // is it allowed to dereference an iterator returned by std::max_element?
}

double ScaleStats::p95_ms() const {
    // 95th percentile "nearest-rank".
    //   - sorts a COPY (does not reorder samples_: the chronological order is
    //     used by the viewer);
    //   - rank = ceil(0.95 * n), clamped to [1, n];
    //   - returns the value at that rank (careful: rank is 1-based -> index 0-based).
    if (samples_.empty()) {
        return 0.0;
    }
    std::deque<double> copy = samples_;
    std::sort(copy.begin(), copy.end());
    std::size_t n = copy.size();
    std::size_t rank = static_cast<std::size_t>(std::ceil(0.95 * n)); // should always be <= n

    if (rank < 1) {
        rank = 1;
    } else if (rank > n) {
        rank = n;
    }
    return copy[rank - 1]; 
}

std::size_t ScaleStats::over_budget(double budget_ms) const {
    // counts the samples strictly above the budget.
    std::size_t count = 0;
    for (double sample : samples_) {
        if (sample > budget_ms) {
            ++count;
        }
    }
    return count;
}

} // namespace up
