#include "cam/protocol.hpp"

#include <algorithm>
#include <array>

namespace cam {

// --- CRC32 (IEEE), table-based ---------------------------------------------
// A precomputed table trades 1 KiB of memory for ~8x fewer operations per
// byte: instead of looping 8 times per byte (once per bit), we do a single
// table lookup. Since we CRC every datagram payload at video frame rates, the
// table version is the right choice here. The table itself is generated once,
// on first use, from the same IEEE polynomial (0xEDB88320, reflected).
namespace {

std::array<std::uint32_t, 256> make_crc_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        table[i] = c;
    }
    return table;
}

const std::array<std::uint32_t, 256>& crc_table() {
    static const std::array<std::uint32_t, 256> table = make_crc_table();
    return table;
}

} // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t n) {
    const auto& table = crc_table();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

// --- big-endian helpers -----------------------------------------------------
namespace {

std::uint8_t* put_u16(std::uint8_t* p, std::uint16_t v) {
    *p++ = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    *p++ = static_cast<std::uint8_t>(v & 0xFF);
    return p;
}
std::uint8_t* put_u32(std::uint8_t* p, std::uint32_t v) {
    *p++ = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    *p++ = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    *p++ = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    *p++ = static_cast<std::uint8_t>(v & 0xFF);
    return p;
}
std::uint8_t* put_u64(std::uint8_t* p, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        *p++ = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF);
    }
    return p;
}

const std::uint8_t* get_u16(const std::uint8_t* p, std::uint16_t& v) {
    v = static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
    return p + 2;
}
const std::uint8_t* get_u32(const std::uint8_t* p, std::uint32_t& v) {
    v = (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
      | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
    return p + 4;
}
const std::uint8_t* get_u64(const std::uint8_t* p, std::uint64_t& v) {
    v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<std::uint64_t>(p[i]);
    }
    return p + 8;
}

} // namespace

// --- header (de)serialisation ----------------------------------------------
void write_header(const Header& h, std::uint8_t* out) {
    std::uint8_t* p = out;
    p = put_u16(p, h.magic);
    *p++ = h.version;
    *p++ = h.flags;
    p = put_u32(p, h.frame_id);
    p = put_u64(p, h.timestamp_us);
    p = put_u32(p, h.frame_size);
    p = put_u16(p, h.fragment_count);
    p = put_u16(p, h.fragment_index);
    p = put_u16(p, h.payload_size);
    p = put_u32(p, h.payload_crc);
    // p - out == HEADER_SIZE (30)
}

Header read_header(const std::uint8_t* in) {
    Header h;
    const std::uint8_t* p = in;
    p = get_u16(p, h.magic);
    h.version = *p++;
    h.flags = *p++;
    p = get_u32(p, h.frame_id);
    p = get_u64(p, h.timestamp_us);
    p = get_u32(p, h.frame_size);
    p = get_u16(p, h.fragment_count);
    p = get_u16(p, h.fragment_index);
    p = get_u16(p, h.payload_size);
    p = get_u32(p, h.payload_crc);
    return h;
}

// --- fragmentation ----------------------------------------------------------
std::vector<std::vector<std::uint8_t>> fragment(std::uint32_t frame_id,
                                                std::uint64_t timestamp_us,
                                                const std::uint8_t* jpeg,
                                                std::size_t size) {
    // Fragment count: ceil(size / MAX_PAYLOAD), at least 1 (an empty frame
    // produces one datagram with an empty payload, so the receiver still knows
    // a frame exists).
    const std::size_t count = (size == 0) ? 1 : (size + MAX_PAYLOAD - 1) / MAX_PAYLOAD;

    std::vector<std::vector<std::uint8_t>> datagrams;
    datagrams.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t start = i * MAX_PAYLOAD;
        const std::size_t len = std::min(MAX_PAYLOAD, size - std::min(start, size));

        Header h;
        h.frame_id = frame_id;
        h.timestamp_us = timestamp_us;
        h.frame_size = static_cast<std::uint32_t>(size);
        h.fragment_count = static_cast<std::uint16_t>(count);
        h.fragment_index = static_cast<std::uint16_t>(i);
        h.payload_size = static_cast<std::uint16_t>(len);
        h.payload_crc = crc32(jpeg + start, len);

        std::vector<std::uint8_t> dg(HEADER_SIZE + len);
        write_header(h, dg.data());
        if (len > 0) {
            std::copy(jpeg + start, jpeg + start + len, dg.data() + HEADER_SIZE);
        }
        datagrams.push_back(std::move(dg));
    }
    return datagrams;
}

} // namespace cam
