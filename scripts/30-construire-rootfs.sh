#!/bin/bash
# Étape 4 — Construit le système complet, prêt à flasher, dans out/ :
#   boot.img, vendor_boot.img  (noyau + initramfs + DTB, format Android v4)
#   rootfs.img                 (ext4 « diting-root », Arch Linux ARM + Plasma Mobile,
#                               pilotes et firmware de VOTRE téléphone inclus)
#
# À lancer via : scripts/build-in-container.sh scripts/30-construire-rootfs.sh
# L'image contient votre firmware et vos calibrations : NE PAS LA PARTAGER.
. "$(dirname "$0")/lib.sh"
need_root
need arch-chroot bsdtar curl mkfs.ext4 repo-add
check_binfmt

[ -s "$REPO/diting.db.tar.gz" ] || die "aucun paquet construit : lancez d'abord 20-construire-paquets.sh"
ls "$REPO"/linux-diting-*.pkg.tar.* >/dev/null 2>&1 || die "linux-diting absent du dépôt local"
ls "$REPO"/diting-device-*.pkg.tar.* >/dev/null 2>&1 || die "diting-device absent du dépôt local"
fw=1
ls "$REPO"/firmware-xiaomi-diting-*.pkg.tar.* >/dev/null 2>&1 || {
	fw=0
	warn "firmware-xiaomi-diting absent (étape 10) : pas de modem, d'audio ni d'accélération GPU"
}

# --- Paquets ----------------------------------------------------------------------
# Indispensables (l'échec d'un seul arrête la construction)
REQUIRED=(
	base sudo nano less e2fsprogs dosfstools usbutils
	mkinitcpio dtc python python-gobject
	networkmanager modemmanager libqmi bluez bluez-utils openssh
	pipewire pipewire-pulse pipewire-alsa wireplumber alsa-ucm-conf alsa-utils
	mesa upower polkit
	sddm plasma-mobile plasma-dialer callaudiod
)
# Facultatifs (installés s'ils existent dans ALARM)
OPTIONAL=(
	# interface et réglages
	plasma-settings plasma-nm plasma-pa bluedevil powerdevil kscreen
	maliit-keyboard xdg-desktop-portal-kde breeze breeze-gtk kde-gtk-config
	plasma-mobile-sounds qqc2-breeze-style
	# applications Plasma Mobile
	spacebar mmsd-tng qmlkonsole angelfish kclock kweather kalk koko
	kasts neochat kdeconnect discover packagekit-qt6 flatpak
	# multimédia, caméra, GPU
	vulkan-freedreno snapshot gst-plugin-pipewire pipewire-libcamera
	gstreamer-vaapi mpv libva-utils mesa-utils vulkan-tools v4l-utils
	# polices
	noto-fonts noto-fonts-emoji
	# confort
	htop bash-completion man-db wget geoclue
)

ROOTFS_SIZE_EXTRA="$ROOTFS_EXTRA_MB"
trap 'alarm_release_root "$ROOT"' EXIT
alarm_prepare_root "$ROOT"
alarm_add_local_repo "$ROOT"
ch "$ROOT" pacman -Sy --noconfirm

log "retrait du noyau générique d'ALARM"
for p in linux-aarch64 linux-aarch64-headers uboot-tools; do
	ch "$ROOT" pacman -Q "$p" >/dev/null 2>&1 && ch "$ROOT" pacman -Rdd --noconfirm "$p"
done
rm -rf "$ROOT"/boot/dtbs "$ROOT"/boot/Image* "$ROOT"/boot/initramfs-linux*.img

log "paquets du diting (noyau, pilotes, firmware)"
mapfile -t diting_pkgs < <(ch "$ROOT" pacman -Sl diting | awk '{print $2}')
extra=()
[[ " ${diting_pkgs[*]} " == *" mkbootimg-aosp "* ]] || extra+=(android-tools)
# firmware libre : paquets découpés d'Arch, sinon l'ancien paquet unique
if alarm_has_pkg "$ROOT" linux-firmware-qcom; then
	extra+=(linux-firmware-qcom)
	alarm_has_pkg "$ROOT" linux-firmware-atheros && extra+=(linux-firmware-atheros)
else
	extra+=(linux-firmware)
fi
ch "$ROOT" pacman -S --noconfirm --needed "${extra[@]}" "${diting_pkgs[@]}"

log "Plasma Mobile et services"
ch "$ROOT" pacman -S --noconfirm --needed "${REQUIRED[@]}"
avail=()
for p in "${OPTIONAL[@]}"; do
	[[ "$p" == \#* ]] && continue
	if alarm_has_pkg "$ROOT" "$p"; then avail+=("$p"); else warn "paquet facultatif indisponible : $p"; fi
done
ch "$ROOT" pacman -S --noconfirm --needed "${avail[@]}" ||
	warn "certains paquets facultatifs n'ont pas pu être installés"

# --- Configuration ---------------------------------------------------------------
log "configuration du système"
echo "$HOSTNAME_DITING" > "$ROOT/etc/hostname"
cat > "$ROOT/etc/hosts" <<EOF
127.0.0.1	localhost
::1		localhost
127.0.1.1	$HOSTNAME_DITING
EOF
ln -sf "/usr/share/zoneinfo/$TIMEZONE_DITING" "$ROOT/etc/localtime"
for l in "$LOCALE_DITING" en_US.UTF-8; do
	sed -i "s/^#\s*\($l UTF-8\)/\1/" "$ROOT/etc/locale.gen"
done
ch "$ROOT" locale-gen
echo "LANG=$LOCALE_DITING" > "$ROOT/etc/locale.conf"
echo "KEYMAP=$KEYMAP_DITING" > "$ROOT/etc/vconsole.conf"

# identifiant machine fixe : l'adresse Bluetooth (diting-bt-address) en dépend
rm -f "$ROOT/etc/machine-id"
ch "$ROOT" systemd-machine-id-setup

cat > "$ROOT/etc/fstab" <<'EOF'
# racine = partition userdata ; agrandie à toute la partition au 1er démarrage
LABEL=diting-root	/	ext4	defaults,noatime,x-systemd.growfs	0 1
EOF

U="$USERNAME_DITING"
if ! ch "$ROOT" id "$U" >/dev/null 2>&1; then
	ch "$ROOT" useradd -m -s /bin/bash "$U"
fi
for g in wheel audio video input rfkill network uucp; do
	ch "$ROOT" getent group "$g" >/dev/null && ch "$ROOT" usermod -aG "$g" "$U"
done
echo "$U:$PASSWORD_DITING" | ch "$ROOT" chpasswd
# compte par défaut d'ALARM (mot de passe « alarm ») : supprimé s'il n'est pas utilisé
if [ "$U" != alarm ] && ch "$ROOT" id alarm >/dev/null 2>&1; then
	ch "$ROOT" userdel -r alarm 2>/dev/null || true
fi
ch "$ROOT" passwd -l root >/dev/null
echo '%wheel ALL=(ALL:ALL) ALL' > "$ROOT/etc/sudoers.d/10-wheel"
chmod 440 "$ROOT/etc/sudoers.d/10-wheel"

mkdir -p "$ROOT/etc/ssh/sshd_config.d"
cat > "$ROOT/etc/ssh/sshd_config.d/10-diting.conf" <<'EOF'
# clés uniquement (le mot de passe est un code PIN)
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitRootLogin no
EOF
if [ -s "$TOP/$SSH_PUBKEY_FILE" ] || [ -s "$SSH_PUBKEY_FILE" ]; then
	k="$SSH_PUBKEY_FILE"; [ -s "$k" ] || k="$TOP/$SSH_PUBKEY_FILE"
	home="$(ch "$ROOT" getent passwd "$U" | cut -d: -f6)"
	install -d -m700 "$ROOT$home/.ssh"
	install -m600 "$k" "$ROOT$home/.ssh/authorized_keys"
	ch "$ROOT" chown -R "$U:" "$home/.ssh"
	log "clé SSH ajoutée pour $U"
fi

# SDDM : Wayland (KWin), connexion automatique dans Plasma Mobile
session="$(cd "$ROOT/usr/share/wayland-sessions" 2>/dev/null && ls ./*mobile*.desktop 2>/dev/null | head -1)"
session="${session#./}"
[ -n "$session" ] || die "session Plasma Mobile introuvable dans /usr/share/wayland-sessions"
mkdir -p "$ROOT/etc/sddm.conf.d"
cat > "$ROOT/etc/sddm.conf.d/10-diting.conf" <<EOF
[General]
DisplayServer=wayland
GreeterEnvironment=QT_WAYLAND_SHELL_INTEGRATION=layer-shell
InputMethod=

[Wayland]
CompositorCommand=kwin_wayland --drm --no-lockscreen --no-global-shortcuts --locale1 --inputmethod maliit-keyboard

[Autologin]
User=$U
Session=$session
EOF

log "services"
ch "$ROOT" systemctl disable systemd-networkd.service systemd-networkd-wait-online.service 2>/dev/null || true
for s in NetworkManager ModemManager bluetooth sddm sshd rmtfs tqftpserv systemd-timesyncd \
	diting-usb-gadget diting-boot-successful diting-modem-setup diting-voice-call \
	diting-speaker-protection diting-wifi-band-steer; do
	if [ -e "$ROOT/usr/lib/systemd/system/$s.service" ]; then
		ch "$ROOT" systemctl enable "$s.service"
	else
		warn "service absent : $s"
	fi
done
ch "$ROOT" systemctl set-default graphical.target

# Dépôt local conservé dans l'image (réinstallation, paquets *-diting)
alarm_remove_local_repo "$ROOT"
mkdir -p "$ROOT/var/lib/diting-repo"
cp -a "$REPO"/. "$ROOT/var/lib/diting-repo/"
sed -i '/^\[core\]/i [diting]\nSigLevel = Optional TrustAll\nServer = file:///var/lib/diting-repo\n' "$ROOT/etc/pacman.conf"
# sur le téléphone, le bac à sable de pacman fonctionne : le réactiver
sed -i '/^DisableSandbox$/d' "$ROOT/etc/pacman.conf"

# --- Images de démarrage ---------------------------------------------------------
log "initramfs et images boot/vendor_boot"
ch "$ROOT" sh -c '[ -e /boot/vmlinuz-linux-diting ] || cp /usr/lib/modules/*-diting/vmlinuz /boot/vmlinuz-linux-diting'
ch "$ROOT" mkinitcpio -p linux-diting
for i in boot vendor_boot; do
	[ -s "$ROOT/boot/diting/$i.img" ] || die "/boot/diting/$i.img n'a pas été produit (voir le hook diting-bootimg)"
done

ch "$ROOT" pacman -Scc --noconfirm >/dev/null
rm -rf "$ROOT"/var/cache/pacman/pkg/* "$ROOT"/var/log/pacman.log "$ROOT"/root/.bash_history

alarm_release_root "$ROOT"
trap - EXIT

# --- Image ext4 -------------------------------------------------------------------
mkdir -p "$OUT"
cp "$ROOT/boot/diting/boot.img" "$ROOT/boot/diting/vendor_boot.img" "$OUT/"
used="$(du -sxm "$ROOT" | cut -f1)"
size=$(( used + used / 10 + ROOTFS_SIZE_EXTRA ))
log "image rootfs : ${size} Mio (${used} Mio utilisés)"
rm -f "$OUT/rootfs.img"
mkfs.ext4 -q -F -L diting-root -b 4096 -d "$ROOT" "$OUT/rootfs.img" "${size}M"
e2fsck -fy "$OUT/rootfs.img" >/dev/null || true

cp "$TOP/scripts/flasher.sh" "$OUT/flasher.sh"
(cd "$OUT" && sha256sum boot.img vendor_boot.img rootfs.img > SHA256SUMS)
owner="$(stat -c %u:%g "$TOP")"
chown -R "$owner" "$OUT"

log "terminé :"
ls -lh "$OUT" >&2
[ "$fw" = 1 ] && warn "rootfs.img contient VOTRE firmware et vos calibrations : ne la partagez pas"
log "flash : téléphone en mode fastboot, puis  out/flasher.sh"
