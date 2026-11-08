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
> modem 4G (Airtel : appels, SMS, USSD et données mobiles avec les correctifs ModemManager), DSP (audio, capteurs, calcul), SSH par USB et Wi-Fi, Plasma Mobile.
> Les scripts tout-en-un (étapes 2 à 4 du conteneur) n'ont pas été exécutés de bout en bout :
> la méthode réellement suivie est décrite dans « Méthode testée » ci-dessous.

## État du matériel

Le tableau complet (système, affichage, connectivité, téléphonie, audio, capteurs,
caméras, vidéo…) est dans **[scoreboard.md](scoreboard.md)**.

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

## Installation

La méthode testée, les étapes de construction (PC + téléphone), le flash et le retour à
Android sont décrits dans **[docs/installation.md](docs/installation.md)**.

## Ce que contient l'image

Le détail des composants embarqués (noyau, firmware, GPU, audio, modem, capteurs, vidéo,
caméra, bureau) est dans **[docs/contenu.md](docs/contenu.md)**.

## Mises à jour

- `sudo pacman -Syu` met à jour Arch.
- Les paquets `*-diting` restent dans le dépôt local `/var/lib/diting-repo`.
- Après une mise à jour du noyau ou de l'initramfs : `sudo mkinitcpio -P`, puis
  `sudo diting-install-bootimg`, qui écrit les images sur le slot actif après confirmation.
- Si ALARM passe à libcamera 0.8+, pacman signalera un conflit avec `libcamera-diting`.
  Gardez-le, ou acceptez la version d'ALARM : la caméra arrière ne fonctionnera alors plus.
- Tout ce qui concerne l'écran (dalle, DSC, tactile, extinction et rallumage, analyse des journaux) : [screen/README.md](screen/README.md).
- Tout ce qui concerne l'audio (haut-parleurs, écouteur, micros, appels, UCM) : [audio/README.md](audio/README.md).
- Tout ce qui concerne le modem (MPSS, IPA, rmtfs, SIM, ModemManager, données mobiles, appels et SMS) : [modem/README.md](modem/README.md).
- Tout ce qui concerne l'horloge (RTC du PMIC en lecture seule, heure gardée sans réseau par `diting-rtc`) : [rtc/README.md](rtc/README.md).
- Pour tester un pilote ou le device tree sans reconstruire tout le noyau : compiler seulement les
  modules et le DTB sur le téléphone, voir [docs/modules-sur-le-telephone.md](docs/modules-sur-le-telephone.md).

## Limites connues

Les mêmes que diting-ubuntu :

- pas de VoLTE (les appels passent en 3G) ;
- ne fonctionnent pas : lecteur d'empreintes, NFC, IR, encodeur vidéo (le vibreur, lui, marche ici avec le patch `0904`) ;
- la caméra principale ne marche pas si votre unité a le capteur Samsung HM6 au lieu du HP1.

Spécifique à cette version Arch :

- si Mesa est forcé sur zink pour toute la session (`MESA_LOADER_DRIVER_OVERRIDE=zink`), le
  traitement d'image de libcamera ne peut pas importer les images des caméras (« zink: couldn't
  allocate memory ») et plasma-camera s'arrête : lancer plasma-camera sans cette variable
  (lanceur copié dans `~/.local/share/applications/` avec `Exec=env -u MESA_LOADER_DRIVER_OVERRIDE …`) ;
- ne pas lancer `kbuildsycoca6` depuis SSH : sans l'environnement de la session, il produit un cache
  vide et toutes les icônes disparaissent (le reconstruire avec les variables de `plasmashell`) ;

- les correctifs ModemManager (plantage netlink, abonnement des données mobiles) ne sont pas
  empaquetés : on recompile le paquet d'Arch avec les patches `0001` et `0002`, voir
  `pkgbuilds/modemmanager-fix/`, puis `IgnorePkg = modemmanager libmm-glib` ;
- le patch phoc (choix de fréquence d'affichage) est inutile, KWin gère les modes lui-même ;
- dalle 42 02 0a : 60, 90 et 120 Hz vérifiés sur le téléphone.

## Licences

- Scripts et PKGBUILD : GPL-2.0-or-later, comme diting-ubuntu.
- Le paquet firmware et `rootfs.img` contiennent du firmware Xiaomi/Qualcomm signé pour votre
  téléphone : **ne les redistribuez pas**.
