# TP-P3 — Indices (progressifs)

Ouvre-les seulement si tu bloques. Chaque niveau en dit un peu plus.

---

## ScaleStats

<details>
<summary>Indice 1 — record / fenêtre</summary>

`push_back` l'échantillon, puis `while (samples_.size() > window_) samples_.pop_front();`. C'est tout : une `std::deque<double>` bornée par le nombre.
</details>

<details>
<summary>Indice 2 — p95, méthode</summary>

Ne trie pas `samples_` en place (le viewer compte sur l'ordre chrono). Copie dans un `std::vector<double>`, `std::sort`, puis prends le rang.
</details>

<details>
<summary>Indice 3 — p95, le rang exact</summary>

```cpp
std::size_t rang = static_cast<std::size_t>(std::ceil(0.95 * n));
if (rang == 0) rang = 1;
if (rang > n) rang = n;
return trie[rang - 1];   // 1-based -> 0-based
```
Sur 100 valeurs 1..100 : `ceil(95) = 95` → `trie[94] = 95`. Sur 1 valeur : rang 1 → `trie[0]`.
</details>

---

## UpscalePolicy

<details>
<summary>Indice 1 — monter / descendre d'un cran proprement</summary>

Deux helpers dans un `namespace {}` anonyme, bornés aux extrêmes :

```cpp
Interp lower(Interp i) {
    if (i == Interp::Nearest) return Interp::Nearest;
    return static_cast<Interp>(static_cast<int>(i) - 1);
}
Interp higher(Interp i) {
    if (i == Interp::Lanczos) return Interp::Lanczos;
    return static_cast<Interp>(static_cast<int>(i) + 1);
}
```
</details>

<details>
<summary>Indice 2 — la structure des trois cas</summary>

```cpp
if (measured_ms > budget_ms_) {
    good_streak_ = 0;
    // descendre (si possible) + ++downgrades_
} else if (measured_ms < budget_ms_ * 0.6) {
    ++good_streak_;
    if (good_streak_ >= 8) {
        // monter (si possible) + ++upgrades_
        good_streak_ = 0;
    }
} else {
    good_streak_ = 0;   // dans le budget mais pas confortable
}
return current_;
```
</details>

<details>
<summary>Indice 3 — ne compter un vrai changement que s'il a lieu</summary>

N'incrémente `downgrades_`/`upgrades_` que si la méthode a **effectivement** changé (sinon, à `Nearest`, un dépassement gonflerait le compteur sans rien bouger) :

```cpp
const Interp cible = lower(current_);
if (cible != current_) { current_ = cible; ++downgrades_; }
```
Idem pour la montée avec `higher`.
</details>

<details>
<summary>Si vraiment bloqué</summary>

Compare avec `solution/upscale_policy.cpp`. Lis surtout le pourquoi de l'asymétrie descente/montée dans l'en-tête et l'ENONCE : c'est le concept à retenir, pas les lignes.
</details>
