# TP-P3 — Upscaling classique dans le budget temps réel

**Durée estimée :** 2 h — **Prérequis :** Phase 0-2, TP-P1a/b/c — **Fichiers à compléter :** `src/scale_stats.cpp`, `src/upscale_policy.cpp`

---

## L'idée

Ta caméra envoie du VGA (640×480). On veut l'afficher plus grand — QVGA/VGA → 2× ou 3× — en **agrandissant côté PC**. C'est le bon endroit : la source est contrainte (l'ESP32), la machine puissante est ici.

Mais agrandir coûte du temps, et on est en temps réel : à ~28 fps tu as **~35 ms par image** pour tout faire (décoder + agrandir + afficher). Les algos d'interpolation ne coûtent pas pareil, du plus grossier au plus fin :

```
Nearest   <   Linear   <   Cubic   <   Lanczos
(rapide, crénelé)                    (lent, net)
```

Choisir Lanczos une fois pour toutes, c'est risquer de rater le budget sur une machine lente. Choisir Nearest, c'est laisser de la qualité sur la table quand la machine suit. La bonne réponse est **adaptative** : on mesure le coût réel et on ajuste la méthode image par image.

Ce TP n'écrit **pas** d'OpenCV. Tu écris les deux pièces **pures** qui pilotent la décision ; le viewer (fourni) les branche à `cv::resize`. C'est la même séparation qu'au TP-P1c : lib pure testable d'un côté, OpenCV dans l'exécutable de l'autre.

---

## Partie 1 — `ScaleStats` *(~40 min)*

Une fenêtre glissante sur les **N derniers coûts** (en ms). Cousine de ta `MetricsWindow` (TP-P1b), en plus simple : ici on évince par le **nombre** d'échantillons, pas par le temps.

```cpp
void        record(double ms);
std::size_t count() const;
double      avg_ms() const;              // 0 si vide
double      max_ms() const;              // 0 si vide
double      p95_ms() const;              // 95e centile, 0 si vide
std::size_t over_budget(double budget_ms) const;   // combien au-dessus du budget
```

Le point neuf, c'est le **p95** (95e centile). Pourquoi lui et pas seulement la moyenne ? Parce que ce n'est pas le coût *moyen* qui fait sauter une image, c'est le coût dans le **pire cas courant**. Une moyenne à 20 ms avec des pointes régulières à 45 ms rate le temps réel une image sur dix — la moyenne le cache, le p95 le montre.

Méthode « nearest-rank » : trie une **copie** des échantillons (ne réordonne jamais `samples_`, l'ordre chronologique sert au viewer), prends le rang `ceil(0.95 · n)`, renvoie la valeur à ce rang. Attention au passage rang 1-based → index 0-based.

---

## Partie 2 — `UpscalePolicy` *(~1 h 20 — le cœur du TP)*

Le contrôleur adaptatif. Tu lui donnes le temps mesuré après chaque upscale ; il te rend la méthode à utiliser pour la **prochaine** image.

```cpp
UpscalePolicy(double budget_ms, Interp start = Interp::Cubic);
Interp update(double measured_ms);   // renvoie la methode a utiliser ensuite
Interp current() const;
```

La règle, en trois cas :

1. **On dépasse le budget** (`measured > budget`) → on **redescend d'un cran** tout de suite (sans passer sous `Nearest`). Rater le temps réel coûte une image sautée : on ne temporise pas.
2. **On est confortablement sous le budget** (`measured < budget × 0.6`) → on accumule de la confiance ; après **8 images** confortables **consécutives**, on **remonte d'un cran** (sans dépasser `Lanczos`) et on remet le compteur à zéro.
3. **Entre les deux** (dans le budget mais pas confortable) → on **reste**, et on **casse la série** de confiance.

Le point subtil est l'**hystérésis** : on descend vite mais on remonte lentement, et jamais sur une seule bonne image. Sans ça, à la frontière d'un niveau, tu oscillerais entre deux méthodes à chaque image (flicker visible). L'asymétrie « descente immédiate / montée prudente » est exactement celle d'un contrôle de débit d'encodeur vidéo.

`Interp` est un `enum class` ordonné de 0 (Nearest) à 3 (Lanczos). L'ordre **est** la logique : monter/descendre = `±1` sur la valeur entière, borné aux extrêmes.

---

## Valider

```bash
# Tests purs (pas besoin d'OpenCV) — squelette puis corrigé :
cmake -S . -B build && cmake --build build -j
ctest --test-dir build -R upscale --output-on-failure

cmake -S . -B build-sol -DUP_USE_SOLUTION=ON
cmake --build build-sol -j && ctest --test-dir build-sol -R upscale --output-on-failure
```

Puis, **en vrai**, avec OpenCV installé (`sudo apt install libopencv-dev`) et l'ESP32 (ou le simulateur) qui émet :

```bash
./build/upscaler/viewer_up 9000 2 30      # port 9000, x2, budget 30 ms
```

Regarde l'incrustation en haut de l'image : la méthode courante, le coût (dernier / moyenne / p95), les dépassements, et les compteurs de descentes `v` / montées `^`. Démarre en **Lanczos** volontairement : si ta machine ne suit pas, tu **verras** la politique redescendre vers Cubic/Linear toute seule, puis éventuellement remonter. Fais varier le facteur (`3`, `4`) et le budget (`15`, `50`) pour provoquer l'adaptation.

## Critères de réussite

- [ ] Les tests `[up]` passent (squelette **et** corrigé)
- [ ] Tu sais pourquoi le **p95** compte plus que la moyenne pour le temps réel
- [ ] Tu sais pourquoi la montée exige une **série stable** et la descente non (hystérésis)
- [ ] En live, la méthode s'adapte quand tu changes le facteur ou le budget
- [ ] Tu sais pourquoi l'upscale se mesure **au plus serré** (juste `cv::resize`), pas décodage inclus

---

## Pour aller plus loin (bonus, hors barème)

- **Facteur d'estimation par méthode.** Aujourd'hui la politique ne connaît que le coût de la méthode *courante*. On pourrait mémoriser le coût typique de **chaque** méthode et sauter directement au bon niveau au lieu de descendre cran par cran.
- **Comparatif d'images.** Affiche côte à côte source et agrandi (`cv::hconcat`), ou deux méthodes en parallèle, pour juger la qualité à l'œil — utile pour la Phase 5 (super-résolution IA) où on comparera au classique.
- **Budget dérivé du fps réel.** Au lieu d'un budget fixe, calcule-le depuis le fps observé (`1000 / fps`) pour qu'il s'ajuste au flux.
