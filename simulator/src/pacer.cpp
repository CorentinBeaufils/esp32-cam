#include "sim/pacer.hpp"

// ---------------------------------------------------------------------------
// Pacer: paces emission to a target fps (regular deadlines).
// ---------------------------------------------------------------------------
namespace sim {

Pacer::Pacer(double target_fps) {
    // convert target_fps into a period (nanoseconds). 25 fps -> 40 ms.
    // Guard against fps <= 0 (set a reasonable default value).
    if (target_fps <= 0) {
        target_fps = 25; // 25 fps default
    }
    period_ = std::chrono::nanoseconds(static_cast<long long>(1e9 / target_fps));
}

void Pacer::start(clock::time_point now) {
    // arm the first deadline on `now`.
    deadline_ = now + period_;
}

std::chrono::nanoseconds Pacer::next_wait(clock::time_point now) {
    //   - advance the deadline by one period: deadline_ += period_
    //   - if now <= deadline_  : we are on time -> return (deadline_ - now)
    //   - otherwise (behind)   : count the missed beats
    //         beats = (now - deadline_) / period_
    //         skipped_ += beats
    //     then resynchronize (deadline_ = now) and return 0 (emit right away,
    //     without a catch-up burst).
    if (now <= deadline_) {
        auto wait_time = deadline_ - now;
        deadline_ += period_;
        return wait_time;
    } else {
        std::uint64_t beats = (now - deadline_) / period_; // rounds down during the operation, should we
                                                           //   skip the frame already in progress or not to be "on time"?
                                                           // moreover we reset the timer to 0, which is not necessarily the case
        skipped_ += beats;
        deadline_ = now + period_;
        return std::chrono::nanoseconds(0);
    }
}

} // namespace sim
