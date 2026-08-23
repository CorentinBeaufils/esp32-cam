# TP-P1a — Indices progressifs

---

## Partie 1 — le `Pacer`

<details><summary>Indice 1 — la période depuis le fps</summary>

```cpp
Pacer::Pacer(double target_fps) {
    const double fps = (target_fps > 0.0) ? target_fps : 1.0;  // pas de /0
    period_ = std::chrono::nanoseconds(static_cast<std::int64_t>(1e9 / fps));
}
```

On calcule en nanosecondes pour rester précis même sur des fps non entiers
(29,97 fps, par exemple).

</details>

<details><summary>Indice 2 — start et le squelette de next_wait</summary>

```cpp
void Pacer::start(clock::time_point now) { deadline_ = now; }

std::chrono::nanoseconds Pacer::next_wait(clock::time_point now) {
    deadline_ += period_;                 // créneau de la PROCHAINE image
    if (now <= deadline_) {
        return deadline_ - now;           // à l'heure : on attend
    }
    // ... en retard ...
}
```

`deadline_ += period_` avant tout : on vise toujours le créneau théorique
suivant, ce qui évite la dérive.

</details>

<details><summary>Indice 3 — la branche "en retard"</summary>

```cpp
const auto retard = now - deadline_;
skipped_ += static_cast<std::uint64_t>(retard / period_);  // créneaux manqués
deadline_ = now;   // resynchronisation : on repart d'ici
return std::chrono::nanoseconds(0);
```

`retard / period_` est une division de `duration` par `duration` → un entier
(combien de périodes entières de retard). `deadline_ = now` fait que le prochain
créneau sera `now + period_` : on reprend le rythme sans rafale.

</details>

---

## Partie 2 — l'`Emitter`

<details><summary>Indice 1 — le constructeur UDP</summary>

```cpp
Emitter::Emitter(asio::io_context& io, const std::string& host, unsigned short port)
    : socket_(io) {
    socket_.open(asio::ip::udp::v4());
    dest_ = asio::ip::udp::endpoint(asio::ip::make_address(host), port);
}
```

Pas de `connect` : UDP est sans connexion. On mémorise juste où envoyer.

</details>

<details><summary>Indice 2 — send_frame</summary>

```cpp
const auto datagrams = cam::fragment(frame_id, timestamp_us, jpeg, size);
for (const auto& dg : datagrams) {
    socket_.send_to(asio::buffer(dg), dest_);
    ++datagrams_sent_;
    bytes_sent_ += dg.size();
}
```

`fragment()` fait tout le travail de découpage (Phase 0). Il ne te reste qu'à
pousser chaque datagramme sur la socket.

</details>

<details><summary>Indice 3 — pourquoi send_to synchrone est correct ici</summary>

Un envoi UDP ne « bloque » pas comme une écriture TCP : le noyau prend le
datagramme dans son tampon d'émission et rend la main immédiatement (si le
tampon est plein, il jette — c'est UDP). Pour un émetteur, `send_to` synchrone
est donc parfait, et bien plus simple qu'une coroutine.

Et `asio::buffer(dg)` est sûr parce que `send_to` est **complet** quand il rend
la main : le tampon n'a pas besoin de survivre au-delà de l'appel. C'est
exactement le cas « synchrone » du TP3, où le temporaire était acceptable.

</details>

---

## Environnement suspect ?

```bash
cmake -S . -B build-sol -DSIM_USE_SOLUTION=ON
cmake --build build-sol && ctest --test-dir build-sol -R "sim"
```

Si les corrigés passent, le problème est dans ton code.
