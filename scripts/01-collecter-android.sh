#!/bin/bash
# Étape 1 — À LANCER MAINTENANT, téléphone sous Android avec un root temporaire.
#
# Lecture seule : rien n'est écrit sur le téléphone. Récupère dans device-data/ :
#   - toutes les partitions brutes sauf userdata/super/cust (boot, vendor_boot,
#     dtbo, vbmeta, persist, modemst1/2, fsg, fsc, metadata, chaîne de boot…)
#     → sauvegarde pour revenir à Android ET pour l'IMEI/calibrations ;
#   - les firmwares propriétaires sous forme de fichiers (/vendor/firmware,
#     firmware_mnt, dsp, bt_firmware, /vendor/etc, persist…) ;
#   - les modules capteurs photo (CamX) pour les tables de registres caméra ;
#   - les journaux et l'arbre de périphériques Android (diagnostic).
# Avec --userdata : copie brute compressée de userdata (vos données, chiffrées).
#
# Utilise le script collect-android.sh de diting-ubuntu (même format, donc
# même constructeur de firmware que le projet Ubuntu).
#
#   scripts/01-collecter-android.sh [--userdata]
. "$(dirname "$0")/lib.sh"
need adb tar git sha256sum

with_userdata=0
case "${1:-}" in
--userdata) with_userdata=1 ;;
"") ;;
*) die "usage : $0 [--userdata]" ;;
esac

adb get-state >/dev/null 2>&1 || die "aucun téléphone visible par adb (débogage USB activé ?)"
[ "$(adb shell getprop ro.product.device | tr -d '\r')" = diting ] || die "ce n'est pas un diting (Xiaomi 12T Pro)"
adb shell su -c id 2>/dev/null | grep -q 'uid=0' ||
	die "pas de root : démarrez le boot.img patché par Magisk (fastboot boot) et autorisez « Shell » dans Magisk"

fetch_reference
mkdir -p "$DEVICE_DATA"
chmod 700 "$DEVICE_DATA"

{
	echo "modele=$(adb shell getprop ro.product.model | tr -d '\r')"
	echo "version=$(adb shell getprop ro.build.version.incremental | tr -d '\r')"
	echo "slot=$(adb shell getprop ro.boot.slot_suffix | tr -d '\r')"
	echo "date=$(date -u +%FT%TZ)"
} > "$DEVICE_DATA/TELEPHONE.txt"
cat "$DEVICE_DATA/TELEPHONE.txt" >&2

log "collecte (partitions, firmwares, journaux) — plusieurs minutes"
DITING_NOTHP=0 bash "$REF/ubuntu-diting/scripts/collect-android.sh"

log "modules capteurs photo (/vendor/lib64/camera)"
mkdir -p "$DEVICE_DATA/camera"
adb exec-out "su -c 'cd /vendor/lib64/camera && tar -cf - com.qti.sensormodule.*.bin'" |
	tar -x -C "$DEVICE_DATA/camera"
compgen -G "$DEVICE_DATA/camera/*s5khp1*" >/dev/null ||
	warn "pas de module S5KHP1 : votre unité a peut-être le capteur HM6 (caméra principale non prise en charge)"

if [ "$with_userdata" = 1 ]; then
	need zstd
	log "copie brute de userdata (peut prendre plus d'une heure)"
	adb exec-out "su -c 'cat /dev/block/by-name/userdata'" | zstd -T0 -3 > "$DEVICE_DATA/partitions/userdata.img.zst.part"
	mv "$DEVICE_DATA/partitions/userdata.img.zst.part" "$DEVICE_DATA/partitions/userdata.img.zst"
	(cd "$DEVICE_DATA/partitions" && sha256sum userdata.img.zst > userdata.img.zst.sha256)
fi

log "vérification des fichiers attendus par le constructeur de firmware"
for t in vendor_firmware vendor_firmware_mnt vendor_dsp vendor_bt_firmware vendor_etc mnt_vendor_persist; do
	[ -s "$DEVICE_DATA/firmware/$t.tar" ] || die "manquant : firmware/$t.tar"
done
for p in boot vendor_boot dtbo; do
	slot="$(sed -n 's/^slot=//p' "$DEVICE_DATA/TELEPHONE.txt")"
	[ -s "$DEVICE_DATA/partitions/$p$slot.img" ] || die "manquant : partitions/$p$slot.img"
done
for p in persist modemst1 modemst2 fsg metadata; do
	[ -s "$DEVICE_DATA/partitions/$p.img" ] || warn "partition $p non sauvegardée"
done

log "terminé : $DEVICE_DATA ($(du -sh "$DEVICE_DATA" | cut -f1))"
warn "ce dossier est PRIVÉ (IMEI, adresses MAC, firmware signé) : gardez-en une copie hors du PC, ne le publiez jamais"
