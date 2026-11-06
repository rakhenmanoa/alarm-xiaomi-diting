#!/bin/bash
# Série de captures sans relancer la commande à chaque fois.
#
#   sudo ./gf-serie.sh -r ~/jeu/ref.bin [POSE]        référence, SANS doigt
#   sudo ./gf-serie.sh ~/jeu/index [N] [POSE]         N captures (défaut 10)
#
# Pour chaque capture : pose le doigt sur le disque, puis Entrée (un compte à
# rebours de 2 s laisse le temps de bien appuyer). « q » + Entrée termine.
# La numérotation reprend après le dernier fichier du dossier (01.bin, 02.bin…),
# on peut donc relancer pour compléter. À la fin, les fichiers sont rendus à
# l'utilisateur qui a lancé sudo.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
CAP=${CAPTURE:-$HERE/gf-capture.sh}
OWNER=${SUDO_USER:-$USER}

if [ "$1" = "-r" ]; then
  OUT=${2:?usage: gf-serie.sh -r REF.bin [POSE]}
  POSE=${3:-50}
  mkdir -p "$(dirname "$OUT")"
  read -r -p "Écran libre, AUCUN doigt, puis Entrée : " _
  "$CAP" "$OUT" "$POSE"
  chown "$OWNER" "$OUT" "$(dirname "$OUT")"
  exit 0
fi

DIR=${1:?usage: gf-serie.sh DOSSIER [N] [POSE]}
N=${2:-10}
POSE=${3:-50}
mkdir -p "$DIR"
LAST=$(ls "$DIR" 2>/dev/null | sed -n 's/^\([0-9]\+\)\.bin$/\1/p' | sort -n | tail -1)
K=$((10#${LAST:-0}))
for ((i = 1; i <= N; i++)); do
  K=$((K + 1))
  F=$(printf '%s/%02d.bin' "$DIR" "$K")
  read -r -p "[$i/$N] Pose le doigt (varie un peu la position), Entrée — q pour finir : " A
  [ "$A" = "q" ] && break
  echo -n "  appuie… "; sleep 1; echo -n "1… "; sleep 1
  if "$CAP" "$F" "$POSE"; then
    chown "$OWNER" "$F"
  else
    echo "  échec, on réessaie ce numéro"; K=$((K - 1)); i=$((i - 1))
  fi
done
chown -R "$OWNER" "$DIR"
echo "terminé : $(ls "$DIR" | wc -l) fichiers dans $DIR"
