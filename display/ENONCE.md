# TP-P1c — Affichage OpenCV + handoff thread-safe

**Durée estimée :** 1 h 30 — **Prérequis :** TP0-TP9, Phase 0, TP-P1a/b — **Fichier à compléter :** `src/latest_frame.cpp`

> **Format allégé.** Tu maîtrises l'asynchrone, la concurrence et la télémétrie ;
> je ne détaille plus le connu. Ici : le **contrat** (`include/disp/latest_frame.hpp`)
> et les **tests** disent quoi faire, à toi le comment. Le corrigé est là si tu
> bloques, mais ne le regarde qu'après avoir essayé.

---

## Ce qui est nouveau, ce qui ne l'est pas

**Nouveau — OpenCV.** Le décodage JPEG (`cv::imdecode`) et l'affichage
(`cv::imshow` / `cv::waitKey`). C'est une bibliothèque que tu n'as jamais
touchée : je te fournis `main.cpp` **complet et commenté**. Lis-le, ne le
réécris pas — deviner une API inconnue n'apprend rien.

**Ton exercice — `LatestFrame`.** LE concept de ce TP : le point de passage
thread-safe entre le **thread réseau** (qui produit des trames) et le **thread
d'affichage** (qui les consomme à son rythme, imposé par OpenCV sur le thread
principal). C'est la concrétisation de ton intuition read-on-write du tout
début.

## Le contrat

```cpp
void store(std::shared_ptr<const cam::Frame> frame);   // thread réseau
std::shared_ptr<const cam::Frame> take();              // thread affichage
std::uint64_t dropped() const;                          // télémétrie
```

Politique **« le plus récent gagne »** : si une trame attend déjà quand une
nouvelle arrive, on **jette l'ancienne** (périmée) et on la compte. En temps
réel, afficher une image en retard n'a aucun intérêt.

Deux threads appellent `store` et `take` **en même temps** : c'est tout l'enjeu.
Trois lignes de logique, une seule difficulté — la protection. Tu as tout ce
qu'il faut depuis le TP6.

## Valider

```bash
# la lib et ses tests ne dependent PAS d'OpenCV
cmake -S . -B build && cmake --build build -j && ctest --test-dir build -R display --output-on-failure

# l'outil roi de ce TP : ThreadSanitizer, sur le handoff concurrent
cmake -S . -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"
cmake --build build-tsan -j && ctest --test-dir build-tsan -R display --output-on-failure
```

Puis, **le flux vidéo complet, en vrai** (installe OpenCV si ce n'est pas fait :
`sudo apt install libopencv-dev`) :

```bash
# terminal 1 — le visualiseur
./build/display/viewer 9000
# terminal 2 — le simulateur
./build/simulator/simulator 127.0.0.1 9000 25 8000
```

Une fenêtre s'ouvre. Comme le simulateur émet des octets synthétiques (pas un
vrai JPEG), OpenCV n'affichera pas encore d'image cohérente — c'est **normal à ce
stade**. Le pipeline, lui, tourne : réception → réassemblage → handoff →
tentative de décodage. Le vrai JPEG viendra quand tu brancheras une source réelle
(webcam ou firmware ESP32). Tu peux le vérifier tout de suite en faisant émettre
au simulateur une vraie image encodée — mais ça, c'est le pont vers la Phase 2.

## Critères de réussite

- [ ] Les tests `[display]` passent, **y compris sous ThreadSanitizer**
- [ ] Tu sais pourquoi `store`/`take` doivent être protégés (deux threads)
- [ ] Tu sais pourquoi l'affichage est sur le thread principal (contrainte OpenCV)
      et le réseau sur un thread de fond (modèle du TP6)
- [ ] Tu sais ce que compte `dropped()` et ce que ça révèle (l'écran ne suit pas
      le réseau)

**Fin de la Phase 1.** Tu as la chaîne complète : émission cadencée → réseau UDP
→ réassemblage → télémétrie → affichage, chaque maillon testé. Reste le firmware
ESP32 réel (Phase 2), l'upscaling (Phase 3) et le comparatif (Phase 4).
