#include "cam/protocol.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// replayer - the benchmark's LOAD GENERATOR.
//
// Emits a SYNTHETIC and REPRODUCIBLE UDP stream (fixed seed) over loopback, at
// the rate / size of your choice, and can inject loss and corruption. It is
// the "same stream for everyone": we replay it identically against each
// receiver, without ever touching the real network (127.0.0.1 does not leave
// the machine -> zero risk of DoS, and no spurious loss: it is YOU who decides
// the losses).
//
// It reuses the REAL protocol (cam::fragment): real 30-byte headers, real
// frame_id, real CRC32 -> what the receiver measures is exact.
//
//   replayer <host> <port> <fps> <frame_bytes> <seconds>
//            [loss_pct=0] [corrupt_pct=0] [seed=1]
//
// No asio: a plain blocking UDP socket + a steady_clock cadence.
//
// v1 note: ONE stream. To raise the load, play with <fps> and <frame_bytes>
// (more packets/s), and pin the receiver on a weak core (taskset + hog). N-stream
// fan-out is the next knob.
// ---------------------------------------------------------------------------

namespace {

std::uint64_t now_us() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
            "usage: %s <host> <base_port> <fps> <frame_bytes> <seconds> "
            "[loss_pct=0] [corrupt_pct=0] [seed=1] [streams=1]\n"
            "  streams>1 : fan-out over base_port..base_port+streams-1 (fps PER stream)\n",
            argv[0]);
        return 2;
    }
    const std::string host = argv[1];
    const int         base_port = std::atoi(argv[2]);
    const double      fps  = std::atof(argv[3]);
    const std::size_t frame_bytes = static_cast<std::size_t>(std::atoll(argv[4]));
    const double      seconds     = std::atof(argv[5]);
    const double      loss_pct    = (argc > 6) ? std::atof(argv[6]) : 0.0;
    const double      corrupt_pct = (argc > 7) ? std::atof(argv[7]) : 0.0;
    const unsigned    seed        = (argc > 8) ? static_cast<unsigned>(std::atoll(argv[8])) : 1u;
    const int         streams     = (argc > 9) ? std::atoi(argv[9]) : 1;

    if (fps <= 0.0 || seconds <= 0.0 || streams < 1) {
        std::fprintf(stderr, "fps, seconds > 0 and streams >= 1\n");
        return 2;
    }

    // One source socket, N destinations (base_port + s). All streams share the
    // same frame_id sequence -> the datagrams of a frame are identical from one
    // stream to another: we fragment ONCE, then send to each.
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { std::perror("socket"); return 1; }
    in_addr host_addr{};
    if (::inet_pton(AF_INET, host.c_str(), &host_addr) != 1) {
        std::fprintf(stderr, "invalid host: %s\n", host.c_str());
        ::close(fd);
        return 2;
    }
    std::vector<sockaddr_in> dsts(static_cast<std::size_t>(streams));
    for (int s = 0; s < streams; ++s) {
        dsts[s].sin_family = AF_INET;
        dsts[s].sin_addr   = host_addr;
        dsts[s].sin_port   = htons(static_cast<std::uint16_t>(base_port + s));
    }

    std::mt19937 gen(seed);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_int_distribution<int>     byte(0, 255);

    const std::uint64_t total_frames =
        static_cast<std::uint64_t>(fps * seconds);
    const auto t0 = std::chrono::steady_clock::now();
    const double period_ms = 1000.0 / fps;

    std::uint64_t sent_pkts = 0, dropped_pkts = 0, corrupted_pkts = 0;

    // Payload generated ONLY ONCE (the receiver does not decode the content: it
    // measures the SEQUENCE and the CRC). Regenerating 20,000 random bytes per
    // frame would make the generator the bottleneck well before the receiver.
    // We pay the RNG once, then do nothing more than
    // fragment + send.
    std::vector<std::uint8_t> jpeg(frame_bytes);
    for (auto& b : jpeg) b = static_cast<std::uint8_t>(byte(gen));

    for (std::uint64_t f = 1; f <= total_frames; ++f) {
        auto datagrams = cam::fragment(static_cast<std::uint32_t>(f), now_us(),
                                       jpeg.data(), jpeg.size());
        for (auto& dg : datagrams) {
            for (int s = 0; s < streams; ++s) {   // same frame to each stream
                if (loss_pct > 0.0 && unit(gen) * 100.0 < loss_pct) {
                    ++dropped_pkts;                       // simulated loss: we don't send
                    continue;
                }
                if (corrupt_pct > 0.0 && dg.size() > cam::HEADER_SIZE
                    && unit(gen) * 100.0 < corrupt_pct) {
                    // Local copy so as not to damage the datagram for the other streams.
                    std::vector<std::uint8_t> bad(dg);
                    bad[cam::HEADER_SIZE] ^= 0xFF;         // bad CRC
                    ++corrupted_pkts;
                    ::sendto(fd, bad.data(), bad.size(), 0,
                             reinterpret_cast<sockaddr*>(&dsts[s]), sizeof(dsts[s]));
                } else {
                    ::sendto(fd, dg.data(), dg.size(), 0,
                             reinterpret_cast<sockaddr*>(&dsts[s]), sizeof(dsts[s]));
                }
                ++sent_pkts;
            }
        }

        // Cadence: aim for t0 + f * period.
        const auto target = t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double, std::milli>(static_cast<double>(f) * period_ms));
        std::this_thread::sleep_until(target);
    }

    ::close(fd);
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();
    const double achieved_fps = (elapsed > 0.0)
        ? static_cast<double>(total_frames) / elapsed : 0.0;
    std::fprintf(stderr,
        "[replayer] offered: streams=%d frames/stream=%llu target_fps/stream=%.1f "
        "frame_bytes=%zu duration=%.1fs  (aggregate_fps=%.1f)\n"
        "[replayer] REAL  : elapsed=%.2fs fps_atteint/stream=%.1f%s\n"
        "[replayer] packets: sent=%llu dropped(sim)=%llu corrupted(sim)=%llu "
        "loss=%.1f%% corrupt=%.1f%% seed=%u\n",
        streams, (unsigned long long)total_frames, fps, frame_bytes, seconds,
        fps * streams,
        elapsed, achieved_fps,
        (achieved_fps < fps * 0.95 ? "  <-- generator saturated (does not reach target/stream)" : ""),
        (unsigned long long)sent_pkts, (unsigned long long)dropped_pkts,
        (unsigned long long)corrupted_pkts, loss_pct, corrupt_pct, seed);
    return 0;
}
