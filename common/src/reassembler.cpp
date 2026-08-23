#include "cam/reassembler.hpp"

#include <algorithm>
#include <utility>

namespace cam {

Reassembler::Reassembler(std::size_t max_frames_in_flight)
    : max_frames_in_flight_(max_frames_in_flight == 0 ? 1 : max_frames_in_flight) {}

void Reassembler::feed(const std::vector<std::uint8_t>& datagram) {
    if (!datagram.empty()) {
        feed(datagram.data(), datagram.size());
    }
}

void Reassembler::feed(const std::uint8_t* dg, std::size_t n) {
    // 1. Minimum size to hold a header.
    if (n < HEADER_SIZE) {
        ++telemetry_.fragments_rejected;
        return;
    }

    const Header h = read_header(dg);

    // 2. Header validation: stray datagram or unknown version.
    if (h.magic != MAGIC || h.version != VERSION) {
        ++telemetry_.fragments_rejected;
        return;
    }

    // 3. Consistency of announced vs received sizes.
    const std::size_t payload_available = n - HEADER_SIZE;
    if (h.payload_size > payload_available || h.fragment_count == 0
        || h.fragment_index >= h.fragment_count) {
        ++telemetry_.fragments_rejected;
        return;
    }
    const std::uint8_t* payload = dg + HEADER_SIZE;

    // 4. CRC: corruption detection. This is where corruption is measured.
    if (crc32(payload, h.payload_size) != h.payload_crc) {
        ++telemetry_.fragments_rejected;
        return;
    }

    // 5. The fragment must fit inside the announced frame.
    const std::size_t offset = static_cast<std::size_t>(h.fragment_index) * MAX_PAYLOAD;
    if (offset + h.payload_size > h.frame_size) {
        ++telemetry_.fragments_rejected;
        return;
    }

    // 6. Straggler: fragment of an already-finalised frame (completed or
    //    dropped). A UDP duplicate of an emitted frame lands here -- accepting
    //    it would recreate and re-emit the frame. We ignore it, but count it.
    if (has_floor_ && h.frame_id <= floor_) {
        ++telemetry_.fragments_late;
        return;
    }

    // 7. Find / create this frame's entry.
    auto it = in_flight_.find(h.frame_id);
    if (it == in_flight_.end()) {
        Partial f;
        f.timestamp_us = h.timestamp_us;
        f.frame_size = h.frame_size;
        f.fragment_count = h.fragment_count;
        f.present.assign(h.fragment_count, false);
        f.data.assign(h.frame_size, 0);
        it = in_flight_.emplace(h.frame_id, std::move(f)).first;
        // Cap the number of frames in flight: if too many, the OLDEST
        // (incomplete) is dropped -> your "buffer of 2, drop the oldest".
        enforce_in_flight_limit();
        it = in_flight_.find(h.frame_id);
        if (it == in_flight_.end()) {
            // The frame we just created was itself the oldest and got dropped
            // (tiny max_frames_in_flight_): nothing more to do.
            ++telemetry_.fragments_received;
            telemetry_.useful_bytes += h.payload_size;
            return;
        }
    }

    Partial& f = it->second;

    // 8. Fragment already received (UDP duplicate)? Ignore without double count.
    if (f.present[h.fragment_index]) {
        return;
    }

    // 9. Integrate the payload at its place (handles reordering: place by index).
    if (h.payload_size > 0) {
        std::copy(payload, payload + h.payload_size, f.data.data() + offset);
    }
    f.present[h.fragment_index] = true;
    ++f.received;
    ++telemetry_.fragments_received;
    telemetry_.useful_bytes += h.payload_size;

    // 10. Frame complete?
    if (f.received == f.fragment_count) {
        const std::uint32_t id = h.frame_id;
        emit(id, f);
        // Older still-in-progress frames are now stale.
        drop_older_than(id);
        in_flight_.erase(id);
    }
}

void Reassembler::finalize(std::uint32_t frame_id) {
    if (!has_floor_ || frame_id > floor_) {
        floor_ = frame_id;
        has_floor_ = true;
    }
}

void Reassembler::emit(std::uint32_t frame_id, Partial& f) {
    ++telemetry_.frames_completed;
    finalize(frame_id);
    if (on_frame) {
        Frame frame;
        frame.frame_id = frame_id;
        frame.timestamp_us = f.timestamp_us;
        frame.jpeg = std::move(f.data);
        on_frame(frame);
    }
}

void Reassembler::drop_older_than(std::uint32_t frame_id) {
    // Every frame with a STRICTLY smaller frame_id is abandoned: a newer frame
    // is already ready, keeping them would just be latency. They count as lost.
    auto it = in_flight_.begin();
    while (it != in_flight_.end() && it->first < frame_id) {
        ++telemetry_.frames_lost;
        finalize(it->first);
        it = in_flight_.erase(it);
    }
}

void Reassembler::enforce_in_flight_limit() {
    // While we track more than max_frames_in_flight_ frames, drop the oldest
    // (begin() = smallest frame_id, the map is sorted).
    while (in_flight_.size() > max_frames_in_flight_) {
        ++telemetry_.frames_lost;
        finalize(in_flight_.begin()->first);
        in_flight_.erase(in_flight_.begin());
    }
}

} // namespace cam
