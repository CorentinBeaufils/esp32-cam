#include "cam/protocol.hpp"
#include "cam/reassembler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using namespace cam;
using Bytes = std::vector<std::uint8_t>;

namespace {

Bytes fake_jpeg(std::size_t n, std::uint8_t seed) {
    Bytes v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<std::uint8_t>((i * 31 + seed) & 0xFF);
    }
    return v;
}

// Collects the complete frames emitted by a reassembler.
struct Collector {
    std::vector<Frame> frames;
    void attach(Reassembler& r) {
        r.on_frame = [this](const Frame& f) { frames.push_back(f); };
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Header: big-endian (de)serialisation
// ---------------------------------------------------------------------------
TEST_CASE("header: serialisation round-trip", "[protocol][header]") {
    Header h;
    h.frame_id = 0xDEADBEEF;
    h.timestamp_us = 0x0102030405060708ull;
    h.frame_size = 123456;
    h.fragment_count = 12;
    h.fragment_index = 7;
    h.payload_size = 1000;
    h.payload_crc = 0xABCD1234;

    std::uint8_t buffer[HEADER_SIZE];
    write_header(h, buffer);
    const Header d = read_header(buffer);

    CHECK(d.magic == MAGIC);
    CHECK(d.version == VERSION);
    CHECK(d.frame_id == h.frame_id);
    CHECK(d.timestamp_us == h.timestamp_us);
    CHECK(d.frame_size == h.frame_size);
    CHECK(d.fragment_count == h.fragment_count);
    CHECK(d.fragment_index == h.fragment_index);
    CHECK(d.payload_size == h.payload_size);
    CHECK(d.payload_crc == h.payload_crc);
}

TEST_CASE("header: big-endian (most significant byte first)", "[protocol][header]") {
    Header h;
    h.frame_id = 0x01020304;
    std::uint8_t t[HEADER_SIZE];
    write_header(h, t);
    // frame_id starts at byte 4 (after magic(2)+version(1)+flags(1)).
    CHECK(t[4] == 0x01);
    CHECK(t[5] == 0x02);
    CHECK(t[6] == 0x03);
    CHECK(t[7] == 0x04);
}

// ---------------------------------------------------------------------------
// CRC32
// ---------------------------------------------------------------------------
TEST_CASE("crc32: known vector and sensitivity", "[protocol][crc]") {
    const std::string s = "123456789";
    const auto c = crc32(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    CHECK(c == 0xCBF43926u);   // standard CRC-32/ISO-HDLC test vector

    auto a = fake_jpeg(100, 1);
    auto b = a;
    b[50] ^= 0x01;
    CHECK(crc32(a.data(), a.size()) != crc32(b.data(), b.size()));
}

// ---------------------------------------------------------------------------
// Fragmentation + reassembly
// ---------------------------------------------------------------------------
TEST_CASE("round-trip: a frame reassembles identically", "[reassembly]") {
    const auto img = fake_jpeg(3000, 7);
    const auto dgs = fragment(42, 123456, img.data(), img.size());
    REQUIRE(dgs.size() == 3);   // ceil(3000 / 1200)

    Reassembler r;
    Collector c;
    c.attach(r);
    for (const auto& d : dgs) {
        r.feed(d);
    }

    REQUIRE(c.frames.size() == 1);
    CHECK(c.frames[0].frame_id == 42);
    CHECK(c.frames[0].timestamp_us == 123456);
    CHECK(c.frames[0].jpeg == img);
    CHECK(r.telemetry().frames_completed == 1);
}

TEST_CASE("reordering: fragments delivered backwards", "[reassembly]") {
    const auto img = fake_jpeg(5000, 3);
    const auto dgs = fragment(1, 0, img.data(), img.size());

    Reassembler r;
    Collector c;
    c.attach(r);
    for (auto it = dgs.rbegin(); it != dgs.rend(); ++it) {
        r.feed(*it);
    }
    REQUIRE(c.frames.size() == 1);
    CHECK(c.frames[0].jpeg == img);
}

TEST_CASE("loss: a missing fragment abandons the frame", "[reassembly][loss]") {
    const auto img = fake_jpeg(4000, 9);
    const auto dgs = fragment(10, 0, img.data(), img.size());

    Reassembler r;
    Collector c;
    c.attach(r);
    r.feed(dgs[0]);   // dgs[1] skipped
    r.feed(dgs[2]);
    CHECK(c.frames.empty());

    // A newer frame arrives complete: the old one (10) is abandoned.
    const auto img2 = fake_jpeg(500, 1);
    for (const auto& d : fragment(11, 0, img2.data(), img2.size())) {
        r.feed(d);
    }
    REQUIRE(c.frames.size() == 1);
    CHECK(c.frames[0].frame_id == 11);
    CHECK(r.telemetry().frames_lost == 1);
}

TEST_CASE("corruption: a bad CRC rejects the fragment", "[reassembly][corruption]") {
    const auto img = fake_jpeg(800, 5);
    auto dgs = fragment(1, 0, img.data(), img.size());
    dgs[0][HEADER_SIZE + 10] ^= 0xFF;   // corrupt the payload

    Reassembler r;
    Collector c;
    c.attach(r);
    r.feed(dgs[0]);
    CHECK(c.frames.empty());
    CHECK(r.telemetry().fragments_rejected == 1);
}

TEST_CASE("invalid magic: stray datagram rejected", "[reassembly][corruption]") {
    const auto img = fake_jpeg(300, 1);
    auto dgs = fragment(1, 0, img.data(), img.size());
    dgs[0][0] = 0x00;   // break the magic

    Reassembler r;
    Collector c;
    c.attach(r);
    r.feed(dgs[0]);
    CHECK(r.telemetry().fragments_rejected == 1);
}

TEST_CASE("duplicate: a fragment received twice emits one frame", "[reassembly]") {
    const auto img = fake_jpeg(500, 2);   // single fragment
    const auto dgs = fragment(1, 0, img.data(), img.size());

    Reassembler r;
    Collector c;
    c.attach(r);
    r.feed(dgs[0]);
    r.feed(dgs[0]);   // duplicate of an already-emitted frame

    REQUIRE(c.frames.size() == 1);
    CHECK(r.telemetry().fragments_late == 1);   // the duplicate is counted late
}

TEST_CASE("real-time buffer: keep at most N frames in flight", "[reassembly][buffer]") {
    Reassembler r(2);   // at most 2 frames in progress
    Collector c;
    c.attach(r);

    for (std::uint32_t id = 100; id < 103; ++id) {
        const auto img = fake_jpeg(4000, static_cast<std::uint8_t>(id));
        const auto dgs = fragment(id, 0, img.data(), img.size());
        r.feed(dgs[0]);   // a single fragment: incomplete frame
    }

    CHECK(r.frames_in_flight() == 2);            // only the 2 newest
    CHECK(r.telemetry().frames_lost == 1);       // the oldest dropped
}

TEST_CASE("empty frame: size 0 produces an empty frame", "[reassembly]") {
    const auto dgs = fragment(1, 0, nullptr, 0);
    REQUIRE(dgs.size() == 1);

    Reassembler r;
    Collector c;
    c.attach(r);
    r.feed(dgs[0]);
    REQUIRE(c.frames.size() == 1);
    CHECK(c.frames[0].jpeg.empty());
}

TEST_CASE("large frame: many fragments", "[reassembly]") {
    const auto img = fake_jpeg(50000, 42);   // ~42 fragments
    const auto dgs = fragment(7, 0, img.data(), img.size());
    REQUIRE(dgs.size() == (50000 + MAX_PAYLOAD - 1) / MAX_PAYLOAD);

    Reassembler r;
    Collector c;
    c.attach(r);
    for (const auto& d : dgs) {
        r.feed(d);
    }
    REQUIRE(c.frames.size() == 1);
    CHECK(c.frames[0].jpeg == img);
}
