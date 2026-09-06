#include "up/upscale_policy.hpp"

// ---------------------------------------------------------------------------
// UpscalePolicy: adaptive controller for the interpolation method.
//
// Core of the module: an adaptive quality controller. See the header
// (upscale_policy.hpp) for the rule; the SPEC details the hysteresis.
// ---------------------------------------------------------------------------
namespace up {

const char* to_string(Interp interp) {
    switch (interp) {
        case Interp::Nearest: return "nearest";
        case Interp::Linear:  return "linear";
        case Interp::Cubic:   return "cubic";
        case Interp::Lanczos: return "lanczos";
    }
    return "?";
}

UpscalePolicy::UpscalePolicy(double budget_ms, Interp start)
    : budget_ms_(budget_ms), current_(start) {}

Interp UpscalePolicy::update(double measured_ms) {
    // three cases:
    //   1) measured > budget          -> good_streak_ = 0; step down one level
    //                                     (without going below Nearest); ++downgrades_.
    //   2) measured < budget * 0.6    -> ++good_streak_; if good_streak_ reaches
    //                                     the threshold (8): step up one level (without
    //                                     exceeding Lanczos), ++upgrades_, and
    //                                     reset good_streak_ to 0.
    //   3) otherwise (within budget but -> good_streak_ = 0; stay.
    //      not comfortable)
    // Tip: clamp the levels using the enum order (static_cast<int>).

    if (measured_ms > budget_ms_) {
        good_streak_ = 0;
        if (current_ != Interp::Nearest) {
            current_ = static_cast<Interp>(static_cast<int>(current_) - 1); // question about enum cases: is it safe to static_cast<int> an enum class and decrement it?
            ++downgrades_;
        }
    } else if (measured_ms < budget_ms_ * 0.6) {
        ++good_streak_;
        if (good_streak_ >= 8) {
            good_streak_ = 0;
            if (current_ != Interp::Lanczos) {
                current_ = static_cast<Interp>(static_cast<int>(current_) + 1);
                ++upgrades_;
            }
        }
    } else {
        good_streak_ = 0;
    }
    
    return current_;
}

Interp UpscalePolicy::current() const { return current_; }
std::uint64_t UpscalePolicy::downgrades() const { return downgrades_; }
std::uint64_t UpscalePolicy::upgrades() const { return upgrades_; }

} // namespace up
