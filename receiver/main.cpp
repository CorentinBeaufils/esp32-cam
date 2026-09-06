#include "recv/receiver.hpp"

#include <asio.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

// ---------------------------------------------------------------------------
// Headless receiver: receives the UDP stream, reassembles it, and prints the
// telemetry every second. Used to VERIFY the pipeline and, with --csv, to LOG
// the "real conditions" scenario measurements.
//
//   ./receiver [port] [--csv file.csv]        (default port 9000)
//
// e.g. a measurement run:
//   ./receiver 9000 --csv data/real_close.csv
// ---------------------------------------------------------------------------
namespace {

asio::awaitable<void> print_stats_loop(const rx::Receiver& receiver,
                                       std::ofstream* csv) {
    auto ex = co_await asio::this_coro::executor;
    asio::steady_timer timer(ex);
    int t = 0;   // elapsed seconds (one iteration ~ 1 s)
    while (true) {
        timer.expires_after(std::chrono::seconds(1));
        co_await timer.async_wait(asio::use_awaitable);

        const auto& tel = receiver.telemetry();
        const auto& m   = receiver.metrics();
        const std::uint64_t total = tel.frames_completed + tel.frames_lost;
        const double loss_pct =
            (total > 0) ? (100.0 * static_cast<double>(tel.frames_lost) / static_cast<double>(total))
                        : 0.0;

        std::printf(
            "fps=%5.1f  latency=%5.1f ms  jitter=%4.1f ms  |  "
            "completed=%llu  lost=%llu (%.1f%%)  corrupt=%llu  late=%llu\n",
            m.fps(), m.avg_latency_ms(), m.jitter_ms(),
            static_cast<unsigned long long>(tel.frames_completed),
            static_cast<unsigned long long>(tel.frames_lost), loss_pct,
            static_cast<unsigned long long>(tel.fragments_rejected),
            static_cast<unsigned long long>(tel.fragments_late));

        ++t;
        if (csv && csv->is_open()) {
            // Log the RAW cumulative counters, not the loss_pct printed above
            // (that % is cumulative since launch -> useless for comparing
            // scenarios). Per-second rates are derived offline from the deltas.
            (*csv) << t << ',' << m.fps() << ',' << m.jitter_ms() << ','
                   << tel.frames_completed << ',' << tel.frames_lost << ','
                   << tel.fragments_rejected << '\n';
            csv->flush();  // flush per line: a Ctrl-C would not flush the buffer
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    unsigned short port = 9000;
    std::string    csv_path;

    // Parse: an integer = port; "--csv <file>" = logging.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
        } else {
            port = static_cast<unsigned short>(std::atoi(argv[i]));
        }
    }

    std::ofstream csv;
    if (!csv_path.empty()) {
        csv.open(csv_path);
        if (!csv.is_open()) {
            // open() does NOT create parent directories and fails SILENTLY.
            // Refuse to start rather than measuring into the void.
            std::fprintf(stderr,
                "Error: cannot open '%s' for writing.\n"
                "  -> does the folder exist? (mkdir -p) write permission?\n",
                csv_path.c_str());
            return 1;
        }
        csv << "t_s,fps,jitter_ms,completed,lost,corrupt\n";
        csv.flush();   // header to disk right away (a Ctrl-C does not flush)
        std::printf("CSV logging -> %s\n", csv_path.c_str());
    }

    asio::io_context io;
    rx::Receiver receiver(io, port);
    receiver.start();

    asio::co_spawn(io, print_stats_loop(receiver, csv_path.empty() ? nullptr : &csv),
                   asio::detached);

    std::printf("Receiver listening on port %u  (Ctrl-C to quit)\n",
                receiver.port());
    io.run();
    return 0;
}
