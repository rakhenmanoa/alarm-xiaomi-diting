# Profils de consommation (platform profile)

Pilote `diting-platform-profile` : expose `low-power`, `balanced` et `performance` dans
`/sys/class/platform-profile/` et `/sys/firmware/acpi/platform_profile`, ce que lisent
power-profiles-daemon et Plasma. Chaque profil règle par QoS le min/max des 3 groupes de cœurs
(policy0, policy4, policy7) et du GPU (`3d00000.gpu`), sans écraser le gouverneur ni les réglages sysfs.

Le pilote est intégré au noyau par des patches de `pkgbuilds/linux-diting/` :

| Patch | Rôle |
|---|---|
| `0931` | rend `ACPI_PLATFORM_PROFILE` sélectionnable |
| `0932` | fait fonctionner la classe sans ACPI (le diting démarre par device tree : `platform_profile` refusait de se charger, `Operation not supported`) ; crée `/sys/firmware/acpi` lui-même |
| `0933` | le pilote lui-même, `drivers/soc/qcom/diting-platform-profile.c` (`CONFIG_QCOM_DITING_PLATFORM_PROFILE=m`) |

Le paquet `diting-device` (pkgrel 5) pose `/usr/lib/modules-load.d/diting-platform-profile.conf` : le module
se charge au démarrage. Il remplace l'ancien `diting-perf.service` : `balanced` en reprend les valeurs.
Le `rate_limit_us` de schedutil (500 µs) n'est pas repris.

**Non testé en paquet** : testé sur le téléphone en module compilé à la main (2026-11-15, noyau pkgrel 38) :
les trois profils changent bien les limites CPU et GPU, et `powerprofilesctl` les voit. Par SSH, polkit
refuse `powerprofilesctl set` (session non locale) : utiliser `sudo`.

## Valeurs des profils

Dans `profiles[]` du fichier du pilote (kHz). Paliers GPU (MHz) : 220 285 324 364 439 515 580 645 710 765
815 862 900. `low-power` plafonne le GPU à 515 MHz ; `balanced` = ancien `diting-perf` ; `performance`
relève les minima. Mesure au repos le 2026-11-15 : pas de différence de consommation entre `low-power` et
`balanced` (environ 165 mA) ; l'écart se verra sous charge. L'économie d'énergie Wi-Fi, elle, fait gagner
environ 40 mA (156 → 114 mA).

## Modifier le pilote sans reconstruire le noyau

Dans l'arbre de `docs/modules-sur-le-telephone.md`, avec les patches 0931 à 0933 appliqués :

    cd ~/linux-7.2.10
    scripts/config --module ACPI_PLATFORM_PROFILE --module QCOM_DITING_PLATFORM_PROFILE
    make LOCALVERSION= olddefconfig
    make LOCALVERSION= KBUILD_MODPOST_WARN=1 drivers/acpi/platform_profile.ko drivers/soc/qcom/diting-platform-profile.ko
    sudo install -Dm644 -t /usr/lib/modules/$(uname -r)/updates/ drivers/acpi/platform_profile.ko drivers/soc/qcom/diting-platform-profile.ko
    sudo depmod -a

Pour ne plus utiliser ces copies une fois le paquet installé : supprimer les deux fichiers de `updates/`, puis `depmod -a`.

## Tuile des réglages rapides (Plasma Mobile)

La tuile « Profil d'énergie » du tiroir des réglages rapides est fournie par `pkgbuilds/plasma-mobile-diting`
(patch 0003, sources dans `quicksettings/platformprofile` de Plasma Mobile). Un appui passe de `power-saver` à
`balanced` puis `performance` ; elle est allumée dès qu'on quitte `balanced`. Icônes de KDE (feuille, compteur,
éclair). Elle passe par `powerprofilesctl` (power-profiles-daemon), donc par le pilote ci-dessus ; le profil est
relu toutes les 10 s pour suivre les changements faits ailleurs. Elle remplace l'ancien paquet
`plasma-quicksetting-platformprofile-diting` (retiré du dépôt ; `replaces`/`conflicts` dans le PKGBUILD).

**Testée sur le téléphone le 2026-11-15** (la tuile s'affiche et change le profil). Installation puis rechargement du shell :

    cd pkgbuilds/plasma-mobile-diting && makepkg -si
    kquitapp6 plasmashell; plasmashell --replace &   # ou se reconnecter

La tuile s'ajoute en fin de liste des tuiles actives (éditeur du tiroir pour la déplacer). Si elle n'apparaît pas :
`powerprofilesctl get` doit répondre dans la session du téléphone, et `journalctl --user -b | grep -i platformprofile`
montre l'erreur QML. Si `org.kde.plasma.plasma5support` manque, installer le paquet `plasma5support`.
