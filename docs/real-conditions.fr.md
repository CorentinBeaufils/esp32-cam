[English](real-conditions.en.md) · **Français**

# Bilan — ESP32-CAM en conditions réelles (Partie 2)

Après le banc **synthétique** (loopback, `benchmark.fr.md`) qui comparait les
architectures de récepteurs sans jamais montrer de perte, cette partie mesure le
système **sur air réel** : vraie carte ESP32-CAM, vraie caméra, vrai WiFi.

## Montage

- **Émetteur** : ESP32-CAM (AI-Thinker), capteur OV2640, JPEG **QVGA** (320×240),
  qualité 12, cadence visée **25 fps**. `WiFi.setSleep(false)` (voir plus bas).
  Alimentée par **power bank** → libre de se déplacer.
- **Récepteur** : PC **fixe**, collé à l'AP, récepteur headless `./receiver 9000
  --csv …`. Journalisation d'**une ligne/seconde** : `t, fps, gigue, completes,
  perdues, corrompues` (compteurs **bruts cumulés** → taux calculés en analyse).
- **Réseau** : hotspot **2,4 GHz**. Le PC restant près de l'AP (bon lien fixe),
  le seul lien qui varie est **ESP32 ↔ AP** — on fait donc bien varier un seul
  facteur en déplaçant l'ESP32.
- **4 scénarios**, ~60 s chacun, joués à la suite (environnement RF comparable).

## Résultats

![Synthèse par scénario](../bench/esp32_data/real_env/charts/reel_synthese.png)

![Dans le temps](../bench/esp32_data/real_env/charts/reel_temporel.png)

| Scénario | fps moyen | Pertes (cumul 60 s) | Gigue médiane | Corruption |
|---|---:|---:|---:|---:|
| Proche, vue directe | 25,6 | **0,0 %** (0 / 1501) | 4 ms | 0 |
| 1 cloison légère · 4 m | 17,4 | **8,7 %** (95 / 1087) | 17 ms | 0 |
| 2 cloisons légères · 7 m | 10,9 | **38,5 %** (341 / 886) | 34 ms | 0 |
| Mur béton · 4 m | 8,8 | **57,4 %** (428 / 746) | 43 ms | 0 |

## Ce que ça montre

**1. En vue directe, le lien est parfait — et identique au banc.** 0 % de perte,
25,6 fps (la cadule visée), ~4 ms de gigue. Le pipeline complet (capture → JPEG →
fragmentation UDP → réassemblage → décodage → affichage) tient le temps réel sans
accroc. Le banc synthétique n'avait donc pas menti : à lien parfait, tout passe.

**2. Les obstacles révèlent l'impairment que le banc ne montrait jamais.** Sur
loopback, pertes et gigue étaient ~0 par construction. Sur air, dès qu'un mur
s'interpose : les pertes montent, la gigue explose, le fps s'effondre. C'est
**tout l'intérêt de la Partie 2** — passer de « ça marche en labo » à « voici
comment ça se dégrade dans la vraie vie », avec des chiffres.

**3. La dégradation est monotone, et le matériau domine.** Proche 0 % → 1 cloison
8,7 % → 2 cloisons 38,5 % → **béton 57,4 %**. Un simple mur en béton fait plus de
dégâts que deux cloisons légères plus loin : à 2,4 GHz, c'est l'**atténuation par
le matériau** qui commande, pas la distance seule. Au-delà de ~40 % de perte, le
flux devient inregardable (gels prolongés visibles sur la courbe du béton : fps
qui tombe à 1, pertes qui grimpent en continu).

**4. Corruption = 0 partout — et c'est instructif.** On ne voit **jamais** de trame
corrompue acceptée : uniquement des **pertes** (datagrammes entiers manquants). La
raison est en dessous de nous : la couche liaison WiFi a son propre **FCS** et
**jette les trames radio corrompues** avant qu'elles nous parviennent. À notre
niveau applicatif, un lien dégradé se traduit donc en *disparition* de paquets, pas
en *octets faux*. Notre CRC32 applicatif reste un **filet de sécurité** justifié
(un datagramme corrompu-mais-accepté n'est pas impossible), mais en pratique l'air
ne nous en livre quasi jamais.

**5. La gigue : réglée en amont.** Premier constat en conditions réelles : ~120 ms
de gigue, dus au **power-save modem** de l'ESP32 (livraison en bouffées).
`WiFi.setSleep(false)` l'a effondrée. Toutes les mesures ci-dessus sont
power-save **désactivé** ; la gigue résiduelle suit alors la qualité du lien (pics
quand les trames stagnent).

## Limites & méthode

- **2,4 GHz, voisinage RF non contrôlé** : les valeurs absolues dépendent de
  l'environnement du jour ; ce sont les **écarts entre scénarios** qui comptent.
- `loss %` = cumul sur tout le run (les compteurs bruts permettent de recalculer
  un taux par seconde si besoin).
- **RSSI non journalisé** (il n'est sorti que sur la série de l'ESP32) → piste
  d'amélioration : le remonter dans le flux pour corréler dBm ↔ pertes.
- **Latence absolue** volontairement absente (horloges ESP/PC non synchronisées) :
  seules fps, gigue et pertes sont fiables.
- Deux fichiers de mesure (`02_…` / `03_…`) avaient leurs **noms intervertis** à la
  prise ; les libellés physiques ont été rétablis à l'analyse (données inchangées).

## Conclusion

Le projet est complet de bout en bout : protocole UDP maison → récepteur asio →
upscaling adaptatif → **dashboard Qt temps réel**, validé du banc synthétique
jusqu'au **vrai ESP32 sur WiFi**. Le dashboard affiche en direct exactement ce que
ce bilan mesure ; la caractérisation ci-dessus donne les limites réelles du lien
(un mur béton casse le flux, une vue directe tient les 25 fps sans perte).
