# État du projet — passation entre conversations

> **À lire en premier dans toute nouvelle conversation.** Ce fichier résume qui je
> suis, où en est le travail, et comment on collabore. La source de vérité reste
> le **code sur disque** (ce fichier peut être légèrement en retard : en cas de
> doute, le dépôt gagne).

## Contexte & profil

- Jeune ingénieur info : socle C solide, C++ **moderne** encore en consolidation
  (shared_ptr/move/RAII/lambda/coroutines vus au fil des TP). Expérience Ubuntu,
  compilation, WSL2.
- **Préférences de réponse : concis et direct, en français.** Peu de blabla.
- **Guidage : allégé** depuis le TP-P1c (l'élève trouve les TP « très guidés »
  sinon). Énoncé qui explique les *concepts*, indices repliés, pas de code mâché.

## Deux dépôts (même dossier `asio-tp/`)

1. **`asio-tp/`** — cours façon TP universitaire pour apprendre asio. **TP0→TP9
   tous faits et validés** par l'élève (tests passent). Namespaces `tpNN`, en
   français. Infra : CMake + FetchContent (asio standalone, Catch2), sondes
   `verif/` + `verifier.sh`.
2. **`asio-tp/projet-esp32-cam/`** — le projet capstone (ci-dessous).

## Le projet ESP32-CAM

Flux vidéo temps réel : ESP32-CAM capture du JPEG → **UDP** → PC réassemble,
mesure la télémétrie (fps, pertes, corruption, latence, gigue), affiche (OpenCV),
**upscale**, puis on comparera plusieurs récepteurs.

### Décisions techniques figées (ne pas re-débattre sans raison)

- **UDP** volontaire (temps réel : perdre une trame > bloquer). Protocole :
  en-tête **30 octets big-endian** {magic `0xE5C0`, version, flags, frame_id,
  timestamp_us, frame_size, fragment_count, fragment_index, payload_size,
  payload_crc}, `MAX_PAYLOAD=1200`, **CRC32 table**. Figé dans `common/` ET
  **copié** dans `firmware/src/cam/` (vendorisé pour PlatformIO).
- Réassemblage **« le plus récent gagne »**, buffer de 2 trames, drop-oldest.
- Handoff réseau→affichage **`LatestFrame`** (latest-wins, mutex). Le compteur
  `dropped()` = trames écrasées avant affichage = écran plus lent que le réseau
  (sain, ≠ perte réseau).
- **Namespace récepteur = `rx`** (pas `recv` : collision avec POSIX `recv()`).
- Latence absolue **non fiable** (horloges ESP32 vs PC non synchronisées) ; ce
  qui compte : **fps, gigue, pertes/corruption**.
- **`JPEG_QUALITY` est inversé** sur esp_camera : nombre **bas = haute qualité**.
  Source retenue : **VGA + quality 10** (propre + vrais pixels), ~25-28 fps —
  plafond matériel OV2640 (même `TARGET_FPS=50` ne dépasse pas ~29).
- WSL2 : réception LAN OK **uniquement en networking `mirrored`** (`.wslconfig`
  `[wsl2] networkingMode=mirrored`, `wsl --shutdown`, pare-feu UDP ouvert,
  `PC_IP` = IP LAN Windows). **Confirmé fonctionnel sur vrai matériel.**

### État des phases

| Phase | Contenu | État |
|---|---|---|
| 0 | Protocole commun (format, CRC, fragmentation, réassemblage, télémétrie) | ✅ fait, testé |
| 1 | Simulateur UDP + récepteur asio + affichage OpenCV (P1a/b/c) | ✅ fait, validé |
| 2 | Firmware ESP32 réel (PlatformIO) | ✅ **flashe et émet en vrai** |
| 3 | Upscaling classique adaptatif + mesure | ✅ **livré — élève est en train de le faire** |
| 4 | Récepteur Node.js + banc de comparaison des récepteurs | ⏳ **prochaine étape** |
| 5 | Upscaling IA (super-résolution), hors temps réel | plus tard |

### Modules côté PC (`projet-esp32-cam/`)

- `common/` (ns `cam`) : `protocol.hpp/.cpp`, `reassembler.hpp/.cpp`. Pur, testé.
- `simulator/` (ns `sim`) : `Pacer`, `Emitter`, `main.cpp` (le « faux ESP32 »).
- `receiver/` (ns `rx`, **C++20**) : `MetricsWindow`, `Receiver` (coroutine
  `async_receive_from`). `main.cpp` = `receiver` (headless, imprime fps/s).
- `display/` (ns `disp`) : `LatestFrame` (pur, testable TSan) + `main.cpp` =
  `viewer` (OpenCV).
- `upscaler/` (ns `up`, **Phase 3**) : `ScaleStats` + `UpscalePolicy` (purs) +
  `main.cpp` = `viewer_up` (OpenCV). **Squelette à remplir par l'élève**,
  solution dans `solution/`, tests `tests/test_upscaler.cpp` (préfixe `upscale::`).
- `firmware/` : PlatformIO, `platform = espressif32@6.9.0`, board `esp32dev`,
  protocole vendorisé. `src/config.h` **gitignoré** (identifiants Wi-Fi — ne
  jamais commiter).

### Comment on travaille (rituel de chaque TP)

Chaque TP a : `src/exercice` (squelette TODO), `solution/`, tests Catch2,
`ENONCE.md`, `INDICES.md` (indices progressifs repliés). L'élève code, puis
demande : (1) critique, (2) signaler ce qui passe « par chance » / mauvaises
pratiques, (3) répondre à ses questions conceptuelles, (4) vérifier sa
compréhension. Options CMake `-D<MOD>_USE_SOLUTION=ON` pour compiler contre le
corrigé.

### Build & run

```bash
cd projet-esp32-cam
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure          # tous les tests
ctest --test-dir build -R upscale                    # juste la Phase 3

# En vrai (OpenCV requis : sudo apt install libopencv-dev) :
./build/receiver/receiver 9000                       # télémétrie headless
./build/display/viewer 9000                          # affichage
./build/upscaler/viewer_up 9000 2 30                 # affichage + upscaling x2, budget 30 ms
# ESP32 flashé + alimenté émet tout seul (WSL en mirrored).
```

### Limites de vérification de l'assistant (sandbox Linux)

GCC plus ancien que chez l'élève ; les compils asio/C++20/coroutines *timeout*
souvent ; TSan ne s'init pas (ASLR). Donc : je valide la **logique pure** sous
ASan/UBSan + checks de syntaxe, et je m'appuie sur les patterns validés + les
runs `verifier.sh`/`ctest` de l'élève pour l'asio.

## Prochaine étape

1. L'élève termine **TP-P3** (`upscaler/src/scale_stats.cpp` + `upscale_policy.cpp`).
2. Il reviendra pour la **critique P3** (rituel habituel).
3. Puis **Phase 4** : récepteur **Node.js** (même protocole/en-tête) + banc de
   comparaison des trois récepteurs (C++ asio / Node / baseline) sur fps, pertes,
   latence CPU. Prévoir un format de métriques commun pour comparer.

## Reprendre dans une nouvelle conversation

1. Attacher le dossier `asio-tp/`.
2. Dire : « Lis `projet-esp32-cam/ETAT-DU-PROJET.md`, on continue le projet
   ESP32-CAM. » (préciser : critique P3, ou démarrer Phase 4).
3. Au besoin, pointer l'`ENONCE.md` du TP concerné.
