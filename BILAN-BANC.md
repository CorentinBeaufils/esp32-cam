# Banc de comparaison des récepteurs UDP — bilan

Étude mesurée : **un récepteur asio (coroutines) vaut-il le coup face à un récepteur
naïf `recvfrom` bloquant**, pour le flux vidéo de l'ESP32-CAM ? Réponse chiffrée,
obtenue en cinq manches sur un banc reproductible.

## TL;DR

- Sur **peu de flux actifs** (le cas de ce projet), le `recvfrom` bloquant par socket
  est le choix le plus simple et le plus performant. asio n'apporte pas d'avantage.
- La supériorité apparente du thread-par-socket en multi-flux venait d'un **tampon de
  réception (`SO_RCVBUF`) trop petit**, pas de l'architecture : correctement dimensionné,
  l'asio shardé **égale** le thread-par-socket (0 % de perte, CPU comparable).
- L'intérêt réel d'asio est le régime **c10k** (des milliers de connexions surtout
  *inactives*) — l'exact opposé de ce flux. Ici, on n'y entre jamais.

## Méthode

- **Machine** : Intel i7-14650HX (8 P-cores + 8 E-cores), WSL2 (Ubuntu).
- **Charge** : générateur synthétique reproductible (`replayer`) émettant le **vrai
  protocole** (en-têtes 30 o, `frame_id`, CRC32) sur **loopback** (`127.0.0.1`) — aucun
  trafic réseau réel, pertes/corruption injectables à la demande.
- **Métrique commune** : `bench::RunReport` (lib pure, testée) — débit, pertes (trous de
  `frame_id`), corruption, doublons, désordre, gigue — sérialisée en CSV identique pour
  tous les récepteurs, donc directement comparables.
- **Rigueur** : récepteur épinglé (`taskset`), **10 passes** par point, médiane +
  bande min–max, affinité vérifiée (`taskset -cp`). Données brutes dans `bench/data/`.

## Récepteurs comparés

| Nom | Modèle |
|---|---|
| `recv_baseline` | `recvfrom` bloquant, un flux |
| `recv_baseline_mt` | **un thread bloquant par socket** (N threads) |
| `recv_asio_mux` | N sockets sur **1 `io_context`, 1 thread** |
| `recv_asio_pool` | N sockets sur **1 `io_context`, M threads** (pool) |
| `recv_asio_shard` | **M `io_context` indépendants, 1 par cœur** (sharding) |

## Les cinq manches

### 1. Flux unique, un demi-cœur affamé (balayage du débit)

Débit monté de 750 à 1450 fps, récepteur sur un cœur partagé avec un hog.

| | baseline | asio (1 flux) |
|---|---|---|
| Perte @1450 fps | ~1,8 % | ~3,2 % |
| CPU | référence | ~+10 % / trame |

→ Le baseline naïf gagne : l'asio paie un surcoût par paquet (CRC, réassemblage
« le plus récent gagne », télémétrie, machinerie coroutine) et sature un peu plus tôt.

### 2. Multi-flux, thread-par-socket vs asio 1-thread (1 cœur)

60 fps/flux, N de 1 à 128.

| N | `baseline_mt` perte | `asio_mux` perte |
|---|---|---|
| ≤32 | 0 % | 0 % |
| 64 | 0 % | 3,6 % |
| 128 | 0 % | 15,5 % |

→ L'async **mono-thread plafonne** : 1 thread = 1 cœur. Le thread-par-socket étale
la charge et reste sans perte.

### 3. Multi-flux, pool asio (2 threads, 1 `io_context`, 2 cœurs)

| N | `baseline_mt` | `asio_pool` |
|---|---|---|
| 64 | 0 % | 3,3 % |
| 128 | 0 % | 15,5 % |

→ Le pool **n'aide pas** : M threads sur *un même* `io_context` se disputent le même
reactor epoll (verrou partagé). Même plafond que le mono-thread, pour ~2× le CPU à
faible charge.

### 4. Multi-flux, sharding (M `io_context` indépendants, 2 cœurs)

| N | `baseline_mt` | `asio_shard` | `cpu_pct` shard |
|---|---|---|---|
| 64 | 0 % | 3,2 % | 44 % |
| 128 | 0 % | 14,9 % | **90 %** |

→ Le sharding **n'aide pas non plus** — mais le **révélateur** apparaît : à N=128, tous
les récepteurs plafonnent **sous 100 % d'un cœur**. **Le CPU n'a jamais été le goulot.**
On ajoutait des cœurs à un problème qui n'en était pas un.

### 5. Multi-flux, sharding + `SO_RCVBUF` = 8 Mo (2 cœurs)

| N | `asio_shard` défaut | `asio_shard` 8 Mo | `baseline_mt` 8 Mo |
|---|---|---|---|
| 64 | 3,2 % | **0 %** | 0 % |
| 128 | 14,9 % | **0 %** | 0 % |

→ **Cause racine trouvée.** Avec un tampon de 8 Mo, la perte de l'async **disparaît**,
à égalité exacte avec le thread-par-socket, pour un CPU comparable. Le déficit de
livraison résiduel (~12 % à N=128) est identique pour les deux : c'est le **générateur**
qui bride, pas le récepteur.

## Explication

La perte de l'async n'était **ni un problème de CPU, ni de cœurs, ni d'architecture** :
c'était la **latence de service sous rafales**. Le thread-par-socket draine chaque socket
*à l'instant* où le paquet arrive (le noyau réveille le thread garé sur ce `fd`), donc le
tampon ne se remplit jamais. Le chemin async ajoute un aller-retour
`epoll → dispatch → coroutine` ; quand toutes les sockets reçoivent en même temps
(rafale synchronisée du générateur), le reactor les sert en file et les derniers tampons
débordent — **sauf** si `SO_RCVBUF` est assez grand pour absorber la rafale.

Note : la rafale parfaitement synchronisée est un artefact du banc (de vraies caméras ne
sont pas *frame-lock*). En conditions réelles, l'écart async/threads serait encore plus
faible.

## Conclusion — quand utiliser quoi

- **Peu de flux actifs (ce projet)** → **threads bloquants** : plus simple, plus bas en
  latence, aucun réglage. asio n'a pas de terrain pour gagner.
- **asio correctement réglé** (`SO_RCVBUF`, sharding par cœur) → **égale** les threads
  sur flux actif, sans les surpasser.
- **c10k** (milliers de connexions surtout inactives) → **asio** : là où un thread par
  socket ruine mémoire et ordonnanceur, l'event loop brille. Non pertinent ici.

Réglage clé retenu : sur un récepteur async, **dimensionner `SO_RCVBUF`** (et
`net.core.rmem_max` côté OS) est plus déterminant que le nombre de threads.

## Reproduire

```bash
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel -j

# balayage débit (manche 1)
bash bench/run_bench.sh 750 800 850 900 950 1000 1050 1100 1150 1200 1250 1300 1350 1400 1450

# balayage multi-flux (manches 2-5), ex. sharding avec gros tampon :
sudo sysctl -w net.core.rmem_max=33554432
RCVBUF=8388608 RECV_CPU=3,4 GEN_CPU=6 REPEATS=5 FPS=60 \
  RECV_BIN=./build-rel/bench/recv_asio_shard OUT=shard_big.csv \
  bash bench/run_fanout.sh 1 2 4 8 16 32 64 128
```

Graphes : `bench/comparison*.html`. Données brutes : `bench/data/*.csv`.
