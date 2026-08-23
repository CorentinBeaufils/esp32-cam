# TP-P1a — L'émetteur UDP (le simulateur)

**Durée estimée :** 2 h — **Prérequis :** TP0 à TP9, Phase 0 — **Fichiers à compléter :** `src/pacer.cpp`, `src/emitter.cpp`

---

## Où on en est

La Phase 0 t'a donné le protocole (fragmentation / réassemblage), testé sans
réseau. La Phase 1 le met sur le fil. On commence par l'**émetteur** — le « faux
ESP32 » — parce que c'est le plus autonome et qu'il te fait découvrir la seule
vraie nouveauté asio de cette phase : **UDP**.

Deux pièces à écrire :

- **`Pacer`** — le régulateur de cadence. Logique pure (aucun asio), testable
  avec des instants synthétiques. C'est lui qui répond à ta toute première
  préoccupation : *garantir un nombre d'images par seconde malgré un temps de
  traitement variable*.
- **`Emitter`** — l'envoi UDP. Il réutilise `cam::fragment()` (Phase 0) et
  découvre `udp::socket`.

Le `main.cpp` (fourni complet) assemble les deux en un simulateur exécutable.

---

## Partie 1 — le `Pacer` *(1 h)*

```cpp
Pacer(double target_fps);
void start(clock::time_point now);
std::chrono::nanoseconds next_wait(clock::time_point now);
```

Le principe est le **pas de temps fixe avec protection anti-rafale** :

- le constructeur convertit `target_fps` en une **période** (25 fps → 40 ms) ;
- `start(now)` arme la première échéance ;
- `next_wait(now)`, appelé après chaque émission, avance l'échéance d'une
  période et renvoie le temps à attendre avant la prochaine image.

Le cœur, c'est la gestion du retard. Tu raisonnes sur des **échéances absolues**,
pas sur « attendre 40 ms depuis maintenant » — c'est ce qui empêche la dérive :

```
deadline += période
si  now <= deadline  : à l'heure  -> attendre (deadline - now)
sinon (en retard)    : compter les créneaux manqués, se resynchroniser, attendre 0
```

La subtilité qui compte : **en retard, on n'envoie PAS en rafale pour rattraper**.
On compte les battements manqués (`skipped_beats`, ta télémétrie de trames
sautées) et on repart de l'instant présent. Un ESP32 qui a mis 100 ms sur une
image de 40 ms n'essaie pas d'en envoyer trois d'un coup ensuite — il reprend le
rythme.

Quatre tests t'attendent : période correcte, attente normale à l'heure, retard →
attente nulle + battements comptés, et **absence de dérive** sur 100 itérations.

---

## Partie 2 — l'`Emitter` *(1 h — la nouveauté UDP)*

```cpp
Emitter(asio::io_context& io, const std::string& host, unsigned short port);
void send_frame(std::uint32_t frame_id, std::uint64_t timestamp_us,
                const std::uint8_t* jpeg, std::size_t size);
```

**Le constructeur.** UDP n'a pas de connexion — pas de `connect()`, pas de
handshake. Tu ouvres juste la socket et tu retiens l'adresse de destination :

```cpp
socket_.open(asio::ip::udp::v4());
dest_ = asio::ip::udp::endpoint(asio::ip::make_address(host), port);
```

C'est la grande différence avec les TP : en TCP tu établissais une connexion
(`async_connect`) ; en UDP tu envoies des datagrammes « à l'aveugle » vers une
adresse, sans savoir si quelqu'un écoute.

**`send_frame`.** Tu réutilises la Phase 0, puis tu envoies chaque datagramme :

```cpp
const auto datagrams = cam::fragment(frame_id, timestamp_us, jpeg, size);
for (const auto& dg : datagrams) {
    socket_.send_to(asio::buffer(dg), dest_);
    // + compteurs
}
```

`send_to` est **synchrone** et ne bloque pas en pratique (l'envoi UDP remet le
datagramme au noyau et rend la main). Pour un émetteur, pas besoin de coroutine :
un envoi direct suffit. Note que `asio::buffer(dg)` est sûr ici — l'appel est
complet avant de rendre la main, contrairement à l'asynchrone où il aurait fallu
prolonger la durée de vie du tampon.

Les tests envoient une trame en **UDP réel sur la loopback** (127.0.0.1), la
reçoivent, la passent au `Reassembler`, et vérifient qu'elle revient identique.
C'est ton premier aller-retour réseau complet du projet.

---

## Valider

```bash
# vérifie d'abord que l'environnement est sain (contre les corrigés)
cmake -S . -B build-sol -DSIM_USE_SOLUTION=ON
cmake --build build-sol -j && ctest --test-dir build-sol --output-on-failure

# ton build
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Puis **lance le simulateur** pour de vrai :

```bash
./build/simulator/simulator 127.0.0.1 9000 25 8000
```

Il affiche chaque seconde le nombre de trames, de datagrammes, d'octets et de
battements sautés. Laisse-le tourner — au TP-P1b, tu écriras le récepteur qui
consomme ce flux.

## Critères de réussite

- [ ] Les tests `[pacer]` et `[emitter]` passent
- [ ] Le simulateur tourne et affiche un débit stable au fps demandé
- [ ] Tu sais expliquer pourquoi on raisonne sur des échéances absolues (dérive)
- [ ] Tu sais pourquoi on ne rattrape pas un retard par une rafale
- [ ] Tu sais ce qui change entre un `udp::socket` et un `tcp::socket`
      (pas de connexion, `send_to` vers un endpoint au lieu de `async_write`)

Ensuite : **TP-P1b**, le récepteur UDP + la télémétrie temps réel.
