#!/bin/bash
# Fonctions communes. À sourcer, pas à exécuter.
# shellcheck disable=SC2034
set -euo pipefail

TOP="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=../config.sh
. "$TOP/config.sh"

WORK="${WORK:-$TOP/work}"
OUT="${OUT:-$TOP/out}"
DEVICE_DATA="${DEVICE_DATA:-$TOP/device-data}"   # PRIVÉ : IMEI, calibrations, firmware signé
REF="$WORK/diting-ubuntu"                         # clone épinglé de royka1/diting-ubuntu
REPO="$WORK/repo"                                 # paquets pacman construits (*.pkg.tar.zst)
ROOT="$WORK/rootfs"                               # arborescence du rootfs aarch64

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*" >&2; }
warn() { printf '\033[1;33mATTENTION :\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31mERREUR :\033[0m %s\n' "$*" >&2; exit 1; }

need() {
	local t
	for t in "$@"; do
		command -v "$t" >/dev/null 2>&1 || die "outil manquant : $t"
	done
}

need_root() { [ "$(id -u)" = 0 ] || die "à lancer en root (ou via scripts/build-in-container.sh)"; }

# Clone (ou met à jour) diting-ubuntu au commit épinglé.
fetch_reference() {
	need git
	if [ ! -d "$REF/.git" ]; then
		log "clonage de diting-ubuntu"
		mkdir -p "$WORK"
		git clone -q "$DITING_UBUNTU_URL" "$REF"
	fi
	if [ "$(git -C "$REF" rev-parse HEAD)" != "$DITING_UBUNTU_COMMIT" ]; then
		git -C "$REF" fetch -q origin
		git -C "$REF" checkout -q "$DITING_UBUNTU_COMMIT"
	fi
	# les scripts de diting-ubuntu cherchent device-data/ à la racine de leur dépôt
	if [ ! -e "$REF/device-data" ]; then
		mkdir -p "$DEVICE_DATA"
		ln -s "$DEVICE_DATA" "$REF/device-data"
	fi
}

# Valeur d'une variable de diting-ubuntu/ubuntu-diting/config/{diting.conf,kernel.lock}
ref_var() {
	(
		set +u
		# shellcheck source=/dev/null
		. "$REF/ubuntu-diting/config/diting.conf"
		# shellcheck source=/dev/null
		. "$REF/ubuntu-diting/config/kernel.lock"
		printf '%s' "${!1}"
	)
}

# --- Arch Linux ARM ----------------------------------------------------------------

# Vérifie que l'hôte exécute les binaires aarch64 (qemu-user + binfmt, drapeau F).
check_binfmt() {
	[ "$(uname -m)" = aarch64 ] && return 0
	local f=/proc/sys/fs/binfmt_misc/qemu-aarch64
	[ -r "$f" ] && grep -q '^enabled' "$f" ||
		die "émulation aarch64 absente. Sur l'hôte : Arch → pacman -S qemu-user-static qemu-user-static-binfmt ;
       Debian/Ubuntu → apt install qemu-user-static binfmt-support"
	grep -q '^flags:.*F' "$f" ||
		die "binfmt qemu-aarch64 sans le drapeau F (fix-binary) : utilisez qemu-user-static(-binfmt)"
}

# Télécharge le tarball ALARM (mis en cache dans work/).
fetch_alarm_tarball() {
	local t="$WORK/ArchLinuxARM-aarch64-latest.tar.gz"
	mkdir -p "$WORK"
	if [ ! -s "$t" ] || [ -n "$(find "$t" -mtime +7 2>/dev/null)" ]; then
		log "téléchargement d'Arch Linux ARM aarch64"
		curl -fL --retry 3 -o "$t.part" "$ALARM_TARBALL_URL"
		curl -fsL -o "$t.md5" "$ALARM_TARBALL_URL.md5"
		[ "$(md5sum "$t.part" | cut -d' ' -f1)" = "$(awk '{print $1; exit}' "$t.md5")" ] ||
			die "somme MD5 du tarball ALARM incorrecte"
		mv "$t.part" "$t"
	fi
	echo "$t"
}

# Commande dans une racine aarch64.
ch() { local r="$1"; shift; arch-chroot "$r" /usr/bin/env LANG=C.UTF-8 "$@"; }

# Crée une racine ALARM fraîche, clés pacman initialisées, à jour.
alarm_prepare_root() {
	local r="$1" tarball
	tarball="$(fetch_alarm_tarball)"
	log "nouvelle racine ALARM : $r"
	if [ -d "$r" ]; then
		umount -R "$r" 2>/dev/null || true
		rm -rf "$r"
	fi
	mkdir -p "$r"
	bsdtar -xpf "$tarball" -C "$r"
	# arch-chroot exige que la racine soit un point de montage
	mount --bind "$r" "$r"
	if [ -n "$ALARM_MIRROR" ]; then
		echo "Server = $ALARM_MIRROR/\$arch/\$repo" > "$r/etc/pacman.d/mirrorlist"
	fi
	sed -i 's/^#\?ParallelDownloads.*/ParallelDownloads = 5/' "$r/etc/pacman.conf"
	sed -i 's/^CheckSpace/#CheckSpace/' "$r/etc/pacman.conf"
	ch "$r" pacman-key --init
	ch "$r" pacman-key --populate archlinuxarm
	ch "$r" pacman -Syu --noconfirm
}

alarm_release_root() {
	local r="$1"
	umount -R "$r" 2>/dev/null || true
}

# Déclare le dépôt local de paquets diting dans une racine (monté sur /diting-repo).
alarm_add_local_repo() {
	local r="$1"
	mkdir -p "$r/diting-repo" "$REPO"
	if [ ! -e "$REPO/diting.db.tar.gz" ]; then
		# dépôt vide mais valide
		tar -czf "$REPO/diting.db.tar.gz" -T /dev/null
		ln -sf diting.db.tar.gz "$REPO/diting.db"
	fi
	mountpoint -q "$r/diting-repo" || mount --bind "$REPO" "$r/diting-repo"
	if ! grep -q '^\[diting\]' "$r/etc/pacman.conf"; then
		# placé avant [core] : nos paquets ont priorité
		sed -i '/^\[core\]/i [diting]\nSigLevel = Optional TrustAll\nServer = file:///diting-repo\n' "$r/etc/pacman.conf"
	fi
}

alarm_remove_local_repo() {
	local r="$1"
	sed -i '/^\[diting\]/,/^Server = file:\/\/\/diting-repo/d' "$r/etc/pacman.conf"
	umount "$r/diting-repo" 2>/dev/null || true
	rmdir "$r/diting-repo" 2>/dev/null || true
}

# Le dépôt ALARM fournit-il ce paquet ?
alarm_has_pkg() { ch "$1" pacman -Si "$2" >/dev/null 2>&1; }

# Ajoute des paquets construits au dépôt local.
repo_add() {
	mkdir -p "$REPO"
	cp -f "$@" "$REPO/"
	local f names=()
	for f in "$@"; do names+=("$REPO/${f##*/}"); done
	repo-add -q -R "$REPO/diting.db.tar.gz" "${names[@]}"
}

# makepkg sur l'hôte (conteneur), en utilisateur « builder ».
#   host_makepkg <dossier PKGBUILD> [options makepkg…]  → affiche les paquets produits
host_makepkg() {
	local src="$1"; shift
	local name b
	name="$(basename "$src")"
	b="$WORK/build-host/$name"
	rm -rf "$b" && mkdir -p "$b" "$WORK/pkgs-host"
	cp -a "$src/." "$b/"
	mkdir -p "$WORK/sources"
	chown -R builder: "$b" "$WORK/pkgs-host" "$WORK/sources"
	(cd "$b" && sudo -u builder env PKGDEST="$WORK/pkgs-host" SRCDEST="$WORK/sources" BUILDDIR="$b" \
		makepkg -f --noconfirm "$@" >&2)
	(cd "$b" && sudo -u builder env PKGDEST="$WORK/pkgs-host" makepkg --packagelist "$@" 2>/dev/null)
}
