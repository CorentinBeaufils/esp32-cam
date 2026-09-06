#pragma once

#include <chrono>
#include <cstdint>

// ---------------------------------------------------------------------------
// Pacer: rate regulator for a fixed-rate emission loop.
//
// The problem (your remark from the start): an ESP32 takes a VARIABLE amount of
// time to capture and send an image. If we emitted "as fast as possible", the
// rate would be irregular; if we always emitted "after a fixed delay", the
// slightest lag would accumulate and we would drift.
//
// The Pacer implements a fixed time step with anti-burst protection:
//   - it aims for a regular deadline (period = 1 / fps);
//   - if we are ahead, it says how long to wait;
//   - if we are BEHIND (emission took too long), it does NOT try to catch up by
//     emitting in a burst: it resynchronizes and COUNTS the missed "beats" --
//     exactly the frames dropped by an overloaded ESP32.
//
// Pure logic, no asio: testable by injecting synthetic timestamps.
// ---------------------------------------------------------------------------
namespace sim {

class Pacer {
public:
    using clock = std::chrono::steady_clock;

    explicit Pacer(double target_fps);

    // Call ONCE before the loop, to arm the first deadline.
    void start(clock::time_point now);

    // Call after emitting an image. Advances to the next deadline and returns
    // the time to wait before the next emission (>= 0). If we are more than
    // one period behind, resynchronizes on `now` and counts the missed
    // beat(s).
    std::chrono::nanoseconds next_wait(clock::time_point now);

    std::chrono::nanoseconds period() const { return period_; }
    std::uint64_t skipped_beats() const { return skipped_; }

private:
    std::chrono::nanoseconds period_;
    clock::time_point deadline_;
    std::uint64_t skipped_ = 0;
};

} // namespace sim
