#include "disp/latest_frame.hpp"
#include "recv/receiver.hpp"

#include <asio.hpp>
#include <opencv2/opencv.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

// ---------------------------------------------------------------------------
// Viewer: receives the UDP stream (network thread), decodes the JPEG and
// displays it (main thread, required by OpenCV). The bridge between the two is
// your LatestFrame: "most recent wins".
//
//   ./viewer [port]          (default 9000)
//
// THE THREADING ARCHITECTURE (provided here):
//   - the Receiver and its io_context run on a BACKGROUND THREAD;
//     on each complete frame, we store it in the LatestFrame;
//   - the MAIN THREAD loops: take() -> decode -> display. OpenCV requires
//     imshow/waitKey to be on the main thread, hence this split.
//
// Separate-network-thread model: network on one side, the UI on the other, and
// a protected handoff point in the middle.
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    const unsigned short port =
        static_cast<unsigned short>((argc > 1) ? std::atoi(argv[1]) : 9000);

    asio::io_context io;
    rx::Receiver receiver(io, port);
    disp::LatestFrame latest;

    // NETWORK thread -> stores each complete frame in the handoff point.
    receiver.on_frame = [&latest](const cam::Frame& frame) {
        // We copy the frame into a shared_ptr<const>: the display thread will
        // keep it alive long enough to decode it, independently of the network.
        latest.store(std::make_shared<const cam::Frame>(frame));
    };
    receiver.start();

    // The work_guard prevents io.run() from returning when the queue is empty
    // the receiver must stay alive even without traffic.
    auto guard = asio::make_work_guard(io);
    std::thread reseau([&io] { io.run(); });

    std::printf("Viewer: port %u  (press 'q' or Esc to quit)\n",
                receiver.port());

    const std::string fenetre = "ESP32-CAM";
    cv::namedWindow(fenetre, cv::WINDOW_AUTOSIZE);

    // DISPLAY loop, on the main thread.
    bool quitter = false;
    while (!quitter) {
        auto frame = latest.take();
        if (frame && !frame->jpeg.empty()) {
            // imdecode: JPEG (bytes) -> image (cv::Mat), decoded in color.
            const cv::Mat img = cv::imdecode(cv::Mat(1, static_cast<int>(frame->jpeg.size()),
                                                     CV_8U,
                                                     const_cast<std::uint8_t*>(frame->jpeg.data())),
                                             cv::IMREAD_COLOR);
            if (!img.empty()) {
                cv::imshow(fenetre, img);
            }
        }

        // waitKey hands control to OpenCV to refresh the window and read the
        // keyboard. 1 ms: we run fast, the display keeps up with the stream.
        // 'q' or Esc to exit.
        const int touche = cv::waitKey(1);
        if (touche == 'q' || touche == 27) {
            quitter = true;
        }
    }

    // Clean shutdown: stop the receiver, release the guard, join.
    receiver.stop();
    guard.reset();
    io.stop();
    if (reseau.joinable()) {
        reseau.join();
    }
    cv::destroyAllWindows();

    std::printf("Frames dropped at display (network faster than screen): %llu\n",
                static_cast<unsigned long long>(latest.dropped()));
    return 0;
}
