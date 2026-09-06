#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

// ---------------------------------------------------------------------------
// ScaleStats: sliding statistics over the PROCESSING TIME of a frame
// (here: the cost of the upscale, in milliseconds).
//
// It is the cousin of MetricsWindow, but it does not measure the
// network: it measures the CPU COST of your display pipeline. We want to know
// "am I enlarging each image fast enough to keep up in real time?".
//
// Window by NUMBER of samples (the last N frames), not by duration:
// we reason "over the last 120 images", not "over the last second".
//
// Three useful views:
//   - avg_ms()  : the typical cost;
//   - p95_ms()  : the cost in the common worst case (95th percentile) -- it is
//                 THIS that makes frames drop, not the average;
//   - over_budget(budget): how many recent frames exceeded the budget.
//
// PURE logic, no dependency (neither OpenCV nor asio) -> exactly testable.
// ---------------------------------------------------------------------------
namespace up {

class ScaleStats {
public:
    // window: number of samples kept (default 120 ~ 4-5 s at 25 fps).
    explicit ScaleStats(std::size_t window = 120);

    // Records the cost of a frame (ms). Evicts the oldest if full.
    void record(double ms);

    std::size_t count() const;      // samples currently in the window
    double avg_ms() const;          // average (0 if empty)
    double max_ms() const;          // maximum (0 if empty)
    double p95_ms() const;          // 95th percentile (0 if empty)

    // Number of samples in the window strictly above the budget.
    std::size_t over_budget(double budget_ms) const;

private:
    std::size_t window_;
    std::deque<double> samples_;
};

} // namespace up
