# Libérer la RAM de Waydroid à la fermeture d'une app

Paquet `pkgbuilds/diting-waydroid-ram`. Remplace, dans le raccourci d'une app Waydroid choisie,
`waydroid app launch <paquet>` par `diting-waydroid-ram run <paquet>` : l'app est lancée, l'aide
root attend qu'elle n'ait plus de fenêtre dans Android (`dumpsys window windows`), puis écrit
`1G swappiness=200` dans `/sys/fs/cgroup/lxc.payload.waydroid/memory.reclaim`.

**Testé sur le téléphone le 2026-11-15** : détection de la fermeture et libération de la RAM fonctionnent.

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

## Dans les réglages Plasma Mobile

`pkgbuilds/plasma-mobile-diting` (patch 0002) ajoute dans Réglages > Intégration Android > Applications un
interrupteur « Libérer la RAM à la fermeture » par app ; il appelle `diting-waydroid-ram on/off <paquet>` et relit
`diting-waydroid-ram list`. Sans le paquet `diting-waydroid-ram`, l'interrupteur est masqué. **Testé sur le téléphone le 2026-11-15.**
L'écran de réglages garde l'ancienne page tant qu'on ne l'a pas rouverte : fermer puis rouvrir Réglages après l'installation.
L'avertissement `Cannot read property 'width' of null` (ligne `ListView.view.width`) vient de KDE, sans conséquence.
