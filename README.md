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

> Port expérimental, construit à partir d'un projet testé sur un seul exemplaire (22081212UG, EEA,
> HyperOS OS3.0.5.0). Les paquets Arch n'ont pas encore été essayés sur un vrai téléphone.

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
| Capteurs | `hexagonrpc-diting`, `libssc`, `iio-sensor-proxy-diting` (rotation, proximité, lumière, boussole) |
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
- le patch phoc (choix de fréquence d'affichage) est inutile, KWin gère les modes lui-même.

## Licences

- Scripts et PKGBUILD : GPL-2.0-or-later, comme diting-ubuntu.
- Le paquet firmware et `rootfs.img` contiennent du firmware Xiaomi/Qualcomm signé pour votre
  téléphone : **ne les redistribuez pas**.
