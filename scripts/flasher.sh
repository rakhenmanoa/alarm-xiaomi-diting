#!/bin/sh
# Installe Arch Linux ARM sur le Xiaomi 12T Pro (diting) depuis fastboot.
#
#   ./flasher.sh [--yes]
#
# À lancer dans le dossier contenant boot.img, vendor_boot.img et rootfs.img,
# téléphone en mode fastboot (power + volume bas), bootloader déverrouillé.
# Sur le slot actif :
#   boot_<slot>         <- boot.img         (noyau Linux)
#   vendor_boot_<slot>  <- vendor_boot.img  (DTB, initramfs)
#   dtbo_<slot>         effacé              (les overlays Android cassent le DTB mainline)
#   userdata            <- rootfs.img       (EFFACE TOUTES LES DONNÉES ANDROID)
# super, modem, persist, l'autre slot et le bootloader ne sont pas touchés.
# Retour à Android : scripts/restaurer-android.sh (sauvegardes de l'étape 1).
set -eu
cd "$(dirname "$0")"
for f in boot.img vendor_boot.img rootfs.img; do
	[ -f "$f" ] || { echo "manquant : $f"; exit 1; }
done
[ ! -f SHA256SUMS ] || sha256sum -c SHA256SUMS
command -v fastboot >/dev/null || { echo "fastboot introuvable (android-tools / platform-tools)"; exit 1; }

product="$(fastboot getvar product 2>&1 | awk '/^product:/ { print $2 }')"
[ "$product" = diting ] || { echo "l'appareil connecté est « $product », pas « diting »"; exit 1; }
unlocked="$(fastboot getvar unlocked 2>&1 | awk '/^unlocked:/ { print $2 }')"
[ "$unlocked" = yes ] || { echo "le bootloader n'est pas déverrouillé"; exit 1; }
slot="$(fastboot getvar current-slot 2>&1 | awk '/^current-slot:/ { print $2 }')"
case "$slot" in a|b) ;; *) echo "slot actif illisible ('$slot')"; exit 1 ;; esac

echo
echo "Xiaomi 12T Pro détecté, slot actif : $slot"
echo "Écriture de boot_$slot et vendor_boot_$slot, effacement de dtbo_$slot,"
echo "ÉCRASEMENT de userdata : toutes les données Android seront perdues."
if [ "${1:-}" != --yes ]; then
	printf 'Tapez « oui » pour continuer : '
	read -r r
	[ "$r" = oui ] || { echo "annulé"; exit 1; }
fi
fastboot flash "boot_$slot" boot.img
fastboot flash "vendor_boot_$slot" vendor_boot.img
fastboot erase "dtbo_$slot"
fastboot flash userdata rootfs.img
fastboot reboot
echo
echo "Terminé. Premier démarrage : le système de fichiers s'agrandit, puis Plasma Mobile"
echo "s'ouvre. Code PIN initial : celui de config.sh (147147 par défaut) → changez-le (passwd)."
