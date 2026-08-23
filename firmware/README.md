# Phase 2 — Firmware ESP32-CAM

Le « vrai ESP32 » : il capture des images JPEG et les envoie en UDP au récepteur
PC, avec **le même protocole** que le simulateur (`common/cam/protocol.hpp`). Ton
récepteur (`viewer` / `receiver`) n'a **rien à changer** — il ne sait pas s'il
parle à la carte ou au simulateur.

> ⚠️ **Code fourni, non testé par l'assistant.** Le firmware ne se compile pas
> sur PC (framework Arduino, toolchain Xtensa). Il s'appuie sur les patterns
> officiels de l'ESP32-CAM AI-Thinker, mais tu seras le premier à le flasher.
> Attends-toi à ajuster une ou deux choses (brochage si autre carte, réglages).

---

## Matériel

- Une **ESP32-CAM AI-Thinker** (OV2640) — la plus répandue.
- Un **adaptateur USB-série** (FTDI/CP2102) en 3,3 V pour flasher : l'ESP32-CAM
  n'a pas d'USB.
- Alimentation **5 V solide** : la caméra + Wi-Fi tirent des pics de courant ;
  une alim faible provoque des redémarrages ou des échecs d'init.

## Câblage pour flasher (adaptateur USB-série ↔ ESP32-CAM)

| USB-série | ESP32-CAM |
|---|---|
| 5V | 5V |
| GND | GND |
| TX | U0R (RX) |
| RX | U0T (TX) |
| — | **GPIO0 ↔ GND** (uniquement pour entrer en mode flash) |

Séquence : relie **GPIO0 à GND**, appuie sur RESET (ou coupe/rallume), lance le
flash. Une fois flashé, **débranche GPIO0 de GND** et RESET pour exécuter.

## Configuration

```bash
cp src/config.example.h src/config.h
```

Édite `src/config.h` :
- `WIFI_SSID` / `WIFI_PASSWORD` — ton réseau (2,4 GHz : l'ESP32 ne fait pas de 5 GHz) ;
- `PC_IP` — l'IP de ton PC sur le réseau local (ex. `192.168.1.42`), **pas**
  `127.0.0.1` : l'ESP32 doit joindre ton PC par le réseau ;
- `PC_PORT` — le port de ton récepteur (défaut 9000) ;
- `FRAME_SIZE`, `JPEG_QUALITY`, `TARGET_FPS` — commence petit (QVGA, qualité 12,
  25 fps).

`config.h` est gitignoré : tes identifiants ne partent pas dans git.

## Compiler et flasher

Avec PlatformIO (dans WSL ou l'extension VS Code) :

```bash
cd firmware
pio run                 # compile
pio run -t upload       # flash (GPIO0 à GND, voir ci-dessus)
pio device monitor      # logs série (115200 bauds)
```

Au boot, le moniteur affiche l'IP obtenue et la cible d'envoi.

## Lancer la chaîne complète

```bash
# Sur le PC : le recepteur, sur le port choisi
./build/receiver/receiver 9000        # ou ./build/display/viewer 9000

# L'ESP32, une fois flashe et alimente, emet tout seul.
```

Ouvre le **pare-feu** du PC pour ce port UDP, et vérifie que PC et ESP32 sont sur
le **même sous-réseau**. Le récepteur doit afficher un fps proche de `TARGET_FPS`
et, avec le `viewer`, **enfin de vraies images** (là où le simulateur n'envoyait
que du bruit synthétique).

---

## Deux points d'honnêteté technique

**La latence absolue n'aura pas de sens.** Le champ `timestamp_us` est rempli
avec `micros()` de l'ESP32 (son uptime), une horloge **différente** de celle du
PC. Sans synchronisation d'horloge (NTP/PTP), la « latence » calculée côté PC est
un décalage arbitraire, pas un vrai délai. Ce qui reste **valable et utile** : le
**fps**, la **gigue** (variation, indépendante du décalage), et les **pertes /
corruptions**. Pour une vraie latence, il faudrait synchroniser les horloges —
c'est un raffinement possible plus tard.

**Le protocole est copié dans `src/`.** Pour rester simple et robuste,
`src/cam/protocol.hpp` et `src/protocol.cpp` sont des **copies** de `common/`.
PlatformIO compile tout `src/` sans configuration spéciale. Inconvénient : deux
copies à garder synchronisées — mais le protocole est figé, donc le risque est
faible. Si tu modifies le protocole côté PC, recopie les deux fichiers :

```bash
cp ../common/include/cam/protocol.hpp src/cam/protocol.hpp
cp ../common/src/protocol.cpp         src/protocol.cpp
```

(Un lien symbolique évite la duplication si ton système le permet.)

---

## Dépannage

| Symptôme | Piste |
|---|---|
| `Echec init camera : 0x...` au boot | brochage (autre carte ?), ou alim 5 V trop faible |
| Redémarrages en boucle (brownout) | alimentation insuffisante : meilleure alim / meilleurs fils |
| Wi-Fi ne se connecte pas | réseau 2,4 GHz ? SSID/mot de passe ? portée ? |
| Le récepteur ne reçoit rien | `PC_IP` correcte ? même sous-réseau ? pare-feu UDP ouvert ? |
| fps très bas | baisse la résolution / augmente `JPEG_QUALITY` (valeur plus grande = plus léger) |
| Beaucoup de pertes au récepteur | Wi-Fi saturé/faible ; rapproche la carte du point d'accès |
