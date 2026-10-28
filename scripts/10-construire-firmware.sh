#!/bin/bash
# Étape 2 — Assemble le paquet firmware-xiaomi-diting à partir de device-data/
# (sauvegardes de l'étape 1), avec le constructeur de diting-ubuntu, puis le
# reconditionne en paquet pacman.
#
# À lancer via : scripts/build-in-container.sh scripts/10-construire-firmware.sh
. "$(dirname "$0")/lib.sh"
need_root
need git gcc m4 alsatplg dpkg-deb python3 makepkg

for t in vendor_firmware vendor_firmware_mnt vendor_dsp vendor_bt_firmware vendor_etc mnt_vendor_persist; do
	[ -s "$DEVICE_DATA/firmware/$t.tar" ] || die "manquant : device-data/firmware/$t.tar (étape 1 : 01-collecter-android.sh)"
done

fetch_reference
refs="$REF/references"
mkdir -p "$refs"
clone_at() { # nom url commit
	[ -d "$refs/$1/.git" ] || git clone -q "$2" "$refs/$1"
	git -C "$refs/$1" checkout -q "$3" 2>/dev/null || { git -C "$refs/$1" fetch -q origin; git -C "$refs/$1" checkout -q "$3"; }
}
clone_at audioreach-topology "$(ref_var AUDIOREACH_TOPOLOGY_URL)" "$(ref_var AUDIOREACH_TOPOLOGY_COMMIT)"
clone_at ar-audioreach-graphservices "$(ref_var AUDIOREACH_GRAPHSERVICES_URL)" "$(ref_var AUDIOREACH_GRAPHSERVICES_COMMIT)"

log "compilation d'acdb-query (lecture de la base ACDB audio)"
mkdir -p "$WORK/acdb"
bash "$REF/tools/acdb/build.sh" "$WORK/acdb" > "$WORK/acdb/build.log" 2>&1 ||
	{ tail -20 "$WORK/acdb/build.log"; die "échec de la compilation d'acdb-query"; }

log "assemblage du firmware (modem, DSP, GPU, Wi-Fi/BT, audio, capteurs, caméra)"
out="$WORK/firmware-deb"
rm -rf "$out" && mkdir -p "$out"
CAM="$DEVICE_DATA/camera"
[ -d "$CAM" ] || warn "pas de device-data/camera : tables de registres caméra absentes"
FW_SRC="$DEVICE_DATA/firmware" CAM_SRC="$CAM" \
	ACDB_QUERY="$WORK/acdb/acdb-query" ART_DIR="$refs/audioreach-topology" \
	FIRMWARE_MTIME="$(ref_var KERNEL_RELEASE_DATE)" \
	bash "$REF/tools/firmware/diting-build-firmware" "$out"
deb="$(ls "$out"/firmware-xiaomi-diting_*_all.deb)"

log "paquet pacman"
pb="$WORK/pkgbuild-firmware"
rm -rf "$pb" && cp -a "$TOP/pkgbuilds/firmware-xiaomi-diting" "$pb"
cp "$deb" "$pb/firmware-xiaomi-diting.deb"

# Tables de registres des caméras que le constructeur de diting-ubuntu ne
# convertit pas encore (frontale IMX596, macro GC02M1 aux adresses 8 bits),
# avec le convertisseur de tools/camera ; installées par le PKGBUILD.
mkdir -p "$pb/camregs"
for c in "com.qti.sensormodule.diting_sunny_imx596_front.bin:imx596-front.camregs" \
         "com.qti.sensormodule.diting_ofilm_gc02m1_macro.bin:gc02m1-macro.camregs"; do
	module="${c%%:*}" out="${c##*:}"
	if [ ! -f "$CAM/$module" ]; then
		warn "module caméra absent : $module ($out non généré)"
		continue
	fi
	python3 -I -B "$TOP/tools/camera/chromatix-to-camregs.py" "$CAM/$module" "$pb/camregs/$out" >&2 ||
		die "échec de la conversion de $module"
done
ver="$(dpkg-deb -f "$deb" Version)"
sed -i "s/^pkgver=.*/pkgver=${ver//-/_}/" "$pb/PKGBUILD"
mapfile -t pkgs < <(host_makepkg "$pb")
[ -s "${pkgs[0]}" ] || die "makepkg n'a rien produit"

# Le registre capteurs relit (et réécrit, ce qui plante le SLPI) tout fichier
# de configuration dont la date diffère : les dates du vendor doivent survivre.
log "contrôle des dates de la configuration capteurs"
d1="$(dpkg-deb --fsys-tarfile "$deb" | tar -tv --full-time 2>/dev/null | awk '/sensors\/config\/.+json/ {print $4, $5, $6; exit}')"
d2="$(bsdtar -tvf "${pkgs[0]}" 2>/dev/null | grep -m1 'sensors/config/.*json' || true)"
[ -n "$d1" ] && log "  .deb : $d1"
[ -n "$d2" ] && log "  .pkg : $d2"

repo_add "${pkgs[@]}"
log "firmware prêt : ${pkgs[*]##*/} (PRIVÉ, ne pas redistribuer)"
