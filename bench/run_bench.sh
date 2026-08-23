#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_bench.sh — balayage de débit pour le banc TP-P4.
#
# Pour chaque fps cible : lance recv_baseline (en fond), lance replayer, récupère
# la ligne CSV du récepteur + le fps réellement atteint par le générateur, et
# agrège UNE ligne par run dans un CSV (en-tête écrit une seule fois).
#
# Usage :
#   ./run_bench.sh [fps1 fps2 ...]                 # defaut: 500..5000
#
# Réglages par variables d'environnement (avec leurs valeurs par défaut) :
#   BIN=./build-rel/bench   dossier des binaires (compile en Release !)
#   PORT=9000  FRAME_BYTES=20000  SECONDS_RUN=5  IDLE_MS=800  SEED=1
#   LOSS=0  CORRUPT=0        pertes / corruption injectées (%)
#   OUT=bench.csv            fichier de sortie
#   RECV_CPU=  GEN_CPU=      coeurs d'epinglage (taskset) ; vide = pas d'epinglage
#   HOG=0                    HOG=1 + RECV_CPU=N -> lance un hog CPU sur ce coeur
#   REPEATS=1                nb de passes par point (colonne 'pass' ; ~5 conseille)
#   RECV_BIN=$BIN/recv_baseline   recepteur teste (pointe sur recv_asio pour ton impl)
#
# Exemple "environnement restreint" (recepteur affame sur le coeur 3) :
#   RECV_CPU=3 GEN_CPU=5 HOG=1 OUT=baseline.csv ./run_bench.sh 1000 2000 3000 4000 5000
# ---------------------------------------------------------------------------
set -u

BIN="${BIN:-./build-rel/bench}"
# Recepteur a tester : par defaut le baseline ; pointe RECV_BIN sur recv_asio
# pour mesurer ton implementation (l'impl s'auto-etiquette dans le CSV).
RECV_BIN="${RECV_BIN:-$BIN/recv_baseline}"
REPLAYER_BIN="${REPLAYER_BIN:-$BIN/replayer}"
PORT="${PORT:-9000}"
FRAME_BYTES="${FRAME_BYTES:-20000}"
SECONDS_RUN="${SECONDS_RUN:-5}"
IDLE_MS="${IDLE_MS:-800}"
SEED="${SEED:-1}"
LOSS="${LOSS:-0}"
CORRUPT="${CORRUPT:-0}"
OUT="${OUT:-bench.csv}"
RECV_CPU="${RECV_CPU:-}"
GEN_CPU="${GEN_CPU:-}"
HOG="${HOG:-0}"
REPEATS="${REPEATS:-1}"    # nb de passes par point (colonne 'pass' ; median a agreger)

FPS_LIST=("$@")
if [ ${#FPS_LIST[@]} -eq 0 ]; then FPS_LIST=(500 1000 2000 3000 4000 5000); fi

if [ ! -x "$RECV_BIN" ] || [ ! -x "$REPLAYER_BIN" ]; then
  echo "ERREUR: binaires introuvables (RECV_BIN='$RECV_BIN', REPLAYER_BIN='$REPLAYER_BIN')." >&2
  echo "  Compile d'abord :  cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel -j" >&2
  echo "  (ou pointe BIN=... / RECV_BIN=... vers le bon dossier)" >&2
  exit 1
fi

# Préfixes taskset (tableaux pour ne pas casser sur les espaces).
RECV_PREFIX=(); [ -n "$RECV_CPU" ] && RECV_PREFIX=(taskset -c "$RECV_CPU")
GEN_PREFIX=();  [ -n "$GEN_CPU" ]  && GEN_PREFIX=(taskset -c "$GEN_CPU")

# Hog CPU optionnel sur le coeur du récepteur.
HOGPID=""
cleanup() { [ -n "$HOGPID" ] && kill "$HOGPID" 2>/dev/null; }
trap cleanup EXIT INT TERM
if [ "$HOG" = "1" ]; then
  if [ -z "$RECV_CPU" ]; then echo "HOG=1 demande RECV_CPU=<coeur>." >&2; exit 2; fi
  taskset -c "$RECV_CPU" yes > /dev/null &
  HOGPID=$!
  echo "[bench] hog CPU sur coeur $RECV_CPU (pid $HOGPID)"
fi

echo "pass,target_fps,offered_fps_real,impl,cpu_ms,cpu_pct,delivered,unique,lost,corrupt,duplicate,reordered,seconds,fps,loss_pct,jitter_ms" > "$OUT"

# Passes en boucle EXTERNE (une passe = un balayage complet), fps en interne :
# ca eparpille la derive lente (thermique, autres process) sur tous les points
# au lieu de la concentrer sur un seul.
for P in $(seq 1 "$REPEATS"); do
  echo "[bench] === passe ${P}/${REPEATS} ==="
  for FPS in "${FPS_LIST[@]}"; do
    RECVOUT="$(mktemp)"
    "${RECV_PREFIX[@]}" "$RECV_BIN" "$PORT" "$IDLE_MS" > "$RECVOUT" 2>/dev/null &
    RPID=$!
    sleep 0.3
    REAL="$("${GEN_PREFIX[@]}" "$REPLAYER_BIN" 127.0.0.1 "$PORT" "$FPS" "$FRAME_BYTES" "$SECONDS_RUN" "$LOSS" "$CORRUPT" "$SEED" 2>&1 \
            | sed -n 's/.*fps_atteint=\([0-9.]*\).*/\1/p')"
    wait "$RPID"
    DATA="$(tail -n1 "$RECVOUT")"     # 2e ligne = la ligne de donnees du releve
    rm -f "$RECVOUT"
    echo "${P},${FPS},${REAL:-NA},${DATA}" >> "$OUT"
    echo "   [p${P}] ${FPS} -> ${DATA}"
  done
done

echo "[bench] ecrit dans $OUT :"
column -s, -t "$OUT" 2>/dev/null || cat "$OUT"
