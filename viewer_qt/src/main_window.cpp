#include "qtv/main_window.hpp"

#include <QChart>
#include <QChartView>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineSeries>
#include <QPixmap>
#include <QValueAxis>
#include <QVBoxLayout>
#include <QWidget>

// ---------------------------------------------------------------------------
// MainWindow. The constructor builds the widgets, layouts and chart axes; the
// two slots below run on the GUI thread (they are invoked through a queued
// connection), which is the only place widgets may be touched.
// ---------------------------------------------------------------------------
namespace qtv {

namespace {
// A "value" QLabel: large, bold, initialised to "--".
QLabel* makeValue() {
    auto* l = new QLabel("--");
    QFont f = l->font();
    f.setPointSize(18);
    f.setBold(true);
    l->setFont(f);
    return l;
}
} // namespace

MainWindow::MainWindow(StreamController* controller, QWidget* parent)
    : QMainWindow(parent), controller_(controller) {
    // ---- Video column (left) ----------------------------------------------
    video_ = new QLabel;
    video_->setMinimumSize(640, 480);
    video_->setAlignment(Qt::AlignCenter);
    video_->setStyleSheet("background:#111; color:#888;");
    video_->setText("waiting for stream...");

    // ---- Stats panel (right) ----------------------------------------------
    fpsValue_    = makeValue();
    lossValue_   = makeValue();
    jitterValue_ = makeValue();
    methodValue_ = makeValue();

    auto* grid = new QGridLayout;
    grid->addWidget(new QLabel("FPS"),        0, 0);
    grid->addWidget(fpsValue_,                0, 1);
    grid->addWidget(new QLabel("Loss %"),   1, 0);
    grid->addWidget(lossValue_,               1, 1);
    grid->addWidget(new QLabel("Jitter (ms)"), 2, 0);
    grid->addWidget(jitterValue_,             2, 1);
    grid->addWidget(new QLabel("Upscale"),    3, 0);
    grid->addWidget(methodValue_,             3, 1);

    // ---- Live chart (fps + loss over time) --------------------------------
    fpsSeries_  = new QLineSeries;  fpsSeries_->setName("fps");
    lossSeries_ = new QLineSeries;  lossSeries_->setName("loss %");

    chart_ = new QChart;
    chart_->addSeries(fpsSeries_);
    chart_->addSeries(lossSeries_);
    chart_->setTitle("Throughput and loss over time");

    axisX_ = new QValueAxis;
    axisX_->setTitleText("time");
    axisX_->setRange(0.0, window_);
    axisFps_ = new QValueAxis;
    axisFps_->setTitleText("fps");
    axisFps_->setRange(0.0, 60.0);
    axisLoss_ = new QValueAxis;
    axisLoss_->setTitleText("%");
    axisLoss_->setRange(0.0, 100.0);

    chart_->addAxis(axisX_,   Qt::AlignBottom);
    chart_->addAxis(axisFps_, Qt::AlignLeft);
    chart_->addAxis(axisLoss_, Qt::AlignRight);
    fpsSeries_->attachAxis(axisX_);   fpsSeries_->attachAxis(axisFps_);
    lossSeries_->attachAxis(axisX_);  lossSeries_->attachAxis(axisLoss_);

    chartView_ = new QChartView(chart_);
    chartView_->setMinimumHeight(220);

    auto* right = new QVBoxLayout;
    right->addLayout(grid);
    right->addWidget(chartView_, 1);

    auto* root = new QHBoxLayout;
    root->addWidget(video_, 3);
    root->addLayout(right, 2);

    auto* central = new QWidget;
    central->setLayout(root);
    setCentralWidget(central);
    setWindowTitle("ESP32-CAM -- Qt dashboard");

    // Wire the controller's signals to our slots. The emitter runs on the
    // network thread and the receiver (this window) lives on the GUI thread, so
    // Qt::AutoConnection resolves to a QUEUED connection: the slots run on the
    // GUI thread, which is what makes touching widgets inside them legal.
    connect(controller_, &StreamController::frameReady,
            this,        &MainWindow::onFrame);
    connect(controller_, &StreamController::telemetry,
            this,        &MainWindow::onTelemetry);
}

// Show the frame. We are on the GUI thread (the slot is delivered through a
// queued connection), so touching widgets here is safe.
void MainWindow::onFrame(const QImage& image) {
    if (image.isNull()) {
        return;
    }
    QPixmap pix = QPixmap::fromImage(image);
    video_->setPixmap(pix.scaled(video_->size(),
                                 Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation));
}

// Refresh the labels and push one point into each of the two series.
void MainWindow::onTelemetry(const Stats& stats) {
    fpsValue_->setText(QString::number(stats.fps, 'f', 1));
    lossValue_->setText(QString::number(stats.lossPct, 'f', 1));
    jitterValue_->setText(QString::number(stats.jitterMs, 'f', 1));
    methodValue_->setText(stats.upscale);

    tick_ += 1.0;
    fpsSeries_->append(tick_, stats.fps);
    lossSeries_->append(tick_, stats.lossPct);
    if (tick_ > window_) {   // slide the X axis once the window is full
        axisX_->setRange(tick_ - window_, tick_);
    }
}

} // namespace qtv
