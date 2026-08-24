# ESP32-CAM → UDP → PC : pipeline vidéo temps réel + banc de récepteurs

![CI](https://github.com/CorentinBeaufils/esp32-cam/actions/workflows/ci.yml/badge.svg)

Flux vidéo temps réel : une **ESP32-CAM** capture du JPEG, l'envoie en **UDP** vers un
PC qui **réassemble**, mesure la télémétrie (fps, pertes, corruption, gigue), **affiche**
(OpenCV) et **agrandit** (upscaling classique adaptatif). Le tout couronné par une
**étude comparative mesurée de récepteurs** (bloquant vs asio) — le cœur du projet.

> **Point fort du dépôt → [`BILAN-BANC.md`](BILAN-BANC.md)** : cinq manches de mesures
> qui répondent, chiffres à l'appui, à « asio vaut-il le coup ici ? ».

## Résultats en un coup d'œil

Montée en charge multi-flux (N récepteurs concurrents, un budget de cœurs fixe) : le
thread-par-socket reste **sans perte** ; l'async mono-thread décroche dès N≈64.

![Comparaison multi-flux](bench/charts/multiflux.png)

**La cause racine n'était pas l'architecture, mais le tampon `SO_RCVBUF`.** Correctement
dimensionné, l'asio shardé rejoint le thread-par-socket (**0 % de perte**, CPU comparable) :

![Effet du tampon de réception](bench/charts/rootcause_buffer.png)

Méthode complète, cinq manches et données brutes → **[`BILAN-BANC.md`](BILAN-BANC.md)**
(graphes interactifs : [`bench/comparison*.html`](bench/)).

## Choix techniques

- **UDP volontaire** : en temps réel, perdre une trame vaut mieux que bloquer. Le
  protocole porte de quoi *détecter et mesurer* ce qu'UDP ne garantit pas — en-tête
  30 octets big-endian (`magic`, `frame_id`, `timestamp_us`, fragmentation, `payload_crc`),
  `MAX_PAYLOAD=1200`, CRC32 par table.
- **Réassemblage « le plus récent gagne »** (buffer 2 trames, drop-oldest) : la fraîcheur
  prime sur la complétude.
- **Réception asynchrone** côté PC via asio (coroutines, `async_receive_from`) — dont ce
  dépôt montre, mesures à l'appui, les vraies limites face à un modèle bloquant.

## Architecture

```
ESP32-CAM ──JPEG/UDP──► PC : réassemblage → décodage → upscaling → affichage
                                    └────────► télémétrie (fps, pertes, gigue)
```

Deux visualiseurs distincts consomment ce flux : `display/viewer` (affichage direct)
et `upscaler/viewer_up` (agrandissement adaptatif **puis** affichage).

| Module | Rôle |
|---|---|
| `common/` (`cam`) | protocole : fragmentation, réassemblage, CRC, télémétrie (pur, testé) |
| `simulator/` (`sim`) | « faux ESP32 » : émetteur UDP synthétique |
| `receiver/` (`rx`) | récepteur asio (C++20, coroutines) + fenêtre de métriques |
| `display/` (`disp`) | handoff thread-safe `LatestFrame` + visualiseur OpenCV |
| `upscaler/` (`up`) | upscaling classique **adaptatif** dans le budget temps réel |
| `bench/` | **le banc** : générateur, 5 récepteurs, métrique commune, harnais, graphes |
| `firmware/` | firmware ESP32 réel (PlatformIO) |

> `annexe-tp/` (énoncés, indices, corrigés des TP d'origine) est conservé en local
> pour révision mais **git-ignoré** — hors du dépôt public.

## Construire & lancer

```bash
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel -j
ctest --test-dir build-rel --output-on-failure     # logique pure (Catch2)

# en vrai (OpenCV requis) :
./build-rel/receiver/receiver 9000                 # télémétrie headless
./build-rel/display/viewer 9000                    # affichage
./build-rel/upscaler/viewer_up 9000 2 30           # affichage + upscaling x2, budget 30 ms
```

Le banc (générateur + récepteurs, sockets POSIX, sans OpenCV) : voir
[`BILAN-BANC.md`](BILAN-BANC.md) § *Reproduire*.

## Firmware

PlatformIO (`platform = espressif32`, board `esp32dev`). **Copier
`firmware/src/config.example.h` en `config.h`** et y mettre ses identifiants Wi-Fi :
`config.h` est **git-ignoré** (ne jamais commiter d'identifiants).

## Ce que le banc démontre (résumé)

Pour une poignée de flux vidéo actifs, un `recvfrom` **bloquant par socket** est le
choix le plus simple et le plus performant ; l'écart apparent en sa faveur en multi-flux
venait d'un **`SO_RCVBUF` trop petit**, pas de l'architecture — correctement réglé,
l'asio shardé **égale** les threads. asio devient pertinent au **c10k** (des milliers de
connexions inactives), pas ici. Détails et données : [`BILAN-BANC.md`](BILAN-BANC.md).
