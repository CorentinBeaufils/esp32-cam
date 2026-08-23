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
// Visualiseur : reçoit le flux UDP (thread réseau), décode le JPEG et l'affiche
// (thread principal, imposé par OpenCV). Le pont entre les deux est ton
// LatestFrame : « le plus récent gagne ».
//
//   ./viewer [port]          (défaut 9000)
//
// L'ARCHITECTURE THREADS (fournie ici) :
//   - le Receiver et son io_context tournent sur un THREAD DE FOND ;
//     à chaque trame complète, on la dépose dans le LatestFrame ;
//   - le THREAD PRINCIPAL boucle : take() -> décode -> affiche. OpenCV exige
//     que imshow/waitKey soient sur le thread principal, d'où ce découpage.
//
// Modele a thread reseau separe : reseau d'un
// côté, interface de l'autre, et un point de passage protégé au milieu.
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    const unsigned short port =
        static_cast<unsigned short>((argc > 1) ? std::atoi(argv[1]) : 9000);

    asio::io_context io;
    rx::Receiver receiver(io, port);
    disp::LatestFrame latest;

    // Thread RÉSEAU -> dépose chaque trame complète dans le point de passage.
    receiver.on_frame = [&latest](const cam::Frame& frame) {
        // On copie la trame dans un shared_ptr<const> : le thread d'affichage
        // la gardera vivante le temps de la décoder, indépendamment du réseau.
        latest.store(std::make_shared<const cam::Frame>(frame));
    };
    receiver.start();

    // Le work_guard empêche io.run() de rendre la main quand la file est vide
    // le recepteur doit rester en vie meme sans trafic.
    auto guard = asio::make_work_guard(io);
    std::thread reseau([&io] { io.run(); });

    std::printf("Visualiseur : port %u  (touche 'q' ou Échap pour quitter)\n",
                receiver.port());

    const std::string fenetre = "ESP32-CAM";
    cv::namedWindow(fenetre, cv::WINDOW_AUTOSIZE);

    // Boucle d'AFFICHAGE, sur le thread principal.
    bool quitter = false;
    while (!quitter) {
        auto frame = latest.take();
        if (frame && !frame->jpeg.empty()) {
            // imdecode : JPEG (octets) -> image (cv::Mat), décodée en couleur.
            const cv::Mat img = cv::imdecode(cv::Mat(1, static_cast<int>(frame->jpeg.size()),
                                                     CV_8U,
                                                     const_cast<std::uint8_t*>(frame->jpeg.data())),
                                             cv::IMREAD_COLOR);
            if (!img.empty()) {
                cv::imshow(fenetre, img);
            }
        }

        // waitKey cède la main à OpenCV pour rafraîchir la fenêtre et lire le
        // clavier. 1 ms : on tourne vite, l'affichage suit le flux. 'q' ou Échap
        // pour sortir.
        const int touche = cv::waitKey(1);
        if (touche == 'q' || touche == 27) {
            quitter = true;
        }
    }

    // Arrêt propre : on stoppe le récepteur, on relâche le guard, on rejoint.
    receiver.stop();
    guard.reset();
    io.stop();
    if (reseau.joinable()) {
        reseau.join();
    }
    cv::destroyAllWindows();

    std::printf("Trames sautees a l'affichage (reseau plus rapide que l'ecran) : %llu\n",
                static_cast<unsigned long long>(latest.dropped()));
    return 0;
}
