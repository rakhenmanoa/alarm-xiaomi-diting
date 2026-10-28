#!/bin/bash
# Étape 3 — Construit les paquets pacman propres au diting dans work/repo :
#   linux-diting (en croisé, rapide), puis sous émulation aarch64 :
#   qrtr / rmtfs / tqftpserv / mkbootimg-aosp (seulement si ALARM ne les a pas),
#   hexagonrpc-diting, fastrpc-diting, libssc, iio-sensor-proxy-diting, diting-lever-reveil,
#   libva-v4l2-stateful-diting, libcamera-diting (option), diting-device.
#
# À lancer via : scripts/build-in-container.sh scripts/20-construire-paquets.sh
# Variables : FORCE=1 reconstruit tout ; ONLY="paquet …" limite la liste.
. "$(dirname "$0")/lib.sh"
need_root
need makepkg repo-add arch-chroot bsdtar curl
check_binfmt

FORCE="${FORCE:-0}"
ONLY="${ONLY:-}"
PB="$TOP/pkgbuilds"
mkdir -p "$REPO"

wanted() { [ -z "$ONLY" ] || [[ " $ONLY " == *" $1 "* ]]; }
in_repo() { # nom version-release
	[ "$FORCE" != 1 ] && ls "$REPO/$1-$2-"*.pkg.tar.* >/dev/null 2>&1
}
pkg_verrel() { # dossier → pkgver-pkgrel (valeurs statiques du PKGBUILD)
	(
		set +u
		_commit='' _tag='' _kver=''
		# shellcheck source=/dev/null
		. "$1/PKGBUILD" >/dev/null 2>&1
		echo "$pkgver-$pkgrel"
	)
}

# --- 1. Noyau (croisé sur l'hôte) ------------------------------------------------
if wanted linux-diting; then
	if in_repo linux-diting "$(pkg_verrel "$PB/linux-diting")"; then
		log "linux-diting déjà construit"
	else
		log "construction du noyau linux-diting (en croisé, 15 à 60 min)"
		args=(--skippgpcheck)
		if [ "$(uname -m)" != aarch64 ]; then
			need aarch64-linux-gnu-gcc
			args+=(--config "$TOP/files/makepkg-cross-aarch64.conf" --nodeps)
		fi
		mapfile -t pkgs < <(host_makepkg "$PB/linux-diting" "${args[@]}")
		repo_add "${pkgs[@]}"
	fi
fi

# --- 2. Racine de construction aarch64 -------------------------------------------
BR="$WORK/buildroot"
trap 'alarm_release_root "$BR"' EXIT
alarm_prepare_root "$BR"
alarm_add_local_repo "$BR"
ch "$BR" pacman -Sy --noconfirm --needed base-devel git meson ninja
ch "$BR" sh -c 'id builder >/dev/null 2>&1 || useradd -m builder'
echo 'builder ALL=(ALL) NOPASSWD: ALL' > "$BR/etc/sudoers.d/builder"
sed -i "s/^#\?MAKEFLAGS=.*/MAKEFLAGS=\"-j$(nproc)\"/" "$BR/etc/makepkg.conf"
# un seul .pkg par paquet, sans paquet -debug
sed -i '/^OPTIONS=/ s/\([( ]\)debug\b/\1!debug/' "$BR/etc/makepkg.conf"

chroot_build() { # dossier PKGBUILD
	local src="$1" name
	name="$(basename "$src")"
	if ! wanted "$name"; then return 0; fi
	if in_repo "$name" "$(pkg_verrel "$src")"; then
		log "$name déjà construit"
		return 0
	fi
	log "construction de $name (sous émulation aarch64)"
	rm -rf "$BR/build/$name" "$BR/build/out"
	mkdir -p "$BR/build/out"
	cp -a "$src" "$BR/build/$name"
	ch "$BR" chown -R builder: /build
	ch "$BR" pacman -Sy --noconfirm >/dev/null
	ch "$BR" sudo -u builder sh -c "cd /build/$name && PKGDEST=/build/out makepkg -s -f --noconfirm --skippgpcheck" ||
		die "échec de la construction de $name"
	repo_add "$BR"/build/out/*.pkg.tar.*
}

# --- 3. Outils Qualcomm : seulement s'ils manquent dans ALARM ---------------------
for p in qrtr rmtfs tqftpserv; do
	if alarm_has_pkg "$BR" "$p"; then
		log "$p : fourni par ALARM"
	else
		chroot_build "$PB/$p"
	fi
done

ch "$BR" pacman -S --noconfirm --needed android-tools >/dev/null 2>&1 || true
if [ -x "$BR/usr/bin/mkbootimg" ]; then
	log "mkbootimg : fourni par android-tools"
else
	chroot_build "$PB/mkbootimg-aosp"
fi

# --- 4. Pilotes et services du diting --------------------------------------------
for p in hexagonrpc-diting fastrpc-diting libssc iio-sensor-proxy-diting diting-lever-reveil libva-v4l2-stateful-diting; do
	chroot_build "$PB/$p"
done

if [ "$BUILD_LIBCAMERA" = 1 ]; then
	v="$(ch "$BR" pacman -Si libcamera 2>/dev/null | awk -F': ' '/^Version/ {print $2; exit}')"
	case "$v" in
	""|0.7.*) chroot_build "$PB/libcamera-diting" ;;
	*) warn "libcamera d'ALARM en version $v (≠ 0.7.x) : patches caméra non appliqués, libcamera-diting ignoré" ;;
	esac
fi

chroot_build "$PB/diting-device"

log "paquets disponibles dans $REPO :"
ls "$REPO"/*.pkg.tar.* | sed 's|.*/|   |' >&2
