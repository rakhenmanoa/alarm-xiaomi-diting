# Profils de consommation (platform profile)

Module `diting-platform-profile` : expose `low-power`, `balanced` et `performance` dans
`/sys/class/platform-profile/`, ce que lisent power-profiles-daemon et Plasma. Chaque profil règle
par QoS le min/max des 3 groupes de cœurs (policy0, policy4, policy7) et du GPU (`3d00000.gpu`),
sans écraser le gouverneur ni les réglages sysfs.

**Non testé** : écrit d'après le noyau principal (v7.2), jamais compilé sur le téléphone. Les valeurs
de `low-power` et `performance`, et le plafond GPU, sont des points de départ à régler après mesure.

## Prérequis noyau

`CONFIG_ACPI_PLATFORM_PROFILE` n'a pas de ligne de menu dans le noyau : le patch
`pkgbuilds/linux-diting/0931-…` lui en ajoute une, et le PKGBUILD l'active en module. Le patch `0932` fait aussi fonctionner la classe sans ACPI : sur le diting, démarré par device tree, `platform_profile` refusait de se charger (`Operation not supported`) car `acpi_disabled` est vrai. Il crée `/sys/firmware/acpi` lui-même pour le fichier historique lu par power-profiles-daemon (pkgrel 39).

## Compiler et charger (sur le téléphone, noyau pkgrel 38 ou 39 installé ; avec le 38, recompiler d'abord `platform_profile.ko` avec le patch 0932 (voir ci-dessous))

    cd extra/platform-profile
    make KDIR=/lib/modules/$(uname -r)/build
    sudo install -Dm644 diting-platform-profile.ko /usr/lib/modules/$(uname -r)/updates/
    sudo depmod -a
    sudo modprobe diting-platform-profile
    cat /sys/class/platform-profile/*/name
    echo low-power | sudo tee /sys/firmware/acpi/platform_profile

`balanced` reprend les valeurs de `diting-perf.service` : une fois le module validé, désactiver ce
service (`systemctl disable diting-perf`) pour que les minima ne soient plus fixés deux fois.

## Sans reconstruire tout le noyau (noyau pkgrel 38)

    cd ~/linux-7.2.10
    patch -Np1 < ~/alarm-xiaomi-diting/pkgbuilds/linux-diting/0932-ACPI-platform_profile-Work-with-ACPI-disabled.patch
    make LOCALVERSION= KBUILD_MODPOST_WARN=1 drivers/acpi/platform_profile.ko
    sudo install -Dm644 drivers/acpi/platform_profile.ko /usr/lib/modules/$(uname -r)/updates/platform_profile.ko
    sudo depmod -a
