# Installation — alarm-diting

Construction de l'image et flash du Xiaomi 12T Pro (`diting`). Les chemins (`scripts/`,
`pkgbuilds/`, `files/`, `out/`…) sont relatifs à la racine du dépôt.
Retour au [README principal](../README.md).

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
   `hexagonrpc-diting`, `fastrpc-diting`, `libssc`, `iio-sensor-proxy-diting`, `diting-lever-reveil`,
   `libva-v4l2-stateful-diting` et `libcamera-diting`. Ensuite, Plasma Mobile depuis ALARM,
   puis `plasma-dialer` et `mobile-config-firefox` depuis l'AUR (`yay-bin`).

### Pièges rencontrés

La liste des problèmes rencontrés et de leurs contournements (pacman/qemu, GPT de secours,
firmware, audio, micros, modem, Wi-Fi, tactile, vibreur…) est dans
**[pieges.md](pieges.md)**.

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
