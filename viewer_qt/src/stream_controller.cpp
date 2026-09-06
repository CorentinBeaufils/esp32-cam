#include "qtv/stream_controller.hpp"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>

// ---------------------------------------------------------------------------
// StreamController: the network -> GUI bridge.
//
// It runs the viewer_up pipeline (decode JPEG -> adaptive, timed resize), but
// instead of displaying anything itself it EMITS:
//   - telemetry(Stats)    : fps / loss / jitter / upscale method;
//   - frameReady(QImage)  : the ready (already upscaled) frame, IF it decodes.
// These emits leave the NETWORK thread; Qt delivers them to the GUI thread as a
// queued connection.
//
// PRINCIPLE: telemetry = NETWORK health. It is emitted on EVERY complete frame,
// even when the image cannot be decoded (synthetic stream, corruption). Only the
// image display depends on a successful decode.
// ---------------------------------------------------------------------------
namespace qtv {

namespace {
// Our pure enum -> the OpenCV interpolation flag.
int cv_flag(up::Interp i) {
    switch (i) {
        case up::Interp::Nearest: return cv::INTER_NEAREST;
        case up::Interp::Linear:  return cv::INTER_LINEAR;
        case up::Interp::Cubic:   return cv::INTER_CUBIC;
        case up::Interp::Lanczos: return cv::INTER_LANCZOS4;
    }
    return cv::INTER_LINEAR;
}
} // namespace

StreamController::StreamController(unsigned short port, int upscaleFactor,
                                  double budgetMs, QObject* parent)
    : QObject(parent),
      io_(),
      receiver_(io_, port),
      guard_(asio::make_work_guard(io_)),
      factor_(std::max(1, upscaleFactor)),
      policy_(budgetMs, up::Interp::Cubic),
      scale_(120) {
    // The Receiver callback runs on the NETWORK thread. We prepare the frame
    // there and then emit -- no widget is touched here.
    receiver_.on_frame = [this](const cam::Frame& frame) { onFrame(frame); };
}

StreamController::~StreamController() { stop(); }

void StreamController::onFrame(const cam::Frame& frame) {
    const up::Interp method = policy_.current();

    // --- 1) TELEMETRY: network health, INDEPENDENT of decoding. -------------
    // A complete frame arrived: the metrics (fps/loss/jitter) are already up to
    // date on the receiver side (updated on THIS thread, before this callback).
    // We ALWAYS emit them -- otherwise an undecodable stream would freeze the
    // dashboard.
    {
        const auto& t = receiver_.telemetry();
        const auto& m = receiver_.metrics();
        const std::uint64_t total = t.frames_completed + t.frames_lost;

        Stats s;
        s.fps       = m.fps();
        s.lossPct   = (total > 0) ? 100.0 * double(t.frames_lost) / double(total) : 0.0;
        s.jitterMs  = m.jitter_ms();
        s.latencyMs = m.avg_latency_ms();
        s.upscale   = QString::fromLatin1(up::to_string(method));
        emit telemetry(s);
    }

    // --- 2) IMAGE: may fail (non-JPEG stream, corruption). If so: no frame is
    //     emitted, but the telemetry above has already been sent. -------------
    if (frame.jpeg.empty()) {
        return;
    }
    const cv::Mat src = cv::imdecode(
        cv::Mat(1, static_cast<int>(frame.jpeg.size()), CV_8U,
                const_cast<std::uint8_t*>(frame.jpeg.data())),
        cv::IMREAD_COLOR);
    if (src.empty()) {
        return;   // payload not decodable (e.g. synthetic simulator)
    }

    // --- 3) Adaptive resize, timed -> feeds the policy. ---------------------
    cv::Mat dst;
    const auto t0 = std::chrono::steady_clock::now();
    cv::resize(src, dst, cv::Size(src.cols * factor_, src.rows * factor_),
               0, 0, cv_flag(method));
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    scale_.record(ms);
    policy_.update(ms);

    // --- 4) Convert -> QImage (toQImage) then emit to the UI. ---------------
    emit frameReady(toQImage(dst));
}

void StreamController::start() {
    if (running_) {
        return;
    }
    running_ = true;
    receiver_.start();
    net_ = std::thread([this] { io_.run(); });
}

void StreamController::stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    receiver_.stop();     // close the socket -> operation_aborted -> loop exits
    guard_.reset();       // release the work guard: io_.run() can return
    io_.stop();
    if (net_.joinable()) {
        net_.join();
    }
}

unsigned short StreamController::port() const { return receiver_.port(); }

} // namespace qtv
