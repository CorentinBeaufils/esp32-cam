# TP-P4 — Indices (progressifs)

Ouvre-les seulement si tu bloques. Chaque niveau en dit un peu plus.

---

## RunReport — on_frame

<details>
<summary>Indice 1 — l'ossature des trois blocs</summary>

Trois responsabilités indépendantes dans `on_frame`, dans cet ordre :

```cpp
// 1) compteurs de base
++delivered_;
if (!crc_ok) ++corrupt_;

// 2) sequence (frame_id)
if (!have_any_) { have_any_ = true; base_id_ = frame_id; max_id_ = frame_id; }
else { /* desordre + max */ }
if (!seen_.insert(frame_id).second) ++duplicate_;
prev_id_ = frame_id;

// 3) timing (arrival_ms)
if (!have_time_) { have_time_ = true; first_ms_ = arrival_ms; }
else { gaps_.push_back(arrival_ms - prev_ms_); /* borne */ }
last_ms_ = arrival_ms;
prev_ms_ = arrival_ms;
```
</details>

<details>
<summary>Indice 2 — désordre & max</summary>

Dans la branche `else` (pas la première trame) :

```cpp
if (frame_id < prev_id_) ++reordered_;   // arrive apres un id plus grand
if (frame_id > max_id_)  max_id_ = frame_id;
```
Le désordre se juge par rapport à l'id **précédent** (`prev_id_`), la borne de perte par rapport au **max** vu.
</details>

<details>
<summary>Indice 3 — borner la fenêtre de gigue</summary>

Comme ta `ScaleStats` (P3) : une `std::deque` bornée par le nombre.

```cpp
gaps_.push_back(arrival_ms - prev_ms_);
while (gaps_.size() > jitter_window_) gaps_.pop_front();
```
</details>

---

## RunReport — snapshot

<details>
<summary>Indice 4 — perte (expected / lost)</summary>

```cpp
if (have_any_) {
    const std::uint64_t expected = static_cast<std::uint64_t>(max_id_ - base_id_) + 1;
    r.lost = (expected > r.unique) ? (expected - r.unique) : 0;
    r.loss_pct = 100.0 * double(r.lost) / double(expected);
}
```
`r.unique = seen_.size()`. Le `max(0, ...)` protège si des doublons faisaient `unique > expected` (ne devrait pas, mais garde-fou).
</details>

<details>
<summary>Indice 5 — débit (le fencepost)</summary>

```cpp
r.seconds = have_time_ ? (last_ms_ - first_ms_) / 1000.0 : 0.0;
if (r.seconds > 0.0 && r.delivered > 1)
    r.fps = double(r.delivered - 1) / r.seconds;   // n trames -> n-1 intervalles
```
</details>

<details>
<summary>Indice 6 — gigue (écart absolu moyen)</summary>

Deux passes sur `gaps_` : la moyenne, puis la moyenne des écarts absolus à cette moyenne.

```cpp
double m = 0; for (double g : gaps_) m += g; m /= gaps_.size();
double mad = 0; for (double g : gaps_) mad += std::fabs(g - m); mad /= gaps_.size();
r.jitter_ms = mad;
```
Même définition que `rx::MetricsWindow::jitter_ms()` — reste cohérent avec le reste du code.
</details>

<details>
<summary>Si vraiment bloqué</summary>

Compare avec `solution/run_report.cpp`. Le concept à retenir, ce n'est pas les lignes : c'est **perte ≠ corruption ≠ doublon ≠ désordre** et le **fencepost du débit**.
</details>
