# TP-P1b — Le récepteur UDP + télémétrie temps réel

**Durée estimée :** 2 h 30 — **Prérequis :** TP0-TP9, Phase 0, TP-P1a — **Fichiers à compléter :** `src/metrics.cpp`, `src/receiver.cpp`

---

## Où on en est

Ton simulateur émet un flux UDP. On écrit maintenant le **récepteur** : il reçoit
les datagrammes, les réassemble en trames (ton `Reassembler` de la Phase 0), et
calcule la télémétrie en direct. C'est la moitié « ta version » du comparatif à
venir.

Deux pièces :

- **`MetricsWindow`** — le calcul des métriques temporelles (fps, latence,
  gigue) sur une fenêtre glissante. Logique pure, testable exactement.
- **`Receiver`** — la boucle de réception UDP. Nouveauté : **UDP asynchrone**
  (`async_receive_from` en coroutine). L'émetteur envoyait en synchrone ; un
  récepteur, lui, ne doit jamais bloquer — d'où l'asynchrone, en style linéaire
  du TP8/TP9.

Les pertes et la corruption, elles, sont **déjà** comptées par le `Reassembler`
(Phase 0) : tu les lis, tu ne les recalcules pas.

---

## Partie 1 — `MetricsWindow` *(1 h)*

```cpp
void   add(std::uint64_t emit_us, std::uint64_t recv_us);
double fps() const;
double avg_latency_ms() const;
double jitter_ms() const;
```

Une **fenêtre glissante** sur les dernières trames (défaut 1 s). Pourquoi une
fenêtre, et pas une moyenne depuis le début ? Parce qu'une moyenne globale
lisserait tout : si le flux se dégrade dans la dernière seconde, tu veux le voir,
pas le noyer sous dix minutes de bonnes mesures.

**`add`** : calcule la latence (`recv - emit`), empile l'échantillon, puis
évince par l'avant tout ce qui est plus vieux que la fenêtre.

⚠️ **Le piège du calcul de latence.** `recv_us` et `emit_us` sont des `uint64_t`.
Dès que l'émetteur et le récepteur sont deux machines différentes, leurs horloges
diffèrent légèrement, et `recv` peut être **inférieur** à `emit`. Une soustraction
non signée donnerait alors un nombre gigantesque. Calcule la différence en
**`int64_t`** et borne-la à ≥ 0.

**`fps`** : nombre d'échantillons dans la fenêtre / largeur de la fenêtre en
secondes. **`jitter`** : l'écart absolu moyen des latences autour de leur moyenne
(« de combien la latence bouge, en moyenne ») — plus lisible qu'un écart-type
pour de la gigue réseau, et sans racine carrée.

---

## Partie 2 — le `Receiver` *(1 h 30 — la nouveauté UDP async)*

```cpp
Receiver(asio::io_context& io, unsigned short port);
void start();
void stop();
```

**Le constructeur** lie déjà la socket (dans la liste d'initialisation). Il te
reste à **brancher `reassembler_.on_frame`** : à chaque trame complète, mesurer
la latence (`now_us()` fournie) et prévenir l'utilisateur.

```cpp
reassembler_.on_frame = [this](const cam::Frame& frame) {
    const std::uint64_t recv = now_us();
    metrics_.add(frame.timestamp_us, recv);
    if (on_frame) on_frame(frame);
};
```

**`loop()`** est le cœur. C'est une coroutine qui reçoit en boucle :

```cpp
while (running_) {
    asio::ip::udp::endpoint from;
    std::error_code ec;
    const std::size_t n = co_await socket_.async_receive_from(
        asio::buffer(buffer_), from, asio::redirect_error(asio::use_awaitable, ec));
    if (ec) {
        if (ec == asio::error::operation_aborted) break;   // stop() a fermé la socket
        continue;                                          // autre erreur : on continue
    }
    reassembler_.feed(buffer_.data(), n);
}
```

`async_receive_from` remplit `buffer_` avec **un** datagramme et donne sa taille.
`buffer_` est un membre : il survit au `co_await` (comme les locales d'une
coroutine), et comme une seule boucle tourne à la fois, aucun conflit d'accès.

**`start`** lance cette coroutine : `asio::co_spawn(socket_.get_executor(),
loop(), asio::detached)`. **`stop`** ferme la socket, ce qui fait échouer le
`async_receive_from` en attente avec `operation_aborted` (souvenir du TP2/TP9) —
la boucle sort proprement, sans blocage.

**Pourquoi async ici et sync pour l'émetteur ?** L'émetteur pousse quand il veut,
un envoi à la fois. Le récepteur, lui, doit être prêt à tout instant sans jamais
bloquer le thread — sinon il raterait des datagrammes pendant qu'il attend. C'est
exactement le rôle d'asio.

---

## Valider

```bash
cmake -S . -B build-sol -DSIM_USE_SOLUTION=ON -DRECV_USE_SOLUTION=ON
cmake --build build-sol -j && ctest --test-dir build-sol --output-on-failure

cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

Puis, **en vrai**, dans deux terminaux :

```bash
# Terminal 1 — le récepteur (à lancer en premier)
./build/receiver/receiver 9000
# Terminal 2 — le simulateur
./build/simulator/simulator 127.0.0.1 9000 25 8000
```

Le récepteur affiche chaque seconde : `fps ≈ 25`, une latence de quelques ms sur
la loopback, une gigue faible, et 0 perte / 0 corruption. Tu vois ton flux vivre.

## Critères de réussite

- [ ] Les tests `[metrics]` et `[receiver]` passent
- [ ] Récepteur + simulateur tournent ensemble et affichent ~25 fps stable
- [ ] Tu sais pourquoi la latence doit se calculer en signé (dérive d'horloge)
- [ ] Tu sais pourquoi le récepteur est asynchrone alors que l'émetteur est
      synchrone
- [ ] Tu sais comment `stop()` débloque la boucle (fermeture → `operation_aborted`)

Ensuite : **TP-P1c** — l'affichage OpenCV et le handoff « dernière image gagne »
entre le thread réseau et le thread d'affichage. Ton intuition read-on-write s'y
concrétise enfin.
