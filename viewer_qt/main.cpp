#include "qtv/main_window.hpp"
#include "qtv/stream_controller.hpp"

#include <QApplication>
#include <QMetaType>

#include <algorithm>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Qt dashboard entry point.
//
//   ./viewer_qt [port] [factor] [budget_ms]      (defaults: 9000  2  30)
//
// Run it against the simulator (or, later, the real ESP32):
//   Terminal 1 :  ./viewer_qt 9000
//   Terminal 2 :  ./simulator 127.0.0.1 9000 25 8000
//
// Architecture: a NETWORK thread (inside StreamController) receives + decodes +
// upscales and then EMITS signals; the GUI thread (here) displays. The only
// contact point is Qt's signal queue -- no hand-written mutex.
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // A custom type crossing a queued connection must be registered with the
    // meta-object system (otherwise: "unable to handle unregistered datatype" at
    // runtime).
    qRegisterMetaType<qtv::Stats>();

    const unsigned short port =
        static_cast<unsigned short>((argc > 1) ? std::atoi(argv[1]) : 9000);
    const int    factor    = (argc > 2) ? std::max(1, std::atoi(argv[2])) : 2;
    const double budget_ms = (argc > 3) ? std::atof(argv[3]) : 30.0;

    qtv::StreamController controller(port, factor, budget_ms);
    qtv::MainWindow window(&controller);
    window.resize(1100, 640);
    window.show();

    controller.start();          // start the network thread
    const int rc = app.exec();   // GUI event loop (blocking)
    controller.stop();           // clean shutdown of the network thread
    return rc;
}
