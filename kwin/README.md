# KWin — modes basse résolution agrandis par le DPU

Patch pour **KWin 6.8.2** qui ajoute à l'écran interne des modes « réduits »
(par ex. 610×1356 et 407×904), à 60, 90 et 120 Hz.

Dans ces modes, la dalle reçoit toujours ses timings natifs (1220×2712) ; KWin, les applis et
le bureau travaillent en basse résolution, puis l'image est agrandie vers la taille native :

- **facteur entier (÷2, ÷3)** : KWin agrandit lui-même l'image en *plus proche voisin* (GL_NEAREST),
  en une seule passe GPU plein écran — même netteté que sway (`scale_filter smart`) ;
- **autres facteurs**, ou `KWIN_DRM_SCALED_NEAREST=0` : agrandissement **matériel** par le DPU
  (SRC/CRTC des plans DRM). Le scaler QSEED du DPU est réglé en bilinéaire par le pilote mainline,
  d'où une image plus floue, surtout en ×3.

Objectif : la fluidité de la session imbriquée (sway + KWin en 407×904) sans ses
inconvénients (rotation, extinction de l'écran, précision du tactile, Firefox).

> **État : expérimental.** v1 (agrandissement DPU) testée sur le téléphone le 2026-11-02 :
> fonctionne, mais floue en ×3, réactivité proche du natif en ×2. v2 (plus proche voisin dans
> KWin) pas encore testée.

## Fonctionnement

| Fichier | Modification |
|---|---|
| `drm_connector.{h,cpp}` | `DrmConnectorMode` « mis à l'échelle » : taille réduite, timings natifs ; génération des modes selon `KWIN_DRM_SCALED_MODES` (écrans internes seulement) |
| `drm_pipeline.cpp` | `prepareAtomicPlane` : la destination (`CRTC_*`) du plan est agrandie vers la taille native ; si KWin a déjà agrandi la couche, le tampon natif est envoyé tel quel |
| `drm_egl_layer.cpp` | couche principale en mode réduit à facteur entier : tampon de sortie à la taille native, rendu de la scène à la taille réduite |
| `drm_egl_layer_surface.{h,cpp}` | réutilise le *shadow buffer* (gestion des couleurs) : scène rendue en petit, recopiée en plein écran avec `GL_NEAREST` |

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
