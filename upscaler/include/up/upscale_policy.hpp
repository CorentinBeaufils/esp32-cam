#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// UpscalePolicy: chooses, image after image, the interpolation METHOD to use
// in order to stay within the real-time budget.
//
// The concrete problem: at ~28 fps you have ~35 ms per image to do EVERYTHING
// (decode + enlarge + display). Enlargement algorithms do not cost the same --
// from cheapest to most expensive / from coarsest to finest:
//
//     Nearest  <  Linear  <  Cubic  <  Lanczos
//
// Lanczos is the nicest but may not fit within the budget depending on the
// machine and the resolution. Rather than choosing once and for all, we ADAPT:
// we measure the real time of the upscale, and
//   - if we exceed the budget -> we step down one level (cheaper);
//   - if we are comfortably UNDER the budget for a while -> we try one level
//     nicer again.
//
// This is exactly the rate-control principle of a video encoder, in miniature.
// The HYSTERESIS (the "for a while") avoids oscillation: without it, we would
// flip between two levels on every image.
//
// PURE and DETERMINISTIC logic -> testable case by case.
// ---------------------------------------------------------------------------
namespace up {

// Ordered from cheapest/coarsest (0) to most expensive/finest (3). The order IS
// used (we go up/down via ++/--), do not change it without thinking.
enum class Interp {
    Nearest = 0,
    Linear  = 1,
    Cubic   = 2,
    Lanczos = 3,
};

const char* to_string(Interp interp);

class UpscalePolicy {
public:
    // budget_ms: max time allowed per frame for the upscale.
    // start    : starting method (Cubic: a good compromise).
    explicit UpscalePolicy(double budget_ms, Interp start = Interp::Cubic);

    // Call AFTER each upscale with the measured time (ms). Updates the level and
    // returns the method to use for the NEXT frame.
    Interp update(double measured_ms);

    Interp current() const;
    std::uint64_t downgrades() const;   // number of step-downs (telemetry)
    std::uint64_t upgrades() const;     // number of step-ups

private:
    double budget_ms_;
    Interp current_;
    int good_streak_ = 0;               // consecutive frames well under budget
    std::uint64_t downgrades_ = 0;
    std::uint64_t upgrades_ = 0;
};

} // namespace up
