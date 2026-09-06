#include "qtv/stream_controller.hpp"

#include <opencv2/opencv.hpp>

#include <QImage>

// ---------------------------------------------------------------------------
// cv::Mat (BGR) -> QImage (RGB). Three things to get right:
//   1. Channel order: OpenCV stores BGR, QImage expects RGB -> convert.
//   2. Ownership: the QImage(data, w, h, bytesPerLine, format) constructor does
//      NOT copy -- it points at the cv::Mat buffer, which dies when this
//      function returns. We return .copy() so the QImage owns its pixels.
//   3. Stride: pass the real row stride (mat.step), not cols * channels.
// ---------------------------------------------------------------------------
namespace qtv {

QImage toQImage(const cv::Mat& bgr) {
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888).copy();
}

} // namespace qtv
