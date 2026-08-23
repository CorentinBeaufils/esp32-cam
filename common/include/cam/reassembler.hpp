#pragma once

#include "cam/protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

// ---------------------------------------------------------------------------
// Reassembler: consumes raw UDP datagrams and emits COMPLETE JPEG frames. It
// does NO networking -- you feed it bytes, like a byte-stream decoder. That is what
// makes it testable without a socket or hardware.
//
// Real-time handling:
//   - it keeps at most `max_frames_in_flight` frames in progress (default 2);
//   - as soon as a frame completes, OLDER still-incomplete frames are dropped
//     (counted as lost): freshness wins over completeness;
//   - a datagram whose magic/version/CRC does not match is rejected and counted
//     as corrupt.
//
// A frame is emitted only when COMPLETE: the display never sees a half-written
// image (no "read-on-write").
// ---------------------------------------------------------------------------
namespace cam {

struct Frame {
    std::uint32_t frame_id = 0;
    std::uint64_t timestamp_us = 0;
    std::vector<std::uint8_t> jpeg;
};

struct Telemetry {
    std::uint64_t frames_completed = 0;   // frames reassembled and emitted
    std::uint64_t frames_lost = 0;        // frames abandoned (missing fragment)
    std::uint64_t fragments_received = 0;  // valid fragments integrated
    std::uint64_t fragments_rejected = 0;  // magic/version/CRC/inconsistency
    std::uint64_t fragments_late = 0;      // valid but for an already-finalised frame
    std::uint64_t useful_bytes = 0;        // valid payloads accumulated
};

class Reassembler {
public:
    explicit Reassembler(std::size_t max_frames_in_flight = 2);

    // Called once per complete frame, in completion order.
    std::function<void(const Frame&)> on_frame;

    // Feed one raw UDP datagram (header + payload).
    void feed(const std::uint8_t* datagram, std::size_t n);
    void feed(const std::vector<std::uint8_t>& datagram);

    const Telemetry& telemetry() const { return telemetry_; }

    // Number of frames currently being reassembled.
    std::size_t frames_in_flight() const { return in_flight_.size(); }

private:
    struct Partial {
        std::uint64_t timestamp_us = 0;
        std::uint32_t frame_size = 0;
        std::uint16_t fragment_count = 0;
        std::uint16_t received = 0;
        std::vector<bool> present;         // one bit per fragment
        std::vector<std::uint8_t> data;    // buffer of frame_size bytes
    };

    void emit(std::uint32_t frame_id, Partial& f);
    void drop_older_than(std::uint32_t frame_id);
    void enforce_in_flight_limit();
    void finalize(std::uint32_t frame_id);   // updates the floor

    std::size_t max_frames_in_flight_;
    std::map<std::uint32_t, Partial> in_flight_;   // sorted by ascending frame_id
    Telemetry telemetry_;

    // Floor: highest frame_id already finalised (completed or dropped). Any
    // fragment of a frame <= floor is a straggler and is ignored -- otherwise a
    // duplicate UDP datagram would resurrect an already-emitted frame.
    std::uint32_t floor_ = 0;
    bool has_floor_ = false;
};

} // namespace cam
