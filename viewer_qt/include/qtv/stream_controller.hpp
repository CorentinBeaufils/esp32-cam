#pragma once

#include "recv/receiver.hpp"   // rx::Receiver, cam::Frame
#include "up/scale_stats.hpp"
#include "up/upscale_policy.hpp"

#include <asio.hpp>

#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <cstdint>
#include <thread>

// Keep <opencv2/...> out of this header: forward-declare cv::Mat (faster to
// compile, and the header stays usable without pulling in all of OpenCV). Only
// the .cpp that actually manipulates the image includes OpenCV.
namespace cv { class Mat; }

namespace qtv {

// ---------------------------------------------------------------------------
// Implemented in src/to_qimage.cpp.
//
// Converts an OpenCV image (BGR, 8 bits x 3 channels) to a QImage (RGB). The
// returned object MUST own its own bytes: it outlives the source cv::Mat, which
// is destroyed right after the call.
// ---------------------------------------------------------------------------
QImage toQImage(const cv::Mat& bgr);

// Telemetry snapshot, sent from the NETWORK thread to the GUI thread. A copyable
// POD -> crosses a queued connection without trouble.
struct Stats {
    double  fps       = 0.0;
    double  lossPct   = 0.0;
    double  jitterMs  = 0.0;
    double  latencyMs = 0.0;
    QString upscale;          // current upscale method ("Cubic", "Lanczos", ...)
};

// ---------------------------------------------------------------------------
// StreamController: owns the network pipeline (an rx::Receiver plus an
// io_context running on a SEPARATE std::thread) and pushes it to the UI through
// SIGNALS.
//
// Qt golden rule: a widget is only ever touched from the GUI thread. Here the
// network lives on another thread, so we never touch the UI directly -- we EMIT
// signals. Because the emitter (this thread) and the receiver (the GUI thread)
// differ, Qt AUTOMATICALLY switches to a "queued" connection: the argument is
// copied and the slot runs on the GUI side. That is the whole point of the
// signals/slots model.
//
// Reuses rx::Receiver as-is.
// ---------------------------------------------------------------------------
class StreamController : public QObject {
    Q_OBJECT
public:
    explicit StreamController(unsigned short port,
                              int    upscaleFactor = 2,
                              double budgetMs      = 30.0,
                              QObject* parent = nullptr);
    ~StreamController() override;

    void start();                 // start the network thread
    void stop();                  // stop cleanly (join the thread)
    unsigned short port() const;

signals:
    void frameReady(const QImage& image);      // a frame ready to display
    void telemetry(const qtv::Stats& stats);   // refreshed stats

private:
    void onFrame(const cam::Frame& frame);     // RUNS ON THE NETWORK THREAD

    asio::io_context io_;
    rx::Receiver     receiver_;
    asio::executor_work_guard<asio::io_context::executor_type> guard_;
    int               factor_;
    up::UpscalePolicy policy_;
    up::ScaleStats    scale_;
    std::thread       net_;
    bool              running_ = false;
};

} // namespace qtv

// A custom type crossing a queued connection must be declared as a metatype.
Q_DECLARE_METATYPE(qtv::Stats)
