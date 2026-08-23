#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// ESP32-CAM -> PC protocol (UDP)
//
// A JPEG frame is split into UDP DATAGRAMS. Each carries a fixed-size header
// (big-endian, network order) followed by a slice of the JPEG.
//
// UDP guarantees neither delivery, order, nor integrity -- on purpose (real
// time). The protocol therefore carries what is needed to detect and measure:
//   - LOSS       : gaps in fragment_index / frame_id ;
//   - CORRUPTION : a payload_crc that does not match ;
//   - REORDERING : fragment_index lets us place each slice ;
//   - LATENCY    : timestamp_us stamped at send time.
//
// This is TP5 (framing / reassembly) moved to UDP, plus the gap handling that
// TCP used to hide.
// ---------------------------------------------------------------------------
namespace cam {

inline constexpr std::uint16_t MAGIC = 0xE5C0;   // recognition marker
inline constexpr std::uint8_t  VERSION = 1;
inline constexpr std::size_t   HEADER_SIZE = 30;

// Maximum useful bytes per datagram. Margin under the Ethernet MTU (1500):
// 1500 - 20 (IP) - 8 (UDP) - 30 (header) = 1442 possible; we take 1200 to stay
// robust (VPN, tunnels, reduced MTU).
inline constexpr std::size_t   MAX_PAYLOAD = 1200;

// A datagram header, already decoded from its 30 bytes.
struct Header {
    std::uint16_t magic = MAGIC;
    std::uint8_t  version = VERSION;
    std::uint8_t  flags = 0;
    std::uint32_t frame_id = 0;
    std::uint64_t timestamp_us = 0;
    std::uint32_t frame_size = 0;       // total JPEG bytes of the whole frame
    std::uint16_t fragment_count = 0;
    std::uint16_t fragment_index = 0;
    std::uint16_t payload_size = 0;     // useful bytes in THIS datagram
    std::uint32_t payload_crc = 0;
};

// CRC32 (IEEE 802.3, same as zlib). Detects payload corruption.
std::uint32_t crc32(const std::uint8_t* data, std::size_t n);

// Serialise the header big-endian into out (>= HEADER_SIZE bytes).
void write_header(const Header& h, std::uint8_t* out);

// Decode a header from in (>= HEADER_SIZE bytes).
Header read_header(const std::uint8_t* in);

// Split a JPEG frame into ready-to-send datagrams (header + payload). Each
// fragment's payload_crc is computed here. Always returns at least one
// datagram, even for an empty frame (fragment_count = 1, empty payload).
std::vector<std::vector<std::uint8_t>> fragment(std::uint32_t frame_id,
                                                std::uint64_t timestamp_us,
                                                const std::uint8_t* jpeg,
                                                std::size_t size);

} // namespace cam
