# TP-P4 — Banc de comparaison des récepteurs

**Durée estimée :** ~2 h (la lib pure) — **Prérequis :** Phases 0-3 — **Fichier à compléter :** `src/run_report.cpp`

---

## L'idée

Tu as un récepteur soigné (asio, coroutines, réassemblage « le plus récent gagne »). Question honnête : **est-ce que ça vaut le coup** face à un récepteur bête (`recvfrom` bloquant, une boucle) ? Pour répondre sans te raconter d'histoires, il faut **mesurer**, et mesurer **équitablement**.

Trois principes tiennent tout le banc :

1. **Même flux pour tout le monde.** Un vrai flux ESP n'est pas reproductible (le Wi-Fi bouge d'une seconde à l'autre) : mauvais socle pour un A/B. On **enregistre une fois** un vrai flux, puis on le **rejoue** à l'identique contre chaque récepteur — sur `127.0.0.1` (loopback), donc **jamais sur le réseau** (zéro risque pour les autres, et aucune perte parasite : loopback ne drop pas).
2. **Isoler le récepteur.** Générateur et récepteur sur des **cœurs séparés** (`taskset`) ; on ne mesure que le récepteur. Pour faire *diverger* deux architectures à faible débit, on **affame le CPU** (E-core, ou un hog sur le même cœur) ou on **monte le débit** (N flux, plus petit MTU). C'est là qu'asio est censé payer — sur un seul flux VGA modeste, l'émetteur (l'OV2640) est le goulot, pas le PC, et tu verras peut-être « aucune différence » : c'est un résultat, pas un échec.
3. **Même relevé pour tout le monde.** Chaque récepteur crache **le même enregistrement** — sinon on ne compare rien. C'est l'objet de ce TP.

Ce TP n'écrit **pas** de réseau. Tu écris la pièce **pure** qui transforme un flux de trames livrées en un **relevé comparable** ; les exécutables (replayer, baseline) la branchent. Même séparation qu'aux TP précédents.

---

## Ce que tu écris — `RunReport`

Une classe pure qui ingère les trames qu'un récepteur **livre** (une par une) et produit un `Report` : le format commun, sérialisable en une ligne CSV.

```cpp
void   on_frame(std::uint32_t frame_id, double arrival_ms, bool crc_ok);
Report snapshot() const;
std::string        to_csv() const;      // fourni
static std::string csv_header() const;  // fourni
```

Le point neuf, c'est la **comptabilité de séquence** à partir des `frame_id` de l'en-tête. UDP ne garantit ni livraison, ni ordre, ni intégrité — le protocole porte juste ce qu'il faut pour le *reconstituer* après coup :

- **Perte.** Sur l'intervalle observé `[base, max]` (base = 1er id vu, max = plus grand id vu), on **attend** `max − base + 1` id distincts. Ce qui manque à l'appel est perdu : `lost = expected − unique`. C'est la formule de RTP. Subtil : on ne peut **rien** dire des trames perdues *avant* la première reçue — pas de référence. La 1re trame **fixe** la base.
- **Doublon.** Un même `frame_id` livré deux fois (UDP peut dupliquer, ou ton réassembleur ré-émettre). `seen_.insert(id).second == false` te le dit.
- **Désordre.** Une trame arrivée **après** un id supérieur déjà vu. Ce n'est *pas* une perte : elle est là, juste en retard.
- **Corruption.** `crc_ok == false` : arrivée mais `payload_crc` KO. Distinct de la perte (la trame est bien là).

Et deux mesures de temps :

- **Débit.** `n` trames, c'est `n − 1` **intervalles**. Le fps se calcule sur les intervalles, pas les points : `fps = (delivered − 1) / seconds`. Compter les points sur-estime d'un cran (le classique fencepost).
- **Gigue.** Écart absolu moyen des inter-arrivées autour de leur moyenne — **même choix que `rx::MetricsWindow`** (lisible, pas de racine carrée). Espacement constant → 0.

`RunReport` ne recalcule pas ce que `MetricsWindow` fait déjà en *fenêtre glissante* : ici c'est le relevé **du run entier**, l'unité qu'on compare entre récepteurs.

---

## Valider

```bash
cmake -S . -B build && cmake --build build -j
ctest --test-dir build -R bench --output-on-failure

cmake -S . -B build-sol -DBENCH_USE_SOLUTION=ON
cmake --build build-sol -j && ctest --test-dir build-sol -R bench --output-on-failure
```

## Critères de réussite

- [ ] Les tests `[bench]` passent (squelette **et** corrigé)
- [ ] Tu sais pourquoi la **1re trame fixe la base** (on ne compte pas les pertes d'avant)
- [ ] Tu sais distinguer **perte / corruption / doublon / désordre** (quatre choses différentes)
- [ ] Tu sais pourquoi le débit se calcule sur les **intervalles** (`n−1`), pas les trames
- [ ] Tu sais pourquoi on **rejoue une capture sur loopback** plutôt qu'un flux ESP live

---

## La suite (fournie, après ta lib)

- **`replayer`** — enregistre un vrai flux ESP (octets + horodatage), puis le rejoue sur `127.0.0.1`, ×N flux, à débit réglable. Le générateur de charge, reproductible.
- **`recv_baseline`** — récepteur naïf (`recvfrom` bloquant, réassemblage minimal) qui crache le **même** CSV. L'étalon face à ton asio.
- **Hook `RunReport`** dans ton récepteur asio existant : il émet le même CSV → on aligne les lignes.
- **Le protocole `bin`** de comparaison : `taskset` + hog CPU pour créer l'« environnement restreint », balayage du débit / nb de flux jusqu'au point de bascule.

## Pour aller plus loin (bonus)

- **Borner la mémoire.** `seen_` grandit sur un run long. Sur un flux ~monotone, un `set` des seuls id récents + un compteur suffit.
- **Sonde RTT.** Un bit `FLAG_PROBE` dans l'en-tête existant (aller-retour horodaté côté ESP) → mesurer RTT/offset façon NTP, et au passage le temps de retournement du récepteur (`t2−t1`) = une métrique de comparaison de plus.
- **Débit dérivé du fps réel** plutôt qu'un budget fixe (rejoint le bonus P3).
