#pragma once

#include "qtv/stream_controller.hpp"   // StreamController, qtv::Stats

#include <QMainWindow>
#include <QImage>

// Qt forward-declarations (the .cpp includes the real headers). In Qt6 the
// QtCharts classes live in the GLOBAL namespace (there is no QtCharts namespace
// anymore).
class QLabel;
class QLineSeries;
class QChart;
class QChartView;
class QValueAxis;

namespace qtv {

// ---------------------------------------------------------------------------
// MainWindow: the dashboard window.
//   - left  : the video (a QLabel showing a QPixmap);
//   - right : a stats panel (labels) plus a live chart (QtCharts).
//
// The two slots below run on the GUI thread (they are delivered through a queued
// connection), which is the only place widgets may be touched.
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(StreamController* controller, QWidget* parent = nullptr);

private slots:
    void onFrame(const QImage& image);
    void onTelemetry(const qtv::Stats& stats);

private:
    StreamController* controller_;

    QLabel* video_;
    QLabel* fpsValue_;
    QLabel* lossValue_;
    QLabel* jitterValue_;
    QLabel* methodValue_;

    QLineSeries* fpsSeries_;
    QLineSeries* lossSeries_;
    QChart*      chart_;
    QChartView*  chartView_;
    QValueAxis*  axisX_;
    QValueAxis*  axisFps_;
    QValueAxis*  axisLoss_;

    double  tick_   = 0.0;    // chart X coordinate (one step per telemetry event)
    double  window_ = 60.0;   // visible width of the X axis (in steps)
};

} // namespace qtv
