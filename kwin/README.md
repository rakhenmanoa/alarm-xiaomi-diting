# KWin — modes basse résolution agrandis par le DPU

Patch pour **KWin 6.8.2** qui ajoute à l'écran interne des modes « réduits »
(par ex. 610×1356 et 407×904), à 60, 90 et 120 Hz.

Dans ces modes, la dalle reçoit toujours ses timings natifs (1220×2712) ; KWin, les applis et
le bureau travaillent en basse résolution, et la couche finale est agrandie **en matériel** par
le contrôleur d'affichage (DPU) via SRC/CRTC des plans DRM — comme le fait Android.

Objectif : la fluidité de la session imbriquée (sway + KWin en 407×904) sans ses
inconvénients (rotation, extinction de l'écran, précision du tactile, Firefox).

> **État : expérimental, non encore testé sur le téléphone.** Inconnue principale : le pilote
> `msm`/DPU accepte-t-il d'agrandir la couche principale d'un facteur ×2 / ×3 ? S'il refuse,
> KWin rejette simplement le mode au test atomique, rien ne casse.

## Fonctionnement

| Fichier | Modification |
|---|---|
| `drm_connector.{h,cpp}` | `DrmConnectorMode` « mis à l'échelle » : taille réduite, timings natifs ; génération des modes selon `KWIN_DRM_SCALED_MODES` (écrans internes seulement) |
| `drm_pipeline.cpp` | `prepareAtomicPlane` : la destination (`CRTC_*`) du plan est agrandie de la taille du mode vers la taille native |

Sans la variable `KWIN_DRM_SCALED_MODES`, KWin se comporte exactement comme l'original.

## Construire

```sh
sudo pacman -S --needed base-devel git pacman-contrib
git clone https://gitlab.archlinux.org/archlinux/packaging/packages/kwin.git ~/build/kwin
cd ~/build/kwin
git checkout 6.8.2-1
cp <ce dépôt>/kwin/0001-drm-scaled-modes.patch .
```

Dans le `PKGBUILD` : ajouter `0001-drm-scaled-modes.patch` à `source=()`, et dans `prepare()` :

```sh
patch -d $pkgname-$pkgver -p1 < 0001-drm-scaled-modes.patch
```

Puis :

```sh
updpkgsums
makepkg -s --nocheck --skippgpcheck
sudo pacman -U kwin-6.8.2-1-*.pkg.tar.zst
```

Ajouter `IgnorePkg = kwin` dans `/etc/pacman.conf` pendant les essais.

## Activer

```sh
mkdir -p ~/.config/environment.d
echo 'KWIN_DRM_SCALED_MODES=2,3' > ~/.config/environment.d/kwin-scaled.conf
```

Après redémarrage :

```sh
kscreen-doctor -o
kscreen-doctor output.DSI-1.mode.610x1356@120
kscreen-doctor output.DSI-1.scale.1.5    # même taille d'interface qu'en 1220×2712 ×3
```

Avec le mode 407×904, utiliser l'échelle 1.

## Revenir en arrière

```sh
sudo pacman -U /var/cache/pacman/pkg/kwin-6.8.2-1-aarch64.pkg.tar.zst
rm ~/.config/environment.d/kwin-scaled.conf
```
