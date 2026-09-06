#pragma once

#include "cam/reassembler.hpp"   // cam::Frame

#include <cstdint>
#include <memory>
#include <mutex>

// ---------------------------------------------------------------------------
// LatestFrame: a thread-safe handoff point between the NETWORK thread (which
// produces complete frames) and the DISPLAY thread (which consumes them at its
// own pace).
//
// "Most recent wins" policy: if a new frame arrives before the display has
// consumed the previous one, we OVERWRITE the old one. In real time, showing a
// stale image is pointless -- we always want the freshest one. This is the
// "drop the oldest" buffer from the very beginning, applied at the
// network/display boundary.
//
// Two threads access it at the same time: store() and take() MUST be
// protected. That is the only real challenge in this file -- and it is tested
// under ThreadSanitizer.
//
// We exchange std::shared_ptr<const cam::Frame>: the frame is immutable and
// shared, so there is neither an expensive copy nor a race on its content.
// ---------------------------------------------------------------------------
namespace disp {

class LatestFrame {
public:
    // Called by the NETWORK thread. Replaces the pending frame (if any).
    void store(std::shared_ptr<const cam::Frame> frame);

    // Called by the DISPLAY thread. Returns the last frame stored and empties
    // the slot; returns nullptr if there is nothing new.
    std::shared_ptr<const cam::Frame> take();

    // Number of frames overwritten without having been consumed (telemetry:
    // "the display is not keeping up with the network").
    std::uint64_t dropped() const;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const cam::Frame> slot_;
    std::uint64_t dropped_ = 0;
};

} // namespace disp
