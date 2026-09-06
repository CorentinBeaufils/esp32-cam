#include "disp/latest_frame.hpp"
#include "recv/receiver.hpp"
#include "up/scale_stats.hpp"
#include "up/upscale_policy.hpp"

#include <asio.hpp>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Viewer WITH UPSCALING.
//
//   ./viewer_up [port] [factor] [budget_ms]     (defaults: 9000  2  30)
//
// Same skeleton as the simple viewer (network in the background -> LatestFrame ->
// display on the main thread), with one extra stage in the display
// loop:
//
//   decode (imdecode)  ->  ENLARGE (cv::resize, TIMED)  ->  display
//
// The cost of the enlargement is measured on each frame and fed into:
//   - ScaleStats    : average / p95 / budget overruns;
//   - UpscalePolicy : chooses the interpolation method for the NEXT frame
//     to stay within the real-time budget.
//
// These two pieces are YOUR work (the `upscale` lib). This file only wires them
// to OpenCV and draws the telemetry over the image.
// ---------------------------------------------------------------------------

namespace {

// Translates our enum (pure, without OpenCV) to the OpenCV interpolation flag.
int cv_flag(up::Interp interp) {
    switch (interp) {
        case up::Interp::Nearest: return cv::INTER_NEAREST;
        case up::Interp::Linear:  return cv::INTER_LINEAR;
        case up::Interp::Cubic:   return cv::INTER_CUBIC;
        case up::Interp::Lanczos: return cv::INTER_LANCZOS4;
    }
    return cv::INTER_LINEAR;
}

// Small text overlay helper, legible on any background (black outline +
// white text).
void draw_line(cv::Mat& img, const std::string& txt, int y) {
    const auto font = cv::FONT_HERSHEY_SIMPLEX;
    cv::putText(img, txt, {10, y}, font, 0.5, {0, 0, 0}, 3, cv::LINE_AA);
    cv::putText(img, txt, {10, y}, font, 0.5, {255, 255, 255}, 1, cv::LINE_AA);
}

} // namespace

int main(int argc, char** argv) {
    const unsigned short port =
        static_cast<unsigned short>((argc > 1) ? std::atoi(argv[1]) : 9000);
    const int    factor    = (argc > 2) ? std::max(1, std::atoi(argv[2])) : 2;
    const double budget_ms = (argc > 3) ? std::atof(argv[3]) : 30.0;

    asio::io_context io;
    rx::Receiver receiver(io, port);
    disp::LatestFrame latest;

    receiver.on_frame = [&latest](const cam::Frame& frame) {
        latest.store(std::make_shared<const cam::Frame>(frame));
    };
    receiver.start();

    auto guard = asio::make_work_guard(io);
    std::thread reseau([&io] { io.run(); });

    std::printf("Upscaling viewer: port %u  x%d  budget %.1f ms  "
                "(press 'q' or Esc to quit)\n",
                receiver.port(), factor, budget_ms);

    // Our two pure components. We START in Lanczos on purpose: if the machine
    // cannot keep up, the policy will step down on its own -- we SEE the
    // adaptation happen.
    up::ScaleStats   stats(120);
    up::UpscalePolicy policy(budget_ms, up::Interp::Lanczos);

    const std::string fenetre = "ESP32-CAM (upscaled)";
    cv::namedWindow(fenetre, cv::WINDOW_AUTOSIZE);

    // Display fps counter (1 s window).
    int    frames_1s = 0;
    double fps_aff   = 0.0;
    auto   t_fps     = std::chrono::steady_clock::now();

    double last_ms = 0.0;
    bool quitter = false;
    while (!quitter) {
        auto frame = latest.take();
        if (frame && !frame->jpeg.empty()) {
            const cv::Mat src = cv::imdecode(
                cv::Mat(1, static_cast<int>(frame->jpeg.size()), CV_8U,
                        const_cast<std::uint8_t*>(frame->jpeg.data())),
                cv::IMREAD_COLOR);

            if (!src.empty()) {
                const up::Interp methode = policy.current();

                // --- The upscale, timed as tightly as possible (just cv::resize) ---
                cv::Mat dst;
                const auto t0 = std::chrono::steady_clock::now();
                cv::resize(src, dst,
                           cv::Size(src.cols * factor, src.rows * factor),
                           0, 0, cv_flag(methode));
                const auto t1 = std::chrono::steady_clock::now();
                last_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

                // Feed the stats, then let the policy choose what comes next.
                stats.record(last_ms);
                policy.update(last_ms);

                // display fps
                ++frames_1s;
                const auto now = std::chrono::steady_clock::now();
                if (now - t_fps >= std::chrono::seconds(1)) {
                    fps_aff = frames_1s /
                        std::chrono::duration<double>(now - t_fps).count();
                    frames_1s = 0;
                    t_fps = now;
                }

                // --- Overlaid telemetry ---
                char l1[128], l2[128], l3[128];
                std::snprintf(l1, sizeof l1, "%dx%d -> %dx%d  x%d  [%s]",
                              src.cols, src.rows, dst.cols, dst.rows, factor,
                              up::to_string(methode));
                std::snprintf(l2, sizeof l2,
                              "upscale %.1f ms (avg %.1f / p95 %.1f)  budget %.0f ms",
                              last_ms, stats.avg_ms(), stats.p95_ms(), budget_ms);
                std::snprintf(l3, sizeof l3,
                              "disp %.0f fps  over %zu/%zu  v%llu ^%llu",
                              fps_aff, stats.over_budget(budget_ms), stats.count(),
                              static_cast<unsigned long long>(policy.downgrades()),
                              static_cast<unsigned long long>(policy.upgrades()));
                draw_line(dst, l1, 20);
                draw_line(dst, l2, 40);
                draw_line(dst, l3, 60);

                cv::imshow(fenetre, dst);
            }
        }

        const int touche = cv::waitKey(1);
        if (touche == 'q' || touche == 27) {
            quitter = true;
        }
    }

    receiver.stop();
    guard.reset();
    io.stop();
    if (reseau.joinable()) {
        reseau.join();
    }
    cv::destroyAllWindows();

    std::printf("Done. Mean upscale %.1f ms (p95 %.1f)  downgrades %llu  upgrades %llu\n",
                stats.avg_ms(), stats.p95_ms(),
                static_cast<unsigned long long>(policy.downgrades()),
                static_cast<unsigned long long>(policy.upgrades()));
    std::printf("Frames dropped at display: %llu\n",
                static_cast<unsigned long long>(latest.dropped()));
    return 0;
}
