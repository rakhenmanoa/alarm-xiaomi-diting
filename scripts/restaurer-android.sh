#!/bin/bash
# Retour à Android tel qu'il était, à partir des sauvegardes de l'étape 1
# (device-data/partitions). Téléphone en mode fastboot.
#
#   scripts/restaurer-android.sh            boot, vendor_boot, dtbo + userdata vierge
#   scripts/restaurer-android.sh --donnees  idem, puis userdata (+ metadata) d'origine
. "$(dirname "$0")/lib.sh"
need fastboot
P="$DEVICE_DATA/partitions"
slot="$(sed -n 's/^slot=_\?//p' "$DEVICE_DATA/TELEPHONE.txt" 2>/dev/null)"
cur="$(fastboot getvar current-slot 2>&1 | awk '/^current-slot:/ { print $2 }')"
[ -n "$slot" ] || slot="$cur"
[ "$slot" = "$cur" ] || warn "slot sauvegardé ($slot) ≠ slot actif ($cur) : restauration sur $slot puis activation"
for p in boot vendor_boot dtbo; do
	[ -s "$P/${p}_$slot.img" ] || die "sauvegarde manquante : $P/${p}_$slot.img"
done
[ "$(fastboot getvar product 2>&1 | awk '/^product:/ { print $2 }')" = diting ] || die "pas de diting en fastboot"

donnees=0
[ "${1:-}" = --donnees ] && donnees=1
if [ "$donnees" = 1 ]; then
	[ -s "$P/userdata.img.zst" ] || die "pas de sauvegarde userdata (01-collecter-android.sh --userdata)"
	[ -s "$P/metadata.img" ] || die "pas de sauvegarde metadata"
fi

echo "Restauration d'Android sur le slot $slot (Linux sera effacé). Tapez « oui » :"
read -r r; [ "$r" = oui ] || die "annulé"

for p in boot vendor_boot dtbo; do
	fastboot flash "${p}_$slot" "$P/${p}_$slot.img"
done
[ "$slot" = "$cur" ] || fastboot --set-active="$slot"

if [ "$donnees" = 1 ]; then
	log "décompression de userdata"
	zstd -d -f "$P/userdata.img.zst" -o "$WORK/userdata.img"
	fastboot flash metadata "$P/metadata.img"
	fastboot flash userdata "$WORK/userdata.img"
	rm -f "$WORK/userdata.img"
else
	# données Android vierges (comme après une réinitialisation)
	fastboot erase metadata || true
	fastboot -w
fi
fastboot reboot
log "Android redémarre. En cas d'échec avec --donnees : relancez sans l'option (données vierges)."
