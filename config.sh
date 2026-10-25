# alarm-diting — configuration (sourcée par scripts/lib.sh)
# Arch Linux ARM + Plasma Mobile pour le Xiaomi 12T Pro / Redmi K50 Ultra (diting)
# Base matérielle : https://github.com/royka1/diting-ubuntu (noyau, patches, firmware)

# --- Dépôt de référence (épinglé) ---------------------------------------------
DITING_UBUNTU_URL="https://github.com/royka1/diting-ubuntu.git"
DITING_UBUNTU_COMMIT="c68ff47db94792fb5fd608e98d1ae489c2e592b1"   # 2026-10-21

# --- Arch Linux ARM -------------------------------------------------------------
ALARM_TARBALL_URL="${ALARM_TARBALL_URL:-http://os.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz}"
# Miroir pacman utilisé pendant la construction (vide = celui du tarball)
ALARM_MIRROR="${ALARM_MIRROR:-}"

# --- Système ----------------------------------------------------------------------
HOSTNAME_DITING="${HOSTNAME_DITING:-diting}"
USERNAME_DITING="${USERNAME_DITING:-alarm}"
# Code PIN / mot de passe initial (à changer au premier démarrage : passwd)
PASSWORD_DITING="${PASSWORD_DITING:-147147}"
TIMEZONE_DITING="${TIMEZONE_DITING:-Indian/Antananarivo}"
LOCALE_DITING="${LOCALE_DITING:-fr_FR.UTF-8}"
KEYMAP_DITING="${KEYMAP_DITING:-fr}"

# Clé(s) SSH publique(s) à autoriser (facultatif ; SSH n'accepte que les clés) :
# déposez-les dans files/authorized_keys
SSH_PUBKEY_FILE="${SSH_PUBKEY_FILE:-files/authorized_keys}"

# Construire aussi libcamera patchée (caméras arrière). Long sous émulation.
BUILD_LIBCAMERA="${BUILD_LIBCAMERA:-1}"

# Marge libre ajoutée à l'image rootfs (le système de fichiers est ensuite
# agrandi à toute la partition userdata au premier démarrage)
ROOTFS_EXTRA_MB="${ROOTFS_EXTRA_MB:-1536}"

# Ligne de commande ajoutée au noyau (la racine est la partition userdata)
DITING_ROOT_CMDLINE="root=PARTLABEL=userdata rw rootwait"
