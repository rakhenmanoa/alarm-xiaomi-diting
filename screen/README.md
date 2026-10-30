# Écran du Xiaomi 12T Pro (diting) — tout ce qui a été fait

Ce dossier regroupe tout ce qui concerne l'écran du diting sous Arch Linux ARM : l'affichage
(dalle, DSI, DSC), le tactile, l'extinction et le rallumage de l'écran, la fluidité, et les
problèmes encore ouverts.

> **Ce sont des copies de référence, figées le 2026-10-30.** Les fichiers réellement construits
> et installés restent ceux de `pkgbuilds/` (`linux-diting`, `diting-device`, `diting-lever-reveil`).
> Après une modification, mettez d'abord ces originaux à jour, puis recopiez ici.

## Contenu

| Dossier | Contenu | Original |
|---|---|---|
| `pilote/panel-xiaomi-l12-36-02-0b.c` | Pilote DRM de la dalle, **après** application de toute la série (Linux 7.2.10 + diting-ubuntu `c68ff47` + `0901`) | créé par les patches `0009`, `0012`, `0015`, `0901` |
| `patches/diting-ubuntu/` | Patches affichage et tactile de royka1/diting-ubuntu (commit `c68ff47db947`) | `kernel/patches/` de diting-ubuntu |
| `patches/alarm-diting/` | Patches propres à ce dépôt (dalle 42 02 0a, DSC, tactile Goodix) | `pkgbuilds/linux-diting/090[0-2]` |
| `scripts/` | Réglages, services et outils installés par `diting-device` | `pkgbuilds/diting-device/` |
| `scripts/diting-lever-reveil/` | Démon « lever pour réveiller » (source, service, PKGBUILD) | `pkgbuilds/diting-lever-reveil/` |

## 1. Matériel : deux écrans, deux tactiles

Selon les lots, Xiaomi monte l'une ou l'autre de ces combinaisons. Le bootloader indique la dalle
dans la ligne de commande du noyau :

```sh
tr ' ' '\n' < /proc/cmdline | grep dsi_display
```

| | Variante A (diting-ubuntu) | Variante B (ce téléphone) |
|---|---|---|
| Dalle | `qcom,mdss_dsi_l12_36_02_0b_dsc_cmd` | `qcom,mdss_dsi_l12_42_02_0a_dsc_cmd` |
| Tactile | ST FTM5 (`st,ftm5-spi`) | Goodix GT9916 (`goodix,gt9916`, pilote mainline `goodix_berlin_spi`) |

Les deux dalles sont des AMOLED 1220×2712 avec un DDIC Novatek, en DSI **mode commande**
(4 lanes), compressées en DSC 1.1 (10 bpc, 8 bpp).

## 2. Affichage

### Patches du noyau, dans l'ordre d'application

| Patch | Origine | Rôle |
|---|---|---|
| `0009` | diting-ubuntu | Pilote de la dalle 36 02 0b. Séquence d'init générée par linux-mdss-dsi-panel-driver-generator depuis le DT du vendeur ; alimentations vddio, vddd et vci ; luminosité big-endian 0–2047 (au-delà : plage HBM). |
| `0010` | diting-ubuntu | DT : MDSS, DSI0 (vdda L10C) et sa PHY (vdds L5B), reset TLMM 0, **TE sur TLMM 86** (mdp_vsync), vddio L12C, vci L13C 3,0 V, vddd 1,2 V commuté par TLMM 66. |
| `0011` | diting-ubuntu | msm/dsi : la dalle peut fournir ses propres paramètres de contrôle de débit DSC. Sinon, toutes les zones non unies sont corrompues. |
| `0012` | diting-ubuntu | Paramètres DSC Novatek du vendeur pour la 36 02 0b (SCR, `first_line_bpg_offset` 13). |
| `0015` | diting-ubuntu | Lien DSI à 1,1 Gbit/s par lane pour toutes les fréquences (le porche avant fixe le débit en mode commande), ajout des modes 90 et 120 Hz. |
| `0901` | ce dépôt | **Dalle 42 02 0a** : séquence d'init et commandes de fréquence du DT vendeur, table DSC 1.1 standard (avant SCR), `first_line_bpg_offset` 12. Le pilote choisit la variante d'après `msm_drm.dsi_display0=` du bootloader. Ajoute les paramètres `dsc_rc` et `dsc_fl`. |
| `0902` | ce dépôt | msm/dsi : paramètre `msm.dsc_slice_per_pkt`. En amont, une seule tranche DSC part par paquet DSI ; la 42 02 0a veut les deux tranches d'une ligne dans le même paquet (vendeur : `qcom,mdss-dsc-slice-per-pkt = 2`). |

### Réglages installés
- `scripts/diting-dsc.conf` → `/usr/lib/modprobe.d/` : `options msm dsc_slice_per_pkt=2`.
  **Sans ce réglage, la moitié gauche de l'écran est brouillée** sur la 42 02 0a (testé le 2026-10-26).
- Pour une nouvelle variante de dalle, la table DSC se change sans recompiler, dans `/etc/modprobe.d/` :
  `options panel_xiaomi_l12_36_02_0b dsc_rc=… dsc_fl=…`
  (`dsc_rc` : -1 auto, 0 DSC 1.1, 1 Novatek, 2 DSC 1.1 SCR ; `dsc_fl` : 0 = valeur de la dalle).

### État
- 42 02 0a : **60 Hz vérifié**. Les commandes 90 et 120 Hz viennent du DT vendeur et n'ont pas été testées.
- Bureau : KWin Wayland, lancé par SDDM (`scripts/30-construire-rootfs.sh`, fichier `/etc/sddm.conf.d/10-diting.conf`).
- Mesa forcé sur zink pour toute la session Plasma (`MESA_LOADER_DRIVER_OVERRIDE=zink`), sauf
  plasma-camera : libcamera ne peut pas importer les images des caméras avec zink (voir le README principal).

## 3. Tactile

| Patch / fichier | Rôle |
|---|---|
| `0007` (diting-ubuntu) | Pilote minimal ST FTM5 en SPI (variante A, inutilisé ici). |
| `0008` (diting-ubuntu) | DT du tactile ST : SPI4, interruption TLMM 21, reset TLMM 20, IOVDD L2C, AVDD commuté par TLMM 71. |
| `0900` (ce dépôt) | **Remplace** le ST par le Goodix GT9916 dans le DT (même bus, mêmes broches). Sur une unité à tactile ST, retirez ce patch du PKGBUILD. |
| `scripts/diting-touch-cmd` | Outil de commande du contrôleur Goodix par le fichier sysfs `registers` du pilote mainline (trames du pilote Xiaomi `goodix_9916`) : `info`, `mode`, `game`, `rate`, `palm`, `charger`, `grip`, lecture, écriture et modification de la configuration. |
| `scripts/diting-touch.service` | Au démarrage : `grip off`, puis `mode 2`. |
| `scripts/diting-touch.sleep` | La même chose après chaque sortie de veille (le contrôleur est éteint pendant la veille). |

- **Coins du bas retenus** : le firmware Goodix retenait les touchers des coins du bas (environ
  10 % × 10 %) jusqu'au relâchement, et `&123` et Entrée du clavier répondaient en retard. Le premier
  octet du module `0x64` de la configuration commande cette retenue (trouvé par essais le 2026-10-27) :
  `diting-touch-cmd grip off`. Ce réglage est perdu à chaque mise sous tension du contrôleur, d'où le
  service et le hook de veille.
- **Réglage d'Android** : le pilote mainline n'envoie jamais la commande `0x18` (mode normal, filtre
  de bord 2). `mode 2` le fait. Le mode jeu (`0x17`) envoie les coins plus tôt, mais le mode normal a
  été préféré (2026-10-26).
- Avant toute écriture : `diting-touch-cmd config-read sauvegarde.bin`.
- **À vérifier** : le README principal indique un bus SPI à 12 MHz comme sous Android, mais le patch
  `0900` règle `spi-max-frequency = <1000000>` (1 MHz). Pour connaître la valeur réelle :
  `find /sys/firmware/devicetree -path '*touchscreen*' -name spi-max-frequency -exec xxd {} \;`
  (`000f4240` = 1 MHz, `00b71b00` = 12 MHz).

## 4. Extinction et rallumage de l'écran

### Pendant les appels — `scripts/diting-proximite-appel`
Plasma Mobile n'éteint pas l'écran contre l'oreille. Ce script écoute la proximité
(`monitor-sensor`) et, si ModemManager a un appel actif, lance `kscreen-doctor --dpms off`, puis
`on` quand l'oreille s'éloigne. Un appel qui sonne encore n'est pas actif. Service utilisateur
démarré avec la session. Testé le 2026-10-28.

### Lever pour réveiller — `scripts/diting-lever-reveil/`
Le SLPI fait tourner l'algorithme Qualcomm `sns_tilt_to_wake`. libssc l'expose (patch libssc `0001`),
et ses événements réveillent aussi le téléphone du suspend.

| Version | Date | Changement |
|---|---|---|
| 0.2.1 | 2026-10-29 | Ouverture asynchrone du capteur (les fonctions `*_sync` de libssc tournaient à 100 % d'un cœur). |
| 0.2.2 | 2026-10-30 | Au moins 3 s entre deux rallumages. En main, le geste arrivait jusqu'à 4 fois par seconde, et chaque fois `kscreen-doctor --dpms on` était lancé, même écran allumé. |
| 0.3.0 | 2026-10-30 | **Capteur ouvert seulement quand l'écran est éteint.** L'état de l'écran est suivi avec le protocole Wayland DPMS de KWin (`org_kde_kwin_dpms`). Écran éteint : ouverture du capteur et réservation de la proximité ; écran allumé : fermeture et libération. Rallumage par une requête DPMS directe (plus de `kscreen-doctor` ni de libkscreen). |

- La 0.3.0 a été testée avec un compositeur Wayland factice et un faux libssc, **pas encore sur le
  téléphone**. Point à confirmer : le SLPI accepte-t-il de rouvrir le capteur après une fermeture ? Si le
  geste ne marche qu'une fois, ajouter `Environment=DITING_LEVER_GARDER_OUVERT=1` au service (capteur
  ouvert en permanence, mais toujours rien si l'écran est déjà allumé).
- Dépend de KWin. Sans KWin, le service s'arrête en erreur, sans autre conséquence. Variante
  indépendante du bureau étudiée mais non retenue pour l'instant : état lu dans
  `/sys/class/drm/card1-DSI-1/dpms` (le noyau le met à jour mais ne notifie pas, donc relecture
  périodique), plus `PrepareForSleep` de logind, et rallumage par une touche virtuelle uinput (`KEY_WAKEUP`).

## 5. Fluidité — `scripts/diting-perf.service`
Le GPU (`simple_ondemand`) et les petits cœurs montaient trop tard en fréquence, et l'interface
saccadait. Minima testés le 2026-10-26 : GPU 439 MHz, policy0 1056 MHz, policy4 1113,6 MHz,
`rate_limit_us` 500 de schedutil. À désactiver si la batterie compte plus :
`systemctl disable diting-perf`.

## 6. Journal du 29 oct. 2026 : ce qui a été analysé

Démarrage de 14:17 à 17:05, terminé par un **gel sans arrêt propre**.

| Message | Verdict |
|---|---|
| `adreno 3d00000.gpu: supply vdd / vddcx not found, using dummy regulator` (et PCIe, cs35l41, DSI `refgen`, UFS) | **Normal.** Alimentations historiques que le pilote cherche par défaut ; sur ce SoC, le GPU est alimenté par le GMU et les power domains du `gpucc`. Le GPU fonctionne (firmware `a730_sqe.fw` et GMU v4.0.7 chargés, aucune faute ni hang en 3 h). |
| `sm8450-gpucc … Timed out. Forcing sync_state()` | **Normal**, effet de `fw_devlink.sync_state=timeout` sur la ligne de commande. |
| `disp_cc_mdss_mdp_clk_src: rcg didn't update its configuration` + WARNING `clk-rcg2.c:136` | Une fois, au chargement de `dispcc_sm8450` : le noyau gare l'horloge MDP sur XO alors que le splash du bootloader l'utilise encore. Sans conséquence observée (aucune erreur DSI, vblank ou underrun ensuite). Correction possible sur le modèle du patch `0055` (caméras) ; faible priorité. |
| `kwin_wayland: Atomic commit failed: EBUSY` (18 fois) | Côté KWin, le noyau ne signale rien. **Coïncide avec les rafales de gestes** de `diting-lever-reveil` (16:56:33–:50 ; 17:04:36–17:05:18, jusqu'à 4 gestes par seconde). Le dernier EBUSY (17:05:53) est la dernière ligne avant le gel. Lien avec le gel non prouvé. Corrigé côté démon (0.2.2 puis 0.3.0). |

Pour la cause du gel, à faire sur le téléphone : `ls /sys/fs/pstore/` (ramoops est activé) et
`tr ' ' '\n' < /proc/cmdline | grep bootinfo` (raison d'arrêt donnée par le bootloader).

Hors écran, mais vu dans le même journal : interruption « handover » du SLPI à environ 10 par seconde
(64 % du journal). Corrigée par le patch noyau `pkgbuilds/linux-diting/0910` (backport Linux 7.3,
`linux-diting` pkgrel 13).

## 7. Pistes ouvertes
- Tester la 0.3.0 de `diting-lever-reveil` sur le téléphone (réouverture du capteur).
- Vérifier le 90 et le 120 Hz sur la 42 02 0a.
- Lever le doute sur la fréquence SPI du tactile (1 MHz dans le DT, 12 MHz selon le README).
- Lire pstore après le prochain gel.
- Éventuellement : garer proprement l'horloge MDP au démarrage (WARNING `disp_cc`).
