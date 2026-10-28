#!/bin/bash
# Lance une étape de construction dans un conteneur Arch Linux (x86_64), quelle
# que soit la distribution de l'hôte. Docker ou Podman requis, ainsi que
# l'émulation aarch64 sur l'hôte (qemu-user-static + binfmt, drapeau F).
#
#   scripts/build-in-container.sh scripts/10-construire-firmware.sh
#   scripts/build-in-container.sh scripts/20-construire-paquets.sh
#   scripts/build-in-container.sh scripts/30-construire-rootfs.sh
#   scripts/build-in-container.sh tout     (les trois à la suite)
set -euo pipefail
TOP="$(cd "$(dirname "$0")/.." && pwd)"
IMG=alarm-diting-builder

if command -v podman >/dev/null; then E=podman
elif command -v docker >/dev/null; then E=docker
else echo "Docker ou Podman requis" >&2; exit 1; fi

if [ "$(uname -m)" != aarch64 ]; then
	f=/proc/sys/fs/binfmt_misc/qemu-aarch64
	if ! grep -qs '^enabled' "$f" || ! grep -qs '^flags:.*F' "$f"; then
		echo "Émulation aarch64 absente sur l'hôte (binfmt avec drapeau F)." >&2
		echo "  Arch : sudo pacman -S qemu-user-static qemu-user-static-binfmt" >&2
		echo "  Debian/Ubuntu : sudo apt install qemu-user-static binfmt-support" >&2
		exit 1
	fi
fi

if ! $E image inspect "$IMG" >/dev/null 2>&1 || [ "${REBUILD_IMAGE:-0}" = 1 ]; then
	$E build -t "$IMG" -f "$TOP/files/Containerfile" "$TOP/files"
fi

[ $# -ge 1 ] || { echo "usage : $0 <script> [args] | tout" >&2; exit 2; }
if [ "$1" = tout ]; then
	set -- bash -ec "scripts/10-construire-firmware.sh && scripts/20-construire-paquets.sh && scripts/30-construire-rootfs.sh"
fi

# --privileged : montages (arch-chroot, loop) ; le dépôt est monté sur /src
exec $E run --rm -it --privileged --network host \
	-v "$TOP:/src" -w /src \
	-e ALARM_MIRROR -e BUILD_LIBCAMERA -e PASSWORD_DITING -e USERNAME_DITING \
	-e TIMEZONE_DITING -e LOCALE_DITING -e SSH_PUBKEY_FILE -e MAKEFLAGS -e ONLY -e FORCE \
	"$IMG" "$@"
