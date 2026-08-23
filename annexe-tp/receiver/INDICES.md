# TP-P1b — Indices progressifs

---

## Partie 1 — `MetricsWindow`

<details><summary>Indice 1 — add : latence signée + éviction</summary>

```cpp
const std::int64_t delta =
    static_cast<std::int64_t>(recv_us) - static_cast<std::int64_t>(emit_us);
const double latency_ms = (delta > 0) ? (delta / 1000.0) : 0.0;
samples_.push_back(Sample{recv_us, latency_ms});

while (!samples_.empty() && samples_.front().recv_us + window_us_ < recv_us) {
    samples_.pop_front();
}
```

Le calcul en `int64_t` évite le débordement si `recv < emit`. L'éviction compare
l'ancienneté du plus vieil échantillon à la fenêtre, **relativement à la trame
qu'on vient d'ajouter**.

</details>

<details><summary>Indice 2 — fps, latence, gigue</summary>

```cpp
double MetricsWindow::fps() const {
    const double window_s = window_us_ / 1e6;
    return (window_s > 0) ? samples_.size() / window_s : 0.0;
}
double MetricsWindow::avg_latency_ms() const {
    if (samples_.empty()) return 0.0;
    double s = 0; for (const auto& x : samples_) s += x.latency_ms;
    return s / samples_.size();
}
double MetricsWindow::jitter_ms() const {
    if (samples_.empty()) return 0.0;
    const double moy = avg_latency_ms();
    double s = 0; for (const auto& x : samples_) s += std::fabs(x.latency_ms - moy);
    return s / samples_.size();     // écart absolu moyen
}
```

`<cmath>` pour `std::fabs`. Attention aux divisions par la taille : garde le
`if (empty) return 0`.

</details>

---

## Partie 2 — le `Receiver`

<details><summary>Indice 1 — brancher on_frame (constructeur)</summary>

```cpp
reassembler_.on_frame = [this](const cam::Frame& frame) {
    const std::uint64_t recv = now_us();       // fournie dans le .cpp
    metrics_.add(frame.timestamp_us, recv);
    if (on_frame) on_frame(frame);
};
```

C'est le seul endroit où l'on connaît l'instant d'émission (`timestamp_us`) ET
de réception : donc le seul endroit où mesurer la latence.

</details>

<details><summary>Indice 2 — start et stop</summary>

```cpp
void Receiver::start() {
    running_ = true;
    asio::co_spawn(socket_.get_executor(), loop(), asio::detached);
}
void Receiver::stop() {
    running_ = false;
    std::error_code ignore;
    socket_.close(ignore);   // débloque le async_receive_from -> operation_aborted
}
```

`co_spawn(..., asio::detached)` lance la coroutine sans en attendre le résultat.
`<system_error>` pour `std::error_code`.

</details>

<details><summary>Indice 3 — la boucle de réception</summary>

```cpp
asio::awaitable<void> Receiver::loop() {
    std::error_code ec;
    while (running_) {
        asio::ip::udp::endpoint from;
        const std::size_t n = co_await socket_.async_receive_from(
            asio::buffer(buffer_), from,
            asio::redirect_error(asio::use_awaitable, ec));
        if (ec) {
            if (ec == asio::error::operation_aborted) break;
            continue;
        }
        reassembler_.feed(buffer_.data(), n);
    }
    co_return;
}
```

`async_receive_from` prend un tampon (ici le membre `buffer_`), un endpoint de
sortie (`from`, qui te dira qui a envoyé — inutile ici mais requis), et le token.
Avec `redirect_error`, l'erreur va dans `ec` au lieu de lever : tu la testes
comme au TP9. `operation_aborted` = `stop()` a fermé la socket → on sort.

</details>

<details><summary>Indice 4 — si un test se fige</summary>

Un test qui ne finit pas = `io.run()` a toujours du travail. C'est normal ici :
la boucle de réception repose toujours un `async_receive_from`. Les tests
utilisent donc `io.run_for(200ms)` (borne le temps) au lieu de `io.run()`. Si
TON test à toi se fige, vérifie que `stop()` ferme bien la socket, et que la
boucle teste `operation_aborted` pour sortir.

</details>

---

## Environnement suspect ?

```bash
cmake -S . -B build-sol -DRECV_USE_SOLUTION=ON -DSIM_USE_SOLUTION=ON
cmake --build build-sol && ctest --test-dir build-sol -R "recv"
```
