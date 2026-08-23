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
// Visualiseur AVEC UPSCALING.
//
//   ./viewer_up [port] [facteur] [budget_ms]     (defauts : 9000  2  30)
//
// Meme squelette que le viewer simple (reseau en fond -> LatestFrame ->
// affichage sur le thread principal), avec un étage en plus dans la boucle
// d'affichage :
//
//   décoder (imdecode)  ->  AGRANDIR (cv::resize, CHRONOMÉTRÉ)  ->  afficher
//
// Le coût de l'agrandissement est mesuré à chaque trame et injecté dans :
//   - ScaleStats    : moyenne / p95 / dépassements du budget ;
//   - UpscalePolicy : choisit la méthode d'interpolation de la PROCHAINE trame
//     pour tenir le budget temps réel.
//
// Ces deux pièces sont TA production (lib `upscale`). Ce fichier ne fait que les
// brancher à OpenCV et dessiner la télémétrie par-dessus l'image.
// ---------------------------------------------------------------------------

namespace {

// Traduit notre enum (pur, sans OpenCV) vers le drapeau d'interpolation OpenCV.
int cv_flag(up::Interp interp) {
    switch (interp) {
        case up::Interp::Nearest: return cv::INTER_NEAREST;
        case up::Interp::Linear:  return cv::INTER_LINEAR;
        case up::Interp::Cubic:   return cv::INTER_CUBIC;
        case up::Interp::Lanczos: return cv::INTER_LANCZOS4;
    }
    return cv::INTER_LINEAR;
}

// Petit incrustateur de texte lisible sur n'importe quel fond (liseré noir +
// texte blanc).
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

    std::printf("Visualiseur upscaling : port %u  x%d  budget %.1f ms  "
                "(touche 'q' ou Echap pour quitter)\n",
                receiver.port(), factor, budget_ms);

    // Nos deux composants purs. On DÉMARRE en Lanczos volontairement : si la
    // machine ne suit pas, la politique redescendra toute seule -- on VOIT
    // l'adaptation se produire.
    up::ScaleStats   stats(120);
    up::UpscalePolicy policy(budget_ms, up::Interp::Lanczos);

    const std::string fenetre = "ESP32-CAM (upscaled)";
    cv::namedWindow(fenetre, cv::WINDOW_AUTOSIZE);

    // Compteur de fps d'affichage (fenêtre 1 s).
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

                // --- L'upscale, chronométré au plus serré (juste cv::resize) ---
                cv::Mat dst;
                const auto t0 = std::chrono::steady_clock::now();
                cv::resize(src, dst,
                           cv::Size(src.cols * factor, src.rows * factor),
                           0, 0, cv_flag(methode));
                const auto t1 = std::chrono::steady_clock::now();
                last_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

                // Alimente les stats, puis laisse la politique choisir la suite.
                stats.record(last_ms);
                policy.update(last_ms);

                // fps d'affichage
                ++frames_1s;
                const auto now = std::chrono::steady_clock::now();
                if (now - t_fps >= std::chrono::seconds(1)) {
                    fps_aff = frames_1s /
                        std::chrono::duration<double>(now - t_fps).count();
                    frames_1s = 0;
                    t_fps = now;
                }

                // --- Télémétrie incrustée ---
                char l1[128], l2[128], l3[128];
                std::snprintf(l1, sizeof l1, "%dx%d -> %dx%d  x%d  [%s]",
                              src.cols, src.rows, dst.cols, dst.rows, factor,
                              up::to_string(methode));
                std::snprintf(l2, sizeof l2,
                              "upscale %.1f ms (moy %.1f / p95 %.1f)  budget %.0f ms",
                              last_ms, stats.avg_ms(), stats.p95_ms(), budget_ms);
                std::snprintf(l3, sizeof l3,
                              "aff %.0f fps  depass %zu/%zu  v%llu ^%llu",
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

    std::printf("Fin. Upscale moyen %.1f ms (p95 %.1f)  descentes %llu  montees %llu\n",
                stats.avg_ms(), stats.p95_ms(),
                static_cast<unsigned long long>(policy.downgrades()),
                static_cast<unsigned long long>(policy.upgrades()));
    std::printf("Trames sautees a l'affichage : %llu\n",
                static_cast<unsigned long long>(latest.dropped()));
    return 0;
}
