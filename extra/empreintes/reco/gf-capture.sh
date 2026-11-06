#!/bin/bash
# Capture d'usine éclairée par le HBM local, pour gfreco.py.
#
#   ./gf-capture.sh SORTIE.bin [POSE]       (défaut : pose 50)
#
# Une capture AVEC le doigt, puis une SANS doigt (la référence) :
# les deux avec la même pose. À lancer en root ; ~/qseecom-cli doit exister.
set -e
OUT=${1:?usage: gf-capture.sh SORTIE.bin [POSE]}
POSE=${2:-50}
UH=$(getent passwd "${SUDO_USER:-$USER}" | cut -d: -f6)
CLI=${QSECOMCLI:-$UH/qseecom-cli}
HBM=$(ls /sys/devices/platform/*/*/local_hbm /sys/devices/platform/*/*/*/local_hbm 2>/dev/null | head -1)
[ -n "$HBM" ] || { echo "local_hbm introuvable (patch 0923 installé ?)" >&2; exit 1; }
gpioset -z -c gpiochip6 41=1          # reset du capteur (à refaire après chaque démarrage)
TMP=$(mktemp -d)
trap 'echo 0 > "$HBM"; rm -rf "$TMP"' EXIT
echo 1 > "$HBM"
sleep 0.3
GF_SET="1003/18@28=$POSE,1003/18@40=0x3f800000" GF_DUMP="$TMP/c" \
  "$CLI" gfdit --goodix-carveout 1002 9 32 1002 0 28 1002 1 44 1002 2 744 1002 6 480 1001 0 180 \
  1003 19 252 1003 18 44 1003 21 921640 < /dev/null > "$TMP/log" 2>&1 || { cat "$TMP/log"; exit 1; }
cp "$TMP"/c-1003-21-*.bin "$OUT"
chmod 600 "$OUT"
echo "écrit $OUT"
