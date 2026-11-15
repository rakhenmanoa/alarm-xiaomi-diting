# Libérer la RAM de Waydroid à la fermeture d'une app

Paquet `pkgbuilds/diting-waydroid-ram`. Remplace, dans le raccourci d'une app Waydroid choisie,
`waydroid app launch <paquet>` par `diting-waydroid-ram run <paquet>` : l'app est lancée, l'aide
root attend qu'elle n'ait plus de fenêtre dans Android (`dumpsys window windows`), puis écrit
`1G swappiness=200` dans `/sys/fs/cgroup/lxc.payload.waydroid/memory.reclaim`.

**Non testé sur le téléphone** (logique testée avec un faux `waydroid` et un faux cgroup).

## Utilisation

    diting-waydroid-ram list                  # apps Waydroid, [x] = option active
    diting-waydroid-ram on  com.ovelin.guitartuna
    diting-waydroid-ram off com.ovelin.guitartuna
    systemctl --user enable --now diting-waydroid-ram.path   # repatche si Waydroid réécrit ses raccourcis

L'état est dans `~/.config/diting-waydroid-ram/apps`. `off` remet la commande d'origine.

## Si ça ne marche pas

Vérifier d'abord la détection de fenêtre (app ouverte, puis fermée) :

    sudo /usr/lib/diting/diting-waydroid-ram-helper check com.ovelin.guitartuna    # open / closed

Si `closed` alors que l'app est ouverte, la syntaxe de `waydroid shell dumpsys window windows` ou le format des
lignes `Window{… paquet/activité}` diffère : voir `ouverte()` dans `diting-waydroid-ram-helper`.
Libération immédiate à la main : `sudo /usr/lib/diting/diting-waydroid-ram-helper reclaim`.

L'aide est lancée par `sudo -n` : `/etc/sudoers.d/diting-waydroid-ram` l'autorise sans mot de passe au groupe
`wheel` seulement pour ce fichier (il lit les fenêtres et écrit `memory.reclaim`, rien d'autre). Valeur libérée :
variable `DITING_WAYDROID_RECLAIM` (défaut `1G swappiness=200`).

## Suite prévue

Option « Libérer la RAM à la fermeture » dans les réglages d'intégration Android de Plasma Mobile
(patch de `plasma-mobile-diting`), qui appellera `diting-waydroid-ram on/off`.
