# alarm-diting — Arch Linux ARM + Plasma Mobile pour le Xiaomi 12T Pro

Arch Linux ARM (aarch64) avec **Plasma Mobile** pour le Xiaomi 12T Pro / Redmi K50 Ultra
(`diting`, Snapdragon 8+ Gen 1, SM8475).

Tout le travail matériel vient de [royka1/diting-ubuntu](https://github.com/royka1/diting-ubuntu) :
noyau mainline 7.2.10 et ses 96 patches, DTB, extraction du firmware, audio, appels, modem, capteurs.
Ce dépôt le réutilise tel quel, épinglé au commit `c68ff47`, et remplace la couche Ubuntu par :

- des **PKGBUILD** ;
- **mkinitcpio** ;
- un hook pacman ;
- un rootfs ALARM.

L'image produite contient **déjà le firmware de votre téléphone**, extrait avant d'effacer Android.
Il n'y a donc pas d'extraction au premier démarrage.

> Port expérimental. **Testé le 2026-10-26 sur un 12T Pro avec dalle « 42 02 0a » et tactile
> Goodix** (variante absente de diting-ubuntu) : démarrage, écran 60 Hz, tactile, GPU, Wi-Fi,
> modem 4G (Airtel), DSP (audio, capteurs, calcul), SSH par USB et Wi-Fi, Plasma Mobile.
> Les scripts tout-en-un (étapes 2 à 4 du conteneur) n'ont pas été exécutés de bout en bout :
> la méthode réellement suivie est décrite dans « Méthode testée » ci-dessous.

## Variantes matérielles du diting

Xiaomi monte deux écrans et deux contrôleurs tactiles différents selon les lots. Le bootloader
indique la dalle dans la ligne de commande du noyau :

```sh
tr ' ' '\n' < /proc/cmdline | grep dsi_display
```

| Élément | Variante A (diting-ubuntu) | Variante B (ce dépôt) |
|---|---|---|
| Dalle | `mdss_dsi_l12_36_02_0b` | `mdss_dsi_l12_42_02_0a` |
| Tactile | ST FTM5 (`st,ftm5-spi`) | Goodix GT9916 (`goodix,gt9916`, pilote `goodix_berlin_spi`) |

- Le pilote de dalle (patch `0901`) gère les deux dalles et choisit seul d'après le bootloader.
  Pour la 42 02 0a : table DSC Novatek, `first_line_bpg_offset` 12, et **deux tranches DSC par
  paquet DSI** (`msm.dsc_slice_per_pkt=2`, patch `0902`, réglé par `diting-device`). Sans ce
  dernier réglage, la moitié gauche de l'écran est brouillée.
- Le patch `0900` **remplace** le tactile ST par le Goodix dans le DTB. Sur une unité à tactile ST,
  retirez ce patch du PKGBUILD de `linux-diting`.
- Réglages DSC modifiables sans recompiler, pour une nouvelle variante :
  `options panel_xiaomi_l12_36_02_0b dsc_rc=… dsc_fl=…` dans `/etc/modprobe.d/` (voir le patch `0901`).

## Méthode testée

Plus rapide que l'émulation complète : une image minimale construite sur le PC, puis le reste
compilé **sur le téléphone**, par SSH.

1. Collecte Android (étape 1 ci-dessous). Le firmware est assemblé sur le PC avec
   `tools/firmware/diting-build-firmware` de diting-ubuntu, puis reconditionné par
   `pkgbuilds/firmware-xiaomi-diting`.
2. Noyau compilé en croisé sur le PC :
   `makepkg -fC --config files/makepkg-cross-aarch64.conf --nodeps --skippgpcheck`.
3. `diting-device` construit sur le PC (`makepkg -d`, paquet `any`).
4. Rootfs ALARM minimal en chroot qemu : `linux-diting`, `firmware-xiaomi-diting`,
   `linux-firmware-qcom` et `-atheros`, `android-tools` (mkbootimg), NetworkManager, openssh,
   `diting-device` installé avec `-Sdd`. Puis `mkinitcpio -P`, qui produit `/boot/diting/*.img`,
   et une image ext4 `diting-root` créée avec `mkfs.ext4 -d`.
5. Flash de `boot_a`, `vendor_boot_a` et `userdata`, effacement de `dtbo_a`.
6. Sur le téléphone, par SSH (USB : `172.16.42.1`) : `makepkg -si` de `tqftpserv`,
   `hexagonrpc-diting`, `fastrpc-diting`, `libssc`, `iio-sensor-proxy-diting`,
   `libva-v4l2-stateful-diting` et `libcamera-diting`. Ensuite, Plasma Mobile depuis ALARM,
   puis `plasma-dialer` et `mobile-config-firefox` depuis l'AUR (`yay-bin`).

### Pièges rencontrés

- **pacman 7 sous qemu-user ou sans Landlock** : « restricting filesystem access failed ». Ajoutez
  `DisableSandbox` dans `[options]` de `pacman.conf`. Le noyau `-5` active Landlock.
- **`diting-boot-successful` en échec** (« bad partition entry CRC ») : sur cette unité, la table
  GPT de secours de `sde` n'avait pas été mise à jour correctement par le bootloader. Il a fallu la
  reconstruire depuis la table principale. **Sans ce service, le slot devient inutilisable après
  7 démarrages.**
- **`linux-firmware` d'ALARM sans `linux-firmware-qcom`** : il faut l'installer, sinon
  `a730_sqe.fw` manque et le GPU ne démarre pas.
- **Audio** : le profil UCM d'origine ne passe pas avec PipeWire 1.6 (capture non routée → profil HiFi rejeté) et mélange gauche/droite sur les deux amplis ; corrigé par `pkgbuilds/diting-device/ucm-diting.py`.
- **Micros** : bruit blanc même ADC coupés, car les ports 1 et 2 du codec WCD938x (ADC1 et ADC4) partageaient le port maître SoundWire 1 et émettaient dans la même case. Le patch noyau `0903` envoie le port 2 du codec sur le port maître 2, sur la lane 1 au décalage 1 (la lane 2 ne donne rien sur cette carte). L'ADC4 arrive alors sur `SWR_MIC5`, qu'utilise l'UCM, et la route DAPM suit (`TX SWR_INPUT5`), sinon l'ADC4 et MIC BIAS4 restent éteints. Test sans recompiler, sur `/boot/dtbs/qcom/sm8475-xiaomi-diting.dtb` : `fdtput -t u <dtb> /soc@0/soundwire@33b0000/codec@0,3 qcom,tx-port-mapping 1 2 3 4`, `fdtput -t bu <dtb> /soc@0/soundwire@33b0000 qcom,ports-lane-control 1 1 0 0`, `fdtput -t bu <dtb> /soc@0/soundwire@33b0000 qcom,ports-offset1 0 1 1 1`, `fdtput -t s <dtb> /sound audio-routing "AMIC1" "MIC BIAS1" "AMIC5" "MIC BIAS4" "TX SWR_INPUT0" "ADC1_OUTPUT" "TX SWR_INPUT5" "ADC4_OUTPUT"`, puis `mkinitcpio -P` et `diting-install-bootimg`.
- **Modem** : ModemManager 1.24.2 plante toutes les 20 à 30 s (SIGSEGV dans `mm-netlink.c`), la 4G coupe ; recompiler avec le correctif officiel, voir `pkgbuilds/modemmanager-fix/`.
- **Wi-Fi** : l'économie d'énergie fait couper le Wi-Fi ; diting-device la désactive (`60-diting-wifi-powersave.conf` pour NetworkManager).
- **Fluidité** : le GPU et les petits cœurs montaient trop tard en fréquence (interface saccadée) ; `diting-perf.service` relève les minima (GPU 439 MHz, cœurs 1056 et 1113,6 MHz). À désactiver si la batterie compte plus que la fluidité.
- **Tactile, coins du bas** : le firmware Goodix retient les touchers des coins du bas (environ 10 % de large, 10 % de haut) jusqu'au relâchement ; `&123` et Entrée du clavier répondaient en retard. Le premier octet du module 0x64 de la configuration commande cette retenue (trouvé par essais avec `libinput debug-events`) : `diting-touch-cmd grip off` le coupe. Il n'est pas conservé à la mise sous tension du contrôleur : `diting-touch.service` et le hook de veille le réappliquent, puis envoient le réglage d'Android (`mode 2`, commande 0x18, trame du pilote Xiaomi `goodix_9916`). Sauvegarder d'abord la configuration avec `diting-touch-cmd config-read`. Bus SPI du tactile à 12 MHz comme sous Android.
- **Vibreur** : aucun pilote mainline pour le bloc haptique haute tension du PM8350B ; le patch noyau `0904` ajoute un pilote minimal (vibration constante, ff-memless, séquence du pilote `qcom-hv-haptics`, Vmax 2000 mV) et le nœud DT. Plasma Mobile vibre par feedbackd (`org.sigxcpu.Feedback.Haptic`) ; diting-device en dépend et installe la règle udev `90-diting-vibreur.rules` (`FEEDBACKD_TYPE=vibra`). Test : `diting-vibrate 300 100` ou `fbcli -e phone-incoming-call`. Pour tester sans recompiler le paquet : `extra/pm8350b-haptics/`.
- **PolicyKit** : sans règle, appels et USSD sont refusés à plasma-dialer ; diting-device installe `50-diting-modemmanager.rules` (groupe wheel).
- **libssc** demande `gobject-introspection`, et **libcamera** a besoin de `-Dwerror=false` avec
  GCC 16.
- Après un échec de `makepkg`, relancez avec **`-C`** : les fichiers créés par les patches restent
  sinon dans `src/`.

## Étapes

| # | Script | Où | Durée |
|---|---|---|---|
| 1 | `scripts/01-collecter-android.sh [--fichiers] [--userdata]` | PC, téléphone sous Android + root temporaire | 10 min (+ selon les options) |
| 2 | `scripts/10-construire-firmware.sh` | conteneur | 2 min |
| 3 | `scripts/20-construire-paquets.sh` | conteneur | 1 à 3 h (émulation) |
| 4 | `scripts/30-construire-rootfs.sh` | conteneur | 30 à 60 min |
| 5 | `out/flasher.sh` | PC, téléphone en fastboot | 10 min |

Les étapes 2 à 4 se lancent avec `scripts/build-in-container.sh <script>`, ou toutes ensemble avec
`scripts/build-in-container.sh tout`.

### Prérequis sur le PC (Linux x86_64)

- `adb` et `fastboot` (android-tools / platform-tools), `git`, `zstd` ;
- **Docker ou Podman** ;
- l'émulation aarch64 :
  - Arch : `sudo pacman -S qemu-user-static qemu-user-static-binfmt`
  - Debian/Ubuntu : `sudo apt install qemu-user-static binfmt-support`
- environ 60 Go libres, dont la sauvegarde `userdata` si vous la faites.

### 1. Root temporaire et collecte (à faire AVANT tout flash)

Rien n'est écrit sur le téléphone : `fastboot boot` démarre une image une seule fois, sans la flasher.

1. Notez votre version HyperOS (*Paramètres → À propos du téléphone*).
2. Téléchargez la **ROM fastboot officielle** de cette version exacte et extrayez son `boot.img`.
3. Installez l'appli **Magisk** sur le téléphone, puis patchez ce `boot.img` avec. Vous obtenez
   `magisk_patched-….img`.
4. Redémarrez en fastboot (power + volume bas), puis :
   ```sh
   fastboot boot magisk_patched-XXXX.img
   ```
5. Android démarre avec le root. Activez le débogage USB.
6. Lancez :
   ```sh
   scripts/01-collecter-android.sh            # firmwares + toutes les partitions sauf userdata/super
   scripts/01-collecter-android.sh --fichiers # + vos photos, documents… (taille ≈ espace utilisé)
   scripts/01-collecter-android.sh --userdata # + copie brute de userdata (retour à Android « tel quel »)
   ```
7. Acceptez la demande root « Shell » dans Magisk.
8. Redémarrez normalement : le téléphone revient exactement à son état d'origine.

Le dossier `device-data/` contient alors :

- les sauvegardes de `boot`, `vendor_boot`, `dtbo`, `vbmeta`, `persist`, `modemst1/2`, `fsg`, `fsc`,
  `metadata` et de la chaîne de démarrage ;
- tous les blobs propriétaires (modem, ADSP, CDSP, SLPI, GPU, Wi-Fi/BT, ampli, ACDB, capteurs) ;
- les modules caméra.

**Ce dossier est privé** : il contient l'IMEI, les adresses MAC et les calibrations. Gardez-en une
copie hors du PC.

### 2 à 4. Construction

Mettez éventuellement votre clé SSH publique dans `files/authorized_keys`, puis vérifiez `config.sh` :
utilisateur, code PIN, fuseau, langue, option libcamera. Ensuite :

```sh
scripts/build-in-container.sh tout
```

Le résultat se trouve dans `out/` : `boot.img`, `vendor_boot.img`, `rootfs.img`, `flasher.sh` et
`SHA256SUMS`.

### 5. Installation

Téléphone en fastboot :

```sh
out/flasher.sh
```

Ce script écrit `boot_<slot>` et `vendor_boot_<slot>`, efface `dtbo_<slot>` et **écrase `userdata`**.

Au premier démarrage, le système de fichiers s'agrandit et Plasma Mobile s'ouvre automatiquement.
Le code PIN par défaut est **147147** : changez-le avec `passwd`.

En USB, le téléphone est une carte réseau : `ssh alarm@172.16.42.1` (clé uniquement).

### Retour à Android

```sh
scripts/restaurer-android.sh            # Android d'origine, données vierges
scripts/restaurer-android.sh --donnees  # + userdata/metadata d'origine (si sauvegardés)
```

## Ce que contient l'image

| Domaine | Composants |
|---|---|
| Noyau | `linux-diting` 7.2.10 + patches diting-ubuntu, Image non compressée, DTB avec ids ABL |
| Démarrage | mkinitcpio minimal ; hook `/usr/lib/initcpio/post/diting-bootimg` → `/boot/diting/{boot,vendor_boot}.img` |
| Firmware | `firmware-xiaomi-diting` (le vôtre) + `linux-firmware-qcom`/`-atheros` |
| GPU | Mesa freedreno / Turnip |
| Audio, appels | topologie AudioReach, UCM, protection des haut-parleurs, `diting-voice-call`, `diting-callaudio` (remplace le service de callaudiod pour plasma-dialer), `fastrpc-diting` |
| Modem | rmtfs (lecture seule), tqftpserv, qrtr, `diting-modem-setup`, ModemManager |
| Capteurs | `hexagonrpc-diting`, `libssc`, `iio-sensor-proxy-diting` (rotation, proximité, lumière, boussole) ; gyroscope lisible avec `ssccli --sensor gyroscope` ; `diting-proximite-appel` éteint l'écran contre l'oreille pendant un appel (Plasma Mobile ne le fait pas) |
| Vidéo | `libva-v4l2-stateful-diting` (H.264/HEVC/VP9 matériels, VA-API) |
| Caméra | `libcamera-diting` 0.7.0 + patches (si ALARM est en 0.7.x) |
| Bureau | Plasma Mobile (SDDM Wayland, connexion automatique), plasma-dialer, Spacebar, Angelfish… |

## Mises à jour

- `sudo pacman -Syu` met à jour Arch.
- Les paquets `*-diting` restent dans le dépôt local `/var/lib/diting-repo`.
- Après une mise à jour du noyau ou de l'initramfs : `sudo mkinitcpio -P`, puis
  `sudo diting-install-bootimg`, qui écrit les images sur le slot actif après confirmation.
- Si ALARM passe à libcamera 0.8+, pacman signalera un conflit avec `libcamera-diting`.
  Gardez-le, ou acceptez la version d'ALARM : la caméra arrière ne fonctionnera alors plus.

## Limites connues

Les mêmes que diting-ubuntu :

- pas de VoLTE (les appels passent en 3G) ;
- ne fonctionnent pas : caméras avant et macro, lecteur d'empreintes, NFC, IR, vibration, encodeur vidéo ;
- la caméra principale ne marche pas si votre unité a le capteur Samsung HM6 au lieu du HP1.

Spécifique à cette version Arch :

- le correctif ModemManager (crash après une sortie de veille) n'est pas inclus : systemd relance le
  service ;
- le patch phoc (choix de fréquence d'affichage) est inutile, KWin gère les modes lui-même ;
- dalle 42 02 0a : seul le 60 Hz a été vérifié ; les commandes 90 et 120 Hz viennent du DT
  vendeur mais n'ont pas été testées.

## Licences

- Scripts et PKGBUILD : GPL-2.0-or-later, comme diting-ubuntu.
- Le paquet firmware et `rootfs.img` contiennent du firmware Xiaomi/Qualcomm signé pour votre
  téléphone : **ne les redistribuez pas**.
