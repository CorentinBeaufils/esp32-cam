# TP `viewer_qt` — dashboard Qt (vidéo + stats + graphe live)

## But

Afficher le flux ESP32-CAM dans une **vraie fenêtre** : la vidéo à gauche, un
panneau de stats à droite (fps, pertes %, gigue, méthode d'upscale), et **un
graphe QtCharts** qui trace fps et pertes dans le temps. C'est le remplaçant
« présentable » du `viewer` OpenCV (`imshow`), et la base pour les tests en
conditions réelles à venir.

Tu réutilises **tout** ton pipeline existant : `rx::Receiver` (réseau UDP async),
`up::UpscalePolicy` + `up::ScaleStats` (upscaling adaptatif), `cam::*` (protocole).
Rien à réécrire côté cœur — on ne fait qu'y **brancher une IHM Qt**.

## Les 2 concepts Qt à intégrer

### 1. Le modèle signals/slots

Un objet Qt (`QObject`) peut **émettre** des signaux ; d'autres objets y
connectent des **slots** (des méthodes). `connect(source, &T::signal, cible,
&U::slot)` relie les deux. Quand `emit signal(args)` part, tous les slots
connectés sont appelés avec `args`. C'est de l'observateur typé, vérifié à la
compilation (syntaxe pointeur-de-membre).

### 2. Le threading : LA règle d'or

**On ne touche JAMAIS un widget en dehors du thread GUI.** Qt n'est pas
thread-safe côté widgets : appeler `setPixmap` depuis le thread réseau = crash ou
corruption, tôt ou tard.

Or ici, comme dans le `viewer` OpenCV, le réseau tourne sur un **thread à part**
(l'`io_context` d'asio dans un `std::thread`). Comment lui faire mettre à jour
l'écran sans toucher aux widgets ? **Il émet un signal.**

La magie : quand l'émetteur et le récepteur d'une connexion sont sur des threads
**différents**, Qt (en `Qt::AutoConnection`, le défaut) bascule en connexion
**queued** : l'argument du signal est **copié**, déposé dans la file d'événements
du thread GUI, et le slot s'exécute **côté GUI**. Le `std::thread` + `mutex` que
tu gérais à la main dans le `LatestFrame`, ici c'est la **file d'événements Qt**
qui le fait — proprement, sans que tu écrives un seul verrou.

> Conséquence : un slot connecté à un signal venu du thread réseau s'exécute sur
> le thread GUI. **C'est pour ça que tu as le droit d'y toucher les widgets.**

## Architecture (fournie)

```
 THREAD RESEAU (io_context)                    THREAD GUI (QApplication)
 ─────────────────────────                     ─────────────────────────
 rx::Receiver.on_frame                          MainWindow::onFrame(QImage)
   -> imdecode (JPEG->Mat BGR)     signaux         -> video_->setPixmap(...)
   -> resize adaptatif (upscale)   ========>     MainWindow::onTelemetry(Stats)
   -> toQImage(Mat)  [À TOI]       (queued)         -> labels + QtCharts
   -> emit frameReady / telemetry
        StreamController                              connexions [À TOI]
```

- **`StreamController`** (fourni) : possède le `Receiver` + le thread réseau ;
  décode, agrandit, convertit, et **émet** `frameReady(QImage)` et
  `telemetry(Stats)`.
- **`MainWindow`** : construit l'IHM (fourni) ; **tu** câbles les connexions et
  **tu** écris les deux slots.
- **`toQImage`** : **tu** l'écris.
- **`main.cpp`** (fourni) : `QApplication`, enregistre le métatype `Stats`, crée
  le tout, `controller.start()`, `app.exec()`.

## Ce que tu implémentes (3 TODO)

1. **`src/to_qimage.cpp`** — `cv::Mat` (BGR) → `QImage` (RGB). Court, mais 3
   pièges : ordre des canaux (BGR≠RGB), **propriété des octets** (`QImage` sur un
   buffer OpenCV qui va mourir → `.copy()`), et **stride** (`mat.step`).
2. **`src/main_window.cpp` → `onFrame` / `onTelemetry`** — les slots de
   rafraîchissement (pixmap ; labels ; `append` dans les deux séries + défilement
   de l'axe X).
3. **`src/main_window.cpp` → constructeur (bloc TODO 3)** — les deux `connect(...)`.

Le squelette **compile et se lance tel quel** (fenêtre vide, aucune donnée) :
c'est le point de départ. Remplis les TODO, et l'image + les stats apparaissent.

## Compiler & lancer

```bash
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel -j --target viewer_qt

# test face au simulateur (2 terminaux)
./build-rel/viewer_qt/viewer_qt 9000
./build-rel/simulator/simulator 127.0.0.1 9000 25 8000
```

## Questions à te poser (on en discute après)

- Pourquoi `Stats` doit-il être `Q_DECLARE_METATYPE` + `qRegisterMetaType` ?
  (indice : que fait une connexion *queued* de l'argument ?)
- Le `QImage` émis est copié à la traversée du thread. Si `toQImage` **ne** faisait
  **pas** `.copy()`, que partagerait la copie, et pourquoi ça casse ?
- Ici, chaque trame émise crée un événement Qt. Si le GUI est plus lent que le
  réseau, la file **grossit** (contrairement au `LatestFrame` « le plus récent
  gagne »). Est-ce un problème à ~25 fps mono-flux ? Comment le corriger si besoin ?
- `onFrame`/`onTelemetry` s'exécutent sur quel thread ? Comment en être **sûr**
  (ex. `QThread::currentThread()`) ?

## Limites assumées (v1)

Pas de RTT (nécessite la sonde `FLAG_PROBE`, plus tard). Pas de coalescing type
`LatestFrame` : on s'appuie sur la file Qt (suffisant en mono-flux). On garde
volontairement minimal.
